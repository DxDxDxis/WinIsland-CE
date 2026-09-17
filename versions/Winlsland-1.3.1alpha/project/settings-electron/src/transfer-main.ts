import { BrowserWindow, ipcMain } from 'electron';
import path from 'node:path';
import type { Reply } from './protocol';
type Invoke = (params: Record<string, unknown>) => Promise<Reply>;
export async function importTransferPaths(call: Invoke, paths: string[]): Promise<Reply> {
  const ids: string[] = []; let batch: string[] = [];
  const commit = async () => { const reply = await call({ action: 'import', paths: batch }) as Reply & { ids?: string[] }; if (!reply.ok) throw Error(reply.error); ids.push(...(reply.ids || [])); batch = []; };
  try {
    for (const file of paths) { if (batch.length && (batch.length >= 128 || Buffer.byteLength(JSON.stringify([...batch, file]), 'utf8') > 56000)) await commit(); batch.push(file); }
    if (batch.length) await commit(); return { ok: true, ids };
  } catch (error) { return { ok: false, code: 'IMPORT', error: `导入失败；此前已加入 ${ids.length} 条记录，可在待选择列表中处理。${String(error)}` }; }
}
export function registerTransfer(getWindow: () => BrowserWindow | null, call: Invoke) {
  const valid = (event: Electron.IpcMainEvent | Electron.IpcMainInvokeEvent) => {
    const win = getWindow(); return win && event.sender === win.webContents && event.senderFrame === win.webContents.mainFrame;
  };
  const resolve = async (id: unknown) => {
    if (typeof id !== 'string' || !/^[\da-f-]{36}$/i.test(id)) throw Error('无效中转条目');
    const reply = await call({ action: 'resolve', id }) as Reply & { path?: string };
    if (!reply.ok || !reply.path) throw Error(!reply.ok ? reply.error : '文件不可用'); return reply.path;
  };
  ipcMain.handle('transfer:files', async (event, paths: unknown) => {
    if (!valid(event) || !Array.isArray(paths) || !paths.length || paths.length > 2000 || paths.some(p => typeof p !== 'string' || !path.isAbsolute(p))) return { ok: false, error: '无效的文件拖入' };
    return importTransferPaths(call, paths);
  });
  ipcMain.handle('transfer:drag', async (event, id: unknown) => {
    if (!valid(event)) return { ok: false, error: '无效拖出窗口' };
    try {
      await resolve(id); if (!valid(event)) return { ok: false, error: '窗口已关闭' };
      // Host OLE drag source restricts the effect to COPY, including same-volume targets.
      return await call({ action: 'drag', id });
    } catch (error) { return { ok: false, error: String(error) }; }
  });
}
