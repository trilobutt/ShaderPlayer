<#
.SYNOPSIS
    Configures (if needed) and builds ShaderPlayer via the documented MSVC + Qt + CMake
    environment.

.DESCRIPTION
    Wraps the build line documented in CLAUDE.md so the eight-plus done-when: checks in
    tasks/todo.md do not each re-derive it. Locates the Visual Studio installation with
    vswhere (BuildTools installs need `-products *`, or vswhere prints nothing), puts the
    VS Installer directory, Qt's bin, and CMake's bin on PATH *before* calling
    vcvars64.bat (cmd expands %PATH% when it parses the line, so appending after the call
    would discard everything vcvars added), then runs `cmake --build build`.

    Nothing here is a fixed absolute path beyond the VS Installer's own well-known
    location: CMake and Qt are discovered on this machine, so a fresh clone builds without
    editing the script.

.PARAMETER Target
    Build one target instead of all of them. `-Target shaderfx` skips Qt, moc and the
    windeployqt step entirely, which is the difference between a two-second edit loop
    on the headless runner and a full application link.
#>
param([string]$Target)

$ErrorActionPreference = 'Stop'

$repoRoot  = Split-Path $PSScriptRoot -Parent
$buildDir  = Join-Path $repoRoot 'build'

# --- Visual Studio -----------------------------------------------------------------

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) {
    Write-Error "ERROR: vswhere.exe not found at '$vswhere'"
    exit 1
}

$vsInstallPath = & $vswhere -latest -products * -property installationPath
if ([string]::IsNullOrWhiteSpace($vsInstallPath)) {
    Write-Error "ERROR: vswhere found no Visual Studio installation (checked with -products *)"
    exit 1
}

$vcvars = Join-Path $vsInstallPath 'VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path $vcvars)) {
    Write-Error "ERROR: vcvars64.bat not found under '$vsInstallPath'"
    exit 1
}

$vsInstallerDir = Split-Path $vswhere -Parent

# --- CMake and Ninja ---------------------------------------------------------------
#
# A standalone CMake install is not assumed: Visual Studio bundles both CMake and Ninja,
# and vcvars64.bat puts them on PATH itself. Prefer whatever is already on PATH, then the
# VS copies, then a standalone install; only directories that actually exist are added.

$toolDirs = @()
$onPath = Get-Command cmake -ErrorAction SilentlyContinue
if ($onPath) { $toolDirs += Split-Path $onPath.Source -Parent }
$vsCmakeRoot = Join-Path $vsInstallPath 'Common7\IDE\CommonExtensions\Microsoft\CMake'
$toolDirs += (Join-Path $vsCmakeRoot 'CMake\bin')
$toolDirs += (Join-Path $vsCmakeRoot 'Ninja')
$toolDirs += (Join-Path $env:ProgramFiles 'CMake\bin')
$toolDirs = @($toolDirs | Where-Object { $_ -and (Test-Path $_) } | Select-Object -Unique)

if (-not ($toolDirs | Where-Object { Test-Path (Join-Path $_ 'cmake.exe') })) {
    Write-Error "ERROR: no cmake.exe found on PATH, in '$vsCmakeRoot', or in Program Files"
    exit 1
}

# --- Qt ----------------------------------------------------------------------------
#
# Qt's bin must be on PATH for the build, not merely for CMake: KSyntaxHighlighting builds
# katehighlightingindexer.exe and runs it mid-build, which dies with 0xc0000135 without the
# Qt DLLs beside it. Take the location from the configured cache where there is one, so the
# build cannot use a different Qt from the one it was configured against.

function Find-QtPrefix {
    param([string]$CacheFile)

    $candidates = @()

    if (Test-Path $CacheFile) {
        foreach ($line in Get-Content -LiteralPath $CacheFile) {
            if ($line -match '^Qt6_DIR:[^=]*=(.+)$') {
                # <prefix>/lib/cmake/Qt6 -> <prefix>
                $candidates += (Join-Path $matches[1] '..\..\..')
            } elseif ($line -match '^CMAKE_PREFIX_PATH:[^=]*=(.+)$') {
                $candidates += ($matches[1] -split ';')
            }
        }
    }

    $candidates += @($env:QT_ROOT_DIR, $env:QTDIR)

    $qtRoot = Join-Path $env:SystemDrive 'Qt'
    if (Test-Path $qtRoot) {
        $candidates += (Get-ChildItem $qtRoot -Directory -Filter '6.*' -ErrorAction SilentlyContinue |
                        Sort-Object Name -Descending |
                        ForEach-Object { Get-ChildItem $_.FullName -Directory -Filter 'msvc*_64' -ErrorAction SilentlyContinue } |
                        Select-Object -ExpandProperty FullName)
    }

    foreach ($c in $candidates) {
        if ([string]::IsNullOrWhiteSpace($c)) { continue }
        $bin = Join-Path $c 'bin'
        if (Test-Path (Join-Path $bin 'windeployqt.exe')) {
            return (Resolve-Path $bin).Path
        }
    }
    return $null
}

$qtBin = Find-QtPrefix (Join-Path $buildDir 'CMakeCache.txt')
if (-not $qtBin) {
    Write-Error ("ERROR: no Qt 6 install found. Install Qt 6.9+ (msvc2022_64), then either " +
                 "put it under $(Join-Path $env:SystemDrive 'Qt'), set QT_ROOT_DIR, or pass " +
                 "-DCMAKE_PREFIX_PATH=<qt prefix> when configuring.")
    exit 1
}

# --- Run ---------------------------------------------------------------------------
#
# The PATH additions must precede `call vcvars64.bat`, not follow it: cmd expands
# %PATH% when it parses the line, so appending afterwards silently discards everything
# vcvars added.
$pathPrefix = (@($vsInstallerDir, $qtBin) + $toolDirs) -join ';'
$prelude    = "set ""PATH=$pathPrefix;%PATH%"" & call ""$vcvars"" >nul"

# A fresh clone has no cache; configure it rather than failing on a missing build tree.
$commands = @()
if (-not (Test-Path (Join-Path $buildDir 'CMakeCache.txt'))) {
    Write-Host "No configured build tree; running cmake --preset windows-msvc"
    $commands += 'cmake --preset windows-msvc'
}
$targetArg = if ([string]::IsNullOrWhiteSpace($Target)) { '' } else { " --target $Target" }
$commands += "cmake --build build$targetArg"

Push-Location $repoRoot
try {
    foreach ($c in $commands) {
        & cmd /c "$prelude & $c"
        $exitCode = $LASTEXITCODE
        if ($exitCode -ne 0) { break }
    }
} finally {
    Pop-Location
}

if ($exitCode -ne 0) {
    exit $exitCode
}
