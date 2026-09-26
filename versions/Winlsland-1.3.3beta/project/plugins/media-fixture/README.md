# 多媒体/交互皮肤 SDK 示例

适用 WinIsland 1.2.7alpha-r1-dynamic-media-20260914。通过 winisland.scene 与 winisland.media ABI 1 运行，需要宿主，不是独立 EXE。

导入 built/media-fixture.wimod 可看到透明 PNG、多帧 GIF 和 H.264 视频，皮肤主动请求 640×352 DIP。按钮控制 Play/Pause、Seek、Stop、Loop、Reload GIF、Local file；PNG可拖，进度条支持拖动/左右键。默认静音。

权威源码 source/tests/skin-example.cpp，构建 source/tests/build-skins.ps1；实际 SDK 会从宿主 source/src 复制。assets 已提供，重新生成可运行 generate-assets.py [测试ffmpeg绝对路径]（需要Pillow）。运行不依赖Pillow/FFmpeg。

旧 source/tests/media-fixture.cpp 与 build-media-fixture.ps1 属于静态原型存档，不是 r1 发布构建入口；不要用它们覆盖最终wimod。宿主媒体与scene文档位于../../docs。

示例源码和本地生成的几何/色块/正弦测试资源可以自由使用、修改和分发。此授权不替代WinIsland或第三方工具自身许可证。
