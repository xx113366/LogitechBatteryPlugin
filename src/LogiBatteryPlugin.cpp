// LogiBatteryPlugin.cpp - 插件实现
#include "LogiBatteryPlugin.h"

#include <shellapi.h>

#include "OptionsDialog.h"

#include <algorithm>
#include <cstdio>
#include <cwchar>
#include <utility>

namespace {

    const wchar_t* const kPluginName = L"罗技设备电量";
    const wchar_t* const kPluginDesc = L"显示罗技鼠标/键盘的电池电量（通过 HID++ 协议读取）";
    const wchar_t* const kPluginAuthor = L"xb615381";
    const wchar_t* const kPluginVersion = L"1.3.0";
    const wchar_t* const kPluginUrl = L"https://github.com/xb615381/LogitechBatteryPlugin";

    const wchar_t* const kItemName[3] = { L"罗技设备1电量", L"罗技设备2电量", L"罗技设备3电量" };
    const wchar_t* const kItemId[3] = { L"LogiBattery1", L"LogiBattery2", L"LogiBattery3" };

    inline int ClampInt(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

    bool FileExists(const std::wstring& path) {
        DWORD attr = GetFileAttributesW(path.c_str());
        return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
    }

    // 配置文件的最后写入时间，用于「改完 ini 立即生效」
    ULONGLONG ConfigStamp(const std::wstring& path) {
        WIN32_FILE_ATTRIBUTE_DATA fad{};
        if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad)) return 0;
        ULARGE_INTEGER u{};
        u.HighPart = fad.ftLastWriteTime.dwHighDateTime;
        u.LowPart = fad.ftLastWriteTime.dwLowDateTime;
        return u.QuadPart;
    }

    // 把 TrafficMonitor 传来的颜色字符串转成 COLORREF；失败返回 CLR_INVALID
    COLORREF ParseColorString(const wchar_t* s) {
        if (!s || !*s) return CLR_INVALID;
        if (s[0] == L'#') {                       // #RRGGBB
            unsigned long v = wcstoul(s + 1, nullptr, 16);
            int r = (int)((v >> 16) & 0xFF), g = (int)((v >> 8) & 0xFF), b = (int)(v & 0xFF);
            return RGB(r, g, b);
        }
        if (wcschr(s, L',')) {                     // 255,255,255
            int r = 0, g = 0, b = 0;
            if (swscanf_s(s, L"%d,%d,%d", &r, &g, &b) == 3)
                return RGB(ClampInt(r, 0, 255), ClampInt(g, 0, 255), ClampInt(b, 0, 255));
            return CLR_INVALID;
        }
        // 纯数字：按 COLORREF 0x00BBGGRR 处理（十进制或 0x 前缀十六进制）
        wchar_t* end = nullptr;
        unsigned long v = wcstoul(s, &end, 0);
        if (end && *end) return CLR_INVALID;
        return (COLORREF)v;
    }

    std::wstring Shorten(const std::wstring& s, size_t max_len) {
        if (s.size() <= max_len) return s;
        return s.substr(0, max_len) + L"…";
    }

    // 只替换 ini 里已有的键值行，保留 UTF-8 BOM、中文注释和用户的其他设置。
    // 不用 WritePrivateProfileStringW：它会按 ANSI 重写文件，把中文注释弄乱。
    bool WriteIniValues(const std::wstring& path,
                        const std::vector<std::pair<std::string, std::string>>& kv) {
        HANDLE h = CreateFileW(path.c_str(), GENERIC_READ,
                               FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return false;
        const DWORD size = GetFileSize(h, nullptr);
        std::string text(size, '\0');
        DWORD read = 0;
        if (size > 0) ReadFile(h, &text[0], size, &read, nullptr);
        CloseHandle(h);
        text.resize(read);

        const bool bom = text.size() >= 3 &&
                         (BYTE)text[0] == 0xEF && (BYTE)text[1] == 0xBB && (BYTE)text[2] == 0xBF;
        const std::string body = bom ? text.substr(3) : text;

        std::vector<std::string> lines;
        size_t start = 0;
        while (start <= body.size()) {
            const size_t e = body.find('\n', start);
            if (e == std::string::npos) { lines.push_back(body.substr(start)); break; }
            lines.push_back(body.substr(start, e - start));
            start = e + 1;
        }
        while (!lines.empty() && lines.back().empty()) lines.pop_back();

        for (const auto& item : kv) {
            bool found = false;
            for (auto& line : lines) {
                const size_t q = line.find_first_not_of(" \t\r");
                if (q == std::string::npos) continue;
                if (line[q] == ';' || line[q] == '#' || line[q] == '[') continue;
                std::string t = line.substr(q);
                if (!t.empty() && t.back() == '\r') t.pop_back();
                if (t.size() > item.first.size() &&
                    t.compare(0, item.first.size(), item.first) == 0 &&
                    t[item.first.size()] == '=') {
                    const std::string cr = (!line.empty() && line.back() == '\r') ? "\r" : "";
                    line = line.substr(0, q) + item.first + "=" + item.second + cr;
                    found = true;
                    break;
                }
            }
            if (!found) lines.push_back(item.first + "=" + item.second);
        }

        std::string out;
        if (bom) out += "\xEF\xBB\xBF";
        for (const auto& line : lines) { out += line; out += '\n'; }

        HANDLE hw = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hw == INVALID_HANDLE_VALUE) return false;
        DWORD written = 0;
        WriteFile(hw, out.data(), (DWORD)out.size(), &written, nullptr);
        CloseHandle(hw);
        return true;
    }

} // namespace

