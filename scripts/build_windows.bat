@echo off
rem Builds the WERS plugins with Visual Studio on this computer, runs the tests and writes logs next to this file.
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
set VST=%BUILD%\plugins\wers\plugin\WERS_artefacts\Release\VST3\WERS.vst3
set "BARWA=%BUILD%\plugins\barwa\WersBarwa_artefacts\Release\VST3\WERS Barwa.vst3"
set "REFREN=%BUILD%\plugins\refren\WersRefren_artefacts\Release\VST3\WERS Refren.vst3"
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
    echo configure !TIME!>> "%STATUS%"
    echo   Konfiguracja, pobieranie JUCE i whisper.cpp...
    cmake -S "%ROOT%\src-tree" -B "%BUILD%" -G "Visual Studio 17 2022" -A x64 -DWERS_BUILD_TESTS=ON -DWERS_WITH_WHISPER=ON -DWERS_BUILD_PROBE=ON > "%LOGS%\configure.log" 2>&1
    if errorlevel 1 ( echo FAILED configure>> "%STATUS%" & goto end )
    echo build !TIME!>> "%STATUS%"
    echo   Kompilacja...
    cmake --build "%BUILD%" --config Release --parallel --target wers_tests wers_dsp_tests wers_plugin_tests wers_ui_shots wers_cli wers_whisper WERS_VST3 WersProbe_VST3 barwa_tests barwa_plugin_tests barwa_ui_shots barwa_measure WersBarwa_VST3 refren_tests refren_render refren_plugin_tests refren_preset_tests refren_ui_shots refren_trim refren_measure WersRefren_VST3 -- /m /nologo /v:m > "%LOGS%\build.log" 2>&1
    if errorlevel 1 ( echo FAILED build>> "%STATUS%" & goto end )
    echo PASSED build !TIME!>> "%STATUS%"
    if exist "%ROOT%\WERS.vst3" rmdir /s /q "%ROOT%\WERS.vst3"
    xcopy /e /i /q /y "%VST%" "%ROOT%\WERS.vst3" >> "%LOGS%\tools.log" 2>&1
    if exist "%ROOT%\WERS Barwa.vst3" rmdir /s /q "%ROOT%\WERS Barwa.vst3"
    xcopy /e /i /q /y "%BARWA%" "%ROOT%\WERS Barwa.vst3" >> "%LOGS%\tools.log" 2>&1
    if exist "%ROOT%\WERS Refren.vst3" rmdir /s /q "%ROOT%\WERS Refren.vst3"
    xcopy /e /i /q /y "%REFREN%" "%ROOT%\WERS Refren.vst3" >> "%LOGS%\tools.log" 2>&1
)

