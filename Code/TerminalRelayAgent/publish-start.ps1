param(
    [string]$Configuration = "Release",
    [string]$Runtime = "win-x64",
    [string]$PublishDir = ".\publish",
    [int]$WebPort = 19090,
    [switch]$SelfContained,
    [switch]$NoStart
)

$ErrorActionPreference = "Stop"

$projectDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$publishPath = Join-Path $projectDir $PublishDir
$pidPath = Join-Path $publishPath "terminal-agent.pid"
$exePath = Join-Path $publishPath "TerminalRelayAgent.exe"
$dllPath = Join-Path $publishPath "TerminalRelayAgent.dll"

Push-Location $projectDir
try {
    $publishArgs = @(
        "publish",
        ".\TerminalRelayAgent.csproj",
        "-c", $Configuration,
        "-r", $Runtime,
        "-o", $publishPath,
        "/p:PublishSingleFile=false"
    )

    if ($SelfContained) {
        $publishArgs += "--self-contained"
        $publishArgs += "true"
    } else {
        $publishArgs += "--self-contained"
        $publishArgs += "false"
    }

    & dotnet @publishArgs
    if ($LASTEXITCODE -ne 0) {
        throw "dotnet publish failed with exit code $LASTEXITCODE"
    }

    $envSettings = @{
        "ASPNETCORE_URLS" = "http://0.0.0.0:$WebPort"
    }
    $envSettings | ConvertTo-Json | Set-Content -Path (Join-Path $publishPath "run.settings.json") -Encoding UTF8

    if ($NoStart) {
        Write-Host "[agent] Published to $publishPath"
        Write-Host "[agent] Start manually with: TerminalRelayAgent.exe"
        return
    }

    if (Test-Path $pidPath) {
        $oldPid = (Get-Content -Raw $pidPath).Trim()
        if ($oldPid) {
            $oldProcess = Get-Process -Id $oldPid -ErrorAction SilentlyContinue
            if ($oldProcess) {
                Stop-Process -Id $oldPid -Force
            }
        }
    }

    $startInfo = @{
        FilePath = if (Test-Path $exePath) { $exePath } else { "dotnet" }
        WorkingDirectory = $publishPath
        WindowStyle = "Hidden"
        PassThru = $true
    }
    if (-not (Test-Path $exePath)) {
        $startInfo.ArgumentList = "`"$dllPath`""
    }

    $env:ASPNETCORE_URLS = "http://0.0.0.0:$WebPort"
    $process = Start-Process @startInfo
    Set-Content -Path $pidPath -Value $process.Id -Encoding ASCII

    Write-Host "[agent] Published to $publishPath"
    Write-Host "[agent] Started PID $($process.Id)"
    Write-Host "[agent] Web UI: http://127.0.0.1:$WebPort"
}
finally {
    Pop-Location
}
