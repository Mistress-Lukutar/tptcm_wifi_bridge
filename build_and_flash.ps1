#requires -Version 5.1
<#
.SYNOPSIS
    Build and flash the TPTCM WiFi Bridge firmware using ESP-IDF.

.DESCRIPTION
    The script sets up the ESP-IDF environment using the actually installed paths,
    builds the project, and flashes the resulting image to the specified COM port.
    Optionally, it can open the serial monitor right after flashing.

    By default the output is compact: only the result (OK / FAILED) and a short
    summary are shown. Use -FullOutput to see the complete idf.py log.

.PARAMETER Port
    COM port the board is connected to (e.g. COM3).

.PARAMETER Baud
    Flash baud rate. Defaults to 460800.

.PARAMETER Monitor
    If set, idf.py monitor is started after flashing.

.PARAMETER Clean
    If set, idf.py fullclean is run before building.

.PARAMETER BuildOnly
    If set, only build the project without flashing.

.PARAMETER FullOutput
    If set, show the full idf.py output. By default only a compact summary is shown.

.PARAMETER IdfPath
    Path to the installed ESP-IDF. Defaults to C:\esp\v6.0.2\esp-idf.

.PARAMETER IdfToolsPath
    Path to the ESP-IDF tools directory. Defaults to C:\Espressif.

.PARAMETER PythonEnvPath
    Path to the ESP-IDF Python virtual environment.
    Defaults to C:\Espressif\tools\python\v6.0.2\venv.

.EXAMPLE
    .\build_and_flash.ps1 -Port COM3

.EXAMPLE
    .\build_and_flash.ps1 -Port COM3 -Monitor -Clean

.EXAMPLE
    .\build_and_flash.ps1 -Clean -BuildOnly

.EXAMPLE
    .\build_and_flash.ps1 -Clean -BuildOnly -FullOutput
#>

[CmdletBinding()]
param (
    [Parameter(Mandatory = $false, HelpMessage = "COM port of the board, e.g. COM3")]
    [string]$Port,

    [Parameter(HelpMessage = "Flash baud rate")]
    [int]$Baud = 460800,

    [Parameter(HelpMessage = "Start monitor after flashing")]
    [switch]$Monitor,

    [Parameter(HelpMessage = "Run fullclean before building")]
    [switch]$Clean,

    [Parameter(HelpMessage = "Build only, do not flash")]
    [switch]$BuildOnly,

    [Parameter(HelpMessage = "Show full idf.py output")]
    [switch]$FullOutput,

    [Parameter(HelpMessage = "Path to ESP-IDF")]
    [string]$IdfPath = 'C:\esp\v6.0.2\esp-idf',

    [Parameter(HelpMessage = "Path to ESP-IDF tools")]
    [string]$IdfToolsPath = 'C:\Espressif',

    [Parameter(HelpMessage = "Path to ESP-IDF Python virtual environment")]
    [string]$PythonEnvPath = 'C:\Espressif\tools\python\v6.0.2\venv'
)

$ErrorActionPreference = 'Stop'

# --- Environment sanitization for non-PowerShell shells --------------------
# ESP-IDF (specifically idf_tools.py) refuses to run when it sees the MSYSTEM
# environment variable, which Git Bash / MSYS2 / MinGW inherit. Agents often
# invoke this script from such a shell. Strip the marker from this PowerShell
# session so the ESP-IDF activation below sees a plain Windows environment.
if (-not [string]::IsNullOrEmpty($env:MSYSTEM)) {
    Remove-Item Env:\MSYSTEM -ErrorAction SilentlyContinue
}

# --- Helper: run idf.py and handle output ---
function Invoke-IdfCommand {
    param (
        [Parameter(Mandatory = $true)]
        [string]$Label,

        [Parameter(Mandatory = $true)]
        [string]$LogFile,

        [Parameter(Mandatory = $true)]
        [array]$Arguments
    )

    Write-Host "$Label..." -ForegroundColor Cyan -NoNewline

    # Native commands may write warnings to stderr. With $ErrorActionPreference = 'Stop'
    # those warnings would become terminating errors, so temporarily allow them to flow.
    $prevErrorAction = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        if ($FullOutput) {
            & $env:PYTHON $idfPy @Arguments 2>&1 | Tee-Object -FilePath $LogFile
        } else {
            & $env:PYTHON $idfPy @Arguments > $LogFile 2>&1
        }
    } finally {
        $ErrorActionPreference = $prevErrorAction
    }

    $exitCode = $LASTEXITCODE

    if ($exitCode -eq 0) {
        Write-Host " OK" -ForegroundColor Green

        if (-not $FullOutput) {
            Show-CompactSummary -LogFile $LogFile
        }
    } else {
        Write-Host " FAILED (exit $exitCode)" -ForegroundColor Red

        if (-not $FullOutput) {
            Write-Host "---- Error log tail ----" -ForegroundColor Yellow
            Get-Content -Path $LogFile -Tail 60 | ForEach-Object { Write-Host $_ }
        }

        throw "$Label failed with exit code $exitCode"
    }
}

