cmake_minimum_required(VERSION 3.16)
project(LogitechBattery CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 插件 DLL：TrafficMonitor 会按主程序位数加载，因此需要生成 x64 或 Win32 对应版本
add_library(LogitechBattery SHARED
    src/dllmain.cpp
    src/LogiBatteryPlugin.cpp
    src/battery_icon.cpp
    src/mouse_icon_data.cpp
    src/OptionsDialog.cpp
    src/hid_logitech.cpp
)

target_compile_definitions(LogitechBattery PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)

target_link_libraries(LogitechBattery PRIVATE
    setupapi
    hid
    shell32
    user32
    gdi32
    gdiplus
    comctl32
)

# 去掉 lib 前缀，输出 LogitechBattery.dll
set_target_properties(LogitechBattery PROPERTIES
    PREFIX ""
    OUTPUT_NAME "LogitechBattery"
)

if (MSVC)
    # 静态链接 CRT，避免目标机器缺少 VC 运行库
    target_compile_options(LogitechBattery PRIVATE /MT /W3 /utf-8)
else()
    target_compile_options(LogitechBattery PRIVATE -Wall -Wextra -O2)
    target_link_options(LogitechBattery PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# 可选：宿主测试程序（模拟 TrafficMonitor 加载插件）
add_executable(PluginHostTest EXCLUDE_FROM_ALL probe/host_test.cpp)
target_include_directories(PluginHostTest PRIVATE src)
target_link_libraries(PluginHostTest PRIVATE user32 gdi32 gdiplus)
if (NOT MSVC)
    target_link_options(PluginHostTest PRIVATE -static -static-libgcc -static-libstdc++)
endif()
