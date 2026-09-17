const {_electron:electron,expect}=require('@playwright/test'),fs=require('fs'),path=require('path'),assert=require('assert/strict'),{spawn}=require('child_process');
const {hostCall}=require('./dist/transport');
const root=path.resolve('..'),out=path.join(root,'verification/settings-animation/local-'+Date.now()),live=path.join(out,'core');fs.mkdirSync(live,{recursive:true});
const pause=ms=>new Promise(r=>setTimeout(r,ms));let core,app,page,c;const rows=[];
const host=(cmd,p={})=>hostCall(c.pipe,c.token,cmd,p);
const idle=async()=>{await expect(page.locator('#content')).toHaveAttribute('aria-busy','false');await expect.poll(async()=>page.locator('[data-state=pending]').count()).toBe(0);};
async function check(name,fn){try{const evidence=await fn();rows.push({name,passed:true,evidence});console.log('PASS '+name);}catch(e){rows.push({name,passed:false,error:String(e)});console.log('FAIL '+name+': '+String(e).slice(0,300));}}
async function open(){app=await electron.launch({executablePath:path.join(root,'release/settings/WinIslandSettings.exe'),args:['--pipe='+c.pipe,'--token='+c.token,'--session-dir='+path.join(out,'session')]});page=await app.firstWindow();await expect(page.locator('#connection')).toContainText('已连接');await idle();}
async function watch(key){return page.evaluate(key=>{
  const panel=document.querySelector('main > section'),other=[...panel.querySelectorAll('input,select,label,button')].filter(e=>e.id!==key);
  const snapshot=e=>({value:e.value,disabled:e.disabled,selectionStart:e.selectionStart,selectionEnd:e.selectionEnd,rect:JSON.stringify(e.getBoundingClientRect()),background:getComputedStyle(e).backgroundColor,opacity:getComputedStyle(e).opacity,transform:getComputedStyle(e).transform,font:getComputedStyle(e).font});
  window.watchState={other,initial:other.map(snapshot),mutations:[],samples:[],focus:document.activeElement,snapshot};
  window.watchObserver=new MutationObserver(rs=>{for(const r of rs)if(r.target.id!==key)window.watchState.mutations.push({id:r.target.id,kind:r.type,attribute:r.attributeName});});
  window.watchObserver.observe(panel,{attributes:true,childList:true,subtree:true});
  let count=0;const sample=()=>{window.watchState.samples.push(other.map(snapshot));if(++count<60)window.watchFrame=requestAnimationFrame(sample);};window.watchFrame=requestAnimationFrame(sample);
},key);}
async function endWatch(){await pause(300);return page.evaluate(()=>{const s=window.watchState;window.watchObserver.disconnect();cancelAnimationFrame(window.watchFrame);return {mutations:s.mutations,replaced:s.other.some(e=>!e.isConnected),differences:s.samples.flatMap((sample,frame)=>sample.flatMap((x,i)=>JSON.stringify(x)===JSON.stringify(s.initial[i])?[]:[{frame,id:s.other[i].id,before:s.initial[i],after:x}])),focusRetained:document.activeElement===s.focus,frames:s.samples.length};});}
(async()=>{
 core=spawn(path.join(root,'release/WinIsland-1.2.7alpha.exe'),['--verify',live],{windowsHide:true});for(let i=0;i<100&&!fs.existsSync(path.join(live,'settings-connection.json'));i++)await pause(100);c=JSON.parse(fs.readFileSync(path.join(live,'settings-connection.json')));await open();
 await check('all 20 ordered category pairs have correct signed entrance/exit; repeat does not animate',async()=>{
  const samples=[];
  for(let from=0;from<5;from++)for(let to=0;to<5;to++)if(from!==to){await page.evaluate(i=>document.getElementById('tab-'+i).click(),from);await pause(300);const s=await page.evaluate(to=>{document.getElementById('tab-'+to).click();const get=target=>document.getAnimations().find(a=>a.effect.target===target)?.effect.getKeyframes();return {in:get(document.getElementById('panel-'+to)),out:get(document.querySelector('.page-ghost > section')),count:document.querySelectorAll('main > section').length};},to);samples.push({from,to,...s});await pause(250);}
  fs.writeFileSync(path.join(out,'directions.json'),JSON.stringify(samples,null,2));
  for(const s of samples){const direction=Math.sign(s.to-s.from);assert.equal(s.in[0].transform,`translateX(${direction*24}px)`);assert.equal(s.out[1].transform,`translateX(${-direction*24}px)`);assert.equal(s.count,1);}
  await pause(100);const same=await page.evaluate(()=>{const before=document.querySelector('main > section');document.querySelector('[aria-selected=true]').click();return {same:before===document.querySelector('main > section'),animations:document.getAnimations().length};});assert(same.same);assert.equal(same.animations,0);return {pairs:samples.length,same};
 });
 for(const [key,index,draft] of [['resident',0,'seconds'],['hideNative',0,'seconds'],['showFps',2,'pingTarget'],['showPing',2,'pingTarget'],['topAttach',4,'topArcScale']])await check(key+' switch preserves adjacent draft, nodes, geometry, styles, selection and keyboard focus',async()=>{
  await page.evaluate(i=>document.getElementById('tab-'+i).click(),index);await pause(350);await page.locator('#'+draft).fill(draft==='pingTarget'?'192.168.1.123':'1.25');await page.locator('#'+draft).focus();if(draft==='pingTarget')await page.locator('#'+draft).evaluate(e=>e.setSelectionRange(2,6));
  await watch(key);const before=(await host('settings.read')).settings[key];await page.evaluate(key=>document.getElementById(key).click(),key);await idle();const evidence=await endWatch();fs.writeFileSync(path.join(out,key+'.json'),JSON.stringify(evidence,null,2));
  assert.equal((await host('settings.read')).settings[key],!before);assert(!evidence.replaced);assert.deepEqual(evidence.mutations,[]);assert.deepEqual(evidence.differences,[]);assert(evidence.focusRetained);await page.screenshot({path:path.join(out,key+'.png')});return evidence;
 });
 await check('rapid odd/even switch intent is coalesced and latest accepted state wins',async()=>{
  await page.evaluate(()=>document.getElementById('tab-0').click());await pause(300);let s=(await host('settings.read')).settings;
  await page.evaluate(()=>{for(let i=0;i<9;i++)document.getElementById('resident').click();for(let i=0;i<8;i++)document.getElementById('hideNative').click();});await idle();assert.equal((await host('settings.read')).settings.resident,!s.resident);assert.equal((await host('settings.read')).settings.hideNative,s.hideNative);return {residentClicks:9,hideNativeClicks:8};
 });
 await check('actual revision conflict only rolls changed switch back; preserves neighboring draft and focus',async()=>{
  const s=await host('settings.read');await host('settings.write',{revision:s.revision,patch:{seconds:s.settings.seconds+1}});await page.locator('#seconds').fill('19.25');await page.locator('#seconds').focus();await watch('resident');await page.evaluate(()=>document.getElementById('resident').click());await idle();await expect(page.locator('#error')).toBeVisible();const evidence=await endWatch();assert.deepEqual(evidence.differences,[]);assert(evidence.focusRetained);assert.equal((await host('settings.read')).settings.resident,s.settings.resident);await expect(page.locator('#seconds')).toHaveValue('19.25');return evidence;
 });
 await check('delayed real IPC: typing continues while pending, intent coalesces to last click',async()=>{
  await page.locator('#refresh').click();await idle();await page.locator('#seconds').fill('28.75');await page.locator('#seconds').focus();
  await app.evaluate(({ipcMain})=>{const original=ipcMain._invokeHandlers.get('host:call');globalThis.localTestOriginal=original;globalThis.localTestWrites=[];ipcMain.removeHandler('host:call');ipcMain.handle('host:call',async(...args)=>{if(args[2]==='settings.write'){globalThis.localTestWrites.push(args[3].patch);await new Promise(r=>setTimeout(r,180));}return original(...args);});});
  const old=(await host('settings.read')).settings.resident;await watch('resident');
  await page.evaluate(()=>document.getElementById('resident').click());await expect(page.locator('#resident')).toHaveAttribute('data-state','pending');
  await page.evaluate(()=>{document.getElementById('resident').click();document.getElementById('resident').click();document.getElementById('resident').click();});
  await expect(page.locator('#seconds')).toBeFocused();await expect(page.locator('#seconds')).toBeEnabled();await idle();const evidence=await endWatch();assert.deepEqual(evidence.differences,[]);assert.deepEqual(evidence.mutations,[]);
  const writes=await app.evaluate(({ipcMain})=>{ipcMain.removeHandler('host:call');ipcMain.handle('host:call',globalThis.localTestOriginal);return globalThis.localTestWrites;});assert.equal(writes.length,2);assert.equal((await host('settings.read')).settings.resident,old);await expect(page.locator('#seconds')).toHaveValue('28.75');return {writes,frames:evidence.frames,unchangedNeighbors:true};
 });
 await check('reopen restores selected category and scroll; actual core values reload',async()=>{
  await page.evaluate(()=>document.getElementById('tab-4').click());await pause(300);await page.locator('#plugins-open').scrollIntoViewIfNeeded();await page.locator('#topArcScale').evaluate(e=>e.focus({preventScroll:true}));const scroll=await page.locator('#content').evaluate(e=>e.scrollTop);await app.close();await open();await expect(page.locator('#tab-4')).toHaveAttribute('aria-selected','true');assert(Math.abs(await page.locator('#content').evaluate(e=>e.scrollTop)-scroll)<2);await expect(page.locator('#topArcScale')).toBeFocused();return {scroll,focus:'topArcScale'};
 });
})().catch(e=>{rows.push({name:'harness',passed:false,error:String(e)});console.error(e);}).finally(async()=>{await app?.close().catch(()=>{});fs.writeFileSync(path.join(live,'exit.request'),'exit');fs.writeFileSync(path.join(out,'results.json'),JSON.stringify(rows,null,2));console.log(out);if(rows.some(r=>!r.passed))process.exitCode=1;});
