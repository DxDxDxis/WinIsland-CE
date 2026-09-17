"""Verify originals read-only, archive copies, navigation and version normalization."""
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor
import json,hashlib,re,zipfile,datetime,subprocess
root=Path(__file__).resolve().parents[1];source=Path('E:/aaAAx项目')
def sha(p):
    with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest().upper()
rows=json.loads((root/'catalog/copied-files.json').read_text(encoding='utf-8'))
def verify(row):
    errors=[];p=root/row['destination']
    if not p.is_file() or sha(p)!=row['sha256']:errors.append('COPY '+row['destination'])
    if '!/' not in row['source']:
        original=source/row['source']
        if not original.is_file() or sha(original)!=row['sha256']:errors.append('ORIGINAL '+row['source'])
    else:
        archive,name=row['source'].split('!/',1)
        with zipfile.ZipFile(source/archive) as z:
            if hashlib.sha256(z.read(name)).hexdigest().upper()!=row['sha256']:errors.append('ZIP SOURCE '+row['source'])
    return errors
with ThreadPoolExecutor(max_workers=8) as pool:errors=[e for result in pool.map(verify,rows) for e in result]
versions=json.loads((root/'catalog/versions.json').read_text(encoding='utf-8'))
names=[x['directory'] for x in versions]
if len(names)!=len(set(names)):errors.append('Duplicate formal version directory')
for row in versions:
    name=Path(row['directory']).name
    if not re.fullmatch(r'Winlsland-\d+\.\d+\.\d+(?:社区版|(?:alpha|beta)(?:_?\d+)?(?:-[rc]\d+)?)',name):errors.append('Invalid formal name '+name)
    if not (root/row['directory']/'README.md').is_file():errors.append('Missing version README '+name)
if versions[0]['version']!='1.3.1alpha' or versions[-1]['version']!='1.1.1':errors.append('Newest / oldest order incorrect')
actual={p.name for p in (root/'versions').iterdir() if p.is_dir()}
if actual!={Path(x).name for x in names}:errors.append('Unindexed version directory')
pages=[root/'README.md',*list((root/'docs').glob('*.md')),*[root/x/'README.md' for x in names]]
broken=[]
for p in pages:
    s=p.read_text(encoding='utf-8')
    for href in re.findall(r'\]\(([^)]+)\)',s):
        if href.startswith(('https:','http:','mailto:','#')):continue
        href=href.split('#')[0].replace('%20',' ')
        if not (p.parent/href).exists():broken.append({'file':p.relative_to(root).as_posix(),'target':href})
errors.extend('LINK '+x['file']+' -> '+x['target'] for x in broken)
sensitive=[]
for row in rows:
    n=Path(row['destination']).name.lower()
    if n in {'settings-connection.json','settings.xml','notifications.db','host-state.txt','install-location.json','.env'} or n.endswith(('.pfx','.p12','.pem')):sensitive.append(row['destination'])
errors.extend('SENSITIVE '+x for x in sensitive)
result={'checkedAt':datetime.datetime.now().astimezone().isoformat(),'copiedFiles':len(rows),'copiedBytes':sum(r['bytes'] for r in rows),'versionCount':len(versions),'newest':versions[0]['version'],'oldest':versions[-1]['version'],'originalsUnchangedForCopiedFiles':not any(e.startswith(('ORIGINAL','ZIP SOURCE')) for e in errors),'errors':errors,'brokenCuratedLinks':broken,'knownSensitiveFiles':sensitive,'scope':'Checks copied originals, not all excluded caches or running files. No historical rebuild or GitHub upload.'}
(root/'catalog/verification.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(result,ensure_ascii=False));raise SystemExit(bool(errors))
