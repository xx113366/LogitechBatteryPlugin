// hid_logitech.cpp - 罗技 HID++ 通信实现
#include "hid_logitech.h"

#include <setupapi.h>
#include <hidsdi.h>
#include <hidpi.h>

#include <algorithm>
#include <cstring>

#ifdef _MSC_VER
#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "hid.lib")
#endif

namespace logi {

    namespace {

        constexpr BYTE  kReportShort = 0x10;
        constexpr BYTE  kReportLong = 0x11;
        constexpr BYTE  kSoftwareId = 0x01;
        constexpr WORD  kLogitechVid = 0x046D;

        // HID++ feature ID
        constexpr unsigned short FEAT_ROOT = 0x0000;
        constexpr unsigned short FEAT_DEVICE_NAME = 0x0005;
        constexpr unsigned short FEAT_BATTERY_STATUS = 0x1000;
        constexpr unsigned short FEAT_BATTERY_VOLTAGE = 0x1001;
        constexpr unsigned short FEAT_UNIFIED_BATTERY = 0x1004;

        inline int Clamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

        // 判断一条应答是否属于该请求
        bool ReplyBelongsTo(const BYTE* req, const BYTE* rsp) {
            BYTE devnum = req[1];
            if (rsp[1] != devnum && rsp[1] != static_cast<BYTE>(devnum ^ 0xFF))
                return false;
            if (rsp[2] == 0xFF || rsp[2] == 0x8F)          // 错误应答
                return rsp[3] == req[2] && rsp[4] == req[3];
            return rsp[2] == req[2] && rsp[3] == req[3];
        }

        inline bool IsErrorReply(const BYTE* rsp) {
            return rsp[2] == 0xFF || rsp[2] == 0x8F;
        }

        inline bool IsSuccessReply(const BYTE* rsp) {
            return rsp[2] != 0xFF && rsp[2] != 0x8F;
        }

        // 根据电压估算电量（与 Solaar 相同的分段线性插值）
        int EstimateLevelFromVoltage(int mv) {
            static const int table[][2] = {
                {4186, 100}, {4067, 90}, {3989, 80}, {3922, 70}, {3859, 60},
                {3811, 50},  {3778, 40}, {3751, 30}, {3717, 20}, {3671, 10},
                {3646, 5},   {3579, 2},  {3500, 0}
            };
            constexpr int n = static_cast<int>(sizeof(table) / sizeof(table[0]));
            if (mv >= table[0][0]) return table[0][1];
            if (mv <= table[n - 1][0]) return table[n - 1][1];
            for (int i = 0; i < n - 1; ++i) {
                if (mv <= table[i][0] && mv >= table[i + 1][0]) {
                    int vh = table[i][0], ph = table[i][1];
                    int vl = table[i + 1][0], pl = table[i + 1][1];
                    // 线性插值：注意表是按电压降序排列
                    return pl + (ph - pl) * (mv - vl) / (vh - vl);
                }
            }
            return 0;
        }

