// scan.cpp - 扫描所有 device index，尝试多种写入方式，找出接收器的应答通道
#include <windows.h>
#include <hidsdi.h>
#include <hidpi.h>
#include <cstdio>
#include <string>
#include <vector>

static std::wstring U8ToW(const char* s) {
    int need = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    std::wstring w(need, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s, -1, &w[0], need);
    while (!w.empty() && w.back() == L'\0') w.pop_back();
    return w;
}

struct Chan {
    HANDLE h = INVALID_HANDLE_VALUE;
    int outLen = 0, inLen = 0;
};

static bool ReadReport(HANDLE h, int len, BYTE* buf, DWORD timeout) {
    OVERLAPPED ov{};
    ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    BOOL r = ReadFile(h, buf, len, nullptr, &ov);
    DWORD got = 0;
    bool ok = false;
    if (!r && GetLastError() == ERROR_IO_PENDING) {
        if (WaitForSingleObject(ov.hEvent, timeout) == WAIT_OBJECT_0) {
            if (GetOverlappedResult(h, &ov, &got, FALSE) && got > 0) ok = true;
        } else {
            CancelIoEx(h, &ov);
            WaitForSingleObject(ov.hEvent, 30);
        }
    } else if (r && GetOverlappedResult(h, &ov, &got, FALSE) && got > 0) {
        ok = true;
    }
    CloseHandle(ov.hEvent);
    return ok;
}

static bool WriteReport(HANDLE h, const BYTE* buf, int len) {
    OVERLAPPED ov{};
    ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    BOOL ok = WriteFile(h, buf, len, nullptr, &ov);
    DWORD got = 0;
    bool result = false;
    if (!ok && GetLastError() == ERROR_IO_PENDING) {
        if (WaitForSingleObject(ov.hEvent, 500) == WAIT_OBJECT_0)
            result = GetOverlappedResult(h, &ov, &got, FALSE) != FALSE;
        else CancelIoEx(h, &ov);
    } else if (ok) {
        result = GetOverlappedResult(h, &ov, &got, FALSE) != FALSE;
    }
    CloseHandle(ov.hEvent);
    return result;
}

int main(int argc, char** argv) {
    SetConsoleOutputCP(CP_UTF8);
    if (argc < 4) { printf("usage: scan <path> <outLen> <inLen>\n"); return 1; }
    std::wstring path = U8ToW(argv[1]);
    int outLen = atoi(argv[2]);
    int inLen = atoi(argv[3]);

    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                           FILE_FLAG_OVERLAPPED, nullptr);
    if (h == INVALID_HANDLE_VALUE) { printf("open failed %lu\n", GetLastError()); return 1; }

    // 先看有没有滞留的数据
    BYTE tmp[64];
    int stale = 0;
    while (ReadReport(h, inLen, tmp, 30)) {
        printf("stale: ");
        for (int i = 0; i < inLen; ++i) printf("%02X ", tmp[i]);
        printf("\n");
        if (++stale > 20) break;
    }
    printf("stale count = %d\n", stale);

    for (int dev = 0; dev <= 0x0F; ++dev) {
        BYTE req[20] = {};
        req[0] = (outLen >= 20) ? 0x11 : 0x10;
        req[1] = (BYTE)dev;
        req[2] = 0x00;
        req[3] = 0x11;
        req[4] = 0x00;
        req[5] = 0x00;
        req[6] = (BYTE)(GetTickCount() & 0xFF);
        if (!WriteReport(h, req, outLen)) { printf("dev 0x%02X write failed\n", dev); continue; }
        BYTE rsp[64] = {};
        if (ReadReport(h, inLen, rsp, 120)) {
            printf("dev 0x%02X reply: ", dev);
            for (int i = 0; i < inLen; ++i) printf("%02X ", rsp[i]);
            printf("\n");
        } else {
            printf("dev 0x%02X no reply\n", dev);
        }
    }

    // devIdx 0xFF
    {
        BYTE req[20] = {};
        req[0] = (outLen >= 20) ? 0x11 : 0x10;
        req[1] = 0xFF;
        req[2] = 0x00;
        req[3] = 0x11;
        req[4] = 0x00;
        req[5] = 0x00;
        req[6] = (BYTE)(GetTickCount() & 0xFF);
        WriteReport(h, req, outLen);
        BYTE rsp[64] = {};
        if (ReadReport(h, inLen, rsp, 150)) {
            printf("dev 0xFF reply: ");
            for (int i = 0; i < inLen; ++i) printf("%02X ", rsp[i]);
            printf("\n");
        } else {
            printf("dev 0xFF no reply\n");
        }
    }

    CloseHandle(h);
    return 0;
}
