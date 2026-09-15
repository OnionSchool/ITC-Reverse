@echo off
setlocal

if "%~1"=="" (
  echo 用法: %~nx0 ^<接口编号^> [输出文件] [ITCCapture.exe 路径]
  exit /b 1
)

set "INTERFACE=%~1"
set "OUTPUT=%~2"
set "CAPTURE=%~3"
if "%OUTPUT%"=="" set "OUTPUT=%~dp0captures\itc.pcap"
if "%CAPTURE%"=="" set "CAPTURE=%~dp0ITCCapture.exe"

if not exist "%CAPTURE%" (
  echo 找不到 ITCCapture.exe: "%CAPTURE%"
  exit /b 1
)

for %%I in ("%OUTPUT%") do if not exist "%%~dpI" mkdir "%%~dpI"

echo 只读抓包已启动，不会发送任何网络数据。
echo 接口: %INTERFACE%
echo 按 Ctrl+C 停止抓包。
"%CAPTURE%" --interface %INTERFACE% --output "%OUTPUT%"

if exist "%OUTPUT%" (
  certutil -hashfile "%OUTPUT%" SHA256 > "%OUTPUT%.sha256"
  echo 已保存: "%OUTPUT%"
)
