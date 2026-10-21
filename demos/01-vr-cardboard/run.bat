@echo off
rem Demo 01 in one step: flash the firmware to the robot and start the phone relay.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\..\tools\run_demo.ps1" 01 %*
