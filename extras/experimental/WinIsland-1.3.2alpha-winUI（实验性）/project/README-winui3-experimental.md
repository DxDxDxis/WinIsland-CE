# WinIsland 1.3.2alpha WinUI 3（实验性）

这是基于 `1.3.2alpha` 的独立设置界面实验分支。分发时只需提供：

`release/WinIsland-1.3.2alpha.exe`

主 EXE 内嵌 WinUI 设置客户端和所需运行资源；首次打开设置时释放到统一数据目录。用户不需要手动复制 DLL，也不会在 EXE 所在目录散落配置、插件或缓存。

当前方案保留 C++ 宿主、插件 ABI、统一数据目录、音乐、歌词、通知和灵动岛核心。WinUI 客户端不直接加载插件 DLL，所有设置、插件操作和文件中转操作通过受限 IPC 完成。

实验性限制和验证状态见 `docs/winui3-final-verification.md`。
