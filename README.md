# MiniHUD

Windows 游戏性能小黑卡。默认在屏幕左上角用单行显示帧率、CPU 温度、GPU 温度和显存已用/总量。卡片无边框、点击穿透，优先支持无边框窗口游戏；不向游戏进程注入代码。

## 下载

[下载 Windows x64 EXE](https://github.com/YishV/MiniHUD/releases/latest/download/MiniHUD.exe) · [查看所有版本](https://github.com/YishV/MiniHUD/releases)

便携使用：把 EXE 放到固定目录后运行。发布页提供 ZIP 包、源代码和 SHA-256 校验文件。

![MiniHUD 猫咪图标](resources/cat-preview.png)

## 运行

双击 `release\MiniHUD.exe`。首次运行若弹出 Windows 管理员提示，需要同意才能采集 ETW 帧率。`Ctrl+Shift+F12` 显示或隐藏小黑条；黑条显示时点击托盘图标打开设置，右键可隐藏或退出。隐藏黑条时托盘图标也隐藏，再按相同快捷键即可恢复两者；也可再次运行程序打开设置。

中文设置窗口的 **显示设置** 页可开关指标，切换单行/多行（每行 1–8 项），切换中文 / English 指标，调整指标水平间距、行间距、位置、大小、透明度和刷新间隔；**告警阈值** 页可修改各项标红阈值；**电脑配置** 页显示 CPU、GPU、内存和磁盘型号及传感器状态。关闭设置时保存到程序旁的 `config.ini`。

| 指标 | 数据来源 | 默认 |
|---|---|---|
| FPS | Windows ETW | 开 |
| CPU 温度 | 已安装的 PawnIO 驱动；Intel 路径仍为实验性 | 开 |
| GPU 温度、显存 | NVIDIA NVML、AMD ADL 或 Windows 回退源 | 开 |
| RAM 已用/总量 | Windows 内存 API | 关 |
| SSD 温度 | Windows 存储温度属性；设备不支持时显示 N/A | 关 |
| 磁盘读写速度 | Windows PDH 所有物理磁盘总吞吐 | 关 |
| 网速上传/下载 | Windows 活跃网卡累计字节差分 | 关 |

当前 CPU 温度采集通过 [PawnIO](https://github.com/namazso/PawnIO) 驱动实现。发布版不内置安装器：内嵌的安装器会让未签名的 EXE 带上“体内藏一个可执行文件、释放到临时目录再运行”的特征，被杀软误报为木马（v0.1.1 曾被 Defender 判为 `Trojan:Win32/Wacatac.H!ml`）。缺少驱动时在“电脑配置”页点击“下载 PawnIO 驱动”打开官方下载页，安装完成后关闭并重新打开设置即可重新检测。安装需要管理员权限；已安装兼容驱动时不重复提示。未安装时其余指标照常可用。温度与速率采集取决于设备和驱动支持。独占全屏游戏不保证显示。

## 登录自启动与图标

在“显示设置 → 行为”中勾选“登录 Windows 后自动启动”。新版首次运行默认启用并更新启动任务；之后在设置里主动关闭会保留选择。开关读取和更新当前用户的 Windows 计划任务状态。任务仅在当前用户登录约 8 秒后，以交互式管理员权限运行当前程序并传入 `--autostart`；黑条和托盘图标自动显示。快捷键隐藏黑条时托盘也隐藏，再按快捷键同时恢复。手动运行与自启的显示逻辑一致。不保存账户密码。关闭开关会删除 MiniHUD 自己的启动任务。程序移动后，设置会提示更新启动路径。请从固定安装目录启用；Windows 自身的节能或组织策略仍可能影响启动。

图标是原创白底深灰线条猫，托盘和 EXE 使用同一组 16–256 像素图标。SVG 源文件在 `resources/icon.svg`，执行 `python tools/make_icon.py` 或 `powershell -File tools/make_icon.ps1` 可重新生成 ICO，不依赖额外 Python 包。

## 告警与布局

默认 FPS 低于 45、帧时间达到 33.3 ms、CPU 占用达到 95%、CPU 温度达到 90℃、GPU 温度达到 85℃、SSD 温度达到 70℃、内存/显存占用达到 90% 时，数值变为红色。GPU 利用率、功耗与读写速度没有通用异常上限，默认不告警，可自行设置。阈值填 0 关闭单项告警，也可关闭全部告警；无数据不会误报。温度阈值始终使用摄氏度。

单行放不下屏幕时自动换行。旧版配置首次加载时迁移到左上角单行，之后保留用户选择。不包含网络延迟功能。

## 编译

需要 Visual Studio 2022 C++ 工具链和 CMake 3.24 以上。首次配置会下载固定版本的 Dear ImGui、PawnIO 模块与 AMD ADL SDK 头文件。默认不把官方 PawnIO 2.2.0 安装器打进 EXE——内嵌后未签名 EXE 容易被 Defender 判成 `Trojan:Win32/Wacatac.H!ml`；需要离线自包含版本可改为 `ON`，构建时仍会验证安装器 SHA-256：

```powershell
cmake -S . -B build\vs2022-final -G "Visual Studio 17 2022" -A x64
cmake --build build\vs2022-final --config Release --parallel 4
New-Item -ItemType Directory release -Force | Out-Null
Copy-Item build\vs2022-final\Release\MiniHUD.exe release\MiniHUD.exe
```

## 来源与许可

本项目基于 [andrei-cb/fps_monitor](https://github.com/andrei-cb/fps_monitor) 的 `4b9ae7b` 版本，保留 [原项目说明](UPSTREAM.md) 与 [AGPL-3.0 许可证](LICENSE)。新增 Windows 磁盘和网络采集、电脑配置页及简洁卡片布局。分发修改版时遵守 AGPL-3.0，保留源码和版权信息。

## 第三方驱动

PawnIO 安装器保持官方原样，来源为 [PawnIO.Setup 2.2.0](https://github.com/namazso/PawnIO.Setup/releases/tag/2.2.0)。驱动源代码和许可证见 [PawnIO](https://github.com/namazso/PawnIO)，模块源代码见 [PawnIO.Modules](https://github.com/namazso/PawnIO.Modules)。安装器内嵌不会增加常驻进程；只有用户确认安装时才释放运行。
