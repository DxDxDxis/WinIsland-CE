@echo off
chcp 65001 >nul
call "E:\WinIslandBuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cl /nologo /std:c++20 /EHsc /MT /O2 /utf-8 /LD "E:\aaAAx项目\WinIsland-社区版-大更新\release\1.2.7alpha-r1\plugins\media-fixture\source\tests\skin-example.cpp" /Fo"E:\aaAAx项目\WinIsland-社区版-大更新\release\1.2.7alpha-r1\plugins\media-fixture\built\skin.obj" /Fe"E:\aaAAx项目\WinIsland-社区版-大更新\release\1.2.7alpha-r1\plugins\media-fixture\built\media-fixture\bin\media-fixture.dll" /link /IMPLIB:"E:\aaAAx项目\WinIsland-社区版-大更新\release\1.2.7alpha-r1\plugins\media-fixture\built\skin.lib"
exit /b %errorlevel%