Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$ProjectDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$PublishDir = Join-Path $ProjectDir "publish"
$PidPath = Join-Path $PublishDir "relay-server.pid"

if (-not (Test-Path $PidPath)) {
    Write-Host "No PID file found: $PidPath"
    exit 0
}

$pidValue = (Get-Content -Raw $PidPath).Trim()
if ([string]::IsNullOrWhiteSpace($pidValue)) {
    Write-Host "PID file is empty."
    exit 0
}

$process = Get-Process -Id ([int]$pidValue) -ErrorAction SilentlyContinue
if ($null -eq $process) {
    Write-Host "RelayServer process is not running: $pidValue"
    Remove-Item -LiteralPath $PidPath -Force
    exit 0
}

Stop-Process -Id ([int]$pidValue) -Force
Remove-Item -LiteralPath $PidPath -Force
Write-Host "RelayServer stopped: $pidValue"
