@echo off
if not exist "%~dp0.venv\Scripts\python.exe" (echo Primero ejecuta setup.bat & exit /b 1)
"%~dp0.venv\Scripts\python.exe" "%~dp0relay\relay.py" %*
