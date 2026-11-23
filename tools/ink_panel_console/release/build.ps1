#requires -Version 5.1
<#
.SYNOPSIS
  Build a Windows release of ink_panel_console (PyInstaller onedir).

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
$NrfRoot = Join-Path (Split-Path -Parent $Root) "nrf_console"
Set-Location $Root

function Resolve-Python {
    param([string]$Hint)
    if ($Hint -and (Test-Path $Hint)) { return $Hint }
    $venvPy = Join-Path $NrfRoot ".venv\Scripts\python.exe"
    if (Test-Path $venvPy) { return $venvPy }
    $localVenv = Join-Path $Root ".venv\Scripts\python.exe"
    if (Test-Path $localVenv) { return $localVenv }
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
Write-Host "nrf:    $NrfRoot"

if (-not (Test-Path (Join-Path $NrfRoot "nrf_console"))) {
    throw "Sibling tools/nrf_console missing — required to bundle BLE/protocol."
}

& $py -m pip install -q -r (Join-Path $Root "requirements.txt")
& $py -m pip install -q -r (Join-Path $NrfRoot "requirements.txt")
& $py -m pip install -q "pyinstaller>=6.0"

$dist = Join-Path $ReleaseDir "dist"
$work = Join-Path $ReleaseDir "build"
$spec = Join-Path $ReleaseDir "ink_panel_console.spec"

if ($Clean) {
    if (Test-Path $dist) { Remove-Item -Recurse -Force $dist }
    if (Test-Path $work) { Remove-Item -Recurse -Force $work }
}

if ($Console) {
    $env:INK_PANEL_CONSOLE_CONSOLE = "1"
    Write-Host "Console build (INK_PANEL_CONSOLE_CONSOLE=1)"
} else {
    Remove-Item Env:INK_PANEL_CONSOLE_CONSOLE -ErrorAction SilentlyContinue
}

$args = @(
    "-m", "PyInstaller",
    "--noconfirm",
    "--distpath", $dist,
    "--workpath", $work,
    $spec
)

Write-Host ">> $py $($args -join ' ')"
& $py @args
$buildExit = $LASTEXITCODE
Remove-Item Env:INK_PANEL_CONSOLE_CONSOLE -ErrorAction SilentlyContinue
if ($buildExit -ne 0) {
    throw "PyInstaller failed with exit $buildExit"
}

$outDir = Join-Path $dist "ink_panel_console"
$exe = Join-Path $outDir "ink_panel_console.exe"
if (-not (Test-Path $exe)) {
    throw "Missing output: $exe"
}

$ver = & $py -c "import sys; sys.path.insert(0, r'$Root'); from ink_panel_console import __version__; print(__version__)"
$docs = Join-Path $outDir "docs"
New-Item -ItemType Directory -Force -Path $docs | Out-Null
$readmeSrc = Join-Path $Root "..\..\projects\ink_panel\README.md"
if (Test-Path $readmeSrc) {
    Copy-Item -Force $readmeSrc (Join-Path $docs "ink_panel_README.md")
}
$productSrc = Join-Path $Root "..\..\tmp\ink_panel\PRODUCT.md"
if (Test-Path $productSrc) {
    Copy-Item -Force $productSrc (Join-Path $docs "PRODUCT.md")
}

$stamp = Join-Path $outDir "VERSION.txt"
@(
    "ink_panel_console $ver"
    "built=$(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')"
    "python=$(& $py -c 'import sys; print(sys.version.split()[0])')"
    "pairs_with_firmware=0.4.0"
) | Set-Content -Encoding utf8 $stamp

Write-Host ""
Write-Host "Release OK:"
Write-Host "  $exe"
Write-Host "Copy the whole folder to ship:"
Write-Host "  $outDir"
Write-Host ""
Write-Host "Requires: Windows 10/11 + Bluetooth"
if ($Console) {
    Write-Host "Built WITH console window (-Console)."
}
