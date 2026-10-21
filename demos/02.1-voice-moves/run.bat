@echo off
rem Demo 02.1 in one step: flash the firmware to the robot and start the Gemini relay (head, moods and dance by voice).
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\..\tools\run_demo.ps1" 02.1 %*
