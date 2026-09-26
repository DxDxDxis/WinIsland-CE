@echo off
chcp 65001 >nul
call "E:\WinIslandBuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cl /nologo /std:c++20 /EHsc /MT /O2 /utf-8 /LD "E:\aaAAx项目\新版本\1.2.7alpha\source\tests\scene-fixture.cpp" /Fo"E:\aaAAx项目\新版本\1.2.7alpha\plugins\host-scene\built\scene-fixture.obj" /Fe"E:\aaAAx项目\新版本\1.2.7alpha\plugins\host-scene\built\scene-fixture\bin\example.dll" /link /IMPLIB:"E:\aaAAx项目\新版本\1.2.7alpha\plugins\host-scene\built\scene-fixture.lib"
exit /b %errorlevel%