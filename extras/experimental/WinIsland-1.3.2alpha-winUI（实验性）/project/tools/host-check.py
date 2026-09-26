"""Test harness only: exercise the real, isolated host protocol without UI credentials in output."""
from pathlib import Path
import json,ctypes,struct,time,sys
root=Path(__file__).resolve().parents[1]
active=json.loads((root/'verification/native-live/active.json').read_text(encoding='utf-8'))
data=Path(active['data']);conn=json.loads((data/'settings-connection.json').read_text())
k=ctypes.WinDLL('kernel32',use_last_error=True)
k.CreateFileW.argtypes=[ctypes.c_wchar_p,ctypes.c_ulong,ctypes.c_ulong,ctypes.c_void_p,ctypes.c_ulong,ctypes.c_ulong,ctypes.c_void_p];k.CreateFileW.restype=ctypes.c_void_p
k.ReadFile.argtypes=[ctypes.c_void_p,ctypes.c_void_p,ctypes.c_ulong,ctypes.POINTER(ctypes.c_ulong),ctypes.c_void_p]
k.WriteFile.argtypes=k.ReadFile.argtypes;k.CloseHandle.argtypes=[ctypes.c_void_p]
def call(command,params=None):
 req=json.dumps({'protocol':1,'token':conn['token'],'command':command,'params':params or {}}).encode()
 for i in range(200):
  h=k.CreateFileW(conn['pipe'],0xc0000000,0,None,3,0,None)
  if h!=ctypes.c_void_p(-1).value:break
  time.sleep(.025)
 else:raise RuntimeError('IPC unavailable')
 def read(n):
  out=b''
  while len(out)<n:
   b=ctypes.create_string_buffer(n-len(out));num=ctypes.c_ulong()
   if not k.ReadFile(h,b,len(b),ctypes.byref(num),None) or not num.value:raise RuntimeError('read failed')
   out+=b.raw[:num.value]
  return out
 try:
  blob=struct.pack('<I',len(req))+req;n=ctypes.c_ulong();assert k.WriteFile(h,blob,len(blob),ctypes.byref(n),None)
  size=struct.unpack('<I',read(4))[0];assert 0<size<=4194304;reply=json.loads(read(size));k.WriteFile(h,b'\1',1,ctypes.byref(n),None)
  if not reply.get('ok'):raise RuntimeError(reply)
  return reply
 finally:k.CloseHandle(h)
if __name__=='__main__':
 command=sys.argv[1] if len(sys.argv)>1 else 'settings.read'
 params=json.loads(sys.argv[2]) if len(sys.argv)>2 else {}
 result=call(command,params)
 print(json.dumps(result,ensure_ascii=True))
