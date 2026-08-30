@echo off
setlocal
cd /d "%~dp0"

if exist "%USERPROFILE%\.platformio\penv\Scripts\python.exe" (
  "%USERPROFILE%\.platformio\penv\Scripts\python.exe" "lib\CoreFSM\tools\corefsm_studio.py"
  goto :end
)

where py >nul 2>nul
if not errorlevel 1 (
  py -3 "lib\CoreFSM\tools\corefsm_studio.py"
  goto :end
)

where python >nul 2>nul
if not errorlevel 1 (
  python "lib\CoreFSM\tools\corefsm_studio.py"
  goto :end
)

echo No se ha encontrado Python. Instala PlatformIO o Python 3 y vuelve a intentarlo.
pause

:end
endlocal
