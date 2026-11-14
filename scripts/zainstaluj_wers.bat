@echo off
rem Copies the WERS plugins into the standard VST3 folder of Windows, which needs administrator rights.
rem The plugin folders may stand next to this file, in a VST3 folder next to it, or in a build of this project.
setlocal EnableExtensions DisableDelayedExpansion
title WERS: instalacja
set "HERE=%~dp0"
set "P1=WERS.vst3"
set "B1=plugins\wers\plugin\WERS_artefacts\Release\VST3"
set "P2=WERS Barwa.vst3"
set "B2=plugins\barwa\WersBarwa_artefacts\Release\VST3"
set "P3=WERS Refren.vst3"
set "B3=plugins\refren\WersRefren_artefacts\Release\VST3"

rem Nothing to copy means nothing to ask the administrator for.
set /a FOUND=0
call :locate "%P1%" "%B1%"
if defined SRC set /a FOUND+=1
call :locate "%P2%" "%B2%"
if defined SRC set /a FOUND+=1
call :locate "%P3%" "%B3%"
if defined SRC set /a FOUND+=1
if %FOUND%==0 goto nothing_found

net session >nul 2>&1
if not errorlevel 1 goto elevated
echo.
echo   Windows zapyta o zgode, bo folder wtyczek VST3 wymaga uprawnien administratora.
rem The path travels in a variable, so apostrophes, brackets and ampersands in folder names change nothing.
set "WERS_SELF=%~f0"
powershell -NoProfile -ExecutionPolicy Bypass -Command "try { Start-Process -FilePath $env:WERS_SELF -Verb RunAs -Wait -ErrorAction Stop; exit 0 } catch { exit 1 }"
if not errorlevel 1 exit /b 0
echo.
echo   Nie dostalem zgody administratora, wiec nic nie zostalo skopiowane.
echo   Uruchom ten plik jeszcze raz i kliknij Tak w oknie Windows.
echo.
pause
exit /b 1

:elevated
if defined CommonProgramW6432 (set "TARGET=%CommonProgramW6432%\VST3") else (set "TARGET=%CommonProgramFiles%\VST3")
if not exist "%TARGET%" mkdir "%TARGET%"
set /a DONE=0
echo.
call :install "%P1%" "%B1%"
call :install "%P2%" "%B2%"
call :install "%P3%" "%B3%"
echo.
echo   Zainstalowano %DONE% z %FOUND% wtyczek do folderu:
echo   "%TARGET%"
echo.
echo   W DAW wlacz ponowne skanowanie wtyczek. Jesli DAW ma wlasna liste folderow,
echo   dopisz do niej ten folder. Otwieram go teraz w Eksploratorze.
start "" explorer "%TARGET%"
echo.
pause
exit /b 0

:nothing_found
echo.
rem Windows opens a file inside a ZIP from a temporary copy, without the folders that stand beside it.
echo "%HERE%" | find /i ".zip" >nul
if errorlevel 1 goto not_in_zip
echo   Ten plik jest uruchomiony z wnetrza archiwum ZIP.
echo   Kliknij ZIP prawym przyciskiem, wybierz Wyodrebnij wszystkie,
echo   a potem uruchom zainstaluj_wers.bat z rozpakowanego folderu.
echo.
pause
exit /b 1
:not_in_zip
echo   Nie znalazlem zadnej wtyczki. Obok tego pliku powinny lezec foldery
echo   WERS.vst3, WERS Barwa.vst3 i WERS Refren.vst3, kazdy w calosci.
echo   Ten plik jest w folderze:
echo   "%HERE%"
echo.
pause
exit /b 1

:locate
rem %1 is the plugin folder, %2 is where a build of this project leaves it. SRC is left empty when it is nowhere.
set "SRC="
if exist "%HERE%%~1\Contents\x86_64-win\%~1" set "SRC=%HERE%%~1"
if not defined SRC if exist "%HERE%VST3\%~1\Contents\x86_64-win\%~1" set "SRC=%HERE%VST3\%~1"
if not defined SRC if exist "%HERE%build\%~2\%~1\Contents\x86_64-win\%~1" set "SRC=%HERE%build\%~2\%~1"
if not defined SRC if exist "%HERE%..\build\%~2\%~1\Contents\x86_64-win\%~1" set "SRC=%HERE%..\build\%~2\%~1"
exit /b 0

:install
call :locate "%~1" "%~2"
if not defined SRC (
    echo   Brak %~1, pomijam.
    exit /b 0
)
rem A running DAW holds the plugin file. Deleting around it would leave a broken copy, so nothing is touched then.
tasklist /m "%~1" 2>nul | find /i ".exe" >nul
if not errorlevel 1 (
    echo   %~1 jest wczytany w DAW. Zamknij DAW i uruchom ten plik jeszcze raz.
    exit /b 0
)
if exist "%TARGET%\%~1" rmdir /s /q "%TARGET%\%~1"
xcopy /e /i /q /y "%SRC%" "%TARGET%\%~1" >nul
if errorlevel 1 (
    echo   Nie udalo sie skopiowac %~1.
    exit /b 0
)
if not exist "%TARGET%\%~1\Contents\x86_64-win\%~1" (
    echo   Kopia %~1 jest niepelna.
    exit /b 0
)
rem Files from a downloaded ZIP carry a mark from the internet. Some programs refuse them, so the mark goes.
set "WERS_COPY=%TARGET%\%~1"
powershell -NoProfile -ExecutionPolicy Bypass -Command "Get-ChildItem -LiteralPath $env:WERS_COPY -Recurse -File | Unblock-File" >nul 2>&1
set /a DONE+=1
echo   Gotowe: %~1
exit /b 0
