"""Read original projects; only write under this archive. No original is modified."""
from pathlib import Path
import os, re, json, hashlib, shutil, zipfile, datetime, struct
from concurrent.futures import ThreadPoolExecutor

OUT=Path(__file__).resolve().parents[1]
SRC=Path('E:/aaAAx项目')
REL=SRC/'WinIsland-社区版-大更新/release'
ROWS=[]; COPIES=[]; SKIPPED=[]
SKIP={'node_modules','.git','.vs','__pycache__','dependency-cache','electron-data','obj','target','bin','built','artifacts','baseline','snapshots','backups','verification','validation','validated','runtime-test','test-fixtures','source-snapshot','test262','build-electron','dist-release','logs','Cache','Code Cache','Crashpad','migration','mod-cache','temp','staging'}
SKIP.update({'downloads','python-build','qt','qcloud-build','qcloud-build-ascii','qcloud-build-new','qcloud-build-msvc'})
CODE={'.cs','.cpp','.c','.h','.hpp','.inl','.inc','.rs','.ts','.tsx','.js','.mjs','.cjs','.html','.css','.ps1','.py','.sh','.bat','.cmd','.cmake','.json','.md','.txt','.rc','.manifest','.ico','.svg','.png','.jpg','.jpeg','.webp','.gif','.yml','.yaml','.toml','.lock','.xml','.def','.sln','.vcxproj','.props','.targets','.map','.d.ts','.tcl','.rst','.bnf','.idl','.qrc','.pro','.qss'}
BADFILES={'settings-connection.json','settings.xml','host-state.txt','install-location.json','notifications.db','state.txt','command.txt','exit.request','desktop.ini','.env','credentials.json','debug.log','copy-baseline.log'}
def sha(p):
    with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest().upper()
def write(p,s):p.parent.mkdir(parents=True,exist_ok=True);p.write_text(s,encoding='utf-8')
def cp(src,dst,category='original'):
    if not src.is_file():return
    if src.is_symlink():SKIPPED.append({'source':str(src.relative_to(SRC)),'reason':'link'});return
    dst.parent.mkdir(parents=True,exist_ok=True)
    h=sha(src)
    if dst.exists():
        if sha(dst)!=h:raise RuntimeError('Refuse overwrite different file '+str(dst))
    else:shutil.copy2(src,dst)
    if sha(dst)!=h or sha(src)!=h:raise RuntimeError('Copy/hash mismatch '+str(src))
    COPIES.append({'source':str(src.relative_to(SRC)).replace('\\','/'),'destination':str(dst.relative_to(OUT)).replace('\\','/'),'bytes':src.stat().st_size,'sha256':h,'category':category})
def allowed(p,rel):
    if p.name in BADFILES or p.name.lower().endswith(('.pfx','.p12','.pem','.key','.db','.dmp','.log','.obj','.pdb','.ilk','.pch','.pyc')):return False
    if any(d in SKIP or d.startswith(('verification-','build-','component-test','runtime-smoke')) or re.fullmatch(r'build\d*',d) for d in rel.parts[:-1]):return False
    if p.suffix.lower() in {'.exe','.dll','.lib','.a','.wimod','.zip','.7z','.tgz','.gz','.bin','.pak','.asar','.dat','.node','.mo','.qm'}:return False
    return p.suffix.lower() in CODE or p.suffix=='' or p.name.startswith(('.gitignore','.gitattributes','.gitmodules','.clang','LICENSE','COPYING','NOTICE'))
