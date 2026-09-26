"""Build synthetic, copyright-free protocol fixtures using an independent Python reference.
Requires pycryptodome only for regenerating fixtures, never at application runtime.
"""
from pathlib import Path
import sys, json, re, base64, zlib, hashlib
r=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(r/'verification/lyrics-1.3.4/python-deps'))
from Crypto.Cipher import AES
from Crypto.Util.Padding import pad
p=r/'test-fixtures/lyrics';p.mkdir(parents=True,exist_ok=True)
up=Path('C:/Users/Public/WinIsland-Lyricify-audit-20260925/Lyricify.Lyrics.Helper/Decrypter/Qrc/DESHelper.cs').read_text('utf-8-sig')
sboxes=[list(map(int,re.findall(r'\d+',re.search(r'sbox'+str(i)+r' = \{(.*?)\}',up,re.S)[1]))) for i in range(1,9)]
pc_c=[56,48,40,32,24,16,8,0,57,49,41,33,25,17,9,1,58,50,42,34,26,18,10,2,59,51,43,35]
pc_d=[62,54,46,38,30,22,14,6,61,53,45,37,29,21,13,5,60,52,44,36,28,20,12,4,27,19,11,3]
pc2=[13,16,10,23,0,4,2,27,14,5,20,9,22,18,11,3,25,7,15,6,26,19,12,1,40,51,30,36,46,54,29,39,50,44,32,47,43,48,38,55,33,52,45,41,49,35,28,31]
ip=[58,50,42,34,26,18,10,2,60,52,44,36,28,20,12,4,62,54,46,38,30,22,14,6,64,56,48,40,32,24,16,8,57,49,41,33,25,17,9,1,59,51,43,35,27,19,11,3,61,53,45,37,29,21,13,5,63,55,47,39,31,23,15,7]
fp=[ip.index(i)+1 for i in range(1,65)]
e=[32,1,2,3,4,5,4,5,6,7,8,9,8,9,10,11,12,13,12,13,14,15,16,17,16,17,18,19,20,21,20,21,22,23,24,25,24,25,26,27,28,29,28,29,30,31,32,1]
pbox=[16,7,20,21,29,12,28,17,1,15,23,26,5,18,31,10,2,8,24,14,32,27,3,9,19,13,30,6,22,11,4,25]
def perm(v,table,width):
 out=0
 for i in table:out=(out<<1)|((v>>(width-i))&1)
 return out
def swap(b):return b[0:4][::-1]+b[4:8][::-1]
def schedule(k):
 k=int.from_bytes(swap(k),'big');c=perm(k,[v+1 for v in pc_c],64)<<4;d=perm(k,[v+1 for v in pc_d],64)<<4;out=[]
 for sh in [1,1,2,2,2,2,2,2,1,2,2,2,2,2,2,1]:
  c=((c<<sh)|(c>>(28-sh)))&0xfffffff0;d=((d<<sh)|(d>>(28-sh)))&0xfffffff0
  out.append((perm(c,[v+1 for v in pc2[:24]],32)<<24)|perm(d,[v-27+1 for v in pc2[24:]],32))
 return out
def des(b,keys):
 v=perm(int.from_bytes(swap(b),'big'),ip,64);l=v>>32;r=v&0xffffffff
 for k in keys:
  v=perm(r,e,32)^k;x=0
  for i in range(8):
   q=(v>>(42-i*6))&63;index=(q&32)|((q&31)>>1)|((q&1)<<4);x=(x<<4)|sboxes[i][index]
  l,r=r,l^perm(x,pbox,32)
 return swap(perm((r<<32)|l,fp,64).to_bytes(8,'big'))
def qrc(text):
 raw=zlib.compress(text.encode());raw+=b'\0'*((-len(raw))%8);key=b'!@#)(*$%123ZXC!@!@#)(NHL';keys=[schedule(key[:8]),schedule(key[8:16])[::-1],schedule(key[16:])]
 out=b''
 for i in range(0,len(raw),8):
  b=raw[i:i+8]
  for k in keys:b=des(b,k)
  out+=b
 return out.hex()
