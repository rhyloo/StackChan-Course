@echo off
rem Demo 08 en un paso: sube el control del servo SG90 al robot y abre el monitor serie.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\..\tools\run_demo.ps1" 08 %*
