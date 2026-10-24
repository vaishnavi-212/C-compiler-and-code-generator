@echo off
cd /d "%~dp0"
echo Unblocking downloaded files ...
powershell -NoProfile -ExecutionPolicy Bypass -Command "Get-ChildItem -LiteralPath '%~dp0' -Recurse -File | Unblock-File" 2>nul
where python >nul 2>nul
if %errorlevel%==0 (python server.py) else (py server.py)
pause
