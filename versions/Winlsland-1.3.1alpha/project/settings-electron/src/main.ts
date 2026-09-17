import { app, BrowserWindow, dialog, ipcMain, session, nativeImage } from 'electron';
import path from 'node:path';
import fs from 'node:fs';
import { hostCall } from './transport';
import { registerTransfer, importTransferPaths } from './transfer-main';
import type { Command, Reply } from './protocol';
const arg = (name: string) => process.argv.find(x => x.startsWith('--' + name + '='))?.slice(name.length + 3) ?? '';
const pipe = arg('pipe'), token = arg('token'), corePid = arg('core-pid'), sessionDir = arg('session-dir');
if (!sessionDir || !path.isAbsolute(sessionDir)) throw new Error('设置组件必须由宿主提供已确认的数据目录');
const userData = path.join(sessionDir, 'electron-data');
fs.mkdirSync(userData, { recursive: true }); app.setPath('userData', userData); app.setPath('sessionData', path.join(userData, 'chromium'));
for (const name of ['temp','logs','crashDumps'] as const) { const dir = path.join(sessionDir, name === 'temp' ? 'temp' : 'logs/electron/' + name); fs.mkdirSync(dir, { recursive: true }); app.setPath(name, dir); }
app.commandLine.appendSwitch('log-file', path.join(sessionDir, 'logs/electron/chromium.log'));
let window: BrowserWindow | null = null;
const calls = new Map<string, AbortController>();
let chain: Promise<unknown> = Promise.resolve();
const allowed: Command[] = ['transfer.call', 'settings.read', 'settings.write', 'layout.reset', 'capabilities.read', 'media.read', 'lyrics.import', 'lyrics.clear', 'diagnostics.action', 'mods.list', 'mods.affected', 'mods.import', 'mods.action', 'mods.invoke', 'mods.folder', 'mods.log'];
function validSender(event: Electron.IpcMainInvokeEvent | Electron.IpcMainEvent) {
  return !!window && event.sender === window.webContents && event.senderFrame === window.webContents.mainFrame;
}
ipcMain.handle('host:call', async (event, id: unknown, command: Command, params: unknown): Promise<Reply> => {
  if (!validSender(event) || typeof id !== 'string' || id.length > 80 || calls.has(id) || calls.size >= 32 || !allowed.includes(command) ||
      !params || typeof params !== 'object' || JSON.stringify(params).length > 60000) return { ok: false, code: 'INVALID', error: '无效的设置请求' };
  const controller = new AbortController(); calls.set(id, controller);
  const run = async (): Promise<Reply> => {
    if (controller.signal.aborted) return { ok: false, code: 'CANCELLED', error: '已取消请求' };
    let input = params as Record<string, unknown>;
    if (command === 'mods.import' || command === 'lyrics.import') {
      const extensions = command === 'mods.import' ? ['wimod'] : ['lrc'];
      const selection = await dialog.showOpenDialog(window!, { title: command === 'mods.import' ? '添加模组' : '导入当前歌曲歌词', properties: ['openFile'], filters: [{ name: extensions[0], extensions }] });
      if (selection.canceled || !selection.filePaths[0] || controller.signal.aborted) return { ok: false, code: 'CANCELLED', error: '已取消选择' };
      input = { ...input, path: selection.filePaths[0] };
    }
    if (command === 'transfer.call') {
      const action = input.action;
      if (action === 'pick' || action === 'relink') {
        const lease = () => hostCall(pipe, token, 'transfer.call', { action: 'interaction', active: true }).catch(() => {});
        await lease(); const renewal = setInterval(() => void lease(), 2000);
        const choice = await dialog.showOpenDialog(window!, { title: action === 'pick' ? '添加中转文件（稍后选择是否复制）' : '重新定位文件', properties: action === 'pick' ? ['openFile', 'multiSelections'] : ['openFile'] }).finally(() => { clearInterval(renewal); void hostCall(pipe, token, 'transfer.call', { action: 'interaction', active: false }).catch(() => {}); });
        if (choice.canceled || !choice.filePaths.length) return { ok: false, code: 'CANCELLED', error: '已取消选择' };
        if (action === 'pick') return importTransferPaths(params => hostCall(pipe, token, 'transfer.call', params, controller.signal), choice.filePaths);
        input = { action: 'relink', id: input.id, path: choice.filePaths[0] };
      } else if (!['list', 'enable', 'choose', 'cancel', 'remove', 'location', 'refresh', 'forget', 'side-auto', 'interaction'].includes(String(action))) return { ok: false, code: 'INVALID', error: '此中转操作不对渲染页开放' };
    }
    return hostCall(pipe, token, command, input, controller.signal);
  };
  const result = chain.then(run, run).catch((error: unknown): Reply => ({ ok: false, code: 'IPC', error: String(error) }));
  chain = result.catch(() => undefined);
  try { return await result; } finally { calls.delete(id); }
});
registerTransfer(() => window, params => hostCall(pipe, token, 'transfer.call', params, new AbortController().signal, params.action === 'drag' ? 60000 : 15000));
ipcMain.on('host:cancel', (event, id: string) => { if (validSender(event)) calls.get(id)?.abort(); });
ipcMain.on('window:close', event => { if (validSender(event)) window?.close(); });
function create() {
  window = new BrowserWindow({ width: 960, height: 780, minWidth: 640, minHeight: 480, title: 'WinIsland 设置 · 1.3.1alpha 社区版',
    backgroundColor: '#f5f6f9', autoHideMenuBar: true,
    webPreferences: { contextIsolation: true, nodeIntegration: false, sandbox: true, preload: path.join(__dirname, 'preload.js') } });
  window.setMenu(null); window.webContents.setWindowOpenHandler(() => ({ action: 'deny' }));
  window.webContents.on('will-navigate', event => event.preventDefault());
  window.on('closed', () => { for (const c of calls.values()) c.abort(); window = null; });
  void window.loadFile(path.join(__dirname, 'index.html'));
}
if (!app.requestSingleInstanceLock({ corePid })) app.quit();
else {
  app.on('second-instance', () => { if (window) { if (window.isMinimized()) window.restore(); window.show(); window.focus(); } else create(); });
  app.whenReady().then(() => {
    session.defaultSession.setPermissionRequestHandler((_wc, _permission, callback) => callback(false));
    session.defaultSession.setPermissionCheckHandler(() => false); create();
  });
  app.on('window-all-closed', () => app.quit());
}

