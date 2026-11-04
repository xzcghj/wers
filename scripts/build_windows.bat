@echo off
rem Builds WERS with Visual Studio on this computer, runs the tests and writes logs next to this file.
rem job.txt names the steps to run, for example: build tests pluginval model install
setlocal EnableDelayedExpansion
title WERS: budowa i testy
cd /d "%~dp0"
set ROOT=%CD%
set LOGS=%ROOT%\logs
if not exist "%LOGS%" mkdir "%LOGS%"
set STATUS=%LOGS%\status.txt
echo started %DATE% %TIME%> "%STATUS%"
set JOB=build tests pluginval
if exist job.txt set /p JOB=<job.txt
echo job: %JOB%>> "%STATUS%"
echo.
echo   WERS: budowa i testy. Kroki: %JOB%
echo   Pierwsza budowa trwa kilkanascie minut. Wyniki trafiaja do folderu logs.
echo.
set BUILD=%ROOT%\build
set VST=%BUILD%\src\plugin\WERS_artefacts\Release\VST3\WERS.vst3
where cmake >nul 2>nul || set "PATH=C:\Program Files\CMake\bin;%PATH%"
where git >nul 2>nul || set "PATH=C:\Program Files\Git\cmd;%PATH%"

echo %JOB% | findstr /c:"build" >nul
if not errorlevel 1 (
    rem Sources arrive as one archive, unpacked over a clean tree so no old file stays behind.
    if exist "%ROOT%\wers-src.tar" (
        if exist "%ROOT%\src-tree" rmdir /s /q "%ROOT%\src-tree"
        mkdir "%ROOT%\src-tree"
        tar -xf "%ROOT%\wers-src.tar" -C "%ROOT%\src-tree" > "%LOGS%\unpack.log" 2>&1
        if errorlevel 1 ( echo FAILED unpack>> "%STATUS%" & goto end )
    )
    cmake --version > "%LOGS%\tools.log" 2>&1
    git --version >> "%LOGS%\tools.log" 2>&1
    powershell -NoProfile -Command "Get-CimInstance Win32_Processor | Select-Object Name,NumberOfCores,NumberOfLogicalProcessors | Format-List; 'RAM GB: ' + [math]::Round((Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory/1GB)" >> "%LOGS%\tools.log" 2>&1
    echo configure %TIME%>> "%STATUS%"
    echo   Konfiguracja, pobieranie JUCE i whisper.cpp...
    cmake -S "%ROOT%\src-tree" -B "%BUILD%" -G "Visual Studio 17 2022" -A x64 -DWERS_BUILD_TESTS=ON -DWERS_WITH_WHISPER=ON -DWERS_BUILD_PROBE=ON > "%LOGS%\configure.log" 2>&1
    if errorlevel 1 ( echo FAILED configure>> "%STATUS%" & goto end )
    echo build %TIME%>> "%STATUS%"
    echo   Kompilacja...
    cmake --build "%BUILD%" --config Release --parallel --target wers_tests wers_plugin_tests wers_ui_shots wers_cli wers_whisper WERS_VST3 WersProbe_VST3 -- /m /nologo /v:m > "%LOGS%\build.log" 2>&1
    if errorlevel 1 ( echo FAILED build>> "%STATUS%" & goto end )
    echo PASSED build %TIME%>> "%STATUS%"
    if exist "%ROOT%\WERS.vst3" rmdir /s /q "%ROOT%\WERS.vst3"
    xcopy /e /i /q /y "%VST%" "%ROOT%\WERS.vst3" >> "%LOGS%\tools.log" 2>&1
)

echo %JOB% | findstr /c:"tests" >nul
if not errorlevel 1 (
    echo tests %TIME%>> "%STATUS%"
    echo   Testy...
    "%BUILD%\tests\Release\wers_tests.exe" > "%LOGS%\tests.log" 2>&1
    if errorlevel 1 ( echo FAILED core tests>> "%STATUS%" ) else ( echo PASSED core tests>> "%STATUS%" )
    "%BUILD%\src\plugin\Release\wers_plugin_tests.exe" > "%LOGS%\plugin_tests.log" 2>&1
    if errorlevel 1 ( echo FAILED plugin tests>> "%STATUS%" ) else ( echo PASSED plugin tests>> "%STATUS%" )
    if exist "%ROOT%\testdata\vocal_dry.wav" (
        if not exist "%LOGS%\shots" mkdir "%LOGS%\shots"
        "%BUILD%\src\plugin\Release\wers_ui_shots.exe" "%LOGS%\shots" "%ROOT%\testdata\vocal_dry.wav" "%ROOT%\testdata\beat.wav" > "%LOGS%\shots.log" 2>&1
        if errorlevel 1 ( echo FAILED ui shots>> "%STATUS%" ) else ( echo PASSED ui shots>> "%STATUS%" )
    )
)

