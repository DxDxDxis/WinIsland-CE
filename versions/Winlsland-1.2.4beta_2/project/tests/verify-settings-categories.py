"""Native settings raster and interaction regression against the built EXE."""
import argparse, ctypes as C, subprocess, time, json
from ctypes import wintypes as W
from pathlib import Path
from PIL import ImageGrab, ImageChops
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--exe',type=Path,required=True);a=p.parse_args()
out=a.output.resolve();out.mkdir(exist_ok=False);u=C.windll.user32;k=C.windll.kernel32
u.SetProcessDpiAwarenessContext(C.c_void_p(-4))
for name,args,ret in [('SendMessageW',[W.HWND,W.UINT,W.WPARAM,W.LPARAM],C.c_ssize_t),('GetDlgItem',[W.HWND,C.c_int],W.HWND),('SetWindowPos',[W.HWND,W.HWND,C.c_int,C.c_int,C.c_int,C.c_int,W.UINT],W.BOOL),('GetWindowRect',[W.HWND,C.POINTER(W.RECT)],W.BOOL),('PostMessageW',[W.HWND,W.UINT,W.WPARAM,W.LPARAM],W.BOOL),('GetDlgCtrlID',[W.HWND],C.c_int),('IsWindowVisible',[W.HWND],W.BOOL),('SetWindowTextW',[W.HWND,W.LPCWSTR],W.BOOL),('RedrawWindow',[W.HWND,C.c_void_p,C.c_void_p,W.UINT],W.BOOL),('GetWindowThreadProcessId',[W.HWND,C.POINTER(W.DWORD)],W.DWORD),('GetGuiResources',[W.HANDLE,W.DWORD],W.DWORD)]:
    f=getattr(u,name);f.argtypes=args;f.restype=ret
k.OpenProcess.argtypes=[W.DWORD,W.BOOL,W.DWORD];k.OpenProcess.restype=W.HANDLE;k.CloseHandle.argtypes=[W.HANDLE]
class SI(C.Structure):_fields_=[('cbSize',W.UINT),('fMask',W.UINT),('nMin',C.c_int),('nMax',C.c_int),('nPage',W.UINT),('nPos',C.c_int),('nTrackPos',C.c_int)]
class GI(C.Structure):_fields_=[('cbSize',W.DWORD),('flags',W.DWORD),('active',W.HWND),('focus',W.HWND),('capture',W.HWND),('menu',W.HWND),('move',W.HWND),('caret',W.HWND),('rect',W.RECT)]
u.GetScrollInfo.argtypes=[W.HWND,C.c_int,C.POINTER(SI)];u.GetGUIThreadInfo.argtypes=[W.DWORD,C.POINTER(GI)]
result=[];app=subprocess.Popen([str(a.exe.resolve()),'--verify',str(out)]);h=v=0
def state():
    try:return dict(x.split('=',1) for x in (out/'state.txt').read_text('utf-8').splitlines() if '=' in x)
    except (OSError,ValueError):return {}
def wait(fn):
    until=time.monotonic()+15
    while time.monotonic()<until:
        if fn(state()):return state()
        time.sleep(.025)
    raise AssertionError(state())
def send(c):
    path=out/'command.txt';path.write_text(c,'utf-8');wait(lambda s:not path.exists());time.sleep(.15)
def scroll():
    si=SI();si.cbSize=C.sizeof(si);si.fMask=0x17;u.GetScrollInfo(v,1,C.byref(si));return si.nPos
def check(ok,label):
    if not ok:raise AssertionError(label+f'; scroll={scroll()}')
    result.append(label);print('PASS:',label,flush=True)
def set_text(h,text):
    value=C.create_unicode_buffer(text);u.SendMessageW(h,0x0c,0,C.cast(value,C.c_void_p).value)
