<#
.SYNOPSIS
    Configures (if needed) and builds ShaderPlayer via the documented MSVC + CMake
    environment.

.DESCRIPTION
    Wraps the build line documented in CLAUDE.md so the eight-plus done-when: checks in
    tasks/todo.md do not each re-derive it. Locates the Visual Studio installation with
    vswhere (BuildTools installs need `-products *`, or vswhere prints nothing), puts the
    VS Installer directory and CMake's bin on PATH *before* calling vcvars64.bat (cmd
    expands %PATH% when it parses the line, so appending after the call would discard
    everything vcvars added), then runs `cmake --build build`.

    Nothing here is a fixed absolute path beyond the VS Installer's own well-known
    location: CMake is discovered on this machine, and Qt by CMakeLists.txt, so a fresh
    clone builds without editing the script.

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

# --- Run ---------------------------------------------------------------------------
#
# The PATH additions must precede `call vcvars64.bat`, not follow it: cmd expands
# %PATH% when it parses the line, so appending afterwards silently discards everything
# vcvars added.
$pathPrefix = (@($vsInstallerDir) + $toolDirs) -join ';'
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
