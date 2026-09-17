"""Real CloudMusic window-state / native HTTPS checks; no injected responses."""
import argparse,ctypes as C,json,subprocess,time
from ctypes import wintypes as W
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--observer',type=Path,required=True);p.add_argument('--exe',type=Path,required=True);p.add_argument('--player-window',type=int,required=True);a=p.parse_args();out=a.observer;h=a.player_window
u=C.windll.user32
u.ShowWindowAsync.argtypes=[W.HWND,C.c_int];u.IsIconic.argtypes=[W.HWND];u.IsWindowVisible.argtypes=[W.HWND]
u.GetForegroundWindow.restype=W.HWND;u.SetForegroundWindow.argtypes=[W.HWND]
u.SendMessageW.argtypes=[W.HWND,W.UINT,W.WPARAM,W.LPARAM];u.SendMessageW.restype=C.c_ssize_t
results=[];snapshots=[];original_min=u.IsIconic(h);foreground=u.GetForegroundWindow();paused_by_test=False;advanced=False
def state():
 try:return dict(x.split('=',1) for x in (out/'state.txt').read_text('utf-8').splitlines() if '=' in x)
 except (ValueError,OSError):return {}
def wait(fn,label,seconds=25):
 end=time.monotonic()+seconds
 while time.monotonic()<end:
  s=state()
  if fn(s):return s
  time.sleep(.1)
 raise AssertionError(label)
def send(c):
 f=out/'command.txt';f.write_text(c,'utf-8');wait(lambda s:not f.exists(),c)
def click(button):
 send('music-expand');s=wait(lambda s:'Button'+str(button) in s and s.get('Busy')=='False','controls ready')
 x,y=map(float,s['Button'+str(button)].split(','));dpi=float(s['Dpi']);lp=int(x*dpi)|(int(y*dpi)<<16);w=int(s['MusicWindow'])
 u.SendMessageW(w,0x201,1,lp);u.SendMessageW(w,0x202,0,lp)
def check(ok,label):
 results.append({'pass':bool(ok),'test':label});print(('PASS ' if ok else 'FAIL ')+label,flush=True)
def snapshot(phase):
 s=state();keys=['HasMusic','MusicPlaying','InfoSource','LyricsLoaded','LyricState','LyricProvider','TimelineValid','TimelineReason','LyricRequest','NoticesVisible','NoticesCompleted']
 snapshots.append({'phase':phase,'window_minimized':bool(u.IsIconic(h)),**{k:s.get(k) for k in keys}})
 return s
def api(phase):
 for option in ['--lyric-providers-test','--lyric-network-test']:
  path=out/(phase+option+'.txt');r=subprocess.run([str(a.exe.resolve()),option,str(path.resolve())],timeout=40)
  text=path.read_text('utf-8') if path.exists() else ''
  check(r.returncode==0 and text.startswith('PASS'),phase+' '+option+' native HTTPS + parsed/matched lyrics')
try:
 u.ShowWindowAsync(h,9);u.SetForegroundWindow(h)
 first=wait(lambda s:s.get('LyricState')=='player_line','real visible current lyric');api('visible');snapshot('foreground')
 if foreground and foreground!=h:u.SetForegroundWindow(foreground)
 time.sleep(2);snapshot('background');first=state();title=first.get('FixtureTitle');request=first.get('LyricRequest')
 u.ShowWindowAsync(h,6);s=wait(lambda s:s.get('LyricState')=='no_progress','minimized real state')
 snapshot('minimized');api('minimized');s=state()
 check(s.get('FixtureTitle')==title and s.get('HasMusic')=='True' and s.get('LyricsLoaded')=='True' and s.get('InfoSource')=='SMTC','Minimization retains actual SMTC song, session and parsed lyrics')
 check(s.get('LyricRequest')==request,'Minimization does not restart lyric search or download')
 check(s.get('FixtureLyric','').startswith('静态歌词') and s.get('LyricSyncSource')=='none','Missing real progress is shown as retained static lyrics, never fake sync')
 if s.get('MusicPlaying')=='True':
  click(2);wait(lambda s:s.get('MusicPlaying')=='False','pause minimized');paused_by_test=True;snapshot('minimized-paused')
  check(bool(u.IsIconic(h)),'Island pause works while CloudMusic remains minimized')
  click(2);wait(lambda s:s.get('MusicPlaying')=='True','resume minimized');paused_by_test=False;snapshot('minimized-resumed')
  check(bool(u.IsIconic(h)),'Island resume works while CloudMusic remains minimized')
 click(3);new=wait(lambda s:s.get('FixtureTitle') and s.get('FixtureTitle')!=title,'next minimized');advanced=True
 check(not new.get('FixtureLyric') or new.get('LyricState') not in ('player_line','synced'),'Minimized next song does not inherit old synchronized lyric')
 wait(lambda s:s.get('LyricsLoaded')=='True','next lyric lookup');snapshot('minimized-next')
 click(1);wait(lambda s:s.get('FixtureTitle')==title,'previous minimized');advanced=False
 wait(lambda s:s.get('LyricsLoaded')=='True','previous lyric cache');snapshot('minimized-previous')
 check(bool(u.IsIconic(h)),'Minimized previous restores original song with matched lyrics')
 # Hidden-to-tray equivalent window state, without sending WM_CLOSE (which may exit the player).
 u.ShowWindowAsync(h,0);time.sleep(3);s=snapshot('hidden-window')
 check(s.get('HasMusic')=='True' and s.get('LyricsLoaded')=='True','Hiding the player window does not discard its media session or lyrics')
 u.ShowWindowAsync(h,9);wait(lambda s:s.get('LyricState')=='player_line','restore actual current line');snapshot('restored')
 check(True,'Restored current line is reacquired; this is separate from minimized sync capability')
 send('music-collapse');send('monitor-export')
finally:
 if paused_by_test:
  click(2);wait(lambda s:s.get('MusicPlaying')=='True','restore original playback')
 if advanced:
  try:click(1)
  except Exception:pass
 u.ShowWindowAsync(h,6 if original_min else 9)
 if foreground:u.SetForegroundWindow(foreground)
 (out/'minimized-results.json').write_text(json.dumps({'checks':results,'states':snapshots,'tray_note':'hidden window is a controlled visibility check, not a test of the player tray-close policy'},ensure_ascii=False,indent=2),'utf-8')
