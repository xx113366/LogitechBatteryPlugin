// diag.cpp - 底层 I/O 诊断：打印每一步的 Windows 错误码和原始报文
#include <windows.h>
#include <hidsdi.h>
#include <hidpi.h>
#include <cstdio>
#include <string>
#include <vector>

static void PrintErr(const char* what, DWORD ec) {
    wchar_t* msg = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, ec, 0, (LPWSTR)&msg, 0, nullptr);
    printf("  %s failed: %lu %ls\n", what, ec, msg ? msg : L"");
    if (msg) LocalFree(msg);
}

int main(int argc, char** argv) {
    SetConsoleOutputCP(CP_UTF8);
    if (argc < 2) { printf("usage: diag <device-path>\n"); return 1; }

    // 把 UTF-8 路径转为宽字符
    int need = MultiByteToWideChar(CP_UTF8, 0, argv[1], -1, nullptr, 0);
    std::wstring path(need, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, argv[1], -1, &path[0], need);
    while (!path.empty() && path.back() == L'\0') path.pop_back();

    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                           FILE_FLAG_OVERLAPPED, nullptr);
    if (h == INVALID_HANDLE_VALUE) { PrintErr("CreateFile(OVERLAPPED)", GetLastError()); return 1; }
    printf("opened (overlapped)\n");

    PHIDP_PREPARSED_DATA ppd = nullptr;
    if (HidD_GetPreparsedData(h, &ppd)) {
        HIDP_CAPS caps{};
        if (HidP_GetCaps(ppd, &caps) >= 0) {
            printf("caps: usagePage=%04X usage=%04X in=%d out=%d feat=%d\n",
                   caps.UsagePage, caps.Usage, caps.InputReportByteLength,
                   caps.OutputReportByteLength, caps.FeatureReportByteLength);
            DWORD ec = 0;
            if (!HidD_SetNumInputBuffers(h, 32)) ec = GetLastError();
            printf("SetNumInputBuffers: %s (%lu)\n", ec ? "fail" : "ok", ec);
        }
        HidD_FreePreparsedData(ppd);
    }

    int outLen = 7;
    if (argc >= 3) outLen = atoi(argv[2]);

    // 构造 ping 报文
    std::vector<BYTE> buf(outLen, 0);
    buf[0] = (outLen >= 20) ? 0x11 : 0x10;
    buf[1] = (BYTE)(argc >= 5 ? strtol(argv[4], nullptr, 0) : 0x01);   // device index
    buf[2] = 0x00;
    buf[3] = 0x11;   // function 1, swid 1
    buf[4] = 0x00;
    buf[5] = 0x00;
    buf[6] = (BYTE)(GetTickCount() & 0xFF);

    printf("write %d bytes: ", outLen);
    for (int i = 0; i < outLen; ++i) printf("%02X ", buf[i]);
    printf("\n");

    OVERLAPPED ov{};
    ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    BOOL ok = WriteFile(h, buf.data(), outLen, nullptr, &ov);
    DWORD ec = GetLastError();
    if (!ok) {
        printf("WriteFile returned FALSE, err=%lu\n", ec);
        if (ec == ERROR_IO_PENDING) {
            DWORD w = WaitForSingleObject(ov.hEvent, 500);
            printf("wait=%lu\n", w);
            DWORD got = 0;
            GetOverlappedResult(h, &ov, &got, FALSE);
            printf("overlapped result: %lu bytes\n", got);
        } else {
            PrintErr("WriteFile", ec);
            return 1;
        }
    } else {
        printf("WriteFile ok\n");
    }
    CloseHandle(ov.hEvent);

    // 读 10 次，打印所有收到的报文
    int inLen = outLen;
    if (argc >= 4) inLen = atoi(argv[3]);
    for (int i = 0; i < 10; ++i) {
        std::vector<BYTE> in(inLen, 0);
        OVERLAPPED rov{};
        rov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        BOOL r = ReadFile(h, in.data(), inLen, nullptr, &rov);
        DWORD e = GetLastError();
        DWORD got = 0;
        if (!r && e == ERROR_IO_PENDING) {
            DWORD w = WaitForSingleObject(rov.hEvent, 250);
            if (w == WAIT_OBJECT_0) GetOverlappedResult(h, &rov, &got, FALSE);
            else { CancelIoEx(h, &rov); WaitForSingleObject(rov.hEvent, 50); }
        } else if (r) {
            GetOverlappedResult(h, &rov, &got, FALSE);
        } else {
            printf("ReadFile err %lu\n", e);
        }
        CloseHandle(rov.hEvent);
        if (got) {
            printf("read[%d] %lu bytes: ", i, got);
            for (DWORD k = 0; k < got; ++k) printf("%02X ", in[k]);
            printf("\n");
        } else {
            printf("read[%d] timeout/empty\n", i);
        }
    }
    CloseHandle(h);
    return 0;
}
