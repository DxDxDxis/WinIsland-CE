const { _electron: electron } = require('@playwright/test');
const fs=require('node:fs'), path=require('node:path'),assert=require('node:assert/strict'),{spawn}=require('node:child_process');
const {hostCall}=require('./dist/transport');
const root=path.resolve(__dirname,'..'),out=path.join(root,'verification','community-ui'),data=path.join(out,'data');
fs.mkdirSync(path.join(data,'mods'),{recursive:true});
for(const id of ['community-base','community-consumer','community-qoi-decoder','community-qoi-view','community-restart']){
 fs.copyFileSync(path.join(root,'community-examples/packages',id+'.wimod'),path.join(data,'mods',id+'.wimod'));
 fs.mkdirSync(path.join(data,'mods',id),{recursive:true});fs.writeFileSync(path.join(data,'mods',id,'host-state.txt'),'disabled');
}
for(const f of ['exit.request','settings-connection.json'])fs.rmSync(path.join(data,f),{force:true});
const sleep=ms=>new Promise(r=>setTimeout(r,ms));
async function until(fn){for(let i=0;i<240;i++){if(await fn())return;await sleep(50);}throw Error('timeout');}
let core,app,c;const checks=[];
const host=(cmd,p={})=>hostCall(c.pipe,c.token,cmd,p,undefined,20000);
async function action(op,id){await host('mods.action',{action:op,id});await until(async()=>!(await host('mods.list')).busy);}
async function capture(name){fs.writeFileSync(path.join(data,'command.txt'),'capture');await until(()=>!fs.existsSync(path.join(data,'command.txt')));await sleep(100);fs.copyFileSync(path.join(data,'capture.png'),path.join(out,name+'.png'));}
(async()=>{
 try{
  core=spawn(path.join(root,'release/WinIsland-1.3.2alpha.exe'),['--verify',data],{windowsHide:true,cwd:data});
  await until(()=>fs.existsSync(path.join(data,'settings-connection.json')));c=JSON.parse(fs.readFileSync(path.join(data,'settings-connection.json')));
  await until(async()=>!(await host('mods.list')).busy);
  await action('enable','community-consumer');let list=await host('mods.list');assert(list.mods.find(x=>x.id==='community-base').enabled);checks.push('live app enables required provider before consumer');await sleep(600);await capture('service-result');
  await host('mods.action',{action:'disable',id:'community-base',cascade:true});await until(async()=>!(await host('mods.list')).busy);
  await action('enable','community-qoi-decoder');await action('enable','community-qoi-view');await sleep(1000);await capture('qoi-island');
  app=await electron.launch({executablePath:path.join(root,'release/settings/WinIslandSettings.exe'),args:['--pipe='+c.pipe,'--token='+c.token,'--session-dir='+path.join(out,'electron')],timeout:30000});const page=await app.firstWindow();
  const pixels=await app.evaluate(({nativeImage},file)=>{const im=nativeImage.createFromPath(file),b=im.toBitmap();let n=0;for(let i=0;i<b.length;i+=4)if(b[i]===96&&b[i+1]===192&&b[i+2]===32&&b[i+3]===255)n++;return {green:n,size:im.getSize()};},path.join(out,'qoi-island.png'));
  assert(pixels.green>50,JSON.stringify(pixels));checks.push('real native renderer capture contains QOI decoded green pixels');
  await page.locator('#tab-4').click();await page.locator('#plugins-open').click();await sleep(800);
  await action('enable','community-restart');await action('disable','community-restart');
  await until(()=>page.locator('#mod-community-restart-summary').count());await page.locator('#mod-community-restart-summary').click();
  await until(async()=>(await page.locator('[data-mod-id="community-restart"] .mod-status').textContent()).includes('重启后生效'));
  const status=await page.locator('[data-mod-id="community-restart"] .mod-status').textContent();assert(status.includes('DLL 已加载'));checks.push('packaged settings shows pending restart and still-loaded DLL truthfully');
  await page.screenshot({path:path.join(out,'restart-status.png')});
  checks.push('existing settings layout/navigation and plugin entry usable');
  fs.writeFileSync(path.join(out,'results.json'),JSON.stringify({passed:true,checks,pixels},null,2));console.log(JSON.stringify({passed:true,checks,pixels}));
 }catch(e){fs.writeFileSync(path.join(out,'results.json'),JSON.stringify({passed:false,checks,error:String(e)},null,2));throw e;}
 finally{if(app)await app.close();if(core?.exitCode===null){fs.writeFileSync(path.join(data,'exit.request'),'exit');await new Promise(r=>core.once('exit',r));}}
})().catch(e=>{console.error(String(e));process.exitCode=1;});
