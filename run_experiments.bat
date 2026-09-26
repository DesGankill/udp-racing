@echo off
setlocal

echo ========================================
echo UDP Racing - Practical Work 2
echo ========================================

if exist results.csv del results.csv

call :run_experiment baseline
call :run_experiment delay_50
call :run_experiment delay_100
call :run_experiment jitter
call :run_experiment loss_5
call :run_experiment combined

echo.
echo ========================================
echo ALL EXPERIMENTS COMPLETED
echo ========================================
echo Results: results.csv
pause
exit /b


:run_experiment
echo.
echo ========================================
echo Experiment: %1
echo ========================================

echo Starting server with profile: %1
start "UDP Server %1" /min cmd /c "server.exe %1"

C:\Windows\System32\timeout.exe /t 1 /nobreak >nul

echo Starting client...
client.exe %1

echo.
echo Stopping server...
taskkill /F /IM server.exe >nul 2>&1

C:\Windows\System32\timeout.exe /t 1 /nobreak >nul

echo Experiment %1 completed.

exit /b