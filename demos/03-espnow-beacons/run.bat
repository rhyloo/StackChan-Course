@echo off
rem Demo 03 in one step: flash the receiver to the robot and open the map. 'run.bat beacons' flashes the three beacons.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\..\tools\run_demo.ps1" 03 %*
