# nRF Connect SDK wrapper for this repository (Windows).
#
# Examples:
#   .\nrf.cmd build
#   .\nrf.cmd -Project factory build-all
#   .\nrf.cmd -Project factory release 0.0.1
#   .\nrf.cmd -Project factory flash
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

function Test-WestBuildTree {
    return (Test-Path (Join-Path $BuildDir "build.ninja"))
}

function Test-AppReleaseConfig {
    $cfg = Join-Path $BuildDir "$Project\zephyr\.config"
    if (-not (Test-Path $cfg)) {
        return $false
    }
    return [bool](Select-String -Path $cfg -Pattern '^CONFIG_APP_BUILD_RELEASE=y$' -Quiet)
}

function Test-SysbuildHasDomain {
    # Sysbuild domains.yaml lists child images (factory/demo + optional mcuboot).
    $domains = Join-Path $BuildDir "domains.yaml"
    if (-not (Test-Path $domains)) {
        return $false
    }
    return [bool](Select-String -Path $domains -Pattern ("^\s*-\s*name:\s*{0}\s*$" -f [regex]::Escape($Project)) -Quiet)
}

function Get-SysbuildWatchFiles {
    # Inputs that require top-level sysbuild/MCUBoot reconfigure (not app-only).
    $bootDir = Join-Path $RepoRoot "bootloader"
    $files = [System.Collections.Generic.List[string]]::new()
    foreach ($p in @(
            (Join-Path $AppDir "sysbuild.conf"),
            (Join-Path $AppDir "sysbuild.cmake"),
            (Join-Path $AppDir "pm_static.yml"),
            (Join-Path $AppDir "Kconfig.sysbuild"),
            (Join-Path $bootDir "sysbuild_include.cmake"),
            (Join-Path $bootDir "prj.conf"),
            (Join-Path $bootDir "pm_static.yml"),
            (Join-Path $bootDir "boards\nrf54l15dk_nrf54l15_cpuapp.conf"),
            (Join-Path $bootDir "boards\nrf54l15dk_nrf54l15_cpuapp.overlay")
        )) {
        if (Test-Path $p) { [void]$files.Add($p) }
    }
    $hooksDir = Join-Path $bootDir "hooks"
    if (Test-Path $hooksDir) {
        Get-ChildItem -Path $hooksDir -File -Recurse -Include *.c, *.h, *.txt, *.yml, *.cmake |
            ForEach-Object { [void]$files.Add($_.FullName) }
    }
    return $files
}

function Test-NeedFullSysbuild {
    # True when MCUBoot/sysbuild inputs are newer than the top build.ninja.
    # App-only edits (src/*.c, prj.conf) must NOT trip this — those stay on --domain.
    if (-not (Test-WestBuildTree)) {
        return $true
    }
    if (-not (Test-SysbuildHasDomain)) {
        return $true
    }
    $stamp = (Get-Item (Join-Path $BuildDir "build.ninja")).LastWriteTime
    foreach ($f in Get-SysbuildWatchFiles) {
        if ((Get-Item $f).LastWriteTime -gt $stamp) {
            Write-Host "Full sysbuild: newer than build.ninja -> $f"
            return $true
        }
    }
    return $false
}

