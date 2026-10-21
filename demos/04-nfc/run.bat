@echo off
rem Demo 04 in one step: flash the NFC reader to the robot and show what it reads.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\..\tools\run_demo.ps1" 04 %*
