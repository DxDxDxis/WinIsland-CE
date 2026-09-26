const {_electron:electron}=require('@playwright/test');
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict'),{spawn,execFileSync}=require('node:child_process');
const {hostCall}=require('./dist/transport');
const root=path.resolve(__dirname,'..'),out=path.join(root,'verification','clipboard-paint-'+Date.now()),data=path.join(out,'data');fs.mkdirSync(data,{recursive:true});
const sleep=ms=>new Promise(r=>setTimeout(r,ms));
const until=async fn=>{for(let n=0;n<150;n++){if(await fn())return;await sleep(100);}throw Error('timed out');};
let core,app,backup;
function native(args=[]){return JSON.parse(execFileSync('powershell.exe',['-NoProfile','-ExecutionPolicy','Bypass','-File',path.join(root,'verification/native-clipboard-control.ps1'),'-CorePid',String(core.pid),...args],{windowsHide:true,encoding:'utf8'}));}
(async()=>{try{
 core=spawn(path.join(root,'build-1.3.3beta/WinIsland-1.3.3beta.exe'),['--verify',data],{cwd:data,windowsHide:true});
 await until(()=>fs.existsSync(path.join(data,'settings-connection.json')));const c=JSON.parse(fs.readFileSync(path.join(data,'settings-connection.json')));
 const call=(action,params={})=>hostCall(c.pipe,c.token,'clipboard.call',{action,...params},undefined,15000);
 app=await electron.launch({executablePath:path.join(root,'release/settings/WinIslandSettings.exe'),args:['--pipe='+c.pipe,'--token='+c.token,'--core-pid='+core.pid,'--session-dir='+data]});
 backup=await app.evaluate(async({clipboard})=>Promise.all((await clipboard.read()).map(item=>Promise.all(item.types.map(async type=>{const value=await item.getType(type);return {type,bytes:value instanceof Blob?Buffer.from(await value.arrayBuffer()).toString('base64'):null,value:value instanceof Blob?null:value};})))));
 await app.evaluate(({clipboard})=>clipboard.writeText('原生编辑绘制回归测试 Native editor 123\n第二行：中文 / Unicode 😀'));
 await until(async()=>(await call('list')).entries?.length>0);native(['-Action','800']);await sleep(1100);
 const first=native(['-InspectPaint','-Screenshot',path.join(out,'editor.png')]);fs.writeFileSync(path.join(out,'paint.json'),JSON.stringify(first,null,2));console.log(first);
 assert(first.paint.backgroundFraction>.8,'editor background is missing / stale paint');assert(first.paint.textPixels>30,'editor text is not painted');assert.equal(first.editorLines,2,'LF must display as two lines');
 for(let i=0;i<3;i++){native(['-Action','825']);await sleep(70);native(['-Action','800']);await sleep(800);const paint=native(['-InspectPaint']);assert(paint.paint.backgroundFraction>.8);assert(paint.paint.textPixels>30);}
 const original=(await call('list')).entries[0];
 const initial=(await call('get',{id:original.id})).entry.text;
 const typed=native(['-ClickEditor','-TypeText',' 实际键盘输入 XYZ']);assert(typed.editorFocused,'real click must focus EDIT');native(['-Action','820']);await until(async()=>(await call('get',{id:original.id})).entry.text===initial+' 实际键盘输入 XYZ');
 native(['-Screenshot',path.join(out,'editor-after-real-input.png')]);
 const longText=Array.from({length:80},(_,i)=>'第 '+i+' 行 Native Unicode 😀').join('\n');
 const fixture=path.join(out,'long-text.txt');fs.writeFileSync(fixture,longText);native(['-TextFile',fixture]);assert.equal(native().editorLines,80);native(['-ScrollLines','40']);assert(native().firstLine>=30,'long text scroll must move');native(['-Action','820']);await until(async()=>(await call('get',{id:original.id})).entry.text===longText);
 native(['-CopySelection']);assert.equal(await app.evaluate(({clipboard})=>clipboard.readText()),longText.replace(/\n/g,'\r\n'));await sleep(150);assert.equal((await call('list')).entries.length,1,'selection copy must not recapture');
 await app.evaluate(({clipboard})=>clipboard.writeText('粘贴第一行\n粘贴第二行'));native(['-Paste']);assert.equal(native().editorLines,2,'pasted LF must display as two lines');
 fs.writeFileSync(path.join(out,'result.txt'),'PASS real mouse hit/focus + SendInput typing/save, real screen text, LF display/paste/save, scrolling, selection copy and repeated transitions');
 }finally{
 if(core&&core.exitCode===null){fs.writeFileSync(path.join(data,'exit.request'),'exit');await Promise.race([new Promise(r=>core.once('exit',r)),sleep(10000)]);if(core.exitCode===null)core.kill();}
 if(app){if(backup)await app.evaluate(async({clipboard,ClipboardItem},values)=>{if(!values.length){clipboard.clear();return;}await clipboard.write(values.map(items=>new ClipboardItem(Object.fromEntries(items.map(x=>[x.type,x.bytes!==null?new Blob([Buffer.from(x.bytes,'base64')],{type:x.type}):x.value])))));},backup).catch(()=>{});await app.close().catch(()=>{});}
 console.log('Evidence:',out);
 }})().catch(e=>{fs.writeFileSync(path.join(out,'failure.txt'),String(e));console.error(e);process.exitCode=1;});
