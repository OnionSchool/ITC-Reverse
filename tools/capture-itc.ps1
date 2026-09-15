param(
  [Parameter(Mandatory = $true)]
  [string]$Interface,
  [string]$Dumpcap = ".\dumpcap.exe",
  [string]$OutputDirectory = ".\captures"
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path -LiteralPath $Dumpcap)) {
  throw "找不到 dumpcap.exe: $Dumpcap"
}

New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
$capture = Join-Path $OutputDirectory ("itc-" + $timestamp + ".pcapng")
$filter = "tcp port 8000 or tcp port 15001 or (udp and dst net 225.101.1.0/24)"

Write-Host "只读抓包已启动，不会发送任何网络数据。"
Write-Host "接口: $Interface"
Write-Host "过滤器: $filter"
Write-Host "按 Ctrl+C 停止。"

& $Dumpcap -i $Interface -f $filter -w $capture

if (Test-Path -LiteralPath $capture) {
  $hash = Get-FileHash -Algorithm SHA256 -LiteralPath $capture
  $hash.Path + "`t" + $hash.Hash | Out-File -Encoding ascii ($capture + ".sha256")
  Write-Host "已保存: $capture"
}