function Show-CompactSummary {
    param ([string]$LogFile)

    $tail = Get-Content -Path $LogFile -Tail 30

    # Binary size summary lines (app and bootloader).
    $sizeLines = $tail | Where-Object { $_ -match '\.bin binary size .* free' }
    if ($sizeLines) {
        $sizeLines | ForEach-Object { Write-Host "  $_" }
    }

    # Flash command hint for a successful build.
    $flashHint = $tail | Where-Object { $_ -match 'To flash, run:|python -m esptool --chip' } | Select-Object -First 1
    if ($flashHint) {
        Write-Host "  $flashHint"
    }
}

# --- Validation ---
if (-not $BuildOnly -and [string]::IsNullOrWhiteSpace($Port)) {
    throw "Port is required unless -BuildOnly is specified."
}

if (-not (Test-Path -Path $IdfPath -PathType Container)) {
    throw "ESP-IDF not found at: $IdfPath"
}

$exportScript = Join-Path $IdfPath 'export.ps1'
if (-not (Test-Path -Path $exportScript -PathType Leaf)) {
    throw "export.ps1 not found: $exportScript"
}

$pythonExe = Join-Path $PythonEnvPath 'Scripts\python.exe'
if (-not (Test-Path -Path $pythonExe -PathType Leaf)) {
    throw "ESP-IDF Python interpreter not found: $pythonExe"
}

$projectRoot = Split-Path -Parent $MyInvocation.MyCommand.Definition
Set-Location -LiteralPath $projectRoot

# --- Activate ESP-IDF environment ---
# export.ps1 calls the system 'python', so make sure the venv python comes first
# in PATH and set the key variables explicitly.
$env:IDF_PATH = $IdfPath
$env:IDF_TOOLS_PATH = $IdfToolsPath
$env:IDF_PYTHON_ENV_PATH = $PythonEnvPath
$env:PYTHON = $pythonExe
$env:ESP_ROM_ELF_DIR = Join-Path $IdfToolsPath 'tools\esp-rom-elfs\20241011'

# Directories idf.py expects to find in PATH.
$toolPaths = @(
    Join-Path $PythonEnvPath 'Scripts'
    Join-Path $IdfToolsPath 'tools\xtensa-esp-elf\esp-15.2.0_20251204\bin'
    Join-Path $IdfToolsPath 'tools\cmake\4.0.3\bin'
    Join-Path $IdfToolsPath 'tools\ninja\1.12.1'
    Join-Path $IdfPath 'tools'
)
foreach ($tp in $toolPaths) {
    if (Test-Path -Path $tp) {
        $env:PATH = "$tp;$($env:PATH)"
    }
}

# export.ps1 generates environment variables for the current PowerShell session.
$exportLog = Join-Path $env:TEMP "tptcm_export_$PID.log"
$prevErrorAction = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
try {
    if ($FullOutput) {
        . $exportScript 2>&1 | Tee-Object -FilePath $exportLog
    } else {
        . $exportScript > $exportLog 2>&1
    }
} finally {
    $ErrorActionPreference = $prevErrorAction
}

if (-not $env:PYTHON) {
    if (-not $FullOutput) {
        Write-Host "---- ESP-IDF activation log tail ----" -ForegroundColor Yellow
        Get-Content -Path $exportLog -Tail 40 | ForEach-Object { Write-Host $_ }
    }
    throw "PYTHON is not set after ESP-IDF activation"
}

$idfPy = Join-Path $env:IDF_PATH 'tools\idf.py'

# --- Build ---
$buildLog = Join-Path $env:TEMP "tptcm_build_$PID.log"

if ($Clean) {
    $cleanLog = Join-Path $env:TEMP "tptcm_clean_$PID.log"
    Invoke-IdfCommand -Label "Cleaning previous build" -LogFile $cleanLog -Arguments @('fullclean')
}

Invoke-IdfCommand -Label "Building TPTCM WiFi Bridge project" -LogFile $buildLog -Arguments @('build')

# --- Flash ---
if ($BuildOnly) {
    Write-Host "Build-only mode requested, skipping flash." -ForegroundColor Green
    return
}

$flashArgs = @('-p', $Port, '-b', "$Baud", 'flash')
if ($Monitor) {
    $flashArgs += 'monitor'
}

$flashLog = Join-Path $env:TEMP "tptcm_flash_$PID.log"
Invoke-IdfCommand -Label "Flashing board on port $Port" -LogFile $flashLog -Arguments $flashArgs

Write-Host "Done." -ForegroundColor Green
