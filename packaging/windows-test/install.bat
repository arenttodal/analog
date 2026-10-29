@echo off
rem ANDOR Analog test build: installs the VST3 (Common Files\VST3, where every Windows host
rem looks) and the standalone app (Program Files, with a Start menu entry). Both live in
rem Program Files, so this asks for administrator rights once.
net session >nul 2>&1
if errorlevel 1 (
    powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
    exit /b
)
cd /d "%~dp0"

set "VST3=%CommonProgramFiles%\VST3"
if not exist "%VST3%" mkdir "%VST3%"
if exist "%VST3%\ANDOR Analog.vst3" rmdir /s /q "%VST3%\ANDOR Analog.vst3"
xcopy /e /i /q /y "ANDOR Analog.vst3" "%VST3%\ANDOR Analog.vst3\" >nul
echo Installed ANDOR Analog.vst3  -^>  %VST3%

if exist "ANDOR Analog.exe" (
    set "APP=%ProgramFiles%\ANDOR Analog"
    call :app
)
echo.
echo Restart your DAW and rescan plug-ins. The standalone app is in the Start menu.
pause
exit /b

:app
if not exist "%APP%" mkdir "%APP%"
copy /y "ANDOR Analog.exe" "%APP%\" >nul
powershell -NoProfile -Command "$s = (New-Object -ComObject WScript.Shell).CreateShortcut([Environment]::GetFolderPath('CommonStartMenu') + '\Programs\ANDOR Analog.lnk'); $s.TargetPath = '%APP%\ANDOR Analog.exe'; $s.Save()"
echo Installed ANDOR Analog.exe  -^>  %APP%
exit /b