echo %JOB% | findstr /c:"tests" >nul
if not errorlevel 1 (
    echo tests !TIME!>> "%STATUS%"
    echo   Testy...
    "%BUILD%\plugins\wers\tests\Release\wers_tests.exe" > "%LOGS%\tests.log" 2>&1
    if errorlevel 1 ( echo FAILED core tests>> "%STATUS%" ) else ( echo PASSED core tests>> "%STATUS%" )
    "%BUILD%\plugins\wers\plugin\Release\wers_plugin_tests.exe" > "%LOGS%\plugin_tests.log" 2>&1
    if errorlevel 1 ( echo FAILED plugin tests>> "%STATUS%" ) else ( echo PASSED plugin tests>> "%STATUS%" )
    "%BUILD%\libs\dsp\Release\wers_dsp_tests.exe" > "%LOGS%\dsp_tests.log" 2>&1
    if errorlevel 1 ( echo FAILED dsp tests>> "%STATUS%" ) else ( echo PASSED dsp tests>> "%STATUS%" )
    "%BUILD%\plugins\barwa\Release\barwa_tests.exe" > "%LOGS%\barwa_tests.log" 2>&1
    if errorlevel 1 ( echo FAILED barwa tests>> "%STATUS%" ) else ( echo PASSED barwa tests>> "%STATUS%" )
    "%BUILD%\plugins\barwa\Release\barwa_plugin_tests.exe" > "%LOGS%\barwa_plugin_tests.log" 2>&1
    if errorlevel 1 ( echo FAILED barwa plugin tests>> "%STATUS%" ) else ( echo PASSED barwa plugin tests>> "%STATUS%" )
    if not exist "%LOGS%\barwa_shots" mkdir "%LOGS%\barwa_shots"
    "%BUILD%\plugins\barwa\Release\barwa_ui_shots.exe" "%LOGS%\barwa_shots" > "%LOGS%\barwa_shots.log" 2>&1
    if errorlevel 1 ( echo FAILED barwa ui shots>> "%STATUS%" ) else ( echo PASSED barwa ui shots>> "%STATUS%" )
    "%BUILD%\plugins\refren\Release\refren_tests.exe" > "%LOGS%\refren_tests.log" 2>&1
    if errorlevel 1 ( echo FAILED refren tests>> "%STATUS%" ) else ( echo PASSED refren tests>> "%STATUS%" )
    "%BUILD%\plugins\refren\Release\refren_plugin_tests.exe" > "%LOGS%\refren_plugin_tests.log" 2>&1
    if errorlevel 1 ( echo FAILED refren plugin tests>> "%STATUS%" ) else ( echo PASSED refren plugin tests>> "%STATUS%" )
    "%BUILD%\plugins\refren\Release\refren_preset_tests.exe" > "%LOGS%\refren_preset_tests.log" 2>&1
    if errorlevel 1 ( echo FAILED refren preset tests>> "%STATUS%" ) else ( echo PASSED refren preset tests>> "%STATUS%" )
    if not exist "%LOGS%\refren_shots" mkdir "%LOGS%\refren_shots"
    "%BUILD%\plugins\refren\Release\refren_ui_shots.exe" "%LOGS%\refren_shots" > "%LOGS%\refren_shots.log" 2>&1
    if errorlevel 1 ( echo FAILED refren ui shots>> "%STATUS%" ) else ( echo PASSED refren ui shots>> "%STATUS%" )
    if exist "%ROOT%\testdata\vocal_dry.wav" (
        if not exist "%LOGS%\shots" mkdir "%LOGS%\shots"
        "%BUILD%\plugins\wers\plugin\Release\wers_ui_shots.exe" "%LOGS%\shots" "%ROOT%\testdata\vocal_dry.wav" "%ROOT%\testdata\beat.wav" > "%LOGS%\shots.log" 2>&1
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
        echo pluginval !TIME!>> "%STATUS%"
        echo   pluginval...
        "%ROOT%\tools\pluginval.exe" --validate-in-process --strictness-level 10 --timeout-ms 600000 "%VST%" > "%LOGS%\pluginval.log" 2>&1
        if errorlevel 1 ( echo FAILED pluginval>> "%STATUS%" ) else ( echo PASSED pluginval>> "%STATUS%" )
        "%ROOT%\tools\pluginval.exe" --validate-in-process --strictness-level 10 --timeout-ms 600000 "%BARWA%" > "%LOGS%\pluginval_barwa.log" 2>&1
        if errorlevel 1 ( echo FAILED pluginval barwa>> "%STATUS%" ) else ( echo PASSED pluginval barwa>> "%STATUS%" )
        "%ROOT%\tools\pluginval.exe" --validate-in-process --strictness-level 10 --timeout-ms 600000 "%REFREN%" > "%LOGS%\pluginval_refren.log" 2>&1
        if errorlevel 1 ( echo FAILED pluginval refren>> "%STATUS%" ) else ( echo PASSED pluginval refren>> "%STATUS%" )
    ) else (
        echo FAILED pluginval download>> "%STATUS%"
    )
)

