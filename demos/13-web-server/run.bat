@echo off
rem Demo 13 in one step: flash the web server to the robot and show its address (open it in a browser).
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\..\tools\run_demo.ps1" 13 %*
