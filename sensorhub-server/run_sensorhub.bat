@echo off
setlocal
cd /d "%~dp0"
py -m waitress --threads=8 --listen=0.0.0.0:8000 app:app
endlocal
