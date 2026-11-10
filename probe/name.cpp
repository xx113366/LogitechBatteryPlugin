// name.cpp - 专门调试 0x0005 DEVICE_NAME 的报文格式
#include "../src/hid_logitech.h"
#include <cstdio>

static void Dump(const char* tag, const BYTE* b, int len) {
    printf("%-22s", tag);
    for (int i = 0; i < len; ++i) printf("%02X ", b[i]);
    printf("  |");
    for (int i = 0; i < len; ++i) printf("%c", (b[i] >= 32 && b[i] < 127) ? b[i] : '.');
    printf("|\n");
}

static bool LongCall(logi::Device& dev, BYTE feat, BYTE func, BYTE p0, BYTE p1, BYTE* rsp) {
    BYTE req[20] = {};
    req[0] = 0x11;
    req[1] = (BYTE)dev.device_index;
    req[2] = feat;
    req[3] = (BYTE)((func << 4) | 0x01);
    req[4] = p0;
    req[5] = p1;
    return dev.ep->Request(req, 20, rsp, 20, 500);
}

int main() {
    SetConsoleOutputCP(CP_UTF8);
    auto ifaces = logi::EnumInterfaces();
    for (auto& iface : ifaces) {
        if (iface.ep->output_len() < 20) continue;
        logi::Device dev = iface;
        dev.device_index = 1;

        BYTE rsp[20] = {};
        if (!LongCall(dev, 0x00, 0x00, 0x00, 0x05, rsp)) continue;
        if (rsp[2] == 0xFF || rsp[2] == 0x8F) { printf("0x0005 unsupported\n"); continue; }
        BYTE idx = rsp[4];
        printf("DEVICE_NAME feature index = %d\n", idx);

        BYTE c[20] = {};
        if (LongCall(dev, idx, 0x00, 0x00, 0x00, c)) Dump("fn0 getCount:", c, 20);
        int len = c[4];
        printf("reported name length = %d\n", len);

        for (int off = 0; off < len && off < 48; off += 15) {
            BYTE f[20] = {};
            char tag[32];
            snprintf(tag, sizeof(tag), "fn1 off=%d:", off);
            if (LongCall(dev, idx, 0x01, (BYTE)off, 0x00, f)) Dump(tag, f, 20);
        }

        // 也试一下 0x0003 相关信息与 0x1004 详情
        BYTE u[20] = {};
        if (LongCall(dev, 0x00, 0x00, 0x10, 0x04, u) && u[2] != 0xFF) {
            BYTE uidx = u[4];
            printf("UNIFIED_BATTERY feature index = %d\n", uidx);
            BYTE caps[20] = {};
            if (LongCall(dev, uidx, 0x00, 0x00, 0x00, caps)) Dump("0x1004 fn0 caps:", caps, 20);
            BYTE st[20] = {};
            if (LongCall(dev, uidx, 0x01, 0x00, 0x00, st)) Dump("0x1004 fn1 status:", st, 20);
        }
    }
    return 0;
}
