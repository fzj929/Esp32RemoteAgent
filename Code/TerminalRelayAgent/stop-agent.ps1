$ErrorActionPreference = "Stop"

$projectDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$publishPath = Join-Path $projectDir "publish"
$pidPath = Join-Path $publishPath "terminal-agent.pid"

if (-not (Test-Path $pidPath)) {
    Write-Host "[agent] PID file not found: $pidPath"
    return
}

$pidValue = (Get-Content -Raw $pidPath).Trim()
if (-not $pidValue) {
    Remove-Item -LiteralPath $pidPath -Force
    return
}

$process = Get-Process -Id $pidValue -ErrorAction SilentlyContinue
if ($process) {
    Stop-Process -Id $pidValue -Force
    Write-Host "[agent] Stopped PID $pidValue"
}

Remove-Item -LiteralPath $pidPath -Force
