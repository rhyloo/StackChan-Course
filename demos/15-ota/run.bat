@echo off
rem Demo 15 in one step: flash the OTA base over USB once; then send tests over the Wi-Fi (run.bat send leds).
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\..\tools\run_demo.ps1" 15 %*
