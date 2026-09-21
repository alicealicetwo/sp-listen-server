@echo off
REM ============================================================
REM  build.bat -- builds sp_listen.dll (x64) from sp_listen.cpp in this
REM  folder and installs it next to the game exe.
REM
REM  Run this from the "x64 Native Tools Command Prompt for VS"
REM  (Start menu -> Visual Studio -> Tools); only that shell knows cl.exe.
REM
REM  Install target: the folder in the SP_GAMEDIR environment variable
REM  (the game's Binaries\Win64). If it is not set, or this script already
REM  lives in the game folder, the DLL stays here.
REM
REM  Usage:   set SP_GAMEDIR=D:\Games\SuperPeople\BravoHotelGame\Binaries\Win64
REM           build.bat
REM ============================================================
setlocal
cd /d "%~dp0"

where cl >nul 2>&1
if errorlevel 1 (
    echo.
    echo [ERROR] cl.exe not found. Start this script from the
    echo         "x64 Native Tools Command Prompt for VS".
    echo.
    pause
    exit /b 1
)
if not exist sp_listen.cpp (
    echo [ERROR] sp_listen.cpp is not in this folder.
    pause
    exit /b 1
)

REM A running game keeps sp_listen.dll locked; the copy would fail silently.
tasklist /fi "imagename eq BravoHotelClient-Win64-Shipping.exe" 2>nul | find /i "BravoHotelClient" >nul
if not errorlevel 1 (
    echo.
    echo [ERROR] The game is still running -- sp_listen.dll is locked. Close it first.
    echo.
    pause
    exit /b 1
)

echo Building sp_listen.dll ...
cl /nologo /LD /O2 /EHsc /std:c++17 /DNDEBUG sp_listen.cpp /Fe:sp_listen.dll /link psapi.lib
if errorlevel 1 ( echo. & echo [ERROR] Build failed. & pause & exit /b 1 )
del /q sp_listen.obj sp_listen.exp sp_listen.lib 2>nul

if "%SP_GAMEDIR%"=="" (
    echo.
    echo [OK] Built: %~dp0sp_listen.dll
    echo      SP_GAMEDIR is not set -- copy the DLL next to BravoHotelClient-Win64-Shipping.exe yourself.
    pause
    exit /b 0
)
if /i "%~dp0"=="%SP_GAMEDIR%\" goto :done
if not exist "%SP_GAMEDIR%\BravoHotelClient-Win64-Shipping.exe" (
    echo.
    echo [WARN] No game exe in %SP_GAMEDIR% -- DLL stays here: %~dp0sp_listen.dll
    pause
    exit /b 0
)
copy /y sp_listen.dll "%SP_GAMEDIR%\sp_listen.dll" >nul
if errorlevel 1 ( echo. & echo [ERROR] Copy to game folder failed. & pause & exit /b 1 )
:done
echo.
echo [OK] Built and installed. Check the first line of sp_listen.log after the next start.
pause
endlocal
