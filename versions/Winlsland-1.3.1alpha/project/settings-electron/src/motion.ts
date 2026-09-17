// Electron only; browser animations share document.timeline's monotonic clock.
// Core-confirmed manual preference. System media queries never write or override it.
let reducedMotion = false;
const motionListeners = new Set<() => void>();
export const isReducedMotion = () => reducedMotion;
export function onReducedMotionChange(listener: () => void) { motionListeners.add(listener); return () => { motionListeners.delete(listener); }; }
export function setReducedMotion(value: boolean) {
  if (reducedMotion === value) return;
  reducedMotion = value; document.documentElement.dataset.reducedMotion = String(value);
  resetGeometry(); motionListeners.forEach(listener => listener());
}
const tokens = getComputedStyle(document.documentElement);
export const motionDuration = (name: string) => reducedMotion ? 0 : parseFloat(tokens.getPropertyValue('--motion-' + name));
export const motionEase = tokens.getPropertyValue('--motion-ease').trim();
const pages = new Map<string, HTMLElement>(), scrolls = new Map<HTMLElement, number>();
const host = document.getElementById('content')!, nav = document.getElementById('tabs')!;
let current: HTMLElement | undefined, selected: HTMLElement | undefined;
const viewport = document.createElement('span'), indicator = document.createElement('span');
viewport.className = 'indicator-viewport'; viewport.setAttribute('aria-hidden', 'true');
indicator.className = 'nav-indicator'; viewport.append(indicator); nav.append(viewport);
export function registerPanel(panel: HTMLElement) { pages.set(panel.id, panel); }
export function findControl(id: string): HTMLElement | null {
  for (const panel of pages.values()) { if (panel.id === id) return panel; const e = panel.querySelector<HTMLElement>('#' + CSS.escape(id)); if (e) return e; } return null;
}
export function coreControls() {
  const controls = new Set(Array.from(document.querySelectorAll<HTMLInputElement | HTMLButtonElement | HTMLSelectElement>('[data-core]')));
  for (const panel of pages.values()) panel.querySelectorAll<HTMLInputElement | HTMLButtonElement | HTMLSelectElement>('[data-core]').forEach(e => controls.add(e));
  return controls;
}
export function showPanel(next: HTMLElement, _direction: -1 | 0 | 1 = 0) {
  if (current === next) return;
  if (current) scrolls.set(current, host.scrollTop);
  for (const panel of pages.values()) { panel.inert = panel !== next; panel.remove(); }
  next.hidden = false; next.inert = false; host.append(next); current = next;
  host.scrollTop = scrolls.get(next) ?? 0;
}
export function navigationSnapshot() {
  if (current) scrolls.set(current, host.scrollTop);
  return Object.fromEntries(Array.from(scrolls, ([panel, scroll]) => [panel.id, scroll]));
}
export function restoreNavigationScroll(values: Record<string, number>) {
  for (const [id, scroll] of Object.entries(values)) { const panel = pages.get(id); if (panel && Number.isFinite(scroll) && scroll >= 0) scrolls.set(panel, scroll); }
  if (current) host.scrollTop = scrolls.get(current) ?? 0;
}

