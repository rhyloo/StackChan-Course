@echo off
rem Demo 14: build the micro:bit program, copy it to the MICROBIT drive and open the arm's web page.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\..\tools\run_demo.ps1" 14 %*