        std::wstring ToWide(const std::string& s) {
            if (s.empty()) return std::wstring();
            int need = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
            if (need <= 0) return std::wstring();
            std::wstring out(need, L'\0');
            MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), &out[0], need);
            return out;
        }

    } // namespace

    // ---------------------------------------------------------------- Endpoint

    Endpoint::~Endpoint() {
        if (h_feature_ != INVALID_HANDLE_VALUE) CloseHandle(h_feature_);
        if (h_overlap_ != INVALID_HANDLE_VALUE) CloseHandle(h_overlap_);
    }

    bool Endpoint::Open(const std::wstring& path, std::wstring* diag) {
        path_ = path;
        h_feature_ = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h_feature_ == INVALID_HANDLE_VALUE) {
            if (diag) *diag = L"CreateFile failed";
            return false;
        }
        handle_ = h_feature_;

        PHIDP_PREPARSED_DATA ppd = nullptr;
        if (!HidD_GetPreparsedData(h_feature_, &ppd)) {
            if (diag) *diag = L"HidD_GetPreparsedData failed";
            return false;
        }
        HIDP_CAPS caps{};
        NTSTATUS st = HidP_GetCaps(ppd, &caps);
        HidD_FreePreparsedData(ppd);
        if (st < 0) {
            if (diag) *diag = L"HidP_GetCaps failed";
            return false;
        }
        usage_page_ = caps.UsagePage;
        usage_ = caps.Usage;
        feature_len_ = caps.FeatureReportByteLength;
        output_len_ = caps.OutputReportByteLength;
        input_len_ = caps.InputReportByteLength;
        strategy_ = 0;
        mode_name_ = "auto";
        return true;
    }

    bool Endpoint::RequestFeature(const BYTE* req, int req_len, BYTE* rsp, int rsp_len, DWORD timeout_ms) {
        if (feature_len_ < req_len) return false;
        BYTE out[64] = {};
        memcpy(out, req, req_len);
        out[0] = (req_len >= 20 ? kReportLong : kReportShort);
        if (!HidD_SetFeature(h_feature_, out, feature_len_))
            return false;

        DWORD deadline = GetTickCount() + timeout_ms;
        for (;;) {
            BYTE in[64] = {};
            if (!HidD_GetFeature(h_feature_, in, feature_len_))
                return false;
            in[0] = (feature_len_ >= 20 ? kReportLong : kReportShort);
            if (ReplyBelongsTo(req, in)) {
                memcpy(rsp, in, static_cast<size_t>(rsp_len < feature_len_ ? rsp_len : feature_len_));
                return true;
            }
            if (GetTickCount() >= deadline) return false;
            Sleep(2);
        }
    }

    bool Endpoint::EnsureOverlapped() {
        if (h_overlap_ != INVALID_HANDLE_VALUE) return true;
        if (path_.empty()) return false;
        h_overlap_ = CreateFileW(path_.c_str(), GENERIC_READ | GENERIC_WRITE,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                 FILE_FLAG_OVERLAPPED, nullptr);
        return h_overlap_ != INVALID_HANDLE_VALUE;
    }

    bool Endpoint::RequestWriteRead(const BYTE* req, int req_len, BYTE* rsp, int rsp_len, DWORD timeout_ms) {
        if (!EnsureOverlapped()) return false;
        if (output_len_ > 0 && output_len_ < req_len) return false;   // 该端点装不下这条报文
        int wlen = (output_len_ > req_len ? output_len_ : req_len);
        if (wlen > 64) return false;
        BYTE out[64] = {};
        memcpy(out, req, req_len);
        out[0] = (req_len >= 20 ? kReportLong : kReportShort);

        // 清掉输入队列里的旧报文
        for (int i = 0; i < 16; ++i) {
            OVERLAPPED ov{};
            ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            BYTE tmp[64] = {};
            BOOL r = ReadFile(h_overlap_, tmp, input_len_ > 0 ? input_len_ : 20, nullptr, &ov);
            if (!r && GetLastError() == ERROR_IO_PENDING) {
                if (WaitForSingleObject(ov.hEvent, 0) == WAIT_OBJECT_0) {
                    DWORD got = 0;
                    GetOverlappedResult(h_overlap_, &ov, &got, FALSE);
                } else {
                    CancelIoEx(h_overlap_, &ov);
                    WaitForSingleObject(ov.hEvent, 20);
                    CloseHandle(ov.hEvent);
                    break;
                }
            }
            CloseHandle(ov.hEvent);
            if (!r) break;
        }

        OVERLAPPED wov{};
        wov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        BOOL ok = WriteFile(h_overlap_, out, wlen, nullptr, &wov);
        if (!ok && GetLastError() != ERROR_IO_PENDING) {
            CloseHandle(wov.hEvent);
            return false;
        }
        if (!ok) {
            DWORD w = WaitForSingleObject(wov.hEvent, timeout_ms);
            if (w != WAIT_OBJECT_0) {
                CancelIoEx(h_overlap_, &wov);
                WaitForSingleObject(wov.hEvent, 20);
                CloseHandle(wov.hEvent);
                return false;
            }
        }
        CloseHandle(wov.hEvent);

        DWORD deadline = GetTickCount() + timeout_ms;
        for (;;) {
            OVERLAPPED rov{};
            rov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            BYTE in[64] = {};
            DWORD rd = input_len_ > 0 ? input_len_ : 20;
            if (rd > 64) rd = 64;
            BOOL r = ReadFile(h_overlap_, in, rd, nullptr, &rov);
            bool got = false;
            if (!r && GetLastError() == ERROR_IO_PENDING) {
                DWORD remain = (deadline > GetTickCount()) ? (deadline - GetTickCount()) : 0;
                if (WaitForSingleObject(rov.hEvent, remain) == WAIT_OBJECT_0) got = true;
                else CancelIoEx(h_overlap_, &rov);
            } else if (r) {
                got = true;
            }
            DWORD transferred = 0;
            if (got) {
                if (!GetOverlappedResult(h_overlap_, &rov, &transferred, FALSE))
                    transferred = 0;
            }
            CloseHandle(rov.hEvent);
            if (got && transferred >= 4) {
                in[0] = (rd >= 20 ? kReportLong : kReportShort);
                if (ReplyBelongsTo(req, in)) {
                    memcpy(rsp, in, static_cast<size_t>(rsp_len < rd ? rsp_len : rd));
                    return true;
                }
            }
            if (GetTickCount() >= deadline) return false;
        }
    }

    bool Endpoint::Request(const BYTE* req, int req_len, BYTE* rsp, int rsp_len, DWORD timeout_ms) {
        if (!ok()) return false;
        if (rsp_len < 7) return false;

        if (strategy_ != 0) {
            switch (strategy_) {
            case 1: return RequestFeature(req, req_len, rsp, rsp_len, timeout_ms);
            case 3: return RequestWriteRead(req, req_len, rsp, rsp_len, timeout_ms);
            default: return false;
            }
        }

        // 首次调用：依次尝试，成功后锁定策略
        // 1) HidD_SetFeature + HidD_GetFeature（USB 接收器最常见）
        if (RequestFeature(req, req_len, rsp, rsp_len, timeout_ms)) {
            strategy_ = 1;
            mode_name_ = "feature";
            return true;
        }
        // 2) WriteFile + ReadFile（蓝牙直连设备）
        if (RequestWriteRead(req, req_len, rsp, rsp_len, timeout_ms)) {
            strategy_ = 3;
            mode_name_ = "write/read";
            return true;
        }
        return false;
    }

    // ---------------------------------------------------------------- 枚举

    std::vector<Device> EnumInterfaces(std::wstring* diag) {
        std::vector<Device> list;
        GUID guid{};
        HidD_GetHidGuid(&guid);

        HDEVINFO set = SetupDiGetClassDevsW(&guid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
        if (set == INVALID_HANDLE_VALUE) {
            if (diag) *diag = L"SetupDiGetClassDevs failed";
            return list;
        }

        SP_DEVICE_INTERFACE_DATA did{};
        did.cbSize = sizeof(did);

        for (DWORD i = 0; SetupDiEnumDeviceInterfaces(set, nullptr, &guid, i, &did); ++i) {
            DWORD need = 0;
            SetupDiGetDeviceInterfaceDetailW(set, &did, nullptr, 0, &need, nullptr);
            if (need == 0) continue;

            std::vector<BYTE> buf(need);
            auto* detail = reinterpret_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA_W>(buf.data());
            detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
            if (!SetupDiGetDeviceInterfaceDetailW(set, &did, detail, need, nullptr, nullptr))
                continue;

            std::wstring path = detail->DevicePath;

            HANDLE h = CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   nullptr, OPEN_EXISTING, 0, nullptr);
            if (h == INVALID_HANDLE_VALUE) continue;

            HIDD_ATTRIBUTES attr{};
            attr.Size = sizeof(attr);
            bool got_attr = HidD_GetAttributes(h, &attr) != FALSE;

            wchar_t product[256] = {};
            HidD_GetProductString(h, product, sizeof(product));

            int up = 0, us = 0, feat = 0, outl = 0, inl = 0;
            PHIDP_PREPARSED_DATA ppd = nullptr;
            if (HidD_GetPreparsedData(h, &ppd)) {
                HIDP_CAPS caps{};
                if (HidP_GetCaps(ppd, &caps) >= 0) {
                    up = caps.UsagePage;
                    us = caps.Usage;
                    feat = caps.FeatureReportByteLength;
                    outl = caps.OutputReportByteLength;
                    inl = caps.InputReportByteLength;
                }
                HidD_FreePreparsedData(ppd);
            }
            CloseHandle(h);

            if (!got_attr || attr.VendorID != kLogitechVid) continue;
            if (feat < 7 && outl < 7) continue;          // 需要支持 7/20 字节报文
            if (up < 0xFF00) continue;                   // 只要厂商自定义集合（HID++ 端点）

            auto ep = std::make_shared<Endpoint>();
            std::wstring d;
            if (!ep->Open(path, &d)) continue;

            Device dev;
            dev.path = path;
            dev.product = product;
            dev.vid = attr.VendorID;
            dev.pid = attr.ProductID;
            dev.ep = ep;
            list.push_back(dev);
        }

        SetupDiDestroyDeviceInfoList(set);
        return list;
    }

    // ---------------------------------------------------------------- 设备操作

    namespace {

        bool SendLong(Device& dev, BYTE feat, BYTE func, BYTE p0, BYTE p1, BYTE p2, BYTE* rsp, DWORD timeout = 300) {
            if (!dev.ep || !dev.ep->ok()) return false;
            BYTE req[20] = {};
            req[0] = kReportLong;
            req[1] = static_cast<BYTE>(dev.device_index);
            req[2] = feat;
            req[3] = static_cast<BYTE>((func << 4) | kSoftwareId);
            req[4] = p0;
            req[5] = p1;
            req[6] = p2;
            return dev.ep->Request(req, 20, rsp, 20, timeout);
        }

        bool SendShort(Device& dev, BYTE reg, BYTE p0, BYTE p1, BYTE p2, BYTE* rsp, DWORD timeout = 300) {
            if (!dev.ep || !dev.ep->ok()) return false;
            BYTE req[7] = {};
            req[0] = kReportShort;
            req[1] = static_cast<BYTE>(dev.device_index);
            req[2] = reg;
            req[3] = 0x00;
            req[4] = p0;
            req[5] = p1;
            req[6] = p2;
            return dev.ep->Request(req, 7, rsp, 20, timeout);
        }

        // HID++ ping：确认某个 device index 上是否有设备在
        // 注意：很多设备只在其原生报文长度（7 或 20 字节）的通道上应答
        bool Ping(Device& dev, int devnum, DWORD timeout_ms) {
            if (!dev.ep || !dev.ep->ok()) return false;
            int len = (dev.ep->output_len() >= 20) ? 20 : 7;
            BYTE req[20] = {};
            req[0] = (len >= 20) ? kReportLong : kReportShort;
            req[1] = static_cast<BYTE>(devnum);
            req[2] = 0x00;
            req[3] = 0x10 | kSoftwareId;
            req[4] = 0x00;
            req[5] = 0x00;
            req[6] = static_cast<BYTE>(GetTickCount() & 0xFF);
            BYTE rsp[20] = {};
            if (!dev.ep->Request(req, len, rsp, 20, timeout_ms)) return false;
            return IsSuccessReply(rsp) && rsp[1] == static_cast<BYTE>(devnum);
        }

        // IRoot.getFeature → 返回 feature index（0 表示不支持，-1 表示无应答）
        int GetFeatureIndex(Device& dev, unsigned short feature_id, int* cache) {
            if (cache && *cache >= 0) return *cache;
            BYTE rsp[20] = {};
            int idx = -1;
            if (SendLong(dev, 0x00, 0x00, static_cast<BYTE>(feature_id >> 8),
                         static_cast<BYTE>(feature_id & 0xFF), 0x00, rsp)) {
                if (IsErrorReply(rsp)) idx = 0;
                else if (rsp[0] == kReportLong && rsp[4] != 0x00) idx = rsp[4];
                else idx = 0;
            }
            if (cache) *cache = idx;
            return idx;
        }

    } // namespace

    bool PingDevice(Device& dev, int devnum, DWORD timeout_ms) {
        return Ping(dev, devnum, timeout_ms);
    }

    void ScanReceiverDevices(Device& iface, std::vector<Device>& out, int max_slot) {
        if (!iface.ep || !iface.ep->ok()) return;
        std::vector<Device> found;
        for (int slot = 1; slot <= max_slot; ++slot) {
            Device d = iface;
            d.device_index = slot;
            d.is_receiver = true;
            if (Ping(d, slot, 300)) {
                // 记录每个设备的 feature 缓存（Device 是按值拷贝的，需要各自独立）
                d.feat_name = -1;
                d.feat_bat_status = -1;
                d.feat_bat_voltage = -1;
                d.feat_unified_battery = -1;
                d.name_queried = false;
                found.push_back(d);
            }
        }

        if (!found.empty()) {
            for (auto& d : found) out.push_back(d);
            return;
        }

        // 没有从设备应答，可能是直连（蓝牙 / USB 直连）设备：device index 0xFF
        Device d = iface;
        d.device_index = 0xFF;
        d.is_receiver = false;
        d.feat_name = -1;
        d.feat_bat_status = -1;
        d.feat_bat_voltage = -1;
        d.feat_unified_battery = -1;
        d.name_queried = false;
        if (Ping(d, 0xFF, 300)) out.push_back(d);
    }

    bool QueryDeviceName(Device& dev) {
        if (dev.name_queried) return !dev.name.empty();
        dev.name_queried = true;
        int idx = GetFeatureIndex(dev, FEAT_DEVICE_NAME, &dev.feat_name);
        if (idx <= 0) return false;

        BYTE rsp[20] = {};
        if (!SendLong(dev, static_cast<BYTE>(idx), 0x00, 0, 0, 0, rsp)) return false;
        if (IsErrorReply(rsp)) return false;
        int len = rsp[4];
        if (len <= 0 || len > 64) return false;

        std::string name;
        while (static_cast<int>(name.size()) < len) {
            BYTE frag[20] = {};
            if (!SendLong(dev, static_cast<BYTE>(idx), 0x01, static_cast<BYTE>(name.size()), 0, 0, frag))
                break;
            if (IsErrorReply(frag)) break;

            // 两种设备实现都见过：
            //   有长度前缀：frag[4] = 本块字符数，字符从 frag[5] 开始
            //   无长度前缀：字符直接从现在 frag[4] 开始（如 PRO X2 SUPERSTRIKE）
            const int start = (frag[4] <= 16) ? 5 : 4;
            std::string chunk;
            for (int k = start; k < 20; ++k) {
                char c = static_cast<char>(frag[k]);
                if (c == '\0') break;
                chunk.push_back(c);
            }
            if (chunk.empty()) break;
            name += chunk;
        }
        dev.name = ToWide(name);
        return !dev.name.empty();
    }

    bool QueryBattery(Device& dev, BatteryInfo& info) {
        info = BatteryInfo{};
        if (!dev.ep || !dev.ep->ok()) return false;

        // 1) feature 0x1000 BATTERY_STATUS
        int f = GetFeatureIndex(dev, FEAT_BATTERY_STATUS, &dev.feat_bat_status);
        if (f > 0) {
            BYTE rsp[20] = {};
            if (SendLong(dev, static_cast<BYTE>(f), 0x00, 0, 0, 0, rsp) && IsSuccessReply(rsp)) {
                int level = rsp[4];
                int status = rsp[6];
                if (level > 0 && level <= 100) {
                    info.percent = level;
                    info.level_flags = status;
                    info.charging = (status == 1 || status == 2);
                    info.full = (status == 3);
                    info.valid = true;
                    info.source = SRC_BATTERY_STATUS;
                    return true;
                }
            }
        }

        // 2) feature 0x1004 UNIFIED_BATTERY
        f = GetFeatureIndex(dev, FEAT_UNIFIED_BATTERY, &dev.feat_unified_battery);
        if (f > 0) {
            BYTE rsp[20] = {};
            if (SendLong(dev, static_cast<BYTE>(f), 0x01, 0, 0, 0, rsp) && IsSuccessReply(rsp)) {
                int discharge = rsp[4];
                int level = rsp[5];
                int status = rsp[6];
                if (discharge > 0 && discharge <= 100) {
                    info.percent = discharge;
                    info.level_flags = status;
                    info.charging = (status == 1 || status == 2);
                    info.full = (status == 3);
                    info.valid = true;
                    info.source = SRC_UNIFIED_BATTERY;
                    return true;
                }
                // 只有粗略档位
                int approx = (level == 8) ? 100 : (level == 4) ? 75 : (level == 2) ? 25 : (level == 1) ? 5 : 0;
                if (approx > 0) {
                    info.percent = approx;
                    info.level_flags = status;
                    info.charging = (status == 1 || status == 2);
                    info.valid = true;
                    info.source = SRC_UNIFIED_BATTERY;
                    return true;
                }
            }
        }

        // 3) feature 0x1001 BATTERY_VOLTAGE
        f = GetFeatureIndex(dev, FEAT_BATTERY_VOLTAGE, &dev.feat_bat_voltage);
        if (f > 0) {
            BYTE rsp[20] = {};
            if (SendLong(dev, static_cast<BYTE>(f), 0x00, 0, 0, 0, rsp) && IsSuccessReply(rsp)) {
                int mv = (rsp[4] << 8) | rsp[5];
                int flags = rsp[6];
                if (mv > 2000 && mv < 6000) {
                    info.voltage_mv = mv;
                    info.level_flags = flags;
                    info.charging = (flags & 0x80) != 0;
                    info.percent = Clamp(EstimateLevelFromVoltage(mv), 0, 100);
                    info.valid = true;
                    info.source = SRC_BATTERY_VOLTAGE;
                    return true;
                }
            }
        }

        // 4) HID++ 1.0 寄存器 0x07（老设备兜底）
        {
            BYTE rsp[20] = {};
            if (SendShort(dev, 0x07, 0x00, 0x00, 0x00, rsp) && IsSuccessReply(rsp) && rsp[2] == 0x07) {
                int flags = rsp[3];
                int level = rsp[4];
                if (level > 100) { level = rsp[3]; flags = rsp[4]; }
                if (level > 0 && level <= 100) {
                    info.percent = level;
                    info.level_flags = flags;
                    info.charging = (flags == 1);
                    info.full = (flags == 3);
                    info.valid = true;
                    info.source = SRC_HIDPP10_REG07;
                    return true;
                }
            }
        }

        return false;
    }

    std::wstring DescribeDevice(const Device& dev) {
        if (!dev.name.empty()) return dev.name;
        if (!dev.product.empty()) return dev.product;
        wchar_t buf[64];
        swprintf(buf, 64, L"Logitech %04X:%04X", dev.vid, dev.pid);
        return buf;
    }

} // namespace logi