def tree(src,dst,runtime=False):
    if not src.is_dir():return
    pending=[]
    for base,dirs,files in os.walk(src):
        dirs[:]=[d for d in dirs if d not in SKIP and not d.startswith(('verification-','component-test','runtime-smoke')) and not (not runtime and (d.startswith('build-') or re.fullmatch(r'build\d*',d)))]
        for n in files:
            p=Path(base)/n;rel=p.relative_to(src)
            if n in BADFILES or (not runtime and not allowed(p,rel)):continue
            if not runtime and 'tests' in p.parts and 'third_party' not in p.parts and p.suffix.lower() not in {'.cs','.cpp','.h','.c','.rs','.ts','.js','.cjs','.mjs','.ps1','.py','.sh','.cmd','.bat','.cmake'}:continue
            if runtime and p.suffix.lower() in {'.log','.db','.obj','.pdb','.ilk'}:continue
            pending.append((p,dst/rel,'runtime' if runtime else 'source/docs'))
    with ThreadPoolExecutor(max_workers=8) as pool:
        list(pool.map(lambda args:cp(*args),pending))
def pe_time(p):
    with p.open('rb') as f:
        f.seek(0x3c);offset=struct.unpack('<I',f.read(4))[0];f.seek(offset+8);return struct.unpack('<I',f.read(4))[0]
def build_info(p):return {'source':str(p.relative_to(SRC)).replace('\\','/'),'peLinkTime':datetime.datetime.fromtimestamp(pe_time(p),datetime.timezone.utc).astimezone().isoformat(),'sha256':sha(p)}
def label(v):return 'Winlsland-'+v+('社区版' if re.fullmatch(r'\d+\.\d+\.\d+',v) else '')
def add(v,src=None,note='',main=True,location=None):
    print('Copying latest version: '+v,flush=True)
    folder=OUT/(location or ('versions' if main else 'supplementary'))/label(v);folder.mkdir(parents=True,exist_ok=True)
    rec={'version':v,'directory':str(folder.relative_to(OUT)).replace('\\','/'),'note':note,'sourceStatus':'未找到独立完整对应源码','sourceRoots':[],'executables':[],'evidence':[]};ROWS.append(rec)
    if src:
        src=Path(src);rec['sourceRoots'].append(str(src.relative_to(SRC)).replace('\\','/'));project=folder/'project'
        # Preserve source-relative structure. Top-level runtime/deployment directories
        # are copied separately without application state or generated tests.
        for p in src.iterdir():
            if p.is_dir() and p.name in {'src','source','settings-electron','sdk','SDK','examples','plugins','docs','licenses','third_party','lyric-helper','animation','native','scripts','assets','tools','tests'}:tree(p,project/p.name)
            elif p.is_file() and allowed(p,Path(p.name)):cp(p,project/p.name,'source/docs')
        code=[p for p in project.rglob('*') if p.suffix in {'.cs','.cpp','.c','.rs','.ts'}]
        if code:rec['sourceStatus']='已收录所见源码；未重新构建证明与所有成品逐字节对应'
        for p in [src/'src/core.h',src/'source/src/core.h',src/'AppVersion.cs',src/'build-manifest.json']:
            if p.is_file():
                s=p.read_text(encoding='utf-8-sig',errors='replace');lines=[l.strip() for l in s.splitlines() if any(k in l for k in ['Version[]','BuildId[]','Display =','Number =','"buildId"','"version"','"product"'])];rec['evidence'].append({'path':str(p.relative_to(src)).replace('\\','/'),'lines':lines[:8]})
        candidates=list(src.glob('WinIsland*.exe'))
        if (src/'release').is_dir() and src != SRC/'WinIsland-社区版-大更新':candidates+=list((src/'release').rglob('WinIsland*.exe'))
        candidates=[p for p in candidates if p.name==f'WinIsland-{v}.exe' or (v in ['1.1.2','1.2.0'] and v in p.name)]
        if candidates:
            primary=max(candidates,key=pe_time);rec['selectedBuild']=build_info(primary)
            cp(primary,folder/'dist'/primary.name,'binary')
            for sibling in primary.parent.iterdir():
                if sibling.is_dir() and sibling.name in {'settings','lyric-provider','licenses','locales','resources','tls'}:tree(sibling,folder/'dist'/sibling.name,True)
                elif sibling.is_file() and sibling.name!=primary.name and sibling.suffix.lower() in {'.dll','.pak','.bin','.dat','.json','.md','.txt','.conf','.html'} and sibling.name not in BADFILES:cp(sibling,folder/'dist'/sibling.name,'runtime')
        for n in ['lyric-provider','licenses']:
            tree(src/n,folder/'dist'/n,True)
            if n=='lyric-provider':tree(src/n,project/n,True)
        if (src/'release/settings').is_dir():tree(src/'release/settings',project/'release/settings',True)
        if (src/'release/licenses').is_dir():tree(src/'release/licenses',project/'release/licenses',True)
    return folder,rec