echo %JOB% | findstr /c:"measure" >nul
if not errorlevel 1 (
    rem Measures on the songs named in measure_list.txt. Only numbers are written.
    echo measure !TIME!>> "%STATUS%"
    echo   Pomiary Barwy na numerach...
    "%BUILD%\plugins\barwa\Release\barwa_measure.exe" --list "%ROOT%\measure_list.txt" --out "%LOGS%\measure.tsv" > "%LOGS%\measure.log" 2>&1
    if errorlevel 1 ( echo FAILED measure>> "%STATUS%" ) else ( echo PASSED measure !TIME!>> "%STATUS%" )
)

echo %JOB% | findstr /c:"cpu" >nul
if not errorlevel 1 (
    echo cpu !TIME!>> "%STATUS%"
    echo   Pomiar obciazenia procesora...
    "%BUILD%\plugins\barwa\Release\barwa_measure.exe" --cpu > "%LOGS%\cpu.log" 2>&1
    if errorlevel 1 ( echo FAILED cpu>> "%STATUS%" ) else ( echo PASSED cpu>> "%STATUS%" )
)

echo %JOB% | findstr /c:"refrenrender" >nul
if not errorlevel 1 (
    rem Listening samples from the vocals named in refren_list.txt. They stay in the logs folder on this computer.
    echo refren render !TIME!>> "%STATUS%"
    echo   Probki Refrenu do odsluchu...
    if exist "%LOGS%\refren_probe" rmdir /s /q "%LOGS%\refren_probe"
    "%BUILD%\plugins\refren\Release\refren_render.exe" --list "%ROOT%\refren_list.txt" --out "%LOGS%\refren_probe" > "%LOGS%\refren_render.log" 2>&1
    if errorlevel 1 ( echo FAILED refren render>> "%STATUS%" ) else ( echo PASSED refren render !TIME!>> "%STATUS%" )
)

echo %JOB% | findstr /c:"refrenstats" >nul
if not errorlevel 1 (
    rem Numbers from the vocals named in refren_list.txt and the load on the processor. Only numbers are written.
    echo refren stats !TIME!>> "%STATUS%"
    echo   Pomiary Refrenu...
    "%BUILD%\plugins\refren\Release\refren_measure.exe" --list "%ROOT%\refren_list.txt" --out "%LOGS%\refren_measure.tsv" > "%LOGS%\refren_measure.log" 2>&1
    if errorlevel 1 ( echo FAILED refren measure>> "%STATUS%" ) else ( echo PASSED refren measure !TIME!>> "%STATUS%" )
    "%BUILD%\plugins\refren\Release\refren_measure.exe" --cpu > "%LOGS%\refren_cpu.log" 2>&1
    if errorlevel 1 ( echo FAILED refren cpu>> "%STATUS%" ) else ( echo PASSED refren cpu>> "%STATUS%" )
    "%BUILD%\plugins\refren\Release\refren_trim.exe" --load > "%LOGS%\refren_load.log" 2>&1
    if errorlevel 1 ( echo FAILED refren load>> "%STATUS%" ) else ( echo PASSED refren load>> "%STATUS%" )
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
    echo nettest !TIME!>> "%STATUS%"
    "%BUILD%\plugins\wers\plugin\Release\wers_plugin_tests.exe" -ns -tc="the model downloads*" > "%LOGS%\nettest.log" 2>&1
    if errorlevel 1 ( echo FAILED nettest>> "%STATUS%" ) else ( echo PASSED nettest !TIME!>> "%STATUS%" )
)

echo %JOB% | findstr /c:"turbo" >nul
if not errorlevel 1 call :transcribe ggml-large-v3-turbo-q5_0 words_turbo

