const {_electron:electron,expect}=require('@playwright/test'),fs=require('fs'),path=require('path'),assert=require('assert/strict'),net=require('net');
const {hostCall}=require('./dist/transport');const root=path.resolve('..'),live=path.join(root,'verification/electron-extra'),out=path.join(root,'verification/extra');fs.mkdirSync(out,{recursive:true});
const c=JSON.parse(fs.readFileSync(path.join(live,'settings-connection.json')));const rows=[];let app,page;
const pause=ms=>new Promise(r=>setTimeout(r,ms));
const host=(cmd,p={})=>hostCall(c.pipe,c.token,cmd,p);
const idle=()=>expect(page.locator('#content')).toHaveAttribute('aria-busy','false');
async function check(name,fn){try{await fn();rows.push({name,passed:true});console.log('PASS '+name);}catch(e){rows.push({name,passed:false,error:String(e)});throw e;}}
async function open(scale){app=await electron.launch({executablePath:path.join(root,'release/settings/WinIslandSettings.exe'),args:['--pipe='+c.pipe,'--token='+c.token,'--session-dir='+path.join(out,'session-'+scale),'--force-device-scale-factor='+scale],timeout:30000});page=await app.firstWindow();await expect(page.locator('#connection')).toContainText('已连接');await idle();}
async function pick(file){await app.evaluate(({dialog},file)=>{dialog.showOpenDialog=async()=>({canceled:false,filePaths:[file]});},file);}
(async()=>{
 await open(1);
 await check('software rendering core + actual Electron controls',async()=>assert.equal((await host('capabilities.read')).renderer,'Direct2D software'));
 await page.locator('#tab-4').click();await page.locator('#plugins-open').click();await idle();
 await check('renderer import through main file-picker boundary',async()=>{await pick(path.join(root,'verification/ui-import.wimod'));await page.locator('#mods-import').click();await idle();await expect(page.locator('[data-mod-id=ui-import]')).toBeVisible();});
 await check('invalid package error visible and no package installed',async()=>{const f=path.join(out,'broken.wimod');fs.writeFileSync(f,'broken');await pick(f);await page.locator('#mods-import').click();await idle();await expect(page.locator('#error')).toBeVisible();assert.equal((await host('mods.list')).mods.length,1);});
 await check('dependency cascade confirmation cancel and confirm',async()=>{
  await pick(path.join(root,'verification/ui-dependent.wimod'));await page.locator('#mods-import').click();await idle();await page.locator('#ui-dependent-enable').click();await idle();
  await page.locator('#ui-import-disable').click();await expect(page.locator('#confirm')).toBeVisible();await expect(page.locator('#confirm-text')).toContainText('ui-dependent');await page.locator('#confirm button[value=cancel]').click();await idle();assert((await host('mods.list')).mods.every(m=>m.enabled));
  await page.locator('#ui-import-disable').click();await page.locator('#confirm button[value=confirm]').click();await idle();assert((await host('mods.list')).mods.every(m=>!m.enabled));
 });
 await check('batch disable and unload keep files, buttons stay visible',async()=>{await page.locator('#ui-dependent-enable').click();await idle();for(const id of ['mods-disable','mods-unload']){await page.locator('#'+id).click();await page.locator('#confirm button[value=confirm]').click();await idle();assert((await host('mods.list')).mods.every(m=>!m.enabled));await expect(page.locator('#mods-import')).toBeVisible();}assert(fs.existsSync(path.join(live,'mods/ui-import.wimod')));});
 await check('typed plugin setting persists through host callback',async()=>{await page.locator('#ui-import-enable').click();await idle();const input=page.locator('[data-mod-id=ui-import] input[type=checkbox]');await input.check();await page.locator('[data-mod-id=ui-import] button[id$="-save"]').click();await idle();assert((await host('mods.list')).resources.some(r=>r.owner==='ui-import'&&r.value==='1'));});
 await page.locator('#plugins-back').click();
 await check('diagnostics real start stop export',async()=>{await page.locator('#tab-3').click();for(const action of ['start','stop','export']){await page.locator('#diagnostics-'+action).click();await idle();}const data=await host('media.read');assert(fs.existsSync(data.exportPath));});
 await check('lyric import and clear through real parser/cache (explicit synthetic media fixture)',async()=>{
  fs.writeFileSync(path.join(live,'command.txt'),'music=play');await expect.poll(async()=>(await host('media.read')).active).toBe(true);
  const lrc=path.join(out,'验证 歌词.lrc');fs.writeFileSync(lrc,'[00:00.00]fixture lyric\n[00:15.00]second line');await pick(lrc);await page.locator('#tab-1').click();await page.locator('#refresh').click();await idle();await page.locator('#lyrics-import').click();await idle();assert(fs.readdirSync(path.join(live,'lyrics')).some(n=>n.endsWith('.lrc')));await page.locator('#lyrics-clear').click();await idle();
 });
 await app.close();
 for(const scale of [1,1.25,1.5,2])await check('Electron device scale '+scale+' no overflow or motion',async()=>{await open(scale);await page.locator('#tab-4').click();assert.equal(await page.evaluate(()=>window.devicePixelRatio),scale);assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth));await page.screenshot({path:path.join(out,'dpi-'+scale+'.png')});await app.close();});
 await check('actual transport timeout and cancellation',async()=>{const name='\\\\.\\pipe\\WinIsland.Settings.987654';const server=net.createServer(s=>s.on('error',()=>{}));await new Promise(r=>server.listen(name,r));let a=await hostCall(name,'token','settings.read',{},undefined,150);assert.equal(a.code,'TIMEOUT');const controller=new AbortController();const promise=hostCall(name,'token','settings.read',{},controller.signal);setTimeout(()=>controller.abort(),30);assert.equal((await promise).code,'CANCELLED');server.close();});
})().catch(e=>{console.error(e);process.exitCode=1;}).finally(async()=>{await app?.close().catch(()=>{});fs.writeFileSync(path.join(out,'results.json'),JSON.stringify(rows,null,2));fs.writeFileSync(path.join(live,'exit.request'),'exit');});
