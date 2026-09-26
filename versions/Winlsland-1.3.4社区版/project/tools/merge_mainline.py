from pathlib import Path
import subprocess,shutil,difflib,json
r=Path(r'E:\aaAAx项目\WinIsland-社区版-大更新\release');a=r/'1.3.2alpha';m=r/'1.3.3beta';t=Path.cwd();changes=[]
for rel in ['source/src/accessibility.cpp','source/src/app.cpp','source/src/render.cpp','source/src/render.h','source/src/settings_bridge.cpp','source/src/settings_host.inc']:
 current=(t/rel).read_text('utf-8-sig');base=(a/rel).read_text('utf-8-sig');remote=(m/rel).read_text('utf-8-sig')
 # Product identity is handled separately, retaining experimental identity during the merge.
 if rel.endswith('app.cpp'):
  remote=remote.replace('wcscpy_s(tray.szTip, L"WinIsland 1.3.3beta 社区版");','wcscpy_s(tray.szTip, L"WinIsland 1.3.1alpha 社区版");').replace('wi::wide(e.what()).c_str(), L"WinIsland 1.3.3beta 社区版"','wi::wide(e.what()).c_str(), L"WinIsland 1.3.1alpha 社区版"')
 for i,name in enumerate(['current','base','incoming']):
  (t/'verification'/f'merge-{name}.txt').write_text([current,base,remote][i],encoding='utf-8')
 args=['git','merge-file','-p',str(t/'verification/merge-current.txt'),str(t/'verification/merge-base.txt'),str(t/'verification/merge-incoming.txt')]
 p=subprocess.run(args,capture_output=True)
 if p.returncode:raise RuntimeError(rel+' merge conflict '+p.stdout.decode('utf-8'))
 (t/rel).write_bytes(p.stdout);changes.append({'file':rel,'method':'three-way merge using 1.3.2alpha common base'})
for name in ['clipboard.h','clipboard.cpp','clipboard_tests.cpp','clipboard_app.inc']:
 shutil.copy2(m/'source/src'/name,t/'source/src'/name);changes.append({'file':'source/src/'+name,'method':'new mainline file'})
shutil.copy2(m/'sdk/clipboard-host-interface.md',t/'sdk/clipboard-host-interface.md')
p=t/'source/build.ps1';s=p.read_text('utf-8-sig').replace('settings_bridge.cpp transfer.cpp','settings_bridge.cpp clipboard.cpp clipboard_tests.cpp transfer.cpp');p.write_text(s,encoding='utf-8-sig')
(t/'verification/port-manifest.json').write_text(json.dumps(changes,indent=2,ensure_ascii=False),encoding='utf-8')
print('Three-way core merges completed')
