"""Real mouse routing to a different-process underlay, not WM_NCHITTEST alone."""
import argparse, ctypes as C, json, os, subprocess, time
from ctypes import wintypes as W
from pathlib import Path

p=argparse.ArgumentParser()
p.add_argument('--exe',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--software',action='store_true')
p.add_argument('--probe-styles',action='store_true')
p.add_argument('--full',action='store_true')
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
u=C.windll.user32; g=C.windll.gdi32; k=C.windll.kernel32
u.SetProcessDpiAwarenessContext(C.c_void_p(-4))
PROC=C.WINFUNCTYPE(C.c_ssize_t,W.HWND,W.UINT,W.WPARAM,W.LPARAM)
class WC(C.Structure):
    _fields_=[('style',W.UINT),('proc',PROC),('cbCls',C.c_int),('cbWnd',C.c_int),('instance',W.HINSTANCE),('icon',W.HICON),('cursor',W.HANDLE),('brush',W.HBRUSH),('menu',W.LPCWSTR),('name',W.LPCWSTR)]
u.CreateWindowExW.restype=W.HWND
u.CreateWindowExW.argtypes=[W.DWORD,W.LPCWSTR,W.LPCWSTR,W.DWORD,C.c_int,C.c_int,C.c_int,C.c_int,W.HWND,W.HMENU,W.HINSTANCE,C.c_void_p]
u.DefWindowProcW.restype=C.c_ssize_t;u.DefWindowProcW.argtypes=[W.HWND,W.UINT,W.WPARAM,W.LPARAM]
u.SendMessageW.restype=C.c_ssize_t;u.SendMessageW.argtypes=[W.HWND,W.UINT,W.WPARAM,W.LPARAM]
u.PostMessageW.argtypes=[W.HWND,W.UINT,W.WPARAM,W.LPARAM]
u.WindowFromPoint.restype=W.HWND
u.WindowFromPoint.argtypes=[W.POINT]
u.DispatchMessageW.restype=C.c_ssize_t;u.DispatchMessageW.argtypes=[C.POINTER(W.MSG)]
u.PeekMessageW.argtypes=[C.POINTER(W.MSG),W.HWND,W.UINT,W.UINT,W.UINT]
u.GetWindowRect.argtypes=[W.HWND,C.POINTER(W.RECT)]
u.SetWindowPos.argtypes=[W.HWND,W.HWND,C.c_int,C.c_int,C.c_int,C.c_int,W.UINT]
u.GetWindowLongPtrW.restype=C.c_ssize_t;u.GetWindowLongPtrW.argtypes=[W.HWND,C.c_int]
u.SetWindowLongPtrW.restype=C.c_ssize_t;u.SetWindowLongPtrW.argtypes=[W.HWND,C.c_int,C.c_ssize_t]
u.SetLayeredWindowAttributes.argtypes=[W.HWND,W.DWORD,C.c_ubyte,W.DWORD]
u.SetCapture.argtypes=[W.HWND];u.DestroyWindow.argtypes=[W.HWND]
clicks=[];drag=None;pointer_events=[]
@PROC
def proc(h,m,w,l):
    global drag
    if m in (0x21,0x201,0x202,0x215):pointer_events.append((m,int(w),int(l)))
    if m==0x21:return 1 # MA_ACTIVATE: activation must not consume the tested click.
    if m==0x215:drag=None;return 0
    if m==0x201:
        pos=W.POINT();r=W.RECT();u.GetCursorPos(C.byref(pos));u.GetWindowRect(h,C.byref(r))
        clicks.append((pos.x,pos.y));drag=(pos.x,pos.y,r.left,r.top);u.SetCapture(h);return 0
    if m==0x200 and drag:
        pos=W.POINT();u.GetCursorPos(C.byref(pos))
        u.SetWindowPos(h,None,drag[2]+pos.x-drag[0],drag[3]+pos.y-drag[1],0,0,0x15);return 0
    if m==0x202:drag=None;u.ReleaseCapture();return 0
    return u.DefWindowProcW(h,m,w,l)
wc=WC();wc.proc=proc;wc.brush=6;wc.name='WinIsland.HitTest.Underlay';u.RegisterClassW(C.byref(wc))
under=u.CreateWindowExW(0x80,wc.name,'WinIsland mouse routing test',0x90000000,80,70,1750,920,None,None,None,None)
u.SetWindowPos(under,W.HWND(-1),0,0,0,0,0x13)
def pump(seconds=.05):
    until=time.monotonic()+seconds;msg=W.MSG()
    while time.monotonic()<until:
        while u.PeekMessageW(C.byref(msg),None,0,0,1):u.TranslateMessage(C.byref(msg));u.DispatchMessageW(C.byref(msg))
        time.sleep(.003)
def state():
    try:return dict(l.split('=',1) for l in (a.output/'state.txt').read_text('utf-8').splitlines() if '=' in l)
    except (OSError,ValueError):return {}
def wait(fn,seconds=8):
    until=time.monotonic()+seconds
    while time.monotonic()<until:
        s=state()
        if fn(s):return s
        pump(.02)
    raise AssertionError('state timeout')
def send(command):
    q=a.output/'command.txt';q.write_text(command,'utf-8');wait(lambda s:not q.exists());pump(.15)
def pointer(x,y,move=False):
    s=state()
    for key in ('RenderWindow','MusicWindow'):
        if s.get(key):u.SetWindowPos(int(s[key]),W.HWND(-1),0,0,0,0,0x13)
    before=len(clicks);events_before=len(pointer_events);u.SetCursorPos(round(x),round(y));pump(.1)
    u.mouse_event(2,0,0,0,0);pump(.1)
    if move:u.SetCursorPos(round(x+25),round(y+18));pump(.1)
    u.mouse_event(4,0,0,0,0);pump(.1)
    if len(clicks)==before:
        pt=W.POINT();u.GetCursorPos(C.byref(pt))
        print('Missing underlay click:',{'requested':[x,y],'actual':[pt.x,pt.y],'target':u.WindowFromPoint(pt),'events':pointer_events[events_before:]},flush=True)
    return len(clicks)-before
def check_surroundings(label):
    from PIL import Image
    s=wait(lambda s:s.get('Animating')=='False');send('capture');s=state()
    image=Image.open(a.output/'capture.png').convert('RGBA')
    image.save(a.output/(label+'.png'))
    h=int(s['MusicWindow']);r=W.RECT();u.GetWindowRect(h,C.byref(r));dpi=float(s['Dpi'])
    region=g.CreateRectRgn(0,0,0,0)
    u.GetWindowRgn.argtypes=[W.HWND,W.HRGN];g.PtInRegion.argtypes=[W.HRGN,C.c_int,C.c_int]
    u.GetWindowRgn(h,region)
    transparent=0;active=0
    for yy in range(0,image.height,4):
        for xx in range(0,image.width,4):
            hit=bool(g.PtInRegion(region,xx,yy));alpha=image.getpixel((xx,yy))[3]
            assert not hit or alpha>0,f'{label}: hit region outside rendered alpha at {xx},{yy}'
            active+=hit;transparent+=not alpha
    g.DeleteObject(region)
    points=[(r.left+6,r.top+min(180*dpi,250)),(r.right-6,r.top+min(180*dpi,250)),
            ((r.left+r.right)//2,r.top+float(s['Height'])*dpi+12)]
    # Non-interactive visible idle/message text must also pass through.
    if s['HasMusic']=='False':points.append(((r.left+r.right)//2,r.top+12*dpi))
    if s.get('NoticeTitle'):points.append(((r.left+r.right)//2,r.top+(float(s['MusicHeight'])+25)*dpi))
    for x,y in points:
        assert pointer(x,y)==1,f'{label}: background click blocked at {x},{y}; target={u.WindowFromPoint(W.POINT(round(x),round(y)))} expected={under} input={h}; state={state()}'
    x,y=points[0];before=W.RECT();after=W.RECT();u.GetWindowRect(under,C.byref(before))
    assert pointer(x,y,True)==1,f'{label}: drag start blocked'
    u.GetWindowRect(under,C.byref(after));assert after.left-before.left==25 and after.top-before.top==18,f'{label}: drag interrupted'
    u.SetWindowPos(under,None,80,70,0,0,0x15)
    results.append({'scenario':label,'transparent_pixels_sampled':transparent,'interactive_pixels_sampled':active,'background_click_and_drag':True})
    print('PASS:',label,'cross-process clicks/drags and alpha bounds',flush=True)
env=os.environ.copy()
if a.software:env['WINISLAND_SOFTWARE']='1'
app=subprocess.Popen([str(a.exe),'--verify',str(a.output)],env=env)
old=W.POINT();u.GetCursorPos(C.byref(old));results=[]
try:
    s=wait(lambda s:s.get('Animating')=='False' and s.get('MusicWindow'))
    h=int(s['MusicWindow']);r=W.RECT();u.GetWindowRect(h,C.byref(r));x=(r.left+r.right)//2;y=r.top+180
    variants=[('original',u.GetWindowLongPtrW(h,-20))]
    if a.probe_styles:variants += [('transparent',variants[0][1]|0x20),('layered-transparent',variants[0][1]|0x80020)]
    for name,style in variants:
        u.SetWindowLongPtrW(h,-20,style)
        if name=='layered-transparent':u.SetLayeredWindowAttributes(h,0,255,2)
        pump(.1)
        if a.probe_styles:
            from PIL import ImageGrab
            ImageGrab.grab(bbox=(r.left,r.top,r.right,r.bottom)).save(a.output/(name+'.png'))
        hit=u.WindowFromPoint(W.POINT(x,y));nchit=u.SendMessageW(h,0x84,0,(y<<16)|(x&65535))
        got=pointer(x,y)
        row={'mode':name,'host_px':[r.right-r.left,r.bottom-r.top],'visible_dip':[s['Width'],s['Height']],'window_at_point':hit,'island_hwnd':h,'underlay_hwnd':under,'nchittest':nchit,'underlay_clicks':got}
        results.append(row);print(json.dumps(row),flush=True)
    if not a.probe_styles:assert results[0]['underlay_clicks']==1,'Transparent island area blocked input to another process'
    if a.full:
        for dpi in (1,1.25,1.5,2):
            send('music-stop');wait(lambda s:s.get('Animating')=='False');send('dpi='+str(dpi))
            check_surroundings(f'idle-{dpi}')
            send('music=play');wait(lambda s:s.get('Animating')=='False')
            check_surroundings(f'music-{dpi}')
            send('music-expand');wait(lambda s:s.get('Animating')=='False')
            check_surroundings(f'expanded-{dpi}')
            send('music-expand')
            send('seconds=3');send('notice-wide');wait(lambda s:s.get('NoticeTitle') and s.get('Animating')=='False')
            check_surroundings(f'message-{dpi}')
            wait(lambda s:not s.get('NoticeTitle') and s.get('Animating')=='False')
        send('dpi=1');send('music=play');send('perf-lyrics');wait(lambda s:s.get('HasLyric')=='True',12)
        check_surroundings('lyrics')
        for i in range(8):
            q=a.output/'command.txt';q.write_text('music-expand' if i%2==0 else 'music-collapse','utf-8')
            wait(lambda s:not q.exists())
            s=state();r=W.RECT();u.GetWindowRect(int(s['MusicWindow']),C.byref(r))
            assert pointer(r.left+4,r.top+180)==1,'Animation temporarily blocked transparent area'
        wait(lambda s:s.get('Animating')=='False')
        print('PASS: interrupted animations keep surrounding input transparent',flush=True)
        results.append({'scenario':'animation-interruption','cross_process_clicks':8})
finally:
    (a.output/'result.json').write_text(json.dumps(results,indent=2),'utf-8')
    (a.output/'exit.request').write_text('exit');app.wait(12)
    u.SetCursorPos(old.x,old.y);u.DestroyWindow(under)
