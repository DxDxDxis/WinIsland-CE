"""Packaged app, actual Windows SMTC + audio, controlled HTTP transport, native UI events."""
import argparse, ctypes as C, json, os, subprocess, threading, time
from ctypes import wintypes as W
from pathlib import Path
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs

p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--exe',type=Path);p.add_argument('--software',action='store_true');p.add_argument('--retry-recovery',action='store_true');a=p.parse_args()
root=Path(__file__).resolve().parents[1];a.output=a.output.resolve();a.output.mkdir(parents=True,exist_ok=False)
u=C.windll.user32;u.SendMessageW.argtypes=[W.HWND,W.UINT,W.WPARAM,W.LPARAM];u.SendMessageW.restype=C.c_ssize_t
u.GetDlgItem.argtypes=[W.HWND,C.c_int];u.GetDlgItem.restype=W.HWND
u.GetWindowRect.argtypes=[W.HWND,C.POINTER(W.RECT)];u.WindowFromPoint.restype=W.HWND
mode='ok';requests=[];passed=[]
class API(BaseHTTPRequestHandler):
    def log_message(self,*args):pass
    def do_GET(self):
        q=parse_qs(urlparse(self.path).query);requests.append((time.monotonic(),mode,q));current=mode
        if current=='slow':time.sleep(3)
        search=urlparse(self.path).path.endswith('/search')
        code=429 if current=='limit' else 503 if current=='error' else 404 if current in ('search','ambiguous') and not search else 200
        title=q.get('track_name',[''])[0];artist=q.get('artist_name',[''])[0];album=q.get('album_name',[''])[0]
        obj={'trackName':title if current!='mismatch' else 'wrong song','artistName':artist,'albumName':album if current not in ('search','ambiguous') else 'Other compilation','duration':180,
            'syncedLyrics':None if current=='empty' else '[offset:500]\n[00:00.50]API first\n[00:03.50]API current\n[00:03.50]translation\n[00:09.00]\n[02:59.00]end'}
        body=json.dumps(([obj,{**obj,'syncedLyrics':'[00:01.00]different recording'}] if current=='ambiguous' else [obj]) if search else obj,ensure_ascii=False).encode()
        if current=='invalid':body=b'not json'
        try:
            self.send_response(code);self.send_header('Content-Type','application/json');self.send_header('Content-Length',str(len(body)));self.end_headers()
            self.wfile.write(body)
        except (BrokenPipeError,ConnectionResetError,ConnectionAbortedError):pass
server=ThreadingHTTPServer(('127.0.0.1',0),API);threading.Thread(target=server.serve_forever,daemon=True).start()
env=os.environ.copy();env['WINISLAND_TEST_LYRIC_PORT']=str(server.server_port);env['WINISLAND_PERF_DIR']=str(a.output/'metrics')
if a.software:env['WINISLAND_SOFTWARE']='1'
exe=a.exe.resolve() if a.exe else root/'release/成品/WinIsland-1.2.4beta.exe';app=subprocess.Popen([str(exe),'--verify',str(a.output)],env=env)
fixture=None;media=a.output/'media'
def state():
    for _ in range(12):
        try:
            value=dict(x.split('=',1) for x in (a.output/'state.txt').read_text('utf-8').splitlines() if '=' in x)
            if value:return value
        except (OSError,ValueError):pass
        time.sleep(.01)
    return {}
def wait(predicate,name,seconds=10):
    end=time.monotonic()+seconds
    while time.monotonic()<end:
        s=state()
        if predicate(s):return s
        if app.poll() is not None:raise AssertionError(f'app exit {app.returncode}')
        time.sleep(.025)
    raise AssertionError(name+'; state='+str(state()))
def check(ok,name):
    if not ok:raise AssertionError(name+'; state='+str(state()))
    passed.append(name);print('PASS:',name,flush=True)
def send(command):
    path=a.output/'command.txt';path.with_suffix('.tmp').write_text(command,'utf-8');path.with_suffix('.tmp').replace(path)
    wait(lambda s:not path.exists(),'command '+command);time.sleep(.13)
