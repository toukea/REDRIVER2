@echo off
rem Compile REDRIVER2 (volant G29). Journal complet : build-wheel.log
cd /d "%~dp0"
echo Compilation en cours, patientez quelques minutes...
powershell -NoProfile -ExecutionPolicy Bypass -File build-wheel.ps1 > build-wheel.log 2>&1
set RESULT=%errorlevel%
type build-wheel.log
echo BUILD_DONE %RESULT%>> build-wheel.log
if not "%RESULT%"=="0" (echo. & echo ECHEC DE LA COMPILATION - voir build-wheel.log) else (echo. & echo COMPILATION REUSSIE - lancer start-wheel-game.cmd)
pause
