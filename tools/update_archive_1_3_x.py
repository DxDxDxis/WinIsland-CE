from pathlib import Path
import hashlib, json, os, shutil, struct, datetime, re, time

SRC = Path(r"E:\aaAAx项目\WinIsland-社区版-大更新\release")
OUT = Path(r"I:\gtb开源")

VERSIONS = [
    ("1.3.4", "1.3.4社区版", "1.3.4-community-lyrics-20260925", True),
    ("1.3.3beta", "1.3.3beta", "1.3.3beta-installer-version-sync-20260920", False),
    ("1.3.2alpha", "1.3.2alpha", "1.3.2alpha-open-runtime-20260918", False),
]
SKIP_DIRS = {
    ".git", ".vs", "node_modules", "dependency-cache", "verification", "snapshots",
    "test-fixtures", "build-electron", "build-open-runtime", "build-translation",
    "obj", "bin", "target", "artifacts", "dist", "release", "__pycache__",
    "logs", "Cache", "Code Cache", "Crashpad", "staging", "temp", "migration",
}
SKIP_PREFIXES = ("build-", "verification-", "runtime-smoke", "component-test")
SKIP_EXTS = {".log", ".db", ".pdb", ".ilk", ".obj", ".pyc", ".dmp", ".user", ".suo"}
SKIP_NAMES = {"credentials.json", ".env", "install-location.json", "settings-connection.json", "host-state.txt"}

