@echo off
pushd "%~dp0data"
if not exist "..\src_rebuild\bin\Release\WheelConfig.exe" (
    echo Compilez d'abord avec PowerShell : .\build-wheel.ps1
    pause
    popd
    exit /b 1
)
start "REDRIVER2 calibration" /wait "..\src_rebuild\bin\Release\WheelConfig.exe"
popd
