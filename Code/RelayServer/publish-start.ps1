param(
    [string]$Configuration = "Release",
    [int]$HttpPort = 18080,
    [int]$HttpsPort = 18443,
    [int]$ControlPort = 6555,
    [string]$CertificatePassword = "",
    [switch]$ForceCertificate,
    [switch]$NoStart
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$ProjectDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectFile = Join-Path $ProjectDir "RelayServer.csproj"
$PublishDir = Join-Path $ProjectDir "publish"
$CertDir = Join-Path $ProjectDir "certs"
$CertPath = Join-Path $CertDir "relay-server.pfx"
$PasswordPath = Join-Path $CertDir "relay-server.password.txt"
$PidPath = Join-Path $PublishDir "relay-server.pid"
$LogPath = Join-Path $PublishDir "relay-server.log"
$ErrorLogPath = Join-Path $PublishDir "relay-server.err.log"
$AdminPasswordPath = Join-Path $PublishDir "admin.bootstrap.password.txt"

function New-RandomPassword {
    $bytes = New-Object byte[] 32
    $rng = [System.Security.Cryptography.RandomNumberGenerator]::Create()
    try {
        $rng.GetBytes($bytes)
    } finally {
        $rng.Dispose()
    }
    return [Convert]::ToBase64String($bytes)
}

function Get-CertificatePassword {
    if (-not [string]::IsNullOrWhiteSpace($CertificatePassword)) {
        return $CertificatePassword
    }

    if ((Test-Path $PasswordPath) -and -not $ForceCertificate) {
        return (Get-Content -Raw $PasswordPath).Trim()
    }

    $password = New-RandomPassword
    Set-Content -Path $PasswordPath -Value $password -Encoding UTF8
    return $password
}

function Get-AdminBootstrapPassword {
    if (Test-Path $AdminPasswordPath) {
        return (Get-Content -Raw $AdminPasswordPath).Trim()
    }

    $password = New-RandomPassword
    Set-Content -Path $AdminPasswordPath -Value $password -Encoding UTF8
    return $password
}

function New-HttpsCertificate {
    param([string]$Password)

    if ((Test-Path $CertPath) -and -not $ForceCertificate) {
        Write-Host "[cert] Reusing existing certificate: $CertPath"
        return
    }

    Write-Host "[cert] Generating self-signed HTTPS certificate..."
    if (Test-Path $CertPath) {
        Remove-Item -LiteralPath $CertPath -Force
    }

    $isWindowsHost = $env:OS -eq "Windows_NT"
    if (Get-Variable -Name IsWindows -ErrorAction SilentlyContinue) {
        $isWindowsHost = $IsWindows
    }

    if ($isWindowsHost) {
        $securePassword = ConvertTo-SecureString -String $Password -Force -AsPlainText
        $dnsNames = @("localhost", $env:COMPUTERNAME)
        $cert = New-SelfSignedCertificate `
            -DnsName $dnsNames `
            -CertStoreLocation "Cert:\CurrentUser\My" `
            -KeyAlgorithm RSA `
            -KeyLength 2048 `
            -KeyExportPolicy Exportable `
            -KeySpec KeyExchange `
            -NotAfter (Get-Date).AddYears(5) `
            -FriendlyName "ESP32 Relay Server HTTPS"

        Export-PfxCertificate -Cert $cert -FilePath $CertPath -Password $securePassword | Out-Null
        Remove-Item -LiteralPath ("Cert:\CurrentUser\My\" + $cert.Thumbprint) -Force
    } else {
        & dotnet dev-certs https -ep $CertPath -p $Password
        if ($LASTEXITCODE -ne 0) {
            throw "Failed to generate HTTPS certificate with dotnet dev-certs."
        }
    }

    Write-Host "[cert] Certificate written: $CertPath"
}

function Write-ProductionSettings {
    param(
        [string]$Password,
        [string]$AdminPassword
    )

    $settingsPath = Join-Path $PublishDir "appsettings.Production.json"
    $escapedCertPath = $CertPath.Replace("\", "\\")
    $escapedPassword = $Password.Replace("\", "\\").Replace('"', '\"')
    $escapedAdminPassword = $AdminPassword.Replace("\", "\\").Replace('"', '\"')
    $json = @"
{
  "Admin": {
    "BootstrapUsername": "admin",
    "BootstrapPassword": "$escapedAdminPassword"
  },
  "Relay": {
    "ControlPort": $ControlPort,
    "PublicPortMin": 6500,
    "PublicPortMax": 6600,
    "ReservedPorts": [6555],
    "DefaultTargetHost": "192.168.77.2",
    "DefaultTargetPort": 3389,
    "HeartbeatTimeoutSeconds": 45,
    "DatabasePath": "relay.db"
  },
  "Kestrel": {
    "Endpoints": {
      "Http": {
        "Url": "http://0.0.0.0:$HttpPort"
      },
      "Https": {
        "Url": "https://0.0.0.0:$HttpsPort",
        "Certificate": {
          "Path": "$escapedCertPath",
          "Password": "$escapedPassword"
        }
      }
    }
  }
}
"@
    Set-Content -Path $settingsPath -Value $json -Encoding UTF8
    Write-Host "[config] Wrote HTTPS Kestrel config: $settingsPath"
}

function Stop-ExistingProcess {
    if (-not (Test-Path $PidPath)) {
        return
    }

    $existingPid = (Get-Content -Raw $PidPath).Trim()
    if ([string]::IsNullOrWhiteSpace($existingPid)) {
        return
    }

    $process = Get-Process -Id ([int]$existingPid) -ErrorAction SilentlyContinue
    if ($null -ne $process) {
        Write-Host "[run] Stopping previous RelayServer process: $existingPid"
        Stop-Process -Id ([int]$existingPid) -Force
        Start-Sleep -Seconds 1
    }
}

function Start-RelayServer {
    Stop-ExistingProcess

    $exe = Join-Path $PublishDir "RelayServer.exe"
    if (-not (Test-Path $exe)) {
        $exe = "dotnet"
        $arguments = @((Join-Path $PublishDir "RelayServer.dll"))
    } else {
        $arguments = @()
    }

    Write-Host "[run] Starting RelayServer..."
    $startArgs = @{
        FilePath = $exe
        WorkingDirectory = $PublishDir
        RedirectStandardOutput = $LogPath
        RedirectStandardError = $ErrorLogPath
        WindowStyle = "Hidden"
        PassThru = $true
    }
    if ($arguments.Count -gt 0) {
        $startArgs.ArgumentList = $arguments
    }

    $process = Start-Process @startArgs

    Set-Content -Path $PidPath -Value $process.Id -Encoding ASCII
    Start-Sleep -Seconds 2

    if ($null -eq (Get-Process -Id $process.Id -ErrorAction SilentlyContinue)) {
        Write-Host "[run] RelayServer exited during startup."
        if (Test-Path $LogPath) { Get-Content $LogPath }
        if (Test-Path $ErrorLogPath) { Get-Content $ErrorLogPath }
        throw "RelayServer failed to start."
    }

    Write-Host "[run] RelayServer started. PID: $($process.Id)"
    Write-Host "[url] HTTP : http://127.0.0.1:$HttpPort"
    Write-Host "[url] HTTPS: https://127.0.0.1:$HttpsPort"
    Write-Host "[tcp] Board control port: $ControlPort"
}

New-Item -ItemType Directory -Force -Path $PublishDir | Out-Null
New-Item -ItemType Directory -Force -Path $CertDir | Out-Null

$password = Get-CertificatePassword
New-HttpsCertificate -Password $password

Write-Host "[frontend] Vue3 static files are in wwwroot and will be included in dotnet publish."
Write-Host "[publish] Publishing $ProjectFile ..."
& dotnet publish $ProjectFile -c $Configuration -o $PublishDir --self-contained false
if ($LASTEXITCODE -ne 0) {
    throw "dotnet publish failed."
}

$adminPassword = Get-AdminBootstrapPassword
Write-ProductionSettings -Password $password -AdminPassword $adminPassword
Write-Host "[admin] Initial admin username: admin"
Write-Host "[admin] Initial admin password file: $AdminPasswordPath"

if ($NoStart) {
    Write-Host "[done] Published without starting. Publish directory: $PublishDir"
    exit 0
}

Start-RelayServer
