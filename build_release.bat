@echo off
rem ---------------------------------------------------------------------------
rem HddGauge - shipped build (requireAdministrator) + windeployqt -> dist\
rem
rem Toolchain location: set QT_DIR / MINGW_DIR in your environment to point at
rem your own Qt 5.8 mingw53_32 kit.  The defaults below are the author's local
rem paths; they will not exist on your machine.
rem ---------------------------------------------------------------------------
setlocal

if not defined QT_DIR    set "QT_DIR=D:\Software\Qt\QT5.8\5.8\mingw53_32"
if not defined MINGW_DIR set "MINGW_DIR=D:\Software\Qt\QT5.8\Tools\mingw530_32"

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
if exist build-admin rmdir /s /q build-admin
mkdir build-admin
cd build-admin

qmake ..\HddGauge.pro
if errorlevel 1 exit /b 1

mingw32-make -j4
if errorlevel 1 exit /b 1

cd ..
if exist dist rmdir /s /q dist
mkdir dist
copy /y build-admin\release\HddGauge.exe dist\ >nul

windeployqt --release --no-translations --no-system-d3d-compiler --no-opengl-sw --no-quick-import --dir dist dist\HddGauge.exe
if errorlevel 1 exit /b 1

rem keep only the plugins the app actually uses (qico for the window icon,
rem qwindows as the platform plugin); drop the rest of the image formats
del /q dist\imageformats\qgif.dll dist\imageformats\qicns.dll dist\imageformats\qjpeg.dll ^
       dist\imageformats\qsvg.dll dist\imageformats\qtga.dll dist\imageformats\qtiff.dll ^
       dist\imageformats\qwbmp.dll dist\imageformats\qwebp.dll 2>nul
rmdir /s /q dist\iconengines 2>nul
del /q dist\Qt5Svg.dll 2>nul

echo.
echo [ok] deployable build in %~dp0dist
echo      HddGauge.exe will request administrator rights on launch.
endlocal
