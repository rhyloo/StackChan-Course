@echo off
rem Demo 11: drive the Keyestudio tank with the PS4 gamepad. "run.bat tank" flashes the firmware to the Arduino; "run.bat vr" adds Stack-chan on top with the VR viewer and voice.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\..\tools\run_demo.ps1" 11 %*