# Published versions, and source versions established from code rather than names.
f,r=add('1.1.1',SRC/'WinIsland-1.1','发布说明明确：只更改发布名，内部仍为 1.1.0.0；已有原 EXE 同哈希证据。')
cp(SRC/'1.1.1（社区版）/WinIsland-1.1.1（社区版）.exe',f/'dist/WinIsland-1.1.1（社区版）.exe','binary')
cp(SRC/'目录整理/1.1.1（社区版）-发布说明.md',f/'historical-docs/发布说明.md')
for v,d in [('1.1.2','1.1.2社区版'),('1.2.0','1.2.0社区版'),('1.2.1beta','1.2.1beta')]:
    f,r=add(v,SRC/d,'该发布目录仅有成品/说明；旧同名工作区已继续升级，不能认定为本版源码。')
f,r=add('1.2.2beta',SRC/'WinIsland-1.1.2社区版','虽然来源文件夹名含 1.1.2，但 AppVersion.cs 明确为 1.2.2beta。')
cp(SRC/'1.2.2beta/WinIsland-1.2.2beta.exe',f/'dist/WinIsland-1.2.2beta.exe','binary')
f,r=add('1.2.3beta',note='从官方历史源码 ZIP 安全解包；core.h 明确标识 1.2.3beta。成品未重新构建。')
archive=SRC/'1.2.3beta社区版（重构底层架构）/WinIsland-1.2.3beta-源码.zip'
cp(archive,f/'archives'/archive.name,'source archive')
with zipfile.ZipFile(archive) as z:
    for item in z.infolist():
        rel=Path(item.filename.replace('\\','/'))
        if item.is_dir():continue
        if rel.is_absolute() or '..' in rel.parts:raise RuntimeError('unsafe zip entry')
        if not allowed(rel,rel):continue
        dst=f/'project'/rel;dst.parent.mkdir(parents=True,exist_ok=True);data=z.read(item)
        if dst.exists() and dst.read_bytes()!=data:raise RuntimeError('zip collision')
        dst.write_bytes(data);h=hashlib.sha256(data).hexdigest().upper();COPIES.append({'source':str(archive.relative_to(SRC)).replace('\\','/')+'!/'+item.filename,'destination':str(dst.relative_to(OUT)).replace('\\','/'),'bytes':len(data),'sha256':h,'category':'archive entry'})
r['sourceStatus']='已从历史源码包恢复；core.h 为 1.2.3beta';r['sourceRoots']=[str(archive.relative_to(SRC)).replace('\\','/')]
cp(SRC/'1.2.3beta社区版（重构底层架构）/发布包/WinIsland-1.2.3beta.exe',f/'dist/WinIsland-1.2.3beta.exe','binary')
f,r=add('1.2.3beta_2',SRC/'1.2.3beta社区版（重构底层架构）','当前 core.h 为 1.2.3beta_2；只选择本版最新成品。')
add('1.2.4beta',SRC/'1.2.4beta','本版 release 内按 PE 构建时间选择最新成品。')
add('1.2.4beta_2',SRC/'1.2.4beta_2')