type Spring = { p: number; v: number; goal: number };
const spring = (p = 0): Spring => ({ p, v: 0, goal: p });
const sx = spring(), sy = spring(), sw = spring(), sh = spring(), stretch = spring();
let frame = 0, lastTime = 0, navWidth = 0, ready = false;
// Exact damped-spring integration; refresh-rate independent, retaining velocity.
function step(s: Spring, dt: number, omega: number, damping: number) {
  const wd = omega * Math.sqrt(1 - damping * damping), a = s.p - s.goal;
  const b = (s.v + damping * omega * a) / wd, e = Math.exp(-damping * omega * dt), c = Math.cos(wd * dt), n = Math.sin(wd * dt);
  s.p = s.goal + e * (a * c + b * n);
  s.v = e * ((b * wd - damping * omega * a) * c - (a * wd + damping * omega * b) * n);
}
function paintIndicator() {
  const width = Math.max(1, sw.p + stretch.p), overflow = parseFloat(tokens.getPropertyValue('--nav-overflow'));
  const x = Math.max(-overflow, Math.min(navWidth + overflow - width, sx.p - width / 2));
  indicator.style.transform = `translate(${x + overflow}px,${sy.p + overflow}px) scale(${width},${Math.max(1, sh.p)})`;
  indicator.style.borderRadius = `${9 / width}px / ${9 / Math.max(1, sh.p)}px`;
  indicator.dataset.running = frame ? 'true' : 'false';
}
function navTick(time: number) {
  const dt = Math.max(0, (time - lastTime) / 1000); lastTime = time;
  const omega = 8 / (motionDuration('nav') / 1000);
  for (const s of [sx, sy, sw, sh]) step(s, dt, omega, .79);
  stretch.goal = Math.min(sw.goal * .16, Math.abs(sx.v) * .015); step(stretch, dt, omega, .9);
  const active = [sx, sy, sw, sh, stretch].some(s => Math.abs(s.p - s.goal) > .025 || Math.abs(s.v) > .5);
  if (active) frame = requestAnimationFrame(navTick);
  else { frame = 0; for (const s of [sx, sy, sw, sh]) { s.p = s.goal; s.v = 0; } stretch.p = stretch.goal = stretch.v = 0; indicator.style.willChange = ''; }
  paintIndicator();
}
export function selectTab(tab: HTMLElement, animate = true) {
  const same = selected === tab; selected = tab;
  if (nav.hidden || (same && animate && ready)) return;
  const parent = nav.getBoundingClientRect(), target = tab.getBoundingClientRect(); navWidth = parent.width;
  [sx.goal, sy.goal, sw.goal, sh.goal] = [target.left - parent.left + target.width / 2, target.top - parent.top, target.width, target.height];
  if (!ready || !animate || !motionDuration('nav')) {
    cancelAnimationFrame(frame); frame = 0;
    for (const s of [sx, sy, sw, sh]) { s.p = s.goal; s.v = 0; } stretch.p = stretch.v = stretch.goal = 0;
    ready = true; indicator.dataset.ready = 'true'; indicator.style.willChange = ''; paintIndicator(); return;
  }
  if (!frame) { lastTime = performance.now(); indicator.style.willChange = 'transform'; indicator.dataset.running = 'true'; frame = requestAnimationFrame(navTick); }
}

// Only card disclosure animates layout. Measure once per target change, then
// browser-interpolate this card's height. Its text is never scaled.
const disclosures = new Set<ReturnType<typeof createDisclosure>>();
export function createDisclosure(clip: HTMLElement, body: HTMLElement) {
  let open = false, height: Animation | undefined, fade: Animation | undefined, destroyed = false, measured = 0;
  function settle() { height?.cancel(); fade?.cancel(); height = fade = undefined; clip.style.height = open ? 'auto' : '0px'; body.style.opacity = open ? '1' : '0'; body.style.transform = ''; }
  function retarget(animate = true) {
    if (destroyed || !clip.isConnected) { settle(); return; }
    const from = clip.getBoundingClientRect().height, to = open ? body.getBoundingClientRect().height : 0;
    const opacity = getComputedStyle(body).opacity, transform = getComputedStyle(body).transform;
    measured = to; height?.cancel(); fade?.cancel(); height = fade = undefined;
    clip.style.height = to + 'px'; clip.inert = !open;
    if (!animate || !motionDuration('card') || Math.abs(from - to) < .5) { settle(); return; }
    const options = { duration: motionDuration('card'), easing: motionEase };
    const animation = clip.animate([{ height: from + 'px' }, { height: to + 'px' }], options); height = animation;
    body.style.opacity = open ? '1' : '0';
    fade = body.animate([{ opacity, transform: from < 1 ? 'translateY(-6px)' : transform }, { opacity: open ? 1 : 0, transform: open ? 'translateY(0)' : 'translateY(-6px)' }], options);
    void animation.finished.then(() => { if (height === animation) settle(); }, () => {});
  }
  const observer = new ResizeObserver(() => { if (open && clip.isConnected && Math.abs(body.getBoundingClientRect().height - measured) > .5) retarget(); }); observer.observe(body);
  const api = { set(value: boolean, animate = true) { open = value; clip.inert = !open; retarget(animate); }, settle, dispose() { destroyed = true; settle(); observer.disconnect(); disclosures.delete(api); } };
  disclosures.add(api); clip.inert = true; clip.style.height = '0px'; body.style.opacity = '0'; return api;
}

