param(
    [Parameter(Mandatory = $true)]
    [string]$WifiSsid,

    [Parameter(Mandatory = $true)]
    [string]$WifiPassword,

    [Parameter(Mandatory = $true)]
    [string]$ServerHost,

    [string]$Port = "COM5",
    [string]$BoardId = "S3-0001",
    [string]$BoardKey = "CHANGE_THIS_DEVICE_SECRET",
    [int]$AssignedPublicPort = 6500,
    [string]$TerminalHost = "192.168.77.2",
    [int]$TerminalPort = 3389,
    [int]$ControlPort = 6555,
    [string]$IdfRoot = "C:\Espressif\frameworks\esp-idf-v5.3.1-2",
    [string]$IdfToolsPath = "C:\Espressif",
    [string]$IdfPython = "C:\Espressif\python_env\idf5.3_py3.11_env\Scripts\python.exe",
    [string]$TempProjectPath = "C:\tmp\Esp32RemoteAgentBuild",
    [switch]$Monitor,
    [switch]$EraseFlash
)

$ErrorActionPreference = "Stop"

function Assert-PathExists {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,

        [Parameter(Mandatory = $true)]
        [string]$Description
    )

    if (-not (Test-Path -LiteralPath $Path)) {
        throw "$Description not found: $Path"
    }
}

function Set-KconfigValue {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Content,

        [Parameter(Mandatory = $true)]
        [string]$Name,

        [Parameter(Mandatory = $true)]
        [string]$Value
    )

    $escapedValue = $Value.Replace('\', '\\').Replace('"', '\"')
    $line = "$Name=`"$escapedValue`""
    if ($Content -match "(?m)^$([regex]::Escape($Name))=") {
        return [regex]::Replace($Content, "(?m)^$([regex]::Escape($Name))=.*$", $line)
    }

    return $Content.TrimEnd() + [Environment]::NewLine + $line + [Environment]::NewLine
}

function Set-KconfigInt {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Content,

        [Parameter(Mandatory = $true)]
        [string]$Name,

        [Parameter(Mandatory = $true)]
        [int]$Value
    )

    $line = "$Name=$Value"
    if ($Content -match "(?m)^$([regex]::Escape($Name))=") {
        return [regex]::Replace($Content, "(?m)^$([regex]::Escape($Name))=.*$", $line)
    }

    return $Content.TrimEnd() + [Environment]::NewLine + $line + [Environment]::NewLine
}

$sourceProject = $PSScriptRoot
$idfPy = Join-Path $IdfRoot "tools\idf.py"
$idfExport = Join-Path $IdfRoot "export.ps1"

Assert-PathExists -Path $sourceProject -Description "Firmware source project"
Assert-PathExists -Path $idfExport -Description "ESP-IDF export script"
Assert-PathExists -Path $idfPy -Description "ESP-IDF idf.py"
Assert-PathExists -Path $IdfPython -Description "ESP-IDF Python environment"

if (-not $TempProjectPath.StartsWith("C:\tmp\", [StringComparison]::OrdinalIgnoreCase)) {
    throw "TempProjectPath must be under C:\tmp to keep cleanup safe. Current value: $TempProjectPath"
}

if (Test-Path -LiteralPath $TempProjectPath) {
    $resolvedTemp = (Resolve-Path -LiteralPath $TempProjectPath).Path
    if (-not $resolvedTemp.StartsWith("C:\tmp\", [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to remove unexpected path: $resolvedTemp"
    }
    Remove-Item -LiteralPath $TempProjectPath -Recurse -Force
}

New-Item -ItemType Directory -Path $TempProjectPath | Out-Null
Copy-Item -LiteralPath (Join-Path $sourceProject "CMakeLists.txt") -Destination $TempProjectPath
Copy-Item -LiteralPath (Join-Path $sourceProject "sdkconfig.defaults") -Destination $TempProjectPath
Copy-Item -LiteralPath (Join-Path $sourceProject "main") -Destination $TempProjectPath -Recurse

$defaultsPath = Join-Path $TempProjectPath "sdkconfig.defaults"
$defaults = Get-Content -LiteralPath $defaultsPath -Raw
$defaults = Set-KconfigValue -Content $defaults -Name "CONFIG_REMOTE_AGENT_WIFI_SSID" -Value $WifiSsid
$defaults = Set-KconfigValue -Content $defaults -Name "CONFIG_REMOTE_AGENT_WIFI_PASSWORD" -Value $WifiPassword
$defaults = Set-KconfigValue -Content $defaults -Name "CONFIG_REMOTE_AGENT_SERVER_HOST" -Value $ServerHost
$defaults = Set-KconfigInt -Content $defaults -Name "CONFIG_REMOTE_AGENT_SERVER_CONTROL_PORT" -Value $ControlPort
$defaults = Set-KconfigValue -Content $defaults -Name "CONFIG_REMOTE_AGENT_BOARD_ID" -Value $BoardId
$defaults = Set-KconfigValue -Content $defaults -Name "CONFIG_REMOTE_AGENT_BOARD_KEY" -Value $BoardKey
$defaults = Set-KconfigInt -Content $defaults -Name "CONFIG_REMOTE_AGENT_ASSIGNED_PUBLIC_PORT" -Value $AssignedPublicPort
$defaults = Set-KconfigValue -Content $defaults -Name "CONFIG_REMOTE_AGENT_TERMINAL_RDP_HOST" -Value $TerminalHost
$defaults = Set-KconfigInt -Content $defaults -Name "CONFIG_REMOTE_AGENT_TERMINAL_RDP_PORT" -Value $TerminalPort

$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllText($defaultsPath, $defaults, $utf8NoBom)

$env:PROCESSOR_ARCHITECTURE = if ($env:PROCESSOR_ARCHITECTURE) { $env:PROCESSOR_ARCHITECTURE } else { "AMD64" }
$env:IDF_TOOLS_PATH = $IdfToolsPath
$env:IDF_PYTHON_ENV_PATH = Split-Path -Parent (Split-Path -Parent $IdfPython)

. $idfExport

$idfArgs = @("-p", $Port)
if ($EraseFlash) {
    $idfArgs += "erase-flash"
}
$idfArgs += @("build", "flash")
if ($Monitor) {
    $idfArgs += "monitor"
}

Write-Host "[firmware] Source project: $sourceProject"
Write-Host "[firmware] Temp build path: $TempProjectPath"
Write-Host "[firmware] Port: $Port"
Write-Host "[firmware] Server: $ServerHost`:$ControlPort"
Write-Host "[firmware] Board: $BoardId public port $AssignedPublicPort"
Write-Host "[firmware] WiFi SSID: $WifiSsid"
Write-Host "[firmware] WiFi password is not printed."

& $IdfPython $idfPy @idfArgs
if ($LASTEXITCODE -ne 0) {
    throw "idf.py failed with exit code $LASTEXITCODE"
}
