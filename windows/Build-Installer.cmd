@echo off
setlocal
if "%~1"=="" (
    echo Uso: Build-Installer.cmd C:\msys64\ucrt64
    exit /b 1
)
set "UCRT=%~f1"
set "PATH=%UCRT%\bin;%PATH%"
for %%I in ("%~dp0..") do set "SOURCE=%%~fI"
set "BUILD=%SOURCE%\build-windows"
set "STAGE=%BUILD%\installer-files"
set "SETUP=%BUILD%\Finily-Setup-x64.exe"

cmake -S "%SOURCE%" -B "%BUILD%" -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_PREFIX_PATH=%UCRT%" "-DCMAKE_CXX_COMPILER=%UCRT%/bin/g++.exe" "-DCMAKE_MAKE_PROGRAM=%UCRT%/bin/ninja.exe" || exit /b 1
cmake --build "%BUILD%" || exit /b 1
set /p "VERSION="<"%BUILD%\finily-version.txt" || exit /b 1
if exist "%STAGE%" rmdir /s /q "%STAGE%"
if exist "%STAGE%" exit /b 1
cmake --install "%BUILD%" --prefix "%STAGE%" || exit /b 1
windeployqt --release --no-translations --no-compiler-runtime --no-system-d3d-compiler --no-system-dxc-compiler "%STAGE%\finily.exe" || exit /b 1
cmake "-DSTAGE=%STAGE%" "-DUCRT=%UCRT%" "-DOBJDUMP=%UCRT%/bin/objdump.exe" -P "%~dp0Stage-Runtime.cmake" || exit /b 1
makensis /INPUTCHARSET UTF8 "/DPAYLOAD_DIR=%STAGE%" "/DSETUP_FILE=%SETUP%" "/DFINILY_VERSION=%VERSION%" "%~dp0Finily.nsi" || exit /b 1
echo %SETUP%
