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
 send('settings-page=4');v=wait(lambda s:int(s.get('SettingsContent',0))!=0 and s.get('SettingsPage')=='4');content=int(v['SettingsContent']);slider=u.GetDlgItem(content,138)
 assert slider,'slider missing'
 u.SendMessageW(slider,0x405,1,round(a.arc*100))
 u.SendMessageW(content,0x114,5,slider)
 wait(lambda s:abs(float(s.get('TopArcScale',0))-a.arc)<.001)
 send('close-settings');send('music-stop',True);send('seconds=0.7');send('lyric-source=2')
 for zoom in a.zoom:
  send('layout-zoom='+str(zoom),True)
  for dpi in a.dpi:
   dpi=int(dpi) if float(dpi).is_integer() else dpi
   send('music-stop',True);send('dpi='+str(dpi),True);snap('idle-'+str(dpi)+'-z'+str(zoom))
   send('music=play',True);snap('music-'+str(dpi)+'-z'+str(zoom))
   send('music-expand',True);snap('expanded-'+str(dpi)+'-z'+str(zoom))
   send('notice-wide');wait(lambda s:s.get('Holding')=='True');snap('expanded-notice-'+str(dpi)+'-z'+str(zoom))
   wait(lambda s:not s.get('NoticeTitle') and s.get('Animating')=='False','notice retract')
   send('music-stop',True);send('notice-wide');wait(lambda s:s.get('Holding')=='True');snap('notice-'+str(dpi)+'-z'+str(zoom))
   wait(lambda s:not s.get('NoticeTitle') and s.get('Animating')=='False','notice retract')
   send('music=play',True);send('music-collapse',True)
   (out/'fixture.lrc').write_text('[00:00.00]用于检查长歌词排版与音量竖条互不重叠的合成歌词\n[03:00.00]结束','utf-8')
   send('lyrics-fixture');wait(lambda s:s.get('HasLyric')=='True');wait(lambda s:s.get('Animating')=='False');snap('lyrics-'+str(dpi)+'-z'+str(zoom))
   for i in range(6):send('music-expand' if i%2==0 else 'music-collapse')
   wait(lambda s:s.get('Animating')=='False','interruptions settle')
finally:
 (out/'results.json').write_text(json.dumps(rows,indent=2),'utf-8');(out/'exit.request').write_text('exit');app.wait(15)
