"""Exercise the running real observer's controls; no fixture or network override.
Temporarily pauses/resumes the actual selected player and minimizes/restores
CloudMusic to verify that stale accessibility lyrics are not called synchronized.
"""
import argparse, ctypes as C, json, time
from ctypes import wintypes as W
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('observer',type=Path);a=p.parse_args();out=a.observer
u=C.windll.user32
u.SendMessageW.argtypes=[W.HWND,W.UINT,W.WPARAM,W.LPARAM];u.SendMessageW.restype=C.c_ssize_t
u.ShowWindowAsync.argtypes=[W.HWND,C.c_int]
u.IsIconic.argtypes=[W.HWND];u.GetAncestor.argtypes=[W.HWND,W.UINT];u.GetAncestor.restype=W.HWND
u.GetWindowThreadProcessId.argtypes=[W.HWND,C.POINTER(W.DWORD)]
u.GetClassNameW.argtypes=[W.HWND,W.LPWSTR,C.c_int]
def state():
 try:return dict(x.split('=',1) for x in (out/'state.txt').read_text('utf-8').splitlines() if '=' in x)
 except (OSError,ValueError):return {}
def wait(fn,label,seconds=20):
 end=time.monotonic()+seconds
 while time.monotonic()<end:
  s=state()
  if fn(s):return s
  time.sleep(.1)
 raise AssertionError(label)
def send(c):
 f=out/'command.txt';f.write_text(c,'utf-8');wait(lambda s:not f.exists(),c)
def toggle():
 send('music-expand');s=wait(lambda s:'Button2' in s and s.get('Busy')=='False','control ready')
 x,y=map(float,s['Button2'].split(','));dpi=float(s['Dpi']);lp=int(x*dpi)|(int(y*dpi)<<16);h=int(s['MusicWindow'])
 u.SendMessageW(h,0x201,1,lp);u.SendMessageW(h,0x202,0,lp)
results=[]
def check(ok,label):
 results.append({'pass':bool(ok),'test':label});print(('PASS ' if ok else 'FAIL ')+label,flush=True)
initial=wait(lambda s:s.get('LyricState')=='player_line','real player lyric line')
paused_by_test=False
try:
 seen=set();end=time.monotonic()+14
 while time.monotonic()<end:
  s=state()
  if s.get('LyricState')=='player_line':seen.add(s.get('LyricIndex'))
  time.sleep(.3)
 check(len(seen)>1,'Real CloudMusic current line changes in island (no fake seconds timeline)')
 if initial['MusicPlaying']=='True':
  toggle();wait(lambda s:s.get('MusicPlaying')=='False','real pause');paused_by_test=True
  time.sleep(.6);index=state().get('LyricIndex');time.sleep(2)
  check(state().get('MusicPlaying')=='False' and index==state().get('LyricIndex'),'Island pause controls CloudMusic; lyric freezes')
  toggle();wait(lambda s:s.get('MusicPlaying')=='True','real resume');paused_by_test=False
  check(True,'Island resume controls CloudMusic; state returns to playing')
 # Locate only the real CloudMusic top-level window via its actual process path.
 windows=[];cbtype=C.WINFUNCTYPE(W.BOOL,W.HWND,W.LPARAM)
 k=C.windll.kernel32;k.OpenProcess.argtypes=[W.DWORD,W.BOOL,W.DWORD];k.OpenProcess.restype=W.HANDLE
 k.QueryFullProcessImageNameW.argtypes=[W.HANDLE,W.DWORD,W.LPWSTR,C.POINTER(W.DWORD)];k.CloseHandle.argtypes=[W.HANDLE]
 def enum(h,l):
  pid=W.DWORD();u.GetWindowThreadProcessId(h,C.byref(pid));handle=k.OpenProcess(0x1000,False,pid.value)
  if handle:
   text=C.create_unicode_buffer(32768);n=W.DWORD(32768)
   if k.QueryFullProcessImageNameW(handle,0,text,C.byref(n)) and Path(text.value).name.lower()=='cloudmusic.exe':
    cls=C.create_unicode_buffer(128);u.GetClassNameW(h,cls,128)
    if cls.value=='OrpheusBrowserHost':windows.append(h)
   k.CloseHandle(handle)
  return True
 cb=cbtype(enum);u.EnumWindows(cb,0)
 if windows:
  h=windows[0];was_min=u.IsIconic(h)
  try:
   u.ShowWindowAsync(h,6)
   wait(lambda s:s.get('LyricState')=='no_progress' and s.get('LyricSyncSource')=='none','minimized line invalidated')
   check(True,'Minimized CloudMusic frozen DOM is rejected as live synchronization')
   u.ShowWindowAsync(h,4)
   wait(lambda s:s.get('LyricState')=='player_line','restored player line')
   check(True,'Restoring player reacquires its actual highlighted lyric')
  finally:
   if was_min:u.ShowWindowAsync(h,6)
 else:results.append({'pass':None,'test':'CloudMusic minimize/restore: main window class not found'})
 send('music-collapse')
finally:
 if paused_by_test:
  toggle();wait(lambda s:s.get('MusicPlaying')=='True','restore original playback')
 (out/'real-controls.json').write_text(json.dumps(results,ensure_ascii=False,indent=2),'utf-8')
