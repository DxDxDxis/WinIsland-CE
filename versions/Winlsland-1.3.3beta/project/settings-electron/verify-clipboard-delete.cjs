const {_electron:electron}=require('@playwright/test');
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict'),{spawn}=require('node:child_process');
const {hostCall}=require('./dist/transport');
const root=path.resolve(__dirname,'..'),out=path.join(root,'verification','clipboard-delete-'+Date.now()),data=path.join(out,'data');fs.mkdirSync(data,{recursive:true});
const settingsExe=process.env.WINISLAND_SETTINGS_EXE||path.join(root,'release/settings/WinIslandSettings.exe');
const sleep=ms=>new Promise(r=>setTimeout(r,ms));
const until=async fn=>{for(let n=0;n<100;n++){if(await fn())return;await sleep(100);}throw Error('condition timed out');};
let core,app,page,backup,connection;const results=[],errors=[];
const call=async(action,p={})=>{const r=await hostCall(connection.pipe,connection.token,'clipboard.call',{action,...p},undefined,15000);assert(r.ok,r.error);return r;};
const ids=async()=>(await call('list')).entries.map(e=>e.id);
async function start(){for(const f of ['exit.request','settings-connection.json'])fs.rmSync(path.join(data,f),{force:true});core=spawn(path.join(root,'build-1.3.3beta/WinIsland-1.3.3beta.exe'),['--verify',data],{cwd:data,windowsHide:true});await until(()=>fs.existsSync(path.join(data,'settings-connection.json')));connection=JSON.parse(fs.readFileSync(path.join(data,'settings-connection.json')));}
async function stop(){if(core&&core.exitCode===null){fs.writeFileSync(path.join(data,'exit.request'),'exit');await Promise.race([new Promise(r=>core.once('exit',r)),sleep(10000)]);if(core.exitCode===null)core.kill();}}
async function check(name,fn){await fn();results.push({name,passed:true});console.log('PASS',name);}
async function modal(){await page.locator('#confirm[open]').waitFor();await page.screenshot({path:path.join(out,'confirmation.png')});fs.writeFileSync(path.join(out,'dialog-state.json'),JSON.stringify(await page.locator('#confirm').evaluate(e=>({inert:e.inert,active:document.activeElement?.outerHTML,contentInert:document.getElementById('content').inert,open:e.open})),null,2));}
async function answer(value){await page.locator(`#confirm button[value="${value}"]`).click({timeout:1800});await page.locator('#confirm[open]').waitFor({state:'hidden',timeout:2000});await until(async()=>await page.locator('#clipboard-page').getAttribute('aria-busy')!=='true');}
(async()=>{try{
 await start();await call('preferences',{preferences:{...(await call('list')).preferences,show:false}});
 app=await electron.launch({executablePath:settingsExe,args:['--pipe='+connection.pipe,'--token='+connection.token,'--core-pid='+core.pid,'--session-dir='+data]});page=await app.firstWindow();page.on('pageerror',e=>errors.push(String(e)));
 backup=await app.evaluate(async({clipboard})=>Promise.all((await clipboard.read()).map(item=>Promise.all(item.types.map(async type=>{const value=await item.getType(type);return {type,bytes:value instanceof Blob?Buffer.from(await value.arrayBuffer()).toString('base64'):null,value:value instanceof Blob?null:value};})))));
 for(let i=0;i<3;i++){await app.evaluate(({clipboard},i)=>clipboard.writeText('删除确认回归 '+i),i);await until(async()=>(await ids()).length===i+1);}
 await page.locator('#tab-4').click();await page.locator('#clipboard-open').click();await page.locator('#clipboard-clear').waitFor();await until(async()=>await page.locator('.clipboard-card:visible').count()===3);
 const before=await ids();
 await check('清空历史：鼠标取消可用且不改记录',async()=>{await page.locator('#clipboard-clear').click();await modal();await answer('cancel');assert.deepEqual(await ids(),before);});
 await check('清空历史：Escape 取消、恢复输入',async()=>{await page.locator('#clipboard-clear').click();await modal();await page.keyboard.press('Escape');await page.locator('#confirm[open]').waitFor({state:'hidden'});await until(async()=>await page.locator('#clipboard-page').getAttribute('aria-busy')!=='true');assert.deepEqual(await ids(),before);assert(await page.locator('#content').evaluate(e=>e.inert));});
 await check('删除所选：取消/确认与稳定 ID 保存一致',async()=>{const card=page.locator('.clipboard-card:visible').first(),id=await card.getAttribute('data-id');await card.locator('input[type=checkbox]').check();await page.locator('#clipboard-delete-selected').click();await modal();await answer('cancel');assert.deepEqual(await ids(),before);await page.locator('#clipboard-delete-selected').click();await modal();await answer('confirm');assert.deepEqual(await ids(),before.filter(x=>x!==id));await until(async()=>await page.locator('.clipboard-card:visible').count()===2);});
 await check('卡片右键删除：取消/确认正常，无焦点死锁',async()=>{const card=page.locator('.clipboard-card:visible').first(),id=await card.getAttribute('data-id'),saved=await ids();await card.click({button:'right'});await page.getByRole('menuitem',{name:'删除',exact:true}).click();await modal();await answer('cancel');assert.deepEqual(await ids(),saved);await card.click({button:'right'});await page.getByRole('menuitem',{name:'删除',exact:true}).click();await modal();await answer('confirm');assert.deepEqual(await ids(),saved.filter(x=>x!==id));});
 await check('详情删除确认框：Tab/Enter 可取消，不误删',async()=>{const card=page.locator('.clipboard-card:visible').first();await card.locator('.clipboard-summary').click();await card.getByRole('button',{name:'删除记录',exact:true}).click();await modal();await page.locator('#confirm button[value=cancel]').focus();await page.keyboard.press('Tab');assert.equal(await page.evaluate(()=>document.activeElement?.getAttribute('value')),'confirm');await page.keyboard.press('Shift+Tab');await page.keyboard.press('Enter');await page.locator('#confirm[open]').waitFor({state:'hidden'});assert.equal((await ids()).length,1);});
 await check('清空历史：确认后真实清空，返回重开仍可操作',async()=>{await page.locator('#clipboard-clear').click();await modal();await answer('confirm');assert.deepEqual(await ids(),[]);await page.locator('#clipboard-back').click();await page.locator('#clipboard-open').click();await page.locator('#clipboard-clear').click();await modal();await answer('cancel');assert.deepEqual(errors,[]);});
 await app.close();app=null;await stop();await start();await check('删除结果在重启后仍保持',async()=>assert.deepEqual(await ids(),[]));
 }catch(e){results.push({passed:false,error:String(e)});console.error(e);process.exitCode=1;}
 finally{
 await stop();
 // Restore in a short-lived settings process only after the isolated listener stops.
 if(!app&&backup)app=await electron.launch({executablePath:settingsExe,args:['--pipe='+connection.pipe,'--token='+connection.token,'--core-pid='+core.pid,'--session-dir='+data]}).catch(()=>null);
 if(app){if(backup)await app.evaluate(async({clipboard,ClipboardItem},values)=>{if(!values.length){clipboard.clear();return;}await clipboard.write(values.map(items=>new ClipboardItem(Object.fromEntries(items.map(x=>[x.type,x.bytes!==null?new Blob([Buffer.from(x.bytes,'base64')],{type:x.type}):x.value])))));},backup).catch(()=>{});await app.close().catch(()=>{});}
 fs.writeFileSync(path.join(out,'results.json'),JSON.stringify(results,null,2));console.log('Evidence:',out);
 }})().catch(e=>{console.error(e);process.exitCode=1;});
