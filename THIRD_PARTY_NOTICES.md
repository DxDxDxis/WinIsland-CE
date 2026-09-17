# 第三方组件与许可边界

WinIsland 社区项目自身按根目录 [BSD-3-Clause](LICENSE) 开源。该许可不替代第三方作者的原许可，也不抹除其版权声明。历史成品按原字节归档，没有更换里面的第三方组件。

| 组件 | 实际用途 | 归档中的许可依据 |
|---|---|---|
| QuickJS-NG | 插件 JavaScript 运行时 | `licenses/current-host/QuickJS-NG.txt`；各版本 `source/third_party/quickjs-ng/LICENSE` |
| miniz | 插件包 ZIP 解压 | `licenses/current-host/miniz.txt` 和各版本 vendored 文件中的声明 |
| GSAP 3.12.5 | 现有插件动画脚本运行时 | `licenses/current-host/GSAP-README.md`、各版本 `source/third_party/gsap/package/package.json`；该包写的是 GreenSock standard no-charge license，**不是 BSD/MIT** |
| QCloudMusicApi | 歌词辅助服务的网易云 API 封装 | `licenses/lyric-provider/QCloudMusicApi-MIT.txt`，以及 `shared/lyric-provider-source/third_party/QCloudMusicApi/LICENSE` |
| Crypto++ | 歌词辅助程序加密依赖 | `licenses/lyric-provider/CryptoPP-License.txt`，构建配套声明 `cryptopp-cmake-BSD.txt` |
| Qt 6.8.3 Core / Network | 歌词辅助程序网络及基础库 | `licenses/lyric-provider/LGPL-3.0.txt`、`GPL-3.0.txt`、`NOTICE.txt`、`qtbase-6.8.3.spdx.json`；按实际模块和选择的许可履行义务 |
| Electron / Chromium / Node.js | TypeScript 设置界面运行环境 | 成品 settings 目录内的 `LICENSE`、`LICENSES.chromium.html` 等原始声明；具体包版本由本版本 `package-lock.json` 确定 |

本归档保留了可找到的第三方源码、说明和许可证。Qt SDK、MSVC、Windows SDK、npm 安装缓存不复制到源码仓库；构建者自行安装相应工具。歌词服务的项目源码与原构建脚本另外集中于 `shared/lyric-provider-source/`。

“项目开放源码”不等于“全部第三方依赖也改用 BSD”，也不等于历史每个二进制都已有可严格匹配的源码快照。缺失项见 [版本索引](docs/VERSIONS.md) 与 [归档说明](docs/ARCHIVE.md)。

发布二进制时，应把对应的第三方声明一起附上，并检查 Qt 等组件的再分发及源码提供要求。当前找到的 GSAP 3.12.5 包声明应按该历史包记录处理，不能用其他版本的新政策直接覆盖。没有在本次整理中替换依赖或重新授权第三方文件。

根许可证沿用原 GitHub 仓库的版权署名 `DxDxDxis`，联系地址 `725513212@qq.com`；既有文件中的原作者署名保留。
