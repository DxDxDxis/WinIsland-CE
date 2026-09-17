"""Publish the verified archive to the explicitly selected repository.

Requires an existing GitHub CLI login and a source commit already on origin/main.
Creates drafts first and publishes only after both attachment digests match.
"""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]
REPO = 'DxDxDxis/WinIsland-CE'
parser = argparse.ArgumentParser()
parser.add_argument('--gh', required=True)
parser.add_argument('--commit', required=True)
args = parser.parse_args()
rows = json.loads((ROOT / 'release-assets/publish/manifest.json').read_text(encoding='utf-8'))
if len(rows) != 18 or rows[0]['version'] != '1.1.1' or rows[-1]['version'] != '1.3.1alpha':
    raise SystemExit('Expected complete, oldest-to-newest package manifest')
state = ROOT / '.publish-state'
state.mkdir(exist_ok=True)

def call(*parts, allow_missing=False):
    for attempt in range(3):
        p = subprocess.run([args.gh, *parts], cwd=ROOT, capture_output=True,
                           encoding='utf-8', errors='replace', timeout=1200)
        if p.returncode == 0:
            return p.stdout.strip()
        if allow_missing and '404' in p.stderr:
            return None
        if attempt == 2:
            raise RuntimeError(p.stderr[-1800:])
        time.sleep(2)

def api(path):
    out = call('api', path, allow_missing=True)
    return json.loads(out) if out is not None else None

def sha(p):
    with p.open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest().lower()

current = api('repos/' + REPO + '/commits/main')
if current['sha'] != args.commit:
    raise SystemExit('Remote main does not match the reviewed archive commit')

results = []
for row in rows:
    tag = row['tag']
    print('Preparing release ' + tag, flush=True)
    release = api('repos/' + REPO + '/releases/tags/' + tag)
    if release is None:
        command = ['release', 'create', tag, '--repo', REPO, '--target', args.commit,
                   '--draft', '--title', row['title'], '--notes-file', row['notes']]
        if row['prerelease']:
            command.append('--prerelease')
        call(*command)
        release = api('repos/' + REPO + '/releases/tags/' + tag)
    if release['target_commitish'] not in (args.commit, 'main'):
        raise RuntimeError('Existing release has a different target: ' + tag)
    if not release['draft'] and release['name'] != row['title']:
        raise RuntimeError('Existing published release differs: ' + tag)
    expected = [ROOT / row['archive'], ROOT / row['checksum']]
    for p in expected:
        checksum = sha(p)
        assets = {a['name']: a for a in release['assets']}
        existing = assets.get(p.name)
        if existing is None:
            call('release', 'upload', tag, str(p), '--repo', REPO)
            release = api('repos/' + REPO + '/releases/tags/' + tag)
            existing = next(a for a in release['assets'] if a['name'] == p.name)
        if existing['size'] != p.stat().st_size or existing.get('digest') != 'sha256:' + checksum:
            raise RuntimeError('Server asset digest mismatch or unavailable: ' + tag + '/' + p.name)
    if release['draft']:
        call('release', 'edit', tag, '--repo', REPO, '--draft=false',
             '--prerelease=' + str(row['prerelease']).lower(),
             '--latest=' + str(row['version'] == '1.3.0').lower())
    release = api('repos/' + REPO + '/releases/tags/' + tag)
    if release['draft'] or release['prerelease'] != row['prerelease']:
        raise RuntimeError('Published status mismatch: ' + tag)
    results.append({'version': row['version'], 'tag': tag, 'url': release['html_url'],
                    'prerelease': release['prerelease'], 'publishedAt': release['published_at'],
                    'assets': [{k: a.get(k) for k in ('name', 'size', 'digest', 'browser_download_url')}
                               for a in release['assets']]})
    (state / 'published-releases.json').write_text(json.dumps(results, ensure_ascii=False, indent=2), encoding='utf-8')
    print('Published and verified ' + tag, flush=True)

print('Published and verified all ' + str(len(results)) + ' releases.', flush=True)
