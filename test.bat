@echo off
setlocal
pushd "%~dp0"

set "VSWHERE=C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%I"
if not defined VSROOT ( echo VS not found & popd & exit /b 1 )
call "%VSROOT%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul

if not exist build mkdir build
if not exist build\obj mkdir build\obj

cl /nologo /std:c++20 /utf-8 /EHsc /W4 /permissive- /DUNICODE /D_UNICODE /DNOMINMAX /O2 /MT ^
  /Fo:build\obj\ ^
  tests\extract_test.cpp src\Common.cpp src\Html.cpp src\Extract.cpp src\Json.cpp src\SiteAdapter.cpp ^
  /Fe:build\extract_test.exe ^
  /link user32.lib
if errorlevel 1 ( echo TEST BUILD FAILED & popd & exit /b 1 )

cl /nologo /std:c++20 /utf-8 /EHsc /W4 /permissive- /DUNICODE /D_UNICODE /DNOMINMAX /O2 /MT ^
  /Fo:build\obj\ ^
  tests\adapter_test.cpp src\Common.cpp src\Html.cpp src\Extract.cpp src\Json.cpp src\SiteAdapter.cpp ^
  /Fe:build\adapter_test.exe ^
  /link user32.lib
if errorlevel 1 ( echo ADAPTER TEST BUILD FAILED & popd & exit /b 1 )

cl /nologo /std:c++20 /utf-8 /EHsc /W4 /permissive- /DUNICODE /D_UNICODE /DNOMINMAX /O2 /MT ^
  /Fo:build\obj\ ^
  tests\txt_test.cpp src\Common.cpp src\Html.cpp src\Extract.cpp src\Json.cpp src\TxtBook.cpp src\Export.cpp src\S2T.cpp ^
  /Fe:build\txt_test.exe ^
  /link user32.lib
if errorlevel 1 ( echo TXT TEST BUILD FAILED & popd & exit /b 1 )

cl /nologo /std:c++20 /utf-8 /EHsc /W4 /permissive- /DUNICODE /D_UNICODE /DNOMINMAX /O2 /MT ^
  /Fo:build\obj\ ^
  tests\droptest.cpp src\DropTarget.cpp ^
  /Fe:build\droptest.exe ^
  /link user32.lib shell32.lib ole32.lib
if errorlevel 1 ( echo DROP TEST BUILD FAILED & popd & exit /b 1 )

echo === SITEADAPTER SELFTEST ===
build\adapter_test.exe
if exist testdata\twkan_book_index.html (
  echo === TOC ===
  build\extract_test.exe testdata\twkan_book_index.html toc https://twkan.com/book/67581/index.html
  echo === CHAPTER ===
  build\extract_test.exe testdata\twkan_chapter_sample.html chapter
) else (
  echo === TOC / CHAPTER: SKIP ^(testdata samples not included in repo^) ===
)
echo === WATERMARK ===
build\extract_test.exe tests\watermark_test.html chapter
echo === TXT SELFTEST ===
build\txt_test.exe --selftest
echo === TXT SAMPLE UTF8 ===
build\txt_test.exe tests\sample_utf8.txt
echo === TXT SAMPLE GBK ===
build\txt_test.exe tests\sample_gbk.txt
echo === DROPTARGET SELFTEST ===
build\droptest.exe
popd
exit /b 0
