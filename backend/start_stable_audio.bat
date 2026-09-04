@echo off
setlocal
set ROOT=%~dp0..
set RUNTIME=%LOCALAPPDATA%\Circat\CircatThought\StableAudioOpen
if not exist "%RUNTIME%\.venv\Scripts\python.exe" (
  echo Stable Audio Open is not installed yet.
  exit /b 1
)
powershell -NoProfile -Command "try { Invoke-WebRequest -UseBasicParsing http://127.0.0.1:8585/health -TimeoutSec 2 ^| Out-Null; exit 0 } catch { exit 1 }"
if errorlevel 1 start "Circat Stable Audio bridge" /min "%RUNTIME%\.venv\Scripts\python.exe" "%ROOT%\backend\stable_audio_bridge.py"
echo Stable Audio Open is running or starting.
