@echo off
setlocal
set "ROOT=%~dp0"
set "DIST=%ROOT%dist"
set "GXX=%ROOT%tools\mingw64\bin\g++.exe"
set "WINDRES=%ROOT%tools\mingw64\bin\windres.exe"
set "FOUND=1"
if not exist "%GXX%" (
    set "FOUND=0"
    set "GXX=g++"
    set "WINDRES=windres"
)
if "%FOUND%"=="0" (
    where g++ >nul 2>nul
    if errorlevel 1 (
        echo [ERROR] MinGW-w64 g++ was not found. Install it or extract it to tools\mingw64.
        exit /b 1
    )
)
if not exist "%DIST%" mkdir "%DIST%"
if not exist "%ROOT%build" mkdir "%ROOT%build"
pushd "%ROOT%"
"%WINDRES%" resources.rc -O coff -o build\resources.res
if errorlevel 1 exit /b 1
"%GXX%" -specs="%ROOT%no-default-manifest.specs" -std=c++20 -municode -mwindows -DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN -DNOMINMAX -O2 -static -static-libgcc -static-libstdc++ main.cpp build\resources.res -lole32 -lcomctl32 -lcomdlg32 -lwinhttp -lshell32 -lshlwapi -lws2_32 -ldxgi -o "%DIST%\OllamaPlusGUI.exe"
if errorlevel 1 exit /b 1
popd
echo [OK] %DIST%\OllamaPlusGUI.exe
endlocal


