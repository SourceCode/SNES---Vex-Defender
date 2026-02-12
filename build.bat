@echo off
REM ============================================================================
REM  VEX DEFENDER - Build Script
REM  One-click build for SNES ROM, tests, and emulator launch.
REM
REM  Usage:
REM    build.bat              Build the SNES ROM
REM    build.bat clean        Clean all build artifacts, then rebuild
REM    build.bat test         Build and run the host-side test suite
REM    build.bat run          Build the ROM and launch in bsnes emulator
REM    build.bat all          Clean, build ROM, run tests, launch emulator
REM    build.bat help         Show this help message
REM
REM  Prerequisites:
REM    - PVSnesLib toolchain at J:\code\snes\snes-build-tools\tools\pvsneslib
REM    - GNU Make on PATH (provided by PVSnesLib devkitsnes)
REM    - LLVM/Clang for test compilation (C:\Program Files\LLVM\bin\clang.exe)
REM    - bsnes v115 at J:\code\snes\snes-build-tools\tmp\bsnes_v115-windows
REM ============================================================================

setlocal enabledelayedexpansion

REM --- Paths ---
set "PROJECT_DIR=%~dp0"
set "BUILD_TOOLS=J:\code\snes\snes-build-tools"
set "PVSNESLIB_HOME=%BUILD_TOOLS%\tools\pvsneslib"
set "DEVKITSNES=%PVSNESLIB_HOME%\devkitsnes"
set "WLA_DIR=%PVSNESLIB_HOME%\tools\wla"
set "BSNES_DIR=%BUILD_TOOLS%\tmp\bsnes_v115-windows"
set "BSNES_EXE=%BSNES_DIR%\bsnes.exe"
set "CLANG=C:\Program Files\LLVM\bin\clang.exe"
set "ROM_NAME=vex_defender"
set "ROM_FILE=%PROJECT_DIR%%ROM_NAME%.sfc"

REM --- Add toolchain to PATH ---
set "PATH=%DEVKITSNES%\bin;%WLA_DIR%;%PATH%"

REM --- Parse command ---
if "%~1"=="" goto :build
if /i "%~1"=="clean" goto :clean_build
if /i "%~1"=="test" goto :test
if /i "%~1"=="run" goto :build_and_run
if /i "%~1"=="all" goto :all
if /i "%~1"=="help" goto :help
if /i "%~1"=="-h" goto :help
if /i "%~1"=="/?" goto :help

echo Unknown command: %~1
echo Run "build.bat help" for usage.
exit /b 1

REM ============================================================================
:help
REM ============================================================================
echo.
echo  VEX DEFENDER Build Script
echo  =========================
echo.
echo  build.bat              Build the SNES ROM (incremental)
echo  build.bat clean        Clean all artifacts, then rebuild from scratch
echo  build.bat test         Compile and run the host-side unit test suite
echo  build.bat run          Build the ROM and launch in bsnes emulator
echo  build.bat all          Clean + build + test + run (full pipeline)
echo  build.bat help         Show this help message
echo.
echo  Output: %ROM_NAME%.sfc
echo.
exit /b 0

REM ============================================================================
:build
REM ============================================================================
echo.
echo ========================================
echo  VEX DEFENDER - Building SNES ROM
echo ========================================
echo.

cd /d "%PROJECT_DIR%"
make 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo.
    echo  BUILD FAILED
    exit /b 1
)

echo.
echo  BUILD SUCCESSFUL: %ROM_FILE%
echo.
exit /b 0

REM ============================================================================
:clean_build
REM ============================================================================
echo.
echo ========================================
echo  VEX DEFENDER - Clean Build
echo ========================================
echo.

cd /d "%PROJECT_DIR%"
make clean 2>nul
make 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo.
    echo  BUILD FAILED
    exit /b 1
)

echo.
echo  CLEAN BUILD SUCCESSFUL: %ROM_FILE%
echo.
exit /b 0

REM ============================================================================
:test
REM ============================================================================
echo.
echo ========================================
echo  VEX DEFENDER - Unit Test Suite
echo ========================================
echo.

if not exist "%CLANG%" (
    echo  ERROR: Clang not found at %CLANG%
    echo  Install LLVM or update CLANG path in this script.
    exit /b 1
)

cd /d "%PROJECT_DIR%"
"%CLANG%" -I tests -I include -Wall -Wno-unused-function -Wno-unused-variable -Wno-unused-but-set-variable tests/test_main.c -o tests/run_tests.exe 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo.
    echo  TEST BUILD FAILED
    exit /b 1
)

echo.
tests\run_tests.exe
set TEST_RESULT=%ERRORLEVEL%
echo.

if %TEST_RESULT% NEQ 0 (
    echo  TESTS FAILED
    exit /b 1
)

echo  ALL TESTS PASSED
echo.
exit /b 0

REM ============================================================================
:build_and_run
REM ============================================================================
call :build
if %ERRORLEVEL% NEQ 0 exit /b 1

echo  Launching bsnes...
if not exist "%BSNES_EXE%" (
    echo  ERROR: bsnes not found at %BSNES_EXE%
    exit /b 1
)
if not exist "%ROM_FILE%" (
    echo  ERROR: ROM not found at %ROM_FILE%
    exit /b 1
)

start "" "%BSNES_EXE%" "%ROM_FILE%"
echo  bsnes launched with %ROM_NAME%.sfc
echo.
exit /b 0

REM ============================================================================
:all
REM ============================================================================
echo.
echo ========================================
echo  VEX DEFENDER - Full Build Pipeline
echo ========================================

echo.
echo [1/4] Cleaning...
cd /d "%PROJECT_DIR%"
make clean 2>nul

echo.
echo [2/4] Building SNES ROM...
make 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo  ROM BUILD FAILED
    exit /b 1
)
echo  ROM built: %ROM_NAME%.sfc

echo.
echo [3/4] Running tests...
if exist "%CLANG%" (
    "%CLANG%" -I tests -I include -Wall -Wno-unused-function -Wno-unused-variable -Wno-unused-but-set-variable tests/test_main.c -o tests/run_tests.exe 2>&1
    if !ERRORLEVEL! NEQ 0 (
        echo  TEST BUILD FAILED
        exit /b 1
    )
    tests\run_tests.exe
    if !ERRORLEVEL! NEQ 0 (
        echo  TESTS FAILED
        exit /b 1
    )
) else (
    echo  Clang not found, skipping tests.
)

echo.
echo [4/4] Launching emulator...
if exist "%BSNES_EXE%" (
    start "" "%BSNES_EXE%" "%ROM_FILE%"
    echo  bsnes launched.
) else (
    echo  bsnes not found, skipping emulator launch.
)

echo.
echo ========================================
echo  Full pipeline complete!
echo ========================================
echo.
exit /b 0
