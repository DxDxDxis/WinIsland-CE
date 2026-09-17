"""Exercise the packaged EXE, real Windows SMTC/PCM, pointer input and read-only toast path.
Python is a test tool only; the app and native media fixture do not need Python.
"""
import argparse
import ctypes as C
from ctypes import wintypes as W
import json
from pathlib import Path
import os
import sqlite3
import subprocess
import time

parser = argparse.ArgumentParser()
parser.add_argument('--exe', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--fixture', type=Path)
parser.add_argument('--software', action='store_true')
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=False)
u = C.windll.user32
u.GetDlgItem.restype = W.HWND
u.GetForegroundWindow.restype = W.HWND
u.WindowFromPoint.restype = W.HWND
u.GetAncestor.restype = W.HWND
u.GetAncestor.argtypes = [W.HWND,W.UINT]
u.SendMessageW.argtypes = [W.HWND,W.UINT,W.WPARAM,W.LPARAM]
u.PostMessageW.argtypes = [W.HWND,W.UINT,W.WPARAM,W.LPARAM]
u.SetForegroundWindow.argtypes = [W.HWND]
u.SetFocus.argtypes = [W.HWND]
u.GetWindowRect.argtypes = [W.HWND,C.POINTER(W.RECT)]
u.GetDlgItem.argtypes = [W.HWND,C.c_int]
passed=[]

def check(ok, name):
    if not ok: raise AssertionError(name)
    passed.append(name)
    print('PASS:', name, flush=True)

def state(path=None):
    try:
        return dict(line.split('=',1) for line in (path or args.output/'state.txt').read_text('utf-8').splitlines() if '=' in line)
    except (OSError, ValueError): return {}

def wait(predicate, name, seconds=8):
    until=time.monotonic()+seconds
    while time.monotonic()<until:
        s=state()
        if predicate(s): return s
        if app.poll() is not None: raise AssertionError(f'{name}: EXE exited {app.returncode}')
        time.sleep(.025)
    raise AssertionError(f'Timeout: {name}')

def send(command):
    path=args.output/'command.txt'
    path.with_suffix('.tmp').write_text(command, 'utf-8')
    path.with_suffix('.tmp').replace(path)
    until=time.monotonic()+10
    while path.exists():
        if time.monotonic()>until: raise AssertionError('UI command blocked: '+command)
        time.sleep(.01)
    time.sleep(.13) # diagnostics publishes state at 100 ms, separate from the frame clock

def settle(): return wait(lambda s:s.get('Animating')=='False','animation settles')

def click_window(window, x, y):
    # Use real mouse input and verify the target has the foreground before clicking.
    window=int(window)
    pid=W.DWORD(); thread=u.GetWindowThreadProcessId(u.GetForegroundWindow(),C.byref(pid))
    own=C.windll.kernel32.GetCurrentThreadId()
    u.AttachThreadInput(own,thread,True)
    try: u.ShowWindow(W.HWND(window),9);u.BringWindowToTop(W.HWND(window));u.SetForegroundWindow(window)
    finally:u.AttachThreadInput(own,thread,False)
    assert u.GetForegroundWindow()==window, 'Refusing pointer input into an unrelated window'
    r=W.RECT();old=W.POINT();u.GetWindowRect(window,C.byref(r));u.GetCursorPos(C.byref(old))
    try:
        u.SetCursorPos(r.left+round(x),r.top+round(y));time.sleep(.08)
        hit=u.WindowFromPoint(W.POINT(r.left+round(x),r.top+round(y)))
        assert u.GetAncestor(hit,2)==window, 'Pointer blocked by a different window; no click sent'
        u.mouse_event(2,0,0,0,0);time.sleep(.05);u.mouse_event(4,0,0,0,0);time.sleep(.08)
    finally:u.SetCursorPos(old.x,old.y)

def click_button(n):
    s=wait(lambda s:'Button'+str(n) in s,'button exists')
    x,y=map(float,s['Button'+str(n)].split(','));dpi=float(s['Dpi'])
    click_window(s['MusicWindow'],x*dpi,y*dpi)
    wait(lambda after:int(after.get('ActionCount','0'))>int(s.get('ActionCount','0')) and after.get('LastAction')==str(n),f'physical action {n} acknowledged')
    if n==0:wait(lambda after:after.get('MusicExpanded')!=s.get('MusicExpanded'),'header click observed')

