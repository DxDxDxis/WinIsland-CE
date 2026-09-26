"""Synthetic end-to-end translation probes; never submits user clipboard contents."""
import json
import sys
import subprocess
import os
import time
import urllib.request
import urllib.error
from pathlib import Path
sys.stdout.reconfigure(encoding='utf-8')

base=sys.argv[1] if len(sys.argv)>1 else 'http://127.0.0.1:8788'
samples={'zh-Hans':'离开房间前请关上窗户。','zh-Hant':'離開房間前請關上窗戶。',
 'en':'Please close the window before you leave the room.','en-US':'Please close the window before you leave the room.',
 'ja':'部屋を出る前に窓を閉めてください。','ko':'방을 나가기 전에 창문을 닫아 주세요.',
 'ru':'Пожалуйста, закройте окно перед выходом из комнаты.',
 'hi':'कृपया कमरे से निकलने से पहले खिड़की बंद कर दें।','de':'Bitte schließen Sie das Fenster, bevor Sie den Raum verlassen.'}
results=[]
def call(path,payload=None):
 data=json.dumps(payload,ensure_ascii=False).encode() if payload else None
 args=['curl.exe' if os.name=='nt' else 'curl','--silent','--show-error','--max-time','60','--write-out','\n%{http_code}','-H','Content-Type: application/json']
 if data is not None:args+=['--data-binary','@-']
 result=subprocess.run(args+[base+path],input=data,stdout=subprocess.PIPE,stderr=subprocess.PIPE,check=True)
 body,status=result.stdout.rsplit(b'\n',1)
 return int(status),json.loads(body)

for src,dst in [('en',k) for k in samples if k not in ('en','en-US')]+[(k,'en') for k in samples if k!='en']+[('de','ja'),('zh-Hans','zh-Hant'),('zh-Hant','zh-Hans'),('auto','en'),('en','en-US')]:
 start=time.monotonic();status,reply=call('/translate',{'q':samples['zh-Hant' if src=='auto' else src],'source':src,'target':dst,'format':'text'})
 # Keep the public API's actual per-client rate limit in place during tests.
 if status==429:
  time.sleep(61);start=time.monotonic();status,reply=call('/translate',{'q':samples['zh-Hant' if src=='auto' else src],'source':src,'target':dst})
 result={'source':src,'target':dst,'status':status,'seconds':round(time.monotonic()-start,2),'reply':reply,'passed':status==200 and bool(reply.get('translatedText'))}
 results.append(result);print(json.dumps(result,ensure_ascii=False),flush=True)
 Path(sys.argv[2] if len(sys.argv)>2 else '/opt/winisland-translate/cloud-verification.json').write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding='utf-8')
sys.exit(0 if all(r['passed'] for r in results) else 1)
