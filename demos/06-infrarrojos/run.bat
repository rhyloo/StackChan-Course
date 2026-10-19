@echo off
rem Demo 06 en un paso: sube el firmware de infrarrojos al robot y muestra los codigos que lee.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\..\tools\run_demo.ps1" 06 %*
