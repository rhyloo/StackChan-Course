@echo off
set ROOT=%~dp0..\..
if not exist "%ROOT%\.venv\Scripts\python.exe" (echo Run setup.bat first & exit /b 1)
"%ROOT%\.venv\Scripts\python.exe" "%~dp0relay\main.py" %*
