"""Small HTTPS-upstream gateway; Caddy terminates TLS. No text/access logging."""
import collections
import json
import os
from pathlib import Path
import subprocess
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

LANGUAGES={'zh-Hans':'简体中文','zh-Hant':'繁體中文','en':'English','en-US':'English (US alias)','ja':'日本語','ko':'한국어','ru':'Русский','hi':'हिन्दी','de':'Deutsch'}
semaphore=threading.BoundedSemaphore(1)
lock=threading.Lock()
requests={}

def missing_models():
    return [a+'-'+b for lang in ('zh','de','hi','ja','ko','ru')
            for a,b in (('en',lang),(lang,'en'))
            if not (Path('/models')/(a+'-'+b)/'model'/'model.bin').is_file()
            or not (Path('/models')/(a+'-'+b)/'sentencepiece.model').is_file()]

class Handler(BaseHTTPRequestHandler):
    protocol_version='HTTP/1.1'
    def log_message(self,*args): pass
    def setup(self):
        super().setup();self.connection.settimeout(10)
    def respond(self,status,body):
        data=json.dumps(body,ensure_ascii=False).encode()
        self.send_response(status);self.send_header('Content-Type','application/json; charset=utf-8');self.send_header('Content-Length',str(len(data)));self.send_header('Cache-Control','no-store');self.send_header('Connection','close');self.end_headers();self.close_connection=True
        try:self.wfile.write(data)
        except (BrokenPipeError,ConnectionResetError):pass
    def do_GET(self):
        if self.path=='/languages':self.respond(200,[{'code':k,'name':v,'targets':list(LANGUAGES)} for k,v in LANGUAGES.items()])
        elif self.path=='/health':
            missing=missing_models()
            self.respond(503 if missing else 200,{'status':'loading' if missing else 'ok','service':'winisland-translation','version':2,'languages':list(LANGUAGES),'missingModels':missing})
        else:self.respond(404,{'error':'Not found'})
    def do_POST(self):
        if self.path!='/translate':return self.respond(404,{'error':'Not found'})
        # Only a private Docker network / loopback port can reach this listener.
        client=self.headers.get('X-Forwarded-For',self.client_address[0]).split(',')[0].strip()
        now=time.monotonic()
        with lock:
            if len(requests)>10000:
                for key in list(requests):
                    if not requests[key] or now-requests[key][-1]>3600:del requests[key]
                if len(requests)>10000:return self.respond(503,{'error':'Server busy'})
            stamps=requests.setdefault(client,collections.deque())
            while stamps and now-stamps[0]>3600:stamps.popleft()
            if len(stamps)>=120 or sum(now-t<60 for t in stamps)>=12:return self.respond(429,{'error':'Rate limit exceeded; retry later'})
            stamps.append(now)
        if not semaphore.acquire(blocking=False):return self.respond(429,{'error':'Translation worker busy; retry shortly'})
        try:
            size=int(self.headers.get('Content-Length','0'))
            if size<1 or size>32768:return self.respond(413,{'error':'Request must be 1-32768 bytes'})
            q=json.loads(self.rfile.read(size))
            if not isinstance(q,dict) or not isinstance(q.get('q'),str) or not 1<=len(q['q'])<=4000 or not q['q'].strip() or q.get('format','text')!='text':return self.respond(400,{'error':'Plain text, 1-4000 characters required'})
            if q.get('target') not in LANGUAGES and q.get('target') not in ('zh','zh-CN','zh-TW'):return self.respond(400,{'error':'Unsupported target language'})
            if q.get('source','auto') not in (*LANGUAGES,'auto','zh','zh-CN','zh-TW'):return self.respond(400,{'error':'Unsupported source language'})
            task=subprocess.run([sys.executable,'-I','-B','/service/cloud_infer.py'],input=json.dumps(q,ensure_ascii=False).encode(),stdout=subprocess.PIPE,stderr=subprocess.DEVNULL,timeout=50)
            if task.returncode or not task.stdout:return self.respond(503,{'error':'Translation worker failed'})
            result=json.loads(task.stdout)
            self.respond(400 if 'error' in result else 200,result)
        except subprocess.TimeoutExpired:self.respond(504,{'error':'Translation timed out; shorten the text'})
        except (ValueError,KeyError,TypeError):self.respond(400,{'error':'Invalid request'})
        except Exception:self.respond(503,{'error':'Translation unavailable'})
        finally:semaphore.release()

if __name__=='__main__':
    ThreadingHTTPServer(('0.0.0.0',5000),Handler).serve_forever()
