@echo off
pushd "%~dp0data"
if not exist "..\src_rebuild\bin\Release\REDRIVER2.exe" (
    echo Compilez d'abord avec PowerShell : .\build-wheel.ps1
    pause
    popd
    exit /b 1
)
start "REDRIVER2" /wait "..\src_rebuild\bin\Release\REDRIVER2.exe" %*
popd