////////////////////////////////////////////////////////////////////////////////
// CLogiBatteryItem

CLogiBatteryItem::CLogiBatteryItem(int index) : m_index(index) {}

const wchar_t* CLogiBatteryItem::GetItemName() const { return kItemName[m_index]; }
const wchar_t* CLogiBatteryItem::GetItemId()   const { return kItemId[m_index]; }

const wchar_t* CLogiBatteryItem::GetItemLableText() const {
    if (!m_label.empty()) return m_label.c_str();
    return L"罗技设备";
}

const wchar_t* CLogiBatteryItem::GetItemValueText() const {
    if (!m_value.empty()) return m_value.c_str();
    return L"--";
}

const wchar_t* CLogiBatteryItem::GetItemValueSampleText() const { return L"100%+"; }

icon::Options CLogiBatteryItem::IconOptions() const {
    icon::Options o;
    o.style = m_icon_style;
    o.show_text = (m_display_mode == 1);
    o.number_inside = (m_display_mode == 3);
    o.gap = m_mouse_gap;
    o.mouse_scale = m_mouse_scale / 100.f;
    return o;
}

// 自绘时主程序会完全忽略 GetItemLableText / GetItemValueText / GetItemValueSampleText，
// 所以图标模式下任务栏上不会残留任何文字（接口文档 IPluginItem::IsCustomDraw）。
bool CLogiBatteryItem::IsCustomDraw() const { return m_display_mode != 2; }

int CLogiBatteryItem::GetItemWidth() const {
    if (m_display_mode == 2) return 0;
    return icon::BaseWidth(IconOptions());       // 96 DPI 基准，主程序会按 DPI 放大
}

int CLogiBatteryItem::GetItemWidthEx(void* hDC) const {
    if (m_display_mode == 2) return 0;
    int dpi = 96;
    if (hDC) {
        dpi = GetDeviceCaps((HDC)hDC, LOGPIXELSY);
        if (dpi < 48) dpi = 96;
    }
    return icon::PixelWidth(IconOptions(), dpi); // 实际像素，主程序不再放大
}

void CLogiBatteryItem::DrawItem(void* hDC, int x, int y, int w, int h, bool dark_mode) {
    if (m_display_mode == 2 || hDC == nullptr) return;

    icon::Status st;
    st.percent = m_percent;
    st.charging = m_charging;
    st.valid = m_valid;
    st.text = GetItemValueText();

    COLORREF outline = (m_value_color != CLR_INVALID) ? m_value_color
                     : (m_label_color != CLR_INVALID) ? m_label_color
                     : CLR_INVALID;
    icon::DrawToHDC((HDC)hDC, x, y, w, h, outline, dark_mode, st, IconOptions());
}

