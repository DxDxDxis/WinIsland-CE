"""Published binary + synthetic SMTC producer. No system input injection.

Checks scaled control hit regions and telemetry layout. Optional Tk underlay
records real pointer events for separate, manual/Computer Use verification.
"""
import argparse, ctypes as C, json, subprocess, time
from ctypes import wintypes as W
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('--interactive', action='store_true')
p.add_argument('--out', default='capsule-controls')
a = p.parse_args()
root = Path(__file__).resolve().parents[1]
out = root / 'tests' / a.out
out.mkdir(exist_ok=False)
media = out / 'media'
media.mkdir()
app = subprocess.Popen([str(root/'release/1.2.4beta_2/WinIsland-1.2.4beta_2.exe'), '--verify', str(out)])
producer = subprocess.Popen([str(root/'tests/bin/WinIsland-MediaFixture.exe'), str(media)])
u = C.windll.user32
g = C.windll.gdi32
g.CreateRectRgn.restype = W.HRGN
u.GetWindowRgn.argtypes = [W.HWND, W.HRGN]
g.PtInRegion.argtypes = [W.HRGN, C.c_int, C.c_int]
g.DeleteObject.argtypes = [W.HANDLE]

def state():
    try:
        return dict(l.split('=', 1) for l in (out/'state.txt').read_text('utf-8').splitlines() if '=' in l)
    except (OSError, ValueError):
        return {}

def wait(check):
    end = time.monotonic()+15
    while time.monotonic()<end:
        s = state()
        if check(s): return s
        assert app.poll() is None and producer.poll() is None, 'process exited'
        time.sleep(.03)
    raise AssertionError(state())

def send(command):
    count = int(state().get('CommandsProcessed', '0'))
    (out/'command.txt').write_text(command, 'utf-8')
    wait(lambda s: int(s.get('CommandsProcessed', '0')) > count)
    wait(lambda s: s.get('Animating') == 'False')

rows = []
try:
    wait(lambda s: 'Version' in s)
    send('lyric-source=2')
    send('media-fixture-only')
    wait(lambda s: s.get('MusicControls') == '3')
    send('show-fps=on')
    send('show-ping=on')
    for dpi in (1, 1.25, 1.5, 2):
        send('dpi='+str(dpi))
        send('music-expand')
        s = wait(lambda s: all('Button'+str(i) in s for i in (1, 2, 3)))
        region = g.CreateRectRgn(0, 0, 0, 0)
        try:
            u.GetWindowRgn(int(s['MusicWindow']), region)
            for i in (0, 1, 2, 3):
                x, y = map(float, s['Button'+str(i)].split(','))
                assert g.PtInRegion(region, round(x*dpi), round(y*dpi)), 'control outside hit area'
        finally:
            g.DeleteObject(region)
        bx, by, bw, bh = map(float, s['BarArea'].split(','))
        assert float(s['SongTextRight']) < bx and bx+bw < float(s['Width'])
        send('capture')
        (out/f'controls-{dpi}.png').write_bytes((out/'capture.png').read_bytes())
        rows.append({'dpi': dpi, 'controls': 3, 'hit_regions': 'pass', 'text_and_bars': 'pass'})
    send('dpi=1')
    send('music-collapse')
    (out/'results.json').write_text(json.dumps(rows, indent=2), 'utf-8')
    print(json.dumps(rows), flush=True)
    if a.interactive:
        import tkinter as tk
        window = tk.Tk()
        window.title('WinIsland pointer validation')
        window.geometry('1200x650+350+90')
        window.configure(bg='#606870')
        stats = {'clicks': 0, 'dragged': False}
        origin = None
        def save():
            (out/'pointer.json').write_text(json.dumps(stats), 'utf-8')
        def down(event):
            global origin
            stats['clicks'] += 1
            origin = (event.x_root, event.y_root, window.winfo_x(), window.winfo_y())
            save()
        def drag(event):
            if origin:
                dx, dy = event.x_root-origin[0], event.y_root-origin[1]
                window.geometry(f'+{origin[2]+dx}+{origin[3]+dy}')
                stats['dragged'] = True
                save()
        window.bind('<ButtonPress-1>', down)
        window.bind('<B1-Motion>', drag)
        save()
        def poll():
            if (out/'finish.request').exists(): window.destroy()
            else: window.after(200, poll)
        window.after(200, poll)
        window.after(240000, window.destroy)
        window.mainloop()
finally:
    (out/'exit.request').write_text('exit')
    (media/'media-command.txt').write_text('exit')
    app.wait(15)
    producer.wait(15)
