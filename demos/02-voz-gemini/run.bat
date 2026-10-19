@echo off
rem Demo 02 en un paso: sube el firmware al robot y arranca la pasarela con Gemini.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\..\tools\run_demo.ps1" 02 %*
