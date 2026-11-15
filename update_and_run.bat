@echo off
:: Self-elevate to Administrator if not already elevated
net session >nul 2>&1
if %errorlevel% neq 0 (
    echo Requesting Administrator privileges...
    powershell -Command "Start-Process '%~f0' -Verb RunAs"
    exit /b
)

echo ========================================================
echo Updating and Launching PowerConfigurator (Administrator)
echo ========================================================

echo Stopping running instances...
taskkill /f /im PowerConfigurator.exe >nul 2>&1
timeout /t 1 /nobreak >nul

echo Copying latest compiled binary...
copy /y "C:\Users\harsh\PowerConfigurator\build\bin\Release\PowerConfigurator.exe" "C:\Users\harsh\PowerConfigurator\PowerConfigurator.exe"

echo Copying LightFX.dll hardware driver...
if exist "C:\Users\harsh\AppData\Local\AlienFX Tools\LightFX\x64\LightFX.dll" (
    copy /y "C:\Users\harsh\AppData\Local\AlienFX Tools\LightFX\x64\LightFX.dll" "C:\Users\harsh\PowerConfigurator\LightFX.dll" >nul
    copy /y "C:\Users\harsh\AppData\Local\AlienFX Tools\LightFX\x64\LightFX.dll" "C:\Users\harsh\PowerConfigurator\build\bin\Release\LightFX.dll" >nul
)

echo Starting PowerConfigurator...
start "" "C:\Users\harsh\PowerConfigurator\PowerConfigurator.exe"

echo Update complete!
timeout /t 2 >nul
