@echo off
setlocal
set PATH=C:\msys64\ucrt64\bin;%PATH%

if not exist build\server.exe (
    echo Build the project with build_all.bat first.
    exit /b 1
)

if not exist docs mkdir docs
if not exist build mkdir build
if exist docs\reliability_samples.csv del docs\reliability_samples.csv

call :run baseline
call :run loss_5
call :run loss_10
call :run loss_20
call :run delay_100_loss_5
call :run jitter_loss_10

echo.
echo Reliability experiments finished.
echo CSV: docs\reliability_samples.csv
exit /b 0

:run
echo.
echo ========================================
echo Reliable series: %1
echo ========================================

taskkill /F /IM server.exe >nul 2>&1
start "UDP Server reliable %1" /min cmd /c "build\server.exe reliable %1 > build\server_%1.log 2>&1"

C:\Windows\System32\timeout.exe /t 1 /nobreak >nul

build\client.exe reliable %1 > build\client_%1.log 2>&1

echo --- client summary ---
findstr /C:"SUMMARY series=" build\client_%1.log

taskkill /F /IM server.exe >nul 2>&1
C:\Windows\System32\timeout.exe /t 1 /nobreak >nul
exit /b 0
