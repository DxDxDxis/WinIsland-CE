from pathlib import Path
import json,hashlib
b=Path.cwd(); results=[]
for name in ['1.3.3beta','WinIsland-1.3.2alpha-winUI（实验性）','1.3.2alpha']:
 f=b/'verification'/('readonly-before-'+name+'.json'); before=json.loads(f.read_text('utf-8-sig'));root=b.parent/name; failures=[]
 for item in before:
  p=root/item['path']
  if not p.is_file(): failures.append({'path':item['path'],'reason':'missing'});continue
  h=hashlib.file_digest(p.open('rb'),'sha256').hexdigest()
  if h.lower()!=item['sha256'].lower():failures.append({'path':item['path'],'reason':'hash'})
 old={i['path'] for i in before};added=[str(p.relative_to(root)) for p in root.rglob('*') if p.is_file() and str(p.relative_to(root)) not in old]
 results.append({'directory':str(root),'filesChecked':len(before),'changed':failures,'added':added});print(name,len(before),'changed',len(failures),'added',len(added),flush=True)
(b/'verification/readonly-after-comparison.json').write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding='utf-8')
