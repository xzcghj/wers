@echo off
rem Kopiuje WERS Lampa do standardowego folderu VST3 Windows (wymaga zgody administratora).
setlocal EnableExtensions DisableDelayedExpansion
title WERS Lampa: instalacja
set "HERE=%~dp0"
set "NAME=WERS Lampa.vst3"
set "SRC="
if exist "%HERE%%NAME%\Contents" set "SRC=%HERE%%NAME%"
if not defined SRC if exist "%HERE%build\WersLampa_artefacts\Release\VST3\%NAME%\Contents" set "SRC=%HERE%build\WersLampa_artefacts\Release\VST3\%NAME%"
if not defined SRC (
    echo.
    echo   Nie znalazlem folderu "%NAME%" obok tego pliku.
    echo   Jesli to ZIP, najpierw go rozpakuj: prawy przycisk, Wyodrebnij wszystkie.
    echo.
    pause
    exit /b 1
)

net session >nul 2>&1
if not errorlevel 1 goto elevated
echo.
echo   Windows zapyta o zgode, bo folder wtyczek VST3 wymaga uprawnien administratora.
set "WIR_SELF=%~f0"
powershell -NoProfile -ExecutionPolicy Bypass -Command "try { Start-Process -FilePath $env:WIR_SELF -Verb RunAs -Wait -ErrorAction Stop; exit 0 } catch { exit 1 }"
if not errorlevel 1 exit /b 0
echo.
echo   Nie dostalem zgody administratora, wiec nic nie zostalo skopiowane.
echo.
pause
exit /b 1

:elevated
if defined CommonProgramW6432 (set "TARGET=%CommonProgramW6432%\VST3") else (set "TARGET=%CommonProgramFiles%\VST3")
if not exist "%TARGET%" mkdir "%TARGET%"
if exist "%TARGET%\%NAME%" rmdir /s /q "%TARGET%\%NAME%"
xcopy /e /i /q /y "%SRC%" "%TARGET%\%NAME%" >nul
if errorlevel 1 (
    echo.
    echo   Kopiowanie sie nie udalo. Zamknij DAW i sprobuj jeszcze raz.
    echo.
    pause
    exit /b 1
)
echo.
echo   Zainstalowano WERS Lampa do folderu:
echo   "%TARGET%"
echo.
echo   W DAW wlacz ponowne skanowanie wtyczek i szukaj "WERS Lampa".
start "" explorer "%TARGET%"
echo.
pause
exit /b 0
