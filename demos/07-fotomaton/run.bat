@echo off
rem Demo 07 en un paso: sube el fotomaton al robot y arranca la pasarela con los filtros.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\..\tools\run_demo.ps1" 07 %*