def command(command):
    path=media/'media-command.txt';path.write_text(command,'utf-8')
    until=time.monotonic()+8
    while path.exists():
        assert time.monotonic()<until;time.sleep(.02)
def settle():return wait(lambda s:s.get('Animating')=='False','settle')
def native_button(n,down=True,up=True):
    if n in (1,2,3,4) and state().get('MusicExpanded')!='True':send('music-expand');settle()
    s=wait(lambda s:'Button'+str(n) in s,'native input geometry');x,y=map(float,s['Button'+str(n)].split(','));dpi=float(s['Dpi']);lp=round(x*dpi)|(round(y*dpi)<<16);h=int(s['MusicWindow'])
    if down:u.SendMessageW(h,0x201,1,lp)
    if up:u.SendMessageW(h,0x202,0,lp)
def capture(name):
    send('capture');(a.output/'capture.png').replace(a.output/(name+'.png'))
try:
    s=wait(lambda s:s.get('Version')=='1.2.4beta','startup');settle()
    check(s['SongSource']=='1' and s['LyricSource']=='0' and s['ShowFps']=='False' and s['ShowPing']=='False','New defaults and standalone EXE startup')
    check(abs(float(s['Width'])-180.4)<.1 and abs(float(s['Height'])-29.92)<.1,'Baseline idle size unchanged')
    native_button(5,up=False);time.sleep(.3)
    check(state()['HoldTriggered']=='False' and float(state()['PressFeedback'])>0,'Press feedback precedes 600 ms threshold')
    u.SendMessageW(int(s['MusicWindow']),0x200,1,1|(180<<16));time.sleep(.3)
    check(state()['HoldTriggered']=='False' and float(state()['PressFeedback'])<.02,'Moving outside cancels early hold feedback')
    native_button(5,up=False);wait(lambda s:s.get('HoldTriggered')=='True','long press')
    capture('idle-long-press');native_button(5,down=False);time.sleep(.35)
    check(float(state()['PressFeedback'])<.02 and state()['HoldTriggered']=='False','Long press gives feedback and release restores exact size')
    send('show-fps=on');settle();capture('idle-fps')
    check(state()['ShowFps']=='True','FPS enabled with placeholder where unsupported')
    send('show-fps=off');send('ping-target=127.0.0.1');send('show-ping=on')
    wait(lambda s:int(s.get('Ping','-1'))>=0,'real loopback ICMP');capture('idle-ping')
    check(state()['PingStatus']=='ICMP 往返延迟' and state()['PingTarget']=='127.0.0.1','Real ICMP reply supplies milliseconds')
    send('ping-target=::1');wait(lambda s:s.get('PingTarget')=='::1' and int(s.get('Ping','-1'))>=0,'real IPv6 loopback ICMP')
    check(True,'IPv6 echo returns real milliseconds');send('ping-target=127.0.0.1')
    send('show-fps=on');settle();capture('idle-both')
    send('seconds=0.6');send('notice-long');wait(lambda s:s.get('NoticeTitle') and s.get('Animating')=='False','notice below metrics')
    check(abs(float(state()['NoticeTop'])-29.92)<.1,'Idle metrics reserve a header above notifications')
    capture('idle-metrics-notice');wait(lambda s:s.get('NoticeTitle')=='' and s.get('Animating')=='False','notice closes')
    send('ping-target=192.0.2.1');wait(lambda s:s.get('PingTarget')=='192.0.2.1' and s.get('Ping')=='-1','ICMP timeout',8)
    send('ping-target=127.0.0.1');wait(lambda s:int(s.get('Ping','-1'))>=0,'ICMP recovers')
    check(True,'Unreachable target returns placeholder then valid target recovers')
    send('settings');s=wait(lambda s:int(s.get('SettingsWindow','0'))>0,'settings');settings=int(s.get('SettingsContent',s['SettingsWindow']))
    for i in (120,121,122,123,124):check(bool(u.GetDlgItem(settings,i)),f'Native settings control {i} exists')
    u.SendMessageW(u.GetDlgItem(settings,120),0x14e,2,0);u.SendMessageW(settings,0x111,120|(1<<16),u.GetDlgItem(settings,120))
    wait(lambda s:s.get('SongSource')=='2','source native selection');send('song-source=1')
    check('<ShowPing>true</ShowPing>' in (a.output/'settings.xml').read_text('utf-8'),'New settings saved in compatible XML')
    send('close-settings')
    media.mkdir();fixture=subprocess.Popen([str(root/'tests/bin/WinIsland-MediaFixture.exe'),str(media)])
    send('media-fixture-only');s=wait(lambda s:s.get('MusicPlaying')=='True' and s.get('MusicControls')=='3','SMTC metadata',15)
    wait(lambda s:s.get('LyricsLoaded')=='True','API lyrics attached',12)
    send('show-fps=off');send('show-ping=off');settle()
    check(state().get('BarCount')=='12' and abs(float(state()['MusicHeight'])-41.36832)<.1,'Twelve bars and compact music height reduced exactly five percent')
    # The fixture lyrics do not exceed the baseline text width.
    check(abs(float(state()['Width'])-393.6933)<.1,'Music external width reduced five percent without bar-driven expansion')
    area=list(map(float,state()['BarArea'].split(',')))
    check(abs(area[2]-51.6)<.05 and abs(area[3]-18)<.05 and abs(area[0]+area[2]-(float(state()['Width'])-18.65*.855))<.1 and float(state()['SongTextRight'])<area[0], 'Larger 12 bars occupy 51.6 x 18 DIP to the left of the unchanged right edge')
    command('audio-on');wait(lambda s:float(s.get('Bars','0'))>.1,'real audio response',15)
    samples=[]
    for _ in range(25):samples.append(float(state()['Bars']));time.sleep(.12)
    check(max(samples)-min(samples)>.03,'Twelve bars respond to changing real PCM amplitude')
    command('mute');wait(lambda s:float(s.get('Bars','1'))<.081,'mute decay',5)
    check(True,'Real mute decays bars to baseline');command('unmute')
    send('show-fps=on');send('show-ping=on');settle()
    area=list(map(float,state()['BarArea'].split(',')))
    check(float(state()['SongTextRight'])<area[0] and abs(float(state()['Width'])-393.6933)<.1, 'FPS/Ping reserve distinct text space without widening the island or overlapping bars')
    command('seek=4');wait(lambda s:s.get('FixtureLyric')=='API current / translation','API LRC timeline')
    check(True,'Actual SMTC seek synchronizes matched API lyrics with offset and translation')
    send('music-expand');settle();capture('music-both-expanded')
    native_button(2);wait(lambda s:s.get('MusicPlaying')=='False' and s.get('Busy')=='False','pause')
    native_button(2);wait(lambda s:s.get('MusicPlaying')=='True' and s.get('Busy')=='False','resume')
    check(True,'SMTC playback controls remain functional with API and telemetry')
    mode='slow';native_button(3);wait(lambda s:'合成媒体 2' in s.get('FixtureTitle',''),'next')
    check(not state().get('FixtureLyric'),'Track switch immediately clears previous lyrics')
    send('notice-long');wait(lambda s:s.get('NoticeTitle') and s.get('Animating')=='False','notice during slow API')
    check(float(state()['NoticeTop'])==float(state()['MusicHeight']),'Slow API cannot block downward music notification animation')
    mode='ok';command('next');wait(lambda s:'合成媒体 3' in s.get('FixtureTitle','') and s.get('LyricsLoaded')=='True','cancel stale API')
    check(True,'Stale HTTP completion is discarded after the next track')
    wait(lambda s:s.get('NoticeTitle')=='','notice drains');mode='limit'
    send('lyric-source=2');wait(lambda s:s.get('LyricsLoaded')=='False','lyrics disabled')
    count=len(requests);time.sleep(2);check(len(requests)==count,'Disabled lyric mode issues no requests')
    send('lyric-source=0');wait(lambda s:'缓存' in s.get('LyricStatus',''),'cache fallback')
    check(state()['LyricsLoaded']=='True','API 429 uses only matching validated cache in automatic mode')
    send('lyric-source=1');wait(lambda s:'限流' in s.get('LyricStatus',''),'API only rejects cache')
    check(state()['LyricsLoaded']=='False','API-only mode does not silently use the cache')
    mode='mismatch';command('next');wait(lambda s:'合成媒体 4' in s.get('FixtureTitle','') and '暂无匹配' in s.get('LyricStatus',''),'mismatch')
    check(state()['LyricsLoaded']=='False','Mismatched API song cannot display lyrics')
    mode='error';command('next');wait(lambda s:'合成媒体 5' in s.get('FixtureTitle','') and '暂无匹配' in s.get('LyricStatus',''),'limited retries',15)
    count=sum('合成媒体 5' in q.get('track_name',[''])[0] for _,_,q in requests)
    check(count==2,'Server error retries are capped at two attempts')
    for case in ('search','ambiguous','empty','invalid'):
        mode=case;command('next')
        wait(lambda s:'正在获取' not in s.get('LyricStatus','') and (s.get('LyricsLoaded')=='True' if case=='search' else '暂无匹配' in s.get('LyricStatus','')),case+' response',15)
        check(state()['LyricsLoaded']==('True' if case=='search' else 'False'),case+' fallback validates recording and rejects ambiguity/empty/malformed responses')
    if a.retry_recovery:
        mode='error';command('next');wait(lambda s:'暂无匹配' in s.get('LyricStatus',''),'network failure before recovery',15)
        mode='ok';send('notice-long');command('pause');wait(lambda s:s.get('MusicPlaying')=='False','pause during retry backoff');command('play')
        wait(lambda s:s.get('LyricsLoaded')=='True','same song recovers without changing settings',70)
        check(True,'Same track automatically recovers after bounded sixty-second backoff while UI stays responsive')
    send('lyric-source=2');command('audio-on');command('smtc-off')
    wait(lambda s:s.get('HasMusic')=='False','SMTC-only source has no fallback')
    send('song-source=0');wait(lambda s:s.get('InfoSource')=='窗口标题' and s.get('MusicPlaying')=='True','automatic window fallback',15)
    check(state()['MusicControls']=='0' and not state().get('FixtureLyric'),'Validated title plus real audio supports fallback without fake controls/progress')
    send('song-source=2');command('smtc-on');wait(lambda s:s.get('InfoSource')=='窗口标题','strict window title source')
    check(state()['MusicControls']=='0','Window-title-only mode does not adopt SMTC controls')
    send('song-source=0');command('empty-artist');wait(lambda s:s.get('InfoSource')=='SMTC + 窗口标题','metadata enrichment')
    check(state()['MusicControls']=='3','Automatic source fills missing artist without losing matching SMTC controls')
    command('full-metadata');send('song-source=1');wait(lambda s:s.get('InfoSource')=='SMTC','strict SMTC restored')
    mode='ok';send('lyric-source=0');wait(lambda s:s.get('LyricsLoaded')=='True','lyrics restore')
    send('music-expand');send('seconds=0.1');send('burst')
    wait(lambda s:s.get('SyntheticDelivered')=='12' and s.get('NoticeTitle')=='' and s.get('Pending')=='0','notification queue',20)
    check(state()['MusicPlaying']=='True','Twelve queued notifications coexist with music, lyric API and telemetry')
    for scale in (1,1.25,1.5,2):
        send('dpi='+str(scale));settle();capture('music-scale-'+str(scale))
    check(True,'100/125/150/200 percent app DPI layout captures completed')
    send('dpi=1');send('show-fps=off');send('show-ping=off');settle()
    check(state()['ShowFps']=='False' and state()['ShowPing']=='False','Both telemetry switches turn off with no reserved idle area')
    command('exit');fixture.wait(8);wait(lambda s:s.get('HasMusic')=='False' and s.get('Animating')=='False','player exit')
    check(abs(float(state()['Height'])-29.92)<.1,'Player exit restores unchanged idle island')
    send('perf-flush')
finally:
    (a.output/'exit.request').write_text('exit');app.wait(15)
    if fixture and fixture.poll() is None:command('exit');fixture.wait(10)
    server.shutdown()
    (a.output/'results.json').write_text(json.dumps({'passed':passed,'exit_code':app.returncode,'requests':len(requests),'software':a.software},ensure_ascii=False,indent=2),'utf-8')
check(app.returncode==0,'Clean native worker shutdown')
