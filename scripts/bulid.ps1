$ErrorActionPreference = 'Stop'

function Invoke-Cleanup {
  Write-Host "---END---" -ForegroundColor Yellow
  if (Get-Command deactivate -ErrorAction SilentlyContinue) { deactivate }
}


# Default settings
$Board = 'nucleo_f070rb'
$DT_OVERLAY = $null
$BuildDir = '.build'
$Clean = $false

# Helper
if ($args -contains '-h' -or $args -contains '--help' -or $args -contains '/?') {
@"
Usage:
  build.ps1 [-Board <name>] [-DT_OVERLAY <path>] [-BuildDir <path>] [-Clean]
  build.ps1 --board <name> --DT_OVERLAY <path> [--build-dir <path>] [--clean]

Examples:
  build.ps1 -Board nucleo_f070rb -DT_OVERLAY boards\nucleo_f070rb.overlay
  build.ps1 --board nucleo_f070rb --DT_OVERLAY boards/nucleo_f070rb.overlay --clean
"@ | Write-Host; exit 0
}

# $args parse
for ($i=0; $i -lt $args.Count; $i++) {
  $tok = $args[$i]
  switch -Regex ($tok) {
    '^(--board|-Board)$'            { $Board = $args[++$i]; continue }
    '^(--DT_OVERLAY|-DT_OVERLAY)$'  { $DT_OVERLAY = $args[++$i]; continue }
    '^(--build-dir|-BuildDir)$'     { $BuildDir = $args[++$i]; continue }
    '^(--clean|-Clean|--pristine)$' { $Clean = $true; continue }
    default {
      if ($tok -like '-*') { Write-Warning "Bilinmeyen argüman: $tok" }
    }
  }
}

# Proje root
$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path

# Zephyr path
$ZephyrBase = "C:\Users\Admin\Developer\sdk\zephyr\zephyrproject\zephyr"
$SdkDir     = "C:\Users\Admin\Developer\sdk\zephyr\zephyr-sdk-0.17.2"

$ZephyrVenvActivate = Join-Path (Join-Path $ZephyrBase '..\.venv\Scripts') 'Activate.ps1'

# Envrioments
if (-not $env:ZEPHYR_BASE)            { $env:ZEPHYR_BASE = $ZephyrBase }
if (-not $env:ZEPHYR_SDK_INSTALL_DIR) { $env:ZEPHYR_SDK_INSTALL_DIR = $SdkDir }

# venv activation
$venvCandidates = @(
  (Join-Path $ProjectRoot '.venv\Scripts\Activate.ps1'),
  $ZephyrVenvActivate
)

$venv = $venvCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if ($venv) {
  . $venv
} else {
  Write-Warning "not found venv: $($venvCandidates -join ', ')"
  $WestScripts = Split-Path $ZephyrVenvActivate -Parent
  if (Test-Path $WestScripts -and $env:PATH -notlike "*$WestScripts*") {
    $env:PATH = "$WestScripts;$env:PATH"
  }
}

# west folder
if (-not (Get-Command west -ErrorAction SilentlyContinue)) { throw "west not found." }

# Overlay: if not provided, according to the board 
if (-not $DT_OVERLAY) { $DT_OVERLAY = "boards/$Board.overlay" }

# Resolve overlay path
$overlayCandidate = $DT_OVERLAY
if (-not (Split-Path $overlayCandidate -IsAbsolute)) { $overlayCandidate = Join-Path $ProjectRoot $overlayCandidate }
if (-not (Test-Path $overlayCandidate)) { throw "Overlay not found: $overlayCandidate" }
$overlayPath = (Resolve-Path $overlayCandidate).Path -replace '\\','/'
$cmakeOverlay = "-DEXTRA_DTC_OVERLAY_FILE=$overlayPath"
if ($overlayPath -match '\s') { $cmakeOverlay = "-DEXTRA_DTC_OVERLAY_FILE=`"$overlayPath`"" }

# Build dizini
if (-not (Split-Path $BuildDir -IsAbsolute)) { $BuildDir = Join-Path $ProjectRoot $BuildDir }
New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null

# Clean
$preserve = if ($Clean) { 'always' } else { 'auto' }

# west args
$westArgs = @(
  'build','-p', $preserve,
  '-b', $Board,
  $ProjectRoot,
  '--build-dir', $BuildDir,
  '--',
  '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON',
  $cmakeOverlay
)

# Run
$code = 0
try {
  Write-Host "Running: west $($westArgs -join ' ')" -ForegroundColor Cyan
  & west @westArgs
  $code = $LASTEXITCODE
}
finally {
  if ($script:CtrlCHandler) {
    [Console]::remove_CancelKeyPress($script:CtrlCHandler)
  }
  Invoke-Cleanup

  if ($script:Cancelled) {
    Write-Host ">> process cancelled" -ForegroundColor Yellow
    exit 130   
  } else {
    exit $code
  }
}


