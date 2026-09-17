"""Read-only verification of repository, published releases, and attachment digests."""
from pathlib import Path
import argparse
import datetime
import hashlib
import json
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]
REPO = 'DxDxDxis/WinIsland-CE'
p = argparse.ArgumentParser()
p.add_argument('--gh', required=True)
p.add_argument('--source-commit', required=True)
args = p.parse_args()

def api(path):
    path += ('&' if '?' in path else '?') + '_verification=' + str(time.time_ns())
    data = subprocess.check_output([args.gh, 'api', path, '-H', 'Cache-Control: no-cache'], cwd=ROOT)
    return json.loads(data)

def sha(p):
    with p.open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest().lower()

rows = json.loads((ROOT/'release-assets/publish/manifest.json').read_text(encoding='utf-8'))
releases = api('repos/'+REPO+'/releases?per_page=100')
by_tag = {r['tag_name']: r for r in releases}
errors=[]
verified=[]
for row in rows:
    tag=row['tag']
    r=by_tag.get(tag)
    if r is None:
        errors.append('Missing release '+tag)
        continue
    if r['draft'] or r['prerelease'] != row['prerelease']:
        errors.append('Publication flags '+tag)
    assets={a['name']:a for a in r['assets']}
    for key in ('archive','checksum'):
        path=ROOT/row[key]
        a=assets.get(path.name)
        if not a or a['size']!=path.stat().st_size or a.get('digest')!='sha256:'+sha(path):
            errors.append('Asset mismatch '+tag+'/'+path.name)
    ref=api('repos/'+REPO+'/git/ref/tags/'+tag)
    obj=ref['object']
    if obj['type']=='tag':
        obj=api('repos/'+REPO+'/git/tags/'+obj['sha'])['object']
    if obj['sha']!=args.source_commit:
        errors.append('Tag commit mismatch '+tag)
    verified.append({'version':row['version'],'url':r['html_url'],'prerelease':r['prerelease'],
                     'publishedAt':r['published_at'],'assets':[{k:a.get(k) for k in
                      ('name','size','digest','browser_download_url')} for a in r['assets']]})

latest=api('repos/'+REPO+'/releases/latest')
if latest['tag_name']!='v1.3.0':
    errors.append('Latest stable release mismatch')
tree=api('repos/'+REPO+'/git/trees/'+args.source_commit+'?recursive=1')
local=subprocess.check_output(['git','ls-tree','-r','--full-tree','-z',args.source_commit],cwd=ROOT)
local_blobs={}
for entry in local.decode('utf-8').split('\0'):
    if entry:
        metadata,path=entry.split('\t',1)
        mode,typ,oid=metadata.split()
        if typ=='blob': local_blobs[path]=oid
remote_blobs={r['path']:r['sha'] for r in tree['tree'] if r['type']=='blob'}
if tree['truncated'] or local_blobs!=remote_blobs:
    errors.append('Remote source tree mismatch')
report={'checkedAt':datetime.datetime.now().astimezone().isoformat(),
        'repository':'https://github.com/'+REPO,'sourceCommit':args.source_commit,
        'sourceFiles':len(remote_blobs),'releaseCount':len(verified),
        'assetCount':sum(len(r['assets']) for r in verified),
        'latestStable':latest['tag_name'],'errors':errors,'releases':list(reversed(verified)),
        'scope':'Verified remote source blob IDs, release flags, tag targets and server SHA-256 attachment digests. Historical programs were not rebuilt or run.'}
(ROOT/'catalog/github-publication.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({k:v for k,v in report.items() if k!='releases'},ensure_ascii=False))
raise SystemExit(bool(errors))
