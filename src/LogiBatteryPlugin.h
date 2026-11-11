// LogiBatteryPlugin.h - TrafficMonitor 罗技设备电量插件
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <string>
#include <vector>

#include "PluginInterface.h"
#include "battery_icon.h"
#include "hid_logitech.h"

namespace optsdlg { struct Values; }

// 显示项目（每个对应任务栏上的一个字段）
class CLogiBatteryItem : public IPluginItem
{
public:
    explicit CLogiBatteryItem(int index);

    const wchar_t* GetItemName() const override;
    const wchar_t* GetItemId() const override;
    const wchar_t* GetItemLableText() const override;
    const wchar_t* GetItemValueText() const override;
    const wchar_t* GetItemValueSampleText() const override;

    bool IsCustomDraw() const override;
    int  GetItemWidth() const override;
    int  GetItemWidthEx(void* hDC) const override;
    void DrawItem(void* hDC, int x, int y, int w, int h, bool dark_mode) override;
    int  OnMouseEvent(MouseEventType type, int x, int y, void* hWnd, int flag) override;

    // 由插件更新显示内容
    void SetContent(const std::wstring& label, const std::wstring& value,
                    int percent, bool charging, bool valid);

    int Index() const { return m_index; }

private:
    icon::Options IconOptions() const;

    int          m_index;
    std::wstring m_label;
    std::wstring m_value;
    int          m_percent = -1;
    bool         m_charging = false;
    bool         m_valid = false;
    int          m_display_mode = 0;    // 0=纯图标 1=图标+数字 2=纯文字 3=电池内百分比
    int          m_icon_style = 0;      // 见 icon::Style
    int          m_mouse_gap = 8;       // 鼠标与电池之间的留白（px @96 DPI）
    int          m_mouse_scale = 100;   // 鼠标图标大小（%，100 = 14.5 px 高）
    COLORREF     m_label_color = CLR_INVALID;
    COLORREF     m_value_color = CLR_INVALID;
    bool         m_dark_mode = false;

    friend class CLogiBatteryPlugin;
};

// 插件主体
class CLogiBatteryPlugin : public ITMPlugin
{
public:
    static CLogiBatteryPlugin& Instance();

    IPluginItem* GetItem(int index) override;
    void         DataRequired() override;
    const wchar_t* GetInfo(PluginInfoIndex index) override;
    const wchar_t* GetTooltipInfo() override;
    OptionReturn ShowOptionsDialog(void* hParent) override;
    void         OnExtenedInfo(ExtendedInfoIndex index, const wchar_t* data) override;
    int          GetCommandCount() override;
    const wchar_t* GetCommandName(int command_index) override;
    void         OnPluginCommand(int command_index, void* hWnd, void* para) override;
    void         OnInitialize(ITrafficMonitor* pApp) override;

    // 设置窗口点「应用」时调用：写 ini、重载配置、立即刷新任务栏，窗口不关闭
    void ApplyOptions(const optsdlg::Values& v);

private:
    CLogiBatteryPlugin();
    void LoadConfig();
    void SaveDefaultConfig() const;
    void EnsureConfigKeys() const;   // 旧版 ini 缺新键时自动补齐，保留原有值与注释
    void RefreshDisplay();
    void UpdateTooltip();
    void AdvanceWork();          // 分步推进：枚举 → 扫描 → 查询
    void StartDiscovery();
    bool QueryOneDevice(int idx);
    std::wstring IniPath() const;

    struct Config
    {
        int  refresh_interval = 60;      // 电量刷新间隔（秒）
        int  rescan_interval = 300;      // 重新扫描设备间隔（秒）
        int  max_slot = 6;               // 接收器插槽数量
        int  device_count = 1;           // 暴露几个显示项目
        int  warn_threshold = 10;        // 低电量提示阈值（%）
        int  warn_cooldown = 21600;      // 低电量提示冷却（秒）
        int  display_mode = 0;           // 0=纯图标 1=图标+数字 2=纯文字 3=电池内百分比
        int  icon_style = 0;             // 0=鼠标+电池 1=鼠标造型电池
        int  mouse_gap = 8;              // 鼠标与电池之间的留白（px @96 DPI）
        int  mouse_scale = 100;          // 鼠标图标大小（%，50~150）
        bool show_device_name = false;   // 纯文字模式下标签用设备名
        bool verbose_log = false;
    };

    enum Phase { PHASE_IDLE, PHASE_ENUM, PHASE_SCAN };

    Config  m_config;
    ITrafficMonitor* m_app = nullptr;
    std::wstring m_config_dir;
    std::wstring m_tooltip;
    bool m_initialized = false;
    bool m_config_loaded = false;
    ULONGLONG m_config_stamp = 0;      // ini 最后写入时间，用于热重载
    bool m_stamp_ready = false;
    bool m_force_refresh = false;

    std::vector<logi::Device>      m_devices;
    std::vector<logi::BatteryInfo> m_batteries;
    std::vector<DWORD>             m_warn_time;
    std::vector<DWORD>             m_last_ok;   // 上次成功读取的时间

    // 分步工作的状态
    Phase  m_phase = PHASE_IDLE;
    DWORD  m_next_query = 0;
    DWORD  m_next_rescan = 0;
    int    m_query_cursor = 0;
    size_t m_scan_iface = 0;
    int    m_scan_slot = 1;
    std::vector<logi::Device> m_interfaces;
    std::vector<logi::Device> m_scan_result;
    bool   m_scan_started = false;

    CLogiBatteryItem m_item0;
    CLogiBatteryItem m_item1;
    CLogiBatteryItem m_item2;
};
