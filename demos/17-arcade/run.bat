@echo off
rem Demo 17 in one step: flash the arcade to the robot and show its serial output.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\..\tools\run_demo.ps1" 17 %*
