# Copy projects/demo to projects/<Name> as a new app template.
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File .\scripts\new_project.ps1 -Name myapp
#   .\nrf.cmd -Project myapp build

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string]$Name
)

$ErrorActionPreference = "Stop"

if ($Name -notmatch '^[A-Za-z][A-Za-z0-9_]*$') {
    Write-Error "Invalid -Name '$Name'. Use letters/digits/underscore; must start with a letter."
}

$RepoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$Template = Join-Path $RepoRoot "projects\demo"
$Dest = Join-Path $RepoRoot "projects\$Name"

if (-not (Test-Path $Template)) {
    Write-Error "Template missing: $Template"
}
if (Test-Path $Dest) {
    Write-Error "Already exists: $Dest"
}

New-Item -ItemType Directory -Path $Dest | Out-Null
New-Item -ItemType Directory -Path (Join-Path $Dest "src") | Out-Null
New-Item -ItemType Directory -Path (Join-Path $Dest "lib") | Out-Null

Copy-Item (Join-Path $Template "CMakeLists.txt") (Join-Path $Dest "CMakeLists.txt")
Copy-Item (Join-Path $Template "prj.conf") (Join-Path $Dest "prj.conf")
foreach ($f in @("main.c", "app.c", "app.h", "README.md")) {
    $src = Join-Path $Template "src\$f"
    if (Test-Path $src) {
        Copy-Item $src (Join-Path $Dest "src\$f")
    }
}
$templateLibReadme = Join-Path $Template "lib\README.md"
if (Test-Path $templateLibReadme) {
    Copy-Item $templateLibReadme (Join-Path $Dest "lib\README.md")
}

$cmake = Join-Path $Dest "CMakeLists.txt"
$cmakeText = Get-Content -Raw -Path $cmake
$cmakeText = $cmakeText -replace 'project\(demo\)', "project($Name)"
Set-Content -Path $cmake -Value $cmakeText -NoNewline

foreach ($rel in @("src\main.c", "src\app.c")) {
    $path = Join-Path $Dest $rel
    if (-not (Test-Path $path)) { continue }
    $text = Get-Content -Raw -Path $path
    $text = $text -replace 'projects/demo', "projects/$Name"
    $text = $text -replace '\bdemo\b', $Name
    Set-Content -Path $path -Value $text -NoNewline
}

$readmeLines = @(
    "# $Name",
    "",
    "App scaffolded from ``projects/demo``.",
    "",
    '```powershell',
    ".\nrf.cmd -Project $Name build",
    "# flash deferred for skeleton stage:",
    "# .\nrf.cmd -Project $Name flash",
    '```',
    "",
    "Lifecycle: ``src/`` (``app_startup`` / ``app_run``). Private drivers: ``lib/<module>/``.",
    "Shared: ``common/`` via ``modules/nrf54_common``.",
    "Board default: from ``nrf.env`` (``BOARD=``).",
    ""
)
Set-Content -Path (Join-Path $Dest "README.md") -Value ($readmeLines -join "`n") -Encoding utf8

Write-Host "Created projects\$Name"
Write-Host "Next:"
Write-Host "  .\nrf.cmd -Project $Name build"
Write-Host "Optional: set PROJECT=$Name in nrf.env"
