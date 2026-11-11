// OptionsDialog.h - 插件的图形化设置窗口（纯 Win32，无 MFC / 无资源脚本）
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <string>

namespace optsdlg {

    struct Values {
        int  mouse_scale = 100;      // 鼠标图标大小（%）
        int  mouse_gap = 8;          // 鼠标与电池的留白（px @96 DPI）
        int  display_mode = 0;       // 0 仅图标 / 1 图标+右侧数字 / 2 纯文字 / 3 电池内百分比
        int  icon_style = 0;         // 见 icon::Style
        int  device_count = 1;       // 显示几个设备项目
        int  preview_percent = 72;   // 预览用的示例电量（实际取当前设备读数）
        bool preview_charging = false;
    };

    // 点「应用」时回调：插件据此写 ini、重载配置并立即刷新任务栏，窗口保持打开。
    using ApplyCallback = void (*)(void* user, const Values& values);

    // 模态显示。返回 true 表示配置发生了改变（点过「应用」或「确定」），
    // values 里是最后一次生效的值；返回 false 表示纯取消。
    // ini_path 只用于「打开完整配置文件」按钮。
    bool Show(HWND parent, const std::wstring& ini_path, Values& values,
              ApplyCallback on_apply = nullptr, void* user = nullptr);

} // namespace optsdlg
