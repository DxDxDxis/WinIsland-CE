const {_electron:electron,expect}=require('@playwright/test');
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const {hostCall}=require('./dist/transport');
const root=path.resolve('..'),live=path.join(root,'verification/electron-live'),out=path.join(root,'verification/ui');fs.mkdirSync(out,{recursive:true});
const credentials=JSON.parse(fs.readFileSync(path.join(live,'settings-connection.json'),'utf8'));
const results=[];let app,page;
const pause=ms=>new Promise(r=>setTimeout(r,ms));
async function check(name,fn){try{await fn();results.push({name,passed:true});console.log('PASS '+name);}catch(e){results.push({name,passed:false,error:String(e)});throw e;}}
async function state(){const s=await hostCall(credentials.pipe,credentials.token,'settings.read',{});assert(s.ok,JSON.stringify(s));return s;}
async function idle(){await expect(page.locator('#content')).toHaveAttribute('aria-busy','false');}
async function open(){app=await electron.launch({executablePath:path.join(root,'release/settings/WinIslandSettings.exe'),args:['--pipe='+credentials.pipe,'--token='+credentials.token,'--core-pid='+fs.readFileSync(path.join(live,'ready.txt'),'utf8'),'--session-dir='+path.join(out,'session')],timeout:30000});page=await app.firstWindow();await expect(page.locator('#connection')).toContainText('已连接');await idle();}
(async()=>{
 await open();
 await check('secure real Electron window',async()=>{const prefs=await app.evaluate(({BrowserWindow})=>BrowserWindow.getAllWindows()[0].webContents.getLastWebPreferences());assert(prefs.contextIsolation);assert(!prefs.nodeIntegration);assert(prefs.sandbox);assert.equal(await page.evaluate(()=>typeof window.require),'undefined');});
 await check('restart preserved settings and plugin enable state',async()=>{assert.deepEqual((await state()).settings,JSON.parse(fs.readFileSync(path.join(live,'expected-restart.json'))));const m=await hostCall(credentials.pipe,credentials.token,'mods.list',{});assert(m.mods.some(x=>x.id==='scene-fixture'&&x.enabled));});
 await check('five pages, rapid switching, keyboard navigation, zero motion',async()=>{
  for(const i of [4,2,0,3,1,4,0,0]){await page.locator('#tab-'+i).click();await expect(page.locator('#panel-'+i)).toBeVisible();assert.equal(await page.locator('section[role=tabpanel]:visible').count(),1);}
  await page.locator('#tab-0').focus();await page.keyboard.press('ArrowRight');await expect(page.locator('#tab-1')).toHaveAttribute('aria-selected','true');
  const moving=await page.evaluate(()=>[...document.querySelectorAll('*')].filter(e=>{const s=getComputedStyle(e);return s.animationName!=='none'||s.transitionDuration.split(',').some(v=>parseFloat(v)>0)}).length);assert.equal(moving,0);
 });
 await check('all checkboxes commit to C++',async()=>{for(const [key,tab] of [['resident',0],['hideNative',0],['showFps',2],['showPing',2],['topAttach',4]]){await page.locator('#tab-'+tab).click();const old=(await state()).settings[key];await page.locator('#'+key).click();await expect.poll(async()=>(await state()).settings[key]).toBe(!old);await idle();await page.locator('#'+key).click();await expect.poll(async()=>(await state()).settings[key]).toBe(old);await idle();}});
 await check('all numeric controls submit, invalid value rolls back',async()=>{for(const [key,tab,value] of [['seconds',0,'6.5'],['fps',0,'60'],['islandZoom',4,'1.4'],['widthRatio',4,'1.1'],['heightRatio',4,'1.1'],['dpiCorrection',4,'1.1'],['topArcScale',4,'1.2']]){await page.locator('#tab-'+tab).click();await page.locator('#'+key).fill(value);await page.locator('#apply-'+key).click();await expect.poll(async()=>(await state()).settings[key]).toBe(Number(value));await idle();}
  await page.locator('#topArcScale').fill('4');await page.locator('#apply-topArcScale').click();await expect(page.locator('#error')).toBeVisible();await expect(page.locator('#topArcScale')).toHaveValue('1.2');await idle();
 });
 await check('every select and text setting reaches host',async()=>{
  for(const [key,tab,value] of [['songSource',1,'1'],['lyricSource',1,'2'],['playerFilter',1,'1'],['radiusMode',4,'2']]){await page.locator('#tab-'+tab).click();await page.locator('#'+key).selectOption(value);await expect.poll(async()=>String((await state()).settings[key])).toBe(value);await idle();}
  await page.locator('#monitorDevice').selectOption({index:1});await idle();assert((await state()).settings.monitorDevice);
  for(const [key,tab,value] of [['lyricApi',1,'https://lrclib.net/api'],['pingTarget',2,'127.0.0.1']]){await page.locator('#tab-'+tab).click();await page.locator('#'+key).fill(value);await page.locator('#apply-'+key).click();await expect.poll(async()=>(await state()).settings[key]).toBe(value);await idle();}
 });
 await check('layout reset and screenshot',async()=>{await page.locator('#tab-4').click();await page.locator('#layout-reset').click();await idle();assert.equal((await state()).settings.islandZoom,1.5);await page.screenshot({path:path.join(out,'settings-layout.png'),fullPage:true});});
 await check('plugin page uses real list and lifecycle',async()=>{
  await page.locator('#plugins-open').click();await idle();await expect(page.locator('#plugins')).toBeVisible();await expect(page.locator('#tabs')).toBeHidden();await expect(page.locator('[data-mod-id=scene-fixture]')).toBeVisible();
  await page.locator('#scene-fixture-disable').click();await idle();await expect(page.locator('[data-mod-id=scene-fixture] .badge')).toContainText('禁用');
  await page.locator('#scene-fixture-enable').click();await idle();await expect(page.locator('[data-mod-id=scene-fixture] .badge')).toContainText('启用');
  await page.locator('#scene-fixture-reload').click();await idle();await page.locator('#scene-fixture-unload').click();await idle();await expect(page.locator('[data-mod-id=scene-fixture] .badge')).toContainText('卸载');
  await page.locator('#scene-fixture-enable').click();await idle();await page.locator('#scene-fixture-log').click();await idle();await expect(page.locator('[data-mod-id=scene-fixture] pre')).not.toBeEmpty();
  await page.locator('#mods-scan').dblclick();await idle();await page.screenshot({path:path.join(out,'plugin-manager.png'),fullPage:true});
  await page.locator('#plugins-back').click();await expect(page.locator('#panel-4')).toBeVisible();await expect(page.locator('#plugins-open')).toBeFocused();await expect(page.locator('#plugins')).toBeHidden();
 });
 await check('window close leaves core alive; reopen rereads values',async()=>{await app.close();assert((await state()).ok);await open();await page.locator('#tab-1').click();await expect(page.locator('#songSource')).toHaveValue('1');});
 await check('resize, minimize/restore and screenshots',async()=>{await app.evaluate(({BrowserWindow})=>{const w=BrowserWindow.getAllWindows()[0];w.setSize(660,510);w.minimize();w.restore();});await expect(page.locator('#tabs')).toBeVisible();await page.screenshot({path:path.join(out,'small-window.png')});});
 await check('core exit produces real visible error, no false success',async()=>{fs.writeFileSync(path.join(live,'exit.request'),'exit');await expect(page.locator('#connection')).toContainText('断开',{timeout:15000});await expect(page.locator('#songSource')).toBeDisabled();await page.screenshot({path:path.join(out,'core-exit.png')});});
})().catch(e=>{console.error(e);process.exitCode=1;}).finally(async()=>{if(app)await app.close().catch(()=>{});fs.writeFileSync(path.join(out,'results.json'),JSON.stringify(results,null,2));});
