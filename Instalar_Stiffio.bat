@echo off
set target=%~dp0dist\StiffioApp.exe

for /f "delims=" %%i in ('powershell -command "[Environment]::GetFolderPath('Desktop')"') do set desktop=%%i

set shortcut=%desktop%\StiffioApp.lnk

powershell ^
$s=(New-Object -COM WScript.Shell).CreateShortcut('%shortcut%'); ^
$s.TargetPath='%target%'; ^
$s.WorkingDirectory='%~dp0dist'; ^
$s.IconLocation='%target%'; ^
$s.Save()