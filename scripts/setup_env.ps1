# Semi-automatic NCS setup for this repo (Windows).
# Installs / guides nRF Util sdk-manager so SDK lands under D:\ncs\<version>.
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File .\scripts\setup_env.ps1
#   powershell -ExecutionPolicy Bypass -File .\scripts\setup_env.ps1 -NcsVersion v3.2.0

[CmdletBinding()]
param(
    [string]$NcsVersion = "v3.2.0",
    [string]$InstallRoot = "D:\ncs",
    # Mainland China: use cn mirror (much faster). Use "global" only if cn fails.
    [string]$Region = "cn",
    [switch]$SkipInstall
)

$ErrorActionPreference = "Stop"

function Update-SessionPathFromRegistry {
    # Cursor/VS Code terminals keep a stale PATH; child `powershell -File` inherits it.
    $machine = [System.Environment]::GetEnvironmentVariable("Path", "Machine")
    $user = [System.Environment]::GetEnvironmentVariable("Path", "User")
    if ($machine -or $user) {
        $env:Path = @($machine, $user) -join ";"
    }
}

function Resolve-Nrfutil {
    Update-SessionPathFromRegistry
    $cmd = Get-Command nrfutil -ErrorAction SilentlyContinue
    if ($null -ne $cmd) {
        return $cmd.Source
    }
    $candidates = @(
        (Join-Path $env:LOCALAPPDATA "Microsoft\WinGet\Links\nrfutil.exe"),
        (Join-Path $env:LOCALAPPDATA "Programs\nrfutil\nrfutil.exe"),
        (Join-Path $env:USERPROFILE ".nrfutil\bin\nrfutil.exe")
    )
    foreach ($c in $candidates) {
        if (Test-Path $c) { return $c }
    }
    return $null
}

$RepoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
if ($NcsVersion -notmatch '^v') {
    $NcsVersion = "v" + $NcsVersion
}
# Prefer D:\ncs\v3.2.0 style (keep leading v in folder name)
$NcsRoot = Join-Path $InstallRoot $NcsVersion

Write-Host "=== nRF54L15 setup_env ==="
Write-Host "Repo        : $RepoRoot"
Write-Host "NCS_VERSION : $NcsVersion"
Write-Host "NCS_ROOT    : $NcsRoot"
Write-Host ""

$nrfutilExe = Resolve-Nrfutil
if ($null -eq $nrfutilExe) {
    Write-Host @"
nrfutil not found.

1) Install: winget install --id NordicSemiconductor.nrfutil -e
   or https://www.nordicsemi.com/Products/Development-tools/nRF-Util
2) Re-run this script (PATH refresh is handled automatically now).
3) Then: nrfutil install sdk-manager
"@
    exit 1
}

Write-Host "nrfutil: $nrfutilExe"

Write-Host "Ensuring sdk-manager plugin..."
& $nrfutilExe install sdk-manager
if ($LASTEXITCODE -ne 0) {
    Write-Warning "nrfutil install sdk-manager returned $LASTEXITCODE (may already be installed)"
}

if (-not $SkipInstall) {
    Write-Host ""
    Write-Host "Installing NCS $NcsVersion (large download, ~10-20 GB)."
    Write-Host "Install root : $InstallRoot  (toolchain + SDK parent)"
    Write-Host "SDK expected : $NcsRoot"
    Write-Host "Download     : region=$Region"
    Write-Host ""

    if (-not (Test-Path $InstallRoot)) {
        New-Item -ItemType Directory -Path $InstallRoot -Force | Out-Null
    }

    # Persist default so later installs also land on D: (not C:\ncs).
    Write-Host ">> nrfutil sdk-manager config install-dir set $InstallRoot"
    & $nrfutilExe sdk-manager config install-dir set $InstallRoot
    if ($LASTEXITCODE -ne 0) {
        Write-Warning "config install-dir set failed (exit $LASTEXITCODE); will still pass --install-dir"
    }

    # NOTE: --sdk-path is NOT a valid nrfutil flag; use --install-dir.
    $installCmd = @(
        "sdk-manager", "install", $NcsVersion,
        "--install-dir", $InstallRoot,
        "--region", $Region
    )
    Write-Host ">> nrfutil $($installCmd -join ' ')"
    & $nrfutilExe @installCmd
    $installed = ($LASTEXITCODE -eq 0)

    if (-not $installed) {
        Write-Host @"

Automatic install did not complete. Manual path:

  nrfutil install sdk-manager
  nrfutil sdk-manager config install-dir set $InstallRoot
  nrfutil sdk-manager install $NcsVersion --install-dir $InstallRoot --region $Region

Ensure the SDK folder exists at:
  $NcsRoot
(or update NCS_ROOT in nrf.env after install)

Then re-run:
  powershell -ExecutionPolicy Bypass -File .\scripts\setup_env.ps1 -SkipInstall
"@
        exit 1
    }
}

# Resolve actual path if installer used D:\ncs\v3.2.0 without patch mismatch
if (-not (Test-Path $NcsRoot)) {
    $candidate = Get-ChildItem -Path $InstallRoot -Directory -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -like ($NcsVersion + "*") -or $_.Name -eq $NcsVersion.TrimStart("v") } |
        Select-Object -First 1
    if ($candidate) {
        $NcsRoot = $candidate.FullName
        Write-Host "Using detected SDK path: $NcsRoot"
    }
}

$zephyr = Join-Path $NcsRoot "zephyr"
if (-not (Test-Path $zephyr)) {
    Write-Warning "zephyr/ not under $NcsRoot — fix NCS_ROOT before building."
}

$envExample = Join-Path $RepoRoot "nrf.env.example"
$envFile = Join-Path $RepoRoot "nrf.env"
if (-not (Test-Path $envFile)) {
    Copy-Item $envExample $envFile
    Write-Host "Created $envFile"
}

function Set-EnvKey {
    param([string]$File, [string]$Key, [string]$Value)
    $lines = @(Get-Content -Path $File)
    $found = $false
    $out = foreach ($line in $lines) {
        if ($line -match ("^\s*" + [regex]::Escape($Key) + "\s*=")) {
            $found = $true
            "$Key=$Value"
        } else {
            $line
        }
    }
    if (-not $found) {
        $out += "$Key=$Value"
    }
    Set-Content -Path $File -Value $out -Encoding UTF8
}

Set-EnvKey -File $envFile -Key "NCS_VERSION" -Value $NcsVersion
Set-EnvKey -File $envFile -Key "NCS_ROOT" -Value $NcsRoot
Set-EnvKey -File $envFile -Key "BOARD" -Value "nrf54l15dk/nrf54l15/cpuapp"
Set-EnvKey -File $envFile -Key "PROJECT" -Value "demo"
Set-EnvKey -File $envFile -Key "FLASH_RUNNER" -Value "jlink"
Set-EnvKey -File $envFile -Key "USE_NRFUTIL_LAUNCH" -Value "1"

Write-Host ""
Write-Host "Updated $envFile"
Write-Host "Next:"
Write-Host "  1) .\scripts\verify_env.ps1"
Write-Host "  2) .\nrf.cmd build"
Write-Host "  3) .\nrf.cmd flash"
Write-Host "  (logs: use your own serial tool on the board UART)"
