@echo off
setlocal
pushd "%~dp0.."
set "VSWHERE=C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%I"
call "%VSROOT%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
if not exist build mkdir build
cl /nologo /std:c++20 /utf-8 /EHsc /W4 /permissive- /DUNICODE /D_UNICODE /DNOMINMAX /O2 ^
  /Fo:build\obj\ /Fe:build\dropprobe.exe tests\dropprobe.cpp ^
  /link /SUBSYSTEM:CONSOLE user32.lib
popd
