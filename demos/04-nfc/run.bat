@echo off
rem Demo 04 en un paso: sube el lector NFC al robot y muestra lo que lee.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\..\tools\run_demo.ps1" 04 %*
