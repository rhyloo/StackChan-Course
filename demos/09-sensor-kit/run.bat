@echo off
rem Demo 09 in one step: flash the kit module reader to the robot and open the serial monitor.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\..\tools\run_demo.ps1" 09 %*