def page(n):u.SendMessageW(h,0x111,250+n,u.GetDlgItem(h,250+n));time.sleep(.04)
def wheel(control,delta=-120):u.SendMessageW(control,0x20A,(delta&65535)<<16,0)
def capture(label=None,window=None):
    time.sleep(.09);r=W.RECT();u.GetWindowRect(window or v,C.byref(r));im=ImageGrab.grab(bbox=(r.left,r.top,r.right,r.bottom))
    if label:im.save(out/(label+'.png'))
    return im
def same(first,after,dpi):
    box=(4,0,first.width-round(25*dpi),first.height-4)
    return ImageChops.difference(first.crop(box),after.crop(box)).getbbox() is None
try:
    wait(lambda s:s.get('Version'));send('music-stop');send('settings');s=wait(lambda s:int(s.get('SettingsContent','0'))>0)
    h=int(s['SettingsWindow']);v=int(s['SettingsContent']);u.SetWindowPos(h,W.HWND(-1),60,60,608,700,0x10);u.SetCursorPos(5,1000)
    pages={0:[100,101,102,103,106],1:[120,121,125,104,105,107],2:[122,123,124,238,239],3:[108,109,110,111,246]}
    for n,ids in pages.items():
        page(n);check(all(u.IsWindowVisible(u.GetDlgItem(v,i)) for i in ids),f'Category {n}: original controls visible')
        check(all(not u.IsWindowVisible(u.GetDlgItem(v,i)) for other,group in pages.items() if other!=n for i in group),f'Category {n}: other categories hidden')
        capture(f'category-{n}',h)
        if n==1:
            baseline=capture();time.sleep(1);check(same(baseline,capture(),1),'Lyrics page stays still without interaction')
    for dpi in (1,1.25,1.5):
        send('dpi='+str(dpi));u.SetWindowPos(h,None,40,40,round(608*dpi),round(640*dpi),0x14);page(1)
        font=u.SendMessageW(u.GetDlgItem(v,125),0x31,0,0)
        for id in (107,104,120,121,125,232):
            u.SendMessageW(v,0x115,6,0);before=scroll();combo=u.GetDlgItem(v,id);selected=u.SendMessageW(combo,0x147,0,0)
            wheel(combo,-30);time.sleep(.04);check(scroll()>before,f'{dpi}: wheel over {id} scrolls page')
            if id in (120,121):check(selected==u.SendMessageW(combo,0x147,0,0),f'{dpi}: closed dropdown selection unchanged')
        u.SendMessageW(v,0x115,7,0);bottom=scroll();first=capture(f'bottom-{dpi}-before')
        for _ in range(30):u.SendMessageW(v,0x115,6,0);u.SendMessageW(v,0x115,7,0)
        after=capture(f'bottom-{dpi}-after');check(scroll()==bottom and same(first,after,dpi),f'{dpi}: 30 scroll cycles have identical pixels')
        check(u.SendMessageW(u.GetDlgItem(v,125),0x31,0,0)==font,f'{dpi}: font handles reused')
        u.RedrawWindow(v,None,None,0x185);check(same(after,capture(),dpi),f'{dpi}: normal result equals full repaint')
        u.SendMessageW(v,0x115,6,0);combo=u.GetDlgItem(v,121);u.SendMessageW(combo,0x14f,1,0);before=scroll();wheel(combo);time.sleep(.1)
        check(scroll()==before,f'{dpi}: open dropdown owns wheel');u.SendMessageW(combo,0x14f,0,0);wheel(combo);check(scroll()>before,f'{dpi}: closed dropdown releases wheel')
        page(3);label=u.GetDlgItem(v,246)
        for text in ('文本已更新', ''):
            set_text(label,'旧内容应该完全消失。'*60);time.sleep(.06);set_text(label,text);first=capture(f'text-{dpi}-{bool(text)}-before',window=label)
            u.RedrawWindow(label,None,None,0x185);check(ImageChops.difference(first,capture(f'text-{dpi}-{bool(text)}-after',window=label)).getbbox() is None,f'{dpi}: shorter/empty text clears old pixels ({bool(text)})')
    send('dpi=1');u.SetWindowPos(h,None,80,80,460,460,0x14);page(1);capture('small-window',h)
    thread=u.GetWindowThreadProcessId(h,None);seen=[]
    for _ in range(18):
        info=GI();info.cbSize=C.sizeof(info);u.GetGUIThreadInfo(thread,C.byref(info));u.PostMessageW(info.focus or h,0x100,9,0);time.sleep(.04)
        u.GetGUIThreadInfo(thread,C.byref(info));seen.append(u.GetDlgCtrlID(info.focus))
    check(len(set(seen))>=7,'Queued Tab keys navigate visible controls')
    page(2);target=u.GetDlgItem(v,124);set_text(target,'127.0.0.1');u.SendMessageW(v,0x111,124|(0x200<<16),target)
    toggle=u.GetDlgItem(v,123);u.SendMessageW(toggle,0xf1,1,0);u.SendMessageW(v,0x111,123,toggle)
    wait(lambda s:s.get('PingTarget')=='127.0.0.1' and int(s.get('Ping','-1'))>=0);check(True,'Input and toggle save and start real ICMP')
    page(3);u.SendMessageW(u.GetDlgItem(v,109),0xf5,0,0);wait(lambda s:'没有可导出' in s.get('MonitorMessage',''));check(True,'Empty export gives clear feedback')
    u.SendMessageW(u.GetDlgItem(v,108),0xf5,0,0);wait(lambda s:s.get('MonitorActive')=='True' and s.get('MonitorPending')=='False')
    send('music=play');send('music-expand');send('notice-long');time.sleep(.8);check(state().get('MusicPlaying')=='True','Music and notification continue with settings and monitoring')
    u.SendMessageW(u.GetDlgItem(v,109),0xf5,0,0);s=wait(lambda s:s.get('ExportPath') and s.get('MonitorPending')=='False')
    check(bool(Path(s['ExportPath']).read_text('utf-8')) and s['MonitorActive']=='True','Export UTF-8 without stopping monitoring')
    send('monitor-stop');wait(lambda s:s.get('MonitorPending')=='False');(out/'blocked-export').write_text('occupied by file');send('monitor-export-blocked');s=wait(lambda s:'文档文件夹' in s.get('MonitorMessage','') and s.get('MonitorPending')=='False')
    check(Path(s['ExportPath']).exists(),'Unwritable export falls back to Documents')
    send('monitor-start');wait(lambda s:s.get('MonitorActive')=='True');send('monitor-stop');wait(lambda s:s.get('MonitorPending')=='False');check(True,'Repeated monitor start/stop')
    proc=k.OpenProcess(0x1000,False,app.pid);gdi=u.GetGuiResources(proc,0);usr=u.GetGuiResources(proc,1)
    for _ in range(50):
        for n in range(4):page(n);u.SendMessageW(v,0x115,7,0);u.SendMessageW(v,0x115,6,0)
    time.sleep(.2);gdi2=u.GetGuiResources(proc,0);usr2=u.GetGuiResources(proc,1);k.CloseHandle(proc)
    check(gdi2<=gdi+2 and usr2<=usr+2,f'200 category switches: GDI {gdi}->{gdi2}, USER {usr}->{usr2}')
    send('close-settings');wait(lambda s:s.get('SettingsWindow')=='0');send('settings');s=wait(lambda s:int(s.get('SettingsContent','0'))>0)
    h=int(s['SettingsWindow']);v=int(s['SettingsContent']);page(2)
    text=C.create_unicode_buffer(64);u.GetWindowTextW.argtypes=[W.HWND,W.LPWSTR,C.c_int];u.GetWindowTextW(u.GetDlgItem(v,124),text,64)
    check(text.value=='127.0.0.1' and u.SendMessageW(u.GetDlgItem(v,123),0xf0,0,0)==1,'Reopening restores saved input and switch');capture('reopened',h)
finally:
    (out/'exit.request').write_text('exit');app.wait(15);(out/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),'utf-8')
