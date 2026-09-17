"""Real native SMTC and native HTTP pipeline; controlled multi-source responses, no real-player claims."""
import argparse, ctypes as C, json, os, subprocess, threading, time
from pathlib import Path
from http.server import BaseHTTPRequestHandler,ThreadingHTTPServer
from urllib.parse import urlparse,parse_qs
from ctypes import wintypes as W
p=argparse.ArgumentParser();p.add_argument('--exe',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--skip-recovery',action='store_true');a=p.parse_args()
root=Path(__file__).resolve().parents[1];out=a.output.resolve();out.mkdir(exist_ok=False)
mode='exact';calls=[];passed=[];track=1
class API(BaseHTTPRequestHandler):
    def log_message(self,*args):pass
    def do_GET(self):
        global calls
        url=urlparse(self.path);q=parse_qs(url.query);path=url.path;current=mode;calls.append((path,q,current));code=200
        title=q.get('track_name',[f'WinIsland 合成媒体 {track}'])[0];artist=q.get('artist_name',['测试歌手'])[0]
        record={'trackName':title,'artistName':artist,'albumName':'本机验证','duration':180,'syncedLyrics':'[00:00.00]LRCLIB first\n[00:03.00]LRCLIB second'}
        if current=='slow' and path=='/api/get':time.sleep(3)
        if path=='/api/get':
            code=429 if current=='rate' else 503 if current=='network' else 200 if current in ('exact','slow','plain') else 404
            body={**record,'syncedLyrics':None,'plainLyrics':'plain first\nplain second'} if current=='plain' else record
        elif path=='/api/search/get':
            title=q.get('s',[''])[0].removesuffix(' 测试歌手')
            song={'id':3000000001,'name':title,'artists':[{'name':'测试歌手'}],'duration':180000,'album':{'name':'本机验证'}}
            songs=[{**song,'id':1,'duration':240000,'album':{'name':'Other recording'}},{**song,'id':2,'name':title+' (Live)'},song]
            if current in ('fallback','plain','none','network','invalid'):songs=[]
            if current=='ambiguous':songs.append({**song,'id':3000000002})
            if current=='cover':songs=[{**song,'artists':[{'name':'Other'}]}]
            body={'code':200,'result':{'songs':songs}}
            if current=='network':code=503
        elif path in ('/api/song/lyric','/api/song/lyric/v1'):
            body={'code':200,'lrc':{'lyric':'[00:00.00]Net first\n[00:03.00]Net second'}}
            if current=='v1':body={'code':200,'lrc':{'lyric':''},'yrc':{'lyric':'[0,3000](0,1000,0)YRC (1000,2000,0)first\n[3000,3000](3000,1000,0)YRC (4000,2000,0)second'}} if path.endswith('/v1') else {'code':200,'lrc':{'lyric':''}}
        elif path=='/api/search':
            body=[{**record,'syncedLyrics':'[00:00.00]Search first\n[00:03.00]Search second'}] if current=='fallback' else []
        else:code=404;body={}
        raw=json.dumps(body,ensure_ascii=False).encode()
        if current=='invalid':raw=b'broken json'
        try:
            self.send_response(code);self.send_header('Content-Length',str(len(raw)))
            if code==429:self.send_header('Retry-After','60')
            self.end_headers();self.wfile.write(raw)
        except (OSError,ConnectionError):pass
server=ThreadingHTTPServer(('127.0.0.1',0),API);threading.Thread(target=server.serve_forever,daemon=True).start()
env=os.environ.copy();env['WINISLAND_TEST_LYRIC_PORT']=str(server.server_port)
app=subprocess.Popen([str(a.exe.resolve()),'--verify',str(out)],env=env);fixture=None
u=C.windll.user32;u.SendMessageW.argtypes=[W.HWND,W.UINT,W.WPARAM,W.LPARAM];u.SendMessageW.restype=C.c_ssize_t
u.GetDlgItem.argtypes=[W.HWND,C.c_int];u.GetDlgItem.restype=W.HWND
def state():
    try:return dict(x.split('=',1) for x in (out/'state.txt').read_text('utf-8').splitlines() if '=' in x)
    except (OSError,ValueError):return {}
def wait(fn,name,seconds=16):
    end=time.monotonic()+seconds
    while time.monotonic()<end:
        s=state()
        if fn(s):return s
        if app.poll() is not None:raise AssertionError('app exited')
        time.sleep(.03)
    raise AssertionError(name+' '+str(state())+' calls='+str(calls[-10:]))
def send(c):
    path=out/'command.txt';path.write_text(c,'utf-8');wait(lambda s:not path.exists(),c);time.sleep(.16)
def command(c):
    path=out/'media/media-command.txt';path.write_text(c,'utf-8');wait(lambda s:not path.exists(),c)
def check(ok,label):
    assert ok,label;passed.append(label);print('PASS:',label,flush=True)
def next_track(case):
    global mode,track
    mode=case;track+=1;start=len(calls);command('next')
    wait(lambda s:s.get('FixtureTitle')==f'WinIsland 合成媒体 {track}','track identity')
    return start
def ready(provider):return wait(lambda s:s.get('LyricProvider')==provider and s.get('LyricsLoaded')=='True',provider)
def paths(start):return [x[0] for x in calls[start:]]
try:
    wait(lambda s:s.get('Version'),'startup');check(state()['SongSource']=='0','New metadata default is SMTC first with automatic title fallback')
    send('monitor-start');(out/'media').mkdir();fixture=subprocess.Popen([str(root/'tests/bin/WinIsland-MediaFixture.exe'),str(out/'media')]);send('media-fixture-only')
    ready('LRCLIB 精确');check(paths(0)==['/api/get'],'Exact synced result stops further queries')
    start=len(calls);send('lyric-source=2');send('lyric-source=0');ready('LRCLIB 精确');check(len(calls)==start and '缓存' in state()['LyricStatus'],'Validated cache is read before all network sources')
    start=next_track('netease');ready('网易云普通')
    check(paths(start)==['/api/get','/api/search/get','/api/song/lyric'],'Exact miss -> uniquely matched NetEase ID -> ordinary lyrics')
    check(calls[-1][1]['id']==['3000000001'],'Wrong first song is rejected and 64-bit ID retained')
    command('seek=4');wait(lambda s:s.get('FixtureLyric')=='Net second','seek');command('pause');wait(lambda s:s.get('MusicPlaying')=='False','pause');before=state()['FixtureLyric'];time.sleep(.4)
    check(state()['FixtureLyric']==before,'Actual SMTC pause keeps current lyric stable');command('play');wait(lambda s:s.get('MusicPlaying')=='True','resume')
    start=len(calls);send('lyric-source=2');send('lyric-source=0');ready('网易云普通');check(len(calls)==start,'NetEase normalized cache reloads without HTTP')
    start=next_track('v1');ready('网易云新版');command('seek=4');wait(lambda s:s.get('FixtureLyric')=='YRC second','YRC sync')
    check(paths(start)==['/api/get','/api/search/get','/api/song/lyric','/api/song/lyric/v1'],'Ordinary empty -> v1 YRC, all word markers removed')
    start=next_track('fallback');ready('LRCLIB 搜索');check(paths(start)==['/api/get','/api/search/get','/api/search'],'LRCLIB candidate search runs last and only once')
    for case in ('ambiguous','cover','none','invalid'):
        next_track(case);wait(lambda s:s.get('LyricsLoaded')=='False' and '暂无歌词' in s.get('LyricStatus',''),case)
        check(not state().get('FixtureLyric') and state()['InfoSource'].startswith('SMTC'),case+' cannot replace SMTC or retain old lyrics')
    next_track('plain');ready('LRCLIB 精确');wait(lambda s:s.get('FixtureLyric','').startswith('文本歌词'),'plain painted');check(state()['LyricFormat']=='Plain' and state()['LyricSyncAvailable']=='False' and state()['FixtureLyric'].startswith('文本歌词'),'Plain fallback is visibly unsynchronized')
    start=next_track('slow');send('notice-long');wait(lambda s:s.get('NoticeTitle') and s.get('Animating')=='False','message during request')
    next_track('netease');ready('网易云普通');time.sleep(.5);check(state()['LyricProvider']=='网易云普通','Slow old-track completion cannot overwrite next track or block notification')
    command('timeline-off');wait(lambda s:s.get('TimelineValid')=='False' and s.get('LyricsLoaded')=='True','no timeline')
    wait(lambda s:s.get('FixtureLyric','').startswith('静态歌词'),'unsynced painted');check(state()['LyricSyncAvailable']=='False' and state()['FixtureLyric'].startswith('静态歌词'),'No timeline displays matched static preview, never a synthetic playback clock')
    command('timeline-on');wait(lambda s:s.get('TimelineValid')=='True','timeline recovered')
    mode='netease';send('settings');s=wait(lambda s:int(s.get('SettingsContent','0'))>0,'settings');v=int(s['SettingsContent']);combo=u.GetDlgItem(v,121)
    u.SendMessageW(combo,0x14e,3,0);start=len(calls);u.SendMessageW(v,0x111,121|(1<<16),combo);wait(lambda s:s.get('LyricSource')=='3','source select');ready('网易云普通')
    start=next_track('netease');ready('网易云普通');check(paths(start)==['/api/search/get','/api/song/lyric'],'Specified NetEase uses no LRCLIB')
    send('lyric-source=1');start=next_track('netease');wait(lambda s:'暂无歌词' in s.get('LyricStatus',''),'LRCLIB only');check(paths(start)==['/api/get','/api/search'],'Specified LRCLIB never uses NetEase')
    mode='fallback';send('lyric-source=4');ready('LRCLIB 搜索');check(calls[-1][0]=='/api/search','Specified search-only source works')
    mode='netease';send('lyric-source=3');ready('网易云普通')
    (out/'fixture.lrc').write_text('[00:00.00]Imported first\n[00:03.00]Imported second','utf-8');send('lyrics-fixture');ready('本地导入')
    check(True,'Explicit imported LRC overrides all remote sources')
    u.SendMessageW(u.GetDlgItem(v,105),0xf5,0,0);wait(lambda s:'已清除' in s.get('LyricStatus',''),'clear import')
    check(not list((out/'lyrics').glob('*.lrc')),'Clear button deletes the current imported lyrics and cache')
    send('lyric-source=2');start=len(calls);time.sleep(.5);check(len(calls)==start,'Disabled lyrics do not query any provider')
    send('lyric-source=0');start=next_track('rate');ready('网易云普通');check('/api/search' not in paths(start),'LRCLIB 429 falls back to independent NetEase without repeated LRCLIB search')
    start=next_track('network');wait(lambda s:'暂无歌词' in s.get('LyricStatus',''),'network failure',20)
    check(paths(start).count('/api/get')==0 and paths(start).count('/api/search')==0 and paths(start).count('/api/search/get')==2,'Service backoff spans songs and retries remain bounded')
    mode='netease'
    if not a.skip_recovery:
        wait(lambda s:s.get('LyricProvider')=='网易云普通' and s.get('LyricsLoaded')=='True','same track recovers after bounded backoff',75)
        check(True,'Same song recovers after network failure without switching player or restarting')
    send('monitor-stop');wait(lambda s:s.get('MonitorPending')=='False','monitor stop');send('monitor-export');s=wait(lambda s:s.get('ExportPath'),'export')
    report=Path(s['ExportPath']).read_text('utf-8');check('NetEase standard' in report and '歌词完成' in report and 'timeline=' in report,'Export contains real request stages, source/format and timeline validity')
    send('lyric-source=3');send('close-settings');send('settings');s=wait(lambda s:int(s.get('SettingsContent','0'))>0,'settings reopened');v=int(s['SettingsContent'])
    check(u.SendMessageW(u.GetDlgItem(v,121),0x147,0,0)==3 and '<LyricSource>3</LyricSource>' in (out/'settings.xml').read_text('utf-8'),'Specified source survives save and settings reopen')
    command('exit');fixture.wait(10);wait(lambda s:s.get('HasMusic')=='False' and s.get('Animating')=='False','player exit');check(not state().get('FixtureLyric'),'Player exit removes lyrics')
finally:
    (out/'exit.request').write_text('exit');app.wait(15)
    if fixture and fixture.poll() is None:
        (out/'media/media-command.txt').write_text('exit');fixture.wait(10)
    server.shutdown();(out/'results.json').write_text(json.dumps({'passed':passed,'calls':calls,'exit':app.returncode},ensure_ascii=False,indent=2),'utf-8')
