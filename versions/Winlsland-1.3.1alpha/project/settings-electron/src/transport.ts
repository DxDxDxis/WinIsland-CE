import net from 'node:net';
import type { Reply } from './protocol';
// One length-prefixed UTF-8 JSON request per connection, closed on every exit path.
export function hostCall(pipe: string, token: string, command: string, params: object, signal?: AbortSignal, timeout = 8000): Promise<Reply> {
  return new Promise(resolve => {
    if (!/^\\\\\.\\pipe\\WinIsland\.Settings\.\d+$/.test(pipe) || !token) { resolve({ ok: false, code: 'CORE_OFFLINE', error: '请从 WinIsland 主程序打开设置窗口' }); return; }
    let done = false; let bytes = Buffer.alloc(0);
    let socket: net.Socket; let retry: ReturnType<typeof setTimeout>; let received: Reply | undefined;
    const finish = (reply: Reply) => { if (done) return; done = true; clearTimeout(timer); clearTimeout(retry); signal?.removeEventListener('abort', cancel); socket?.destroy(); resolve(reply); };
    const cancel = () => finish({ ok: false, code: 'CANCELLED', error: '请求已取消；已提交的操作请刷新确认实际状态' });
    const timer = setTimeout(() => finish({ ok: false, code: 'TIMEOUT', error: '核心响应超时，请刷新确认实际状态' }), timeout);
    signal?.addEventListener('abort', cancel, { once: true }); if (signal?.aborted) cancel();
    function connect() { if(done)return; socket = net.createConnection(pipe);
    socket.on('connect', () => {
      const body = Buffer.from(JSON.stringify({ protocol: 1, token, command, params }));
      if (body.length > 65536) { finish({ ok: false, code: 'SIZE', error: '请求过大' }); return; }
      const length = Buffer.alloc(4); length.writeUInt32LE(body.length); socket.write(Buffer.concat([length, body]));
    });
    socket.on('data', b => {
      bytes = Buffer.concat([bytes, b]); if (bytes.length < 4) return;
      const length = bytes.readUInt32LE(0);
      if (!length || length > 4 * 1024 * 1024) { finish({ ok: false, code: 'PROTOCOL', error: '响应长度无效' }); return; }
      if (bytes.length >= length + 4) {
        try { const reply = JSON.parse(bytes.subarray(4, length + 4).toString()); if (typeof reply.ok !== 'boolean') throw Error(); received=reply; socket.end(Buffer.from([1]), () => finish(reply)); }
        catch { finish({ ok: false, code: 'PROTOCOL', error: '核心返回了无效数据' }); }
      }
    });
    socket.on('error', (e: NodeJS.ErrnoException) => { if(received){finish(received);return;} if(e.code==='EBUSY'&&!done){socket.destroy();retry=setTimeout(connect,25);return;} finish({ ok: false, code: 'CORE_OFFLINE', error: '无法连接 WinIsland 核心，请确认主程序仍在运行后重试' }); });
    socket.on('end', () => { if (!done) finish(received ?? { ok: false, code: 'DISCONNECTED', error: '核心已断开连接，设置尚未确认' }); });
    } connect();
  });
}

