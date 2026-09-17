from pathlib import Path
import json,struct,datetime,hashlib,html,re,subprocess
ROOT=Path(__file__).resolve().parents[1]
rows=json.loads((ROOT/'catalog/versions.json').read_text(encoding='utf-8'))
copies=json.loads((ROOT/'catalog/copied-files.json').read_text(encoding='utf-8'))
def exe_time(p):
    with p.open('rb') as f:
        f.seek(0x3c);offset=struct.unpack('<I',f.read(4))[0];f.seek(offset+8);v=struct.unpack('<I',f.read(4))[0]
    return datetime.datetime.fromtimestamp(v,datetime.timezone.utc).astimezone().isoformat()
for row in rows:
    main=[e for e in row['executables'] if Path(e['path']).name.startswith('WinIsland-') and 'Helper' not in e['path']]
    if main:
        primary=max(main,key=lambda e:exe_time(ROOT/row['directory']/e['path']))
        row['buildTime']=exe_time(ROOT/row['directory']/primary['path']);row['mainExecutable']=primary['path']
    else:row['buildTime']='未找到主 EXE';row['mainExecutable']=''
    readme=ROOT/row['directory']/'README.md'
    s=readme.read_text(encoding='utf-8')
    s+='\n## 原始说明入口\n\n'
    originals=list((readme.parent/'project').glob('README*'))
    for p in originals:s+=f'- [{p.name}]({p.relative_to(readme.parent).as_posix().replace(" ","%20")})（原文保留，内部修复后缀不是正式版号）。\n'
    if not originals:s+='本版未发现原始 README；以本页已知信息与根安装说明为准。\n'
    s+='\n主 EXE 链接时间：`'+row['buildTime']+'`。本轮只核对归档字节，不代表重新运行或编译过此历史版本。\n'
    readme.write_text(s,encoding='utf-8')
(ROOT/'catalog/versions.json').write_text(json.dumps(rows,ensure_ascii=False,indent=2),encoding='utf-8')
table='| 版本（新 → 旧） | 所选构建时间（UTC+08:00） | 源码情况 |\n|---|---|---|\n'
for row in rows:
    table+=f"| [{Path(row['directory']).name}]({row['directory']}/README.md) | {row['buildTime'].replace('T',' ').replace('+08:00','')} | {'已找到源码' if row['sourceStatus'].startswith('已') else '缺少完整对应源码'} |\n"
p=ROOT/'README.md';s=p.read_text(encoding='utf-8');s=s.replace('## 版本演进','## 全部正式版本（每版仅保留最新构建）\n\n'+table+'\n## 版本演进');s=s.replace('- [上传 GitHub 与发布成品](docs/GITHUB_PUBLISH.md)','- [上传 GitHub 与发布成品](docs/GITHUB_PUBLISH.md) · [哪些文件要上传](docs/UPLOAD_CHECKLIST.md)');p.write_text(s,encoding='utf-8')
v='# 版本索引（新 → 旧，每版仅最新构建）\n\n内部修复后缀不是正式版号。r/c 阶段与 alpha/beta 数字沿用源码中已声明的版本。所列时间读取主 EXE 的 PE 链接记录，不是文件夹最后修改时间。\n\n'+table.replace('](versions/','](../versions/')
v+='\n## 来源和完整性\n\n每个版本的 README 说明安装、运行、源文件与成品哈希。原目录与内部构建身份保存在 `catalog/versions.json`，候选记录保存在 `catalog/build-candidates.json`。旧同版修订不会作为独立正式目录。\n'
(ROOT/'docs/VERSIONS.md').write_text(v,encoding='utf-8')
body=''.join(f'<a class="version" href="{html.escape(row["directory"])}/README.md"><strong>{html.escape(Path(row["directory"]).name)}</strong><span>{html.escape(row["buildTime"])}</span><small>{html.escape(row["sourceStatus"])}</small></a>' for row in rows)
page='''<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1"><title>WinIsland 历代版本</title><style>body{margin:0;background:#f5f6f9;color:#202737;font:16px/1.6 "Segoe UI","Microsoft YaHei",sans-serif}main{max-width:980px;margin:auto;padding:40px 24px}h1{margin:0}p{color:#626c7e}.version{display:grid;grid-template-columns:1fr auto;gap:6px 20px;margin:12px 0;padding:18px 22px;background:white;border:1px solid #dce1eb;border-radius:12px;color:inherit;text-decoration:none}.version:hover{border-color:#315ddd}.version:focus-visible{outline:3px solid #315ddd}.version small{grid-column:1/-1;color:#626c7e}@media(max-width:650px){.version{grid-template-columns:1fr}}</style><main><h1>WinIsland 社区版 · 历代版本</h1><p>从新到旧，每个正式版号仅保留最新构建。内部修复后缀不作为版号。</p><p><a href="README.md">项目总览</a> · <a href="docs/UPLOAD_CHECKLIST.md">上传范围</a> · <a href="docs/VERSIONS.md">版本索引</a></p>'''+body+'</main></html>'
(ROOT/'版本导航.html').write_text(page,encoding='utf-8')
c=ROOT/'catalog/build-candidates.json';data=json.loads(c.read_text(encoding='utf-8'))
for x in data:x['path']=x['path'].replace('E:\\aaAAx项目\\','').replace('\\','/')
c.write_text(json.dumps(data,ensure_ascii=False,indent=2),encoding='utf-8')
# List files that Git would actually consider; .gitignore excludes local runtimes.
git=subprocess.run(['git','-c','core.quotePath=false','ls-files','--others','--exclude-standard','-z'],cwd=ROOT,capture_output=True,check=True)
tracked=[x for x in git.stdout.decode('utf-8').split('\0') if x]
large=[{'path':n,'bytes':(ROOT/n).stat().st_size} for n in tracked if (ROOT/n).is_file() and (ROOT/n).stat().st_size>=100*1024*1024]
secrets=[];patterns=[r'-----BEGIN (?:RSA |OPENSSH |EC )?PRIVATE KEY-----',r'gh[pousr]_[A-Za-z0-9]{30,}',r'github_pat_[A-Za-z0-9_]{50,}',r'AKIA[A-Z0-9]{16}']
for n in tracked:
    p=ROOT/n
    if p.suffix.lower() not in {'.json','.md','.txt','.yml','.yaml','.ps1','.py','.ts','.js','.cpp','.h','.c','.ini','.conf'} or p.stat().st_size>10*1024*1024:continue
    text=p.read_text(encoding='utf-8',errors='replace')
    if any(re.search(pattern,text) for pattern in patterns):secrets.append(n)
report={'gitCandidateFiles':len(tracked),'filesAtOrOver100MiB':large,'highConfidenceSecretPatternCandidates':secrets,'notAnExhaustiveSecurityAudit':True,'gitRemoteConfigured':False,'uploaded':False}
(ROOT/'catalog/publish-check.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({'versions':len(rows),'newest':rows[0]['version'],'oldest':rows[-1]['version'],'publishCheck':report},ensure_ascii=False))
