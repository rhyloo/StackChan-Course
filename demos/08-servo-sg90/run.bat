@echo off
rem Demo 08 in one step: flash the SG90 servo control to the robot and open the serial monitor.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\..\tools\run_demo.ps1" 08 %*
