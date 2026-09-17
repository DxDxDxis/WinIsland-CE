export interface Settings {
  resident: boolean; hideNative: boolean; showMusic: boolean; showMessages: boolean; reduceMotion: boolean; seconds: number; fps: number;
  songSource: number; lyricSource: number; playerFilter: number; lyricApi: string;
  showFps: boolean; showPing: boolean; pingTarget: string;
  topArcScale: number; islandZoom: number; widthRatio: number; heightRatio: number; dpiCorrection: number;
  topAttach: boolean; radiusMode: number; monitorDevice: string;
}
export interface ModInfo {
  id: string; name: string; description: string; author: string; version: string; status: string; error: string; path: string;
  signature: string; valid: boolean; loaded: boolean; enabled: boolean; apiVersion: number; loads: number; unloads: number; dependencies: string[];
  entry: string; gameVersion: string; packageHash: string; hostAbi: number; actualAbi: number;
  dependencyDetails: { id: string; name: string; required: string; version: string; satisfied: boolean; reason: string }[];
}
export interface Resource { handle: string; owner: string; key: string; label: string; value: string; kind: number; type: number; minimum: number; maximum: number; choices: string[] }
export interface ModList { busy: boolean; operation: number; completedOperation: number; operationError: string; message: string; mods: ModInfo[]; resources: Resource[] }
export interface Capabilities { version: string; build: string; protocol: number; settingsAnimation: false; reducedMotion: boolean; reducedMotionReason: string; dpi: number; renderer: string; displays: { id: string; width: number; height: number }[] }
export interface MediaState { active: boolean; playing: boolean; key: string; title: string; artist: string; source: string; lyricStatus: string; lyric: string; monitorActive: boolean; monitorPending: boolean; monitorMessage: string; exportPath: string }
export interface Payloads {
  'transfer.call': TransferState;
  'settings.read': { settings: Settings; revision: number }; 'settings.write': Payloads['settings.read']; 'layout.reset': Payloads['settings.read'];
  'capabilities.read': Capabilities; 'media.read': MediaState; 'mods.list': ModList; 'mods.affected': { ids: string[] };
  'mods.import': { operation: number; pending: boolean }; 'mods.action': Payloads['mods.import']; 'mods.invoke': Payloads['mods.import'];
  'mods.folder': object; 'mods.log': { text: string }; 'lyrics.import': object; 'lyrics.clear': object; 'diagnostics.action': { pending: boolean };
}
export type Command = keyof Payloads;
export type Params = { [key: string]: unknown; revision?: number; patch?: Partial<Settings>; id?: string; action?: string; cascade?: boolean; key?: string; handle?: string; value?: string };
export type Reply = ({ ok: true } & Partial<Payloads[Command]>) | { ok: false; code: string; error: string };
export interface SettingsApi { call(id: string, command: Command, params: Params): Promise<Reply>; cancel(id: string): void; close(): void; files(files: File[]): Promise<Reply>; drag(id: string): Promise<Reply> }
declare global { interface Window { winIsland: SettingsApi } }


export interface TransferEntry { id: string; name: string; extension: string; original: string; saved: string; mode: string; state: string; error: string; size: number; imported: number; progress: number }
export interface TransferState { preferences: { sideAutoExpand?: boolean }; enabled: boolean; revision: number; viewRequest: number; selected: string; remember: string; error: string; entries: TransferEntry[]; ids?: string[] }

