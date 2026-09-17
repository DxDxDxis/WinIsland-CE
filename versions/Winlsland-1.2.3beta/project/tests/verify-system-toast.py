"""Submit/remove only our own Windows toast; inspect the real native reader."""
import argparse, json, os, subprocess, time
from pathlib import Path

p=argparse.ArgumentParser()
p.add_argument('--exe',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--toast-script',type=Path,required=True)
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
app=subprocess.Popen([str(a.exe),'--verify',str(a.output)])
def state():
    try:return dict(l.split('=',1) for l in (a.output/'state.txt').read_text('utf-8').splitlines() if '=' in l)
    except (OSError,ValueError):return {}
shell=os.path.join(os.environ['WINDIR'],'System32','WindowsPowerShell','v1.0','powershell.exe')
try:
    deadline=time.monotonic()+10
    while state().get('ReaderHealthy')!='True':
        assert time.monotonic()<deadline,'System SQLite reader unavailable'
        time.sleep(.1)
    subprocess.run([shell,'-NoProfile','-ExecutionPolicy','Bypass','-File',str(a.toast_script)],check=True,capture_output=True)
    deadline=time.monotonic()+10
    while state().get('NoticeTitle')!='WinIsland 集成测试':
        assert time.monotonic()<deadline,'Submitted toast did not reach native island'
        time.sleep(.05)
    (a.output/'result.json').write_text(json.dumps({'real_windows_toast_displayed':True},indent=2),'utf-8')
    print('PASS: real Windows toast reached native database reader and island')
finally:
    (a.output/'exit.request').write_text('exit')
    app.wait(12)
    env=os.environ.copy();env['WINISLAND_TEST_REMOVE']='1'
    subprocess.run([shell,'-NoProfile','-ExecutionPolicy','Bypass','-File',str(a.toast_script)],env=env,capture_output=True)
