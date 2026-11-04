@echo off
rem Builds WERS with Visual Studio on this computer, runs the tests and writes logs next to this file.
rem current.job holds the steps to run, for example: build tests pluginval model reaper
setlocal EnableDelayedExpansion
cd /d "%~dp0"
set ROOT=%CD%
set LOGS=%ROOT%\logs
if not exist "%LOGS%" mkdir "%LOGS%"
set STATUS=%LOGS%\status.txt
echo started %DATE% %TIME%> "%STATUS%"
set JOB=build tests pluginval
if exist current.job set /p JOB=<current.job
echo job: %JOB%>> "%STATUS%"
set BUILD=%ROOT%\build
set VST=%BUILD%\src\plugin\WERS_artefacts\Release\VST3\WERS.vst3
where cmake >nul 2>nul || set "PATH=C:\Program Files\CMake\bin;%PATH%"
where git >nul 2>nul || set "PATH=C:\Program Files\Git\cmd;%PATH%"

echo %JOB% | findstr /c:"build" >nul
if not errorlevel 1 (
    rem Sources arrive as one archive, unpacked over the old tree with old files removed first.
    if exist "%ROOT%\wers-src.tar" (
        if exist "%ROOT%\src-tree" rmdir /s /q "%ROOT%\src-tree"
        mkdir "%ROOT%\src-tree"
        tar -xf "%ROOT%\wers-src.tar" -C "%ROOT%\src-tree" > "%LOGS%\unpack.log" 2>&1
        if errorlevel 1 ( echo FAILED unpack>> "%STATUS%" & goto end )
    )
    cmake --version > "%LOGS%\tools.log" 2>&1
    git --version >> "%LOGS%\tools.log" 2>&1
    powershell -NoProfile -Command "Get-CimInstance Win32_Processor | Select-Object Name,NumberOfCores,NumberOfLogicalProcessors | Format-List; [math]::Round((Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory/1GB)" >> "%LOGS%\tools.log" 2>&1
    echo configure %TIME%>> "%STATUS%"
    cmake -S "%ROOT%\src-tree" -B "%BUILD%" -G "Visual Studio 17 2022" -A x64 -DWERS_BUILD_TESTS=ON -DWERS_WITH_WHISPER=ON > "%LOGS%\configure.log" 2>&1
    if errorlevel 1 ( echo FAILED configure>> "%STATUS%" & goto end )
    echo build %TIME%>> "%STATUS%"
    cmake --build "%BUILD%" --config Release --parallel --target wers_tests wers_plugin_tests wers_cli WERS_VST3 > "%LOGS%\build.log" 2>&1
    if errorlevel 1 ( echo FAILED build>> "%STATUS%" & goto end )
    echo PASSED build %TIME%>> "%STATUS%"
    if exist "%ROOT%\WERS.vst3" rmdir /s /q "%ROOT%\WERS.vst3"
    xcopy /e /i /q /y "%VST%" "%ROOT%\WERS.vst3" >> "%LOGS%\tools.log" 2>&1
)

echo %JOB% | findstr /c:"tests" >nul
if not errorlevel 1 (
    echo tests %TIME%>> "%STATUS%"
    "%BUILD%\tests\Release\wers_tests.exe" > "%LOGS%\tests.log" 2>&1
    if errorlevel 1 ( echo FAILED core tests>> "%STATUS%" ) else ( echo PASSED core tests>> "%STATUS%" )
    "%BUILD%\src\plugin\Release\wers_plugin_tests.exe" > "%LOGS%\plugin_tests.log" 2>&1
    if errorlevel 1 ( echo FAILED plugin tests>> "%STATUS%" ) else ( echo PASSED plugin tests>> "%STATUS%" )
)

echo %JOB% | findstr /c:"pluginval" >nul
if not errorlevel 1 (
    rem pluginval is the standard stress test for plugins. Official release from Tracktion on GitHub.
    if not exist "%ROOT%\tools\pluginval.exe" (
        mkdir "%ROOT%\tools" 2>nul
        curl -L -f -o "%ROOT%\tools\pluginval.zip" https://github.com/Tracktion/pluginval/releases/download/v1.0.4/pluginval_Windows.zip >> "%LOGS%\tools.log" 2>&1
        tar -xf "%ROOT%\tools\pluginval.zip" -C "%ROOT%\tools" >> "%LOGS%\tools.log" 2>&1
    )
    if exist "%ROOT%\tools\pluginval.exe" (
        echo pluginval %TIME%>> "%STATUS%"
        "%ROOT%\tools\pluginval.exe" --validate-in-process --strictness-level 10 --timeout-ms 600000 --output-dir "%LOGS%" "%VST%" > "%LOGS%\pluginval.log" 2>&1
        if errorlevel 1 ( echo FAILED pluginval>> "%STATUS%" ) else ( echo PASSED pluginval>> "%STATUS%" )
    ) else (
        echo FAILED pluginval download>> "%STATUS%"
    )
)

echo %JOB% | findstr /c:"model" >nul
if not errorlevel 1 (
    rem The speech model, then a real transcription of the test vocal.
    set "MODELS=%APPDATA%\WERS\models"
    if not exist "!MODELS!" mkdir "!MODELS!"
    if not exist "!MODELS!\ggml-small-q5_1.bin" (
        echo model download %TIME%>> "%STATUS%"
        curl -L -f -o "!MODELS!\ggml-small-q5_1.part" https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-small-q5_1.bin >> "%LOGS%\tools.log" 2>&1
        if not errorlevel 1 move /y "!MODELS!\ggml-small-q5_1.part" "!MODELS!\ggml-small-q5_1.bin" >nul
    )
    powershell -NoProfile -Command "(Get-Item '!MODELS!\ggml-small-q5_1.bin').Length; (Get-FileHash '!MODELS!\ggml-small-q5_1.bin' -Algorithm SHA256).Hash" > "%LOGS%\model_info.txt" 2>&1
    echo transcribe %TIME%>> "%STATUS%"
    "%BUILD%\tools\Release\wers_cli.exe" "%ROOT%\testdata\vocal_dry.wav" "%ROOT%\testdata\beat.wav" --model "!MODELS!\ggml-small-q5_1.bin" --depth 0.4 --fix-all --render "%LOGS%" --dump "%LOGS%\words.tsv" > "%LOGS%\cli.log" 2>&1
    if errorlevel 1 ( echo FAILED transcribe>> "%STATUS%" ) else ( echo PASSED transcribe>> "%STATUS%" )
)

echo %JOB% | findstr /c:"reaper" >nul
if not errorlevel 1 (
    if exist "%ROOT%\reaper_test.bat" call "%ROOT%\reaper_test.bat"
)

echo DONE %DATE% %TIME%>> "%STATUS%"
:end
echo finished %TIME%>> "%STATUS%"
endlocal
