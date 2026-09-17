# 我应该上传哪些文件？

**请用 Git 的忽略规则上传，不要在网页中把整个 I 盘目录全选拖进去。**

| 内容 | 上传方式 |
|---|---|
| 根 README、LICENSE、第三方声明、.gitignore | Git 源码仓库 |
| docs（含 PNG 图片、思维导图和教程） | Git 源码仓库 |
| versions 中的每版 README、project 内源码/资源/构建脚本/锁文件 | Git 源码仓库，遵循忽略规则 |
| shared、extras、licenses 中的源码与声明 | Git 源码仓库 |
| catalog 的版本、复制、校验清单 | Git 源码仓库，遵循忽略规则 |
| tools 中的整理/校验/打包脚本 | Git 源码仓库 |
| versions 中的 dist、EXE、DLL、Electron 运行包、历史 ZIP | GitHub Releases 附件 |
| .organizing-drafts、缓存、日志、用户配置、IPC 凭据 | 不上传 |
| .git | Git 本地管理目录，不在网页中上传 |

`git add .` 会遵循 `.gitignore`，网页拖拽不会自动替你遵循这些规则。运行 `git status --short` 和 `git diff --cached --stat` 查看暂存范围。不要用 `git add -f` 批量绕过规则。

成品虽被 Git 忽略，仍保留在这个本地归档中。接收源码的人从 GitHub Releases 下载运行包，或按构建说明自行编译；这两条路径在 README 已区分。

最新主 EXE 大于 100 MiB，不适合普通 Git 对象。使用 `python tools/package_release.py --version 1.3.1alpha` 生成 Release 附件，再上传 GitHub Release。具体操作见 [发布说明](GITHUB_PUBLISH.md)。
