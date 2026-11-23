@echo off
setlocal
pushd "%~dp0"

set "WV2=%~dp0.deps\wv2\build\native"
if not exist "%WV2%\include\WebView2.h" (
  echo WebView2 SDK not found at %WV2%
  echo Run: curl -sSL -o .deps\wv2.nupkg https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/1.0.2903.40
  echo Then unzip it into .deps\wv2
  popd & exit /b 1
)

set "VSWHERE=C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" ( echo Visual Studio Installer not found. & popd & exit /b 1 )

for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%I"
if not defined VSROOT ( echo Visual C++ build tools not found. & popd & exit /b 1 )

call "%VSROOT%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
if errorlevel 1 ( popd & exit /b 1 )

if not exist build mkdir build
if not exist build\obj mkdir build\obj

rc /nologo /c 65001 /Fo build\app.res src\app.rc
if errorlevel 1 ( echo RESOURCE BUILD FAILED & popd & exit /b 1 )

rem miniz (third_party, plain C) is compiled on its own, without /W4
cl /nologo /c /O2 /GL /Gy /MT /W3 /D_CRT_SECURE_NO_WARNINGS /DMINIZ_NO_ZLIB_COMPATIBLE_NAMES ^
  /Fo:build\obj\miniz.obj third_party\miniz\miniz.c
if errorlevel 1 ( echo MINIZ BUILD FAILED & popd & exit /b 1 )

cl /nologo /std:c++20 /utf-8 /EHsc /W4 /permissive- /DUNICODE /D_UNICODE /DNOMINMAX /O2 /GL /Gy /MT ^
  /I"%WV2%\include" ^
  /Fo:build\obj\ ^
  src\Common.cpp src\Json.cpp src\Html.cpp src\Http.cpp src\Extract.cpp src\Store.cpp ^
  src\TxtBook.cpp src\Zip.cpp src\EpubBook.cpp src\MobiBook.cpp src\Export.cpp src\S2T.cpp src\DropTarget.cpp src\SiteAdapter.cpp src\App.cpp src\main.cpp ^
  build\obj\miniz.obj ^
  /Fe:build\Reader.exe ^
  /link /SUBSYSTEM:WINDOWS /LTCG /OPT:REF /OPT:ICF ^
  build\app.res ^
  "%WV2%\x64\WebView2LoaderStatic.lib" ^
  user32.lib gdi32.lib shell32.lib ole32.lib oleaut32.lib advapi32.lib winhttp.lib shlwapi.lib version.lib comdlg32.lib
if errorlevel 1 ( echo BUILD FAILED & popd & exit /b 1 )

if not exist dist mkdir dist
copy /y build\Reader.exe dist\Reader.exe >nul
if errorlevel 1 ( echo COPY FAILED - close Reader.exe first, then rerun build.bat & popd & exit /b 1 )
if not exist dist\web mkdir dist\web
xcopy /e /i /y /q web dist\web >nul
echo BUILD OK -^> dist\Reader.exe
popd
exit /b 0
