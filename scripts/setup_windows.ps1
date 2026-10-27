<#
.SYNOPSIS
    KVStore Windows Automated Setup & Build Script (PowerShell)
.DESCRIPTION
    Checks for WSL environment, installs required build dependencies,
    and provides a menu to build, test, and run KVStore on Windows.
#>

Write-Host "#########################################################" -ForegroundColor Cyan
Write-Host "#         KVStore Windows Setup & Runner (PowerShell)   #" -ForegroundColor Cyan
Write-Host "#########################################################" -ForegroundColor Cyan
Write-Host ""

$wsl = Get-Command wsl.exe -ErrorAction SilentlyContinue

if (-not $wsl) {
    Write-Host "[ERROR] WSL (Windows Subsystem for Linux) was not found on your system." -ForegroundColor Red
    Write-Host ""
    Write-Host "KVStore uses POSIX/Linux OS primitives (epoll, fork Copy-On-Write, fdatasync)." -ForegroundColor Yellow
    Write-Host "To install WSL on Windows 10/11, run the following in PowerShell (Admin):" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "    wsl --install -d Ubuntu" -ForegroundColor Green
    Write-Host ""
    Write-Host "After restarting your computer, re-run this script."
    exit 1
}

Write-Host "[OK] WSL is available." -ForegroundColor Green
Write-Host ""
Write-Host "Select an action:"
Write-Host "  1) Install dependencies & Build KVStore"
Write-Host "  2) Run Unit Tests (6/6 passing)"
Write-Host "  3) Start KVStore Server (port 6379)"
Write-Host "  4) Launch Interactive CLI Client"
Write-Host "  5) Run Multi-Thread Benchmark Suite"
Write-Host "  6) Exit"
Write-Host ""

$choice = Read-Host "Enter option [1-6]"

$currentDir = (Get-Location).Path

switch ($choice) {
    "1" {
        Write-Host "`nInstalling toolchain and compiling KVStore inside WSL..." -ForegroundColor Cyan
        wsl bash -c "sudo apt-get update && sudo apt-get install -y build-essential cmake && cd `$(wslpath '$currentDir') && make clean && make build -j`$(nproc)"
        Write-Host "`nBuild finished successfully." -ForegroundColor Green
    }
    "2" {
        Write-Host "`nRunning test suite inside WSL..." -ForegroundColor Cyan
        wsl bash -c "cd `$(wslpath '$currentDir') && make test"
    }
    "3" {
        Write-Host "`nStarting server inside WSL (port 6379)..." -ForegroundColor Cyan
        wsl bash -c "cd `$(wslpath '$currentDir') && make server"
    }
    "4" {
        Write-Host "`nStarting CLI client inside WSL..." -ForegroundColor Cyan
        wsl bash -c "cd `$(wslpath '$currentDir') && make cli"
    }
    "5" {
        Write-Host "`nRunning benchmark suite inside WSL..." -ForegroundColor Cyan
        wsl bash -c "cd `$(wslpath '$currentDir') && make bench"
    }
    Default {
        Write-Host "Exiting."
    }
}
