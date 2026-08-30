# Verify nRF54L15 repo environment (does not build).
# Usage: powershell -ExecutionPolicy Bypass -File .\scripts\verify_env.ps1

$ErrorActionPreference = "Stop"

function Update-SessionPathFromRegistry {
    $machine = [System.Environment]::GetEnvironmentVariable("Path", "Machine")
    $user = [System.Environment]::GetEnvironmentVariable("Path", "User")
    if ($machine -or $user) {
        $env:Path = @($machine, $user) -join ";"
    }
}

$RepoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$EnvFile = Join-Path $RepoRoot "nrf.env"

Update-SessionPathFromRegistry

$ok = $true
function Check([string]$Label, [bool]$Pass, [string]$Hint = "") {
    $mark = if ($Pass) { "OK  " } else { "FAIL" }
    if (-not $Pass) { $script:ok = $false }
    $msg = "[$mark] $Label"
    if (-not $Pass -and $Hint) { $msg += " — $Hint" }
    Write-Host $msg
}

Check "nrf.env exists" (Test-Path $EnvFile) "copy nrf.env.example nrf.env"

$NcsRoot = $null
$NcsVersion = $null
if (Test-Path $EnvFile) {
    Get-Content $EnvFile | ForEach-Object {
        $line = $_.Trim()
        if ($line -eq "" -or $line.StartsWith("#")) { return }
        $eq = $line.IndexOf("=")
        if ($eq -lt 1) { return }
        $key = $line.Substring(0, $eq).Trim()
        $val = $line.Substring($eq + 1).Trim().Trim('"')
        if ($key -eq "NCS_ROOT") { $script:NcsRoot = $val }
        if ($key -eq "NCS_VERSION") { $script:NcsVersion = $val }
    }
}

Check "NCS_ROOT set" (-not [string]::IsNullOrWhiteSpace($NcsRoot)) "set NCS_ROOT in nrf.env"
if ($NcsRoot) {
    Check "NCS_ROOT path" (Test-Path $NcsRoot) "run scripts\setup_env.ps1"
    Check "zephyr/ present" (Test-Path (Join-Path $NcsRoot "zephyr")) "incomplete SDK under NCS_ROOT"
    Check "nrf/ present" (Test-Path (Join-Path $NcsRoot "nrf")) "incomplete SDK under NCS_ROOT"
}

$demo = Join-Path $RepoRoot "projects\demo"
Check "projects/demo" (Test-Path (Join-Path $demo "src\main.c")) "skeleton missing"

$nrfutil = Get-Command nrfutil -ErrorAction SilentlyContinue
Check "nrfutil in PATH" ($null -ne $nrfutil) "install nRF Util + sdk-manager"

$jlink = Get-Command JLinkExe -ErrorAction SilentlyContinue
if ($null -eq $jlink) { $jlink = Get-Command JLink.exe -ErrorAction SilentlyContinue }
Check "J-Link CLI in PATH (optional)" ($null -ne $jlink) "install SEGGER J-Link; west may still locate it"

Write-Host ""
Write-Host "NCS_VERSION: $NcsVersion"
Write-Host "NCS_ROOT   : $NcsRoot"
if ($ok) {
    Write-Host "verify_env: PASS"
    exit 0
}
Write-Host "verify_env: FAIL"
exit 1