echo %JOB% | findstr /c:"install" >nul
if not errorlevel 1 (
    rem The per-user VST3 folder needs no administrator rights.
    set "TARGET=%LOCALAPPDATA%\Programs\Common\VST3"
    if not exist "!TARGET!" mkdir "!TARGET!"
    rem A running DAW holds the plugin file. Deleting around it would leave a broken copy, so nothing is touched then.
    tasklist /m WERS.vst3 2>nul | findstr /i ".exe" >nul
    if not errorlevel 1 (
        echo SKIPPED install: WERS is loaded in a running program>> "%STATUS%"
        echo   Nowa wersja gotowa, ale WERS jest wczytany w DAW. Stara wersja zostaje bez zmian.
    ) else (
        if exist "!TARGET!\WERS.vst3" rmdir /s /q "!TARGET!\WERS.vst3"
        xcopy /e /i /q /y "%VST%" "!TARGET!\WERS.vst3" >> "%LOGS%\tools.log" 2>&1
        if errorlevel 1 ( echo FAILED install>> "%STATUS%" ) else ( echo PASSED install !TARGET!>> "%STATUS%" )
        if exist "!TARGET!\WERS Probe.vst3" rmdir /s /q "!TARGET!\WERS Probe.vst3"
        xcopy /e /i /q /y "%BUILD%\plugins\wers\tools\probe\WersProbe_artefacts\Release\VST3\WERS Probe.vst3" "!TARGET!\WERS Probe.vst3" >> "%LOGS%\tools.log" 2>&1
    )
    tasklist /m "WERS Barwa.vst3" 2>nul | findstr /i ".exe" >nul
    if not errorlevel 1 (
        echo SKIPPED install barwa: WERS Barwa is loaded in a running program>> "%STATUS%"
        echo   Nowa wersja Barwy gotowa, ale Barwa jest wczytana w DAW. Stara wersja zostaje bez zmian.
    ) else (
        if exist "!TARGET!\WERS Barwa.vst3" rmdir /s /q "!TARGET!\WERS Barwa.vst3"
        xcopy /e /i /q /y "%BARWA%" "!TARGET!\WERS Barwa.vst3" >> "%LOGS%\tools.log" 2>&1
        if errorlevel 1 ( echo FAILED install barwa>> "%STATUS%" ) else ( echo PASSED install barwa !TARGET!>> "%STATUS%" )
    )
    tasklist /m "WERS Refren.vst3" 2>nul | findstr /i ".exe" >nul
    if not errorlevel 1 (
        echo SKIPPED install refren: WERS Refren is loaded in a running program>> "%STATUS%"
        echo   Nowa wersja Refrenu gotowa, ale Refren jest wczytany w DAW. Stara wersja zostaje bez zmian.
    ) else (
        if exist "!TARGET!\WERS Refren.vst3" rmdir /s /q "!TARGET!\WERS Refren.vst3"
        xcopy /e /i /q /y "%REFREN%" "!TARGET!\WERS Refren.vst3" >> "%LOGS%\tools.log" 2>&1
        if errorlevel 1 ( echo FAILED install refren>> "%STATUS%" ) else ( echo PASSED install refren !TARGET!>> "%STATUS%" )
    )
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
    echo download %~1 !TIME!>> "%STATUS%"
    echo   Pobieranie modelu mowy %~1...
    curl -L -f -o "%MODELS%\%~1.part" https://huggingface.co/ggerganov/whisper.cpp/resolve/main/%~1.bin >> "%LOGS%\tools.log" 2>&1
    if not errorlevel 1 move /y "%MODELS%\%~1.part" "%MODELS%\%~1.bin" >nul
)
powershell -NoProfile -Command "(Get-Item '%MODELS%\%~1.bin').Length; (Get-FileHash '%MODELS%\%~1.bin' -Algorithm SHA1).Hash" > "%LOGS%\%~1_info.txt" 2>&1
echo transcribe %~1 start %TIME%>> "%STATUS%"
echo   Rozpoznawanie tekstu: %~1...
"%BUILD%\plugins\wers\tools\Release\wers_cli.exe" "%ROOT%\testdata\vocal_dry.wav" "%ROOT%\testdata\beat.wav" --model "%MODELS%\%~1.bin" --depth 0.4 --dump "%LOGS%\%~2.tsv" --raw-dump "%LOGS%\%~2_raw.txt" > "%LOGS%\%~2.log" 2>&1
if errorlevel 1 ( echo FAILED transcribe %~1>> "%STATUS%" ) else ( echo PASSED transcribe %~1 %TIME%>> "%STATUS%" )
exit /b 0
