@echo off
rem Copies the WERS plugins into the standard VST3 folder of Windows, which needs administrator rights.
rem Windows asks for permission once. After that every DAW finds the plugins in their usual place.
net session >nul 2>&1
if errorlevel 1 (
    powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
    exit /b 0
)
setlocal
set "HERE=%~dp0"
set "TARGET=%CommonProgramFiles%\VST3"
if not exist "%TARGET%" mkdir "%TARGET%"
call :install "WERS.vst3"
call :install "WERS Barwa.vst3"
call :install "WERS Refren.vst3"
echo W DAW uruchom ponowne skanowanie wtyczek.
pause
endlocal
exit /b 0

:install
if not exist "%HERE%%~1" (
    echo Nie widze folderu %~1 obok tego pliku.
    exit /b 1
)
tasklist /m "%~1" 2>nul | findstr /i ".exe" >nul
if not errorlevel 1 (
    echo %~1 jest wczytany w DAW. Zamknij DAW i uruchom ten plik jeszcze raz.
    exit /b 1
)
if exist "%TARGET%\%~1" rmdir /s /q "%TARGET%\%~1"
xcopy /e /i /q /y "%HERE%%~1" "%TARGET%\%~1" >nul
if errorlevel 1 (
    echo Nie udalo sie skopiowac %~1. Zamknij DAW i sprobuj jeszcze raz.
    exit /b 1
)
echo Gotowe: %~1 jest w folderze %TARGET%.
exit /b 0
