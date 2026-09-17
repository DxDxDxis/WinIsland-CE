// Renderer-only motion. No host setting, IPC request or island animation is changed here.
const reduced = matchMedia('(prefers-reduced-motion: reduce)');
const tokens = getComputedStyle(document.documentElement);
const duration = (name: string) => reduced.matches ? 0 : parseFloat(tokens.getPropertyValue('--motion-' + name));
const easing = tokens.getPropertyValue('--motion-ease').trim();
const distance = tokens.getPropertyValue('--motion-distance').trim();
const pages = new Map<string, HTMLElement>();
const scrolls = new Map<HTMLElement, number>();
const focus = new Map<HTMLElement, HTMLElement>();
let current: HTMLElement | undefined, incoming: Animation | undefined, outgoing: Animation | undefined;
let ghost: HTMLElement | undefined, slide: Animation | undefined;
let selected: HTMLElement | undefined;
const host = document.getElementById('content')!;
const nav = document.getElementById('tabs')!;
const indicator = document.createElement('span');
indicator.className = 'nav-indicator'; indicator.setAttribute('aria-hidden', 'true'); nav.append(indicator);

export function registerPanel(panel: HTMLElement) { pages.set(panel.id, panel); }
export function findControl(id: string): HTMLElement | null {
  for (const panel of pages.values()) { if (panel.id === id) return panel; const control = panel.querySelector<HTMLElement>('#' + CSS.escape(id)); if (control) return control; }
  return null;
}
export function coreControls() {
  const result = new Set(Array.from(document.querySelectorAll<HTMLInputElement | HTMLButtonElement | HTMLSelectElement>('[data-core]')));
  for (const panel of pages.values()) panel.querySelectorAll<HTMLInputElement | HTMLButtonElement | HTMLSelectElement>('[data-core]').forEach(e => result.add(e));
  return result;
}
function settlePages() { incoming?.cancel(); outgoing?.cancel(); incoming = outgoing = undefined; ghost?.remove(); ghost = undefined; }

// Keep actual controls detached between visits: draft values and local handlers survive,
// while only one real page is mounted. The departing copy has no IDs, handlers or input.
export function showPanel(next: HTMLElement, direction: -1 | 0 | 1) {
  if (current === next) return;
  const previous = current, bounds = host.getBoundingClientRect();
  const oldBounds = previous?.getBoundingClientRect();
  const opacity = previous ? getComputedStyle(previous).opacity : '1';
  if (previous) {
    scrolls.set(previous, host.scrollTop);
    if (previous.contains(document.activeElement)) focus.set(previous, document.activeElement as HTMLElement);
  }
  const copy = previous && direction && duration('page') ? previous.cloneNode(true) as HTMLElement : undefined;
  settlePages();
  for (const panel of pages.values()) { panel.inert = panel !== next; panel.remove(); }
  next.hidden = false; next.inert = false; host.append(next); current = next;
  host.scrollTop = scrolls.get(next) ?? 0;
  if (!copy || !oldBounds) return;
  copy.removeAttribute('id'); copy.querySelectorAll('[id]').forEach(e => e.removeAttribute('id'));
  copy.removeAttribute('role'); copy.setAttribute('aria-hidden', 'true'); copy.inert = true;
  ghost = document.createElement('div'); ghost.className = 'page-ghost'; ghost.inert = true; ghost.setAttribute('aria-hidden', 'true');
  Object.assign(ghost.style, { left: bounds.left + 'px', top: bounds.top + 'px', width: host.clientWidth + 'px', height: host.clientHeight + 'px' });
  Object.assign(copy.style, { position: 'absolute', margin: '0', left: oldBounds.left - bounds.left + 'px', top: oldBounds.top - bounds.top + 'px', width: oldBounds.width + 'px' });
  ghost.append(copy); document.body.append(ghost);
  const options = { duration: duration('page'), easing };
  const offset = parseFloat(distance) * direction;
  outgoing = copy.animate([{ transform: 'translateX(0)', opacity }, { transform: `translateX(${-offset}px)`, opacity: 0 }], options);
  const animation = next.animate([{ transform: `translateX(${offset}px)`, opacity: 0 }, { transform: 'translateX(0)', opacity: 1 }], options);
  incoming = animation;
  void animation.finished.then(() => { if (incoming === animation) settlePages(); }, () => {});
}
export function restorePanelFocus(panel: HTMLElement) { const target = focus.get(panel); if (target?.isConnected) target.focus({ preventScroll: true }); }
export function selectTab(tab: HTMLElement, animate = true) {
  if (selected === tab && animate) return;
  selected = tab;
  if (nav.hidden) return;
  // All geometry is read once per retarget, never per animation frame.
  const parent = nav.getBoundingClientRect(), target = tab.getBoundingClientRect(), from = indicator.getBoundingClientRect();
  const x = target.left - parent.left, y = target.top - parent.top;
  const transform = `translate(${x}px,${y}px)`;
  const first = indicator.dataset.ready !== 'true'; slide?.cancel(); slide = undefined;
  Object.assign(indicator.style, { width: target.width + 'px', height: target.height + 'px', transform });
  indicator.dataset.ready = 'true';
  if (first || !animate || !duration('nav')) return;
  const animation = indicator.animate([
    { transform: `translate(${from.left - parent.left}px,${from.top - parent.top}px) scale(${from.width / target.width},${from.height / target.height})` }, { transform }
  ], { duration: duration('nav'), easing });
  slide = animation;
  void animation.finished.then(() => { if (slide === animation) slide = undefined; }, () => {});
}
function resetGeometry() { settlePages(); if (selected) selectTab(selected, false); }
export function navigationSnapshot() {
  if (current) scrolls.set(current, host.scrollTop);
  return Object.fromEntries(Array.from(scrolls, ([panel, scroll]) => [panel.id, scroll]));
}
export function restoreNavigationScroll(values: Record<string, number>) {
  for (const [id, scroll] of Object.entries(values)) { const panel = pages.get(id); if (panel && Number.isFinite(scroll) && scroll >= 0) scrolls.set(panel, scroll); }
  if (current) host.scrollTop = scrolls.get(current) ?? 0;
}
const observer = new ResizeObserver(resetGeometry); observer.observe(nav); observer.observe(host);
reduced.addEventListener('change', resetGeometry);
export function disposeMotion() {
  settlePages(); slide?.cancel(); observer.disconnect(); reduced.removeEventListener('change', resetGeometry);
  pages.clear(); scrolls.clear(); focus.clear();
}