int CLogiBatteryItem::OnMouseEvent(MouseEventType type, int x, int y, void* hWnd, int flag) {
    // 双击立即刷新（返回 0，让主程序继续处理其他鼠标事件）
    if (type == MT_DBCLICKED) {
        CLogiBatteryPlugin::Instance().OnPluginCommand(0, hWnd, nullptr);
    }
    return 0;
}

void CLogiBatteryItem::SetContent(const std::wstring& label, const std::wstring& value,
                                 int percent, bool charging, bool valid) {
    m_label = label;
    m_value = value;
    m_percent = percent;
    m_charging = charging;
    m_valid = valid;
}

////////////////////////////////////////////////////////////////////////////////
// CLogiBatteryPlugin

CLogiBatteryPlugin& CLogiBatteryPlugin::Instance() {
    static CLogiBatteryPlugin instance;
    return instance;
}

CLogiBatteryPlugin::CLogiBatteryPlugin()
    : m_item0(0), m_item1(1), m_item2(2)
{
}

IPluginItem* CLogiBatteryPlugin::GetItem(int index) {
    if (index < 0 || index >= m_config.device_count) return nullptr;
    switch (index) {
    case 0: return &m_item0;
    case 1: return &m_item1;
    case 2: return &m_item2;
    default: break;
    }
    return nullptr;
}

const wchar_t* CLogiBatteryPlugin::GetInfo(PluginInfoIndex index) {
    switch (index) {
    case TMI_NAME:        return kPluginName;
    case TMI_DESCRIPTION: return kPluginDesc;
    case TMI_AUTHOR:      return kPluginAuthor;
    case TMI_COPYRIGHT:   return L"MIT License";
    case TMI_VERSION:     return kPluginVersion;
    case TMI_URL:         return kPluginUrl;
    default: break;
    }
    return L"";
}

std::wstring CLogiBatteryPlugin::IniPath() const {
    std::wstring dir = m_config_dir;
    if (dir.empty()) {
        wchar_t buf[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, buf, MAX_PATH);
        dir = buf;
        size_t pos = dir.find_last_of(L"\\/");
        if (pos != std::wstring::npos) dir = dir.substr(0, pos);
    }
    if (!dir.empty() && dir.back() != L'\\' && dir.back() != L'/') dir += L'\\';
    return dir + L"LogitechBattery.ini";
}

void CLogiBatteryPlugin::SaveDefaultConfig() const {
    std::wstring path = IniPath();
    if (FileExists(path)) return;
    const wchar_t* content =
        L"; TrafficMonitor 罗技设备电量插件配置\n"
        L"; 图形化设置：右键任务栏 → 其他功能 → 插件管理 → 选中本插件 → 点「选项」\n"
        L"; 本文件供高级配置；改完保存几秒内自动生效，不需要重启 TrafficMonitor\n"
        L"[general]\n"
        L"; 显示模式：0=仅图标（默认，鼠标+电池，任务栏上没有任何文字）\n"
        L";          1=图标 + 右侧百分比数字\n"
        L";          2=纯文字（标签 + 数值，退回主程序绘制）\n"
        L";          3=百分比直接显示在电池内部（电池会自动加宽）\n"
        L"display_mode=0\n"
        L"; 图标样式：0=鼠标图标 + 横向电池（默认）\n"
        L";          1=鼠标造型电池，电量自下而上填充\n"
        L"icon_style=0\n"
        L"; 鼠标图标与电池之间的留白（像素，96 DPI 下；8 约等于一个空格）\n"
        L"mouse_gap=8\n"
        L"; 鼠标图标大小（%，100 = 默认 14.5 px 高；可填 50~150，改完保存几秒生效）\n"
        L"mouse_scale=100\n"
        L"; 电量刷新间隔（秒），无线鼠标建议不低于 30\n"
        L"refresh_interval=60\n"
        L"; 重新扫描设备的间隔（秒）\n"
        L"rescan_interval=300\n"
        L"; 接收器插槽数量（Unifying 为 6，LIGHTSPEED 一般为 1）\n"
        L"max_slot=6\n"
        L"; 显示几个设备项目（1~3）\n"
        L"device_count=1\n"
        L"; 低电量提醒阈值（%），0 表示关闭\n"
        L"warn_threshold=10\n"
        L"; 低电量提醒冷却时间（秒）\n"
        L"warn_cooldown=21600\n"
        L"; 纯文字模式下标签是否显示设备名称（0=固定显示“鼠标电量”）\n"
        L"show_device_name=0\n";

    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;

    // 写入 UTF-8 BOM 以便记事本正确识别中文
    const unsigned char bom[3] = { 0xEF, 0xBB, 0xBF };
    WriteFile(h, bom, 3, &written, nullptr);

    int need = WideCharToMultiByte(CP_UTF8, 0, content, -1, nullptr, 0, nullptr, nullptr);
    if (need > 1) {
        std::string utf8(need - 1, '\0');
        WideCharToMultiByte(CP_UTF8, 0, content, -1, &utf8[0], need, nullptr, nullptr);
        WriteFile(h, utf8.data(), (DWORD)utf8.size(), &written, nullptr);
    }
    CloseHandle(h);
}

