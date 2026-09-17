"""Prepare verified release attachments from the selected archive; no network writes."""
from pathlib import Path
import datetime
import hashlib
import json
import re
import zipfile

ROOT = Path(__file__).resolve().parents[1]
REPO = 'https://github.com/DxDxDxis/WinIsland-CE'
OUT = ROOT / 'release-assets' / 'publish'
OUT.mkdir(parents=True, exist_ok=True)
rows = json.loads((ROOT / 'catalog/versions.json').read_text(encoding='utf-8'))
manifest = []

def digest(p):
    with p.open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest().upper()

for row in reversed(rows):
    version = row['version']
    tag = 'v' + version
    stable = re.fullmatch(r'\d+\.\d+\.\d+', version) is not None
    title = 'WinIsland ' + version + (' 社区版' if stable else '')
    folder = ROOT / row['directory']
    stem = 'WinIsland-' + version + ('-community' if stable else '') + '-Windows-x64'
    dest = OUT / (stem + '.zip')
    files = [(p, p.relative_to(folder / 'dist').as_posix())
             for p in (folder / 'dist').rglob('*') if p.is_file()]
    if not files or not row['executables']:
        raise RuntimeError('Missing runtime: ' + version)
    for exe in row['executables']:
        if digest(folder / exe['path']) != exe['sha256']:
            raise RuntimeError('Executable changed: ' + exe['path'])
    files += [(ROOT / 'LICENSE', 'PROJECT-LICENSE.txt'),
              (ROOT / 'THIRD_PARTY_NOTICES.md', 'THIRD_PARTY_NOTICES.md')]
    files += [(p, 'archive-licenses/' + p.relative_to(ROOT / 'licenses').as_posix())
              for p in (ROOT / 'licenses').rglob('*') if p.is_file()]
    source_url = REPO + '/tree/' + tag + '/' + row['directory']
    notes = f'''# {title}

这是历史成品归档发布，保留所选构建的原始字节；本次没有重新编译或重新执行历史功能测试。

- 正式版号：`{version}`。
- 原构建链接时间：`{row['buildTime']}`（原 EXE 记录，不是本次上传时间）。
- 源码情况：{row['sourceStatus']}。
- 版本判定：{row['note']}

## 下载、安装和运行

下载本页 `{dest.name}`，完整解压后运行目录内的主程序。保留配套 DLL、settings、歌词服务及许可证目录；不要只提取一个 EXE。测试历史版本前备份现有配置，不同时运行多个版本。较新版首次启动需要确认数据目录，旧版以原说明为准。

下载同名 `.sha256` 文件，可用 PowerShell `Get-FileHash -Algorithm SHA256` 校验 ZIP。不能直接运行 GitHub 自动生成的 Source code 压缩包；它是源码归档。

## 源码、说明与许可

- [本版本源码和原说明]({source_url})。
- [全部版本索引]({REPO}/blob/main/docs/VERSIONS.md)。
- [安装和配置]({REPO}/blob/main/docs/INSTALL_AND_CONFIGURE.md)。
- [构建说明]({REPO}/blob/main/docs/BUILD.md)。
- [第三方许可]({REPO}/blob/main/THIRD_PARTY_NOTICES.md)。

所有历史标签指向本次整理后的归档提交，用于定位对应子目录，不冒充当年的 Git 开发记录。缺少完整对应源码的版本只归档成品，不用其他版本源码替代。

项目采用 BSD-3-Clause，第三方依赖保留各自许可。联系：725513212@qq.com。
'''
    if not stable:
        notes += '\n这是 alpha/beta 测试版本，标记为预发布。\n'
    expected = {n for _, n in files} | {'RELEASE-README.md'}
    if len(expected) != len(files) + 1:
        raise RuntimeError('Duplicate ZIP entries: ' + version)
    if dest.exists():
        # Resume only packages with the exact current payload.
        with zipfile.ZipFile(dest) as z:
            if set(z.namelist()) != expected or z.read('RELEASE-README.md') != notes.encode('utf-8'):
                raise RuntimeError('Existing ZIP differs; preserve it: ' + str(dest))
            for p, name in files:
                with z.open(name) as f:
                    if hashlib.file_digest(f, 'sha256').hexdigest().upper() != digest(p):
                        raise RuntimeError('ZIP payload differs: ' + name)
    else:
        temp = dest.with_suffix('.zip.partial')
        if temp.exists():
            temp.rename(temp.with_name(temp.name + '.' + datetime.datetime.now().strftime('%Y%m%d%H%M%S')))
        with zipfile.ZipFile(temp, 'x', compression=zipfile.ZIP_DEFLATED, compresslevel=6) as z:
            for p, name in sorted(files, key=lambda x: x[1]):
                z.write(p, name)
            z.writestr('RELEASE-README.md', notes)
        with zipfile.ZipFile(temp) as z:
            bad = z.testzip()
            if bad or set(z.namelist()) != expected:
                raise RuntimeError('ZIP validation failed: ' + str(bad))
        temp.rename(dest)
    sha = digest(dest)
    checksum = dest.with_suffix('.zip.sha256')
    checksum.write_text(sha + '  ' + dest.name + '\n', encoding='utf-8')
    notes += f'\n## 运行包 SHA-256\n\n```text\n{sha}  {dest.name}\n```\n'
    body = OUT / (stem + '.md')
    body.write_text(notes, encoding='utf-8')
    manifest.append({'version': version, 'tag': tag, 'title': title,
                     'prerelease': not stable, 'archive': str(dest.relative_to(ROOT)),
                     'checksum': str(checksum.relative_to(ROOT)), 'notes': str(body.relative_to(ROOT)),
                     'bytes': dest.stat().st_size, 'sha256': sha})
    print(json.dumps({'version': version, 'bytes': dest.stat().st_size, 'verified': True}), flush=True)
    (OUT / 'manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding='utf-8')

print('Prepared ' + str(len(manifest)) + ' verified release packages.', flush=True)
