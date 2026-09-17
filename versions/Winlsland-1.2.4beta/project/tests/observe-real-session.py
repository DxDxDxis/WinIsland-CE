"""Real-source observation only: no media fixture, network override or chat text logging."""
import argparse,ctypes as C,json,subprocess,time
from ctypes import wintypes as W
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--exe',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--seconds',type=int,default=240);a=p.parse_args()
out=a.output.resolve();out.mkdir(exist_ok=False)
app=subprocess.Popen([str(a.exe.resolve()),'--observe',str(out)])
def state():
 try:return dict(x.split('=',1) for x in (out/'state.txt').read_text('utf-8').splitlines() if '=' in x)
 except (ValueError,OSError):return {}
def send(c):
 f=out/'command.txt';f.write_text(c,'utf-8');end=time.monotonic()+10
 while f.exists() and time.monotonic()<end:time.sleep(.025)
keys=('MusicPlaying','InfoSource','LyricState','LyricProvider','LyricIndex','PositionSeconds','TimelineReason','NoticesVisible','NoticesCompleted','NoticeApplication','NoticeCorrelation','NoticeAlpha','MusicExpanded','Build')
records=[];previous=None
try:
 end=time.monotonic()+12
 while not state().get('Version') and time.monotonic()<end:time.sleep(.05)
 send('monitor-start');print('REAL SOURCE OBSERVER READY',flush=True)
 deadline=time.monotonic()+a.seconds;opened=False
 while time.monotonic()<deadline and not (out/'stop.request').exists():
  s=state();snapshot={k:s.get(k) for k in keys}
  if snapshot!=previous:
   snapshot['time']=time.strftime('%Y-%m-%d %H:%M:%S');records.append(snapshot);previous={k:s.get(k) for k in keys}
   (out/'observations.json').write_text(json.dumps(records,ensure_ascii=False,indent=2),'utf-8')
  if not opened and s.get('HasMusic')=='True':send('music-expand');opened=True
  time.sleep(.5)
 send('monitor-stop');time.sleep(.2);send('monitor-export');time.sleep(.3)
 final=state()
 (out/'result.json').write_text(json.dumps({'latest':{k:final.get(k) for k in keys},'report':final.get('ExportPath'),'mode':'real-observation'},ensure_ascii=False,indent=2),'utf-8')
 print('REAL SOURCE OBSERVER COMPLETED',flush=True)
finally:
 (out/'exit.request').write_text('exit');app.wait(15)
