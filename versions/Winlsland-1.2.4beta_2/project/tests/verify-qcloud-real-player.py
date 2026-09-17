"""Actual installed CloudMusic + public HTTPS, isolated settings, no fake sessions."""
import argparse,ctypes as C,json,os,subprocess,time
from pathlib import Path
from ctypes import wintypes as W
p=argparse.ArgumentParser();p.add_argument('--exe',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--player-window',type=int,required=True);p.add_argument('--lyric-source',type=int,default=5);p.add_argument('--previous-count',type=int,default=0);a=p.parse_args()
out=a.output.resolve();out.mkdir(exist_ok=False);exe=a.exe.resolve();u=C.windll.user32;h=a.player_window
u.IsIconic.argtypes=[W.HWND];u.ShowWindowAsync.argtypes=[W.HWND,C.c_int]
u.GetForegroundWindow.restype=W.HWND;u.SetForegroundWindow.argtypes=[W.HWND]
u.GetDlgItem.argtypes=[W.HWND,C.c_int];u.GetDlgItem.restype=W.HWND
u.SendMessageW.argtypes=[W.HWND,W.UINT,W.WPARAM,W.LPARAM];u.SendMessageW.restype=C.c_ssize_t
original_min=u.IsIconic(h);fg=u.GetForegroundWindow();results=[];snapshots=[];paused=False;advanced=False;previous_count=0
env=os.environ.copy();env['PATH']=os.path.join(os.environ['SYSTEMROOT'],'System32')
for key in tuple(env):
 if key.startswith(('QT_','QML_','WINISLAND_TEST_')):env.pop(key)
app=subprocess.Popen([str(exe),'--observe',str(out)],env=env)
def state():
 try:return dict(x.split('=',1) for x in (out/'state.txt').read_text('utf-8').splitlines() if '=' in x)
 except (OSError,ValueError):return {}
def wait(fn,label,seconds=30):
 end=time.monotonic()+seconds
 while time.monotonic()<end:
  s=state()
  if fn(s):return s
  time.sleep(.1)
 raise AssertionError(label+'; latest='+str({k:state().get(k) for k in ('LyricStatus','LyricState','MusicPlaying')}))
def send(c):
 f=out/'command.txt';f.write_text(c,'utf-8');wait(lambda s:not f.exists(),c,10)
def check(ok,label):
 results.append({'pass':bool(ok),'test':label});print(('PASS ' if ok else 'LIMITATION ')+label,flush=True)
def snap(phase):
 s=state();keys=('Version','Build','InfoSource','FixtureTitle','LyricsLoaded','LyricSource','LyricProvider','LyricState','LyricIndex','LyricSyncSource','PositionSeconds','TimelineValid','TimelineReason','LyricRequest','MusicPlaying')
 snapshots.append({'phase':phase,'minimized':bool(u.IsIconic(h)),**{k:s.get(k) for k in keys}});return s
def click(button):
 send('music-expand');s=wait(lambda s:'Button'+str(button) in s and s.get('Busy')=='False','control ready')
 x,y=map(float,s['Button'+str(button)].split(','));dpi=float(s['Dpi']);lp=int(x*dpi)|(int(y*dpi)<<16);w=int(s['MusicWindow'])
 u.SendMessageW(w,0x201,1,lp);u.SendMessageW(w,0x202,0,lp)
def api(phase):
 report=out/(phase+'-qcloud.txt');r=subprocess.run([str(exe),'--qcloud-network-test',str(report)],env=env,timeout=25)
 text=report.read_text('utf-8') if report.exists() else ''
 check(r.returncode==0 and text.count('PASS:')==3,phase+': packaged helper / both real HTTPS endpoints / parsed lyric lines / cancel')
try:
 wait(lambda s:s.get('Version')=='1.2.4beta_2','startup');send('monitor-start');send('settings')
 s=wait(lambda s:int(s.get('SettingsContent','0'))>0,'settings');v=int(s['SettingsContent']);combo=u.GetDlgItem(v,121)
 u.SendMessageW(combo,0x14e,5,0);u.SendMessageW(v,0x111,121|(1<<16),combo)
 wait(lambda s:s.get('LyricSource')=='5','select QCloud');send('close-settings');send('settings')
 s=wait(lambda s:int(s.get('SettingsContent','0'))>0,'reopened');v=int(s['SettingsContent'])
 check(u.SendMessageW(u.GetDlgItem(v,121),0x147,0,0)==5 and '<LyricSource>5</LyricSource>' in (out/'settings.xml').read_text('utf-8'),'QCloud selection saves and survives settings reopen')
 if a.lyric_source!=5:
  combo=u.GetDlgItem(v,121);u.SendMessageW(combo,0x14e,a.lyric_source,0);u.SendMessageW(v,0x111,121|(1<<16),combo)
  wait(lambda s:s.get('LyricSource')==str(a.lyric_source),'test source selected')
 send('close-settings');u.ShowWindowAsync(h,9);u.SetForegroundWindow(h)
 for i in range(a.previous_count):
  before=state().get('FixtureTitle');click(1);wait(lambda s:s.get('FixtureTitle') and s.get('FixtureTitle')!=before,'select previous test song');previous_count+=1
 wait(lambda s:s.get('LyricsLoaded')=='True' and (a.lyric_source!=5 or s.get('LyricProvider','').startswith('QCloudMusicApi')),'real identified song -> matching -> lyrics',50)
 first=snap('foreground');check(True,'Actual SMTC song matched and loaded: '+first['LyricProvider'])
 wait(lambda s:s.get('LyricState')=='player_line','visible lyric observation');send('capture');api('visible')
 first=snap('before-minimize');title=first['FixtureTitle'];request=first['LyricRequest']
 u.ShowWindowAsync(h,6);time.sleep(4);initial=state();line=initial.get('FixtureLyric');changes=0
 for i in range(16):
  time.sleep(.5);s=state()
  if s.get('LyricState') in ('player_line','synced') and s.get('FixtureLyric')!=line:changes+=1
  line=s.get('FixtureLyric')
 s=snap('minimized');send('capture');api('minimized')
 check(s.get('FixtureTitle')==title and s.get('LyricsLoaded')=='True' and s.get('InfoSource')=='SMTC','Minimized retains actual SMTC metadata and lyrics')
 check(s.get('LyricRequest')==request,'Minimizing does not generate a new lyric request')
 check(changes>0,'Actual minimized synchronized line continues advancing')
 if changes==0:check(s.get('LyricState')=='no_progress' and state().get('FixtureLyric','').startswith('静态歌词'),'Missing progress retains clearly marked static text')
 if s.get('MusicPlaying')=='True':
  click(2);wait(lambda s:s.get('MusicPlaying')=='False','pause');paused=True;snap('minimized-paused')
  click(2);wait(lambda s:s.get('MusicPlaying')=='True','resume');paused=False;snap('minimized-resumed')
  check(bool(u.IsIconic(h)),'Real pause / resume controls work without restoring CloudMusic')
 click(3);wait(lambda s:s.get('FixtureTitle') and s.get('FixtureTitle')!=title,'next song');advanced=True
 s=snap('minimized-next');check(s.get('LyricState') not in ('player_line','synced'),'Next song clears previous highlighted lyric')
 next_state=wait(lambda s:s.get('LyricsLoaded')=='True' or s.get('LyricState') in ('no_match','network_unavailable'),'next lyric outcome',30)
 check(next_state.get('LyricsLoaded')=='True','Next real song has an unambiguous lyric match')
 click(1);wait(lambda s:s.get('FixtureTitle')==title,'previous song');advanced=False
 wait(lambda s:s.get('LyricsLoaded')=='True','previous lyrics');snap('minimized-previous')
 check(bool(u.IsIconic(h)),'Real next / previous and matched lyrics work minimized')
 u.ShowWindowAsync(h,9);wait(lambda s:s.get('LyricState')=='player_line','restored real current line');snap('restored')
 check(True,'Restore reacquires real line; separate from minimized synchronization')
 send('monitor-stop');wait(lambda s:s.get('MonitorPending')=='False','monitor stopped');send('monitor-export')
finally:
 if app.poll() is None:
  try:send('monitor-export')
  except Exception:pass
 if paused:
  try:click(2)
  except Exception:pass
 if advanced:
  try:click(1)
  except Exception:pass
 for i in range(previous_count):
  try:
   before=state().get('FixtureTitle');click(3);wait(lambda s:s.get('FixtureTitle') and s.get('FixtureTitle')!=before,'restore playlist position')
  except Exception:break
 u.ShowWindowAsync(h,6 if original_min else 9)
 if fg:u.SetForegroundWindow(fg)
 (out/'exit.request').write_text('exit');app.wait(15)
 (out/'results.json').write_text(json.dumps({'mode':'real sources, isolated settings, clean dependency PATH','checks':results,'snapshots':snapshots},ensure_ascii=False,indent=2),'utf-8')
raise SystemExit(0 if all(r['pass'] for r in results) else 2)
