"""Run on the authorized Linux server. Downloads weights only, not executable code."""
from pathlib import Path
import concurrent.futures
import hashlib
import json
import shutil
import subprocess
import zipfile

ROOT = Path('/opt/winisland-translate/cloud-models')
ROOT.mkdir(exist_ok=True)
VERSIONS = {'zh': '1_9', 'de': '1_3', 'hi': '1_1', 'ja': '1_1', 'ko': '1_1', 'ru': '1_9'}
def install(pair):
    src, dst, version = pair
    name = f'translate-{src}_{dst}-{version}'
    archive = ROOT / (name + '.argosmodel')
    if not archive.exists():
        subprocess.run(['curl', '--fail', '--location', '--retry', '3', '--max-time', '600',
                        '--silent', '--show-error', 'https://argos-net.com/v1/' + archive.name,
                        '-o', str(archive) + '.part'], check=True)
        Path(str(archive) + '.part').rename(archive)
    dest = ROOT / (src + '-' + dst)
    dest.mkdir(exist_ok=True)
    with zipfile.ZipFile(archive) as z:
        for item in z.infolist():
            parts = Path(item.filename).parts
            if '..' in parts or Path(item.filename).is_absolute():
                raise ValueError('unsafe archive path')
            relative = Path(*parts[1:])
            if not relative.parts or relative.parts[0] not in ('model', 'sentencepiece.model', 'metadata.json', 'README.md', 'LICENSE', 'bpe.model', 'bpe.codes') or item.is_dir():
                continue
            target = dest / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            with z.open(item) as source, target.open('wb') as sink:
                shutil.copyfileobj(source, sink)
    result = {'source': src, 'target': dst, 'version': version, 'sha256': hashlib.file_digest(archive.open('rb'), 'sha256').hexdigest(), 'bytes': archive.stat().st_size}
    print(json.dumps(result), flush=True)
    return result

if __name__ == '__main__':
    pairs = [(a,b,v) for lang,v in VERSIONS.items() for a,b in [('en',lang),(lang,'en')]]
    with concurrent.futures.ThreadPoolExecutor(max_workers=2) as executor:
        results = list(executor.map(install, pairs))
    (ROOT / 'manifest.json').write_text(json.dumps(results, indent=2))
