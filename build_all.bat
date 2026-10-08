@echo off
setlocal
set PATH=C:\msys64\ucrt64\bin;%PATH%
set GPP=C:\msys64\ucrt64\bin\g++.exe
set FLAGS=-std=c++17 -Wall -Wextra -Isrc

if not exist build mkdir build

echo === test_protocol ===
%GPP% %FLAGS% tests\test_protocol.cpp -o build\test_protocol.exe -lws2_32
if errorlevel 1 exit /b 1

echo === test_telemetry ===
%GPP% %FLAGS% tests\test_telemetry.cpp src\telemetry.cpp -o build\test_telemetry.exe -lws2_32
if errorlevel 1 exit /b 1

echo === test_reliable_channel ===
%GPP% %FLAGS% tests\test_reliable_channel.cpp src\reliability\reliable_channel.cpp src\reliability\dedup_window.cpp -o build\test_reliable_channel.exe
if errorlevel 1 exit /b 1

echo === test_adaptive_timeout ===
%GPP% %FLAGS% tests\test_adaptive_timeout.cpp src\reliability\adaptive_timeout.cpp -o build\test_adaptive_timeout.exe
if errorlevel 1 exit /b 1

echo === test_dedup ===
%GPP% %FLAGS% tests\test_dedup.cpp src\reliability\dedup_window.cpp -o build\test_dedup.exe
if errorlevel 1 exit /b 1

echo === test_udp_reliability ===
%GPP% %FLAGS% tests\test_udp_reliability.cpp src\reliable_client.cpp src\reliability\reliable_channel.cpp src\reliability\adaptive_timeout.cpp src\reliability\dedup_window.cpp -o build\test_udp_reliability.exe -lws2_32
if errorlevel 1 exit /b 1

echo === server ===
%GPP% %FLAGS% src\server.cpp src\reliability\dedup_window.cpp -o build\server.exe -lws2_32
if errorlevel 1 exit /b 1

echo === client ===
%GPP% %FLAGS% src\client.cpp src\telemetry.cpp src\csv_logger.cpp src\reliable_client.cpp src\reliability\reliable_channel.cpp src\reliability\adaptive_timeout.cpp -o build\client.exe -lws2_32
if errorlevel 1 exit /b 1

echo BUILD_OK
exit /b 0
