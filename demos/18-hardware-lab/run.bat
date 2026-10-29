@echo off
rem Demo 18 in one step: flash the hardware lab to the robot and show its serial output.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\..\tools\run_demo.ps1" 18 %*
