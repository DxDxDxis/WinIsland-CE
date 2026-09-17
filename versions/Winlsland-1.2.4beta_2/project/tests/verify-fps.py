"""Compare ETW result with another process's counted, successful DXGI presents."""
import ctypes as C, subprocess,time,json,argparse
from ctypes import wintypes as W
from pathlib import Path
root=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser();parser.add_argument('--output',type=Path,required=True);args=parser.parse_args();out=args.output.resolve();out.mkdir(exist_ok=False)
u=C.windll.user32;u.GetForegroundWindow.restype=W.HWND;u.SetForegroundWindow.argtypes=[W.HWND];u.ShowWindow.argtypes=[W.HWND,C.c_int]
u.GetWindowThreadProcessId.argtypes=[W.HWND,C.POINTER(W.DWORD)];old=u.GetForegroundWindow()
app=subprocess.Popen([str(root/'release/最终成品/WinIsland-1.2.4beta.exe'),'--verify',str(out)])
graphics=None;results=[]
def state():
    try:return dict(x.split('=',1) for x in (out/'state.txt').read_text('utf-8').splitlines() if '=' in x)
    except (OSError,ValueError):return {}
def wait(fn,seconds=12):
    end=time.monotonic()+seconds
    while time.monotonic()<end:
        s=state()
        if fn(s):return s
        time.sleep(.1)
    raise AssertionError(str(state()))
def send(c):
    p=out/'command.txt';p.write_text(c);wait(lambda s:not p.exists());time.sleep(.2)
def focus(h):
    msg=W.MSG();u.PeekMessageW(C.byref(msg),None,0,0,0)
    thread=u.GetWindowThreadProcessId(u.GetForegroundWindow(),None);own=C.windll.kernel32.GetCurrentThreadId();u.AttachThreadInput(own,thread,True)
    try:u.ShowWindow(h,9);u.SetForegroundWindow(h)
    finally:u.AttachThreadInput(own,thread,False)
    assert u.GetForegroundWindow()==h, f'Foreground acquisition failed: expected {h}, actual {u.GetForegroundWindow()}'
try:
    wait(lambda s:s.get('Version')=='1.2.4beta');send('show-fps=on')
    directory=out/'graphics';directory.mkdir()
    graphics=subprocess.Popen([str(root/'tests/bin/WinIsland-GraphicsFixture.exe'),str(directory)])
    until=time.monotonic()+12
    while not (directory/'ready.txt').exists():
        assert time.monotonic()<until;time.sleep(.05)
    pid,h=map(int,(directory/'ready.txt').read_text().split());focus(h)
    wait(lambda s:int(s.get('Foreground','0'))==pid and int(s.get('Fps','-1'))>0,20)
    for _ in range(5):
        s=state();actual=float((directory/'frames.txt').read_text() or '-1')
        if int(s.get('Foreground','0'))==pid and int(s.get('Fps','-1'))>0:results.append({'app_fps':int(s['Fps']),'producer_fps':actual})
        time.sleep(1.2)
    assert results and all(abs(v['app_fps']-v['producer_fps'])<max(10,v['producer_fps']*.2) for v in results),results
    print('PASS: real foreground DXGI FPS agrees with producer frame count',results,flush=True)
    (directory/'pause.request').write_text('pause');wait(lambda s:s.get('Fps')=='-1',8)
    print('PASS: no DXGI presents shows --',flush=True)
    (directory/'pause.request').unlink();u.ShowWindow(h,6);time.sleep(2)
    assert int(state().get('Foreground','0'))!=pid
    print('PASS: minimized foreground source is removed',flush=True)
    focus(h);wait(lambda s:int(s.get('Foreground','0'))==pid and int(s.get('Fps','-1'))>0)
    (directory/'exit.request').write_text('exit');graphics.wait(8);wait(lambda s:int(s.get('Foreground','0'))!=pid)
    send('show-fps=off');time.sleep(1.5)
    traces=subprocess.run(['logman','query','-ets'],capture_output=True).stdout.decode(errors='replace')
    assert 'WinIsland.FPS.'+str(app.pid) not in traces
    print('PASS: game exit clears source and disabling FPS releases ETW session',flush=True)
finally:
    (out/'exit.request').write_text('exit');app.wait(15)
    if graphics and graphics.poll() is None:(directory/'exit.request').write_text('exit');graphics.wait(8)
    u.SetForegroundWindow(old)
    (out/'result.json').write_text(json.dumps(results,indent=2))
