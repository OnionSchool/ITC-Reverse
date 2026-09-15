param(
  [string]$Dumpcap = ".\dumpcap.exe"
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path -LiteralPath $Dumpcap)) {
  throw "找不到 dumpcap.exe: $Dumpcap"
}

& $Dumpcap -D
