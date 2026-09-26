const { _electron: electron, expect } = require('@playwright/test');
const fs = require('node:fs'), path = require('node:path'), assert = require('node:assert/strict');
const { spawn } = require('node:child_process');
const { hostCall } = require('./dist/transport');
const root = path.resolve(__dirname, '..'), out = path.join(root, 'verification', 'ui-' + Date.now()), live = path.join(out, 'core');
fs.mkdirSync(live, { recursive: true });
const rows = [], errors = [], metrics = {}, frames = []; let core, credentials, app, page, cdp, recording = false;
const pause = ms => new Promise(r => setTimeout(r, ms));
const host = (cmd, params = {}) => hostCall(credentials.pipe, credentials.token, cmd, params);
async function startCore() {
  for (const f of ['settings-connection.json','exit.request']) fs.rmSync(path.join(live, f), { force: true });
  core = spawn(path.join(root,'release/WinIsland-1.3.0.exe'), ['--verify',live], { windowsHide: true });
  for(let i=0;i<150&&!fs.existsSync(path.join(live,'settings-connection.json'));i++)await pause(100);
  credentials = JSON.parse(fs.readFileSync(path.join(live,'settings-connection.json')));
}
async function stopCore() { if(!core || core.exitCode!==null)return;fs.writeFileSync(path.join(live,'exit.request'),'exit');await new Promise((resolve,reject)=>{core.once('exit',resolve);setTimeout(()=>reject(Error('test core exit timeout')),12000).unref();}); }
async function open(scale=1, baseline=false) {
  app=await electron.launch({executablePath:path.join(root,baseline?'settings/WinIslandSettings.exe':'release/settings/WinIslandSettings.exe'),args:['--pipe='+credentials.pipe,'--token='+credentials.token,'--session-dir='+path.join(out,baseline?'before':'session-'+scale),'--force-device-scale-factor='+scale],timeout:30000});
  page=await app.firstWindow();page.on('pageerror',e=>errors.push(String(e)));page.on('console',e=>{if(e.type()==='error')errors.push(e.text());});
  await page.evaluate(()=>window.addEventListener('unhandledrejection',e=>console.error('unhandled: '+e.reason)));
  await expect(page.locator('#connection')).toContainText('已连接');await idle();
}
async function idle(){await expect(page.locator('#content')).toHaveAttribute('aria-busy','false',{timeout:35000});}
async function settled(){await expect(page.locator('.plugin-morph')).toHaveCount(0,{timeout:7000});await expect(page.locator('#content')).not.toHaveAttribute('data-page-state',/opening|closing/);}
async function tab(i){await page.locator('#tab-'+i).click();await expect(page.locator('.nav-indicator')).toHaveAttribute('data-running','false');}
async function enter(){await tab(4);await page.locator('#plugins-open').click();await settled();await idle();}
const card=id=>page.locator('[data-mod-id="'+id+'"]');
async function expand(id){const s=card(id).locator('.mod-summary');if(await s.getAttribute('aria-expanded')!=='true')await s.click();await pause(540);}
async function pick(name){await app.evaluate(({dialog},file)=>{dialog.showOpenDialog=async()=>({canceled:false,filePaths:[file]});},path.join(root,'verification/fixtures',name+'.wimod'));}
async function importMod(id){await pick(id);await page.locator('#mods-import').click();await idle();await expect(card(id)).toBeVisible();}
async function operation(action,id=''){const r=await host('mods.action',{action,id,cascade:true});assert(r.ok,r.error);for(let i=0;i<200;i++){const state=await host('mods.list');if(!state.busy&&state.completedOperation>=r.operation)return state;await pause(50);}throw Error('host operation timeout');}
async function check(name,fn){try{await fn();rows.push({name,passed:true});console.log('PASS '+name);}catch(e){rows.push({name,passed:false,error:String(e)});throw e;}}
async function capture(name){await page.screenshot({path:path.join(out,name+'.png')});}
async function record(){
  cdp=await page.context().newCDPSession(page);await cdp.send('Performance.enable');fs.mkdirSync(path.join(out,'frames'),{recursive:true});
  cdp.on('Page.screencastFrame',async e=>{const file=String(frames.length).padStart(5,'0')+'.jpg';fs.writeFileSync(path.join(out,'frames',file),Buffer.from(e.data,'base64'));frames.push({file,time:e.metadata.timestamp});await cdp.send('Page.screencastFrameAck',{sessionId:e.sessionId}).catch(()=>{});});
  await cdp.send('Page.startScreencast',{format:'jpeg',quality:80,maxWidth:1200,maxHeight:900,everyNthFrame:1});recording=true;
}
const perf=async()=>Object.fromEntries((await cdp.send('Performance.getMetrics')).metrics.map(v=>[v.name,v.value]));
function seed(ids){for(const id of ids){fs.mkdirSync(path.join(live,'mods',id),{recursive:true});fs.copyFileSync(path.join(root,'verification/fixtures',id+'.wimod'),path.join(live,'mods',id+'.wimod'));fs.writeFileSync(path.join(live,'mods',id,'host-state.txt'),'disabled');}}
(async()=>{
  await startCore();await open(1,true);
  await page.locator('#tab-4').click();await page.locator('#plugins-open').click();await idle();await pick('ui-fixture');await page.locator('#mods-import').click();await idle();
  await capture('before-r1-list');await app.close();await open();await record();
  await check('release product / unchanged security boundary and five original categories',async()=>{
    await expect(page.locator('h1')).toContainText('1.3.0 社区版');const p=await app.evaluate(({BrowserWindow})=>BrowserWindow.getAllWindows()[0].webContents.getLastWebPreferences());assert(p.contextIsolation&&p.sandbox&&!p.nodeIntegration);assert.equal(await page.evaluate(()=>typeof window.require),'undefined');assert.equal(await page.locator('#tabs [role=tab]').count(),5);
  });
  await check('spring from actual position / rapid reversal / capped stretch / immediate content state',async()=>{
    await tab(0);metrics.navigation=await page.evaluate(async()=>{
      const pause=t=>new Promise(r=>setTimeout(r,t)),rect=()=>{const r=document.querySelector('.nav-indicator').getBoundingClientRect();return{x:r.x,w:r.width};};
      await new Promise(requestAnimationFrame); await new Promise(requestAnimationFrame); const initial=rect();document.getElementById('tab-4').click();for(let i=0;i<5;i++)await new Promise(requestAnimationFrame);const mid=rect();document.getElementById('tab-0').click();const redirected=rect();
      let min=Infinity,maxWidth=0;for(let i=0;i<45;i++){const r=rect();min=Math.min(min,r.x);maxWidth=Math.max(maxWidth,r.w);await new Promise(requestAnimationFrame);}
      for(const i of [1,4,2,0,1,1,3,2]){document.getElementById('tab-'+i).click();await pause(19);}await pause(900);
      return{initial,mid,redirected,min,maxWidth,final:rect(),target:document.getElementById('tab-2').getBoundingClientRect().x,count:document.querySelectorAll('.nav-indicator').length,pages:document.querySelectorAll('main>section').length,index:document.querySelector('[aria-selected=true]').id,animations:document.getAnimations().length};
    });const m=metrics.navigation;assert(m.mid.x>m.initial.x+5);assert(Math.abs(m.redirected.x-m.mid.x)<1);assert(m.maxWidth<m.initial.w*1.2);assert.equal(m.count,1);assert.equal(m.pages,1);assert.equal(m.index,'tab-2');assert(Math.abs(m.final.x-m.target)<1);assert.equal(m.animations,0);
    await capture('navigation');
  });
  await check('left targets overshoot without text/layout movement',async()=>{
    metrics.left=[];for(const target of [0,1]){await tab(4);const data=await page.evaluate(async target=>{const b=document.getElementById('tab-'+target),goal=b.getBoundingClientRect().x,nav=document.getElementById('tabs').getBoundingClientRect().x; b.click();let min=Infinity;for(let i=0;i<55;i++){min=Math.min(min,document.querySelector('.nav-indicator').getBoundingClientRect().x);await new Promise(requestAnimationFrame);}return{goal,nav,min,end:document.querySelector('.nav-indicator').getBoundingClientRect().x};},target);metrics.left.push(data);assert(data.min<data.goal-.1);assert(data.min>=data.nav-6.1);assert(Math.abs(data.end-data.goal)<1);}
  });
  await check('button-origin expansion, exact reverse, scroll/focus/window bounds restoration',async()=>{
    await tab(4);await page.locator('#topArcScale').fill('1.23');await page.locator('#plugins-open').scrollIntoViewIfNeeded();const scroll=await page.locator('#content').evaluate(e=>e.scrollTop),bounds=await app.evaluate(({BrowserWindow})=>BrowserWindow.getAllWindows()[0].getBounds());
    metrics.expand=await page.evaluate(async()=>{const b=document.getElementById('plugins-open'),r=b.getBoundingClientRect();b.focus();b.click();const start=document.querySelector('.plugin-morph').getBoundingClientRect();await new Promise(r=>setTimeout(r,110));const mid=document.querySelector('.plugin-morph').getBoundingClientRect();return{anchor:{x:r.x,y:r.y,w:r.width,h:r.height},start:{x:start.x,y:start.y,w:start.width,h:start.height},mid:{w:mid.width,h:mid.height},inert:document.getElementById('plugins').inert,old:!!document.getElementById('panel-4')};});
    assert(Math.abs(metrics.expand.start.w-metrics.expand.anchor.w)<2);assert(metrics.expand.mid.w>metrics.expand.start.w);assert(metrics.expand.inert&&metrics.expand.old);await settled();await idle();await capture('compact-list');
    await page.locator('#plugins-back').click();await settled();await expect(page.locator('#plugins-open')).toBeFocused();assert(Math.abs(await page.locator('#content').evaluate(e=>e.scrollTop)-scroll)<2);await expect(page.locator('#topArcScale')).toHaveValue('1.23');assert.deepEqual(await app.evaluate(({BrowserWindow})=>BrowserWindow.getAllWindows()[0].getBounds()),bounds);
    await page.evaluate(()=>document.getElementById('plugins-open').click());await pause(100);await page.keyboard.press('Escape');await pause(100);await page.keyboard.press('Escape');await settled();assert.equal(await page.locator('main>section').count(),1);assert.equal(await page.locator('#panel-4').count(),1);assert(await page.locator('#panel-4').evaluate(e=>e.inert));await idle();
  });
  await check('compact card, measured height, independent expansion and reverse continuity',async()=>{
    await importMod('ui-dependent');const h=await card('ui-fixture').evaluate(e=>e.getBoundingClientRect().height);assert(h<=58);
    metrics.card=await card('ui-fixture').evaluate(async e=>{const b=e.querySelector('.mod-summary'),h=()=>e.getBoundingClientRect().height;b.click();await new Promise(r=>setTimeout(r,120));const middle=h();b.click();const reverse=h();await new Promise(r=>setTimeout(r,80));b.click();return{middle,reverse};});assert(Math.abs(metrics.card.middle-metrics.card.reverse)<1);assert(metrics.card.middle>70);await pause(650);
    await expand('ui-dependent');await expect(card('ui-fixture').locator('.mod-summary')).toHaveAttribute('aria-expanded','true');await page.locator('#mod-ui-fixture-settings').click();await expect(card('ui-fixture')).toContainText('未启用');await capture('details-disabled');
  });
  await check('real enable + all four SDK settings save, drafts survive polls and area changes',async()=>{
    await page.locator('#mod-ui-fixture-enable').click();await idle();assert((await host('mods.list')).mods.find(m=>m.id==='ui-fixture').enabled);
    const setting=k=>card('ui-fixture').locator('[data-setting-key="'+k+'"]');
    await setting('caption').locator('input').fill('尚未保存的中文草稿');await pause(2200);await expect(setting('caption').locator('input')).toHaveValue('尚未保存的中文草稿');
    await page.locator('#mod-ui-fixture-logs').click();await idle();await page.locator('#mod-ui-fixture-settings').click();await expect(setting('caption').locator('input')).toHaveValue('尚未保存的中文草稿');
    await setting('caption').locator('button').click();await idle();await setting('size').locator('input').fill('24');await setting('size').locator('button').click();await idle();
    await setting('enabled').locator('input').focus();await page.keyboard.press('Space');await idle();await expect(setting('enabled').locator('input')).toHaveAttribute('aria-checked','false');
    await setting('choice').locator('select').selectOption('完整');await setting('choice').locator('button').click();await idle();
    const s=await host('mods.list');for(const [key,value]of [['caption','尚未保存的中文草稿'],['size','24'],['enabled','0'],['choice','完整']])assert.equal(s.resources.find(r=>r.owner==='ui-fixture'&&r.key===key).value,value);
    await expect.poll(async()=>(await host('mods.log',{id:'ui-fixture'})).text).toContain('observed persisted settings: 尚未保存的中文草稿|24|0|完整');await page.locator('#mod-ui-fixture-logs').click();await idle();await expect(card('ui-fixture').locator('.mod-log')).toContainText('observed persisted settings: 尚未保存的中文草稿|24|0|完整');await capture('saved-settings-log');
    await page.locator('#mod-ui-fixture-settings').click();await setting('verify-button').locator('button').click();await idle();assert((await host('mods.log',{id:'ui-fixture'})).text.includes('fixture button invoked'));
  });
  await check('reinstall onLoad failure restores original loaded instance and persisted settings',async()=>{
    await page.locator('#mod-ui-fixture-settings').click();const toggle=card('ui-fixture').locator('[data-setting-key=reject-repair] input');await toggle.click();await idle();
    await page.locator('#mod-ui-fixture-reinstall').click();await page.locator('#confirm button[value=confirm]').click();await idle();await expect(page.locator('#error')).toContainText('已恢复原实例');
    const s=await host('mods.list');assert(s.mods.find(m=>m.id==='ui-fixture').enabled);assert.equal(s.resources.find(r=>r.owner==='ui-fixture'&&r.key==='size').value,'24');await toggle.click();await idle();
  });
  await check('real reinstall from package, settings preserved, dependency-aware disable/unload',async()=>{
    await page.locator('#mod-ui-dependent-enable').click();await idle();await page.locator('#mod-ui-fixture-reinstall').click();await expect(page.locator('#confirm-text')).toContainText('重新校验');await page.locator('#confirm button[value=confirm]').click();await idle();let s=await host('mods.list');assert(s.mods.filter(m=>['ui-fixture','ui-dependent'].includes(m.id)).every(m=>m.enabled));assert.equal(s.resources.find(r=>r.owner==='ui-fixture'&&r.key==='size').value,'24');assert(fs.existsSync(path.join(live,'mod-cache/repairs/ui-fixture')));
    await page.locator('#mod-ui-fixture-disable').click();await expect(page.locator('#confirm-text')).toContainText('ui-dependent');await page.locator('#confirm button[value=cancel]').click();await idle();assert((await host('mods.list')).mods.find(m=>m.id==='ui-fixture').enabled);
    await page.locator('#mod-ui-fixture-disable').click();await page.locator('#confirm button[value=confirm]').click();await idle();assert((await host('mods.list')).mods.every(m=>!m.enabled));
    await page.locator('#mod-ui-fixture-unload').click();await idle();assert.equal((await host('mods.list')).mods.find(m=>m.id==='ui-fixture').status,'已卸载');assert(fs.existsSync(path.join(live,'mods/ui-fixture.wimod')));await page.locator('#mod-ui-fixture-enable').click();await idle();
  });
  await check('metadata distinguishes missing/version dependencies, incompatible API and invalid signature',async()=>{
    seed(['ui-missing','ui-version','ui-incompatible','ui-long','ui-signed-invalid']);await page.locator('#mods-scan').click();await idle();
    for(const id of ['ui-missing','ui-version','ui-incompatible','ui-long','ui-signed-invalid'])await expand(id);
    await expect(card('ui-missing')).toContainText('依赖未安装');await expect(card('ui-version')).toContainText('版本不满足要求');await expect(card('ui-incompatible')).toContainText('99');await expect(card('ui-signed-invalid')).toContainText('签名无效');await expect(card('ui-long').locator('.mod-author')).toHaveText('作者未填写');
    await card('ui-long').locator('.mod-summary').click();await pause(600);assert(await card('ui-long').evaluate(e=>e.getBoundingClientRect().height)<=58);assert(await card('ui-long').locator('.mod-name').getAttribute('title'));await capture('metadata-errors');
  });
  await check('invalid setting / stale handle / plugin callback error report real failures',async()=>{
    const before=await host('mods.list'), r=before.resources.find(r=>r.owner==='ui-fixture'&&r.key==='size');
    const result=await host('mods.invoke',{handle:r.handle,value:'999'});assert(result.ok);let s;
    for(let i=0;i<100;i++){s=await host('mods.list');if(!s.busy)break;await pause(40);}assert(s.operationError.includes('设置无效'));assert.equal(s.resources.find(x=>x.handle===r.handle).value,'24');
    await operation('disable','ui-fixture');const stale=await host('mods.invoke',{handle:r.handle,value:'20'});assert.equal(stale.ok,false);await operation('enable','ui-fixture');await page.locator('#mods-scan').click();await idle();
    await page.locator('#mod-ui-fixture-settings').click();await card('ui-fixture').locator('[data-setting-key=failure-button] button').click();await idle();await expect(page.locator('#error')).toContainText('回调异常');assert(!(await host('mods.list')).mods.find(m=>m.id==='ui-fixture').enabled);await page.locator('#mod-ui-fixture-enable').click();await idle();
  });
  await check('IME search/filter/clear and refresh keep keyed nodes, drafts, expansion, no loader operations',async()=>{
    const before=await host('mods.list');await page.locator('#mods-search').evaluate(e=>{window.testCard=document.querySelector('[data-mod-id=ui-fixture]');e.dispatchEvent(new CompositionEvent('compositionstart'));e.value='不存在';e.dispatchEvent(new InputEvent('input',{isComposing:true}));});await expect(card('ui-fixture')).toBeVisible();
    await page.locator('#mods-search').evaluate(e=>e.dispatchEvent(new CompositionEvent('compositionend')));await expect(page.locator('#mods-list .empty')).toContainText('没有匹配');await page.locator('#mods-search-clear').click();await expect(card('ui-fixture')).toBeVisible();
    await page.locator('#mods-search').fill('星河');await expect(card('ui-dependent')).toBeHidden();await page.locator('#mods-search-clear').click();await expect(card('ui-fixture').locator('.mod-summary')).toHaveAttribute('aria-expanded','true');assert(await page.evaluate(()=>window.testCard===document.querySelector('[data-mod-id=ui-fixture]')));assert.equal((await host('mods.list')).operation,before.operation);
  });
  await check('large list and scroll, repeated refresh without DOM rebuild or button loss',async()=>{
    seed(Array.from({length:40},(_,i)=>'list-'+String(i+1).padStart(2,'0')));await page.locator('#mods-scan').click();await idle();assert.equal(await page.locator('.mod-card').count(),47);
    await page.locator('#mods-search').fill('列表');assert.equal(await page.locator('.mod-card:visible').count(),40);await card('list-40').scrollIntoViewIfNeeded();assert(await page.locator('.plugin-overlay').evaluate(e=>e.scrollTop)>300);await page.locator('#mods-search-clear').click();
    await page.locator('#mods-scan').click();await idle();assert(await page.evaluate(()=>window.testCard===document.querySelector('[data-mod-id=ui-fixture]')));await capture('large-list');
  });
  await check('bulk disable/unload/reload preserves buttons packages and settings',async()=>{
    const source=fs.readFileSync(path.join(live,'mods/ui-fixture.wimod')), config=fs.readFileSync(path.join(live,'mods/ui-fixture/settings.xml'));
    for(const op of ['disable','unload','reload']){await page.locator('#mods-'+op).click();await expect(page.locator('#confirm')).toBeVisible();await page.locator('#confirm button[value=confirm]').click();await idle();await expect(page.locator('#mods-import')).toBeVisible();await expect(page.locator('#mods-'+op)).toBeEnabled();}
    assert.deepEqual(fs.readFileSync(path.join(live,'mods/ui-fixture.wimod')),source);assert.deepEqual(fs.readFileSync(path.join(live,'mods/ui-fixture/settings.xml')),config);await page.locator('#mod-ui-fixture-enable').click();await idle();
    await page.locator('#mods-market').click();await expect(page.locator('#status')).toContainText('暂未开放');
  });
  await check('reduced motion and keyboard disclosure; no active animation after settling',async()=>{
    await page.emulateMedia({reducedMotion:'reduce'});await card('ui-long').locator('.mod-summary').focus();await page.keyboard.press('Space');await expect(card('ui-long').locator('.mod-summary')).toHaveAttribute('aria-expanded','true');await page.keyboard.press('Enter');await expect(card('ui-long').locator('.mod-summary')).toHaveAttribute('aria-expanded','false');
    await page.locator('#plugins-back').click();await expect(page.locator('.plugin-morph')).toHaveCount(0);for(const i of [0,4,2,1])await page.locator('#tab-'+i).click();assert.equal(await page.evaluate(()=>document.getAnimations().length),0);await capture('reduced-motion');await page.emulateMedia({reducedMotion:'no-preference'});
  });
  await check('narrow resize during expansion, minimize/restore, hidden settings cannot receive input',async()=>{
    await app.evaluate(({BrowserWindow})=>BrowserWindow.getAllWindows()[0].setSize(660,580));await tab(4);await page.locator('#plugins-open').scrollIntoViewIfNeeded();await page.locator('#plugins-open').click();await pause(85);await app.evaluate(({BrowserWindow})=>BrowserWindow.getAllWindows()[0].setSize(760,680));await settled();await idle();assert.equal(await page.locator('#topArcScale').count(),1);assert(await page.locator('#panel-4').evaluate(e=>e.inert));assert.equal(await page.locator('main>section').count(),1);
    await app.evaluate(({BrowserWindow})=>{const w=BrowserWindow.getAllWindows()[0];w.minimize();w.restore();});await capture('narrow-plugins');assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth));await page.locator('#plugins-back').click();await settled();await expect(page.locator('#plugins-open')).toBeFocused();
    const a=await perf();await pause(1200);const b=await perf();metrics.idle={intervalMs:1200,taskMs:(b.TaskDuration-a.TaskDuration)*1000,heapBytes:b.JSHeapUsedSize,layouts:b.LayoutCount-a.LayoutCount};assert.equal(await page.evaluate(()=>document.getAnimations().length),0);
  });
  await cdp.send('Page.stopScreencast');recording=false;await app.close();
  for(const scale of [1,1.25,1.5,2])await check('packaged renderer DPI '+scale,async()=>{await open(scale);assert.equal(await page.evaluate(()=>devicePixelRatio),scale);await enter();await expand('ui-long');await capture('dpi-'+scale);assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth));await page.locator('#plugins-back').click();await settled();await app.close();});
  await check('core restart restores plugin values and window reopen cleans transitions',async()=>{
    await stopCore();await startCore();await open();await enter();await expand('ui-fixture');await page.locator('#mod-ui-fixture-settings').click();await expect(card('ui-fixture').locator('[data-setting-key=caption] input')).toHaveValue('尚未保存的中文草稿');await expect(card('ui-fixture').locator('[data-setting-key=size] input')).toHaveValue('24');
    await page.locator('#plugins-back').click();await pause(50);await app.close();await open();await expect(page.locator('.plugin-morph')).toHaveCount(0);assert((await host('settings.read')).ok);await capture('final');
  });
  await check('no unhandled renderer errors',async()=>assert.deepEqual(errors,[]));
})().catch(e=>{console.error(e);process.exitCode=1;}).finally(async()=>{
  if(recording)await cdp.send('Page.stopScreencast').catch(()=>{});if(page)await capture('last-state').catch(()=>{});
  await app?.close().catch(()=>{});await stopCore().catch(()=>{});
  fs.writeFileSync(path.join(out,'results.json'),JSON.stringify({rows,metrics,errors},null,2));fs.writeFileSync(path.join(out,'frames.json'),JSON.stringify(frames));fs.writeFileSync(path.join(root,'verification/latest-ui-run.txt'),out);console.log('Evidence: '+out);
});