selected=[('1.2.5alpha','1.2.5alpha-top-docked'),('1.2.5alpha-r1','1.2.5alpha-r1-outward-adjustable'),('1.2.6alpha-c1','1.2.6alpha-c1-wimod'),('1.2.6alpha-c2','1.2.6alpha-c2-open-scene'),('1.2.7alpha-r1','1.2.7alpha-r1-dynamic-final-snapshot-20260914-214403'),('1.3.0','1.3.0-path-state-fix'),('1.3.1alpha','1.3.1alpha')]
for v,dirname in selected:add(v,REL/dirname,'按主 EXE 的 PE 链接构建时间筛选；仅保留本版最新构建及所见源码。')
add('1.2.7alpha',SRC/'新版本/1.2.7alpha','正式 C++ 宿主 + Electron 设置迁移分支，不混入实验重构分支。')
# alpha2 ZIP contains four distinct runtime revisions, no source.
p=REL/'1.2.5alpha2.zip'
with zipfile.ZipFile(p) as z:
    for prefix in ['1.2.5alpha2-r4']:
        if not prefix.startswith('1.2.5alpha2'):continue
        f,r=add('1.2.5alpha2',note='从历史 ZIP 选择最新 r4 运行目录；包内无源码。')
        for i in z.infolist():
            parts=Path(i.filename.replace('\\','/')).parts
            if i.is_dir() or parts[0]!=prefix or len(parts)<2:continue
            rel=Path(*parts[1:]);assert not rel.is_absolute() and '..' not in rel.parts
            if rel.name in BADFILES:continue
            data=z.read(i);dst=f/'dist'/rel;dst.parent.mkdir(parents=True,exist_ok=True);dst.write_bytes(data);COPIES.append({'source':str(p.relative_to(SRC)).replace('\\','/')+'!/'+i.filename,'destination':str(dst.relative_to(OUT)).replace('\\','/'),'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest().upper(),'category':'archive entry'})
tree(SRC/'插件',OUT/'extras/plugins')
# Shared helper sources and notices. Required to explain LGPL/third-party boundaries.
for n in ['lyric-helper','third_party']:
    tree(SRC/'WinIsland-社区版-大更新'/n,OUT/'shared/lyric-provider-source'/n)
cp(SRC/'WinIsland-社区版-大更新/build-qcloud.ps1',OUT/'shared/lyric-provider-source/build-qcloud.ps1')
tree(SRC/'1.2.4beta_2/release/1.2.4beta_2/licenses',OUT/'licenses/lyric-provider',True)
tree(REL/'1.3.1alpha/release/licenses',OUT/'licenses/current-host',True)

def key(v):
    m=re.match(r'(\d+)\.(\d+)\.(\d+)(.*)',v);major,minor,patch=map(int,m.groups()[:3]);suffix=m[4];stage=3 if not suffix else 2 if suffix.startswith('beta') else 1 if suffix.startswith('alpha') else 0
    nums=tuple(int(x) for x in re.findall(r'\d+',suffix));return major,minor,patch,stage,nums,suffix
main=[x for x in ROWS if x['directory'].startswith('versions/')];main.sort(key=lambda x:key(x['version']),reverse=True)
for rec in ROWS:
    f=OUT/rec['directory'];items=[x for x in COPIES if x['destination'].startswith(rec['directory']+'/')]
    exes=[x for x in items if '/dist/' in x['destination'] and x['destination'].endswith('.exe') and Path(x['destination']).name.startswith('WinIsland')];rec['executables']=[{'path':x['destination'][len(rec['directory'])+1:],'bytes':x['bytes'],'sha256':x['sha256']} for x in exes]
    rec['files']=len(items);rec['bytes']=sum(x['bytes'] for x in items)
    source=f/'project';scripts=[p.relative_to(f).as_posix() for p in source.glob('*.ps1')] if source.exists() else []
    if (source/'source/build.ps1').exists():scripts.append('project/source/build.ps1')
    readme=f"# {f.name}\n\n[返回项目总览](../../README.md)\n\n## 归档身份\n\n- 正式版本：`{rec['version']}`\n- 源码状态：{rec['sourceStatus']}\n- 筛选说明：{rec['note']}\n- 来源路径和内部构建标识详见根目录 catalog；内部修复后缀不属于正式版号。\n- 文件数：{len(items)}，大小约 {rec['bytes']/1048576:.2f} MiB。\n\n"
    if rec.get('selectedBuild'):readme+='- 所选 EXE 链接构建时间：`'+rec['selectedBuild']['peLinkTime']+'`。\n\n'
    readme+='## 源码与构建\n\n原源码位于 `project/`（如存在），按原字节复制，未为统一版本名称修改源码。'+(' 构建入口：'+', '.join('`'+s+'`' for s in scripts)+'。' if scripts else ' 未发现可明确对应的顶层构建入口；请不要以相邻版本源码替代。')+'\n\n本轮只归档，不重新编译、不批量运行历史 EXE；不宣称所有历史版本都能在当前环境重建。具体依赖、历史验证限制请读 project 内原 README 和 docs；源代码版权按根 LICENSE，第三方按各自许可。\n\n'
    readme+='## 安装与运行\n\n优先阅读原发布说明。`dist/` 保存该版本成品；多文件发行必须保留歌词服务、DLL、许可证等相对目录，不能只取主 EXE。较新内嵌发行版在首次启动确认数据路径。旧版本可能使用 `%LOCALAPPDATA%/WinIsland`；测试旧版前先备份日常配置，不同时运行多个历史版本。\n\n'
    readme+='## 成品清单\n\n| 文件 | MiB | SHA-256 |\n|---|---:|---|\n'+''.join(f"| [{x['path']}]({x['path'].replace(' ','%20')}) | {x['bytes']/1048576:.2f} | `{x['sha256']}` |\n" for x in rec['executables'])
    if not exes:readme+='\n未在本目录找到明确的主程序成品。\n'
    readme+='\n## 身份证据\n\n'+('```text\n'+'\n'.join(e['path']+'\n'+'\n'.join(e['lines']) for e in rec['evidence'])+'\n```\n' if rec['evidence'] else '见归档说明及历史发布记录；没有足够信息时不推定源码与 EXE 严格对应。\n')
    readme+='\n## 目录、配置与联系\n\n- `project/`：源码、资源、测试脚本、锁文件和原文档；可能包含本地构建所需的运行依赖副本。\n- `dist/`：已存在的发行文件，不代表本轮重新验证过。\n- `historical-*`、`archives/`：仅在发现时保留的历史素材。\n- 配置字段随版本变化，以本版本原说明为准；不要跨版本直接覆盖配置。\n- 许可证：根目录 BSD-3-Clause；第三方声明优先适用于对应第三方文件。\n- 联系：725513212@qq.com。\n'
    write(f/'README.md',readme)
write(OUT/'catalog/versions.json',json.dumps(main,ensure_ascii=False,indent=2))
write(OUT/'catalog/supplementary.json',json.dumps([x for x in ROWS if x not in main],ensure_ascii=False,indent=2))
write(OUT/'catalog/copied-files.json',json.dumps(COPIES,ensure_ascii=False,indent=2))
index='# 版本索引（最新 → 最早）\n\n每个声明版号仅保留最新 PE 链接构建；旧修订不归档。按数字版本及阶段倒序。目录前缀按用户指定使用 `Winlsland`，源码产品名称仍为 `WinIsland`。正式版加“社区版”，alpha/beta 不追加。\n\n| 版本目录 | 源码状态 | 说明 |\n|---|---|---|\n'
for x in main:index+=f"| [{Path(x['directory']).name}](../{x['directory']}/README.md) | {x['sourceStatus']} | {x['note']} |\n"
write(OUT/'docs/VERSIONS.md',index)
print(json.dumps({'versions':len(main),'supplementary':len(ROWS)-len(main),'files':len(COPIES),'GB':sum(x['bytes'] for x in COPIES)/1e9},ensure_ascii=False),flush=True)
