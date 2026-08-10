<#
.SYNOPSIS
    Builds ShaderPlayer via the documented MSVC + Qt + CMake environment.

.DESCRIPTION
    Wraps the build line documented in CLAUDE.md so the eight-plus done-when: checks in
    tasks/todo.md do not each re-derive it. Locates the Visual Studio installation with
    vswhere (BuildTools installs need `-products *`, or vswhere prints nothing), puts the
    VS Installer directory, Qt's bin, and CMake's bin on PATH *before* calling
    vcvars64.bat (cmd expands %PATH% when it parses the line, so appending after the call
    would discard everything vcvars added), then runs `cmake --build build`.
#>

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
$qtBin          = 'C:\Qt\6.9.1\msvc2022_64\bin'
$cmakeBin       = 'C:\Program Files\CMake\bin'
$repoRoot       = Split-Path $PSScriptRoot -Parent

# The PATH additions must precede `call vcvars64.bat`, not follow it: cmd expands
# %PATH% when it parses the line, so appending afterwards silently discards everything
# vcvars added.
$buildCmd = "set ""PATH=$vsInstallerDir;$qtBin;$cmakeBin;%PATH%"" & " +
            "call ""$vcvars"" >nul & cmake --build build"

Push-Location $repoRoot
try {
    & cmd /c $buildCmd
    $exitCode = $LASTEXITCODE
} finally {
    Pop-Location
}

if ($exitCode -ne 0) {
    exit $exitCode
}
