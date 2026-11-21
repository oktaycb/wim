@echo off
rem wim developer build script: configures (once) with Ninja + MSVC, builds,
rem and runs the unit tests. Usage:
rem   scripts\build.bat            debug build + tests
rem   scripts\build.bat release    release build + tests
rem   scripts\build.bat configure  (re)configure only
setlocal enabledelayedexpansion
set "ROOT=%~dp0.."
set "CONFIG=Debug"
if /i "%1"=="release" set "CONFIG=Release"

where cl >nul 2>&1
if errorlevel 1 (
    call "%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
    if errorlevel 1 (
        where cl >nul 2>&1 || (
            echo error: MSVC not found - install the Visual Studio 2022 C++ workload 1>&2
            exit /b 1
        )
    )
)

if not exist "%ROOT%\build\build.ninja" goto configure
if /i "%1"=="configure" goto configure
goto build

:configure
cmake -S "%ROOT%" -B "%ROOT%\build" -G Ninja -DCMAKE_BUILD_TYPE=%CONFIG%
if errorlevel 1 exit /b 1

:build
cmake --build "%ROOT%\build"
if errorlevel 1 exit /b 1

if /i not "%1"=="notest" ctest --test-dir "%ROOT%\build" --output-on-failure
exit /b %errorlevel%
