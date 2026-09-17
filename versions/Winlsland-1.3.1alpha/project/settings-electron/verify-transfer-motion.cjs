const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict'),{spawn}=require('node:child_process');
const {hostCall}=require('./dist/transport');
const root=path.resolve(__dirname,'..'),out=path.join(root,'verification','transfer-motion-'+Date.now());fs.mkdirSync(out,{recursive:true});
const core=spawn(path.join(root,'release/WinIsland-1.3.1alpha.exe'),['--verify',out],{windowsHide:true,cwd:out});
const sleep=ms=>new Promise(r=>setTimeout(r,ms)),results=[];
function state(){try{return Object.fromEntries(fs.readFileSync(path.join(out,'state.txt'),'utf8').split(/\r?\n/).filter(l=>l.includes('=')).map(l=>[l.slice(0,l.indexOf('=')),l.slice(l.indexOf('=')+1)]));}catch{return {};}}
async function until(f){for(let i=0;i<300;i++){const r=await f();if(r)return r;await sleep(30);}throw Error('timeout');}
async function command(text){const before=Number(state().CommandsProcessed||0);fs.writeFileSync(path.join(out,'command.txt'),text);await until(()=>Number(state().CommandsProcessed)>before);return state();}
async function check(name,fn){try{await fn();results.push({name,passed:true});}catch(e){results.push({name,passed:false,error:String(e)});}console.log(JSON.stringify(results.at(-1)));}
(async()=>{try{
 await until(()=>fs.existsSync(path.join(out,'settings-connection.json'))&&state().Version);const c=JSON.parse(fs.readFileSync(path.join(out,'settings-connection.json'))),call=p=>hostCall(c.pipe,c.token,'transfer.call',p);await call({action:'enable',enabled:true});await command('music-stop');await sleep(800);
 const file=path.join(out,'接收时长检查.txt');fs.writeFileSync(file,'fixture');
 const reduced=state().ReducedMotion==='true';const base=n=>reduced?Math.min(n,.16):n;
 await check('idle receiver entry and exit are each baseline 400ms / 1.2',async()=>{let s=await command('transfer-enter='+file);assert.equal(Number(s.Receiving),1);assert(Math.abs(Number(s.AnimationSeconds)-base(.4)/1.2)<.0001,JSON.stringify({seconds:s.AnimationSeconds,reduced}));await sleep(700);s=await command('transfer-leave');assert.equal(Number(s.Receiving),0);assert(Math.abs(Number(s.AnimationSeconds)-base(.4)/1.2)<.0001);});
 await check('ordinary music transition stays 560ms; transfer restore is 560ms / 1.2 once',async()=>{await sleep(700);let s=await command('music=play');assert(Math.abs(Number(s.AnimationSeconds)-base(.56))<.0001);await sleep(750);s=await command('transfer-enter='+file);assert(Math.abs(Number(s.AnimationSeconds)-base(.56)/1.2)<.0001);await sleep(700);s=await command('transfer-leave');assert(Math.abs(Number(s.AnimationSeconds)-base(.56)/1.2)<.0001);});
 await check('accepted drop grows with scaled 400ms timing and business state intact',async()=>{await command('music-stop');await sleep(700);await command('transfer-enter='+file);await sleep(700);const s=await command('transfer-drop='+file);assert.equal(Number(s.Receiving),2);assert(Math.abs(Number(s.AnimationSeconds)-base(.4)/1.2)<.0001);await until(async()=>{const r=await call({action:'list'});return r.entries.some(e=>e.state==='pending');});assert.equal(fs.readFileSync(file,'utf8'),'fixture');await command('transfer-reference');});
 }finally{fs.writeFileSync(path.join(out,'exit.request'),'exit');if(core.exitCode===null)await new Promise(r=>core.once('exit',r));fs.writeFileSync(path.join(out,'results.json'),JSON.stringify(results,null,2));console.log('Evidence: '+out);process.exitCode=results.some(r=>!r.passed)?1:0;}
})().catch(e=>{console.error(e);process.exitCode=1;});
