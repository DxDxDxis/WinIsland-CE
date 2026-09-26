from pathlib import Path
import hashlib, json, shutil, os
r=Path(r'E:\aaAAx项目\WinIsland-社区版-大更新\release'); t=r/'WinIsland-1.3.3exp-winUI（实验性）'; w=r/'WinIsland-1.3.2alpha-winUI（实验性）'
def snapshot(root):
 records=[]
 for p in sorted(root.rglob('*')):
  if p.is_file():
   h=hashlib.sha256()
   with p.open('rb') as f:
    for block in iter(lambda:f.read(1024*1024),b''):h.update(block)
   records.append(dict(path=str(p.relative_to(root)),size=p.stat().st_size,sha256=h.hexdigest()))
 return records
for name in ['1.3.2alpha','1.3.3beta',w.name]:
 records=snapshot(r/name)
 (t/'verification'/('readonly-before-'+name+'.json')).write_text(json.dumps(records,ensure_ascii=False),encoding='utf-8')
 print('Hashed',name,len(records),flush=True)
exclude={'node_modules','build','obj','bin','__pycache__','.git','out','dist','packages'}
def ignored(folder,names):return [n for n in names if n in exclude]
for folder in ['source','sdk','settings-winui','lyric-provider','test-fixtures','examples','community-examples','open-examples']:
 shutil.copytree(w/folder,t/folder,ignore=ignored,dirs_exist_ok=False)
for folder in ['appsdk','framework']:
 shutil.copytree(w/'dependency-cache'/'winui'/folder,t/'dependency-cache'/'winui'/folder)
for file in ['build-release.ps1']:
 shutil.copy2(w/file,t/file)
print('Copied necessary WinUI sources, dependencies, fixtures; no legacy build outputs/user data',flush=True)
