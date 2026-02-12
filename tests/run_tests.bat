@echo off
REM VEX DEFENDER - Test Suite Build & Run
REM Compiles test_main.c against mock SNES headers using Clang
REM
REM Usage: run_tests.bat
REM Returns: exit code 0 if all pass, 1 if any fail

cd /d "%~dp0"

echo Building tests...
"C:\Program Files\LLVM\bin\clang.exe" -I. -I../include -Wall -Wno-unused-function -Wno-unused-variable -Wno-unused-but-set-variable test_main.c -o run_tests.exe 2>&1

if %ERRORLEVEL% NEQ 0 (
    echo BUILD FAILED
    exit /b 1
)

echo.
run_tests.exe
exit /b %ERRORLEVEL%