type Morph = { settings: HTMLElement; plugins: HTMLElement; entry: HTMLElement; opened: boolean; finish: () => void };
let morph: Morph | undefined, shell: HTMLElement | undefined, layer: HTMLElement | undefined;
let shellAnimation: Animation | undefined, contentAnimation: Animation | undefined, serial = 0, pluginScroll = 0;
function underneath(inert: boolean) {
  if (!morph?.plugins.dataset.fullWindow) return;
  for (const child of Array.from(document.body.children)) if (child instanceof HTMLElement && !child.classList.contains('plugin-overlay') && !child.classList.contains('plugin-morph')) child.inert = inert;
}
function contentBounds(full = morph?.plugins.dataset.fullWindow === 'true') {
  if (full) return new DOMRect(0, 0, document.documentElement.clientWidth, document.documentElement.clientHeight);
  const r = host.getBoundingClientRect();
  return new DOMRect(r.left, r.top, host.clientWidth - parseFloat(getComputedStyle(host).paddingRight), host.clientHeight);
}
function placeLayer(target: DOMRect) {
  if (layer) Object.assign(layer.style, { left: target.left + 'px', top: target.top + 'px', width: target.width + 'px', height: target.height + 'px' });
}
function cancelMorphAnimation() {
  ++serial; shellAnimation?.cancel(); contentAnimation?.cancel(); shellAnimation = contentAnimation = undefined;
  shell?.remove(); shell = undefined;
}
function clearMorph() {
  cancelMorphAnimation(); layer?.remove(); layer = undefined;
  if (morph) { morph.plugins.remove(); morph.plugins.inert = true; morph.settings.inert = false; }
  underneath(false); nav.inert = false; morph = undefined;
}
function completeMorph() {
  const state = morph; if (!state) return;
  underneath(state.opened); cancelMorphAnimation(); host.dataset.pageState = state.opened ? 'plugins' : 'settings';
  state.plugins.inert = !state.opened; state.settings.inert = state.opened; nav.inert = state.opened;
  if (state.opened && layer) { layer.inert = false; layer.classList.remove('is-transitioning'); layer.style.opacity = '1'; layer.style.clipPath = ''; }
  else { pluginScroll = layer?.scrollTop ?? pluginScroll; layer?.remove(); layer = undefined; state.plugins.remove(); morph = undefined; }
  state.finish();
}
export function transitionPlugins(opened: boolean, settings: HTMLElement, plugins: HTMLElement, entry: HTMLElement, finish: () => void, resize = false) {
  if (!resize && morph?.opened === opened && shellAnimation) return;
  // Read the composited geometry before cancelling. Reversal continues from the
  // visible position; settings stay mounted and continue receiving core values.
  const previousRect = shell?.getBoundingClientRect();
  const previousCorner = shell && previousRect ? parseFloat(getComputedStyle(shell).borderTopLeftRadius) * previousRect.width / parseFloat(shell.style.width) : undefined;
  const oldOpacity = layer ? getComputedStyle(layer).opacity : '0';
  const target = contentBounds(plugins.dataset.fullWindow === 'true'), anchor = entry.getBoundingClientRect();
  const from = previousRect ?? (opened ? anchor : target), to = opened ? target : anchor;
  const expandedRadius = plugins.dataset.fullWindow === 'true' ? 0 : 12;
  const fromRadius = previousCorner ?? (opened ? 8 : expandedRadius), toRadius = opened ? expandedRadius : 8;
  cancelMorphAnimation();
  if (!layer) {
    layer = document.createElement('div'); layer.className = 'plugin-overlay';
    layer.setAttribute('role', 'region'); layer.setAttribute('aria-label', plugins.id === 'transfer-page' ? '文件中转' : '插件模组管理');
    plugins.hidden = false; layer.append(plugins); document.body.append(layer); placeLayer(target); layer.scrollTop = pluginScroll;
    layer.dataset.fullWindow = plugins.dataset.fullWindow ?? 'false';
  } else placeLayer(target);
  morph = { opened, settings, plugins, entry, finish }; underneath(true);
  nav.inert = settings.inert = plugins.inert = layer.inert = true;
  host.dataset.pageState = opened ? 'opening' : 'closing'; layer.classList.add('is-transitioning');
  if (!motionDuration('expand')) { completeMorph(); return; }
  shell = document.createElement('div'); shell.className = 'plugin-morph'; shell.inert = true; shell.setAttribute('aria-hidden', 'true'); document.body.append(shell);
  Object.assign(shell.style, { left: target.left + 'px', top: target.top + 'px', width: target.width + 'px', height: target.height + 'px' });
  const transform = (r: DOMRect) => `translate(${r.left - target.left}px,${r.top - target.top}px) scale(${Math.max(1, r.width) / target.width},${Math.max(1, r.height) / target.height})`;
  const options: KeyframeAnimationOptions = { duration: motionDuration('expand'), easing: motionEase, fill: 'both' };
  const radius = (r: DOMRect, value: number) => `${value * target.width / Math.max(1, r.width)}px / ${value * target.height / Math.max(1, r.height)}px`;
  shellAnimation = shell.animate([{ transform: transform(from), borderRadius: radius(from, fromRadius) }, { transform: transform(to), borderRadius: radius(to, toRadius) }], options);
  const clip = (r: DOMRect, corner: number) => `inset(${Math.max(0, r.top - target.top)}px ${Math.max(0, target.right - r.right)}px ${Math.max(0, target.bottom - r.bottom)}px ${Math.max(0, r.left - target.left)}px round ${corner}px)`;
  contentAnimation = layer.animate([
    { opacity: oldOpacity, clipPath: clip(from, fromRadius) },
    { opacity: opened ? (previousRect ? oldOpacity : 0) : 0, offset: opened ? .35 : .4 },
    { opacity: opened ? 1 : 0, clipPath: clip(to, toRadius) }
  ], options);
  const generation = serial;
  void shellAnimation.finished.then(() => { if (generation === serial) completeMorph(); }, () => {});
}
function resetGeometry() {
  if (selected && !nav.hidden) selectTab(selected, false);
  if (morph && shell) { const m = morph; transitionPlugins(m.opened, m.settings, m.plugins, m.entry, m.finish, true); }
  else if (layer) placeLayer(contentBounds());
  if (reducedMotion) { if (shell) completeMorph(); disclosures.forEach(d => d.settle()); }
}
const blockUnderlyingScroll = (event: Event) => { if (layer) event.preventDefault(); };
host.addEventListener('wheel', blockUnderlyingScroll, { passive: false });
host.addEventListener('touchmove', blockUnderlyingScroll, { passive: false });
let observedWidth = 0, observedHeight = 0;
const observer = new ResizeObserver(() => { const w = host.clientWidth, h = host.clientHeight; if (w !== observedWidth || h !== observedHeight) { observedWidth = w; observedHeight = h; resetGeometry(); } });
observer.observe(host); observer.observe(nav); window.addEventListener('resize', resetGeometry);
export function disposeMotion() { clearMorph(); host.removeEventListener('wheel', blockUnderlyingScroll); host.removeEventListener('touchmove', blockUnderlyingScroll); cancelAnimationFrame(frame); observer.disconnect(); window.removeEventListener('resize', resetGeometry); motionListeners.clear(); disclosures.forEach(d => d.dispose()); pages.clear(); scrolls.clear(); }
