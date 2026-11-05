# 第三方驱动说明

MiniHUD 不内置 PawnIO 安装器（v0.1.2 起）。缺少驱动时，程序在“电脑配置”页提供官方下载页链接，由用户自行下载并安装官方签名的 PawnIO 2.2.0。构建时可用 `-DFPS_MONITOR_BUNDLE_PAWNIO=ON` 重新内嵌，安装器保持二进制原样，仅在用户确认安装时运行。

- 安装器及版本说明：https://github.com/namazso/PawnIO.Setup/releases/tag/2.2.0
- 安装器 SHA-256：`1f519a22e47187f70a1379a48ca604981c4fcf694f4e65b734aaa74a9fba3032`
- 驱动源代码及 GPL-2.0-or-later 许可证：https://github.com/namazso/PawnIO
- 官方模块源代码及许可证：https://github.com/namazso/PawnIO.Modules
- 官方下载站：https://pawnio.eu/

第三方代码与二进制的版权归原作者所有。MiniHUD 源码及其上游的 AGPL-3.0 许可证见 LICENSE；原项目完整说明见 UPSTREAM.md。

## 安装包

安装包由 Inno Setup 6 生成。安装界面的简体中文语言文件来自 [kira-96/Inno-Setup-Chinese-Simplified-Translation](https://github.com/kira-96/Inno-Setup-Chinese-Simplified-Translation)，MIT License，Copyright (c) 2019 - 2020 kirakira。
