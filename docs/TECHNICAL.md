# 技术实现细节

> 安装、设置与快速开始请看项目根目录的 [README](../README.md)。本文记录 HID++ 协议实测结果、图标绘制、设置窗口实现和开发期调试工具。

给 [TrafficMonitor](https://github.com/zhongyang219/TrafficMonitor) 用的插件：在任务栏 / 主窗口里显示**罗技鼠标（以及键盘）的电池电量**，数据通过罗技私有的 HID++ 协议直接从硬件读取，不依赖 G HUB / Options+ 是否运行。

默认形态是**一枚图标、没有任何文字**：左边是粗线条的鼠标图标（椭圆轮廓 + 滚轮 + 左右键分界弧线），中间留约一个空格的间距，右边是手机电池那样的横向电量条。

![图标效果预览](docs/icon_preview.png)

## 效果

| | |
|---|---|
| 默认 | 鼠标线稿图标 + 留白 + 横向电池，电量按百分比从左往右填充 |
| 鼠标 | 512×512 粗线稿图标，默认 `14.5*s` 高；线条本身就是粗笔触，缩到任务栏尺寸仍然清晰，和电池外框的视觉重量匹配 |
| 鼠标大小 | `mouse_scale=100`（%），可填 50~150，改 ini 保存几秒生效 |
| 间距 | `mouse_gap=8`，约一个空格宽，可在 ini 里调（0~32） |
| 颜色 | >35% 绿 · 15~35% 橙 · ≤15% 红 · 无数据灰 |
| 充电中 | 电池中央叠加白色闪电 |
| 读不到数据 | 空心电池 + 灰色问号 |
| 深浅主题 | 鼠标整体染成主程序的文字颜色（浅色黑线 / 深色白线）；若素材本身带彩色强调层，会单独识别并保留原色 |
| 精确数值 | 鼠标悬停图标，提示里第一行就是 `76%  PRO X2 SUPERSTRIKE` |
| 立即刷新 | 双击图标，或用插件菜单「立即刷新电量」 |

鼠标图标原型是一张 512×512 粗线条图标，已转成 64 像素高的内嵌蒙版编译进 DLL。相比第一版 128px 细线稿，粗笔触缩到十几像素后不会糊成灰线，轮廓、滚轮、按键弧线都更清楚。GDI+ 抗锯齿绘制、按 DPI 自适应缩放，1:1 任务栏尺寸下（约 42×16 px，鼠标本体 14.5 px 高）各部分依然清晰。预览图最下面一行就是 1:1 的真实大小。原始 PNG 不随仓库分发，来源与授权见 `assets/NOTICE.md`。

### 四种显示模式

`display_mode` 控制任务栏上数字怎么出现：

| 值 | 效果 |
|---|---|
| `0` | **仅图标（默认）**。任务栏上完全没有文字，最省空间 |
| `1` | 图标 + 右侧百分比数字（如 `76%+`，加号代表充电中） |
| `2` | 纯文字（`鼠标电量 76%`），退回主程序绘制，不使用图标 |
| `3` | **电池内显示电量数值**。电池 24×13 px（96 DPI），内部画放大的纯数字（无 `%` 号） |

设置窗口的下拉顺序是「仅图标 / 电池内 / 右侧 / 纯文字」，通过 `kModeValues[]` 映射到 `0/3/1/2`，这样新选项能排在顺手的位置，同时保持旧配置文件里 0/1/2 的语义不变。

`icon_style` 控制图标造型：

| 值 | 效果 |
|---|---|
| `0` | **鼠标线稿图标 + 留白 + 横向电池（默认）**，辨识度最高 |
| `1` | 内置线条画的鼠标轮廓本身就是电池外壳（右侧带正极），电量自下而上填充，更紧凑；此样式不使用 PNG 素材，也不受 `mouse_gap` 影响 |

> 为什么可以做到「零文字」：`IsCustomDraw()` 返回 true 时，TrafficMonitor 会**忽略** `GetItemLableText()` / `GetItemValueText()` / `GetItemValueSampleText()` 的返回值，整个显示区域交给插件的 `DrawItem()`。所以图标模式下不会残留标签或数值。

## 功能

- 支持接收器（Unifying / Bolt / LIGHTSPEED）后面的设备，也支持蓝牙直连设备
- 支持同时显示最多 3 个设备（`device_count` 可配，默认 1 个图标）
- 悬停提示里显示设备真实名称、充电状态、电压、上次更新时间
- **内置图形化设置窗口**：滑块调大小/间距、下拉选模式，带浅色+深色实时预览，点确定直接保存
- 低电量自动弹窗提醒（阈值、冷却时间可配）
- 纯 Win32 API + GDI+ 实现，无 MFC、无第三方库，静态链接 CRT，单个 DLL 直接可用
- 只依赖系统 DLL（含 `gdiplus.dll`，Windows Vista 及以后自带）

## 已实测硬件

| 设备 | 连接方式 | 结果 |
|---|---|---|
| Logitech G PRO X2 SUPERSTRIKE | LIGHTSPEED 接收器（USB PID_C54D） | ✅ 电量 / 充电状态 / 设备名全部正常 |

协议层按 HID++ 2.0 规范实现，理论上覆盖绝大多数现代罗技设备：

- 鼠标：MX Master 系列、M720、M590、G304/G502/G Pro 等
- 键盘：MX Keys、K850 等带电池的型号
- 接收器：Unifying（C52B）、Bolt（C548）、LIGHTSPEED（C539/C53F/C54D…）

## 安装

1. 下载或编译得到 `LogitechBattery.dll`
2. 复制到 TrafficMonitor 主程序目录下的 `plugins` 文件夹（没有就新建）
3. 重启 TrafficMonitor
4. 右键任务栏窗口 → **显示项目** → 勾选「罗技设备1电量」
   （或在 **其他功能 → 插件管理** 里确认插件已加载）

> ⚠️ **位数必须匹配**：TrafficMonitor 主程序是 64 位，就必须放 64 位 DLL；32 位主程序必须放 32 位 DLL。放错位数的现象是插件列表里看不到这个插件。

> 从旧版升级：1.0.x 的 `show_icon` 已废弃，插件会自动把没有 `display_mode` 键的旧配置当作「纯图标」处理；1.1.x → 1.2.x、1.2.x → 1.3.x 都不用改任何配置；1.3.0 只是新增 `display_mode=3`，旧的 0/1/2 语义不变。想回到文字显示就设 `display_mode=2`。

## 配置

### 在哪里改

不用手改文本了。右键任务栏窗口 → **其他功能 → 插件管理** → 选中「罗技设备电量」→ 点 **「选项」**，会弹出图形化设置窗口：

![设置窗口](docs/options_dialog.png)

- 上半部是**实时预览**（浅色 / 深色主题各一行），拖动滑块立刻看到效果
- 「鼠标大小」50~150%、「图标间距」0~24 px 用滑块调
- 「显示模式」下拉有 4 种数字呈现方式，「图标样式」「设备数量」也用下拉框选
- 点 **「应用」立即写入并刷新任务栏，窗口不关闭**——可以反复拖滑块、反复应用，不用一次次重开窗口
- 点「确定」保存并关闭；「取消」只关闭窗口，已经「应用」过的改动不会回滚（和 Windows 常规对话框一致）
- 想改刷新间隔等高级项，点「打开完整配置文件」用记事本编辑；这个按钮不会覆盖你在记事本里的改动

配置文件本身仍在主程序目录下 `plugins\` 文件夹中的 `LogitechBattery.ini`。插件每次 `DataRequired()` 都会比对它的修改时间并热重载，所以记事本改完也是几秒生效。首次运行自动生成，旧版升级时缺失的新键会自动补到文件末尾，原有值和注释都不动。只有 `device_count` 这类会改变显示项目数量的设置需要重启一次。

```ini
[general]
; 显示模式：0=仅图标（默认，鼠标+电池，任务栏上没有任何文字）
;          1=图标 + 右侧百分比数字
;          2=纯文字（标签 + 数值，退回主程序绘制）
;          3=百分比直接显示在电池内部（电池会自动加宽）
display_mode=0
; 图标样式：0=鼠标图标 + 横向电池（默认）
;          1=鼠标造型电池，电量自下而上填充
icon_style=0
; 鼠标图标与电池之间的留白（像素，96 DPI 下；8 约等于一个空格）
mouse_gap=8
; 鼠标图标大小（%，100 = 默认 14.5 px 高；可填 50~150，改完保存几秒生效）
mouse_scale=100
; 电量刷新间隔（秒），无线鼠标建议不低于 30
refresh_interval=60
; 重新扫描设备的间隔（秒）
rescan_interval=300
; 接收器插槽数量（Unifying 为 6，LIGHTSPEED 一般为 1）
max_slot=6
; 显示几个设备项目（1~3）
device_count=1
; 低电量提醒阈值（%），0 表示关闭
warn_threshold=10
; 低电量提醒冷却时间（秒）
warn_cooldown=21600
; 纯文字模式下标签是否显示设备名称（0=固定显示“鼠标电量”）
show_device_name=0
```

图标模式下想看精确数值，鼠标停在图标上即可；想一直看到数字就设 `display_mode=1`（图标右侧）或 `display_mode=3`（电池内部）。
觉得鼠标偏小/偏大就调 `mouse_scale`（100 = 当前大小，120 再放大 20%，最小 50、最大 150）。
觉得鼠标和电池挨得太紧就调大 `mouse_gap`（默认 8，设 0 会贴在一起，最大 32）。
把 `max_slot` 改成 `1` 可以让 LIGHTSPEED 接收器的扫描更快。

### 换成自己的鼠标图标

PNG 不是运行时读取的，而是转成蒙版数据编译进 DLL，所以最终只需要一个 DLL 文件：

1. 准备一张**透明（或纯白）背景、单色线条**的方形 PNG，粗线稿效果最好；彩色强调部分会被单独识别为一层
2. 把图标文件放到任意位置（例如 `assets\mouse.png`，该路径已被 gitignore 排除，不会随仓库分发）
3. 运行 `build\make_mouse_icon.exe <你的PNG路径>`（源码 `probe/make_mouse_icon.cpp`）
4. 重新执行 `build.bat`

工具会自动裁掉四周空白、等比缩放到 **64 像素高**（比早期的 48 保留更多边缘信息），生成 `src/mouse_icon_data.h/.cpp`：主体线条的 alpha 蒙版 + 彩色强调层蒙版。绘制时主体染成任务栏文字色，强调层保留原色，因此深浅主题都清晰。这也避免了直接贴 PNG 在深色任务栏上变成「黑线看不见」或「白底方块」的问题。

**线宽会自动判断**：工具统计下采样后实心墨点的占比，低于 0.20 判定为细线稿并做一次轻度膨胀加粗；粗线稿（如当前的 512px 图标）原样保留，不会膨胀糊成一团。所以换成任何风格的素材都不用手调参数。生成时会打印 `ink density` 和是否膨胀，方便核对。

想改默认大小，改 `src/battery_icon.cpp` 里 `DrawMouseAndBattery()` 的 `mh`（当前 `14.5f * s`）并同步调整 `BaseWidth()` 里的 `14.5f`；日常微调直接用设置窗口或 ini 的 `mouse_scale` 即可，不用重新编译。

## 编译

三种方式任选其一。

### 1. 脚本（最快）

```bat
build.bat          :: 生成 dist\LogitechBattery.dll（x64）
build.bat x86      :: 生成 32 位版本（需要 i686 版 MinGW）
```

脚本优先读取 `MINGW_GXX` 环境变量指定的编译器，其次尝试同级的 `..\tools\mingw\` 便携工具链，最后用 PATH 里的 `g++`。

### 2. CMake

```bat
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

输出 `build\Release\LogitechBattery.dll`。要 32 位就 `-A Win32`。
MSVC 下已自动加 `/MT`，不依赖 VC 运行库。

### 3. Visual Studio

新建「动态链接库(DLL)」项目，把 `src` 下所有 `.cpp/.h` 加进去（含自动生成的 `mouse_icon_data.cpp` 和 `OptionsDialog.cpp`），链接
`setupapi.lib hid.lib shell32.lib user32.lib gdi32.lib gdiplus.lib comctl32.lib` 即可。

> MinGW 下需要在包含 `gdiplus.h` 之前包含 `wtypes.h`（`PROPID` 类型），`src/battery_icon.h` 已经处理好了。

## 实现说明

### 整体结构

```
src/
  PluginInterface.h          TrafficMonitor 官方插件接口（API v8，未修改）
  hid_logitech.h/.cpp        罗技 HID++ 通信层（枚举、报文收发、电量解析）
  battery_icon.h/.cpp        图标绘制（GDI+ 抗锯齿，两种造型 / 三种状态配色）
  mouse_icon_data.h/.cpp     鼠标 PNG 转成的内嵌蒙版（自动生成，勿手改）
  OptionsDialog.h/.cpp       图形化设置窗口（纯 Win32 控件 + 实时预览）
  LogiBatteryPlugin.h/.cpp   插件本体（ITMPlugin + IPluginItem）
  dllmain.cpp                DLL 入口，导出 TMPluginGetInstance，保存 DLL 模块句柄
assets/NOTICE.md             素材来源与授权声明（原始 PNG 不随仓库分发）
probe/                       开发期用的探针、宿主测试、预览生成（不参与插件编译）
docs/icon_preview.png        图标效果预览
docs/options_dialog.png      设置窗口截图（默认状态）
docs/options_dialog_inside.png   设置窗口：电池内数字模式
```

### 图标是怎么画出来的

`battery_icon.cpp` 只依赖 `Gdiplus::Graphics`，插件（`DrawToHDC` → HDC）和预览程序（→ Bitmap）共用同一个 `Draw()`，所以预览图就是实际渲染结果。

- 默认样式的鼠标蒙版来自一张 512×512 粗线稿（原始 PNG 不随仓库分发，见 `assets/NOTICE.md`）：工具裁掉透明边距、等比缩到 64 高，生成 `kMouseInk` 主体蒙版；若素材带彩色像素，再生成 `kMouseAccent` 强调层并用 `kMouseHasAccent` 标记（当前图标是单色，第二层不绘制）。绘制时用 `HighQualityBicubic` 缩放到 14.5 px 高（电池为 11 px 高，鼠标略高一点更醒目），该基准值还会乘上 `mouse_scale/100`，alpha 提升 ×1.4 让边缘保持实心
- 生成器按下采样后的墨点密度自动决定是否膨胀：细线稿（密度 < 0.20）做一次四邻域加权膨胀加粗，粗线稿原样保留。实测旧素材 0.145 → 膨胀，当前素材 0.238 → 不膨胀
- 鼠标和电池之间的留白由 `mouse_gap` 决定（默认 8 px，约一个空格），`BaseWidth()` 会把它算进自绘区域宽度
- `icon_style=1` 不用 PNG 素材，改用三段贝塞尔曲线画水滴形外壳并自下而上填充
- 电池是圆角矩形 + 右侧实心正极；填充用 `SetClip(电池路径)` 裁剪，保证圆角内不会溢出
- `display_mode=3` 时电池从 `18×11` 变为 `24×13`（单位 `s`），用 `GraphicsPath::AddString` 画居中的粗体**纯数字**（不带 `%`，省字符换更大的字号）。字号按 `wcslen(text)` 自适应：1~2 位 `9.5*s`、3 位 `8.6*s`、4 位（`100+`）`7.4*s`，描边宽度也随之取 `em*0.085`，避免极端情况把 `+` 裁掉。文字取主题色 `ink`，描边按 `ink` 亮度取反色（浅色主题黑字白边 / 深色主题白字黑边）；**先 `DrawPath` 描边、再 `FillPath` 填字**，否则描边会压在笔画上让细字发灰。充电时文字追加 `+`，无数据显示 `?`，并且不再画闪电/问号避免拥挤；`BaseWidth()` 会同步把电池宽度算进去
- 宽度由 `GetItemWidth()`（96 DPI 基准，主程序放大）和 `GetItemWidthEx()`（实际像素）共同给出；`Draw()` 还会按传入的 `w`/`h` 自适应缩放，主程序用逻辑坐标还是物理坐标都不会被裁切
- GDI+ 首次使用时才初始化（函数内静态对象），**不在 `DllMain` 里调用**，避免 loader lock 问题；万一初始化失败会退化成 GDI 画的简版电池
- **主程序会缓存项目宽度**：TrafficMonitor 在 `CTaskBarDlg::CalculateWindowSize()` 里通过 `GetItemWidth/Ex` 算好 `m_item_widths`，之后 `DrawPluginItem` 把缓存的 `rect.Width()` 传给 `DrawItem`；这个重算只在主程序 `ApplySettings()` 时发生。所以插件改大 `mouse_gap` 后，绘制区宽度不会立刻跟着变。若 `Draw()` 直接用 `need = BaseWidth(opt)*s > w` 就整体缩小 `s`，鼠标会被压到 10px 以下、线条断裂（视觉上像"显示不完全"）
- 因此 `Draw()` 会先把 `w / scale` 换算成 96 DPI 基准宽度 `w_base`：只有 `BaseWidth(opt) > w_base`（按 1:1 都放不下）时，才把超出的量从**纯留白 `gap`** 里扣掉，构造 `draw_opt` 用于绘制；随后仍保留 `need > w` 的整体缩放兜底。这样鼠标和电池尺寸恒定，而主程序给足宽度（例如重启后重新测量）时用户设的间距完全生效。`probe/gapscan.cpp` 可扫描 0~24 全部间距验证三种宽度场景

### 设置窗口是怎么实现的

`ShowOptionsDialog()` 不再打开记事本，而是弹出纯 Win32 模态窗口（`OptionsDialog.cpp`），不用 MFC、不用 .rc 资源脚本，三种构建方式都能直接编译：

- 控件用 `CreateWindowExW` 手工创建（滑块是 comctl32 的 trackbar，其余是标准控件），窗口类用 DLL 自己的 HINSTANCE 注册——模块句柄由 `dllmain.cpp` 在 `DLL_PROCESS_ATTACH` 时保存，不能用主程序 exe 的
- 模态循环自己跑 `GetMessage`，结束时用 `PostMessage` 唤醒，**绝不调用 `PostQuitMessage`**（那会顺带退出主程序的消息循环）；父窗口临时禁用，关闭后恢复
- 预览直接调用与插件相同的 `icon::Draw()`，所见即所得；滑块/下拉变化只重绘预览区
- 保存走自写的 `WriteIniValues()`：逐行替换已有键值，保留 UTF-8 BOM、中文注释和其他设置。**不能用 `WritePrivateProfileStringW`**，它会按 ANSI 重写文件、把中文注释弄乱
- 「应用」按钮通过回调把当前值交回插件的 `ApplyOptions()`：写 ini → `LoadConfig()` → `RefreshDisplay()` → 对主窗口/任务栏窗口 `InvalidateRect + UpdateWindow` 请求立刻重绘，同时同步 ini 时间戳避免热重载重复加载。**窗口不关闭**，可以连续调整；提示行会显示「已应用到任务栏」
- 点过「应用」后再点「取消」不回滚，`Show()` 仍返回"有变化"；点「确定」会再幂等保存一次。最终返回 `OR_OPTION_CHANGED`；原有的 ini 热重载机制照常工作

### 为什么不能只在 Feature Report 上收发

实测这台机器上的 LIGHTSPEED 接收器：两个 HID 集合的 `FeatureReportByteLength` 都是 0，只有输出/输入报告（7 字节和 20 字节）。所以必须走 `WriteFile` / `ReadFile`，并且**报文长度要和集合匹配**：

- 短报文：report id `0x10`，7 字节（HID++ 1.0 寄存器访问）
- 长报文：report id `0x11`，20 字节（HID++ 2.0 特性访问）

代码里两种通道都会尝试，成功一次后锁定策略，兼容部分设备能在 Feature Report 上通信的情况。

### 报文格式（HID++ 2.0）

```
[0] report id (0x11)
[1] device index   (接收器下 1~6，直连设备 0xFF)
[2] feature index  (由 IRoot.getFeature 查到)
[3] function<<4 | software id
[4..] 参数
```

应答校验规则（来自 Solaar 源码与实测）：

- 正常应答：`rsp[2]==req[2] && rsp[3]==req[3]`
- 2.0 错误：`rsp[2]==0xFF`，错误码在 `rsp[5]`
- 1.0 错误：`rsp[2]==0x8F`，错误码在 `rsp[5]`

### 电量特性（按优先级回退）

| Feature | 用途 | 解析 |
|---|---|---|
| `0x1000` BATTERY_STATUS | 电量百分比 | `rsp[4]`=电量%，`rsp[6]`=状态（1 充电 / 2 将充满 / 3 已满） |
| `0x1004` UNIFIED_BATTERY | 新设备（本机实测命中） | `rsp[4]`=电量%，`rsp[5]`=档位，`rsp[6]`=状态 |
| `0x1001` BATTERY_VOLTAGE | 只有电压的设备 | `rsp[4..5]`=毫伏（大端），`rsp[6]`=标志，用分段线性表换算百分比 |
| 寄存器 `0x07` | HID++ 1.0 老设备兜底 | 短报文读取 |

本机设备实测只支持 `0x1004`，所以三级回退是必要的——不能只写一个特性。

设备名走 `0x0005` DEVICE_NAME：`fn0` 取长度，`fn1` 按偏移读 16 字节分片。
注意有的设备分片里带长度前缀、有的不带（本机就不带），代码用「首字节 ≤16 视为长度」来兼容。

### 为什么不卡界面

TrafficMonitor 的 `DataRequired()` 在主线程按秒调用。如果一次调用里做完整的设备扫描（6 个插槽 × 多个接口 + 超时），会冻结界面。

所以插件把工作拆成状态机：每次 `DataRequired()` 最多探测 3 个插槽或查询 1 个设备，其余留到下一次调用。实测首次出数据约 2~3 秒，之后只在刷新间隔（默认 60 秒）到点时才访问硬件。

此外：

- 读取失败时保留 10 分钟内的旧数值——无线鼠标休眠不响应是常态，没必要立刻把图标变成问号
- 在长报文通道上找到设备后，直接跳过同一接收器的其他集合，扫描成本减半

## 常见问题

**插件列表里看不到 / 勾选里没有「罗技设备电量」**
DLL 位数和主程序不一致，或 DLL 放错目录（必须是主程序目录下的 `plugins`）。也可能缺少 UCRT（Win10 及以上自带）。

**图标里是个问号**
接收器没插、设备关机/休眠，或设备不在支持列表。先看悬停提示：提示「未检测到设备」说明枚举阶段就没找到 HID++ 端点。首次启动前 2~3 秒也是问号，属于正常扫描过程。

**任务栏上怎么没有数字？**
这是默认设计（`display_mode=0`）。鼠标悬停看提示，或改成 `display_mode=1`（图标右侧数字）/ `display_mode=3`（电池内数字）/ `display_mode=2`（纯文字）。

**和 G HUB 冲突吗？**
不冲突，两者可以同时读取。实测 G HUB 运行时本插件照常工作。

**电量数值和 G HUB 显示不一致**
正常。G HUB 有时用电压估算，本插件优先用设备上报的百分比。充电时数值变化慢是因为设备本身的汇报粒度。

**刷新太慢 / 太频繁**
改 `refresh_interval`。不建议低于 30 秒，无线设备频繁唤醒会略微增加耗电。

## 调试工具

`probe/` 下有几个开发期程序，排查问题时很有用：

- `probe.cpp` 完整流程：枚举接口 → 扫插槽 → 打印所有特性原始报文 → 走一遍电量解析
- `scan.cpp` 对某个接口把 device index 0x00~0xFF 全 ping 一遍，看谁应答
- `diag.cpp` 底层 I/O 诊断，逐步打印 WriteFile/ReadFile 的错误码和原始字节
- `host_test.cpp` 模拟 TrafficMonitor 加载 DLL 并驱动它，含自绘渲染的像素级与 ASCII 校验
- `preview.cpp` 调用同一份绘制代码生成 `docs/icon_preview.png`（含深浅主题、各种电量、1:1 大小）
- `gdi_diag.cpp` 校验 GDI+ 填充是否真的写入 DIB / DDB 内存 DC
- `make_mouse_icon.cpp` 把 PNG 转成 `src/mouse_icon_data.*`，换图标时用它
- `zoom.cpp` 把任务栏截图按最近邻放大，用来检查 1:1 尺寸下的线条粗细
- `dlgtest.cpp` 自动弹出设置窗口、模拟调滑块、截图并点「应用」/「取消」，校验 ini 写入与即时生效
- `gapscan.cpp` 扫描 `mouse_gap` 0~24 在三种绘制区宽度下的渲染拼图，专门回归「改间距导致图标被缩小」的问题

编译示例：

```bat
g++ -std=c++17 -O2 -o build\preview.exe probe\preview.cpp src\battery_icon.cpp src\mouse_icon_data.cpp -lgdiplus -lgdi32 -luser32
g++ -std=c++17 -O2 -o build\make_mouse_icon.exe probe\make_mouse_icon.cpp -lgdiplus -lgdi32 -lshell32
build\make_mouse_icon.exe <你的PNG路径>
g++ -std=c++17 -O2 -o build\probe.exe probe\probe.cpp src\hid_logitech.cpp -lsetupapi -lhid
```

凡是直接编译 `src/battery_icon.cpp` 的程序，都要一起加上 `src/mouse_icon_data.cpp`，否则会报找不到鼠标蒙版数据。

## 许可与素材

代码部分 MIT。`src/PluginInterface.h` 版权归 TrafficMonitor 作者 Zhong Yang 所有，接口定义未做修改。

两张原始鼠标 PNG（第一版来自爱给网 aigei.com，第二版来源未确认）**不随本仓库分发**，详见 `assets/NOTICE.md`。但 `src/mouse_icon_data.*` 是从其中一张提取的衍生蒙版，**不属于本项目的 MIT 授权范围**。若要再分发或商用，建议用自行绘制或有明确授权的图标重新生成蒙版数据（替换方法见上文「换成自己的鼠标图标」）。
