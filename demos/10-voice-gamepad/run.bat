@echo off
rem Demo 10 in one step: flash demo 02's firmware to the robot and start the relay with voice and PS4 gamepad.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\..\tools\run_demo.ps1" 10 %*
