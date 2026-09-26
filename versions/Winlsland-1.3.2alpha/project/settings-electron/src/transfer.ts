import type { Params, TransferState, TransferEntry } from './protocol';
import { transitionPlugins, isReducedMotion, onReducedMotionChange } from './motion.js';
const make = <K extends keyof HTMLElementTagNameMap>(tag: K, text = '', cls = '') => { const node = document.createElement(tag); node.textContent = text; node.className = cls; return node; };
const size = (bytes: number) => bytes >= 1048576 ? (bytes / 1048576).toFixed(1) + ' MB' : bytes >= 1024 ? (bytes / 1024).toFixed(1) + ' KB' : bytes + ' B';
const stateName = (e: TransferEntry) => e.state === 'ready' ? e.mode === 'saved' ? '已保存' : '仅中转' : ({ pending: '待选择', queued: '等待复制', copying: '复制中 ' + Math.round(e.progress * 100) + '%', failed: '复制失败', invalid: '文件失效', cancelled: '已取消' }[e.state] || e.state);
export class TransferPage {
  readonly root = make('section', '', 'transfer-page'); readonly entry = make('button', '文件中转', 'transfer-entry page-entry');
  private opened = false; private disposed = false; private state?: TransferState; private requestSeen = 0; private loading = false;
  private list = make('div', '', 'transfer-grid'); private search = make('input'); private enabled = make('input');
  private error = make('p', '', 'transfer-error'); private empty = make('p'); private mode = make('div', '', 'transfer-mode'); private sideAuto = make('input'); private saveMode = make('input'); private modeTitle = make('strong'); private modeHint = make('p', '', 'hint');
  private cards = new Map<string, TransferCard>(); private selectedId = ''; private operation = false;
  private previousFocus?: HTMLElement; private scroll = 0; private savedPanel?: HTMLElement; private pending = new Set<string>(); private poll: ReturnType<typeof setInterval>;
  private composing = false; private query = ''; private remember = false; private back = make('button', '返回设置');
  private context?: HTMLElement; private dragging = false;
  constructor(private call: (p: Params) => Promise<TransferState>, private panel: () => HTMLElement, private beforeReturn: () => Promise<void>) {
    this.root.id = 'transfer-page'; this.root.dataset.fullWindow = 'true'; this.entry.id = 'transfer-open'; this.entry.onclick = () => this.open();
    const header = make('div', '', 'transfer-header'); header.append(make('h2', '文件中转'), this.back); this.back.id = 'transfer-back'; this.back.onclick = () => void this.close(); this.root.append(header);
    const label = make('label', '', 'transfer-enable'); this.enabled.type = 'checkbox'; this.enabled.id = 'transfer-enabled'; this.enabled.setAttribute('role', 'switch'); label.append(make('span', '启用文件中转'), this.enabled); this.root.append(label, make('p', '启用后接收灵动岛拖入并显示中转组件；关闭后保留文件与记录。', 'hint'));
    this.enabled.onchange = () => { const desired = this.enabled.checked; this.enabled.checked = this.state?.enabled ?? false; void this.run(async () => { await this.call({ action: 'enable', enabled: desired }); }); };
    const sideLabel = make('label', '', 'transfer-enable'); this.sideAuto.type = 'checkbox'; this.sideAuto.id = 'transfer-side-auto'; this.sideAuto.setAttribute('role', 'switch'); sideLabel.append(make('span', '侧边吸附后自动展开'), this.sideAuto); this.root.append(sideLabel, make('p', '默认只吸附；打开后每次拖动完成并吸附左右侧时展开一次。', 'hint'));
    this.sideAuto.onchange = () => { const desired = this.sideAuto.checked; this.sideAuto.checked = this.state?.preferences.sideAutoExpand ?? false; void this.run(async () => { await this.call({ action: 'side-auto', enabled: desired }); }); };
    const toolbar = make('div', '', 'transfer-toolbar'); this.search.type = 'search'; this.search.placeholder = '搜索文件名或扩展名'; this.search.setAttribute('aria-label', '搜索中转文件');
    this.search.addEventListener('compositionstart', () => { this.composing = true; }); this.search.addEventListener('compositionend', () => { this.composing = false; this.query = this.search.value; this.filter(); }); this.search.oninput = () => { if (!this.composing) { this.query = this.search.value; this.filter(); } };
    const add = make('button', '添加文件'), refresh = make('button', '检查文件'), forget = make('button', '清除记住的导入模式'); add.onclick = () => void this.run(async () => { const result = await this.call({ action: 'pick' }); this.accept(result.ids || []); }); refresh.onclick = () => void this.run(async () => { await this.call({ action: 'refresh' }); }); forget.onclick = () => void this.run(async () => { await this.call({ action: 'forget' }); this.remember = false; this.mode.querySelector('[aria-pressed]')?.setAttribute('aria-pressed', 'false'); });
    toolbar.append(this.search, add, refresh, forget); this.root.append(toolbar, this.error, this.mode, this.empty, this.list); this.error.setAttribute('role', 'alert');
    const modeLabel = make('label', '', 'transfer-mode-choice'); this.saveMode.type = 'checkbox'; this.saveMode.id = 'transfer-save-mode'; this.saveMode.setAttribute('role', 'switch'); this.saveMode.setAttribute('aria-label', '保存并中转'); modeLabel.append(this.saveMode, this.modeTitle);
    const updateMode = () => { this.saveMode.setAttribute('aria-checked', String(this.saveMode.checked)); this.modeTitle.textContent = this.saveMode.checked ? '保存并中转' : '仅中转'; this.modeHint.textContent = this.saveMode.checked ? '复制到数据目录；不移动或覆盖原文件。确认后开始复制。' : '只记录原文件位置；不复制、不移动原文件。'; }; this.saveMode.onchange = updateMode; updateMode();
    const confirm = make('button', '确认导入'), remember = make('button', '记住此选择'); confirm.id = 'transfer-confirm-mode'; confirm.onclick = () => void this.choose(this.saveMode.checked ? 'saved' : 'reference'); remember.setAttribute('aria-pressed', 'false'); remember.onclick = () => { this.remember = !this.remember; remember.setAttribute('aria-pressed', String(this.remember)); };
    this.mode.append(make('h3', '选择导入方式'), modeLabel, this.modeHint, confirm, remember); this.mode.hidden = true;
    this.root.ondragover = e => { if (e.dataTransfer?.types.includes('Files')) { e.preventDefault(); e.dataTransfer.dropEffect = 'copy'; this.root.classList.add('drop-active'); } };
    this.root.ondragleave = e => { if (!this.root.contains(e.relatedTarget as Node)) this.root.classList.remove('drop-active'); };
    this.root.ondrop = e => { e.preventDefault(); this.root.classList.remove('drop-active'); const files = Array.from(e.dataTransfer?.files ?? []); if (!files.length) return; void this.run(async () => { const reply = await window.winIsland.files(files); if (!reply.ok) throw Error(reply.error); this.accept((reply as TransferState).ids || []); }); };
    this.poll = setInterval(() => { if (!document.hidden && !this.dragging) void this.refresh(); }, 750);
    this.root.addEventListener('pointerdown', e => { if (this.context && !this.context.contains(e.target as Node)) { this.context.remove(); this.context = undefined; } });
    window.addEventListener('keydown', this.key, true); void this.refresh();
  }
  private key = (event: KeyboardEvent) => { if (event.key !== 'Escape' || !this.opened) return; event.preventDefault(); event.stopImmediatePropagation(); if (this.context) { this.context.remove(); this.context = undefined; } else if (this.selectedId && this.cards.get(this.selectedId)?.opened) this.setExpanded(this.cards.get(this.selectedId)!, false); else void this.close(); };
  private accept(ids: string[]) { ids.forEach(id => this.pending.add(id)); }
  private async choose(mode: string) { await this.run(async () => { const ids = [...this.pending]; for (let i = 0; i < ids.length; i += 128) await this.call({ action: 'choose', ids: ids.slice(i, i + 128), mode, remember: this.remember }); this.pending.clear(); }); }
  private async run(fn: () => Promise<void>) { if (this.operation) return; this.operation = true; this.error.textContent = ''; try { await fn(); } catch (e) { this.error.textContent = String(e); } finally { this.operation = false; await this.refresh(); } }
  async refresh() {
    if (this.loading || this.disposed) return; this.loading = true;
    try {
      const state = await this.call({ action: 'list', interaction: this.opened && !document.hidden }); if (this.disposed) return;
      if (state.viewRequest > this.requestSeen) { this.requestSeen = state.viewRequest; if (state.viewRequest) { this.open(); if (state.selected) { this.selectedId = state.selected; const card = this.cards.get(state.selected); if (card) card.requested = true; } } }
      if (this.state?.revision !== state.revision) { this.state = state; this.enabled.checked = state.enabled; this.enabled.setAttribute('aria-checked', String(state.enabled)); this.sideAuto.checked = !!state.preferences.sideAutoExpand; this.sideAuto.setAttribute('aria-checked', String(this.sideAuto.checked)); if (state.error) this.error.textContent = state.error; this.render(); }
      if (this.selectedId && this.opened) { const card = this.cards.get(this.selectedId); if (card?.requested) { card.requested = false; this.setExpanded(card, true); } }
    } catch (e) { if (this.opened) this.error.textContent = String(e); } finally { this.loading = false; }
  }
  open() {
    if (this.opened) return; this.opened = true; const content = document.getElementById('content')!;
    if (!this.savedPanel) { this.savedPanel = this.panel(); this.previousFocus = document.activeElement as HTMLElement; this.scroll = content.scrollTop; }
    transitionPlugins(true, this.savedPanel, this.root, this.entry, () => this.back.focus({ preventScroll: true }));
    void this.refresh();
  }
  async close() {
    if (!this.opened || !this.savedPanel) return; this.opened = false; void this.call({ action: 'interaction', active: false }).catch(() => {});
    try { await this.beforeReturn(); } catch (e) { this.error.textContent = String(e); }
    if (this.opened || this.disposed) return;
    document.getElementById('content')!.scrollTop = this.scroll;
    transitionPlugins(false, this.savedPanel, this.root, this.entry, () => {
      document.getElementById('content')!.scrollTop = this.scroll; (this.previousFocus?.isConnected ? this.previousFocus : this.entry).focus({ preventScroll: true }); this.savedPanel = undefined;
    });
  }
  private filter() { const query = this.query.trim().toLocaleLowerCase(); let count = 0; for (const card of this.cards.values()) { card.node.hidden = !card.entry.name.toLocaleLowerCase().includes(query); if (!card.node.hidden) ++count; } this.empty.textContent = count ? '' : this.state?.entries.length ? '没有匹配的文件' : '将任意普通文件拖到此处，或点击添加文件。'; }
  private render() {
    if (!this.state) return;
    const ids = new Set(this.state.entries.map(e => e.id));
    for (const [id, card] of this.cards) if (!ids.has(id)) { card.reveal.dispose(); card.node.remove(); this.cards.delete(id); }
    for (const entry of this.state.entries) {
      if (entry.state === 'pending') this.pending.add(entry.id); else this.pending.delete(entry.id);
      let card = this.cards.get(entry.id);
      if (!card) {
        const node = make('article', '', 'transfer-card'), summary = make('button', '', 'transfer-summary'), clip = make('div', '', 'transfer-details'), body = make('div', '', 'transfer-details-body');
        node.dataset.id = entry.id; clip.id = 'transfer-detail-' + entry.id; summary.setAttribute('aria-controls', clip.id); summary.setAttribute('aria-expanded', 'false'); clip.append(body); node.append(summary, clip); this.list.append(node);
        const title = make('strong'), meta = make('span', '', 'hint'), imported = make('span', '', 'hint'), status = make('span', '', 'transfer-state'); summary.append(title, meta, imported, status);
        const full = make('p'), original = make('p'), saved = make('p'), error = make('p', '', 'transfer-error'), ops = make('div', '', 'actions'); body.append(full, original, saved, error, ops, make('p', '长按摘要拖出文件。移除记录保留原文件和保存副本。', 'hint'));
        const buttons = new Map<string, HTMLButtonElement>();
        const operation = (label: string, params: Params) => { const b = make('button', label); b.onclick = () => void this.run(async () => { b.disabled = true; try { await this.call(params); } finally { b.disabled = false; } }); ops.append(b); buttons.set(label, b); };
        operation('打开原位置', { action: 'location', id: entry.id, original: true }); operation('打开保存位置', { action: 'location', id: entry.id }); operation('重新定位', { action: 'relink', id: entry.id }); operation('从中转站移除', { action: 'remove', id: entry.id }); operation('取消复制', { action: 'cancel', id: entry.id }); operation('重试保存副本', { action: 'choose', ids: [entry.id], mode: 'saved' }); operation('改为仅中转', { action: 'choose', ids: [entry.id], mode: 'reference' });
        card = { node, summary, clip, body, entry, signature: '', opened: false, requested: entry.id === this.selectedId, reveal: disclosure(clip, body), title, meta, imported, status, full, original, saved, error, buttons };
        this.cards.set(entry.id, card); const item = card; let suppressClick = false;
        summary.onclick = () => { if (suppressClick) { suppressClick = false; return; } this.setExpanded(item, !item.opened); };
        node.oncontextmenu = e => { e.preventDefault(); this.showContext(item.entry, e.clientX, e.clientY); };
        let down: { at: number; x: number; y: number } | undefined;
        summary.onpointerdown = e => { suppressClick = false; if (e.button === 0) down = { at: performance.now(), x: e.clientX, y: e.clientY }; };
        summary.onpointermove = e => { if (down && !this.dragging && performance.now() - down.at >= 350 && Math.hypot(e.clientX - down.x, e.clientY - down.y) > 7) { down = undefined; suppressClick = true; this.dragging = true; void this.run(async () => { try { const reply = await window.winIsland.drag(entry.id); if (!reply.ok) throw Error(reply.error); } finally { this.dragging = false; } }); } };
        summary.onpointerup = summary.onpointercancel = () => { down = undefined; };
      }
      card.entry = entry; const signature = JSON.stringify(entry);
      if (signature !== card.signature) {
        card.signature = signature; card.summary.title = entry.name; card.title.textContent = entry.name; card.meta.textContent = (entry.extension || '无扩展名') + ' · ' + size(entry.size); card.imported.textContent = new Date(entry.imported).toLocaleString(); card.status.textContent = stateName(entry);
        card.full.textContent = '完整名称：' + entry.name; card.original.textContent = '原位置：' + entry.original; card.saved.textContent = '保存位置：' + (entry.saved || '未创建副本'); card.error.textContent = entry.error;
        card.buttons.get('打开保存位置')!.hidden = !entry.saved; card.buttons.get('取消复制')!.hidden = !['queued', 'copying'].includes(entry.state);
        for (const key of ['重试保存副本', '改为仅中转']) card.buttons.get(key)!.hidden = !['failed', 'cancelled', 'invalid'].includes(entry.state);
      }
    }
    this.mode.hidden = !this.pending.size; this.filter();
  }
  private setExpanded(card: TransferCard, open: boolean) { card.opened = open; this.selectedId = card.entry.id; card.summary.setAttribute('aria-expanded', String(open)); if (!open && card.clip.contains(document.activeElement)) card.summary.focus({ preventScroll: true }); card.reveal.set(open); }
  private showContext(entry: TransferEntry, x: number, y: number) {
    this.context?.remove(); const menu = make('div', '', 'transfer-context'); this.context = menu; menu.setAttribute('role', 'menu');
    const item = (label: string, action: string, original = false) => { const b = make('button', label); b.setAttribute('role', 'menuitem'); b.onclick = () => { menu.remove(); this.context = undefined; void this.run(async () => { await this.call({ action, id: entry.id, original }); }); }; menu.append(b); };
    item('从中转站移除', 'remove'); item('打开原位置', 'location', true); if (entry.saved) item('打开保存位置', 'location');
    menu.style.left = Math.max(16, Math.min(x, innerWidth - 230)) + 'px'; menu.style.top = Math.max(16, Math.min(y, innerHeight - 150)) + 'px'; this.root.append(menu); menu.querySelector('button')!.focus();
    menu.onkeydown = e => { const buttons = Array.from(menu.querySelectorAll('button')); const index = buttons.indexOf(document.activeElement as HTMLButtonElement); if (e.key === 'ArrowDown' || e.key === 'ArrowUp') { e.preventDefault(); buttons[(index + (e.key === 'ArrowDown' ? 1 : buttons.length - 1)) % buttons.length].focus(); } if (e.key === 'Escape') { e.preventDefault(); e.stopPropagation(); menu.remove(); this.context = undefined; } };
  }
  dispose() { this.disposed = true; clearInterval(this.poll); void this.call({ action: 'interaction', active: false }).catch(() => {}); window.removeEventListener('keydown', this.key, true); this.cards.forEach(card => card.reveal.dispose()); }
}

