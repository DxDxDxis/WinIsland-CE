from pathlib import Path
import zipfile,json
root=Path(__file__).resolve().parent;out=root/'test-packages';out.mkdir(exist_ok=True)
base=root/'packages/community-base.wimod'
for name in ['community-timeout','community-cycle-a','community-cycle-b']:
 with zipfile.ZipFile(base) as src,zipfile.ZipFile(out/(name+'.wimod'),'w',zipfile.ZIP_DEFLATED) as dst:
  for item in src.infolist():
   data=src.read(item.filename)
   if item.filename=='mod.json':
    m=json.loads(data.decode('utf-8-sig'));m['id']=name
    if name=='community-timeout': m['providesServices']=[{'id':'org.winisland.example.math','version':2}]
    else:
     suffix=name[-1];other='b' if suffix=='a' else 'a';m['providesServices']=[{'id':'org.test.'+suffix,'version':1}];m['serviceDependencies']=[{'id':'org.test.'+other,'minVersion':1,'maxVersion':1,'provider':'community-cycle-'+other}]
    data=json.dumps(m,ensure_ascii=False).encode('utf-8')
   dst.writestr(item.filename,data)
