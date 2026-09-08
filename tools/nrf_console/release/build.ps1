#requires -Version 5.1
<#
.SYNOPSIS
  Build a Windows release of nrf_console (PyInstaller onedir).

.EXAMPLE
  .\release\build.ps1
  .\release\build.ps1 -Console
  .\release\build.ps1 -Clean
#>
[CmdletBinding()]
param(
    [switch]$Console,
    [switch]$Clean,
    [string]$Python = ""
)

$ErrorActionPreference = "Stop"
$ReleaseDir = $PSScriptRoot
$Root = Split-Path -Parent $ReleaseDir
Set-Location $Root

function Resolve-Python {
    param([string]$Hint)
    if ($Hint -and (Test-Path $Hint)) { return $Hint }
    if (Get-Command py -ErrorAction SilentlyContinue) {
        $cand = & py -3 -c "import sys; print(sys.executable)" 2>$null
        if ($LASTEXITCODE -eq 0 -and $cand) { return $cand.Trim() }
    }
    if (Get-Command python -ErrorAction SilentlyContinue) {
        return (Get-Command python).Source
    }
    throw "Python 3 not found. Install Python 3.11+ or pass -Python path."
}

$py = Resolve-Python -Hint $Python
Write-Host "Python: $py"
Write-Host "Root:   $Root"

& $py -m pip install -q -r (Join-Path $Root "requirements.txt")
& $py -m pip install -q "pyinstaller>=6.0"

$dist = Join-Path $ReleaseDir "dist"
$work = Join-Path $ReleaseDir "build"
$spec = Join-Path $ReleaseDir "nrf_console.spec"

if ($Clean) {
    if (Test-Path $dist) { Remove-Item -Recurse -Force $dist }
    if (Test-Path $work) { Remove-Item -Recurse -Force $work }
}

$args = @(
    "-m", "PyInstaller",
    "--noconfirm",
    "--distpath", $dist,
    "--workpath", $work,
    $spec
)

if ($Console) {
    $env:NRF_CONSOLE_CONSOLE = "1"
    Write-Host "Console build (NRF_CONSOLE_CONSOLE=1)"
} else {
    Remove-Item Env:NRF_CONSOLE_CONSOLE -ErrorAction SilentlyContinue
}

Write-Host ">> $py $($args -join ' ')"
& $py @args
$buildExit = $LASTEXITCODE
Remove-Item Env:NRF_CONSOLE_CONSOLE -ErrorAction SilentlyContinue
if ($buildExit -ne 0) {
    throw "PyInstaller failed with exit $buildExit"
}

# Optional: patch console=True by rebuilding with --console override is awkward
# via spec; if -Console, rewrite a temp one-liner note.
$outDir = Join-Path $dist "nrf_console"
$exe = Join-Path $outDir "nrf_console.exe"
if (-not (Test-Path $exe)) {
    throw "Missing output: $exe"
}

# Version stamp
$ver = & $py -c "from nrf_console import __version__; print(__version__)"
$stamp = Join-Path $outDir "VERSION.txt"
@(
    "nrf_console $ver"
    "built=$(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')"
    "python=$(& $py -c 'import sys; print(sys.version.split()[0])')"
) | Set-Content -Encoding utf8 $stamp

Write-Host ""
Write-Host "Release OK:"
Write-Host "  $exe"
Write-Host "Copy the whole folder to ship:"
Write-Host "  $outDir"
Write-Host ""
Write-Host "Requires: Windows Bluetooth + WebView2 Runtime"
if ($Console) {
    Write-Host "Built WITH console window (-Console)."
}
