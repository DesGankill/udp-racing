@echo off
setlocal
set PATH=C:\msys64\ucrt64\bin;%PATH%
if not exist docs\media mkdir docs\media
start "prediction-server" /b build\prediction_server.exe --headless
timeout /t 1 /nobreak >nul
build\prediction_client.exe --script --lag-ms 0 --loss 0 --ticks 12 --log docs\media\movement_no_emulation.txt
build\prediction_client.exe --script --no-predict --lag-ms 100 --loss 0 --ticks 36 --log docs\media\baseline_no_prediction.txt
build\prediction_client.exe --script --lag-ms 100 --loss 0 --ticks 24 --log docs\media\prediction_enabled.txt
build\prediction_client.exe --script --cheat --lag-ms 180 --loss 4 --ticks 48 --log docs\media\correction_example.txt
taskkill /IM prediction_server.exe /F >nul 2>&1
echo PREDICTION_DEMO_OK
