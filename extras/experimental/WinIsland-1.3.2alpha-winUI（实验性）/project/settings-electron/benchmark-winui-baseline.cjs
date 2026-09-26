const {_electron:electron}=require('@playwright/test');
const fs=require('fs'),path=require('path'),{spawn,execFileSync}=require('child_process');
const root=path.resolve(__dirname,'..'),out=path.join(root,'verification','electron-baseline');fs.mkdirSync(out,{recursive:true});
const sleep=ms=>new Promise(r=>setTimeout(r,ms));
async function until(fn){for(let n=0;n<200;n++){if(await fn())return;await sleep(50);}throw Error('timeout');}
const data=path.join(out,'data');fs.mkdirSync(data,{recursive:true});
let core,app;const results=[];
(async()=>{try{
 for(const name of ['exit.request','settings-connection.json']){const p=path.join(data,name);if(fs.existsSync(p))fs.unlinkSync(p);}
 fs.mkdirSync(path.join(data,'mods'),{recursive:true});
 for(const id of ['community-base','community-consumer']){fs.copyFileSync(path.join(root,'community-examples/packages',id+'.wimod'),path.join(data,'mods',id+'.wimod'));fs.mkdirSync(path.join(data,'mods',id),{recursive:true});fs.writeFileSync(path.join(data,'mods',id,'host-state.txt'),'disabled');}
 const t=performance.now();core=spawn(path.join(root,'release/WinIsland-1.3.2alpha.exe'),['--verify',data],{windowsHide:true,cwd:data});await until(()=>fs.existsSync(path.join(data,'settings-connection.json')));const coreReadyMs=performance.now()-t;
 const c=JSON.parse(fs.readFileSync(path.join(data,'settings-connection.json')));
 for(let i=0;i<3;i++){
  const start=performance.now();app=await electron.launch({executablePath:path.join(root,'release/settings/WinIslandSettings.exe'),args:['--pipe='+c.pipe,'--token='+c.token,'--session-dir='+path.join(out,'session-'+i)],timeout:30000});const page=await app.firstWindow();await page.locator('#resident').waitFor();await until(()=>page.locator('#resident').isEnabled());const settingsReadyMs=performance.now()-start;
  await sleep(1500);const metrics=await app.evaluate(({app})=>app.getAppMetrics().map(x=>({type:x.type,pid:x.pid,memory:x.memory,cpu:x.cpu})));
  await page.locator('#tab-4').click();let p=performance.now();await page.locator('#plugins-open').click();await page.locator('[data-mod-id="community-base"]').waitFor({state:'visible'});const pluginsMs=performance.now()-p;await page.locator('#plugins-back').click();await sleep(600);
  p=performance.now();await page.locator('.transfer-entry').click();await page.locator('.transfer-page').waitFor({state:'visible'});const transferMs=performance.now()-p;
  if(i===0)await page.screenshot({path:path.join(out,'transfer.png')});results.push({sample:i+1,coreReadyMs,settingsReadyMs,pluginsMs,transferMs,metrics});await app.close();app=null;
 }
 fs.writeFileSync(path.join(out,'results.json'),JSON.stringify({conditions:'Three sequential warm desktop launches, isolated empty transfer index and two disabled plugins; Electron workingSetSize is KiB; UI timings include automation overhead.',results},null,2));console.log(JSON.stringify(results.map(x=>({sample:x.sample,settingsReadyMs:x.settingsReadyMs,pluginsMs:x.pluginsMs,transferMs:x.transferMs,workingSetKiB:x.metrics.reduce((s,m)=>s+m.memory.workingSetSize,0)}))));
}finally{if(app)await app.close();if(core&&core.exitCode===null){fs.writeFileSync(path.join(data,'exit.request'),'exit');await new Promise(r=>core.once('exit',r));}}})().catch(e=>{console.error(String(e));process.exitCode=1;});
