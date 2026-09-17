# GitHub 上传与成品发布

本目录是“源码仓库 + 本地发行档案”。根 README、versions 中源码、SDK、文档与小型图片可以进入 Git；大型成品应发布到 GitHub Releases。GitHub 普通 Git 对单文件有 100 MiB 上限，不能把约 162 MiB 的 1.3.1alpha 主 EXE 直接 git push。

## 1. 提交源码和说明

在 `I:\gtb开源` 检查根 LICENSE、邮箱、目录及版本索引后：

```powershell
git init -b main
git status --short
git add README.md LICENSE THIRD_PARTY_NOTICES.md .gitignore docs versions shared extras licenses catalog tools 版本导航.html
git diff --cached --stat
git commit -m "Archive WinIsland community releases and source"
```

根 `.gitignore` 会跳过发行二进制、运行目录、复制草稿和已知敏感运行文件。不要使用 `git add -f` 批量强制加入被忽略的文件。不要把本机 diagnostics、token、通知数据或日常配置补进源码仓库。`catalog/discovery.json` 含本地盘点绝对路径，默认忽略；可提交的来源清单使用工作区相对路径。

在 GitHub 创建一个自己的空仓库，复制该仓库给出的 HTTPS 或 SSH remote 地址，再按网页提示设置 remote/push。此处不编造账号、仓库地址或已经公开的下载链接。

## 2. 将成品作为 Release 附件

每个正式版本可以使用一个标签，例如 `v1.3.1alpha`。不要用内部修复目录名充当正式产品版号。

```powershell
python tools/package_release.py --version 1.3.1alpha
```

脚本只读取目标归档里的所选 `dist/`，在 `release-assets/` 生成 ZIP、SHA256 和版本说明，同时附根 LICENSE、第三方说明和集中许可。它不会上传、不修改原工程、不执行 EXE。历史缺少依赖或源码的情况写在版本 README，打包不代表自动补全这些缺失。

去 GitHub 的 Releases → Draft a new release，选择对应标签，填写版本说明，再上传生成的包和校验文件。必要时可用 Git LFS 管理大型资产，但不要求把每版成品放入 Git 历史。

## 3. 许可与依赖

根 BSD-3-Clause 覆盖项目自身。保留 Qt、GSAP、Electron、QuickJS、QCloudMusicApi、Crypto++ 等第三方声明；针对实际发布的二进制核对 LGPL 等义务，不把“有 BSD LICENSE”误认为全部依赖都已满足再分发条件。详情见根 THIRD_PARTY_NOTICES。

## 4. 发布前检查

```powershell
python tools/verify_archive.py
git status --short
```

检查总 README 图片是否显示、版本表是否新到旧、只留最新构建、EXE SHA 是否符合清单。确认没有遗漏你需要提供的历史源码；当前明确的源码缺失项须继续在 README 保留。

初始整理阶段未上传。后续实际发布记录以 [整理与发布报告](整理与发布报告.md) 及 `catalog/github-publication.json` 为准。联系方式：`725513212@qq.com`。
