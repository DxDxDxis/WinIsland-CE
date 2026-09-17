import { contextBridge, ipcRenderer } from 'electron';
import type { SettingsApi, Command, Params } from './protocol';
const api: SettingsApi = {
  call: (id: string, command: Command, params: Params) => ipcRenderer.invoke('host:call', id, command, params),
  cancel: (id: string) => ipcRenderer.send('host:cancel', id),
  close: () => ipcRenderer.send('window:close')
};
contextBridge.exposeInMainWorld('winIsland', Object.freeze(api));

