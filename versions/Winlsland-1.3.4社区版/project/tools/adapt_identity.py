from pathlib import Path
import json,re
r=Path.cwd()
v={'display':'1.3.3exp-winUI（实验性）','numeric':'1.3.3.0','buildId':'1.3.3exp-winUI-clipboard-20260920','executable':'WinIsland-1.3.3exp-winUI（实验性）.exe','buildDirectory':'build-1.3.3exp-winUI'}
(r/'product-version.json').write_text(json.dumps(v,ensure_ascii=False,indent=2),encoding='utf-8')
def edit(rel,fn):
 p=r/rel;p.write_text(fn(p.read_text('utf-8-sig')),encoding='utf-8-sig' if p.suffix=='.ps1' else 'utf-8')
edit('source/src/core.h',lambda s:re.sub(r'inline constexpr wchar_t Version\[\].*?;\ninline constexpr char BuildId\[\].*?;', '#include "product_version.h"\ninline constexpr wchar_t Version[] = WI_VERSION_W;\ninline constexpr char BuildId[] = WI_BUILD_ID;',s))
edit('source/src/app.cpp',lambda s:s.replace('L"WinIsland 1.3.2alpha-winUI（实验性）"','(L"WinIsland "+std::wstring(Version)+L" 社区版").c_str()',1).replace('"\\nVersion=1.3.3beta\\nIdle="','"\\nVersion=" << utf8(Version) << "\\nIdle="').replace('L"WinIsland 1.3.2alpha-winUI（实验性）"','(L"WinIsland "+std::wstring(wi::Version)+L" 社区版").c_str()'))
edit('source/src/installation_ui.inc',lambda s:s.replace('L"WinIsland 1.3.2alpha-winUI（实验性）"','L"WinIsland "+std::wstring(Version)+L" 社区版"'))
edit('source/src/app.rc',lambda s:re.sub(r'1 VERSIONINFO.*', '#include "product_version.rcinc"\n',s,flags=re.S))
edit('settings-winui/app.h',lambda s:s.replace('#include "clients.h"','#include "clients.h"\n#include "../source/src/product_version.h"').replace('L"版本：1.3.2alpha-winUI（实验性）"','hstring(L"版本：")+WI_VERSION_W').replace('WinIsland · 设置','WinIsland · 设置（实验性）'))
edit('source/build.ps1',lambda s:s.replace("$ErrorActionPreference='Stop'", "$ErrorActionPreference='Stop'\n$branch=Split-Path $PSScriptRoot\n& python (Join-Path $branch 'tools/generate_version.py')\nif($LASTEXITCODE){throw 'Version generation failed'}\n$product=Get-Content (Join-Path $branch 'product-version.json') -Raw | ConvertFrom-Json\n$exeName=$product.executable",1).replace('WinIsland-1.3.2alpha.exe','$exeName').replace("(Join-Path $OutputDirectory '$exeName')",'(Join-Path $OutputDirectory $exeName)'))
edit('build-release.ps1',lambda s:s.replace("$ErrorActionPreference='Stop'", "$ErrorActionPreference='Stop'\n$product=Get-Content (Join-Path $PSScriptRoot 'product-version.json') -Raw | ConvertFrom-Json\n$buildName=$product.buildDirectory\n$exeName=$product.executable",1).replace('build-1.3.2alpha','$buildName').replace('WinIsland-1.3.2alpha.exe','$exeName').replace("'$buildName/settings'",'"$buildName/settings"'))
edit('settings-winui/build.ps1',lambda s:s.replace("$branch=Split-Path $PSScriptRoot", "$branch=Split-Path $PSScriptRoot\n& python (Join-Path $branch 'tools/generate_version.py')\nif($LASTEXITCODE){throw 'Version generation failed'}",1).replace('cl /nologo /std:c++20','rc /nologo /c 65001 /fo "$obj\\settings.res" "$PSScriptRoot\\app.rc"\nif errorlevel 1 exit /b 1\ncl /nologo /std:c++20').replace('"$PSScriptRoot\\main.cpp" /link','"$PSScriptRoot\\main.cpp" "$obj\\settings.res" /link'))
(r/'settings-winui/app.rc').write_text('#include <windows.h>\n#include "../source/src/settings_version.rcinc"\n',encoding='utf-8')
print('Version call sites migrated; plugin ABI and compatibility ranges unchanged')
