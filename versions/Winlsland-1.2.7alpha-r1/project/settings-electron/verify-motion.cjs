// Actual packaged Electron + unchanged native core, always isolated from user data.
const { _electron: electron, expect } = require('@playwright/test');
const fs = require('node:fs'), path = require('node:path'), assert = require('node:assert/strict');
const { spawn } = require('node:child_process');
const { hostCall } = require('./dist/transport');
const root = path.resolve('..'), out = path.join(root, 'verification/settings-animation/run-' + Date.now());
const live = path.join(out, 'core'); fs.mkdirSync(live, { recursive: true });
const pause = ms => new Promise(r => setTimeout(r, ms));
let core, credentials, app, page, cdp, frames = [], frameIndex = 0;
const rows = [], errors = [], metrics = {};
async function startCore() {
  for (const name of ['settings-connection.json', 'exit.request']) fs.rmSync(path.join(live, name), { force: true });
  core = spawn(path.join(root, 'release/WinIsland-1.2.7alpha.exe'), ['--verify', live], { windowsHide: true });
  for (let i = 0; i < 100 && !fs.existsSync(path.join(live, 'settings-connection.json')); i++) await pause(100);
  credentials = JSON.parse(fs.readFileSync(path.join(live, 'settings-connection.json')));
}
async function stopCore() { if (!core || core.exitCode !== null) return; const ended = new Promise(r => core.once('exit', r)); fs.writeFileSync(path.join(live, 'exit.request'), 'exit'); await ended; }
const host = (command, params = {}) => hostCall(credentials.pipe, credentials.token, command, params);
const idle = async () => { await expect(page.locator('#content')).toHaveAttribute('aria-busy', 'false'); await expect.poll(async () => page.locator('[data-state=pending]').count()).toBe(0); };
async function open(scale = 1, executable = path.join(root, 'release/settings/WinIslandSettings.exe')) {
  app = await electron.launch({ executablePath: executable, args: ['--pipe=' + credentials.pipe, '--token=' + credentials.token, '--session-dir=' + path.join(out, 'session-' + scale), '--force-device-scale-factor=' + scale], timeout: 30000 });
  page = await app.firstWindow(); page.on('pageerror', e => errors.push(String(e)));
  page.on('console', m => { if (m.type() === 'error') errors.push(m.text()); });
  await page.evaluate(() => window.addEventListener('unhandledrejection', e => console.error('UNHANDLED: ' + e.reason)));
  await expect(page.locator('#connection')).toContainText('已连接'); await idle();
  await page.locator('#tab-0').click(); await pause(300);
}
async function check(name, fn) {
  try { await fn(); rows.push({ name, passed: true }); console.log('PASS ' + name); }
  catch (e) { rows.push({ name, passed: false, error: String(e) }); throw e; }
}
async function tab(i) { await page.locator('#tab-' + i).click(); await pause(300); }
async function pick(file) { await app.evaluate(({ dialog }, file) => { dialog.showOpenDialog = async () => ({ canceled: false, filePaths: [file] }); }, file); }
async function getMetrics() { return Object.fromEntries((await cdp.send('Performance.getMetrics')).metrics.map(x => [x.name, x.value])); }
async function capture(name) { await page.screenshot({ path: path.join(out, name + '.png') }); }
async function record() {
  fs.mkdirSync(path.join(out, 'frames')); cdp = await page.context().newCDPSession(page);
  cdp.on('Page.screencastFrame', async e => { const file = String(frameIndex++).padStart(5, '0') + '.jpg'; fs.writeFileSync(path.join(out, 'frames', file), Buffer.from(e.data, 'base64')); frames.push({ file, time: e.metadata.timestamp }); await cdp.send('Page.screencastFrameAck', { sessionId: e.sessionId }).catch(() => {}); });
  await cdp.send('Performance.enable'); await cdp.send('Page.startScreencast', { format: 'jpeg', quality: 78, maxWidth: 1000, maxHeight: 800, everyNthFrame: 2 });
}
(async () => {
  await startCore();
  // Same real core, old packaged settings from verified backup: comparable idle/motion costs.
  const backup = fs.readFileSync(path.resolve('../../backups/settings-animation-current.txt'), 'utf8').trim();
  await open(1, path.join(backup, 'release/settings/WinIslandSettings.exe'));
  cdp = await page.context().newCDPSession(page); await cdp.send('Performance.enable');
  let first = await getMetrics(); await pause(1200); let last = await getMetrics();
  metrics.before = { idleTaskMs: (last.TaskDuration - first.TaskDuration) * 1000, heapBytes: last.JSHeapUsedSize, intervalMs: 1200 };
  await app.close(); await open(); await record();
  await check('packaged Electron safe boundaries / actual five source categories', async () => {
    const preferences = await app.evaluate(({ BrowserWindow }) => BrowserWindow.getAllWindows()[0].webContents.getLastWebPreferences());
    assert(preferences.contextIsolation && preferences.sandbox && !preferences.nodeIntegration);
    assert.equal(await page.evaluate(() => typeof window.require), 'undefined');
    assert.deepEqual(await page.locator('[role=tab]').allTextContents(), ['界面通知', '音乐歌词', '实时信息', '运行诊断', '其他设置']);
    assert(await page.evaluate(() => CSS.supports('appearance', 'base-select')));
  });
  await check('one sliding indicator, continuous interruption, immediate state, enter/exit cleanup', async () => {
    await pause(300); const before = await getMetrics();
    metrics.navigation = await page.evaluate(async () => {
      const tick = ms => new Promise(r => setTimeout(r, ms)), rect = () => document.querySelector('.nav-indicator').getBoundingClientRect().x;
      const begin = rect(); document.getElementById('tab-2').click();
      const immediate = document.getElementById('tab-2').getAttribute('aria-selected');
      await tick(65); const mid = rect(), incoming = getComputedStyle(document.getElementById('panel-2')).transform;
      document.getElementById('tab-4').click(); const retarget = rect();
      let maxGhosts = 0, maxPages = 0;
      for (const i of [1, 3, 0, 4, 2, 1, 1]) { document.getElementById('tab-' + i).click(); maxGhosts = Math.max(maxGhosts, document.querySelectorAll('.page-ghost').length); maxPages = Math.max(maxPages, document.querySelectorAll('main > section').length); await tick(24); }
      await tick(350);
      return { begin, mid, retarget, incoming, immediate, maxGhosts, maxPages, final: rect(), target: document.getElementById('tab-1').getBoundingClientRect().x, ghosts: document.querySelectorAll('.page-ghost').length, animations: document.getAnimations().length, indicatorCount: document.querySelectorAll('.nav-indicator').length };
    });
    const n = metrics.navigation; assert.equal(n.immediate, 'true'); assert(n.mid > n.begin + 2); assert(Math.abs(n.retarget - n.mid) < 3); assert.notEqual(n.incoming, 'none'); assert.equal(n.maxGhosts, 1); assert.equal(n.maxPages, 1); assert.equal(n.ghosts, 0); assert.equal(n.animations, 0); assert.equal(n.indicatorCount, 1); assert(Math.abs(n.final - n.target) < 1);
    const after = await getMetrics(); metrics.navigation.layoutCount = after.LayoutCount - before.LayoutCount;
    // Multiple intentional page mounts are expected; no continuous layout loop.
    const idleBefore = await getMetrics(); await pause(1200); const idleAfter = await getMetrics();
    metrics.after = { idleTaskMs: (idleAfter.TaskDuration - idleBefore.TaskDuration) * 1000, heapBytes: idleAfter.JSHeapUsedSize, idleLayouts: idleAfter.LayoutCount - idleBefore.LayoutCount, intervalMs: 1200 };
    assert(metrics.after.idleLayouts <= 2);
  });
  await check('draft, scroll, repeat selection and keyboard tabs preserve state without writes', async () => {
    const original = await host('settings.read'); await tab(4);
    await page.locator('#topArcScale').fill('1.23'); await page.locator('#plugins-open').scrollIntoViewIfNeeded();
    const scroll = await page.locator('#content').evaluate(e => e.scrollTop);
    await tab(0); await tab(4); await expect(page.locator('#topArcScale')).toHaveValue('1.23');
    assert(Math.abs((await page.locator('#content').evaluate(e => e.scrollTop)) - scroll) < 2);
    assert.equal((await host('settings.read')).revision, original.revision);
    await page.locator('#tab-4').focus(); await page.keyboard.press('Home'); await expect(page.locator('#tab-0')).toBeFocused(); await page.keyboard.press('ArrowRight'); await expect(page.locator('#tab-1')).toBeFocused();
    await page.keyboard.press('End'); await expect(page.locator('#tab-4')).toHaveAttribute('aria-selected', 'true'); await pause(320);
    assert.equal(await page.locator('#panel-0').count(), 0);
  });
  await check('all five switches: actual C++ value, checked/ARIA, mouse and Space', async () => {
    for (const [key, index] of [['resident', 0], ['hideNative', 0], ['showFps', 2], ['showPing', 2], ['topAttach', 4]]) {
      await tab(index); const old = (await host('settings.read')).settings[key]; const control = page.locator('#' + key);
      await control.click(); await idle(); assert.equal((await host('settings.read')).settings[key], !old); await expect(control).toHaveAttribute('aria-checked', String(!old));
      await control.focus(); await page.keyboard.press('Space'); await idle(); assert.equal((await host('settings.read')).settings[key], old); await expect(control).toHaveAttribute('aria-checked', String(old));
    }
  });
  await check('real revision conflict rolls switch back to confirmed core value and shows error', async () => {
    await tab(0); const s = await host('settings.read'); const changed = await host('settings.write', { revision: s.revision, patch: { seconds: s.settings.seconds === 7 ? 8 : 7 } }); assert(changed.ok);
    await page.locator('#resident').click(); await idle(); await expect(page.locator('#error')).toBeVisible();
    assert.equal((await host('settings.read')).settings.resident, s.settings.resident); await expect(page.locator('#resident')).toHaveAttribute('aria-checked', String(s.settings.resident));
    await capture('switch-real-conflict'); await page.locator('#refresh').click(); await idle();
  });
  await check('every actual select animates, keyboard commits actual option, Escape cancels', async () => {
    metrics.pickers = [];
    for (const [id, index] of [['songSource', 1], ['playerFilter', 1], ['lyricSource', 1], ['radiusMode', 4], ['monitorDevice', 4]]) {
      await tab(index); const select = page.locator('#' + id); await select.scrollIntoViewIfNeeded();
      await select.click(); await pause(45);
      const sample = await select.evaluate(e => ({ opacity: getComputedStyle(e, '::picker(select)').opacity, open: e.matches(':open'), transform: getComputedStyle(e, '::picker(select)').transform }));
      metrics.pickers.push({ id, ...sample }); assert(sample.open); assert(Number(sample.opacity) > 0 && Number(sample.opacity) < 1);
      await page.keyboard.press('Home'); await page.keyboard.press('ArrowDown'); await page.keyboard.press('Enter'); await idle();
      const value = await select.inputValue(); assert.equal(String((await host('settings.read')).settings[id]), value);
      await select.click(); await page.keyboard.press('ArrowDown'); await page.keyboard.press('Escape'); await pause(220); assert.equal(await select.inputValue(), value); assert(!await select.evaluate(e => e.matches(':open')));
    }
    await tab(1); await page.locator('#songSource').click(); await pause(220); await capture('dropdown-open');
    await page.locator('h1').click(); assert.equal(await page.locator('select:open').count(), 0);
    await page.locator('#songSource').click(); await page.keyboard.press('Tab'); assert.equal(await page.locator('select:open').count(), 0);
  });
  await check('rapid dropdown changes, page changes dismiss old picker and options', async () => {
    for (let i = 0; i < 6; i++) { await page.locator('#songSource').click(); await page.keyboard.press('Escape'); await page.locator('#playerFilter').click(); await page.keyboard.press('Escape'); }
    await page.locator('#lyricSource').click(); await page.evaluate(() => document.getElementById('tab-0').click()); await pause(350); assert.equal(await page.locator('select:open').count(), 0); assert.equal(await page.locator('#lyricSource').count(), 0);
    assert.equal(await page.locator('.page-ghost').count(), 0);
  });
  await check('numeric/text saving, invalid range, arc defaults remain functional', async () => {
    for (const [id, index, value] of [['seconds',0,'6.5'],['fps',0,'60'],['islandZoom',4,'1.4'],['widthRatio',4,'1.1'],['heightRatio',4,'1.1'],['dpiCorrection',4,'1.1'],['topArcScale',4,'1.2'],['lyricApi',1,'https://lrclib.net/api'],['pingTarget',2,'127.0.0.1']]) {
      await tab(index); await page.locator('#'+id).fill(value); await page.locator('#apply-'+id).click(); await idle(); assert.equal(String((await host('settings.read')).settings[id]), value);
    }
    await tab(4); await page.locator('#topArcScale').fill('4'); await page.locator('#apply-topArcScale').click(); await idle(); await expect(page.locator('#error')).toBeVisible(); await expect(page.locator('#topArcScale')).toHaveValue('1.2');
    await page.locator('#arc-reset').click(); await idle(); assert.equal((await host('settings.read')).settings.topArcScale, 1);
  });
  await check('plugin entry measures 2x / 1.5x, restores focus scroll and unchanged window bounds', async () => {
    await page.locator('#plugins-open').scrollIntoViewIfNeeded(); const b = await page.locator('#plugins-open').boundingBox(); metrics.pluginButton = b; assert(Math.abs(b.width - 164) < 1); assert(Math.abs(b.height - 54.585938) < 1);
    const scroll = await page.locator('#content').evaluate(e => e.scrollTop), bounds = await app.evaluate(({BrowserWindow}) => BrowserWindow.getAllWindows()[0].getBounds());
    await capture('other-settings'); await page.locator('#plugins-open').click(); await idle(); await expect(page.locator('#tabs')).toBeHidden(); assert.equal(await page.locator('main > section').count(), 1); assert.equal(await page.locator('#panel-4').count(), 0);
    await page.locator('#plugins-back').click(); await expect(page.locator('#plugins-open')).toBeFocused(); assert(Math.abs(await page.locator('#content').evaluate(e=>e.scrollTop)-scroll)<2); assert.deepEqual(await app.evaluate(({BrowserWindow})=>BrowserWindow.getAllWindows()[0].getBounds()),bounds);
    await page.locator('#plugins-open').click(); await idle();
  });
  await check('real plugin import, invalid file error, scan, enable/disable/reload/unload/log', async () => {
    await pick(path.join(root,'verification/ui-import.wimod')); await page.locator('#mods-import').click(); await idle(); await expect(page.locator('[data-mod-id=ui-import]')).toBeVisible();
    for (const action of ['enable','disable','reload','unload','enable']) { await page.locator('#ui-import-'+action).click(); await idle(); const m=(await host('mods.list')).mods.find(m=>m.id==='ui-import'); assert.equal(m.enabled,['enable','reload'].includes(action)); }
    await page.locator('#ui-import-log').click(); await idle(); await expect(page.locator('[data-mod-id=ui-import] pre')).not.toBeEmpty();
    await page.locator('#mods-scan').click(); await idle(); await expect(page.locator('#mods-import')).toBeVisible();
    const broken=path.join(out,'broken.wimod'); fs.writeFileSync(broken,'broken'); await pick(broken); await page.locator('#mods-import').click(); await idle(); await expect(page.locator('#error')).toBeVisible(); assert.equal((await host('mods.list')).mods.length,1);
  });
  await check('plugin boolean switch confirmed by host; filter animated and business values unchanged', async () => {
    const input=page.locator('[data-mod-id=ui-import] input[type=checkbox]'); const old=await input.isChecked(); await input.click(); await idle(); assert.equal(await input.isChecked(),!old); await expect(input).toHaveAttribute('aria-checked',String(!old));
    assert((await host('mods.list')).resources.some(r=>r.owner==='ui-import'&&r.type===3&&r.value===(!old?'1':'0')));
    const filter=page.locator('#mods-filter'); await filter.click(); await page.keyboard.press('End'); await page.keyboard.press('Enter'); assert.equal(await filter.inputValue(),'error'); await expect(page.locator('#mods-list')).toContainText('此筛选下没有');
    await filter.click(); await page.keyboard.press('Home'); await page.keyboard.press('Enter'); await expect(page.locator('[data-mod-id=ui-import]')).toBeVisible();
    await capture('plugins');
  });
  await check('dependency cancel/confirm, batch disable/unload, files preserved, market retained', async () => {
    await pick(path.join(root,'verification/ui-dependent.wimod')); await page.locator('#mods-import').click(); await idle(); await page.locator('#ui-dependent-enable').click(); await idle();
    await page.locator('#ui-import-disable').click(); await expect(page.locator('#confirm-text')).toContainText('ui-dependent'); await page.locator('#confirm button[value=cancel]').click(); await idle(); assert((await host('mods.list')).mods.every(m=>m.enabled));
    await page.locator('#ui-import-disable').click(); await page.locator('#confirm button[value=confirm]').click(); await idle(); assert((await host('mods.list')).mods.every(m=>!m.enabled));
    await page.locator('#ui-dependent-enable').click(); await idle();
    for (const id of ['mods-disable','mods-unload','mods-reload']) { await page.locator('#'+id).click(); await page.locator('#confirm button[value=confirm]').click(); await idle(); await expect(page.locator('#mods-import')).toBeVisible(); }
    assert(fs.existsSync(path.join(live,'mods/ui-import.wimod'))); assert(fs.existsSync(path.join(live,'mods/ui-dependent.wimod')));
    await page.locator('#mods-market').click(); await expect(page.locator('#status')).toContainText('暂未开放');
    await page.locator('#mods-folder').click(); await idle(); await expect(page.locator('#status')).toContainText('操作已完成');
  });
  await check('live diagnostics, synthetic music + real lyric parser/import/clear', async () => {
    await page.locator('#plugins-back').click(); await tab(3); for(const id of ['start','stop','export']){await page.locator('#diagnostics-'+id).click();await idle();} assert(fs.existsSync((await host('media.read')).exportPath));
    fs.writeFileSync(path.join(live,'command.txt'),'music=play'); await expect.poll(async()=>(await host('media.read')).active).toBe(true);
    const lrc=path.join(out,'验证 歌词.lrc'); fs.writeFileSync(lrc,'[00:00.00]animation regression lyric\n[00:15.00]second line'); await pick(lrc); await tab(1); await page.locator('#refresh').click(); await idle(); await page.locator('#lyrics-import').click(); await idle(); assert(fs.readdirSync(path.join(live,'lyrics')).some(n=>n.endsWith('.lrc'))); await page.locator('#lyrics-clear').click(); await idle();
  });
  await check('reduced motion while moving settles everything; instant picker and switches', async () => {
    await page.evaluate(()=>document.getElementById('tab-4').click()); await page.emulateMedia({reducedMotion:'reduce'}); await pause(50); assert.equal(await page.locator('.page-ghost').count(),0);
    for(const i of [0,4,1,2,1])await page.evaluate(i=>document.getElementById('tab-'+i).click(),i);
    assert.equal(await page.evaluate(()=>document.getAnimations().length),0);
    await page.locator('#songSource').click(); assert.equal(await page.locator('#songSource').evaluate(e=>getComputedStyle(e,'::picker(select)').opacity),'1'); await page.keyboard.press('Escape'); await capture('reduced-motion'); await page.emulateMedia({reducedMotion:'no-preference'});
  });
  await check('small window, dropdown edge, long list scroll and minimize/restore', async () => {
    await app.evaluate(({BrowserWindow})=>{const w=BrowserWindow.getAllWindows()[0];w.setSize(660,510);w.minimize();w.restore();}); await tab(4); await page.locator('#plugins-open').scrollIntoViewIfNeeded(); assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth));
    await page.locator('#plugins-open').click(); await idle(); const select=page.locator('#mods-filter'); await select.scrollIntoViewIfNeeded();
    await select.evaluate(e=>{for(let i=0;i<100;i++){const option=new Option('Long list fixture '+i+' 多语言项目测试','fixture-'+i);option.dataset.fixture='true';e.append(option);}});
    await select.click(); await pause(240); await page.keyboard.press('End'); await capture('narrow-long-picker');
    const box=await select.locator('option').last().boundingBox(); const viewport=await page.evaluate(()=>({w:innerWidth,h:innerHeight})); assert(box.x>=0&&box.x+box.width<=viewport.w+1&&box.y>=0&&box.y+box.height<=viewport.h+1);
    await page.mouse.wheel(0,-140); await page.keyboard.press('Escape'); await select.evaluate(e=>e.querySelectorAll('[data-fixture]').forEach(o=>o.remove()));
    await page.locator('#plugins-back').click(); await expect(page.locator('#plugins-open')).toBeFocused();
  });
  await cdp.send('Page.stopScreencast'); fs.writeFileSync(path.join(out,'frames.json'),JSON.stringify(frames)); await app.close();
  for(const scale of [1,1.25,1.5,2])await check('packaged Electron scale '+scale+' wrapped navigation/picker bounds',async()=>{
    await open(scale); await tab(4); assert.equal(await page.evaluate(()=>devicePixelRatio),scale); await page.locator('#monitorDevice').scrollIntoViewIfNeeded(); await page.locator('#monitorDevice').click(); await pause(240); await capture('dpi-'+scale); const rect=await page.locator('#monitorDevice option').first().boundingBox(); const v=await page.evaluate(()=>({w:innerWidth,h:innerHeight})); assert(rect.x>=0&&rect.x+rect.width<=v.w+1&&rect.y>=0&&rect.y+rect.height<=v.h+1); await page.keyboard.press('Escape'); await app.close();
  });
  await open();
  await check('reopen/core restart preserves all real settings and exposes disconnect honestly',async()=>{
    const expected=(await host('settings.read')).settings; await stopCore();
    await expect(page.locator('#connection')).toContainText('断开',{timeout:15000}); await expect(page.locator('#resident')).toBeDisabled(); await capture('disconnected');
    await app.close(); await startCore(); assert.deepEqual((await host('settings.read')).settings,expected); await open(); await expect(page.locator('#resident')).toHaveAttribute('aria-checked',String(expected.resident));
  });
  await check('close mid-transition, reopen cleanly; no JS exceptions or unhandled promises',async()=>{
    await page.evaluate(()=>document.getElementById('tab-4').click()); await app.close(); assert((await host('settings.read')).ok); await open(); await pause(700); assert.equal(await page.locator('.page-ghost').count(),0); assert.equal(await page.evaluate(()=>document.getAnimations().length),0); assert.deepEqual(errors,[]); await capture('final-notifications');
  });
})().catch(e=>{console.error(e);process.exitCode=1;}).finally(async()=>{
  await app?.close().catch(()=>{}); await stopCore().catch(()=>{});
  fs.writeFileSync(path.join(out,'results.json'),JSON.stringify({rows,metrics,errors},null,2)); fs.writeFileSync(path.join(root,'verification/settings-animation/latest-run.txt'),out);
  console.log('Evidence: '+out);
});
