@echo off
rem Demo 07 in one step: flash the photo booth to the robot and start the relay with the filters.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\..\tools\run_demo.ps1" 07 %*