plain='[1000,1000]Hel(1000,400)lo(1400,600)\n[2500,500]world(2500,500)'
(p/'sample.qrc.txt').write_text(plain,encoding='utf-8');(p/'sample.qrc.hex').write_text(qrc(plain))
lang=base64.b64encode(json.dumps({'content':[{'type':1,'lyricContent':[['你好'],['世界']]},{'type':0,'lyricContent':[['he','llo'],['world']]}]},ensure_ascii=False).encode()).decode()
krc='\ufeff[language:'+lang+']\n[1000,1000]<0,400,0>Hel<400,600,0>lo\n[2500,500]<0,500,0>world'
k=zlib.compress(krc.encode());key=bytes.fromhex('404761775e3274475136312dced26e69');(p/'sample.krc.b64').write_text(base64.b64encode(b'krc1'+bytes(c^key[i%16] for i,c in enumerate(k))).decode())
yrc='[1000,1000](1000,400,0)Hel(1400,600,0)lo\n[2500,500](2500,500,0)world'
(p/'sample.yrc').write_text(yrc)
lrc='[offset:500]\n[00:01.00][00:02.50]Hello\n[00:03.00]world'
(p/'sample.lrc').write_text(lrc)
(p/'netease.json').write_text(json.dumps({'yrc':{'lyric':yrc},'lrc':{'lyric':'[00:01.00]Hello\n[00:02.50]world'},'ytlrc':{'lyric':'[00:01.00]你好\n[00:02.50]世界'},'yromalrc':{'lyric':'[00:01.00]he llo\n[00:02.50]world'}},ensure_ascii=False),encoding='utf-8')
ttml='''<tt xmlns="http://www.w3.org/ns/ttml" xmlns:ttm="http://www.w3.org/ns/ttml#metadata" xmlns:i="http://music.apple.com/lyric-ttml-internal" xml:lang="en"><head><metadata><i:translation><i:text for="L1">你好</i:text></i:translation></metadata></head><body><div><p begin="00:01.000" end="00:02.000" i:key="L1"><span begin="1s" end="1.4s">Hel</span><span begin="1.4s" end="2s">lo</span><span ttm:role="x-bg" begin="1s" end="2s">(back)</span><span ttm:role="x-roman">he llo</span></p><p begin="2.5s" dur="500ms">world</p></div></body></tt>'''
(p/'sample.ttml').write_text(ttml,encoding='utf-8');(p/'apple.json').write_text(json.dumps({'data':[{'relationships':{'syllable-lyrics':{'data':[{'attributes':{'ttml':ttml}}]}}}]}),encoding='utf-8')
data='{"id":"1","lv":"-1"}';path='/api/song/lyric/v1';digest=hashlib.md5(('nobody'+path+'use'+data+'md5forencrypt').encode()).hexdigest();frame=path+'-36cd479b6b5-'+data+'-36cd479b6b5-'+digest
(p/'eapi.expected').write_text('params='+AES.new(b'e82ckenh8dichen8',AES.MODE_ECB).encrypt(pad(frame.encode(),16)).hex().upper())
secret=b'abcdefghijklmnop';iv=b'0102030405060708';first=base64.b64encode(AES.new(b'0CoJUm6Qyw8W8jud',AES.MODE_CBC,iv).encrypt(pad(data.encode(),16)));params=base64.b64encode(AES.new(secret,AES.MODE_CBC,iv).encrypt(pad(first,16))).decode()
mod=int('e0b509f6259df8642dbc35662901477df22677ec152b5ff68ace615bb7b725152b3ab17a876aea8a5aa76d2e417629ec4ee341f56135fccf695280104e0312ecbda92557c93870114af6c9d05c4f7f0c3685b7a46bee255932575cce10b424d813cfe4875d3e82047b97ddef52741d546b8e289dc6935b3ece0462db0a22b8e7',16)
from urllib.parse import quote
(p/'weapi.expected').write_text('params='+quote(params,safe='')+'&encSecKey='+format(pow(int.from_bytes(secret[::-1],'big'),65537,mod),'0256x'))
(p/'expected.json').write_text(json.dumps({'provenance':'synthetic test text; no downloaded song lyrics','lines':2,'text':['Hello','world'],'starts':[1,2.5],'ends':[2,3],'syllables':[2,1],'translation':['你好','世界'],'romanization':['he llo','world']},ensure_ascii=False,indent=2),encoding='utf-8')
print('Generated synthetic fixtures:',p)
