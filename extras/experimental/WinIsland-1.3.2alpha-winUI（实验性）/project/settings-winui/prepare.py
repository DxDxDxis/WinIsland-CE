from pathlib import Path
import zipfile,subprocess,shutil
root=Path(__file__).resolve().parents[1]
sdk=root/'dependency-cache/winui/appsdk'
out=root/'settings-winui/generated'
out.mkdir(parents=True,exist_ok=True)
framework=root/'dependency-cache/winui/framework'
if not (framework/'Microsoft.UI.Xaml.dll').exists():
 with zipfile.ZipFile(sdk/'tools/MSIX/win10-x64/Microsoft.WindowsAppRuntime.1.6.msix') as z:z.extractall(framework)
kit=Path('C:/Program Files (x86)/Windows Kits/10')
args=[str(kit/'bin/10.0.22621.0/x64/cppwinrt.exe'),'-output',str(out.relative_to(root)),'-input',str(kit/'UnionMetadata/10.0.22621.0/Windows.winmd')]
for folder in ['uap10.0','uap10.0.17763']:
 for p in sorted((sdk/'lib'/folder).glob('*.winmd')):args+=['-input',str(p.relative_to(root))]
for p in (root/'dependency-cache/winui/webview-metadata').rglob('*.winmd'):args+=['-input',str(p.relative_to(root))]
# Use Windows short paths for the legacy tool; all physical files remain here.
import ctypes
buf=ctypes.create_unicode_buffer(32768)
assert ctypes.windll.kernel32.GetShortPathNameW(str(root),buf,len(buf))
short=Path(buf.value)
args=[str(root/'dependency-cache/winui/cppwinrt/bin/cppwinrt.exe')]+args[1:]
args=[str(short/a) if a.startswith(('settings-winui','dependency-cache')) else a for a in args]
subprocess.run(args,check=True,cwd=short)
print('Native WinRT projections ready',flush=True)

(out/'complete.txt').write_text('2.0.240405.15',encoding='ascii')
