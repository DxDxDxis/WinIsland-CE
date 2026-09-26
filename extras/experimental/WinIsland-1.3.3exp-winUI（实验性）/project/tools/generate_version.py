from pathlib import Path
import json,re
r=Path(__file__).resolve().parents[1];v=json.loads((r/'product-version.json').read_text('utf-8-sig'))
num=v['numeric'].replace('.',',');display=v['display'];exe=v['executable']
(r/'source/src/product_version.h').write_text('#pragma once\n#define WI_VERSION_W L"'+display+'"\n#define WI_BUILD_ID "'+v['buildId']+'"\n',encoding='utf-8')
for name,filename in [('product_version.rcinc',exe),('settings_version.rcinc','WinIslandSettings.exe')]:
 (r/'source/src'/name).write_text('''1 VERSIONINFO
FILEVERSION %s
PRODUCTVERSION %s
FILETYPE VFT_APP
BEGIN
 BLOCK "StringFileInfo"
 BEGIN
  BLOCK "040904B0"
  BEGIN
   VALUE "FileVersion", "%s"
   VALUE "ProductVersion", "%s"
   VALUE "ProductName", "WinIsland 社区版（WinUI 实验性）"
   VALUE "FileDescription", "WinIsland %s 社区版"
   VALUE "OriginalFilename", "%s"
  END
 END
 BLOCK "VarFileInfo"
 BEGIN
  VALUE "Translation", 0x0409, 1200
 END
END
'''%(num,num,display,display,display,filename),encoding='utf-8')
for rel in ['source/src/app.manifest','settings-winui/app.manifest']:
 p=r/rel;s=p.read_text('utf-8-sig');s=re.sub(r'(<assemblyIdentity version=")[^"]+',r'\g<1>'+v['numeric'],s);p.write_text(s,encoding='utf-8')
print(display,v['numeric'])
