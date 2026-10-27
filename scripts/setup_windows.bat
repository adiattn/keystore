@echo off
setlocal enabledelayedexpansion

echo #########################################################
echo #            KVStore Windows Setup & Launcher           #
echo #########################################################
echo.

where wsl >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Windows Subsystem for Linux (WSL) was not found.
    echo.
    echo KVStore relies on Linux/POSIX OS primitives (epoll, fork COW, fdatasync).
    echo To install WSL on Windows 10/11, open PowerShell as Administrator and run:
    echo.
    echo     wsl --install -d Ubuntu
    echo.
    echo After installation and restart, run this script again.
    pause
    exit /b 1
)

echo [OK] WSL detected.
echo.
echo Select an option:
echo   [1] Install dependencies and build project
echo   [2] Run all unit tests (6/6)
echo   [3] Start KVStore Server (port 6379)
echo   [4] Launch Interactive CLI Client
echo   [5] Run Multi-Thread Benchmark
echo   [6] Exit
echo.

set /p choice="Enter choice [1-6]: "

if "%choice%"=="1" (
    echo.
    echo Installing build dependencies and compiling inside WSL...
    wsl bash -c "sudo apt-get update && sudo apt-get install -y build-essential cmake && cd '$(wslpath '%CD%')' && make clean && make build -j\$(nproc)"
    echo.
    echo Build completed.
) else if "%choice%"=="2" (
    echo.
    echo Running unit tests inside WSL...
    wsl bash -c "cd '$(wslpath '%CD%')' && make test"
) else if "%choice%"=="3" (
    echo.
    echo Starting KVStore Server on port 6379 (Press Ctrl+C to stop)...
    wsl bash -c "cd '$(wslpath '%CD%')' && make server"
) else if "%choice%"=="4" (
    echo.
    echo Launching KVStore CLI Client...
    wsl bash -c "cd '$(wslpath '%CD%')' && make cli"
) else if "%choice%"=="5" (
    echo.
    echo Running Benchmarks inside WSL...
    wsl bash -c "cd '$(wslpath '%CD%')' && make bench"
) else (
    echo Exiting.
)

pause
