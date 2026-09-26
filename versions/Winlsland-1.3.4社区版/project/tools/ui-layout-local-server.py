"""Synthetic translation fixture; binds loopback only and never reads user text."""
from http.server import ThreadingHTTPServer, BaseHTTPRequestHandler
from pathlib import Path
import json, sys, time
root = Path(sys.argv[1])
class Handler(BaseHTTPRequestHandler):
    def log_message(self, *_): pass
    def do_POST(self):
        self.rfile.read(int(self.headers.get('Content-Length', '0')))
        mode=(root/'translation-mode.txt').read_text().strip()
        time.sleep(3 if mode != 'slow' else 12)
        value={'translatedText':'这是用于界面验证的译文。','detectedLanguage':{'language':'en'}} if mode != 'error' else {'error':'Synthetic translation service failure'}
        body=json.dumps(value,ensure_ascii=False).encode()
        self.send_response(200 if mode != 'error' else 503)
        self.send_header('Content-Type','application/json; charset=utf-8')
        self.send_header('Content-Length',str(len(body))); self.end_headers()
        try:self.wfile.write(body)
        except (ConnectionError,OSError):pass
server=ThreadingHTTPServer(('127.0.0.1',0),Handler)
(root/'translation-port.txt').write_text(str(server.server_port))
server.serve_forever()
