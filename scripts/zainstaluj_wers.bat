@echo off
rem Copies WERS.vst3 into the standard VST3 folder of Windows, which needs administrator rights.
rem Windows asks for permission once. After that every DAW finds the plugin in its usual place.
net session >nul 2>&1
if errorlevel 1 (
    powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
    exit /b 0
)
setlocal
set "SOURCE=%~dp0WERS.vst3"
set "TARGET=%CommonProgramFiles%\VST3"
if not exist "%SOURCE%" (
    echo Nie widze folderu WERS.vst3 obok tego pliku.
    pause
    exit /b 1
)
if not exist "%TARGET%" mkdir "%TARGET%"
if exist "%TARGET%\WERS.vst3" rmdir /s /q "%TARGET%\WERS.vst3"
xcopy /e /i /q /y "%SOURCE%" "%TARGET%\WERS.vst3" >nul
if errorlevel 1 (
    echo Nie udalo sie skopiowac wtyczki. Zamknij DAW i sprobuj jeszcze raz.
    pause
    exit /b 1
)
echo Gotowe. WERS jest w folderze %TARGET%.
echo W DAW uruchom ponowne skanowanie wtyczek.
pause
endlocal