// 老版本生成的 ini 里没有新加的键，用户打开后自然「找不到设置的地方」。
// 这里只追加缺失的键（带中文注释），不改动任何已有键值、不删废弃项、保留 UTF-8 BOM。
void CLogiBatteryPlugin::EnsureConfigKeys() const {
    const std::wstring path = IniPath();
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD size = GetFileSize(h, nullptr);
    std::string text(size, '\0');
    DWORD read = 0;
    if (size > 0) ReadFile(h, &text[0], size, &read, nullptr);
    CloseHandle(h);
    text.resize(read);

    struct Def { const char* key; const wchar_t* block; };
    static const Def defs[] = {
        { "display_mode=", L"; 显示模式：0=仅图标 1=图标+右侧数字 2=纯文字 3=电池内显示百分比\ndisplay_mode=0\n" },
        { "icon_style=",   L"; 图标样式：0=鼠标图标+横向电池 1=鼠标造型电池\nicon_style=0\n" },
        { "mouse_gap=",    L"; 鼠标图标与电池之间的留白（像素，8 约等于一个空格，范围 0~32）\nmouse_gap=8\n" },
        { "mouse_scale=",  L"; 鼠标图标大小（%，100 = 默认 14.5 px 高，范围 50~150）\nmouse_scale=100\n" },
    };

    std::wstring missing;
    for (const Def& d : defs)
        if (text.find(d.key) == std::string::npos) missing += d.block;
    if (missing.empty()) return;

    // 文件末尾可能没有换行，先隔开，并标注这些是新版自动补齐的项
    missing.insert(0, L"\n; ---- 以下为新版配置项（插件自动补齐，可直接修改）----\n");
    int need = WideCharToMultiByte(CP_UTF8, 0, missing.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (need <= 1) return;
    std::string utf8(need - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, missing.c_str(), -1, &utf8[0], need, nullptr, nullptr);

    HANDLE hw = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ,
                            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hw == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(hw, utf8.data(), (DWORD)utf8.size(), &written, nullptr);
    CloseHandle(hw);
}

void CLogiBatteryPlugin::LoadConfig() {
    const std::wstring path = IniPath();
    if (!FileExists(path)) {
        SaveDefaultConfig();
        LoadConfig();   // 生成后再读一次
        return;
    }
    EnsureConfigKeys();
    auto get_int = [&path](const wchar_t* key, int def) -> int {
        return (int)GetPrivateProfileIntW(L"general", key, def, path.c_str());
    };

    m_config.refresh_interval = ClampInt(get_int(L"refresh_interval", 60), 5, 3600);
    m_config.rescan_interval = ClampInt(get_int(L"rescan_interval", 300), 30, 86400);
    m_config.max_slot = ClampInt(get_int(L"max_slot", 6), 1, 6);
    m_config.device_count = ClampInt(get_int(L"device_count", 1), 1, 3);
    m_config.warn_threshold = ClampInt(get_int(L"warn_threshold", 10), 0, 100);
    m_config.warn_cooldown = ClampInt(get_int(L"warn_cooldown", 21600), 60, 86400 * 7);
    m_config.show_device_name = get_int(L"show_device_name", 0) != 0;

    // 旧版配置（只有 show_icon）没有 display_mode：平滑升级为「纯图标」，
    // 而不是沿用旧的文字模式。写了新键之后一律以新键为准。
    wchar_t mode_key[32] = {};
    GetPrivateProfileStringW(L"general", L"display_mode", L"", mode_key, 32, path.c_str());
    m_config.display_mode = (mode_key[0] == L'\0')
                          ? 0 : ClampInt(get_int(L"display_mode", 0), 0, 3);
    m_config.icon_style = ClampInt(get_int(L"icon_style", 0), 0, 1);
    m_config.mouse_gap = ClampInt(get_int(L"mouse_gap", 8), 0, 32);
    m_config.mouse_scale = ClampInt(get_int(L"mouse_scale", 100), 50, 150);

    CLogiBatteryItem* items[3] = { &m_item0, &m_item1, &m_item2 };
    for (CLogiBatteryItem* it : items) {
        it->m_display_mode = m_config.display_mode;
        it->m_icon_style = m_config.icon_style;
        it->m_mouse_gap = m_config.mouse_gap;
        it->m_mouse_scale = m_config.mouse_scale;
    }
}

void CLogiBatteryPlugin::OnInitialize(ITrafficMonitor* pApp) {
    m_app = pApp;
    if (m_app && m_app->GetAPIVersion() >= 1) {
        const wchar_t* dir = m_app->GetPluginConfigDir();
        if (dir) m_config_dir = dir;
    }
    LoadConfig();
    m_config_loaded = true;
        m_phase = PHASE_ENUM;
}

void CLogiBatteryPlugin::OnExtenedInfo(ExtendedInfoIndex index, const wchar_t* data) {
    switch (index) {
    case EI_CONFIG_DIR:
        if (data && *data) {
            m_config_dir = data;
            m_config_loaded = false;
        }
        break;
    case EI_LABEL_TEXT_COLOR: {
        COLORREF c = ParseColorString(data);
        m_item0.m_label_color = c; m_item1.m_label_color = c; m_item2.m_label_color = c;
        break;
    }
    case EI_VALUE_TEXT_COLOR: {
        COLORREF c = ParseColorString(data);
        m_item0.m_value_color = c; m_item1.m_value_color = c; m_item2.m_value_color = c;
        break;
    }
    default:
        break;
    }
}

int CLogiBatteryPlugin::GetCommandCount() { return 1; }

const wchar_t* CLogiBatteryPlugin::GetCommandName(int command_index) {
    if (command_index == 0) return L"立即刷新电量";
    return L"";
}

void CLogiBatteryPlugin::OnPluginCommand(int command_index, void* hWnd, void* para) {
    (void)hWnd; (void)para;
    if (command_index == 0) {
        m_force_refresh = true;
        m_phase = PHASE_ENUM;      // 重新枚举一次，顺便处理设备插拔
    }
}

// 分步推进工作：每次调用最多做一次阻塞操作，避免卡住主程序界面
void CLogiBatteryPlugin::AdvanceWork() {
    DWORD now = GetTickCount();

    if (m_phase == PHASE_ENUM) {
        m_interfaces = logi::EnumInterfaces();
        // 长报文端点优先（HID++ 2.0 设备只在 20 字节通道上应答）
        std::stable_sort(m_interfaces.begin(), m_interfaces.end(),
                         [](const logi::Device& a, const logi::Device& b) {
                             return a.ep->output_len() > b.ep->output_len();
                         });
        m_scan_iface = 0;
        m_scan_slot = 1;
        m_scan_result.clear();
        m_scan_started = false;
        m_phase = PHASE_SCAN;
        return;
    }

    if (m_phase == PHASE_SCAN) {
        // 一次调用最多探测 3 个槽位，避免长时间占用主程序的界面线程
        for (int step = 0; step < 3 && m_scan_iface < m_interfaces.size(); ++step) {
            logi::Device probe = m_interfaces[m_scan_iface];
            probe.device_index = m_scan_slot;
            if (logi::PingDevice(probe, m_scan_slot, 150)) {
                probe.feat_name = -1;
                probe.feat_bat_status = -1;
                probe.feat_bat_voltage = -1;
                probe.feat_unified_battery = -1;
                probe.name_queried = false;
                m_scan_result.push_back(probe);
            }
            if (++m_scan_slot > m_config.max_slot) {
                bool long_ok = m_interfaces[m_scan_iface].ep->output_len() >= 20;
                ++m_scan_iface;
                m_scan_slot = 1;
                // 已在长报文通道上找到设备，其余集合是同一接收器的另一通道，跳过
                if (long_ok && !m_scan_result.empty())
                    m_scan_iface = m_interfaces.size();
            }
        }
        if (m_scan_iface < m_interfaces.size())
            return;     // 还没扫完，下次调用继续

        // 扫描结束：没有子设备时尝试直连设备（device index 0xFF）
        if (m_scan_result.empty()) {
            for (auto& iface : m_interfaces) {
                logi::Device probe = iface;
                probe.device_index = 0xFF;
                if (logi::PingDevice(probe, 0xFF)) {
                    probe.feat_name = -1;
                    probe.feat_bat_status = -1;
                    probe.feat_bat_voltage = -1;
                    probe.feat_unified_battery = -1;
                    probe.name_queried = false;
                    m_scan_result.push_back(probe);
                    break;
                }
            }
        }

        // 去重：同一设备若在短报文端点也应答，只保留长报文端点
        bool has_long = false;
        for (auto& d : m_scan_result)
            if (d.ep->output_len() >= 20) has_long = true;

        std::vector<logi::Device> final_list;
        if (has_long) {
            for (auto& d : m_scan_result)
                if (d.ep->output_len() >= 20) final_list.push_back(d);
        } else {
            final_list = m_scan_result;
        }

        m_devices = final_list;
        std::stable_sort(m_devices.begin(), m_devices.end(),
                         [](const logi::Device& a, const logi::Device& b) {
                             return a.device_index < b.device_index;
                         });
        m_batteries.assign(m_devices.size(), logi::BatteryInfo{});
        m_warn_time.assign(m_devices.size(), 0);
        m_last_ok.assign(m_devices.size(), 0);

        // 读一次设备名（最多 3 个，避免一次调用太慢）
        for (size_t i = 0; i < m_devices.size() && i < 3; ++i) {
            logi::QueryDeviceName(m_devices[i]);
        }

        RefreshDisplay();
        m_phase = PHASE_IDLE;
        m_query_cursor = 0;
        m_next_query = now;
        m_next_rescan = now + (DWORD)m_config.rescan_interval * 1000;
        return;
    }
}

bool CLogiBatteryPlugin::QueryOneDevice(int idx) {
    if (idx < 0 || idx >= (int)m_devices.size()) return false;
    if (idx >= (int)m_batteries.size()) m_batteries.resize(m_devices.size());
    if (idx >= (int)m_last_ok.size()) m_last_ok.resize(m_devices.size(), 0);

    logi::BatteryInfo info;
    if (logi::QueryBattery(m_devices[idx], info)) {
        m_batteries[idx] = info;
        m_last_ok[idx] = GetTickCount();
        return true;
    }

    // 读取失败：10 分钟内保留上次数值（无线设备休眠是常态），超时才显示 --
    DWORD now = GetTickCount();
    if (m_last_ok[idx] != 0 && now - m_last_ok[idx] < 10 * 60 * 1000) {
        // 保留旧值
    } else {
        m_batteries[idx].valid = false;
    }
    return false;
}

void CLogiBatteryPlugin::DataRequired() {
    // 用户用记事本改完 ini 保存后，几秒内自动重新读取，不必重启主程序
    const ULONGLONG stamp = ConfigStamp(IniPath());
    if (stamp != 0) {
        if (m_stamp_ready && stamp != m_config_stamp) {
            LoadConfig();
            m_config_loaded = true;
            RefreshDisplay();
        }
        m_config_stamp = stamp;
        m_stamp_ready = true;
    }

    if (!m_config_loaded) {
        LoadConfig();
        m_config_loaded = true;
    }

    DWORD now = GetTickCount();

    if (m_force_refresh) {
        m_force_refresh = false;
        m_next_query = now;
    }

    if (m_phase != PHASE_IDLE) {
        AdvanceWork();
        return;
    }

    if (m_interfaces.empty() || now >= m_next_rescan) {
        m_phase = PHASE_ENUM;
        AdvanceWork();
        return;
    }

    // 到点刷新：每次调用只查一个设备
    if (now >= m_next_query && !m_devices.empty()) {
        QueryOneDevice(m_query_cursor);
        ++m_query_cursor;
        if (m_query_cursor >= (int)m_devices.size()) {
            m_query_cursor = 0;
            m_next_query = now + (DWORD)m_config.refresh_interval * 1000;
            RefreshDisplay();
        }
    }

    // 低电量提醒
    if (m_config.warn_threshold > 0 && m_app) {
        for (size_t i = 0; i < m_batteries.size(); ++i) {
            const logi::BatteryInfo& b = m_batteries[i];
            if (!b.valid || b.percent < 0 || b.charging) continue;
            if (b.percent > m_config.warn_threshold) continue;
            if (now - m_warn_time[i] < (DWORD)m_config.warn_cooldown * 1000) continue;
            m_warn_time[i] = now;
            wchar_t msg[256] = {};
            swprintf_s(msg, L"%s 电量仅剩 %d%%，请及时充电。",
                       logi::DescribeDevice(m_devices[i]).c_str(), b.percent);
            m_app->ShowNotifyMessage(msg);
        }
    }
}

void CLogiBatteryPlugin::RefreshDisplay() {
    CLogiBatteryItem* items[3] = { &m_item0, &m_item1, &m_item2 };
    for (CLogiBatteryItem* it : items) {
        it->m_display_mode = m_config.display_mode;
        it->m_icon_style = m_config.icon_style;
        it->m_mouse_gap = m_config.mouse_gap;
        it->m_mouse_scale = m_config.mouse_scale;
    }

    for (int i = 0; i < 3; ++i) {
        bool used = (i < m_config.device_count);
        bool has_dev = used && (i < (int)m_devices.size());
        if (!used) {
            items[i]->SetContent(L"", L"--", -1, false, false);
            continue;
        }
        if (!has_dev) {
            items[i]->SetContent(L"罗技设备", L"--", -1, false, false);
            continue;
        }

        const logi::BatteryInfo& b = (i < (int)m_batteries.size()) ? m_batteries[i] : logi::BatteryInfo{};
        std::wstring dev_name = logi::DescribeDevice(m_devices[i]);

        std::wstring label;
        if (m_config.show_device_name && !dev_name.empty()) {
            label = Shorten(dev_name, 14);
        } else {
            static const wchar_t* compact[3] = { L"鼠标电量", L"电量2", L"电量3" };
            label = compact[i];
        }

        std::wstring value;
        if (b.valid && b.percent >= 0) {
            wchar_t buf[32] = {};
            if (b.charging) swprintf_s(buf, L"%d%%+", b.percent);
            else           swprintf_s(buf, L"%d%%", b.percent);
            value = buf;
        } else {
            value = L"--";
        }

        items[i]->SetContent(label, value, b.percent, b.charging, b.valid && b.percent >= 0);
    }

    UpdateTooltip();
}

void CLogiBatteryPlugin::UpdateTooltip() {
    SYSTEMTIME st{};
    GetLocalTime(&st);

    // 图标模式下任务栏上没有数字，精确数值放在悬停提示的第一眼位置
    std::wstring tip;
    if (m_devices.empty()) {
        tip = L"罗技设备电量 · 未检测到设备\n（请确认接收器已插入、设备已开启）";
        m_tooltip = tip;
        return;
    }

    bool any_ok = false;
    for (DWORD t : m_last_ok) if (t != 0) { any_ok = true; break; }

    for (size_t i = 0; i < m_devices.size() && i < 3; ++i) {
        if (i >= m_batteries.size()) break;
        const logi::BatteryInfo& b = m_batteries[i];
        wchar_t line[256] = {};
        const std::wstring name = logi::DescribeDevice(m_devices[i]);
        if (b.valid && b.percent >= 0) {
            const wchar_t* state = b.charging ? L"  充电中" : (b.full ? L"  已充满" : L"");
            if (b.voltage_mv > 0)
                swprintf_s(line, L"%d%%%s  %s  (%d mV)", b.percent, state, name.c_str(), b.voltage_mv);
            else
                swprintf_s(line, L"%d%%%s  %s", b.percent, state, name.c_str());
        } else {
            const wchar_t* note = any_ok ? L"读取失败" : L"读取中…";
            swprintf_s(line, L"--  %s  (%s)", name.c_str(), note);
        }
        tip += line;
        tip += L"\n";
    }

    wchar_t timebuf[96] = {};
    swprintf_s(timebuf, L"更新于 %02d:%02d:%02d   ·   双击图标立即刷新", st.wHour, st.wMinute, st.wSecond);
    tip += timebuf;
    m_tooltip = tip;
}

const wchar_t* CLogiBatteryPlugin::GetTooltipInfo() {
    if (m_tooltip.empty()) return L"";
    return m_tooltip.c_str();
}

void CLogiBatteryPlugin::ApplyOptions(const optsdlg::Values& v) {
    WriteIniValues(IniPath(), {
        { "mouse_scale",  std::to_string(v.mouse_scale) },
        { "mouse_gap",    std::to_string(v.mouse_gap) },
        { "display_mode", std::to_string(v.display_mode) },
        { "icon_style",   std::to_string(v.icon_style) },
        { "device_count", std::to_string(v.device_count) },
    });
    LoadConfig();
    m_config_loaded = true;
    RefreshDisplay();

    // 同步时间戳，避免已有的 ini 热重载再重复加载一次
    const ULONGLONG stamp = ConfigStamp(IniPath());
    if (stamp != 0) { m_config_stamp = stamp; m_stamp_ready = true; }

    // 主动请主程序重绘两个窗口，任务栏上立刻能看到新效果，不必等下一次定时刷新
    if (m_app && m_app->GetAPIVersion() >= 1) {
        void* wins[2] = { m_app->GetTaskbarWindowHwnd(), m_app->GetMainWindowHwnd() };
        for (void* p : wins) {
            HWND hw = (HWND)p;
            if (hw && IsWindow(hw)) {
                InvalidateRect(hw, nullptr, FALSE);
                UpdateWindow(hw);
            }
        }
    }
}

ITMPlugin::OptionReturn CLogiBatteryPlugin::ShowOptionsDialog(void* hParent) {
    if (!m_config_loaded) LoadConfig();

    optsdlg::Values v;
    v.mouse_scale = m_config.mouse_scale;
    v.mouse_gap = m_config.mouse_gap;
    v.display_mode = m_config.display_mode;
    v.icon_style = m_config.icon_style;
    v.device_count = m_config.device_count;
    if (!m_batteries.empty() && m_batteries[0].valid && m_batteries[0].percent >= 0) {
        v.preview_percent = m_batteries[0].percent;
        v.preview_charging = m_batteries[0].charging;
    }

    const std::wstring path = IniPath();
    if (!FileExists(path)) SaveDefaultConfig();

    // 「应用」通过这个回调即时写 ini + 刷新任务栏，但设置窗口保持打开
    const bool changed = optsdlg::Show((HWND)hParent, path, v,
        [](void*, const optsdlg::Values& val) {
            CLogiBatteryPlugin::Instance().ApplyOptions(val);
        }, nullptr);

    if (!changed) {
        // 纯取消，或用户选了「打开完整配置文件」用记事本改：下一次重新读 ini
        m_config_loaded = false;
        return OR_OPTION_UNCHANGED;
    }

    ApplyOptions(v);     // 点「确定」时再兜底保存一次（幂等）
    return OR_OPTION_CHANGED;
}