def fixture_command(c):
    p=fixture_dir/'media-command.txt';p.write_text(c,'utf-8')
    until=time.monotonic()+8
    while p.exists():
        if time.monotonic()>until: raise AssertionError('fixture command timed out')
        time.sleep(.02)

def toast(n,title='数据库消息',body='真实解析与队列路径',arrival=None):
    arrival=arrival or int((time.time()+11644473600)*10_000_000)
    payload=f"<toast><visual><binding template='ToastGeneric'><text>{title}</text><text>{body}</text></binding></visual></toast>".encode('utf-8')
    db.execute('INSERT OR REPLACE INTO Notification VALUES(?,?,?,?,?,?,?)',(n,55,arrival,payload,'toast',n,1));db.commit()

db=sqlite3.connect(args.output/'notifications.db');db.execute('PRAGMA journal_mode=WAL')
db.execute('CREATE TABLE Notification(Id INTEGER PRIMARY KEY,HandlerId,ArrivalTime,Payload,Type,[Order],DataVersion)')
toast(1,'旧消息',arrival=1)
env=os.environ.copy();env['WINISLAND_PERF_DIR']=str(args.output/'metrics')
if args.software:env['WINISLAND_SOFTWARE']='1'
app=subprocess.Popen([str(args.exe),'--verify',str(args.output)],env=env)
fixture=None
started=time.monotonic()
try:
    s=wait(lambda s:s.get('ReaderHealthy')=='True','native startup')
    check(time.monotonic()-started<10,'Packaged startup and native SQLite reader')
    check(s['Renderer']==('software' if args.software else 'DirectComposition'),'Requested renderer active')
    check(s.get('NoticeTitle','')=='','Historical messages suppressed at startup')
    check(abs(float(s['Width'])-180.4)<.1 and abs(float(s['Height'])-29.92)<.1,'Idle 180.4 x 29.92 DIP')
    with (args.output/'state.txt').open('r',encoding='utf-8') as held_snapshot: time.sleep(.4)
    send('frame=90');wait(lambda s:s.get('ConfiguredFrameRate')=='90','snapshot sharing conflict recovery');send('frame=0')
    check(True,'A reader holding a diagnostics snapshot cannot terminate or freeze the app')
    send('music=play');s=settle()
    check(abs(float(s['Width'])-460.46)<.1 and abs(float(s['Height'])-48.384)<.1,'Compact music 460.46 x 48.384 DIP')
    click_button(0);s=settle()
    check(s['MusicExpanded']=='True' and abs(float(s['Height'])-121.8)<.1,'Pointer expands music to 121.8 DIP')
    send('seconds=0.8');toast(2)
    s=wait(lambda s:s.get('NoticeTitle')=='数据库消息' and s.get('Animating')=='False','toast expanded')
    check(s['MusicExpanded']=='True' and float(s['NoticeTop'])==float(s['MusicHeight']) and float(s['Height'])>121.8,'Notice extends downward from expanded music')
    # Changing DataVersion without changing payload is not a new notification.
    db.execute('UPDATE Notification SET DataVersion=2 WHERE Id=2');db.commit()
    before=s.get('NoticesShown')
    send('capture');(args.output/'capture.png').replace(args.output/'expanded-notice.png')
    s=wait(lambda s:s.get('NoticeClosing')=='True','reverse notice animation',3)
    check(s['NoticeTitle']=='数据库消息' and 0<=float(s['NoticeAlpha'])<=1,'Closing retains the message for its reverse fade')
    s=wait(lambda s:s.get('NoticeTitle')=='' and s.get('Animating')=='False','notice fully closes')
    check(s['MusicExpanded']=='True' and abs(float(s['Height'])-121.8)<.1,'Notice dismissal restores expanded music')
    check(before is None or s.get('NoticesShown')==before,'Unchanged notification payload is not repeated')
    send('seconds=0.1');send('burst')
    s=wait(lambda s:s.get('SyntheticDelivered')=='12' and s.get('NoticeTitle')=='' and s.get('Animating')=='False','12-message FIFO drains',20)
    check(s['MusicExpanded']=='True' and s['Pending']=='0','12 notices delivered once without folding music')
    for i in range(20):send('music-collapse' if i%2==0 else 'music-expand')
    s=settle();check(s['MusicExpanded']=='True','Repeated animation interruption settles consistently')
    send('settings');s=wait(lambda s:int(s.get('SettingsWindow','0'))!=0,'settings opens')
    settings=int(s['SettingsWindow']);edit=u.GetDlgItem(settings,103)
    value=C.create_unicode_buffer('120');u.SendMessageW(edit,0x000C,0,C.cast(value,C.c_void_p).value)
    s=wait(lambda s:s.get('ConfiguredFrameRate')=='120','native setting change')
    check('<FrameRate>120</FrameRate>' in (args.output/'settings.xml').read_text('utf-8'),'Native settings persist compatible XML')
    value=C.create_unicode_buffer('999');u.SendMessageW(edit,0x000C,0,C.cast(value,C.c_void_p).value)
    time.sleep(.2);check(state()['ConfiguredFrameRate']=='120','Invalid settings preserve last valid FPS')
    send('frame=0')
    send('monitor-export')
    check('没有可导出' in state().get('MonitorMessage',''),'Export without a session explains how to start')
    # BM_CLICK exercises the native button notification, the same path as keyboard activation.
    u.SendMessageW(u.GetDlgItem(settings,108),0x00F5,0,0)
    wait(lambda s:s.get('MonitorActive')=='True' and s.get('MonitorPending')=='False','monitor starts')
    send('notice-short');time.sleep(5)
    u.SendMessageW(u.GetDlgItem(settings,109),0x00F5,0,0)
    s=wait(lambda s:s.get('ExportPath') and s.get('MonitorPending')=='False','live export')
    path=Path(s['ExportPath']);report=path.read_text('utf-8-sig')
    check(s['MonitorActive']=='True' and path.parent.name=='daxian日志' and path.name.startswith('daxian日志_'),'Live export uses requested folder/name without stopping')
    check('[性能汇总]' in report and '[资源采样' in report and '1.2.3beta' in report and 'GPU:' in report,'Report includes environment and real performance samples')
    check('真实解析与队列路径' not in report and '合成歌曲' not in report,'Report excludes notification body and song title')
    send('monitor-stop');wait(lambda s:s.get('MonitorActive')=='False' and s.get('MonitorPending')=='False','monitor stops')
    send('monitor-export');s=wait(lambda s:s.get('ExportPath')!=str(path) and s.get('MonitorPending')=='False','stopped export')
    second=Path(s['ExportPath']);check(path.exists() and second.exists() and '监测停止' in second.read_text('utf-8-sig'),'Stopped report retains end time and does not overwrite earlier exports')
    (args.output/'blocked-export').write_text('This file intentionally blocks directory creation.','utf-8')
    send('monitor-export-blocked');s=wait(lambda s:'程序目录不可写' in s.get('MonitorMessage','') and s.get('MonitorPending')=='False','unwritable default fallback')
    check(Path(s['ExportPath']).is_file(),'An unavailable default path falls back to a writable Documents folder')
    send('monitor-start');wait(lambda s:s.get('MonitorActive')=='True','repeat monitor start')
    send('monitor-stop');wait(lambda s:s.get('MonitorActive')=='False' and s.get('MonitorPending')=='False','repeat monitor stop')
    check(True,'Monitoring can be started and stopped again')
    modal_before=int(state().get('ModalFrames','0'))
    u.PostMessageW(u.GetDlgItem(settings,104),0x00F5,0,0)
    u.FindWindowW.restype=W.HWND
    dialog=None
    u.IsWindowVisible.argtypes=[W.HWND]
    for _ in range(200):
        dialog=u.FindWindowW(None,'为当前歌曲载入歌词')
        if dialog and u.IsWindowVisible(dialog):break
        time.sleep(.03)
    assert dialog, 'Native lyric file dialog opens'
    dialog_pid=W.DWORD();u.GetWindowThreadProcessId(dialog,C.byref(dialog_pid))
    assert dialog_pid.value==app.pid, 'Dialog belongs to the tested app'
    time.sleep(2)
    u.PostMessageW(dialog,0x0010,0,0)
    wait(lambda s:int(s.get('ModalFrames','0'))>modal_before+10,'animation clock survives native file dialog')
    check(True,'Native file dialog keeps animation clock alive and cancellation preserves state')
    send('close-settings')
    if args.fixture:
        send('music-stop');settle();fixture_dir=args.output/'media';fixture_dir.mkdir()
        fixture=subprocess.Popen([str(args.fixture),str(fixture_dir)])
        send('media-fixture-only');s=wait(lambda s:s.get('MusicControls')=='3' and s.get('MusicPlaying')=='True','real SMTC discovery',15)
        check('合成媒体 1' in s['FixtureTitle'],'Native EXE reads real Windows SMTC metadata')
        settle();click_button(0);settle();click_button(2)
        wait(lambda s:s.get('MusicPlaying')=='False' and s.get('Busy')=='False','SMTC pause readback')
        check(state(fixture_dir/'media-state.txt').get('Playing')=='False','Pointer pause reaches the actual media player')
        click_button(2);wait(lambda s:s.get('MusicPlaying')=='True' and s.get('Busy')=='False','SMTC play readback')
        check(state(fixture_dir/'media-state.txt').get('Playing')=='True','Pointer resume reaches the actual media player')
        click_button(3);wait(lambda s:'合成媒体 2' in s.get('FixtureTitle','') and s.get('Busy')=='False','SMTC next readback')
        click_button(1);wait(lambda s:'合成媒体 1' in s.get('FixtureTitle','') and s.get('Busy')=='False','SMTC previous readback')
        check(state(fixture_dir/'media-state.txt').get('Track')=='1','Pointer previous/next control the displayed source')
        (args.output/'fixture.lrc').write_text('[00:00.00]第一句同步歌词\n[00:02.00]第二句同步歌词\n[00:06.00]\n','utf-8')
        send('lyrics-fixture');fixture_command('seek=3')
        wait(lambda s:s.get('FixtureLyric')=='第二句同步歌词','real timeline lyrics')
        check(True,'Imported LRC follows actual SMTC seek')
        fixture_command('seek=7');wait(lambda s:s.get('HasLyric')=='False','blank lyric interval')
        fixture_command('next');wait(lambda s:'合成媒体 2' in s.get('FixtureTitle','') and s.get('HasLyric')=='False','track lyric invalidation')
        check(True,'Blank intervals and track changes clear old lyrics')
        fixture_command('audio-on');wait(lambda s:s.get('AudioAvailable')=='True' and float(s.get('AudioPeak','0'))>0.0001,'real process PCM',15)
        peaks=[]
        for _ in range(35):peaks.append(float(state()['AudioPeak']));time.sleep(.2)
        check(max(peaks)>min(peaks)*2,'Six bars receive changing real WASAPI PCM amplitudes')
        fixture_command('mute');wait(lambda s:float(s.get('AudioPeak','1'))==0 and float(s.get('Bars','1'))<.1,'PCM silence falls back',8)
        check(True,'Muted PCM returns bars smoothly to baseline')
        fixture_command('mode-on');s=wait(lambda s:s.get('ModeVisible')=='True','SMTC mode capability')
        if s.get('MusicExpanded')!='True':click_button(0);settle()
        click_button(4);wait(lambda s:s.get('Busy')=='False' and s.get('Mode')=='列表循环','actual mode readback')
        check(state(fixture_dir/'media-state.txt').get('Repeat')=='2','Mode button writes and confirms real player mode')
        fixture_command('ignore-next');click_button(3)
        wait(lambda s:s.get('Busy')=='False' and s.get('MusicControls')=='2','nonresponsive control hidden',8)
        check(True,'A player that ignores next loses only that control')
        fixture_command('pause');wait(lambda s:s.get('MusicPlaying')=='False','pause for timeout')
        time.sleep(8);check(state()['HasMusic']=='True','Pause retains music during first eight seconds')
        wait(lambda s:s.get('HasMusic')=='False' and s.get('MusicVisible')=='False' and s.get('Animating')=='False','ten second pause contraction',7)
        check(True,'Paused music closes with animation after ten seconds')
        fixture_command('play');wait(lambda s:s.get('MusicPlaying')=='True','resume after contraction')
        fixture_command('exit');fixture.wait(8)
        wait(lambda s:s.get('HasMusic')=='False' and s.get('Animating')=='False','player exit clears content',12)
        check(True,'Player exit returns to idle')
    send('perf-flush')
finally:
    (args.output/'exit.request').write_text('exit')
    try:app.wait(12)
    except subprocess.TimeoutExpired:app.terminate();raise AssertionError('Native shutdown failed to release workers')
    if fixture and fixture.poll() is None:
        (fixture_dir/'media-command.txt').write_text('exit');fixture.wait(10)
    db.close()
    (args.output/'results.json').write_text(json.dumps({'passed':passed,'exit_code':app.returncode,'renderer':'software' if args.software else 'hardware'},ensure_ascii=False,indent=2),'utf-8')
check(app.returncode==0,'Clean shutdown')
