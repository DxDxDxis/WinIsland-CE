"""Native render-target / settings layout checks; does not change Windows display settings."""
import ctypes as C
from ctypes import wintypes as W
from pathlib import Path
import subprocess,time,sys,json
from PIL import Image,ImageGrab
exe=Path(sys.argv[1]);folder=Path(sys.argv[2]);folder.mkdir(parents=True,exist_ok=False)
p=subprocess.Popen([str(exe),'--verify',str(folder)])
u=C.windll.user32;u.GetDlgItem.restype=W.HWND;u.GetDlgItem.argtypes=[W.HWND,C.c_int]
u.SendMessageW.argtypes=[W.HWND,W.UINT,W.WPARAM,W.LPARAM]
def state():
    try:return dict(l.split('=',1) for l in (folder/'state.txt').read_text('utf-8').splitlines() if '=' in l)
    except:return {}
def wait(fn):
    end=time.monotonic()+10
    while time.monotonic()<end:
        s=state()
        if fn(s):return s
        if p.poll()!=None:raise AssertionError('native app exited')
        time.sleep(.03)
    raise AssertionError('condition timed out')
def command(c):
    file=folder/'command.txt';file.write_text(c,'utf-8');wait(lambda s:not file.exists());time.sleep(.15)
def snap_window(h,file):
    r=W.RECT();u.GetWindowRect(W.HWND(h),C.byref(r));ImageGrab.grab(bbox=(r.left,r.top,r.right,r.bottom)).save(file)
results=[]
try:
    wait(lambda s:s.get('Version')=='1.2.3beta');command('music=play');command('music-expand');command('seconds=3')
    for scale in (1,1.25,1.5,2):
        command('close-settings');command('dpi='+str(scale));command('settings')
        s=wait(lambda s:s.get('Animating')=='False' and float(s.get('Dpi','0'))==scale)
        settings=int(s['SettingsWindow']);snap_window(settings,folder/f'settings-{scale}.png')
        # Scrolling must bring the diagnostic controls into the visible client area.
        u.SendMessageW(settings,0x0115,7,0) # SB_BOTTOM
        time.sleep(.2);u.SendMessageW(settings,0x0115,3,0) # SB_PAGEDOWN
        time.sleep(.2);snap_window(settings,folder/f'settings-bottom-{scale}.png')
        button=u.GetDlgItem(settings,109);r=W.RECT();u.GetWindowRect(button,C.byref(r));client=W.RECT();u.GetClientRect(W.HWND(settings),C.byref(client));point=W.POINT(r.left,r.top);u.ScreenToClient(W.HWND(settings),C.byref(point))
        assert point.y>=0 and point.y+(r.bottom-r.top)<=client.bottom+2,'Export button must be reachable by scrolling'
        command('close-settings');command('notice-wide');wait(lambda s:s.get('Holding')=='True');command('capture')
        image=Image.open(folder/'capture.png').convert('RGBA');box=image.getchannel('A').getbbox();assert box and box[2]<=image.width and box[3]<=image.height
        bg=Image.new('RGBA',image.size,(235,236,240,255));bg.alpha_composite(image);bg.convert('RGB').save(folder/f'island-{scale}.png')
        s=state();assert abs(float(s['NoticeTop'])-float(s['MusicHeight']))<.1
        results.append({'scale':scale,'image_px':image.size,'opaque_bounds':box,'export_button_visible':True})
        wait(lambda s:s.get('NoticeTitle')=='' and s.get('Animating')=='False')
    print(json.dumps(results,indent=2),flush=True)
finally:
    (folder/'exit.request').write_text('exit');p.wait(12);(folder/'results.json').write_text(json.dumps(results,indent=2),'utf-8')
