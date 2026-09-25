@echo off
rem ---------------------------------------------------------------------------
rem HddGauge - development build (asInvoker, no UAC prompt)
rem   output: build\release\HddGauge.exe
rem
rem This is the same manifest the shipped build uses now, so dev and release
rem differ only in whether windeployqt has run.
rem
rem Toolchain location: set QT_DIR / MINGW_DIR in your environment to point at
rem your own Qt 5.8 mingw53_32 kit, e.g.
rem     setx QT_DIR    "C:\Qt\5.8\mingw53_32"
rem     setx MINGW_DIR "C:\Qt\Tools\mingw530_32"
rem The fallbacks below are the stock Qt installer layout.
rem ---------------------------------------------------------------------------
setlocal

if not defined QT_DIR    set "QT_DIR=C:\Qt\5.8\mingw53_32"
if not defined MINGW_DIR set "MINGW_DIR=C:\Qt\Tools\mingw530_32"

set "PATH=%QT_DIR%\bin;%MINGW_DIR%\bin;%PATH%"

where qmake >nul 2>nul
if errorlevel 1 (
    echo [error] qmake not found on PATH.
    echo         Point QT_DIR at your Qt 5.8 mingw53_32 kit, for example:
    echo             set "QT_DIR=C:\Qt\5.8\mingw53_32"
    echo             set "MINGW_DIR=C:\Qt\Tools\mingw530_32"
    exit /b 1
)

cd /d "%~dp0"
if not exist build mkdir build
cd build

qmake ..\HddGauge.pro
if errorlevel 1 exit /b 1

mingw32-make -j4
if errorlevel 1 exit /b 1

echo.
echo [ok] %~dp0build\release\HddGauge.exe
echo      run:  HddGauge.exe --selftest
echo      shot: HddGauge.exe --screenshot out.png --shot-delay 4000
endlocal
