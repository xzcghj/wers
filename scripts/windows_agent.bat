@echo off
rem Runs in a visible window. Waits for a job file and then runs the WERS build and tests.
rem It only ever runs build_windows.bat from this folder. Close this window to stop it.
title WERS: budowa i testy (zamknij to okno, zeby zatrzymac)
cd /d "%~dp0"
if not exist logs mkdir logs
echo.
echo   WERS: budowa i testy na tym komputerze.
echo   To okno czeka na nowa wersje kodu, buduje ja w Visual Studio i uruchamia testy.
echo   Wyniki zapisuje w folderze logs. Zamknij okno, gdy skonczymy.
echo.
set /a COUNT=0
:loop
echo %DATE% %TIME%> logs\alive.txt
if exist run.job (
    echo %TIME% Nowe zadanie...
    move /y run.job current.job >nul
    call build_windows.bat
    del current.job >nul 2>nul
    echo %TIME% Gotowe. Czekam na kolejne zadanie.
)
if exist stop.flag (
    del stop.flag
    exit /b 0
)
timeout /t 4 /nobreak >nul
set /a COUNT+=1
rem Gives up by itself after about eight hours without being closed.
if %COUNT% GEQ 7200 exit /b 0
goto loop
