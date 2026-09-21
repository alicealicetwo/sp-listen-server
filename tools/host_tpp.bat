@echo off
REM ============================================================
REM  host_tpp.bat -- host a round in TPP (third person).
REM
REM  Place this next to BravoHotelClient-Win64-Shipping.exe together with
REM  dxgi.dll, DList.ini and sp_listen.dll.
REM
REM    host_tpp.bat            -> headless: -nullrhi, no renderer, no sound
REM    host_tpp.bat quiet      -> tiny window, no sound
REM    host_tpp.bat window     -> normal small window (the host can play)
REM
REM  Patch switches (SPARGS):
REM    -tpp        force the view type ([vt] lines in sp_listen.log)
REM    -solo       read by the lobby tool, advertises SOLO + this view type
REM    -loadout    apply gold/level/outfit from loadouts.txt (backend)
REM    -cmdfile    read admin commands from sp_listen_cmd.txt next to the exe
REM  Add -rpcnames for a diagnostic round (every RPC logged once).
REM
REM  Check after start: first line of sp_listen.log is the version you
REM  built, then "[i] -tpp erkannt", and "[ws] Streaming" counts up.
REM ============================================================
setlocal
cd /d "%~dp0"
set "EXE=%~dp0BravoHotelClient-Win64-Shipping.exe"
if not exist "%EXE%" ( echo Game exe not found next to this script. & pause & exit /b 1 )

set "MAP=/Game/BravoHotel/Maps/OrbIsland/LV-OrbIsland?listen?AutoStart=1?StartingPlayerCountRate=0.000000?StartingTimeSecond=30"
set "GAMEARGS=-ApiPhase=dev2s -ServicePlatform=Steam -IgnoreCatalogue -log"
set "SPARGS=-loadout -solo -tpp -cmdfile"

if /i "%~1"=="quiet" (
    set "VIDEO=-windowed -ResX=640 -ResY=360 -nosound -nosplash"
) else if /i "%~1"=="window" (
    set "VIDEO=-windowed -ResX=800 -ResY=450 -nosplash"
) else (
    set "VIDEO=-nullrhi -nosound -unattended -nosplash"
)

echo Starting host (tpp): %MAP%
echo Switches: %SPARGS%
echo Video:    %VIDEO%
start "" "%EXE%" %MAP% %GAMEARGS% %SPARGS% %VIDEO%
endlocal
