# MiniHUD

Windows 游戏性能小黑卡。默认在屏幕左上角用单行显示帧率、1% / 0.1% 低帧、CPU 温度、GPU 温度和显存已用/总量。卡片无边框、点击穿透，优先支持无边框窗口游戏；不向游戏进程注入代码。

## 下载

[下载 Windows 安装包](https://github.com/YishV/MiniHUD/releases/latest/download/MiniHUD-Setup.exe) · [查看所有版本](https://github.com/YishV/MiniHUD/releases)

安装包按当前用户装到 `%LocalAppData%\Programs\MiniHUD`，不需要管理员权限即可安装；会创建开始菜单快捷方式和可选的桌面图标，可在「设置 → 应用」里卸载。安装完成后启动 MiniHUD 时仍需同意 Windows 管理员提示。喜欢绿色的可在发布页下载 `MiniHUD-v*-win-x64.zip`，解压后放到固定目录运行；发布页同时提供 SHA-256 校验文件。

![MiniHUD 猫咪图标](resources/cat-preview.png)

## 运行

从开始菜单启动 MiniHUD（免安装版直接双击 `MiniHUD.exe`）。首次运行若弹出 Windows 管理员提示，需要同意才能采集 ETW 帧率。`Ctrl+Shift+F12` 显示或隐藏小黑条；黑条显示时点击托盘图标打开设置，右键可隐藏或退出。隐藏黑条时托盘图标也隐藏，再按相同快捷键即可恢复两者；也可再次运行程序打开设置。

中文设置窗口的 **显示设置** 页可开关指标，切换单行/多行（每行 1–8 项），切换中文 / English 指标，调整指标水平间距、行间距、位置、大小、透明度和刷新间隔；**告警阈值** 页可修改各项标红阈值；**性能记录** 页用于录制一段游戏过程并生成报告；**电脑配置** 页显示整机厂商与型号、主板、BIOS、CPU 核心/线程与频率、每条内存的厂商型号频率、显卡与显存、硬盘型号容量与接口、显示器分辨率刷新率，以及各传感器采集状态，并可导出诊断信息。设置保存在 `%APPDATA%\MiniHUD\config.ini`，卸载程序不会删除它。从旧版本升级时，程序旁旧的 `config.ini` 会在首次启动时自动迁移过去并删除，下载目录不会再留下配置文件。

| 指标 | 数据来源 | 默认 |
|---|---|---|
| FPS | Windows ETW | 开 |
| 1% / 0.1% 低帧 | 最近 60 秒逐帧数据，最慢 1% / 0.1% 帧的平均帧率 | 开 |
| CPU 温度 | 已安装的 PawnIO 驱动；Intel 路径仍为实验性 | 开 |
| GPU 温度、显存 | NVIDIA NVML、AMD ADL 或 Windows 回退源 | 开 |
| RAM 已用/总量 | Windows 内存 API | 关 |
| SSD 温度 | Windows 存储温度属性；设备不支持时显示 N/A | 关 |
| 磁盘读写速度 | Windows PDH 所有物理磁盘总吞吐 | 关 |
| 网速上传/下载 | Windows 活跃网卡累计字节差分 | 关 |

当前 CPU 温度采集通过 [PawnIO](https://github.com/namazso/PawnIO) 驱动实现。发布版不内置安装器：在“电脑配置”页点击“安装 PawnIO 驱动”，程序会从官方地址下载安装器、校验 SHA-256 后以管理员权限静默安装；也可以点“手动下载”自己装。安装需要网络连接和管理员权限，完成后关闭并重新打开设置即可重新检测；已安装兼容驱动时不再提示。未安装时其余指标照常可用。温度与速率采集取决于设备和驱动支持。独占全屏游戏不保证显示。

## 登录自启动

在“显示设置 → 行为”中勾选“登录 Windows 后自动启动”。新版首次运行默认启用并更新启动任务；之后在设置里主动关闭会保留选择。开关读取和更新当前用户的 Windows 计划任务状态。任务仅在当前用户登录约 8 秒后，以交互式管理员权限运行当前程序并传入 `--autostart`；黑条和托盘图标自动显示。快捷键隐藏黑条时托盘也隐藏，再按快捷键同时恢复。手动运行与自启的显示逻辑一致。不保存账户密码。关闭开关会删除 MiniHUD 自己的启动任务。程序移动后，设置会提示更新启动路径。请从固定安装目录启用；Windows 自身的节能或组织策略仍可能影响启动。

## 告警与布局

默认 FPS 低于 45、帧时间达到 33.3 ms、CPU 占用达到 95%、CPU 温度达到 90℃、GPU 温度达到 85℃、SSD 温度达到 70℃、内存/显存占用达到 90% 时，数值变为红色。GPU 利用率、功耗与读写速度没有通用异常上限，默认不告警，可自行设置。阈值填 0 关闭单项告警，也可关闭全部告警；无数据不会误报。温度阈值始终使用摄氏度。

单行放不下屏幕时自动换行。旧版配置首次加载时迁移到左上角单行，之后保留用户选择。不包含网络延迟功能。

## 性能记录与诊断

在「性能记录」页点击「开始记录」，玩一段游戏后点「停止并生成报告」。停止时在 `%APPDATA%\MiniHUD\records` 生成两个文件：`MiniHUD-<时间>.csv`（每秒一行的明细，Excel 可直接打开）和同名 `.md` 报告（平均帧率、1% / 0.1% low、帧时间 P50/P95/P99、卡顿占比、CPU/GPU 占用与温度峰值、显存峰值，并附一段可读结论）。记录只保存在本机，不会上传。

程序会把启动、传感器初始化、驱动安装、记录等关键事件写入 `%APPDATA%\MiniHUD\MiniHUD.log`（超过 512 KB 自动轮转）。遇到异常时，在「电脑配置 → 诊断」点击「导出诊断信息」，会把版本、Windows 版本、制造商/主板/BIOS、传感器状态和日志尾部写入 `%APPDATA%\MiniHUD\diagnostics`，方便反馈问题。

## 编译

需要 Visual Studio 2022 C++ 工具链和 CMake 3.24 以上。首次配置会下载固定版本的 Dear ImGui、PawnIO 模块与 AMD ADL SDK 头文件。默认不把官方 PawnIO 2.2.0 安装器打进 EXE——内嵌后未签名 EXE 容易被 Defender 判成 `Trojan:Win32/Wacatac.H!ml`；需要离线自包含版本可改为 `ON`，构建时仍会验证安装器 SHA-256：

```powershell
cmake -S . -B build\vs2022-final -G "Visual Studio 17 2022" -A x64
cmake --build build\vs2022-final --config Release --parallel 4

# 安装包：需要 Inno Setup 6（winget install JRSoftware.InnoSetup）
& "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe" installer\minihud.iss `
  /DBuildDir=..\build\vs2022-final\Release /DAppVersion=1.1.1
```

生成的安装包在 `release\v1.1.1\MiniHUD-Setup.exe`，安装到 `%LocalAppData%\Programs\MiniHUD`。

## 来源与许可

本项目基于 [andrei-cb/fps_monitor](https://github.com/andrei-cb/fps_monitor) 的 `4b9ae7b` 版本，保留 [原项目说明](UPSTREAM.md) 与 [AGPL-3.0 许可证](LICENSE)。新增 Windows 磁盘和网络采集、电脑配置页及简洁卡片布局。分发修改版时遵守 AGPL-3.0，保留源码和版权信息。

## 第三方驱动

PawnIO 安装器保持官方原样，来源为 [PawnIO.Setup 2.2.0](https://github.com/namazso/PawnIO.Setup/releases/tag/2.2.0)。驱动源代码和许可证见 [PawnIO](https://github.com/namazso/PawnIO)，模块源代码见 [PawnIO.Modules](https://github.com/namazso/PawnIO.Modules)。MiniHUD 不内置安装器：只有你点击“安装 PawnIO 驱动”时才会从官方地址下载，固定 SHA-256 `1f519a22…` 校验通过后才以管理员权限运行；也可以自己从官方页面下载安装。驱动本身不增加常驻进程。