function Invoke-WestBuildConfigured {
    # First-time west build: board + source + cmake opts (creates sysbuild tree).
    param([string[]]$ExtraCmake = @())

    $westArgs = [System.Collections.Generic.List[string]]::new()
    [void]$westArgs.Add("build")
    [void]$westArgs.Add("-b"); [void]$westArgs.Add($Board)
    [void]$westArgs.Add("-s"); [void]$westArgs.Add($AppDir)
    [void]$westArgs.Add("-d"); [void]$westArgs.Add($BuildDir)
    [void]$westArgs.Add("--")
    [void]$westArgs.Add("-DCONFIG_APP_BUILD_RELEASE=n")
    $cmakeExtra = @(Get-ProjectCmakeExtraArgs | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
    foreach ($a in $cmakeExtra) { [void]$westArgs.Add($a) }
    foreach ($a in $ExtraCmake) { [void]$westArgs.Add($a) }
    Invoke-West -WestArgs $westArgs.ToArray()
}

function Invoke-BuildAll {
    # Full sysbuild: MCUBoot + app + merged.hex. Use after bootloader/sysbuild/pm changes.
    if (-not (Test-Path $NcsRoot)) {
        Write-Error "NCS_ROOT not found: $NcsRoot  (run scripts\setup_env.ps1)"
    }

    if (-not (Test-WestBuildTree)) {
        Write-Host "Configuring sysbuild tree at $BuildDir"
        Invoke-WestBuildConfigured
        return
    }

    $westArgs = [System.Collections.Generic.List[string]]::new()
    [void]$westArgs.Add("build")
    [void]$westArgs.Add("-d")
    [void]$westArgs.Add($BuildDir)
    if (Test-AppReleaseConfig) {
        Write-Host "Note: last tree was release; flipping CONFIG_APP_BUILD_RELEASE=n"
        [void]$westArgs.Add("--")
        [void]$westArgs.Add("-DCONFIG_APP_BUILD_RELEASE=n")
    }
    Write-Host "Full sysbuild (mcuboot + $Project + merged.hex)"
    Invoke-West -WestArgs $westArgs.ToArray()
}

function Invoke-Build {
    # Fast default: build only the app domain. Avoids top-level Re-running CMake which
    # regenerates DT headers and force-rebuilds MCUBoot (~60s) even when Kconfig is unchanged.
    if (-not (Test-Path $NcsRoot)) {
        Write-Error "NCS_ROOT not found: $NcsRoot  (run scripts\setup_env.ps1)"
    }

    if (-not (Test-WestBuildTree)) {
        Write-Host "Configuring sysbuild tree at $BuildDir"
        Invoke-WestBuildConfigured
        return
    }

    if (Test-AppReleaseConfig) {
        # CMake -D forces reconfigure; must run at sysbuild top level.
        Write-Host "Note: last tree was release; flipping CONFIG_APP_BUILD_RELEASE=n (full sysbuild)"
        Invoke-BuildAll
        return
    }

    if (Test-NeedFullSysbuild) {
        Invoke-BuildAll
        return
    }

    if (-not (Test-SysbuildHasDomain)) {
        Write-Host "Incremental build (no domains.yaml domain '$Project'; full tree)"
        Invoke-BuildAll
        return
    }

    Write-Host "Incremental app build (--domain $Project); MCUBoot unchanged. Use build-all after bootloader/sysbuild/pm edits."
    Invoke-West -WestArgs @("build", "-d", $BuildDir, "--domain", $Project)
}

function Invoke-MergeHex {
    # Refresh merged.hex from already-built child images (no app/mcuboot compile).
    if (-not (Test-WestBuildTree)) {
        Write-Error "Build dir missing: $BuildDir  (run build first)"
    }
    Write-Host "Updating merged.hex"
    Invoke-West -WestArgs @("build", "-d", $BuildDir, "-t", "merged_hex")
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

function Resolve-JLink {
    $jlink = Get-Command JLinkExe -ErrorAction SilentlyContinue
    if ($null -eq $jlink) {
        $jlink = Get-Command JLink.exe -ErrorAction SilentlyContinue
    }
    return $jlink
}

function Get-Crc32Ieee {
    param([byte[]]$Data)

    # PowerShell treats 0xFFFFFFFF as Int32 -1; never cast that literal to [uint32].
    function ConvertTo-UInt32Unchecked([object]$Value) {
        $bytes = [BitConverter]::GetBytes([int64]$Value)
        return [BitConverter]::ToUInt32($bytes, 0)
    }

    $table = New-Object uint32[] 256
    for ($i = 0; $i -lt 256; $i++) {
        $c = [uint32]$i
        for ($j = 0; $j -lt 8; $j++) {
            if (($c -band 1) -ne 0) {
                $c = ConvertTo-UInt32Unchecked (0xEDB88320 -bxor ($c -shr 1))
            } else {
                $c = [uint32]($c -shr 1)
            }
        }
        $table[$i] = $c
    }

    $crc = [uint32]::MaxValue
    foreach ($b in $Data) {
        $idx = [byte](($crc -bxor $b) -band 0xFF)
        $crc = ConvertTo-UInt32Unchecked ($table[$idx] -bxor ($crc -shr 8))
    }
    return (ConvertTo-UInt32Unchecked ($crc -bxor [uint32]::MaxValue))
}

function Invoke-BootMode {
    # Write boot_share @ 0x164000 so MCUBoot factory bypass can be set from the host.
    # Usage: nrf.cmd boot-mode factory | boot-mode app
    param([Parameter(Mandatory = $true)][string]$ModeRaw)

    $modeName = $ModeRaw.Trim().ToLowerInvariant()
    $modeByte = switch ($modeName) {
        "factory" { [byte]1 }
        "app" { [byte]0 }
        default {
            Write-Error "boot-mode expects 'factory' or 'app' (got '$ModeRaw')"
        }
    }

    $jlink = Resolve-JLink
    if ($null -eq $jlink) {
        Write-Error "J-Link not in PATH (need JLink.exe / JLinkExe for boot-mode)"
    }

    # struct boot_share_record (16 B LE): magic, version, boot_mode, reserved[6], crc32
    $payload = New-Object byte[] 12
    $magic = [BitConverter]::GetBytes([uint32]0x54594253)
    [Array]::Copy($magic, 0, $payload, 0, 4)
    $payload[4] = 1
    $payload[5] = $modeByte
    $crc = Get-Crc32Ieee -Data $payload
    $rec = New-Object byte[] 16
    [Array]::Copy($payload, 0, $rec, 0, 12)
    $crcBytes = [BitConverter]::GetBytes([uint32]$crc)
    [Array]::Copy($crcBytes, 0, $rec, 12, 4)

    $binPath = Join-Path $env:TEMP ("nrf54_boot_share_{0}.bin" -f $modeName)
    [System.IO.File]::WriteAllBytes($binPath, $rec)

    $addr = "0x164000"
    $script = Join-Path $env:TEMP ("nrf54_boot_mode_{0}.jlink" -f $modeName)
    $loadLine = 'LoadFile "{0}",{1}' -f $binPath, $addr
    @(
        "r"
        "h"
        "EraseRange 0x164000, 0x164FFF"
        $loadLine
        "r"
        "g"
        "q"
    ) | Set-Content -Path $script -Encoding ASCII

    Write-Host "boot-mode: write boot_share=$modeName (mode=$modeByte) @ $addr"
    & $jlink.Source -device nRF54L15_M33 -if SWD -speed 4000 -autoconnect 1 -CommanderScript $script
    if ($LASTEXITCODE -ne 0) {
        Write-Error "boot-mode failed (J-Link exit $LASTEXITCODE)"
    }
    Write-Host "boot-mode done; reset if CPU already running"
}

function Invoke-Flash {
    # Factory image always lives in slot1; never program merged.hex over slot0.
    if ($Project -eq "factory") {
        Invoke-FlashSlot1
        return
    }

    if (-not (Test-Path $BuildDir)) {
        Write-Error "Build dir missing: $BuildDir  (run build first)"
    }
    # Domain-only `build` updates signed app but not merged.hex; refresh before SWD.
    if (Test-SysbuildHasDomain) {
        Invoke-Build
        if (Test-Path (Join-Path $BuildDir "mcuboot")) {
            Invoke-MergeHex
        }
    }

    # Sysbuild flash_order puts FLPR (remote / nRF54L15_RV32) first. SWD on this
    # board talks to the M33; flashing RV32 fails. Flash the app domain only
    # (merged.hex = MCUBoot + cpuapp). Skip --domain remote.
    $westArgs = @(
        "flash",
        "-d", $BuildDir,
        "--runner", $FlashRunner,
        "--domain", $Project
    )
    Write-Host "Flash domain: $Project (cpuapp / M33). FLPR remote not flashed via RV32."
    Invoke-West -WestArgs $westArgs
}

# Program factory Direct-XIP signed.bin into mcuboot_secondary @ 0xB6000 (slot0/boot untouched).
# Prefer mcuboot_secondary_app (linked for slot1); fall back to primary-linked signed.bin.
function Invoke-FlashSlot1 {
    if (-not (Test-Path $BuildDir)) {
        Write-Error "Build dir missing: $BuildDir  (run build first)"
    }

    if ($Project -eq "factory") {
        # Secondary XIP variant is a separate sysbuild domain.
        if (-not (Test-WestBuildTree)) {
            Write-Host "Configuring sysbuild tree at $BuildDir"
            Invoke-WestBuildConfigured
        } else {
            Write-Host "Build factory secondary XIP (--domain mcuboot_secondary_app)"
            Invoke-West -WestArgs @("build", "-d", $BuildDir, "--domain", "mcuboot_secondary_app")
        }
    } else {
        Invoke-Build
    }

    $candidates = @(
        (Join-Path $BuildDir "mcuboot_secondary_app\zephyr\zephyr.signed.bin"),
        (Join-Path $BuildDir "$Project\zephyr\zephyr.signed.bin"),
        (Join-Path $BuildDir "zephyr\zephyr.signed.bin")
    )
    $signedBin = $null
    foreach ($c in $candidates) {
        if (Test-Path $c) {
            $signedBin = $c
            break
        }
    }
    if ($null -eq $signedBin) {
        Write-Error "Missing zephyr.signed.bin under $BuildDir (factory needs mcuboot_secondary_app; run build-all)"
    }
    Write-Host "Using signed image: $signedBin"

    $jlink = Resolve-JLink
    if ($null -eq $jlink) {
        Write-Error "J-Link not in PATH (need JLink.exe / JLinkExe for flash-slot1)"
    }

    $slot1Addr = "0xB6000"
    $script = Join-Path $env:TEMP ("nrf54_flash_slot1_{0}.jlink" -f $Project)
    $loadLine = 'LoadFile "{0}",{1}' -f $signedBin, $slot1Addr
    @(
        "r"
        "h"
        $loadLine
        "r"
        "g"
        "q"
    ) | Set-Content -Path $script -Encoding ASCII

    Write-Host "factory flash → slot1: $signedBin -> mcuboot_secondary @ $slot1Addr"
    Write-Host "J-Link script: $script"
    & $jlink.Source -device nRF54L15_M33 -if SWD -speed 4000 -autoconnect 1 -CommanderScript $script
    if ($LASTEXITCODE -ne 0) {
        Write-Error "flash-slot1 failed (J-Link exit $LASTEXITCODE)"
    }
    Write-Host "factory flash done (boot + slot0 not rewritten)"
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

    $jlink = Resolve-JLink
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
        "build-all" { Invoke-BuildAll }
        "flash" { Invoke-Flash }
        "flash-slot1" { Invoke-FlashSlot1 }
        "boot-mode" {
            if ($idx + 1 -ge $cmds.Count) {
                Write-Error "boot-mode requires factory|app. Example: .\nrf.cmd boot-mode factory"
            }
            $modeArg = $cmds[$idx + 1]
            $idx += 1
            Invoke-BootMode -ModeRaw $modeArg
        }
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
  build              fast: app domain only (--domain). Skips MCUBoot rebuild.
  build-all          full sysbuild: MCUBoot + app + merged.hex (bootloader/sysbuild/pm).
  release <x.y.z>    release build into same build/ (log.c no-op) + archive to firmware/<x.y.z>/
  flash              APP: merged.hex → slot0; factory: Direct-XIP secondary → slot1
  flash-slot1        same as factory flash (compat alias)
  boot-mode <m>      J-Link write boot_share: factory | app (MCUBoot bypass)
  fullclean          delete projects/<name>/build/
  detect             print env / tool status
  help               this text

Examples:
  .\nrf.cmd build
  .\nrf.cmd -Project demo fullclean
  .\nrf.cmd -Project demo build-all
  .\nrf.cmd -Project demo flash
  .\nrf.cmd -Project factory build-all
  .\nrf.cmd -Project factory flash
  .\nrf.cmd boot-mode factory
"@
        }
        default {
            Write-Error "Unknown command '$cmd'. Try: build | build-all | release | flash | flash-slot1 | boot-mode | fullclean | detect | help"
        }
    }
    $idx += 1
}
