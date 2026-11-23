@echo off
rem Double-click: build + test, output goes to build.log / test.log
pushd "%~dp0"
echo [1/2] build.bat ...  (close Reader.exe first)
call build.bat > build.log 2>&1
echo [2/2] test.bat ...
call test.bat > test.log 2>&1
echo.
echo Done. Logs: build.log / test.log
popd
pause
