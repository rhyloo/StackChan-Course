@echo off
rem Demo 12 in one step: flash the mini TV to the robot and send it videos over Wi-Fi.
rem Also, without flashing: run.bat sample ^| add VIDEO ^| list ^| remove NAME
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\..\tools\run_demo.ps1" 12 %*
