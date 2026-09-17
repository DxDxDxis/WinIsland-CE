@echo off
chcp 65001 >nul
call "E:\WinIslandBuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "E:\aaAAx项目\1.2.3beta社区版（重构底层架构）\tests"
cl /nologo /EHsc /std:c++20 /utf-8 /MT /Fexml-probe.exe xml-probe.cpp /link shlwapi.lib ole32.lib xmllite.lib
if errorlevel 1 exit /b 1
xml-probe.exe