"""Regression checks against the actual EXE; all media/notifications here are synthetic."""
import argparse,json,os,sqlite3,subprocess,threading,time
from pathlib import Path
from http.server import BaseHTTPRequestHandler,ThreadingHTTPServer
from urllib.parse import urlparse,parse_qs
p=argparse.ArgumentParser();p.add_argument('--exe',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
root=Path(__file__).resolve().parents[1];out=a.output.resolve();out.mkdir(exist_ok=False)
calls=[];results=[]
class API(BaseHTTPRequestHandler):
 def log_message(self,*args):pass
 def do_GET(self):
  q=parse_qs(urlparse(self.path).query);calls.append(self.path)
  obj={'trackName':q.get('track_name',[''])[0],'artistName':'测试歌手','albumName':'本机验证','duration':180,'syncedLyrics':'[00:00.00]first\n[00:03.00]second\n[00:08.00]third'}
  raw=json.dumps(obj,ensure_ascii=False).encode();self.send_response(200);self.end_headers();self.wfile.write(raw)
server=ThreadingHTTPServer(('127.0.0.1',0),API);threading.Thread(target=server.serve_forever,daemon=True).start()
db=sqlite3.connect(out/'notifications.db');db.execute('PRAGMA journal_mode=WAL');db.execute('CREATE TABLE Notification(Id INTEGER PRIMARY KEY,HandlerId,ArrivalTime,Payload,Type,[Order],DataVersion)');db.commit()
env=os.environ.copy();env['WINISLAND_TEST_LYRIC_PORT']=str(server.server_port)
app=subprocess.Popen([str(a.exe.resolve()),'--verify',str(out)],env=env);fixture=None
def state():
 try:return dict(x.split('=',1) for x in (out/'state.txt').read_text('utf-8').splitlines() if '=' in x)
 except (OSError,ValueError):return {}
def wait(fn,label,timeout=12):
 end=time.monotonic()+timeout
 while time.monotonic()<end:
  s=state()
  if fn(s):return s
  time.sleep(.02)
 raise AssertionError(label)
def send(c):
 f=out/'command.txt';f.write_text(c,'utf-8');wait(lambda s:not f.exists(),c)
def media(c):
 f=out/'media/media-command.txt';f.write_text(c,'utf-8');wait(lambda s:not f.exists(),c)
def check(ok,label):
 results.append({'pass':bool(ok),'test':label});print(('PASS ' if ok else 'FAIL ')+label,flush=True)
try:
 wait(lambda s:s.get('ReaderHealthy')=='True','start');send('monitor-start');send('lyric-source=1')
 (out/'media').mkdir();fixture=subprocess.Popen([str(root/'tests/bin/WinIsland-MediaFixture.exe'),str(out/'media')]);send('media-fixture-only')
 wait(lambda s:s.get('LyricsLoaded')=='True','load');media('seek=4');wait(lambda s:s.get('FixtureLyric')=='second','seek')
 before=len(calls);media('timeline-off');wait(lambda s:s.get('TimelineValid')=='False','missing progress');time.sleep(.6)
 media('timeline-on');wait(lambda s:s.get('TimelineValid')=='True' and s.get('FixtureLyric')=='second','progress restored')
 check(len(calls)==before,'Same song losing/recovering progress does not re-fetch lyrics')
 check('同步不可用' not in state().get('LyricStatus',''),'Recovered timeline clears unavailable status')
 send('music-expand');send('seconds=1');wait(lambda s:s.get('Animating')=='False','settle')
 arrival=int((time.time()+11644473600)*1e7)
 def notice(body):
  raw=f'<toast><visual><binding template="ToastGeneric"><text>synthetic notice</text><text>{body}</text></binding></visual></toast>'.encode()
  db.execute('INSERT OR REPLACE INTO Notification VALUES(?,?,?,?,?,?,?)',(9,55,arrival,raw,'toast',9,1));db.commit()
 notice('initial');wait(lambda s:s.get('NoticeClosing')=='True','closing',8)
 notice('updated during close');time.sleep(.75)
 check(state().get('NoticeTitle')=='synthetic notice' and state().get('NoticeClosing')=='False','Changed payload during contraction receives a fresh visible dwell')
 wait(lambda s:not s.get('NoticeCorrelation') and s.get('Animating')=='False','drain notices',8)
 send('music-expand');wait(lambda s:s.get('Animating')=='False','music reopened')
 prior=int(state().get('NoticesVisible','0'));media('qq-show')
 wait(lambda s:s.get('NoticeApplication')=='QQ-popup-fixture' and int(s.get('NoticesVisible','0'))>prior,'native popup displayed',8)
 check(True,'QQ-style native popup uses independent WinEvent/UIA path and is visibly rendered (fixture)')
 check(state()['MusicExpanded']=='True','Native popup preserves expanded music controls')
 media('qq-hide');wait(lambda s:not s.get('NoticeCorrelation'),'popup drains',8)
 prior=int(state().get('NoticesVisible','0'));media('qq-show')
 wait(lambda s:int(s.get('NoticesVisible','0'))>prior,'repeated popup displayed',8)
 check(True,'Recreated popup with identical text is a separate arrival')
 media('qq-hide')
 send('monitor-stop');wait(lambda s:s.get('MonitorPending')=='False','stop');send('monitor-export');s=wait(lambda s:s.get('ExportPath'),'export')
 report=Path(s['ExportPath']).read_text('utf-8')
 check(Path(s['ExportPath']).is_relative_to(out),'Test report stays inside test directory')
 check('运行模式: 测试' in report,'Diagnostic report explicitly identifies test run')
 check(all(x in report for x in ('stage=discovered','stage=parsed','stage=enqueued','stage=visible','stage=ended')),'Notification lifecycle includes parse, queue, visible render and completion')
 check('updated_utc_100ns=' in report and 'index=' in report and 'HTTP/API' not in report,'Timeline and response diagnostics are unambiguous')
 check('Synthetic popup body' not in report and 'updated during close' not in report,'Report omits notification content')
finally:
 (out/'exit.request').write_text('exit');app.wait(15)
 if fixture and fixture.poll() is None:
  (out/'media/media-command.txt').write_text('exit');fixture.wait(10)
 server.shutdown();db.close();(out/'result.json').write_text(json.dumps(results,indent=2),'utf-8')
raise SystemExit(0 if all(x['pass'] for x in results) else 1)
