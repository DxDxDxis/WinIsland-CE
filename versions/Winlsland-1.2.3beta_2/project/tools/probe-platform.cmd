@echo off
chcp 65001 >nul
call "E:\WinIslandBuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "E:\aaAAx项目\1.2.3beta社区版（重构底层架构）\tests"
cl /nologo /std:c++20 /EHsc /MT /O2 /utf-8 /Fo"E:\aaAAx项目\1.2.3beta社区版（重构底层架构）\tools\obj\platform-probe.obj" /Fe"E:\aaAAx项目\1.2.3beta社区版（重构底层架构）\tests\bin\platform-probe.exe" platform-probe.cpp "E:\aaAAx项目\1.2.3beta社区版（重构底层架构）\tools\obj\core.obj" "E:\aaAAx项目\1.2.3beta社区版（重构底层架构）\tools\obj\monitor.obj" /link /SUBSYSTEM:WINDOWS user32.lib shell32.lib shlwapi.lib ole32.lib windowsapp.lib dwrite.lib bcrypt.lib xmllite.lib psapi.lib dxgi.lib advapi32.lib