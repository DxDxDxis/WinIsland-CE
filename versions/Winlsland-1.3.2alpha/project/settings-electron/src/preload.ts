import { contextBridge, ipcRenderer, webUtils } from 'electron';
import type { SettingsApi, Command, Params } from './protocol';
const api: SettingsApi = {
  call: (id: string, command: Command, params: Params) => ipcRenderer.invoke('host:call', id, command, params),
  cancel: (id: string) => ipcRenderer.send('host:cancel', id),
  close: () => ipcRenderer.send('window:close'),
  files: files => ipcRenderer.invoke('transfer:files', files.map(file => webUtils.getPathForFile(file)).filter(Boolean)),
  drag: id => ipcRenderer.invoke('transfer:drag', id)
};
contextBridge.exposeInMainWorld('winIsland', Object.freeze(api));

