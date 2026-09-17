"""Create a release ZIP from the selected local archive; never upload or run it."""
from pathlib import Path
import argparse,json,hashlib,zipfile
p=argparse.ArgumentParser();p.add_argument('--version',required=True);args=p.parse_args()
root=Path(__file__).resolve().parents[1]
versions=json.loads((root/'catalog/versions.json').read_text(encoding='utf-8'))
row=next((x for x in versions if x['version']==args.version),None)
if not row:raise SystemExit('Unknown formal version; consult docs/VERSIONS.md')
folder=root/row['directory'];dist=folder/'dist'
if not dist.is_dir():raise SystemExit('No recorded runtime directory')
output=root/'release-assets';output.mkdir(exist_ok=True)
dest=output/(folder.name+'-Windows-x64.zip')
if dest.exists():raise SystemExit('Already exists; preserve the previous package or choose a new archive location')
files=[(f,f.relative_to(dist).as_posix()) for f in dist.rglob('*') if f.is_file()]
files.extend([(folder/'README.md','VERSION-README.md'),(root/'LICENSE','PROJECT-LICENSE.txt'),(root/'THIRD_PARTY_NOTICES.md','THIRD_PARTY_NOTICES.md')])
files.extend((f,'archive-licenses/'+f.relative_to(root/'licenses').as_posix()) for f in (root/'licenses').rglob('*') if f.is_file())
with zipfile.ZipFile(dest,'x',compression=zipfile.ZIP_DEFLATED,compresslevel=6,allowZip64=True) as z:
    for file,name in sorted(files,key=lambda x:x[1]):z.write(file,name)
with dest.open('rb') as f:digest=hashlib.file_digest(f,'sha256').hexdigest().upper()
dest.with_suffix('.zip.sha256').write_text(digest+'  '+dest.name+'\n',encoding='utf-8')
print(json.dumps({'archive':str(dest),'bytes':dest.stat().st_size,'sha256':digest,'uploaded':False},ensure_ascii=False))
