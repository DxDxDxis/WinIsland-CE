"""Deterministic test media, generated locally; no runtime FFmpeg dependency."""
from pathlib import Path
from PIL import Image, ImageDraw
import sys, subprocess
root=Path(__file__).resolve().parents[2]/'built'/'media-fixture'/'assets'
root.mkdir(parents=True,exist_ok=True)
image=Image.new('RGBA',(320,120),(0,0,0,0)); d=ImageDraw.Draw(image)
d.rounded_rectangle((0,0,319,119),radius=24,fill=(28,64,96,200))
d.text((24,42),'WinIsland r1 / PNG',fill=(255,255,255,255))
image.save(root/'skin.png')
frames=[]
for x,color in zip((8,44,80,116),((230,70,80,255),(60,210,130,255),(55,120,245,255),(230,180,50,255))):
    im=Image.new('RGBA',(160,80),(0,0,0,0)); draw=ImageDraw.Draw(im)
    draw.rectangle((x,16,x+28,60),fill=color); frames.append(im)
frames[0].save(root/'motion.gif',save_all=True,append_images=frames[1:],duration=[120,200,160,240],loop=0,disposal=[1,2,3,1],optimize=False)
gif=Image.open(root/'motion.gif')
for i in range(gif.n_frames):
    gif.seek(i); im=gif.convert('RGBA'); raw=bytearray()
    for r,g,b,a in im.getdata():raw.extend(((b*a+127)//255,(g*a+127)//255,(r*a+127)//255,a))
    (root/f'gif-{i}.bgra').write_bytes(raw)
(root/'broken.png').write_bytes(b'not an image')
if len(sys.argv)>1:
    subprocess.run([sys.argv[1],'-y','-f','lavfi','-i','testsrc2=size=320x180:rate=30','-f','lavfi','-i','sine=frequency=440:sample_rate=44100','-t','3','-c:v','libx264','-pix_fmt','yuv420p','-g','15','-c:a','aac','-movflags','+faststart',str(root/'motion.mp4')],check=True)
print(root)
