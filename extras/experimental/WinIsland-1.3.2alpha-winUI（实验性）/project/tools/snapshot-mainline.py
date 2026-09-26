from pathlib import Path
import hashlib,json,shutil,time
target=Path(__file__).resolve().parents[1]
source=target.parent/'1.3.2alpha'
assert target.name=='WinIsland-1.3.2alpha-winUI（实验性）' and source.is_dir()
# This tool is for initial creation only. Never overwrite an edited branch.
assert not (target/'source').exists(), 'Snapshot already exists; refuse to overwrite branch work'
def digest(p):
 h=hashlib.sha256()
 with p.open('rb') as f:
  for b in iter(lambda:f.read(1048576),b''):h.update(b)
 return h.hexdigest().upper()
files=sorted(p for p in source.rglob('*') if p.is_file())
manifest=[{'path':str(p.relative_to(source)),'bytes':p.stat().st_size,'sha256':digest(p)} for p in files]
(target/'verification/mainline-sha256.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
print('Hashed',len(files),'files',flush=True)
for p in files:
 q=target/p.relative_to(source);q.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,q)
errors=[x['path'] for x in manifest if digest(target/x['path'])!=x['sha256']]
(target/'verification/copy-integrity.json').write_text(json.dumps({'files':len(files),'bytes':sum(x['bytes'] for x in manifest),'errors':errors,'source':str(source),'target':str(target),'git':'No .git repository; full SHA256 inventory and original directory are baseline'},ensure_ascii=False,indent=2),encoding='utf-8')
assert not errors,errors
print('Copy verified',flush=True)
