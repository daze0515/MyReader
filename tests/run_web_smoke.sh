#!/usr/bin/env bash
# 用法： bash tests/run_web_smoke.sh   （在 reader/ 目录下执行）
set -u
cd "$(dirname "$0")/.."
EDGE="/c/Program Files (x86)/Microsoft/Edge/Application/msedge.exe"
PORT=9334
PROFILE=C:/Windows/Temp/edge-cdp-smoke
rm -rf /c/Windows/Temp/edge-cdp-smoke
"$EDGE" --headless=new --disable-gpu --no-first-run --no-default-browser-check \
  --user-data-dir="$PROFILE" --window-size=1200,900 --remote-debugging-port=$PORT about:blank >/dev/null 2>&1 &
for i in $(seq 1 40); do
  curl -s --max-time 1 "http://127.0.0.1:$PORT/json/version" >/dev/null 2>&1 && break
  sleep 0.5
done
SMOKE_PORT=$PORT SMOKE_PAGE="file:///D:/codexproject/reader/tests/web_smoke.html" node tests/cdp_smoke.mjs
powershell -NoProfile -Command "Get-CimInstance Win32_Process -Filter \"Name='msedge.exe'\" | Where-Object { \$_.CommandLine -like '*edge-cdp-smoke*' } | ForEach-Object { Stop-Process -Id \$_.ProcessId -Force }" >/dev/null 2>&1
