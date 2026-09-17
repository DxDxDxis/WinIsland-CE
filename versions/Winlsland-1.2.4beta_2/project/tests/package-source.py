"""Archive source, build instructions and selected evidence, excluding SDKs/audio/binaries."""
from pathlib import Path
import zipfile

root=Path(__file__).resolve().parents[1]
files=[root/'README.md',root/'build.ps1',root/'.clang-format',root/'使用说明.txt']
files+=list((root/'src').rglob('*'))+list((root/'docs').rglob('*'))
for pattern in ('*.py','*.cpp','*.ps1','selftest-extracted.txt','dependencies-final.txt','pe-final.txt'):
    files+=list((root/'tests').glob(pattern))
for relative in ('hardware-release3/results.json','software-release/results.json',
                 'system-toast2/result.json','netease-native/result.txt',
                 'scaling-final/results.json','perf-before-final/metrics/latest.csv',
                 'perf-release/metrics/latest.csv','soak-release/metrics/latest.csv',
                 'soak-release/monitor-report.txt','soak-release/result.json'):
    files.append(root/'tests'/relative)
with zipfile.ZipFile(root/'WinIsland-1.2.3beta-源码.zip','w',zipfile.ZIP_DEFLATED,compresslevel=9) as archive:
    for file in sorted(set(files)):
        if file.is_file():archive.write(file,file.relative_to(root))
print('Source ZIP created without build tools, fixture media or private state snapshots.')
