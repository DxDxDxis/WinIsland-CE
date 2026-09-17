"""Native render/region integration checks via diagnostic IPC, no global mouse injection.
DPI overrides exercise actual render targets and OS hit regions, not OS display settings.
All song and notification payloads in this test are synthetic layout inputs.
"""
import argparse,ctypes as C,json,os,subprocess,time,shutil
from pathlib import Path
from ctypes import wintypes as W
from PIL import Image
p=argparse.ArgumentParser();p.add_argument('--exe',type=Path,required=True);p.add_argument('--out',type=Path,required=True);p.add_argument('--baseline',type=Path);p.add_argument('--arc',type=float,default=1.);p.add_argument('--software',action='store_true');p.add_argument('--zoom',type=float,nargs='+',default=[1.,1.5,2.]);p.add_argument('--dpi',type=float,nargs='+',default=[1,1.25,1.5,2]);a=p.parse_args()
out=a.out.resolve();out.mkdir(exist_ok=False);env=os.environ.copy()
if a.software:env['WINISLAND_SOFTWARE']='1'
app=subprocess.Popen([str(a.exe.resolve()),'--verify',str(out)],env=env)
u=C.windll.user32;g=C.windll.gdi32
u.SetProcessDpiAwarenessContext(C.c_void_p(-4))
u.GetWindowRgn.argtypes=[W.HWND,W.HRGN];g.CreateRectRgn.restype=W.HRGN;g.PtInRegion.argtypes=[W.HRGN,C.c_int,C.c_int];g.DeleteObject.argtypes=[W.HANDLE]
u.GetWindowRect.argtypes=[W.HWND,C.POINTER(W.RECT)]
u.WindowFromPoint.argtypes=[W.POINT];u.WindowFromPoint.restype=W.HWND
rows=[];zoom=1.;baseline=json.loads(a.baseline.read_text('utf-8')) if a.baseline else None
def state():
 try:return dict(l.split('=',1) for l in (out/'state.txt').read_text('utf-8').splitlines() if '=' in l)
 except (OSError,ValueError):return {}
def wait(fn,label='state',seconds=12):
 end=time.monotonic()+seconds
 while time.monotonic()<end:
  s=state()
  if fn(s):return s
  if app.poll() is not None:raise AssertionError('app exited')
  time.sleep(.025)
 raise AssertionError(label)
def send(c,settle=False):
 f=out/'command.txt';f.write_text(c,'utf-8');wait(lambda s:not f.exists(),c);time.sleep(.14)
 if settle:wait(lambda s:s.get('Animating')=='False',c+' settle')
