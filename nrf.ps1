# nRF Connect SDK wrapper for this repository (Windows).
#
# Examples:
#   .\nrf.cmd build
#   .\nrf.cmd -Project factory release 0.0.1
#   .\nrf.cmd -Project demo flash
#   .\nrf.cmd detect
#   .\nrf.cmd fullclean
#
# Requires: nrf.env (copy from nrf.env.example), NCS under NCS_ROOT, J-Link (SWD).

$ErrorActionPreference = "Stop"

function Update-SessionPathFromRegistry {
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

$RepoRoot = [System.IO.Path]::GetFullPath($PSScriptRoot)
$EnvFile = Join-Path $RepoRoot "nrf.env"

$RawArgs = @($args)
if ($RawArgs.Count -ge 1 -and $RawArgs[0] -eq "--") {
    $RawArgs = $RawArgs[1..($RawArgs.Count - 1)]
}

function Get-ProjectNames {
    $projectsDir = Join-Path $RepoRoot "projects"
    if (-not (Test-Path $projectsDir)) {
        return @()
    }
    return @(Get-ChildItem -Path $projectsDir -Directory | ForEach-Object { $_.Name })
}

function Import-NrfEnv {
    param([string]$Path)
    if (-not (Test-Path $Path)) {
        Write-Error "Missing nrf.env. Run: copy nrf.env.example nrf.env  (then edit NCS_ROOT / BOARD)"
    }
    Get-Content -Path $Path | ForEach-Object {
        $line = $_.Trim()
        if ($line -eq "" -or $line.StartsWith("#")) { return }
        $eq = $line.IndexOf("=")
        if ($eq -lt 1) { return }
        $key = $line.Substring(0, $eq).Trim()
        $val = $line.Substring($eq + 1).Trim().Trim('"')
        Set-Item -Path "Env:$key" -Value $val
    }
}

function Get-DefaultProject {
    if ($env:PROJECT -and $env:PROJECT.Trim() -ne "") {
        return $env:PROJECT.Trim()
    }
    return "demo"
}

Import-NrfEnv -Path $EnvFile

$Project = Get-DefaultProject
$Passthrough = @()
$i = 0
while ($i -lt $RawArgs.Count) {
    $arg = $RawArgs[$i]
    if ($arg -ieq "-Project") {
        if ($i + 1 -ge $RawArgs.Count) {
            Write-Error "-Project requires a name. Available: $((Get-ProjectNames) -join ', ')"
        }
        $Project = $RawArgs[$i + 1]
        $i += 2
        continue
    }
    $Passthrough += $arg
    $i += 1
}

$validProjects = @(Get-ProjectNames)
if ($validProjects -notcontains $Project) {
    Write-Error "Unknown -Project '$Project'. Available: $($validProjects -join ', ')"
}

$AppDir = Join-Path $RepoRoot "projects\$Project"
$BuildDir = Join-Path $AppDir "build"
$Board = if ($env:BOARD) { $env:BOARD } else { "nrf54l15dk/nrf54l15/cpuapp" }
$NcsVersion = if ($env:NCS_VERSION) { $env:NCS_VERSION } else { "v3.2.0" }
$NcsRoot = if ($env:NCS_ROOT) { $env:NCS_ROOT } else { "D:\ncs\v3.2.0" }
$FlashRunner = if ($env:FLASH_RUNNER) { $env:FLASH_RUNNER } else { "jlink" }
$UseNrfutil = if ($env:USE_NRFUTIL_LAUNCH) { $env:USE_NRFUTIL_LAUNCH } else { "1" }
$ChipName = if ($env:CHIP_NAME) { $env:CHIP_NAME.Trim() } else { "nrf54l15" }

if ($Passthrough.Count -eq 0) {
    Write-Error "Missing command. Example: .\nrf.cmd build | flash | detect | fullclean"
}

function Invoke-West {
    param([Parameter(Mandatory = $true)][string[]]$WestArgs)

    if ($UseNrfutil -eq "1") {
        $nrfutilExe = Resolve-Nrfutil
        if ($null -eq $nrfutilExe) {
            Write-Error "nrfutil not found. Install: winget install --id NordicSemiconductor.nrfutil -e"
        }
        # Must invoke `west ...`, not bare build/flash (nrfutil spawns COMMAND).
        $launchArgs = @(
            "sdk-manager", "toolchain", "launch",
            "--ncs-version", $NcsVersion,
            "--install-dir", (Split-Path -Parent $NcsRoot),
            "--chdir", $NcsRoot,
            "--",
            "west"
        ) + $WestArgs
        Write-Host ">> nrfutil $($launchArgs -join ' ')"
        & $nrfutilExe @launchArgs
        if ($LASTEXITCODE -ne 0) {
            throw "nrfutil/west failed with exit code $LASTEXITCODE"
        }
        return
    }

    if (-not $env:ZEPHYR_BASE) {
        $env:ZEPHYR_BASE = Join-Path $NcsRoot "zephyr"
    }
    $west = Get-Command west -ErrorAction SilentlyContinue
    if ($null -eq $west) {
        Write-Error "west not in PATH. Set USE_NRFUTIL_LAUNCH=1 or open an NCS toolchain shell."
    }
    Write-Host ">> west $($WestArgs -join ' ')"
    & west @WestArgs
    if ($LASTEXITCODE -ne 0) {
        throw "west failed with exit code $LASTEXITCODE"
    }
}

function Get-ProjectCmakeExtraArgs {
    # Optional projects/<name>/SNIPPET — one snippet name per line (e.g. nordic-flpr).
    $argsList = @()
    $snippetFile = Join-Path $AppDir "SNIPPET"
    if (Test-Path $snippetFile) {
        $names = @(
            Get-Content -Path $snippetFile |
                ForEach-Object { $_.Trim() } |
                Where-Object { $_ -ne "" -and -not $_.StartsWith("#") }
        )
        if ($names.Count -gt 0) {
            $joined = $names -join ";"
            $argsList += "-D${Project}_SNIPPET=$joined"
        }
    }
    return $argsList
}

function Invoke-Build {
    if (-not (Test-Path $NcsRoot)) {
        Write-Error "NCS_ROOT not found: $NcsRoot  (run scripts\setup_env.ps1)"
    }
    $westArgs = [System.Collections.Generic.List[string]]::new()
    $westArgs.Add("build") | Out-Null
    $westArgs.Add("-b") | Out-Null
    $westArgs.Add($Board) | Out-Null
    $westArgs.Add("-s") | Out-Null
    $westArgs.Add($AppDir) | Out-Null
    $westArgs.Add("-d") | Out-Null
    $westArgs.Add($BuildDir) | Out-Null
    $westArgs.Add("--") | Out-Null
    # Override stale release cmake cache (release sets CONFIG_APP_BUILD_RELEASE=y → LOG_* no-op).
    $westArgs.Add("-DCONFIG_APP_BUILD_RELEASE=n") | Out-Null
    $cmakeExtra = @(Get-ProjectCmakeExtraArgs | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
    foreach ($a in $cmakeExtra) { $westArgs.Add($a) | Out-Null }
    Invoke-West -WestArgs $westArgs.ToArray()
}

function Normalize-ReleaseVersion {
    param([string]$Raw)
    $v = $Raw.Trim()
    if ($v.StartsWith("V") -or $v.StartsWith("v")) {
        $v = $v.Substring(1)
    }
    if ($v -notmatch '^\d+\.\d+\.\d+$') {
        Write-Error "Invalid version '$Raw'. Use semver like 0.0.1 or V0.0.1"
    }
    return $v
}

function Invoke-Release {
    param([Parameter(Mandatory = $true)][string]$VersionRaw)

    if (-not (Test-Path $NcsRoot)) {
        Write-Error "NCS_ROOT not found: $NcsRoot  (run scripts\setup_env.ps1)"
    }

    $ver = Normalize-ReleaseVersion -Raw $VersionRaw
    $day = Get-Date -Format "yyyyMMdd"
    $sign = "unsigned"
    $releaseConf = Join-Path $AppDir "prj_release.conf"
    if (-not (Test-Path $releaseConf)) {
        Write-Error "Missing release conf: $releaseConf"
    }

    Write-Host "Release: project=$Project version=$ver sign=$sign (log.c output empty)"

    $verConf = Join-Path $AppDir "prj_version.conf"
    @(
        "# Generated by nrf.cmd release — do not edit; gitignored.",
        "CONFIG_APP_VERSION_STRING=`"$ver`""
    ) -join "`n" | Set-Content -Path $verConf -Encoding ascii

    $westArgs = [System.Collections.Generic.List[string]]::new()
    [void]$westArgs.Add("build")
    [void]$westArgs.Add("-b"); [void]$westArgs.Add($Board)
    [void]$westArgs.Add("-s"); [void]$westArgs.Add($AppDir)
    [void]$westArgs.Add("-d"); [void]$westArgs.Add($BuildDir)
    [void]$westArgs.Add("--")
    [void]$westArgs.Add("-DEXTRA_CONF_FILE=prj_release.conf;prj_version.conf")
    $cmakeExtra = @(Get-ProjectCmakeExtraArgs | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
    foreach ($a in $cmakeExtra) { [void]$westArgs.Add($a) }
    Invoke-West -WestArgs $westArgs.ToArray()

    $zephyrDir = Join-Path $BuildDir "$Project\zephyr"
    $elfSrc = Join-Path $zephyrDir "zephyr.elf"
    $binSrc = Join-Path $zephyrDir "zephyr.bin"
    $hexSrc = Join-Path $BuildDir "merged.hex"
    if (-not (Test-Path $hexSrc)) {
        $hexSrc = Join-Path $zephyrDir "zephyr.hex"
    }

    foreach ($p in @($elfSrc, $binSrc, $hexSrc)) {
        if (-not (Test-Path $p)) {
            Write-Error "Release artifact missing: $p"
        }
    }

    $outDir = Join-Path $RepoRoot "firmware\$ver"
    New-Item -ItemType Directory -Force -Path $outDir | Out-Null
    $stem = "{0}_{1}_{2}_{3}_{4}" -f $ChipName, $Project, $ver, $day, $sign

    $copied = @()
    foreach ($pair in @(
            @{ Src = $elfSrc; Ext = "elf" },
            @{ Src = $hexSrc; Ext = "hex" },
            @{ Src = $binSrc; Ext = "bin" }
        )) {
        $dst = Join-Path $outDir ($stem + "." + $pair.Ext)
        Copy-Item -Force $pair.Src $dst
        $copied += $dst
        Write-Host "Archived $($pair.Ext): $dst"
    }

    Write-Host "Release done ($($copied.Count) files) -> $outDir"
}

function Invoke-Flash {
    if (-not (Test-Path $BuildDir)) {
        Write-Error "Build dir missing: $BuildDir  (run build first)"
    }
    $westArgs = @(
        "flash",
        "-d", $BuildDir,
        "--runner", $FlashRunner
    )
    Invoke-West -WestArgs $westArgs
}

function Invoke-FullClean {
    $legacyRelease = Join-Path $AppDir "build-release"
    foreach ($dir in @($BuildDir, $legacyRelease)) {
        if (Test-Path $dir) {
            Remove-Item -Recurse -Force $dir
            Write-Host "Removed $dir"
        } else {
            Write-Host "Nothing to clean: $dir"
        }
    }
}

function Invoke-Detect {
    Write-Host "RepoRoot     : $RepoRoot"
    Write-Host "Project      : $Project ($AppDir)"
    Write-Host "Board        : $Board"
    Write-Host "Chip         : $ChipName"
    Write-Host "NCS_VERSION  : $NcsVersion"
    Write-Host "NCS_ROOT     : $NcsRoot  [$(if (Test-Path $NcsRoot) { 'ok' } else { 'MISSING' })]"
    Write-Host "BuildDir     : $BuildDir  [$(if (Test-Path $BuildDir) { 'exists' } else { 'absent' })]"
    Write-Host "FLASH_RUNNER : $FlashRunner"
    Write-Host "USE_NRFUTIL  : $UseNrfutil"
    Write-Host "Projects     : $($validProjects -join ', ')"

    $nrfutilExe = Resolve-Nrfutil
    Write-Host "nrfutil      : $(if ($nrfutilExe) { $nrfutilExe } else { 'MISSING' })"

    $jlink = Get-Command JLinkExe -ErrorAction SilentlyContinue
    if ($null -eq $jlink) {
        $jlink = Get-Command JLink.exe -ErrorAction SilentlyContinue
    }
    Write-Host "J-Link       : $(if ($jlink) { $jlink.Source } else { 'not in PATH (west flash may still find it)' })"

    $zephyr = Join-Path $NcsRoot "zephyr"
    Write-Host "zephyr/      : $(if (Test-Path $zephyr) { 'ok' } else { 'MISSING under NCS_ROOT' })"
}

$cmds = @($Passthrough)
$idx = 0
while ($idx -lt $cmds.Count) {
    $cmd = $cmds[$idx].ToLowerInvariant()
    switch ($cmd) {
        "build" { Invoke-Build }
        "flash" { Invoke-Flash }
        "fullclean" { Invoke-FullClean }
        "detect" { Invoke-Detect }
        "release" {
            if ($idx + 1 -ge $cmds.Count) {
                Write-Error "release requires a version. Example: .\nrf.cmd -Project factory release 0.0.1"
            }
            $verArg = $cmds[$idx + 1]
            $idx += 1
            Invoke-Release -VersionRaw $verArg
        }
        "help" {
            Write-Host @"
Usage: nrf [-Project <name>] <command> [command...]

Commands:
  build              west build (debug; app log on) -> projects/<name>/build
  release <x.y.z>    release build into same build/ (log.c no-op) + archive to firmware/<x.y.z>/
  flash              west flash --runner jlink (SWD) [build/]
  fullclean          delete projects/<name>/build/
  detect             print env / tool status
  help               this text

Examples:
  .\nrf.cmd build
  .\nrf.cmd -Project factory release 0.0.1
  .\nrf.cmd -Project demo flash
"@
        }
        default {
            Write-Error "Unknown command '$cmd'. Try: build | release | flash | fullclean | detect | help"
        }
    }
    $idx += 1
}
