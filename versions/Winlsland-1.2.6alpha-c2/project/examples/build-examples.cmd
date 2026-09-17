@echo off
call "E:\WinIslandBuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "E:\aaAAx项目\WinIsland-社区版-大更新\release\1.2.6alpha-c2-open-scene\source\examples"
cl /nologo /std:c++20 /EHsc /MT /O2 /utf-8 /LD example.cpp /Fo"E:\aaAAx项目\WinIsland-社区版-大更新\release\1.2.6alpha-c2-open-scene\examples\example-button\example.obj" /Fe"E:\aaAAx项目\WinIsland-社区版-大更新\release\1.2.6alpha-c2-open-scene\examples\example-button\example.dll" /link /IMPLIB:"E:\aaAAx项目\WinIsland-社区版-大更新\release\1.2.6alpha-c2-open-scene\examples\example-button\example.lib"
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /MT /O2 /utf-8 /DSECOND_EXAMPLE /LD example.cpp /Fo"E:\aaAAx项目\WinIsland-社区版-大更新\release\1.2.6alpha-c2-open-scene\examples\example-overlay\example.obj" /Fe"E:\aaAAx项目\WinIsland-社区版-大更新\release\1.2.6alpha-c2-open-scene\examples\example-overlay\example.dll" /link /IMPLIB:"E:\aaAAx项目\WinIsland-社区版-大更新\release\1.2.6alpha-c2-open-scene\examples\example-overlay\example.lib"
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /MT /O2 /utf-8 /LD fault.cpp /Fo"E:\aaAAx项目\WinIsland-社区版-大更新\release\1.2.6alpha-c2-open-scene\examples\fault-fixture\fault.obj" /Fe"E:\aaAAx项目\WinIsland-社区版-大更新\release\1.2.6alpha-c2-open-scene\examples\fault-fixture\example.dll" /link /IMPLIB:"E:\aaAAx项目\WinIsland-社区版-大更新\release\1.2.6alpha-c2-open-scene\examples\fault-fixture\example.lib"
exit /b %errorlevel%