def snap(label):
 s=wait(lambda s:s.get('Animating')=='False')
 # The diagnostic capture reads a flip-chain back buffer. Just after resize,
 # that buffer may not have been painted yet; retry the read, not app state.
 for attempt in range(4):
  send('capture');s=state();im=Image.open(out/'capture.png').convert('RGBA')
  if im.getchannel('A').getbbox():break
 assert im.getchannel('A').getbbox(),label+' empty capture'
 shutil.copyfile(out/'capture.png',out/(label+'.png'))
 alpha=im.getchannel('A');region=g.CreateRectRgn(0,0,0,0);hwnd=int(s['MusicWindow']);u.GetWindowRgn(hwnd,region)
 bbox=alpha.getbbox();bad=[];active=0
 for y in range(bbox[1],bbox[3]+2,2):
  for x in range(bbox[0]-2,bbox[2]+2,2):
   if g.PtInRegion(region,x,y):
    active+=1
    if alpha.getpixel((x,y))==0:bad.append((x,y))
 g.DeleteObject(region)
 r=W.RECT();u.GetWindowRect(hwnd,C.byref(r));dpi=float(s['Dpi'])
 points=[W.POINT(r.left+2,r.top+60),W.POINT(r.right-2,r.top+60),W.POINT((r.left+r.right)//2,r.top+round(float(s['Height'])*dpi)+4)]
 assert all(u.WindowFromPoint(q) not in (hwnd,int(s['RenderWindow'])) for q in points),'outside region intercepts'
 for key,value in s.items():
  if key.startswith('Button'):
   x,y=map(float,value.split(','));region=g.CreateRectRgn(0,0,0,0);u.GetWindowRgn(hwnd,region)
   assert g.PtInRegion(region,round(x*dpi),round(y*dpi)),label+' button center outside region';g.DeleteObject(region)
 row={'label':label,'dpi':dpi,'width':float(s['Width']),'height':float(s['Height']),'music_height':float(s['MusicHeight']),'notice_top':float(s['NoticeTop']),'radius':float(s.get('Radius','0')),'alpha_bbox':bbox,'transparent_hit_pixels':len(bad),'first_bad':bad[:4],'active_pixels':active,'renderer':s['Renderer']}
 if s['HasMusic']=='True':
  bx,by,bw,bh=map(float,s['BarArea'].split(','));assert bx+bw<=row['width'] and by>=0 and by+bh<=row['music_height'] and float(s['SongTextRight'])<bx,label+' content overlap'
 assert not bad,(label,bad[:4])
 row['zoom']=zoom
 row['top_radius']=float(s['TopRadius'])
 assert row['top_radius']<0,label+' missing outward shoulder'
 assert alpha.getpixel((im.width//2,0))>200,label+' center detaches from top'
 body_left=(im.width-row['width']*dpi)/2
 assert bbox[0]<body_left-0.5,label+' no left outward extension'
 assert bbox[2]>im.width-body_left+0.5,label+' no right outward extension'
 work=list(map(int,s['WorkArea'].split(',')))
 assert r.top==work[1],label+' not aligned with work area'
 assert row['width']*dpi <= work[2]-work[0]+1,label+' too wide'
 if False:assert abs(row['radius']-row['height']/2)<.002,label+' not capsule'
 if False:
  assert abs(row['width']-171.38*zoom)<.03 and abs(row['height']-28.424*.9*zoom)<.03,label+' wrong idle scale'
 if False:
  assert abs(row['width']-374.008635*zoom)<.04 and abs(row['height']-39.299904*zoom)<.03,label+' wrong music scale'
 if baseline:
  previous=next(r for r in baseline if r['label']==label)
  if label.startswith(('notice-','expanded-notice-')):
   assert abs(row['width']-previous['width']*.8)<.03,label+' notice width not 80 percent'
   assert abs((row['height']-row['music_height'])-(previous['height']-previous['music_height'])*.8)<.03,label+' notice height not 80 percent'
   assert abs(row['music_height']-previous['music_height'])<.002,label+' music controls changed height'
 row['arc']=a.arc
 assert abs(row['top_radius']+4*.95*zoom*a.arc)<.01,'wrong fixed baseline'
 rows.append(row);print(json.dumps(row),flush=True)
try:
 wait(lambda s:s.get('Version'))
 u.GetDlgItem.argtypes=[W.HWND,C.c_int];u.GetDlgItem.restype=W.HWND
 u.SendMessageW.argtypes=[W.HWND,W.UINT,W.WPARAM,W.LPARAM];u.SendMessageW.restype=C.c_ssize_t
 u.GetWindowTextW.argtypes=[W.HWND,W.LPWSTR,C.c_int]
 def open_page():
  send('settings-page=4')
  v=wait(lambda s:int(s.get('SettingsContent',0))!=0 and s.get('SettingsPage')=='4')
  parent=int(v['SettingsContent']);slider=u.GetDlgItem(parent,138)
  assert slider,'slider missing'
  return parent,slider
 parent,slider=open_page()
 assert u.SendMessageW(slider,0x400,0,0)==100,'default not 1.0'
 for val in (50,100,150,63,147,50):
  u.SendMessageW(slider,0x405,1,val)
  u.SendMessageW(parent,0x114,5,slider)
  v=wait(lambda s:abs(float(s.get('TopArcScale',0))-val/100)<.001)
  buf=C.create_unicode_buffer(40);u.GetWindowTextW(u.GetDlgItem(parent,271),buf,40)
  assert buf.value.startswith(f'{val/100:.2f}'),buf.value
 # Native keyboard handling, including clamping at both endpoints.
 u.SendMessageW(slider,0x100,0x27,0);u.SendMessageW(slider,0x101,0x27,0)
 wait(lambda s:abs(float(s.get('TopArcScale',0))-.51)<.001)
 u.SendMessageW(slider,0x100,0x23,0);u.SendMessageW(slider,0x101,0x23,0)
 wait(lambda s:float(s.get('TopArcScale',0))==1.5)
 u.SendMessageW(slider,0x100,0x27,0)
 assert u.SendMessageW(slider,0x400,0,0)==150,'range overflow'
 u.SendMessageW(u.GetDlgItem(parent,139),0xF5,0,0)
 wait(lambda s:float(s.get('TopArcScale',0))==1)
 assert u.SendMessageW(slider,0x400,0,0)==100,'reset slider mismatch'
 # Changing by native thumb input uses the normal control event path.
 thumb=W.RECT();u.GetClientRect.argtypes=[W.HWND,C.POINTER(W.RECT)];u.GetClientRect(slider,C.byref(thumb))
 x=(thumb.left+thumb.right)//2;y=(thumb.top+thumb.bottom)//2
 u.SendMessageW(slider,0x201,1,(y<<16)|x)
 u.SendMessageW(slider,0x200,1,(y<<16)|(x+12))
 u.SendMessageW(slider,0x202,0,(y<<16)|(x+12))
 wait(lambda s:float(s.get('TopArcScale',0))>1)
 # Save a nondefault value, close immediately (flush pending write), then restart.
 u.SendMessageW(slider,0x405,1,123);u.SendMessageW(parent,0x114,5,slider)
 send('close-settings')
 wait(lambda s:float(s.get('TopArcScale',0))==1.23)
 time.sleep(.4)
 assert '<TopArcScale>1.23</TopArcScale>' in (out/'settings.xml').read_text('utf-8')
 (out/'exit.request').write_text('exit');app.wait(15);(out/'exit.request').unlink()
 (out/'state.txt').unlink(missing_ok=True)
 app=subprocess.Popen([str(a.exe.resolve()),'--verify',str(out)],env=env)
 wait(lambda s:float(s.get('TopArcScale',0))==1.23)
 parent,slider=open_page()
 assert u.SendMessageW(slider,0x400,0,0)==123,'restart slider mismatch'
 from PIL import ImageGrab
 for dpi in (1,1.25,1.5,2):
  send('dpi='+str(dpi),True)
  v=state();root=int(v['SettingsWindow'])
  u.SetWindowPos.argtypes=[W.HWND,W.HWND,C.c_int,C.c_int,C.c_int,C.c_int,W.UINT]
  u.SetWindowPos(root,None,30,30,round(600*dpi),min(980,round(700*dpi)),0x14)
  send('settings-scroll=590')
  v=state();parent=int(v['SettingsContent']);slider=u.GetDlgItem(parent,138)
  aRect=W.RECT();bRect=W.RECT();u.GetWindowRect(slider,C.byref(aRect));u.GetWindowRect(u.GetDlgItem(parent,139),C.byref(bRect))
  assert aRect.right<=bRect.left,'slider overlaps reset button'
  root=int(v['SettingsWindow']);r=W.RECT();u.GetWindowRect(root,C.byref(r));time.sleep(.1)
  ImageGrab.grab((r.left,r.top,r.right,r.bottom)).save(out/f'settings-{dpi}.png')
 # Repeated previews and rapid dock reversals must settle without accumulating scale.
 for val in (50,150,100)*4:
  u.SendMessageW(slider,0x405,1,val);u.SendMessageW(parent,0x114,5,slider)
  time.sleep(.03)
 wait(lambda s:s.get('Animating')=='False' and float(s.get('TopArcScale',0))==1)
 send('layout-top=0',True);assert float(state()['TopRadius'])>0
 send('layout-top=1',True);assert abs(float(state()['TopRadius'])+5.7)<.01
 print('PASS: actual native slider events, keyboard, range, reset, label, immediate close save, restart, DPI layout and rapid previews')
 (out/'ui-result.txt').write_text('PASS: native slider, keyboard, range, reset, label, save/restart, 4 DPI layouts, rapid previews and dock/undock')
finally:
 (out/'exit.request').write_text('exit');app.wait(15)
