// hid_logitech.h - 罗技 HID++ 设备访问层（纯 Win32 API，无第三方依赖）
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <memory>
#include <string>
#include <vector>

namespace logi {

    constexpr int kMaxSlotPerReceiver = 6;

    // 电池信息来源
    enum BatterySource {
        SRC_NONE = 0,
        SRC_BATTERY_STATUS = 1,    // HID++ 2.0 feature 0x1000
        SRC_BATTERY_VOLTAGE = 2,   // HID++ 2.0 feature 0x1001
        SRC_UNIFIED_BATTERY = 3,   // HID++ 2.0 feature 0x1004
        SRC_HIDPP10_REG07 = 4,     // HID++ 1.0 寄存器 0x07
    };

    struct BatteryInfo {
        bool  valid = false;
        int   percent = -1;        // 0..100，-1 表示未知
        int   voltage_mv = 0;      // 毫伏，0 表示未知
        bool  charging = false;
        bool  full = false;
        int   level_flags = 0;     // 原始状态位
        int   source = SRC_NONE;
    };

    // 一个 HID++ 通信端点（对应一个 HID 集合）。同一接收器上的多个设备共享一个端点。
    class Endpoint {
    public:
        ~Endpoint();
        bool Open(const std::wstring& path, std::wstring* diag = nullptr);

        // 一次请求-应答。req 长度可为 7（短消息）或 20（长消息）。
        bool Request(const BYTE* req, int req_len, BYTE* rsp, int rsp_len, DWORD timeout_ms = 300);

        bool        ok()          const { return handle_ != INVALID_HANDLE_VALUE; }
        int         usage_page()  const { return usage_page_; }
        int         usage()       const { return usage_; }
        int         feature_len() const { return feature_len_; }
        int         output_len()  const { return output_len_; }
        int         input_len()   const { return input_len_; }
        const char* mode()        const { return mode_name_; }

    private:
        bool RequestFeature(const BYTE* req, int req_len, BYTE* rsp, int rsp_len, DWORD timeout_ms);
        bool RequestWriteRead(const BYTE* req, int req_len, BYTE* rsp, int rsp_len, DWORD timeout_ms);
        bool EnsureOverlapped();

        std::wstring path_;
        HANDLE h_feature_ = INVALID_HANDLE_VALUE;   // 同步句柄，用于 HidD_SetFeature/GetFeature
        HANDLE h_overlap_ = INVALID_HANDLE_VALUE;   // 异步句柄，用于 WriteFile/ReadFile
        HANDLE handle_ = INVALID_HANDLE_VALUE;      // 有效性标记（等于上面之一）
        int    feature_len_ = 0;
        int    output_len_ = 0;
        int    input_len_ = 0;
        int    usage_page_ = 0;
        int    usage_ = 0;
        int    strategy_ = 0;                       // 0 未定，1 SetFeature，2 WriteFile+GetFeature，3 WriteFile+ReadFile
        const char* mode_name_ = "none";
    };

    // 一个可显示设备（接收器后面的鼠标/键盘，或直连设备）
    struct Device {
        std::wstring path;          // HID 接口路径
        std::wstring product;       // HID 产品字符串（USB 层）
        std::wstring name;          // HID++ 设备名称（例如 "MX Master 3S"）
        std::wstring serial;
        unsigned short vid = 0;
        unsigned short pid = 0;
        int   device_index = 0;     // HID++ device index：1..6，直连设备为 0xFF
        bool  is_receiver = false;  // 是否通过接收器连接
        bool  present = true;

        std::shared_ptr<Endpoint> ep;

        // 内部缓存：feature index，-1 表示尚未查询，0 表示设备不支持
        int   feat_name = -1;
        int   feat_bat_status = -1;
        int   feat_bat_voltage = -1;
        int   feat_unified_battery = -1;
        bool  name_queried = false;
    };

    // 按 DPI 无关的顺序枚举所有 VID_046D 且支持 HID++ 的接口
    std::vector<Device> EnumInterfaces(std::wstring* diag = nullptr);

    // 在接口上扫描接收器插槽，填充实际存在的设备
    void ScanReceiverDevices(Device& iface, std::vector<Device>& out, int max_slot = kMaxSlotPerReceiver);

    // 探测某个 device index 上是否有设备在（一次 HID++ ping）
    bool PingDevice(Device& dev, int devnum, DWORD timeout_ms = 400);

    // 查询电池
    bool QueryBattery(Device& dev, BatteryInfo& info);

    // 查询设备名称（feature 0x0005）
    bool QueryDeviceName(Device& dev);

    // 把 HID++ 的 device index 转成人类可读描述
    std::wstring DescribeDevice(const Device& dev);

} // namespace logi
