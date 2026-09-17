import type { ModInfo, ModList, Resource } from './protocol';
import { createDisclosure } from './motion.js';
const node = <K extends keyof HTMLElementTagNameMap>(tag: K, text = '', cls = '') => { const e = document.createElement(tag); e.textContent = text; e.className = cls; return e; };
const text = (e: HTMLElement, value: string) => { if (e.textContent !== value) e.textContent = value; };
type Actions = {
  run: (work: () => Promise<void>) => Promise<void>;
  operate: (action: string, id: string) => Promise<void>;
  logs: (id: string) => Promise<string>;
  save: (r: Resource, value: string) => Promise<Resource | undefined>;
  controls: () => void;
};
function coreButton(label: string, run: () => Promise<void>, api: Actions) {
  const b = node('button', label); b.dataset.core = ''; b.onclick = () => void api.run(run); return b;
}
class SettingRow {
  element = node('div', '', 'row mod-setting');
  input?: HTMLInputElement | HTMLSelectElement;
  saveButton: HTMLButtonElement;
  label = node('label'); note = node('span', '', 'hint setting-note');
  dirty = false; pending = false; available = true;
  constructor(public resource: Resource, private api: Actions) {
    const r = resource, line = node('div', '', 'input-line'), labelBox = node('div');
    this.element.dataset.settingKey = r.key;
    const id = 'setting-' + r.owner + '-' + r.kind + '-' + r.key;
    this.label.htmlFor = id; labelBox.append(this.label, this.note); this.element.append(labelBox, line);
    this.saveButton = coreButton(r.kind === 2 ? r.label : '保存', () => this.commit(), api);
    this.saveButton.id = id + '-save';
    if (r.kind !== 2) {
      if (r.type === 4) { const input = node('select'); for (const choice of r.choices) { const o = node('option', choice); o.value = choice; input.append(o); } this.input = input; }
      else { const input = node('input'); input.type = r.type === 3 ? 'checkbox' : r.type === 2 ? 'number' : 'text';
        if (r.type === 2) { input.min = String(r.minimum); input.max = String(r.maximum); input.step = 'any'; }
        if (r.type === 3) input.setAttribute('role', 'switch'); else input.maxLength = 4096;
        this.input = input;
      }
      this.input.id = id; this.input.dataset.core = ''; this.input.setAttribute('aria-describedby', id + '-note'); this.note.id = id + '-note';
      this.input.oninput = () => { if (r.type !== 3) { this.dirty = true; this.describe(); } };
      if (r.type === 3) this.input.onchange = () => { const value = (this.input as HTMLInputElement).checked ? '1' : '0'; this.display(); void api.run(() => this.commit(value)); };
      this.input.onkeydown = e => { if (e.key === 'Enter' && this.input?.tagName !== 'SELECT') { e.preventDefault(); void api.run(() => this.commit()); } };
      line.append(this.input);
    }
    line.append(this.saveButton); this.update(r, true);
  }
  describe(extra = '') {
    const r = this.resource, range = r.type === 2 ? `范围 ${r.minimum}–${r.maximum}。` : '';
    text(this.note, extra || (!this.available ? '资源暂不可用；保留尚未保存的输入，启用后可继续。' : this.pending ? '正在保存…' : this.dirty ? '尚未保存；切换详情区域会保留输入。' : range + '设置键：' + r.key));
  }
  display() {
    if (!this.input) return;
    if (this.resource.type === 3) { (this.input as HTMLInputElement).checked = this.resource.value === '1'; this.input.setAttribute('aria-checked', String(this.resource.value === '1')); }
    else this.input.value = this.resource.value;
  }
  update(r: Resource, available: boolean) {
    this.resource = r; this.available = available; text(this.label, r.label || r.key);
    if (!this.dirty && !this.pending) this.display();
    if (this.input) this.input.dataset.unavailable = String(!available || this.pending);
    this.saveButton.dataset.unavailable = String(!available || this.pending); this.describe();
  }
  async commit(explicit?: string) {
    if (!this.available || this.pending) throw Error('插件设置当前不可用，请等待操作结束或启用模组');
    const value = explicit ?? (this.resource.type === 3 ? ((this.input as HTMLInputElement).checked ? '1' : '0') : this.input?.value ?? '');
    if (this.input && !this.input.checkValidity()) { this.input.reportValidity(); throw Error('请输入符合类型和范围的设置值'); }
    this.pending = true; this.update(this.resource, this.available); this.api.controls();
    try {
      const actual = await this.api.save(this.resource, value);
      if (this.resource.kind !== 2 && !actual) throw Error('保存后设置资源不可用，请查看模组错误；输入已保留');
      if (actual) this.resource = actual;
      this.dirty = false; this.display(); this.input?.removeAttribute('aria-invalid');
      this.describe(actual && actual.value !== value ? '插件校正了输入，现显示实际保存值。' : '已保存到宿主；插件可通过正式设置接口读取。');
    } catch (e) { this.input?.setAttribute('aria-invalid', 'true'); if (this.resource.type === 3) this.display(); this.describe('保存失败，文本草稿保留；请查看上方错误。'); throw e; }
    finally { this.pending = false; if (this.input) this.input.dataset.unavailable = String(!this.available); this.saveButton.dataset.unavailable = String(!this.available); this.api.controls(); }
  }
}
class ModCard {
  element = node('article', '', 'mod-card'); summary = node('button', '', 'mod-summary');
  name = node('span', '', 'mod-name'); author = node('span', '', 'mod-author'); version = node('span', '', 'mod-version');
  clip = node('div', '', 'mod-disclosure'); body = node('div', '', 'mod-body');
  description = node('p'); details = node('dl', '', 'mod-metadata'); status = node('p', '', 'mod-status'); error = node('p', '', 'mod-error');
  settings = node('div', '', 'mod-settings'); settingsHint = node('p', '', 'hint'); log = node('pre', '点击“日志与错误详情”读取当前宿主日志。', 'mod-log');
  subpanel = node('div', '', 'mod-subpanel'); logButton: HTMLButtonElement; settingsButton = node('button', '模组设置');
  rows = new Map<string, SettingRow>(); fields = new Map<string, HTMLElement>(); buttons = new Map<string, HTMLButtonElement>();
  opened = false; selected: 'logs' | 'settings' | '' = ''; signature = '';
  disclosure: ReturnType<typeof createDisclosure>;
  constructor(public info: ModInfo, private api: Actions, private remember: (id: string, open: boolean) => void) {
    const id = 'mod-' + info.id;
    this.element.dataset.modId = info.id; this.summary.id = id + '-summary'; this.clip.id = id + '-details';
    this.summary.setAttribute('aria-expanded', 'false'); this.summary.setAttribute('aria-controls', this.clip.id);
    this.summary.append(this.name, this.author, this.version);
    this.summary.onclick = () => this.expand(!this.opened);
    this.element.onclick = e => { if (e.target === this.element) this.expand(!this.opened); };
    this.body.append(node('h3', '模组详情'), this.description, this.status, this.details, this.error);
    const actions = node('div', '', 'actions mod-operations'); this.body.append(actions);
    for (const [key, label] of [['enable', '启用'], ['disable', '禁用'], ['unload', '卸载'], ['reinstall', '重装']]) {
      const b = coreButton(label, () => api.operate(key, this.info.id), api); b.id = id + '-' + key; actions.append(b); this.buttons.set(key, b);
    }
    const entries = node('div', '', 'actions mod-detail-tabs');
    this.logButton = coreButton('日志与错误详情', async () => { this.area('logs'); const value = await api.logs(this.info.id); text(this.log, (this.info.error ? this.info.error + '\n\n' : '') + (value || '暂无日志内容（最多读取 256 KB）')); }, api);
    this.logButton.id = id + '-logs'; this.settingsButton.id = id + '-settings'; this.settingsButton.onclick = () => this.area('settings');
    this.logButton.setAttribute('aria-controls', id + '-log-panel'); this.settingsButton.setAttribute('aria-controls', id + '-settings-panel');
    this.log.id = id + '-log-panel'; this.settings.id = id + '-settings-panel'; this.settings.append(this.settingsHint);
    entries.append(this.logButton, this.settingsButton); this.subpanel.append(this.log, this.settings); this.body.append(entries, this.subpanel);
    this.clip.append(this.body); this.element.append(this.summary, this.clip); this.disclosure = createDisclosure(this.clip, this.body);
    this.area('');
  }
  expand(value: boolean, animate = true) { this.opened = value; this.summary.setAttribute('aria-expanded', String(value)); if (!value && this.clip.contains(document.activeElement)) this.summary.focus({ preventScroll: true }); this.disclosure.set(value, animate); this.remember(this.info.id, value); }
  area(value: 'logs' | 'settings' | '') {
    this.selected = value; this.log.hidden = value !== 'logs'; this.settings.hidden = value !== 'settings'; this.subpanel.hidden = !value;
    this.logButton.setAttribute('aria-pressed', String(value === 'logs')); this.settingsButton.setAttribute('aria-pressed', String(value === 'settings'));
  }
  field(label: string, value: string) { let e = this.fields.get(label); if (!e) { e = node('dd'); this.fields.set(label, e); this.details.append(node('dt', label), e); } text(e, value); }
  update(m: ModInfo, resources: Resource[], busy: boolean) {
    this.info = m;
    text(this.name, m.name || '未命名模组'); this.name.title = m.name || '未命名模组';
    text(this.author, !m.author || m.author === '作者并未填写' ? '作者未填写' : m.author); this.author.title = this.author.textContent!;
    text(this.version, !m.version || m.version === '版本号未填写' ? '版本未填写' : m.version); this.version.title = this.version.textContent!;
    this.summary.setAttribute('aria-label', `${m.name}，${this.author.textContent}，${this.version.textContent}`);
    text(this.description, m.description === '---' ? '暂无功能简介' : m.description || '暂无功能简介');
    text(this.status, `${m.status} · ${m.enabled ? '已启用' : '未启用'} · ${m.loaded ? 'DLL 已加载' : 'DLL 未加载'}`);
    this.status.dataset.error = String(!!m.error); text(this.error, m.error ? '错误摘要：' + m.error : ''); this.error.hidden = !m.error;
    this.field('完整名称', m.name); this.field('作者 / 版本', `${this.author.textContent} / ${this.version.textContent}`);
    this.field('模组 ID', m.id); this.field('清单 API 版本', m.apiVersion ? String(m.apiVersion) : '未提供或无法读取');
    this.field('生命周期 ABI', `宿主 0x${m.hostAbi.toString(16)}；DLL ${m.actualAbi ? '0x' + m.actualAbi.toString(16) : '尚未加载读取'}`);
    this.field('宿主版本要求', m.gameVersion || '未声明'); this.field('入口 DLL', m.entry || '未提供或无法读取');
    this.field('插件包文件', m.path); this.field('包 SHA-256', m.packageHash || '无法读取'); this.field('签名状态', m.signature || '未知（包无法读取）');
    this.field('依赖模组', m.dependencyDetails.map(d => `${d.name} [${d.id}]；要求 ${d.required || '*'}；已安装 ${d.version || '无'}；${d.reason}`).join('\n') || '无依赖');
    this.field('加载记录', `DLL 加载 ${m.loads} / 释放 ${m.unloads}；${m.valid ? '清单及依赖校验通过' : '清单或依赖校验未通过'}`);
    for (const [key, b] of this.buttons) b.dataset.unavailable = String(busy || (key === 'enable' ? m.enabled || !m.valid : key === 'disable' ? !m.enabled && !m.loaded : key === 'reinstall' ? !m.valid : m.status === '已卸载'));
    const available = new Set<string>();
    for (const r of resources) {
      const key = r.kind + ':' + r.key; available.add(key); let row = this.rows.get(key);
      if (!row) { row = new SettingRow(r, this.api); this.rows.set(key, row); this.settings.append(row.element); }
      row.update(r, m.enabled && !busy);
    }
    for (const [key, row] of this.rows) if (!available.has(key)) { row.update(row.resource, false); row.element.hidden = !row.dirty; }
    for (const key of available) this.rows.get(key)!.element.hidden = false;
    text(this.settingsHint, busy ? '宿主正在处理模组操作，设置资源暂不可用。' : !m.enabled ? '该模组未启用，设置资源暂不可用；启用后读取开发者注册的选项。' : !resources.length ? '该模组未提供可配置选项' : '选项来自插件 SDK 注册；文本和数值点击保存。未保存输入在卡片和区域切换时保留。');
  }
  dispose() { this.disclosure.dispose(); this.element.remove(); }
}
export class PluginList {
  private cards = new Map<string, ModCard>(); private state?: ModList; private query = ''; private filter = 'all'; private composing = false;
  private expanded = new Set<string>(); private empty = node('p', '', 'empty'); private count = node('span', '', 'hint');
  readonly search = node('input');
  constructor(private root: HTMLElement, private api: Actions) {
    try { this.expanded = new Set(JSON.parse(localStorage.getItem('mods-expanded') || '[]')); } catch { /* UI history is optional. */ }
    const toolbar = node('div', '', 'mod-search-row'), box = node('div', '', 'mod-search'), icon = node('span', '⌕', 'search-icon'), clear = node('button', '×', 'search-clear');
    icon.setAttribute('aria-hidden', 'true'); this.search.type = 'search'; this.search.id = 'mods-search'; this.search.placeholder = '搜索已添加的模组名称'; this.search.setAttribute('aria-label', '搜索模组名称');
    clear.setAttribute('aria-label', '清除模组搜索'); clear.id = 'mods-search-clear';
    const apply = () => { this.query = this.search.value.trim().toLocaleLowerCase(); this.filterCards(); };
    this.search.addEventListener('compositionstart', () => { this.composing = true; });
    this.search.addEventListener('compositionend', () => { this.composing = false; apply(); });
    this.search.oninput = () => { if (!this.composing) apply(); };
    this.search.onkeydown = e => { if (e.key === 'Escape' && this.search.value) { e.preventDefault(); e.stopPropagation(); this.search.value = ''; apply(); } };
    clear.onclick = () => { this.composing = false; this.search.value = ''; apply(); this.search.focus(); };
    this.count.setAttribute('role', 'status'); box.append(icon, this.search, clear); toolbar.append(box, this.count); root.before(toolbar); root.append(this.empty);
  }
  setFilter(filter: string) { this.filter = filter; this.filterCards(); }
  update(state: ModList) {
    this.state = state;
    const ids = new Set(state.mods.map(m => m.id));
    for (const [id, card] of this.cards) if (!ids.has(id)) { card.dispose(); this.cards.delete(id); }
    for (const m of state.mods) {
      let card = this.cards.get(m.id);
      if (!card) {
        card = new ModCard(m, this.api, (id, open) => { open ? this.expanded.add(id) : this.expanded.delete(id); try { localStorage.setItem('mods-expanded', JSON.stringify([...this.expanded])); } catch {} });
        this.cards.set(m.id, card); this.root.insertBefore(card.element, this.empty); card.update(m, state.resources.filter(r => r.owner === m.id), state.busy); card.expand(this.expanded.has(m.id), false);
      } else card.update(m, state.resources.filter(r => r.owner === m.id), state.busy);
    }
    this.filterCards(); this.api.controls();
  }
  private filterCards() {
    let count = 0;
    for (const card of this.cards.values()) {
      const m = card.info, show = (this.filter === 'all' || (this.filter === 'enabled' ? m.enabled : this.filter === 'disabled' ? !m.enabled : !!m.error)) && m.name.toLocaleLowerCase().includes(this.query);
      if (!show && card.element.contains(document.activeElement)) this.search.focus({ preventScroll: true });
      card.element.hidden = !show; card.element.inert = !show; if (show) count++;
    }
    this.empty.hidden = count > 0; text(this.empty, this.state?.mods.length ? '没有匹配的模组，请调整搜索或筛选条件。' : '暂未添加模组。点击“添加模组”导入 .wimod 文件。');
    text(this.count, `${count} / ${this.cards.size} 个模组`);
  }
  dispose() { for (const card of this.cards.values()) card.dispose(); this.cards.clear(); }
}