def sha(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest().upper()

def pe_timestamp(path: Path):
    try:
        with path.open("rb") as f:
            f.seek(0x3C)
            off = struct.unpack("<I", f.read(4))[0]
            f.seek(off + 8)
            ts = struct.unpack("<I", f.read(4))[0]
        return datetime.datetime.fromtimestamp(ts, datetime.timezone.utc).astimezone().isoformat()
    except Exception:
        return None

def copy_file(src: Path, dst: Path, records, category):
    dst.parent.mkdir(parents=True, exist_ok=True)
    digest = sha(src)
    if dst.exists():
        if not dst.is_file() or sha(dst) != digest:
            raise RuntimeError(f"refusing to overwrite different file: {dst}")
    else:
        shutil.copy2(src, dst)
    if sha(dst) != digest:
        raise RuntimeError(f"hash mismatch after copy: {src}")
    records.append({"source": str(src), "destination": str(dst.relative_to(OUT)).replace("\\", "/"), "bytes": src.stat().st_size, "sha256": digest, "category": category})

def wanted_file(path: Path) -> bool:
    if path.name in SKIP_NAMES or path.suffix.lower() in SKIP_EXTS:
        return False
    return True

def walk_source(src_root: Path, dest_root: Path, records):
    for base, dirs, files in os.walk(src_root):
        dirs[:] = [d for d in dirs if d not in SKIP_DIRS and not d.startswith(SKIP_PREFIXES)]
        for name in files:
            src = Path(base) / name
            rel = src.relative_to(src_root)
            if not wanted_file(src):
                continue
            # Source archives and generated installers belong in dist, not project.
            if src.suffix.lower() in {".exe", ".dll", ".msi", ".msix", ".zip", ".7z", ".rar"}:
                continue
            copy_file(src, dest_root / rel, records, "source/docs")

def copy_runtime(src_release: Path, dest_dist: Path, records):
    for base, dirs, files in os.walk(src_release):
        # release contents are intentional runtime files; only omit transient logs.
        dirs[:] = [d for d in dirs if d not in {"__pycache__", "verification", "logs", "Cache"}]
        for name in files:
            src = Path(base) / name
            if src.suffix.lower() in {".log", ".pdb", ".ilk", ".obj", ".pyc"}:
                continue
            copy_file(src, dest_dist / src.relative_to(src_release), records, "runtime")

def build_readme(version, folder_name, src_dir, row, records):
    exes = [r for r in records if r["category"] == "runtime" and r["destination"].lower().endswith(".exe")]
    lines = [
        f"# {folder_name}", "", "[返回项目总览](../../README.md)", "",
        "## 归档身份", "", f"- 正式版本：`{version}`",
        f"- 产品构建标识：`{row['buildId']}`",
        f"- 来源目录：`{src_dir}`",
        "- 本目录是从只读 release 源目录复制的独立开源归档；没有修改原工程。",
        "- 版本目录名保留 `Winlsland-` 归档前缀，产品和可执行文件名称仍为 `WinIsland`。", "",
        "## 内容", "",
        "- `project/`：源码、SDK、设置客户端、示例、构建脚本和开发文档。",
        "- `dist/`：对应 release 目录中的可运行构建产物；运行时文件保持相对目录。",
        "- `catalog/`：根目录维护来源、文件计数和哈希记录。", "",
        "## 安装与运行", "",
        f"从 `dist/` 解压并运行 `{exes[0]['destination'].split('/dist/',1)[-1] if exes else 'WinIsland-' + version + '.exe'}`。请保留同目录的 `settings/`、许可证和其他运行时文件；首次运行按该版本引导确认统一数据目录。不要将不同版本的 DLL、插件或用户配置混用。", "",
        "## 构建与兼容", "",
        "源码中的构建入口和原始说明按字节保留。1.3.2alpha/1.3.3beta/1.3.4 的插件 ABI、设置宿主和运行依赖以各自 manifest、SDK 与 README 为准；产品版号升级不自动改变 ABI。此次归档没有在新环境重新编译或宣称所有历史功能已通过完整回归。", "",
        "## 主程序哈希", "",
        "| 文件 | 字节 | SHA-256 | PE 链接时间 |", "|---|---:|---|---|",
    ]
    for e in exes:
        p = OUT / e["destination"]
        lines.append(f"| `{Path(e['destination']).name}` | {e['bytes']} | `{e['sha256']}` | `{row.get('peLinkTime','未读取')}` |")
    lines += ["", "## 许可与联系", "", "项目自身采用 BSD-3-Clause；第三方组件继续适用其原许可，见根目录 `THIRD_PARTY_NOTICES.md` 和 `licenses/`。联系：725513212@qq.com。", ""]
    (OUT / row["directory"] / "README.md").write_text("\n".join(lines), encoding="utf-8")

records = []
new_rows = []
for version, folder_name, build_id, stable in VERSIONS:
    src_dir = SRC / folder_name if (SRC / folder_name).is_dir() else SRC / version
    if not src_dir.is_dir():
        raise RuntimeError(f"missing source directory: {src_dir}")
    folder = OUT / "versions" / ("Winlsland-" + folder_name)
    folder.mkdir(parents=True, exist_ok=True)
    project = folder / "project"
    dist = folder / "dist"
    # Copy source and documentation, preserving relative paths.
    walk_source(src_dir, project, records)
    # Copy only the selected release payload; this is the built runtime, not build caches.
    release_dir = src_dir / "release"
    if not release_dir.is_dir():
        raise RuntimeError(f"missing release directory: {release_dir}")
    copy_runtime(release_dir, dist, records)
    exe_candidates = sorted([p for p in release_dir.glob(f"WinIsland-{version}.exe") if p.is_file()], key=lambda p: (pe_timestamp(p) or "", p.stat().st_mtime), reverse=True)
    if not exe_candidates:
        exe_candidates = sorted([p for p in release_dir.glob("WinIsland*.exe") if p.is_file()], key=lambda p: (pe_timestamp(p) or "", p.stat().st_mtime), reverse=True)
    if not exe_candidates:
        raise RuntimeError(f"no main executable in {release_dir}")
    exe = exe_candidates[0]
    row = {
        "version": version,
        "directory": str(folder.relative_to(OUT)).replace("\\", "/"),
        "sourceStatus": "已收录对应主线源码与 SDK；未在归档机重新构建",
        "sourceRoots": [str(src_dir.relative_to(SRC)).replace("\\", "/")],
        "buildId": build_id,
        "stable": stable,
        "selectedBuild": {"source": str(exe.relative_to(SRC)).replace("\\", "/"), "peLinkTime": pe_timestamp(exe), "sha256": sha(exe)},
        "note": "按该正式版 release 目录及主 EXE 的 PE 链接时间选择；实验 WinUI 分支单独保留为补充资料。",
    }
    row["peLinkTime"] = pe_timestamp(exe)
    row["executables"] = []
    for rec in records:
        if rec["destination"].startswith(row["directory"] + "/dist/") and rec["destination"].lower().endswith(".exe"):
            row["executables"].append({"path": rec["destination"][len(row["directory"])+1:], "bytes": rec["bytes"], "sha256": rec["sha256"]})
    row["files"] = sum(1 for rec in records if rec["destination"].startswith(row["directory"] + "/"))
    row["bytes"] = sum(rec["bytes"] for rec in records if rec["destination"].startswith(row["directory"] + "/"))
    row["buildTime"] = row["peLinkTime"]
    build_readme(version, folder_name, str(src_dir.relative_to(SRC)).replace("\\", "/"), row, records)
    new_rows.append(row)

# Append/update catalogue while retaining the historical rows exactly.
cat_path = OUT / "catalog/versions.json"
old = json.loads(cat_path.read_text(encoding="utf-8"))
by_version = {r["version"]: r for r in old}
for row in new_rows:
    by_version[row["version"]] = row

def version_key(v):
    m = re.match(r"^(\d+)\.(\d+)\.(\d+)(.*)$", v)
    if not m: return (0, 0, 0, 0, ())
    suffix = m.group(4)
    stage = 3 if suffix == "" else (2 if suffix.startswith("beta") else 1 if suffix.startswith("alpha") else 0)
    nums = tuple(int(x) for x in re.findall(r"\d+", suffix))
    return (int(m.group(1)), int(m.group(2)), int(m.group(3)), stage, nums)
ordered = sorted(by_version.values(), key=lambda r: version_key(r["version"]), reverse=True)
cat_path.write_text(json.dumps(ordered, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

# Rebuild the human-readable version index.
idx = ["# 版本索引（新 → 旧，每版仅最新构建）", "", "内部修复后缀不是正式版号。时间读取主 EXE 的 PE 链接记录，不是文件夹最后修改时间。`1.3.2alpha`、`1.3.3beta` 和 `1.3.4社区版` 是本次从 release 目录新增的主线归档。", "", "| 版本（新 → 旧） | 所选构建时间 | 源码情况 |", "|---|---|---|"]
for r in ordered:
    idx.append(f"| [{Path(r['directory']).name}](../{r['directory']}/README.md) | {r.get('peLinkTime') or r.get('buildTime') or '未读取'} | {r.get('sourceStatus','')} |")
idx += ["", "## 来源和完整性", "", "每个版本的 README 说明来源、运行内容与成品哈希。实验性 WinUI 分支不改写正式版号，作为 release 目录中的独立补充素材；旧同版修订不作为新的正式目录。"]
(OUT / "docs/VERSIONS.md").write_text("\n".join(idx) + "\n", encoding="utf-8")

# Update the root README's current-release wording and generated version table.
readme_path = OUT / "README.md"
text = readme_path.read_text(encoding="utf-8")
text = text.replace("1.1.1 社区版到 1.3.1alpha", "1.1.1 社区版到 1.3.4社区版")
text = text.replace("最新归档：**[Winlsland-1.3.1alpha](versions/Winlsland-1.3.1alpha/README.md)**，构建标识 `1.3.1alpha-settings-adjust-20260917`。它属于 alpha 阶段，不把“最新”表述为“所有环境均已验证”。", "最新归档：**[Winlsland-1.3.4社区版](versions/Winlsland-1.3.4社区版/README.md)**，构建标识 `1.3.4-community-lyrics-20260925`。它是当前最新正式社区版；1.3.2alpha 与 1.3.3beta 仍按预发布版本处理，不把“最新”表述为“所有环境均已验证”。")
text = text.replace("[下载发行版本](https://github.com/DxDxDxis/WinIsland-CE/releases) · [1.3.1alpha 预发布](https://github.com/DxDxDxis/WinIsland-CE/releases/tag/v1.3.1alpha) · [1.3.0 社区版](https://github.com/DxDxDxis/WinIsland-CE/releases/tag/v1.3.3beta)", "[下载发行版本](https://github.com/DxDxDxis/WinIsland-CE/releases) · [1.3.4 社区版](https://github.com/DxDxDxis/WinIsland-CE/releases/tag/v1.3.4) · [1.3.3beta](https://github.com/DxDxDxis/WinIsland-CE/releases/tag/v1.3.3beta) · [1.3.2alpha](https://github.com/DxDxDxis/WinIsland-CE/releases/tag/v1.3.2alpha)")
text = text.replace("**1.3.1alpha** 的正式运行文件为 `WinIsland-1.3.1alpha.exe`。", "**1.3.4社区版** 的正式运行文件为 `WinIsland-1.3.4.exe`。")
text = text.replace("最新版设置包含五个主分类", "1.3.4 设置使用 WinUI 3 客户端；历史 1.3.1alpha/1.3.3beta 仍保留各自设置宿主。最新版设置包含五个主分类")
start = text.find("## 全部正式版本（每版仅保留最新构建）")
end = text.find("## 版本演进", start)
if start >= 0 and end > start:
    table = ["## 全部正式版本（每版仅保留最新构建）", "", "| 版本（新 → 旧） | 所选构建时间 | 源码情况 |", "|---|---|---|"]
    for r in ordered:
        table.append(f"| [{Path(r['directory']).name}](versions/{r['directory']}/README.md) | {r.get('peLinkTime') or r.get('buildTime') or '未读取'} | {r.get('sourceStatus','')} |")
    text = text[:start] + "\n".join(table) + "\n\n" + text[end:]
# Ensure the evolution section mentions new versions.
text = text.replace("## 版本演进\n\n| 阶段（新 → 旧） | 项目演进 |\n|---|---|", "## 版本演进\n\n| 阶段（新 → 旧） | 项目演进 |\n|---|---|\n| 1.3.4社区版 | WinUI 3 设置客户端、统一数据目录与插件/歌词运行包的社区版整合 |\n| 1.3.3beta | 复制粘贴入口、WinUI 实验分支准备与主线设置修复 |\n| 1.3.2alpha | 开放运行时、插件 SDK 与复制粘贴功能演进 |")
readme_path.write_text(text, encoding="utf-8")

# Machine-readable update manifest and hash list.
update = {"generatedAt": datetime.datetime.now(datetime.timezone(datetime.timedelta(hours=8))).isoformat(), "source": str(SRC), "target": str(OUT), "versions": new_rows, "filesCopied": len(records), "bytesCopied": sum(r["bytes"] for r in records), "sourceReadOnly": True}
(OUT / "catalog/archive-update-1.3.4.json").write_text(json.dumps(update, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
print(json.dumps({"versions": [r["version"] for r in new_rows], "filesCopied": len(records), "bytesCopied": sum(r["bytes"] for r in records), "target": str(OUT)}, ensure_ascii=False))


# Experimental WinUI branches are archived as supplementary material, not formal tags.
EXPERIMENTAL = ["WinIsland-1.3.2alpha-winUI（实验性）", "WinIsland-1.3.3exp-winUI（实验性）"]
exp_rows = []
for name in EXPERIMENTAL:
    src_dir = SRC / name
    if not src_dir.is_dir():
        continue
    folder = OUT / "extras" / "experimental" / name
    folder.mkdir(parents=True, exist_ok=True)
    exp_records = []
    walk_source(src_dir, folder / "project", exp_records)
    if (src_dir / "release").is_dir():
        copy_runtime(src_dir / "release", folder / "dist", exp_records)
    exes = [r for r in exp_records if r["category"] == "runtime" and r["destination"].lower().endswith(".exe")]
    (folder / "README.md").write_text("\n".join([
        f"# {name}", "", "这是基于主线的实验性 WinUI 分支补充归档，不是独立正式产品版号，也不创建同名 GitHub Release。", "",
        "- `project/`：该实验分支源码、SDK、构建说明和资源。", "- `dist/`：该分支现有 release 运行产物。",
        "- 运行前请阅读分支原 README 和交付说明；实验分支可能与主线 ABI、设置宿主或依赖不同。", "",
        "项目自身采用 BSD-3-Clause；第三方组件继续适用原许可。联系：725513212@qq.com。", ""
    ]), encoding="utf-8")
    exp_rows.append({"name": name, "directory": str(folder.relative_to(OUT)).replace("\\", "/"), "files": len(exp_records), "bytes": sum(x["bytes"] for x in exp_records), "executables": exes})
(OUT / "catalog/experimental-update-1.3x.json").write_text(json.dumps({"generatedAt": datetime.datetime.now(datetime.timezone(datetime.timedelta(hours=8))).isoformat(), "branches": exp_rows}, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
