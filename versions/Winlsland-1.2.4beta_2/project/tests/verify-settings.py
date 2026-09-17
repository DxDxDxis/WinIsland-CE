"""Native wheel ownership, scroll geometry/font stability and repeat-paint screenshots."""
import argparse, ctypes as C, subprocess,time,json
from ctypes import wintypes as W
from pathlib import Path
from PIL import ImageGrab,ImageChops
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--full',action='store_true');p.add_argument('--exe',type=Path);a=p.parse_args()
root=Path(__file__).resolve().parents[1];out=a.output.resolve();out.mkdir(exist_ok=False)
u=C.windll.user32;u.SetProcessDpiAwarenessContext(C.c_void_p(-4));u.SendMessageW.argtypes=[W.HWND,W.UINT,W.WPARAM,W.LPARAM];u.SendMessageW.restype=C.c_ssize_t
u.GetDlgItem.argtypes=[W.HWND,C.c_int];u.GetDlgItem.restype=W.HWND;u.SetWindowPos.argtypes=[W.HWND,W.HWND,C.c_int,C.c_int,C.c_int,C.c_int,W.UINT];u.GetWindowRect.argtypes=[W.HWND,C.POINTER(W.RECT)]
class SI(C.Structure):_fields_=[('cbSize',W.UINT),('fMask',W.UINT),('nMin',C.c_int),('nMax',C.c_int),('nPage',W.UINT),('nPos',C.c_int),('nTrackPos',C.c_int)]
u.GetScrollInfo.argtypes=[W.HWND,C.c_int,C.POINTER(SI)];result=[]
app=subprocess.Popen([str(a.exe or root/'release/最终成品/WinIsland-1.2.4beta.exe'),'--verify',str(out)])
def state():
    try:return dict(x.split('=',1) for x in (out/'state.txt').read_text('utf-8').splitlines() if '=' in x)
    except (OSError,ValueError):return {}
def wait(fn):
    until=time.monotonic()+10
    while time.monotonic()<until:
        if fn(state()):return state()
        time.sleep(.02)
    raise AssertionError(state())
def send(c):
    path=out/'command.txt';path.write_text(c,'utf-8');wait(lambda s:not path.exists());time.sleep(.15)
def scroll():
    si=SI();si.cbSize=C.sizeof(si);si.fMask=0x17;u.GetScrollInfo(h,1,C.byref(si));return si.nPos
def check(ok,label):
    if not ok:raise AssertionError(label+f'; scroll={scroll()}')
    result.append(label);print('PASS:',label,flush=True)
def wheel(control,delta=-120):u.SendMessageW(control,0x20A,(delta&65535)<<16,0)
def capture(label):
    u.SetWindowPos(h,W.HWND(-1),0,0,0,0,0x13);time.sleep(.1);r=W.RECT();u.GetWindowRect(h,C.byref(r));im=ImageGrab.grab(bbox=(r.left,r.top,r.right,r.bottom));im.save(out/(label+'.png'));return im
try:
    wait(lambda s:s.get('Version'));send('settings');h=int(wait(lambda s:int(s.get('SettingsWindow','0'))>0)['SettingsWindow'])
    u.SendMessageW(h,0x115,6,0)
    for _ in range(8):wheel(h)
    combo=u.GetDlgItem(h,121);before=scroll();selected=u.SendMessageW(combo,0x147,0,0)
    wheel(combo);time.sleep(.1)
    check(scroll()>before and u.SendMessageW(combo,0x147,0,0)==selected,'Closed lyric dropdown sends wheel to page without changing selection')
    if a.full:
        for dpi in (1,1.25,1.5):
            send('dpi='+str(dpi));font=u.SendMessageW(u.GetDlgItem(h,102),0x31,0,0)
            for id in (107,102,104,120,121,124,232):
                u.SendMessageW(h,0x115,6,0);wheel(h);before=scroll();wheel(u.GetDlgItem(h,id),-30);time.sleep(.02)
                check(scroll()>before,f'{dpi}: wheel over control {id} scrolls main page, including touchpad delta')
            u.SendMessageW(h,0x115,7,0);bottom=scroll();first=capture(f'bottom-{dpi}-before')
            for _ in range(25):u.SendMessageW(h,0x115,6,0);u.SendMessageW(h,0x115,7,0)
            after=capture(f'bottom-{dpi}-after');check(scroll()==bottom,f'{dpi}: repeated top/bottom scroll returns exact geometry')
            check(u.SendMessageW(u.GetDlgItem(h,102),0x31,0,0)==font,f'{dpi}: scrolling reuses the font handle')
            # Exclude the OS scrollbar's timed hover animation; assert the actual
            # settings content raster and independently checked scroll geometry.
            right=first.width-round(30*dpi)
            diff=ImageChops.difference(first.crop((10,50,right,first.height-10)),after.crop((10,50,right,after.height-10)))
            check(diff.getbbox() is None,f'{dpi}: repeated scrolling leaves no stale text pixels')
            u.SendMessageW(combo,0x14F,1,0);before=scroll();wheel(combo);time.sleep(.1)
            check(scroll()==before,f'{dpi}: expanded dropdown owns option wheel')
            u.SendMessageW(combo,0x14F,0,0)
        send('dpi=1');u.SetWindowPos(h,None,0,0,460,520,0x16);time.sleep(.3);capture('resized')
        class GI(C.Structure):
            _fields_=[('cbSize',W.DWORD),('flags',W.DWORD),('active',W.HWND),('focus',W.HWND),('capture',W.HWND),('menu',W.HWND),('move',W.HWND),('caret',W.HWND),('rect',W.RECT)]
        u.GetGUIThreadInfo.argtypes=[W.DWORD,C.POINTER(GI)];u.GetWindowThreadProcessId.argtypes=[W.HWND,C.POINTER(W.DWORD)];u.GetDlgCtrlID.argtypes=[W.HWND];u.PostMessageW.argtypes=[W.HWND,W.UINT,W.WPARAM,W.LPARAM]
        thread=u.GetWindowThreadProcessId(h,None);seen=[]
        for _ in range(20):
            info=GI();info.cbSize=C.sizeof(info);u.GetGUIThreadInfo(thread,C.byref(info));u.PostMessageW(info.focus or h,0x100,9,0);time.sleep(.04)
            u.GetGUIThreadInfo(thread,C.byref(info));seen.append(u.GetDlgCtrlID(info.focus))
        check(len(set(seen))>=10,'Real queued Tab keys move focus among native settings controls after resize')
finally:
    (out/'exit.request').write_text('exit');app.wait(15);(out/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),'utf-8')
