"""Observe the user's current player; do not start, pause, seek, or change its song."""
import argparse,json,subprocess,time
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--exe',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
out=a.output.resolve();out.mkdir(exist_ok=False);app=subprocess.Popen([str(a.exe.resolve()),'--observe',str(out)])
def state():
    try:return dict(x.split('=',1) for x in (out/'state.txt').read_text('utf-8').splitlines() if '=' in x)
    except (ValueError,OSError):return {}
def send(c):
    path=out/'command.txt';path.write_text(c,'utf-8');end=time.monotonic()+10
    while path.exists() and time.monotonic()<end:time.sleep(.03)
try:
    end=time.monotonic()+12
    while not state().get('Version') and time.monotonic()<end:time.sleep(.05)
    send('monitor-start');send('music-live');end=time.monotonic()+35
    while time.monotonic()<end:
        s=state()
        if s.get('HasMusic')=='True' and s.get('LyricStatus') and '正在获取' not in s['LyricStatus']:break
        time.sleep(.1)
    send('music-expand');time.sleep(.7);send('capture');s=state()
    keys=['Version','HasMusic','MusicPlaying','InfoSource','LyricStatus','LyricsLoaded','LyricProvider','LyricFormat','TimelineValid','LyricSyncAvailable','HasCover','MusicControls','Renderer']
    result={k:s.get(k) for k in keys};result['display_contains_unsynced_label']=s.get('FixtureLyric','').startswith(('未同步','文本歌词'))
    send('monitor-stop');time.sleep(.3);send('monitor-export');time.sleep(.3);result['report']=state().get('ExportPath')
    (out/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),'utf-8');print(json.dumps(result,ensure_ascii=False,indent=2))
finally:
    (out/'exit.request').write_text('exit');app.wait(15)
