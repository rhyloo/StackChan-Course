@echo off
rem Demo 03 en un paso: sube el receptor al robot y abre el mapa. 'run.sh balizas' flashea las tres balizas.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\..\tools\run_demo.ps1" 03 %*
