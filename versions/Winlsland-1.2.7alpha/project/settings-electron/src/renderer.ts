import type { Command, Params, Payloads, Settings, ModList, ModInfo, Capabilities, MediaState } from './protocol';
import { registerPanel, findControl, coreControls, showPanel, selectTab, disposeMotion, navigationSnapshot, restoreNavigationScroll } from './motion.js';
const el = <T extends HTMLElement = HTMLElement>(id: string) => (document.getElementById(id) ?? findControl(id)) as T;
const make = <K extends keyof HTMLElementTagNameMap>(tag: K, text = '', cls = '') => { const e = document.createElement(tag); e.textContent = text; e.className = cls; return e; };
let settings: Settings | null = null, revision = 0, capabilities: Capabilities, media: MediaState;
let page = 0, pluginPage = false, working = false, connected = false, sequence = 0, pluginSignature = '';
let modState: ModList | null = null, selectedFilter = 'all';
let savedFocus: HTMLElement | null = null, savedScroll = 0;
let cancelled=false;
let correction = '';
let disposed = false, switchDrain: Promise<void> | undefined;
let initialNavigation: { scrolls: Record<string, number>; focus?: string } | undefined;
let initialFocus: string | undefined;
const switchIntent = new Map<keyof Settings, boolean>();
const switchQueue = new Map<keyof Settings, boolean>();
const outstanding = new Set<string>();
async function call<C extends Command>(command: C, params: Params = {}): Promise<Payloads[C]> {
  const id = String(++sequence); outstanding.add(id);
  try { const reply = await window.winIsland.call(id, command, params); if (!reply.ok) throw new Error(reply.error); return reply as Payloads[C]; }
  finally { outstanding.delete(id); }
}
function showError(error: unknown) { el('error').hidden = false; el('error').textContent = error instanceof Error ? error.message : String(error); }
function controls() {
  coreControls().forEach(e => { const disabled = working || !connected || e.dataset.unavailable === 'true'; if (e.disabled !== disabled) e.disabled = disabled; });
  el('cancel').hidden = !working; el('content').setAttribute('aria-busy', String(working));
  if (!working && connected && initialFocus) {
    const target = document.getElementById(initialFocus); initialFocus = undefined;
    if (target && !target.closest('[inert]')) target.focus({ preventScroll: true });
  }
}
async function action(run: () => Promise<void>) {
  // Other save commands wait behind the field queue; typing/focus remains usable.
  if (switchDrain) await switchDrain;
  if (disposed) return;
  if (working) return; cancelled=false; correction = ''; working = true; controls(); el('error').hidden = true; el('status').textContent = '正在处理…';
  try { await run(); el('status').textContent = correction || '操作已完成'; }
  catch (error) { showError(error); el('status').textContent = '操作未确认，请查看错误或重新读取状态'; }
  finally { working = false; controls(); }
}
function button(id: string, text: string, run: () => Promise<void>, parent: HTMLElement) { const b = make('button', text); b.id = id; b.dataset.core = ''; b.onclick = () => void action(run); parent.append(b); return b; }
type Field = { key: keyof Settings; label: string; hint?: string; page: number; min?: number; max?: number; options?: string[]; type?: 'check' | 'text' };
const fields: Field[] = [
  { key: 'resident', label: '灵动岛常驻', hint: '关闭后，仅在收到通知时滑入屏幕', page: 0, type: 'check' },
  { key: 'hideNative', label: '隐藏 Windows 自带通知', hint: '保留通知中心记录；只在核心健康运行时接管横幅', page: 0, type: 'check' },
  { key: 'seconds', label: '通知停驻时间', hint: '0.1–3600 秒，支持小数', page: 0, min: .1, max: 3600 },
  { key: 'fps', label: '动画帧率', hint: '0 跟随系统；或 30–240 的整数，仅控制灵动岛动画', page: 0, min: 0, max: 240 },
  { key: 'songSource', label: '歌曲信息来源', page: 1, options: ['自动（SMTC → 窗口标题）', '仅 SMTC', '仅窗口标题'] },
  { key: 'playerFilter', label: '播放器', page: 1, options: ['自动识别', '网易云音乐', 'QQ 音乐', '汽水音乐'] },
  { key: 'lyricSource', label: '歌词来源', page: 1, options: ['自动（缓存优先，多源查询）', '仅 LRCLIB（在线）', '关闭歌词', '仅网易云歌词', '仅 LRCLIB 搜索', 'QCloudMusicApi（网易云歌词）'] },
  { key: 'lyricApi', label: '歌词 API 地址', hint: '兼容 LRCLIB 的 HTTPS 服务；在线查询发送歌曲信息', page: 1, type: 'text' },
  { key: 'showFps', label: '显示帧率（FPS）', hint: '前台应用 DXGI 提交帧率；不支持时显示 --', page: 2, type: 'check' },
  { key: 'showPing', label: '显示网络延迟（Ping）', hint: '每 3 秒检测 ICMP 往返延迟', page: 2, type: 'check' },
  { key: 'pingTarget', label: 'Ping 目标', hint: 'IPv4 / IPv6；留空使用本地网关', page: 2, type: 'text' },
  { key: 'islandZoom', label: '灵动岛缩放比例', page: 4, min: 1, max: 2 },
  { key: 'widthRatio', label: '宽度倍率', page: 4, min: .75, max: 1.5 },
  { key: 'heightRatio', label: '高度倍率', page: 4, min: .75, max: 1.5 },
  { key: 'dpiCorrection', label: 'DPI 尺寸修正', hint: '不改变工作区坐标', page: 4, min: .8, max: 1.25 },
  { key: 'topAttach', label: '顶部贴合工作区', page: 4, type: 'check' },
  { key: 'radiusMode', label: '圆角模式', page: 4, options: ['自动（贴顶平直，下角圆润）', '半圆下角', '2/3 圆度'] },
  { key: 'monitorDevice', label: '显示器', page: 4, options: [] },
  { key: 'topArcScale', label: '顶部外扩圆弧', hint: '0.5–1.5 倍，默认 1.0；仅影响贴顶外弧', page: 4, min: .5, max: 1.5 }
];
const tabs = ['界面通知', '音乐歌词', '实时信息', '运行诊断', '其他设置'];
function navigate(index: number, focus = false) {
  const returning = pluginPage;
  const direction = returning ? 0 : Math.sign(index - page) as -1 | 0 | 1;
  page = index; pluginPage = false; el('tabs').hidden = false;
  tabs.forEach((_, i) => { const tab = el('tab-' + i); tab.setAttribute('aria-selected', String(i === page)); tab.tabIndex = i === page ? 0 : -1; });
  showPanel(el('panel-' + page), direction); selectTab(el('tab-' + page), !returning);
  saveNavigation();
  if (focus) el('tab-' + page).focus({ preventScroll: true });
}
function displayValues(keys: (keyof Settings)[] = fields.map(f => f.key)) {
  if (!settings) return;
  fields.filter(f => keys.includes(f.key)).forEach(f => { const control = el<HTMLInputElement>(f.key); control.removeAttribute('aria-invalid'); if (f.type === 'check') {
    control.checked = settings![f.key] as boolean;
    const checked = String(control.checked); if (control.getAttribute('aria-checked') !== checked) control.setAttribute('aria-checked', checked);
    control.dataset.state = switchIntent.has(f.key) ? 'pending' : control.checked ? 'on' : 'off';
  } else if (control.value !== String(settings![f.key])) control.value = String(settings![f.key]); });
}
async function write(patch: Partial<Settings>) {
  try { const r = await call('settings.write', { revision, patch }); settings = r.settings; revision = r.revision;
    if ((Object.keys(patch) as (keyof Settings)[]).some(key => patch[key] !== settings![key])) correction = '核心已校正输入，当前显示的是实际生效值';
  }
  catch (e) { try { const r = await call('settings.read'); settings = r.settings; revision = r.revision; } catch {} throw e; }
  finally { if (!disposed) displayValues(Object.keys(patch) as (keyof Settings)[]); }
}
function queueSwitch(key: keyof Settings) {
  if (!settings || working || !connected || disposed) return;
  // Native change has already toggled checked. Restore confirmed display in this task.
  // Repeated clicks toggle the latest intent, not the still-confirmed visual state.
  const value = !(switchIntent.get(key) ?? settings[key] as boolean);
  switchIntent.set(key, value); switchQueue.set(key, value); displayValues([key]);
  if (switchDrain) return;
  switchDrain = drainSwitches().finally(() => { switchDrain = undefined; });
}
async function drainSwitches() {
  el('error').hidden = true; correction = ''; el('status').textContent = '正在保存开关…';
  let failed = false;
  while (switchQueue.size && !disposed) {
    const [key, value] = switchQueue.entries().next().value!; switchQueue.delete(key);
    try { if (settings![key] !== value) await write({ [key]: value }); }
    catch (error) { failed = true; if (!disposed) { showError(error); el(key).setAttribute('aria-invalid', 'true'); } }
    if (disposed) break;
    // One in-flight request plus at most one latest request per field. No stale writes.
    if (!switchQueue.has(key)) { switchIntent.delete(key); displayValues([key]); }
  }
  if (!disposed) el('status').textContent = failed ? '开关未确认，已恢复核心返回值' : correction || '开关已保存';
}
function saveNavigation() {
  try { localStorage.setItem('settings-navigation', JSON.stringify({ page, scrolls: navigationSnapshot(), focus: document.activeElement?.id })); } catch { /* Optional UI history; settings remain in C++. */ }
}
function restoreNavigation() {
  try {
    const saved = JSON.parse(localStorage.getItem('settings-navigation') || 'null');
    if (saved && Number.isInteger(saved.page) && saved.page >= 0 && saved.page < tabs.length) {
      initialNavigation = { scrolls: saved.scrolls ?? {}, focus: saved.focus };
      navigate(saved.page); restoreNavigationScroll(initialNavigation.scrolls);
    }
  } catch { /* Ignore invalid local UI history. */ }
}
function build() {
  tabs.forEach((name, i) => {
    const b = make('button', name); b.id = 'tab-' + i; b.setAttribute('role', 'tab'); b.setAttribute('aria-controls', 'panel-' + i); b.onclick = () => navigate(i);
    b.onkeydown = e => { let next = i; if (e.key === 'ArrowRight') next = (i + 1) % tabs.length; else if (e.key === 'ArrowLeft') next = (i + tabs.length - 1) % tabs.length; else if (e.key === 'Home') next = 0; else if (e.key === 'End') next = tabs.length - 1; else return; e.preventDefault(); navigate(next, true); };
    el('tabs').append(b); const panel = make('section'); panel.id = 'panel-' + i; registerPanel(panel); panel.setAttribute('role', 'tabpanel'); panel.setAttribute('aria-labelledby', b.id); panel.append(make('h2', name));
  });
  fields.forEach(f => {
    const row = make('div', '', 'row'), labelArea = make('div'), label = make('label', f.label); label.htmlFor = f.key; labelArea.append(label);
    const hint = make('div', f.hint ?? (f.min !== undefined ? f.min + '–' + f.max : ''), 'hint'); hint.id = f.key + '-hint'; labelArea.append(hint);
    const line = make('div', '', 'input-line'); let input: HTMLInputElement | HTMLSelectElement;
    if (f.options) { input = make('select'); f.options.forEach((text, index) => { const o = make('option', text); o.value = String(index); input.append(o); }); }
    else { input = make('input'); input.type = f.type === 'check' ? 'checkbox' : f.type === 'text' ? 'text' : 'number'; if (f.min !== undefined) { input.min = String(f.min); input.max = String(f.max); input.step = f.key === 'fps' ? '1' : 'any'; } }
    input.id = f.key; input.dataset.core = ''; input.setAttribute('aria-describedby', hint.id + ' error'); line.append(input);
    if (f.type === 'check') input.setAttribute('role', 'switch');
    const commit = async () => {
      const value = f.type === 'check' ? (input as HTMLInputElement).checked : f.type === 'text' || f.key === 'monitorDevice' ? input.value : Number(input.value);
      displayValues([f.key]); // Only the submitted field is synchronized with the host.
      try { if (typeof value === 'number' && !Number.isFinite(value)) throw Error('请输入有效数值'); await write({ [f.key]: value }); input.removeAttribute('aria-invalid'); }
      catch (e) { input.setAttribute('aria-invalid', 'true'); throw e; }
    };
    if (f.type === 'check') input.onchange = () => queueSwitch(f.key);
    else if (f.options) input.onchange = () => void action(commit);
    else { button('apply-' + f.key, '应用', commit, line).setAttribute('aria-label', '应用' + f.label); input.onkeydown = e => { if (e.key === 'Enter') void action(commit); }; }
    row.append(labelArea, line); el('panel-' + f.page).append(row);
  });
  const musicInfo = make('pre'); musicInfo.id = 'media-info'; el('panel-1').append(musicInfo);
  const lyricActions = make('div', '', 'actions'); el('panel-1').append(lyricActions);
  button('lyrics-import', '载入当前歌曲歌词…', async () => { await call('lyrics.import', { key: media.key }); await readMedia(); }, lyricActions);
  button('lyrics-clear', '清除当前歌词', async () => { await call('lyrics.clear', { key: media.key }); await readMedia(); }, lyricActions);
  const diag = make('pre'); diag.id = 'diagnostics-info'; el('panel-3').append(make('p', '每 5 秒采样 CPU、内存与资源占用，记录动画、音乐、歌词与消息环节的耗时和异常。不记录聊天正文、歌曲名称、账号或凭据。'), diag);
  const diagActions = make('div', '', 'actions'); el('panel-3').append(diagActions);
  for (const [id, label] of [['start', '启动信息监测'], ['stop', '停止信息监测'], ['export', '导出日志报告']]) button('diagnostics-' + id, label, async () => {
    await call('diagnostics.action', { action: id }); for (let i = 0; i < 100; i++) { await new Promise(r => setTimeout(r, 200)); await readMedia(); if (!media.monitorPending) { if (media.monitorMessage.startsWith('操作失败')) throw Error(media.monitorMessage); return; } } throw Error('诊断操作仍未完成，请刷新状态');
  }, diagActions);
  const more = make('div', '', 'actions'); el('panel-4').append(more);
  button('layout-reset', '恢复默认布局', async () => { const r = await call('layout.reset', { revision }); settings = r.settings; revision = r.revision; displayValues(); }, more);
  button('arc-reset', '圆弧恢复默认', () => write({ topArcScale: 1 }), more);
  const reduced = make('p'); reduced.id = 'reduced-motion'; el('panel-4').append(reduced);
  const open = make('button', '插件管理'); open.id = 'plugins-open'; open.onclick = () => {
    savedFocus = document.activeElement as HTMLElement; savedScroll = el('content').scrollTop; pluginPage = true; el('tabs').hidden = true;
    showPanel(el('plugins'), 0); el('plugins-back').focus(); el('content').scrollTop=0; void action(readPlugins);
  }; el('panel-4').append(open);
  const plugins = make('section'); plugins.id = 'plugins'; registerPanel(plugins);
  plugins.append(make('h2', '插件模组管理'), make('p', '由原版 C++ 宿主管理。禁用和卸载保留模组包、缓存、设置及日志。'));
  const back = make('button', '返回设置'); back.id = 'plugins-back'; back.onclick = () => { navigate(page); el('content').scrollTop=savedScroll; savedFocus?.focus({ preventScroll: true }); }; plugins.append(back);
  const actions = make('div', '', 'actions'); plugins.append(actions);
  button('mods-import', '添加模组', async () => { await waitOperation(await call('mods.import')); }, actions);
  button('mods-folder', '打开模组目录', async () => { await call('mods.folder'); }, actions);
  button('mods-scan', '刷新插件列表', () => modAction('scan'), actions);
  button('mods-reload', '重新加载插件', () => modAction('reload'), actions);
  button('mods-disable', '禁用全部插件', () => modAction('disable'), actions);
  button('mods-unload', '卸载全部插件', () => modAction('unload'), actions);
  const market = make('button', '插件市场'); market.id = 'mods-market'; market.onclick = () => { el('status').textContent = '插件市场暂未开放，当前没有可连接的市场服务。'; }; actions.append(market);
  const filterLabel = make('label', '筛选模组 '); filterLabel.htmlFor = 'mods-filter'; const filter = make('select'); filter.id = 'mods-filter';
  for (const [value, text] of [['all', '全部'], ['enabled', '已启用'], ['disabled', '已禁用'], ['error', '错误']]) { const o = make('option', text); o.value = value; filter.append(o); }
  filter.onchange = () => { selectedFilter = filter.value; pluginSignature = ''; renderPlugins(); }; const filterRow = make('div', '', 'actions'); filterRow.append(filterLabel, filter); plugins.append(filterRow);
  const list = make('div'); list.id = 'mods-list'; plugins.append(list);
  // Restore history before navigate can overwrite it with the initial page.
  restoreNavigation(); navigate(page); controls();
}
async function confirm(message: string) {
  const d = el<HTMLDialogElement>('confirm'), focus = document.activeElement as HTMLElement; el('confirm-text').textContent = message;
  d.returnValue = ''; d.showModal(); return new Promise<boolean>(resolve => d.addEventListener('close', () => { focus?.focus(); resolve(d.returnValue === 'confirm'); }, { once: true }));
}
async function modAction(actionName: string, id = '') {
  let cascade = false;
  if (['disable', 'unload', 'reload'].includes(actionName)) {
    const affected = await call('mods.affected', { id });
    if (!id || affected.ids.length > 1) { cascade = await confirm('将影响以下模组：' + (affected.ids.join('、') || '无运行中的模组') + '。操作保留所有模组文件和用户数据。'); if (!cascade) throw Error('已取消操作，模组状态未改变'); }
  }
  await waitOperation(await call('mods.action', { action: actionName, id, cascade }), actionName, id);
}
async function waitOperation(result: { operation: number }, actionName = '', id = '') {
  const target = result.operation;
  for (let i = 0; i < 150; i++) {
    if(cancelled)throw Error('已取消等待；宿主已接受的操作仍会完成，请刷新确认');
    await readPlugins();
    if (modState!.completedOperation >= target && !modState!.busy) {
      if (modState!.completedOperation !== target) throw Error('宿主已执行其他操作，请核对最新列表');
      if (modState!.operationError) throw Error(modState!.operationError);
      if (id && ['enable', 'reload'].includes(actionName)) { const m = modState!.mods.find(m => m.id === id); if (!m?.enabled) throw Error(m?.error || '模组未进入启用状态'); }
      return;
    }
    await new Promise(r => setTimeout(r, 200));
  } throw Error('插件仍在处理，请稍后刷新，不能将等待超时视为操作成功');
}
async function readPlugins() { modState = await call('mods.list'); renderPlugins(); }
function renderPlugins() {
  if (!modState) return;
  const signature = JSON.stringify([modState.mods, modState.resources, selectedFilter]); if (signature === pluginSignature) return; pluginSignature = signature;
  const list = el('mods-list'); list.replaceChildren();
  const mods = modState.mods.filter(m => selectedFilter === 'all' || (selectedFilter === 'enabled' ? m.enabled : selectedFilter === 'disabled' ? !m.enabled : !!m.error));
  if (!mods.length) { list.append(make('p', modState.mods.length ? '此筛选下没有插件模组' : '暂未发现插件模组。请导入 .wimod 文件，或将模组放入 mods 文件夹。', 'empty')); return; }
  for (const m of mods) {
    const card = make('article', '', 'card'); card.dataset.modId = m.id;
    const title = make('h3', m.name); title.append(make('span', m.status, 'badge')); card.append(title, make('p', m.description));
    const details = make('dl'); for (const [key, value] of [['ID', m.id], ['作者', m.author], ['版本', m.version], ['API', m.apiVersion ? String(m.apiVersion) : '未通过清单校验'], ['文件', m.path], ['签名', m.signature], ['依赖', m.dependencies.join('、') || '无'], ['状态', (m.valid ? '有效' : '无效') + '；DLL 加载 ' + m.loads + ' / 释放 ' + m.unloads]]) details.append(make('dt', key), make('dd', value)); card.append(details);
    if (m.error) card.append(make('p', '错误：' + m.error));
    const actions = make('div', '', 'actions'); card.append(actions);
    for (const [name, text] of [['enable', '启用'], ['disable', '禁用'], ['unload', '卸载'], ['reload', '重载']]) {
      const b = button(m.id + '-' + name, text, () => modAction(name, m.id), actions); b.dataset.unavailable = String(name === 'enable' ? m.enabled || !m.valid : name === 'disable' ? !m.loaded && !m.enabled : name === 'reload' ? !m.valid : false);
    }
    button(m.id + '-log', '日志与错误详情', async () => { const r = await call('mods.log', { id: m.id }); let log = card.querySelector('pre'); if (!log) { log = make('pre'); card.append(log); } log.textContent = (m.error ? m.error + '\n\n' : '') + (r.text || '暂无日志内容（最多读取 256 KB）'); }, actions);
    for (const r of modState.resources.filter(r => r.owner === m.id)) {
      const row = make('div', '', 'row'), label = make('label', r.label || '插件设置'); const id = 'resource-' + r.handle; label.htmlFor = id; row.append(label);
      if (r.kind === 2) button(id, r.label || '执行插件按钮', async () => { await waitOperation(await call('mods.invoke', { handle: r.handle, value: '' })); }, row);
      else {
        const line = make('div', '', 'input-line'); let input: HTMLInputElement | HTMLSelectElement;
        if (r.type === 4) { input = make('select'); for (const c of r.choices) { const o = make('option', c); o.value = c; input.append(o); } }
        else { input = make('input'); input.type = r.type === 3 ? 'checkbox' : r.type === 2 ? 'number' : 'text'; if (r.type === 2) { input.min = String(r.minimum); input.max = String(r.maximum); input.step = 'any'; } }
        input.id = id; input.dataset.core = ''; input.value = r.value; if (r.type === 3) (input as HTMLInputElement).checked = r.value === '1'; line.append(input);
        const save = async () => { const value = r.type === 3 ? ((input as HTMLInputElement).checked ? '1' : '0') : input.value;
          if (r.type === 3) { (input as HTMLInputElement).checked = r.value === '1'; input.setAttribute('aria-checked', String(r.value === '1')); }
          try { await waitOperation(await call('mods.invoke', { handle: r.handle, value })); } finally { pluginSignature = ''; await readPlugins(); }
          const actual = modState?.resources.find(resource => resource.handle === r.handle)?.value;
          if (actual !== undefined && actual !== value) correction = '插件已校正输入，当前显示的是实际生效值';
        };
        button(id + '-save', '保存', save, line);
        if (r.type === 3) { input.setAttribute('role', 'switch'); input.setAttribute('aria-checked', String((input as HTMLInputElement).checked)); input.onchange = () => void action(save); }
        row.append(line);
      } card.append(row);
    } list.append(card);
  } controls();
}
async function readMedia() {
  media = await call('media.read');
  el('media-info').textContent = media.active ? media.title + ' · ' + media.artist + '\n来源：' + media.source + '\n' + media.lyricStatus + '\n' + media.lyric : '当前没有音乐会话';
  el('diagnostics-info').textContent = media.monitorMessage + (media.exportPath ? '\n导出位置：' + media.exportPath : '');
  for (const id of ['lyrics-import', 'lyrics-clear']) el(id).dataset.unavailable = String(!media.active);
  el('diagnostics-start').dataset.unavailable = String(media.monitorActive || media.monitorPending);
  el('diagnostics-stop').dataset.unavailable = String(!media.monitorActive || media.monitorPending); controls();
}
async function refresh() {
  try {
    const r = await call('settings.read'); settings = r.settings; revision = r.revision;
    capabilities = await call('capabilities.read'); connected = true; displayValues();
    const monitor = el<HTMLSelectElement>('monitorDevice'); monitor.replaceChildren(); const automatic = make('option', '自动（当前显示器，拖动切换）'); automatic.value = ''; monitor.append(automatic);
    for (const d of capabilities.displays) { const o = make('option', d.id + ' · ' + d.width + ' × ' + d.height); o.value = d.id; monitor.append(o); } monitor.value = settings.monitorDevice;
    el('connection').textContent = '已连接 ' + capabilities.version + ' · ' + capabilities.renderer + ' · ' + capabilities.dpi + ' DPI';
    el('reduced-motion').textContent = '降低动画：' + (capabilities.reducedMotion ? '已开启' : '未开启') + '。' + capabilities.reducedMotionReason;
    await readMedia(); if (pluginPage) await readPlugins();
    // Content height is final only after capabilities/monitor/reduced-motion text loads.
    if (initialNavigation) {
      restoreNavigationScroll(initialNavigation.scrolls);
      initialFocus = initialNavigation.focus;
      initialNavigation = undefined; saveNavigation();
    }
  } catch (e) { connected = false; el('connection').textContent = '核心连接不可用'; throw e; } finally { controls(); }
}
el('close').onclick = () => window.winIsland.close();
el('refresh').onclick = () => void action(refresh);
el('cancel').onclick = () => { cancelled=true; for (const id of outstanding) window.winIsland.cancel(id); };
window.addEventListener('beforeunload', () => { saveNavigation(); disposed = true; switchQueue.clear(); switchIntent.clear(); disposeMotion(); clearInterval(pollTimer); for (const id of outstanding) window.winIsland.cancel(id); });
window.addEventListener('keydown', e => { if (e.key === 'Escape' && !e.defaultPrevented && !document.querySelector('select:open') && !el<HTMLDialogElement>('confirm').open) { if (pluginPage) el('plugins-back').click(); else window.winIsland.close(); } });
build(); void action(refresh);
let polling = false;
// State polling only; no page transition timer or animation work.
const pollTimer = setInterval(async () => {
  if (working || switchDrain || polling || document.hidden) return; polling = true;
  try { await readMedia(); if (pluginPage) await readPlugins(); if (!connected) await refresh(); connected = true; }
  catch (e) { connected = false; el('connection').textContent = '核心连接已断开'; showError(e); } finally { polling = false; controls(); }
}, 2000);

