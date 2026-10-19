@echo off
rem Menu de demos: eliges una, se sube al robot y arranca.  demo.bat [numero]
if not "%~1"=="" goto run
echo Demos del curso Stack-chan:
for /d %%d in ("%~dp0demos\*") do echo   %%~nxd
set /p N=Cual? (numero): 
if "%N%"=="" exit /b 0
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\run_demo.ps1" %N%
exit /b %ERRORLEVEL%
:run
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\run_demo.ps1" %*
