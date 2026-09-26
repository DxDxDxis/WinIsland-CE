from pathlib import Path
import subprocess,json,os
root=Path(__file__).resolve().parents[1];p=root/'verification/native-live/active.json';a=json.loads(p.read_text(encoding='utf-8'));d=Path(a['data']);c=json.loads((d/'settings-connection.json').read_text())
env=os.environ.copy()
for key,folder in [('LOCALAPPDATA','local'),('APPDATA','roaming'),('TEMP','temp'),('TMP','temp')]:env[key]=str(d/folder)
with (root/'verification/native-live/process-output.log').open('a') as log:
 ui=subprocess.Popen([str(root/'settings-winui/build/Release/WinIslandSettings.exe'),'--pipe='+c['pipe'],'--token='+c['token']],cwd=d,env=env,stdout=log,stderr=log)
a['settingsPid']=ui.pid;p.write_text(json.dumps(a,ensure_ascii=False),encoding='utf-8');print('Settings PID',ui.pid)