function disclosure(clip: HTMLElement, body: HTMLElement) {
  let open = false, animation: Animation | undefined, measured = 0, destroyed = false;
  const settle = () => { animation?.cancel(); animation = undefined; clip.style.height = open ? 'auto' : '0px'; };
  const retarget = () => {
    if (destroyed) return;
    const from = clip.getBoundingClientRect().height, to = open ? body.getBoundingClientRect().height : 0; measured = body.getBoundingClientRect().height;
    animation?.cancel(); animation = undefined; clip.inert = !open;
    if (isReducedMotion() || Math.abs(from - to) < .5) { settle(); return; }
    clip.style.height = from + 'px';
    const base = parseFloat(getComputedStyle(clip).getPropertyValue('--transfer-detail-duration')) || 650;
    // Previous full opening was 650 / 1.2 ms. Apply this release's 20% speedup once.
    const opening = parseFloat(getComputedStyle(clip).getPropertyValue('--transfer-detail-open-duration')) || 451.388889;
    const duration = open ? opening : base;
    const active = clip.animate([{ height: from + 'px' }, { height: to + 'px' }], { duration: duration * Math.min(1, Math.abs(to - from) / Math.max(1, measured)), easing: 'linear', fill: 'both' }); animation = active;
    void active.finished.then(() => { if (animation === active) settle(); }, () => {});
  };
  const observer = new ResizeObserver(() => { if (open && Math.abs(body.getBoundingClientRect().height - measured) > .5) retarget(); }); observer.observe(body);
  const unsubscribe = onReducedMotionChange(() => { if (isReducedMotion()) settle(); }); clip.inert = true; clip.style.height = '0px';
  return { set(value: boolean) { open = value; retarget(); }, dispose() { destroyed = true; animation?.cancel(); observer.disconnect(); unsubscribe(); } };
}
type TransferCard = { node: HTMLElement; summary: HTMLButtonElement; clip: HTMLElement; body: HTMLElement; entry: TransferEntry; signature: string; opened: boolean; requested: boolean; reveal: ReturnType<typeof disclosure>; title: HTMLElement; meta: HTMLElement; imported: HTMLElement; status: HTMLElement; full: HTMLElement; original: HTMLElement; saved: HTMLElement; error: HTMLElement; buttons: Map<string, HTMLButtonElement> };
