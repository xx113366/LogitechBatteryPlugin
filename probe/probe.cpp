// probe.cpp - 协议探针：在真实硬件上验证 HID++ 报文
#include "../src/hid_logitech.h"

#include <cstdio>
#include <string>
#include <vector>

static std::string U8(const std::wstring& w) {
    if (w.empty()) return std::string();
    int need = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(need, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], need, nullptr, nullptr);
    return s;
}

static void Dump(const char* tag, const BYTE* b, int len) {
    printf("      %-24s", tag);
    for (int i = 0; i < len; ++i) printf("%02X ", b[i]);
    printf("\n");
}

// 发送长消息（feature/function/3 参数）
static bool LongCall(logi::Device& dev, BYTE feat, BYTE func, BYTE p0, BYTE p1, BYTE p2, BYTE* rsp) {
    BYTE req[20] = {};
    req[0] = 0x11;
    req[1] = (BYTE)dev.device_index;
    req[2] = feat;
    req[3] = (BYTE)((func << 4) | 0x01);
    req[4] = p0; req[5] = p1; req[6] = p2;
    return dev.ep->Request(req, 20, rsp, 20, 400);
}

static bool ShortCall(logi::Device& dev, BYTE reg, BYTE p0, BYTE p1, BYTE* rsp) {
    BYTE req[7] = {};
    req[0] = 0x10;
    req[1] = (BYTE)dev.device_index;
    req[2] = reg;
    req[3] = 0x00;
    req[4] = p0; req[5] = p1; req[6] = 0x00;
    return dev.ep->Request(req, 7, rsp, 20, 400);
}

static bool PingSlot(logi::Device& dev, int slot) {
    int len = (dev.ep->output_len() >= 20) ? 20 : 7;
    BYTE req[20] = {};
    req[0] = (len >= 20) ? 0x11 : 0x10;
    req[1] = (BYTE)slot;
    req[2] = 0x00;
    req[3] = 0x11;
    req[4] = 0x00;
    req[5] = 0x00;
    req[6] = (BYTE)(GetTickCount() & 0xFF);
    BYTE rsp[20] = {};
    if (!dev.ep->Request(req, len, rsp, 20, 400)) return false;
    return !(rsp[2] == 0xFF || rsp[2] == 0x8F) && rsp[1] == (BYTE)slot;
}

int main() {
    SetConsoleOutputCP(CP_UTF8);

    std::wstring diag;
    auto ifaces = logi::EnumInterfaces(&diag);
    printf("=== HID interfaces (VID 046D, vendor collection): %zu ===\n", ifaces.size());
    for (size_t i = 0; i < ifaces.size(); ++i) {
        auto& f = ifaces[i];
        printf("[%zu] pid=%04X  usagePage=%04X usage=%04X  feat=%d out=%d in=%d\n",
               i, f.pid, f.ep->usage_page(), f.ep->usage(),
               f.ep->feature_len(), f.ep->output_len(), f.ep->input_len());
        printf("     %s\n", U8(f.path).c_str());
        printf("     product=%s\n", U8(f.product).c_str());
    }

    for (size_t i = 0; i < ifaces.size(); ++i) {
        auto& iface = ifaces[i];
        printf("\n=== probe interface [%zu] pid=%04X ===\n", i, iface.pid);

        // ping 各 slot
        for (int slot = 1; slot <= 6; ++slot) {
            bool ok = PingSlot(iface, slot);
            printf("  slot %d ping: %s\n", slot, ok ? "OK" : "-");
        }
        bool direct = PingSlot(iface, 0xFF);
        printf("  slot 0xFF (direct) ping: %s\n", direct ? "OK" : "-");

        std::vector<int> active;
        for (int slot = 1; slot <= 6; ++slot)
            if (PingSlot(iface, slot)) active.push_back(slot);
        if (active.empty() && direct) active.push_back(0xFF);

        for (int slot : active) {
            logi::Device dev = iface;
            dev.device_index = slot;
            printf("  --- device index 0x%02X ---\n", slot);

            // 版本
            {
                BYTE req[7] = {0x10, (BYTE)slot, 0x00, 0x11, 0x00, 0x00, (BYTE)(GetTickCount() & 0xFF)};
                BYTE rsp[20] = {};
                if (iface.ep->Request(req, 7, rsp, 20, 300))
                    Dump("ping reply:", rsp, 7);
            }

            // IRoot.getFeature 逐个查询
            const unsigned short feats[] = {0x0000, 0x0003, 0x0005, 0x1000, 0x1001, 0x1004, 0x1005};
            for (unsigned short fid : feats) {
                BYTE rsp[20] = {};
                char tag[64];
                snprintf(tag, sizeof(tag), "getFeature(%04X):", fid);
                if (LongCall(dev, 0x00, 0x00, (BYTE)(fid >> 8), (BYTE)(fid & 0xFF), 0x00, rsp))
                    Dump(tag, rsp, 20);
                else
                    printf("      %-24s (no reply)\n", tag);
            }

            // 直接读 0x1000 / 0x1004 / 0x1001 的数据（用已知 feature index）
            struct { unsigned short fid; BYTE func; const char* name; } q[] = {
                {0x1000, 0x00, "0x1000 fn0 status:"},
                {0x1004, 0x01, "0x1004 fn1 status:"},
                {0x1001, 0x00, "0x1001 fn0 voltage:"},
            };
            for (auto& it : q) {
                BYTE rsp[20] = {};
                if (LongCall(dev, 0x00, 0x00, (BYTE)(it.fid >> 8), (BYTE)(it.fid & 0xFF), 0x00, rsp)
                    && (rsp[2] != 0xFF && rsp[2] != 0x8F)) {
                    BYTE idx = rsp[4];
                    if (idx) {
                        BYTE r2[20] = {};
                        if (LongCall(dev, idx, it.func, 0, 0, 0, r2))
                            Dump(it.name, r2, 20);
                    } else {
                        printf("      %-24s (feature unsupported)\n", it.name);
                    }
                }
            }

            // HID++ 1.0 寄存器 0x07
            {
                BYTE rsp[20] = {};
                if (ShortCall(dev, 0x07, 0x00, 0x00, rsp))
                    Dump("reg0x07:", rsp, 7);
                else
                    printf("      %-24s (no reply)\n", "reg0x07:");
            }

            // 用库的完整流程
            logi::BatteryInfo bi;
            if (logi::QueryBattery(dev, bi)) {
                printf("      --> library: percent=%d voltage=%d charging=%d source=%d\n",
                       bi.percent, bi.voltage_mv, (int)bi.charging, bi.source);
            } else {
                printf("      --> library: FAILED\n");
            }
            if (logi::QueryDeviceName(dev)) {
                printf("      --> name=%s\n", U8(dev.name).c_str());
            }
        }
    }
    return 0;
}