echo %JOB% | findstr /c:"pluginval" >nul
if not errorlevel 1 (
    rem pluginval is the standard stress test for plugins, in the official release from Tracktion.
    if not exist "%ROOT%\tools\pluginval.exe" (
        if not exist "%ROOT%\tools" mkdir "%ROOT%\tools"
        curl -L -f -o "%ROOT%\tools\pluginval.zip" https://github.com/Tracktion/pluginval/releases/download/v1.0.4/pluginval_Windows.zip >> "%LOGS%\tools.log" 2>&1
        tar -xf "%ROOT%\tools\pluginval.zip" -C "%ROOT%\tools" >> "%LOGS%\tools.log" 2>&1
    )
    if exist "%ROOT%\tools\pluginval.exe" (
        echo pluginval %TIME%>> "%STATUS%"
        echo   pluginval...
        "%ROOT%\tools\pluginval.exe" --validate-in-process --strictness-level 10 --timeout-ms 600000 "%VST%" > "%LOGS%\pluginval.log" 2>&1
        if errorlevel 1 ( echo FAILED pluginval>> "%STATUS%" ) else ( echo PASSED pluginval>> "%STATUS%" )
    ) else (
        echo FAILED pluginval download>> "%STATUS%"
    )
)

echo %JOB% | findstr /c:"model" >nul
if not errorlevel 1 call :transcribe ggml-small-q5_1 words

echo %JOB% | findstr /c:"copymodel" >nul
if not errorlevel 1 (
    if not exist "%ROOT%\models" mkdir "%ROOT%\models"
    copy /y "%APPDATA%\WERS\models\ggml-small-q5_1.bin" "%ROOT%\models\ggml-small-q5_1.bin" >nul
)

echo %JOB% | findstr /c:"nettest" >nul
if not errorlevel 1 (
    rem The plugin's own download code against the real address. Downloads 547 MB to the temporary folder.
    echo nettest %TIME%>> "%STATUS%"
    "%BUILD%\src\plugin\Release\wers_plugin_tests.exe" -ns -tc="the model downloads*" > "%LOGS%\nettest.log" 2>&1
    if errorlevel 1 ( echo FAILED nettest>> "%STATUS%" ) else ( echo PASSED nettest %TIME%>> "%STATUS%" )
)

echo %JOB% | findstr /c:"turbo" >nul
if not errorlevel 1 call :transcribe ggml-large-v3-turbo-q5_0 words_turbo

echo %JOB% | findstr /c:"install" >nul
if not errorlevel 1 (
    rem The per-user VST3 folder needs no administrator rights.
    set "TARGET=%LOCALAPPDATA%\Programs\Common\VST3"
    if not exist "!TARGET!" mkdir "!TARGET!"
    if exist "!TARGET!\WERS.vst3" rmdir /s /q "!TARGET!\WERS.vst3"
    xcopy /e /i /q /y "%VST%" "!TARGET!\WERS.vst3" >> "%LOGS%\tools.log" 2>&1
    if errorlevel 1 ( echo FAILED install>> "%STATUS%" ) else ( echo PASSED install !TARGET!>> "%STATUS%" )
    if exist "!TARGET!\WERS Probe.vst3" rmdir /s /q "!TARGET!\WERS Probe.vst3"
    xcopy /e /i /q /y "%BUILD%\tools\probe\WersProbe_artefacts\Release\VST3\WERS Probe.vst3" "!TARGET!\WERS Probe.vst3" >> "%LOGS%\tools.log" 2>&1
)

echo DONE %DATE% %TIME%>> "%STATUS%"
:end
echo finished %TIME%>> "%STATUS%"
echo.
echo   Koniec. To okno zamknie sie za minute.
timeout /t 60 /nobreak >nul
endlocal
exit /b 0

:transcribe
rem A speech model from the official whisper.cpp repository, then a transcription of the test vocal.
set "MODELS=%APPDATA%\WERS\models"
if not exist "%MODELS%" mkdir "%MODELS%"
if not exist "%MODELS%\%~1.bin" (
    echo download %~1 %TIME%>> "%STATUS%"
    echo   Pobieranie modelu mowy %~1...
    curl -L -f -o "%MODELS%\%~1.part" https://huggingface.co/ggerganov/whisper.cpp/resolve/main/%~1.bin >> "%LOGS%\tools.log" 2>&1
    if not errorlevel 1 move /y "%MODELS%\%~1.part" "%MODELS%\%~1.bin" >nul
)
powershell -NoProfile -Command "(Get-Item '%MODELS%\%~1.bin').Length; (Get-FileHash '%MODELS%\%~1.bin' -Algorithm SHA1).Hash" > "%LOGS%\%~1_info.txt" 2>&1
echo transcribe %~1 start %TIME%>> "%STATUS%"
echo   Rozpoznawanie tekstu: %~1...
"%BUILD%\tools\Release\wers_cli.exe" "%ROOT%\testdata\vocal_dry.wav" "%ROOT%\testdata\beat.wav" --model "%MODELS%\%~1.bin" --depth 0.4 --dump "%LOGS%\%~2.tsv" --raw-dump "%LOGS%\%~2_raw.txt" > "%LOGS%\%~2.log" 2>&1
if errorlevel 1 ( echo FAILED transcribe %~1>> "%STATUS%" ) else ( echo PASSED transcribe %~1 %TIME%>> "%STATUS%" )
exit /b 0
