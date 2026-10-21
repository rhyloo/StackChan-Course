@echo off
rem Demo menu: pick one, it is flashed to the robot and starts.  demo.bat [number]
if not "%~1"=="" goto run
echo Stack-chan course demos:
for /d %%d in ("%~dp0demos\*") do echo   %%~nxd
set /p N=Which one? (number): 
if "%N%"=="" exit /b 0
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\run_demo.ps1" %N%
exit /b %ERRORLEVEL%
:run
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\run_demo.ps1" %*
