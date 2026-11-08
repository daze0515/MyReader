@echo off
setlocal
pushd "%~dp0.."
set "VSWHERE=C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%I"
call "%VSROOT%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
if not exist build mkdir build
cl /nologo /std:c++20 /utf-8 /EHsc /W4 /permissive- /DUNICODE /D_UNICODE /DNOMINMAX /O2 /MT ^
  /Fo:build\obj\ ^
  tests\http_test.cpp src\Common.cpp src\Http.cpp src\Extract.cpp src\Html.cpp src\Json.cpp src\SiteAdapter.cpp ^
  /Fe:build\http_test.exe /link user32.lib winhttp.lib advapi32.lib
if errorlevel 1 ( echo HTTP TEST BUILD FAILED & popd & exit /b 1 )
echo HTTP TEST BUILD OK
popd
exit /b 0
