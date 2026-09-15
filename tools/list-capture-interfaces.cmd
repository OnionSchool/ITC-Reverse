@echo off
setlocal

set "CAPTURE=%~1"
if "%CAPTURE%"=="" set "CAPTURE=%~dp0ITCCapture.exe"

if not exist "%CAPTURE%" (
  echo 找不到 ITCCapture.exe: "%CAPTURE%"
  exit /b 1
)

"%CAPTURE%" --list
