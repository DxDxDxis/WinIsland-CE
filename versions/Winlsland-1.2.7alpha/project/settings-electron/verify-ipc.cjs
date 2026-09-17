const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const {hostCall}=require('./dist/transport');
const root=path.resolve('..'),dir=path.join(root,'verification/electron-live');
const credentials=JSON.parse(fs.readFileSync(path.join(dir,'settings-connection.json'),'utf8'));
const results=[];
const pause=ms=>new Promise(r=>setTimeout(r,ms));
async function request(command,params={},ok=true){const r=await hostCall(credentials.pipe,credentials.token,command,params);if(ok)assert.equal(r.ok,true,JSON.stringify(r));return r;}
async function check(name,fn){try{await fn();results.push({name,passed:true});console.log('PASS '+name);}catch(e){results.push({name,passed:false,error:String(e)});console.error('FAIL '+name+': '+e);throw e;}}
async function operation(command,params){const r=await request(command,params);for(let i=0;i<200;i++){await pause(50);const s=await request('mods.list');if(!s.busy&&s.completedOperation>=r.operation)return s;}throw Error('operation timeout');}
(async()=>{
 let original=await request('settings.read'),current=original;
 await check('read all 19 persisted fields',async()=>assert.equal(Object.keys(original.settings).length,19));
 await check('real capabilities and current monitor',async()=>{const c=await request('capabilities.read');assert(c.displays.length>0);assert.equal(c.settingsAnimation,false);assert.equal(c.version,'1.2.7alpha');});
 const values={resident:false,hideNative:false,seconds:5.5,fps:60,songSource:1,lyricSource:2,playerFilter:1,lyricApi:'https://lrclib.net',showFps:true,showPing:false,pingTarget:'127.0.0.1',topArcScale:1.1,islandZoom:1.3,widthRatio:1.1,heightRatio:.9,dpiCorrection:1.05,topAttach:false,radiusMode:1,monitorDevice:''};
 for(const [key,value] of Object.entries(values))await check('apply + read + disk '+key,async()=>{current=await request('settings.write',{revision:current.revision,patch:{[key]:value}});assert.equal(current.settings[key],value);const read=await request('settings.read');assert.equal(read.settings[key],value);const tags={resident:'Resident',hideNative:'HideNative',seconds:'DwellSeconds',fps:'FrameRate',songSource:'SongSource',lyricSource:'LyricSource',playerFilter:'PlayerFilter',lyricApi:'LyricApi',showFps:'ShowFps',showPing:'ShowPing',pingTarget:'PingTarget',topArcScale:'TopArcScale',islandZoom:'IslandZoom',widthRatio:'WidthRatio',heightRatio:'HeightRatio',dpiCorrection:'DpiCorrection',topAttach:'TopAttach',radiusMode:'RadiusMode',monitorDevice:'MonitorDevice'};const tag=tags[key],xml=fs.readFileSync(path.join(dir,'settings.xml'),'utf8');const written=xml.match(new RegExp('<'+tag+'>(.*?)</'+tag+'>'))[1];assert.equal(typeof value==='number'?Number(written):written,String(value)==='true'?'true':String(value)==='false'?'false':value);});
 for(const [key,value] of Object.entries({seconds:0,fps:12,islandZoom:.9,widthRatio:2,heightRatio:.5,dpiCorrection:2,topArcScale:2,songSource:5,lyricSource:8,playerFilter:6,radiusMode:4,lyricApi:'http://invalid',pingTarget:'not-an-ip',monitorDevice:'missing-display',resident:2}))await check('reject '+key,async()=>{const r=await request('settings.write',{revision:current.revision,patch:{[key]:value}},false);assert.equal(r.ok,false);assert.deepEqual((await request('settings.read')).settings,current.settings);});
 await check('revision conflict rejected',async()=>assert.equal((await request('settings.write',{revision:0,patch:{resident:true}},false)).code,'CONFLICT'));
 await check('wrong token rejected',async()=>assert.equal((await hostCall(credentials.pipe,'invalid','settings.read',{})).code,'AUTH'));
 await check('unknown command rejected',async()=>assert.equal((await request('file.delete',{},false)).code,'COMMAND'));
 await check('reset layout uses original defaults',async()=>{current=await request('layout.reset',{revision:current.revision});assert.equal(current.settings.islandZoom,1.5);assert.equal(current.settings.topArcScale,1);assert.equal(current.settings.topAttach,true);});
 await check('restore initial settings',async()=>{current=await request('settings.write',{revision:current.revision,patch:original.settings});assert.deepEqual(current.settings,original.settings);});
 const fixture=path.join(root,'plugins/host-scene/built/scene-fixture.wimod');
 await check('real wimod import',async()=>{const s=await operation('mods.import',{path:fixture});assert.equal(s.operationError,'');assert(s.mods.some(m=>m.id==='scene-fixture'&&m.valid&&!m.enabled));});
 await check('duplicate ID import rejected',async()=>assert((await operation('mods.import',{path:fixture})).operationError.includes('重复')));
 for(const action of ['enable','disable','reload','unload','enable'])await check('DLL lifecycle '+action,async()=>{const s=await operation('mods.action',{action,id:'scene-fixture',cascade:false});assert.equal(s.operationError,'');const m=s.mods.find(m=>m.id==='scene-fixture');assert.equal(m.enabled,['enable','reload'].includes(action));assert(fs.existsSync(path.join(dir,'mods/scene-fixture.wimod')));});
 await check('fixture log and callback resources',async()=>{const s=await request('mods.list');assert(s.resources.some(r=>r.owner==='scene-fixture'));assert((await request('mods.log',{id:'scene-fixture'})).text.length>0);});
 await check('invalid package does not leave imported file',async()=>{const f=path.join(dir,'invalid.wimod');fs.writeFileSync(f,'not zip');const s=await operation('mods.import',{path:f});assert(s.operationError);assert.equal(s.mods.length,1);});
 fs.writeFileSync(path.join(dir,'expected-restart.json'),JSON.stringify(current.settings));
})().catch(e=>{console.error(e);process.exitCode=1;}).finally(()=>fs.writeFileSync(path.join(root,'verification/ipc-results.json'),JSON.stringify(results,null,2)));

