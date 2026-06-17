@echo off
echo PhoneVR Driver Installer
echo ========================

:: Find SteamVR drivers path
set STEAMVR_DRIVERS=%ProgramFiles(x86)%\Steam\steamapps\common\SteamVR\drivers

if not exist "%STEAMVR_DRIVERS%" (
    echo ERROR: SteamVR not found at default path.
    echo Please manually copy the "phonevr" folder to your SteamVR\drivers\ directory.
    pause
    exit /b 1
)

echo Installing to: %STEAMVR_DRIVERS%\phonevr

:: Remove old install if present
if exist "%STEAMVR_DRIVERS%\phonevr" (
    echo Removing old install...
    rmdir /s /q "%STEAMVR_DRIVERS%\phonevr"
)

:: Copy driver files
xcopy /E /I /Y "phonevr" "%STEAMVR_DRIVERS%\phonevr"

echo.
echo Done! Driver installed.
echo Start SteamVR and launch the PhoneVR app on your phone.
pause
