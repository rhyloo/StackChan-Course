@echo off
rem Demo 16 in one step: flash the math game to the robot and show its serial output.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\..\tools\run_demo.ps1" 16 %*
