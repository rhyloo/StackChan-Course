@echo off
rem Demo 06 in one step: flash the infrared firmware to the robot and show the codes it reads.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\..\tools\run_demo.ps1" 06 %*
