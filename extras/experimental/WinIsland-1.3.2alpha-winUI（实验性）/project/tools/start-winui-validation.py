from pathlib import Path
import subprocess,time,json,shutil,os
root=Path(__file__).resolve().parents[1]
out=root/'verification/native-live';out.mkdir(exist_ok=True)
data=out/('run-'+str(time.time_ns()));data.mkdir()
(data/'mods').mkdir()
for name in ['community-base','community-consumer']:
 shutil.copy2(root/'community-examples/packages'/f'{name}.wimod',data/'mods'/f'{name}.wimod')
 (data/'mods'/name).mkdir();(data/'mods'/name/'host-state.txt').write_text('disabled')
env=os.environ.copy()
for key,folder in [('LOCALAPPDATA','local'),('APPDATA','roaming'),('TEMP','temp'),('TMP','temp')]:
 d=data/folder;d.mkdir(exist_ok=True);env[key]=str(d)
sink=open(out/'process-output.log','a',encoding='utf-8')
started=time.perf_counter()
core=subprocess.Popen([str(root/'release/WinIsland-1.3.2alpha.exe'),'--verify',str(data)],cwd=data,env=env,stdout=sink,stderr=sink)
for i in range(200):
 if (data/'settings-connection.json').exists():break
 time.sleep(.05)
else:raise RuntimeError('Host not ready')
c=json.loads((data/'settings-connection.json').read_text());core_ms=(time.perf_counter()-started)*1000
started=time.perf_counter()
ui=subprocess.Popen([str(root/'settings-winui/build/Release/WinIslandSettings.exe'),'--pipe='+c['pipe'],'--token='+c['token']],cwd=data,env=env,stdout=sink,stderr=sink)
state={'corePid':core.pid,'settingsPid':ui.pid,'data':str(data),'coreReadyMs':core_ms,'uiStartedUnix':time.time()}
(out/'active.json').write_text(json.dumps(state,ensure_ascii=False),encoding='utf-8')
time.sleep(2)
print(json.dumps({'corePid':core.pid,'settingsPid':ui.pid,'settingsExit':ui.poll(),'coreReadyMs':core_ms}))
