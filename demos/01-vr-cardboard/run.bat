@echo off
rem Demo 01 en un paso: sube el firmware al robot y arranca la pasarela del móvil.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\..\tools\run_demo.ps1" 01 %*
