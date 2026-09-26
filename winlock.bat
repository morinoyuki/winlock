@echo off
setlocal enabledelayedexpansion
rem ============================================================
rem  winlock.bat - Windows-side helper for winlock-guard.exe
rem  Put this file in the SAME folder as:
rem      winlock-guard.exe
rem      winlock.conf
rem  Usage:
rem      winlock.bat                  interactive menu
rem      winlock.bat start|stop|restart|status|monitor|install|uninstall
rem  ASCII only, locale-safe.
rem ============================================================

set "NAME=winlock-guard"
set "DIR=%~dp0"
set "EXE=%DIR%%NAME%.exe"
set "CONF=%DIR%winlock.conf"
set "STATUS=%DIR%winlock-status.txt"
set "RC=1"

rem Make the script independent of the launch working directory.
cd /d "%DIR%" >nul 2>&1

if /i "%~1"=="start" goto o_start
if /i "%~1"=="stop" goto o_stop
if /i "%~1"=="restart" goto o_restart
if /i "%~1"=="status" goto o_status
if /i "%~1"=="monitor" goto o_monitor
if /i "%~1"=="install" goto o_install
if /i "%~1"=="uninstall" goto o_uninstall
if "%~1"=="" goto o_menu
echo Unknown command: %~1
goto :eof

:o_foot
exit /b 0

:o_menu
for /l %%i in (1,1,1000000) do (
  echo.
  echo  ================ winlock helper ================
  echo    1. start guard        4. live monitor
  echo    2. stop guard         5. install + autostart
  echo    3. status             6. uninstall
  echo    0. exit
  echo  ================================================
  choice /c 1234560 /n /m " Select: "
  if errorlevel 7 goto :eof
  set "EL=!errorlevel!"
  if "!EL!"=="6" call :o_uninstall
  if "!EL!"=="5" call :o_install
  if "!EL!"=="4" call :o_monitor
  if "!EL!"=="3" call :o_status
  if "!EL!"=="2" call :o_stop
  if "!EL!"=="1" call :o_start
)
goto :eof

:o_start
if exist "%EXE%" goto o_start2
echo Guard not found: %EXE%
echo Keep winlock.bat together with %NAME%.exe and winlock.conf
goto :eof
:o_start2
call :is_running
if "%RC%"=="0" goto o_start3
powershell -NoProfile -Command "Start-Process '%~dp0%NAME%.exe'" >nul 2>&1
echo Guard started. Current status:
call :o_status
goto :eof
:o_start3
echo Guard is already running. Stop first, then start, to reload config.
goto :eof

:o_stop
call :is_running
if not "%RC%"=="0" goto o_stop2
powershell -NoProfile -Command "Stop-Process -Name '%NAME%' -Force" >nul 2>&1
echo Guard stopped.
goto :eof
:o_stop2
echo Guard is not running.
goto :eof

:o_restart
call :o_stop
ping -n 2 127.0.0.1 >nul
call :o_start
goto :eof

:o_status
call :is_running
if "%RC%"=="0" goto o_status2
echo Guard: NOT running. Use start to launch.
goto :eof
:o_status2
echo Guard: running
if not exist "%STATUS%" goto o_status3
type "%STATUS%"
findstr "focused=1" "%STATUS%" >nul && echo [game focused - Win key blocked]
findstr "focused=0" "%STATUS%" >nul && echo [game not focused - Win key normal]
goto :eof
:o_status3
echo Status file not created yet. It appears about 1s after start.
goto :eof

:o_install
set "DST=%LOCALAPPDATA%\winlock"
powershell -NoProfile -Command "Stop-Process -Name '%NAME%' -Force -ErrorAction SilentlyContinue" >nul 2>&1
if not exist "%DST%" mkdir "%DST%"
copy /y "%EXE%" "%DST%\%NAME%.exe" >nul
copy /y "%CONF%" "%DST%\winlock.conf" >nul
set "AUTO=%APPDATA%\Microsoft\Windows\Start Menu\Programs\Startup\winlock-autostart.bat"
> "%AUTO%" echo @echo off
>>"%AUTO%" echo start "" /min "%DST%\winlock-guard.exe"
powershell -NoProfile -Command "Start-Process '"%DST%\winlock-guard.exe"'" >nul 2>&1
echo.
echo  Installed to:   %DST%
echo  Autostart:      %AUTO%
echo  Guard started from the install folder.
goto :eof

:o_uninstall
powershell -NoProfile -Command "Stop-Process -Name '%NAME%' -Force -ErrorAction SilentlyContinue" >nul 2>&1
set "AUTO=%APPDATA%\Microsoft\Windows\Start Menu\Programs\Startup\winlock-autostart.bat"
if exist "%AUTO%" del "%AUTO%"
echo Guard stopped, autostart removed.
goto :eof

:o_monitor
echo Live monitor, Ctrl+C to quit
for /l %%i in (1,1,1000000) do (
  call :is_running
  if not "!RC!"=="0" ( echo [Guard stopped] & goto :eof )
  cls
  call :o_status
  ping -n 2 127.0.0.1 >nul
)
goto :eof

:is_running
powershell -NoProfile -Command "if (Get-Process -Name '%NAME%' -ErrorAction SilentlyContinue) { exit 0 } else { exit 1 }" >nul 2>&1
set "RC=%errorlevel%"
exit /b