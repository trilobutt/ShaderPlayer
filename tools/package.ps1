<#
.SYNOPSIS
    Builds a release stage and an Inno Setup installer for ShaderPlayer.

.DESCRIPTION
    Stages a release tree by allowlist, never by exclusion, so a new build artefact
    (a stray .pdb, a local config.json, the shader bytecode cache) cannot leak into a
    public download by default: only the files named below are copied, and the result
    is asserted clean afterwards. Then runs Inno Setup over the stage.

    The version is read from CMakeLists.txt rather than taken as a parameter, so there
    is exactly one place it can be wrong.

.PARAMETER BuildDir
    The CMake build directory to stage from. Defaults to "build".

.PARAMETER StageDir
    Where the release tree is assembled before compression. Defaults to
    "build/package"; deleted and recreated on every run.

.PARAMETER OutputDir
    Where the finished installer is written. Defaults to "dist".

.PARAMETER SkipBuild
    Skip the `tools/build.ps1` step and package whatever is already in $BuildDir.
#>
param(
    [string]$BuildDir = "build",
    [string]$StageDir = "build/package",
    [string]$OutputDir = "dist",
    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path $PSScriptRoot -Parent
Push-Location $repoRoot
try {

# --- Version -------------------------------------------------------------------------
#
# Read from CMakeLists.txt rather than accepted as a parameter: a second place to write
# the version is a second place for it to be wrong.

$cmakeListsPath = Join-Path $repoRoot 'CMakeLists.txt'
$versionMatch = [regex]::Match((Get-Content $cmakeListsPath -Raw), 'project\(ShaderPlayer VERSION ([0-9]+\.[0-9]+\.[0-9]+)')
if (-not $versionMatch.Success) {
    throw "Could not find 'project(ShaderPlayer VERSION X.Y.Z' in $cmakeListsPath. Fix: check the project() line has not been reworded."
}
$version = $versionMatch.Groups[1].Value
Write-Host "Packaging ShaderPlayer $version"

# --- Build -----------------------------------------------------------------------------

if (-not $SkipBuild) {
    & pwsh -File (Join-Path $repoRoot 'tools\build.ps1')
    if ($LASTEXITCODE -ne 0) {
        throw "tools/build.ps1 exited with code $LASTEXITCODE. Fix: run it directly and read the build output."
    }
}

$buildDirFull = Join-Path $repoRoot $BuildDir

# --- Assert a release build --------------------------------------------------------
#
# This is the blanked-flags cache failure documented in CLAUDE.md: a cache whose
# CMAKE_CXX_FLAGS entries have been emptied still says RelWithDebInfo and still builds,
# but produces an unoptimised binary with no /O2 and no /DNDEBUG. Check both.

$cacheFile = Join-Path $buildDirFull 'CMakeCache.txt'
if (-not (Test-Path $cacheFile)) {
    throw "$cacheFile not found. Fix: run tools/build.ps1 (or drop -SkipBuild) so the build directory is configured."
}
$cacheContent = Get-Content $cacheFile -Raw
if ($cacheContent -notmatch 'CMAKE_BUILD_TYPE:STRING=RelWithDebInfo') {
    throw "$cacheFile is not configured as RelWithDebInfo. Fix: delete $BuildDir\CMakeCache.txt and $BuildDir\CMakeFiles\, then re-run cmake --preset windows-msvc (see CLAUDE.md, 'When the cache goes wrong')."
}
$flagsMatch = [regex]::Match($cacheContent, 'CMAKE_CXX_FLAGS_RELWITHDEBINFO:STRING=(.*)')
if (-not $flagsMatch.Success -or $flagsMatch.Groups[1].Value -notmatch '/O2' -or $flagsMatch.Groups[1].Value -notmatch '/DNDEBUG') {
    throw "CMAKE_CXX_FLAGS_RELWITHDEBINFO in $cacheFile is missing /O2 or /DNDEBUG (blanked-flags cache failure). Fix: delete $BuildDir\CMakeCache.txt and $BuildDir\CMakeFiles\, then reconfigure (see CLAUDE.md, 'When the cache goes wrong')."
}

# --- Stage -----------------------------------------------------------------------------

$stageDirFull = Join-Path $repoRoot $StageDir
if (Test-Path $stageDirFull) {
    Remove-Item -Recurse -Force $stageDirFull
}
New-Item -ItemType Directory -Force $stageDirFull | Out-Null

function Copy-RequiredFile {
    param([string]$Source, [string]$Destination)
    if (-not (Test-Path $Source)) {
        throw "Required file '$Source' not found. Fix: build ShaderPlayer (tools/build.ps1) before packaging."
    }
    Copy-Item -Path $Source -Destination $Destination -Force
}

function Copy-RequiredDirectory {
    param([string]$Source, [string]$Destination)
    if (-not (Test-Path $Source)) {
        throw "Required directory '$Source' not found. Fix: build ShaderPlayer (tools/build.ps1) before packaging."
    }
    Copy-Item -Path $Source -Destination $Destination -Recurse -Force
}

# Executables.
Copy-RequiredFile (Join-Path $buildDirFull 'ShaderPlayer.exe') $stageDirFull
Copy-RequiredFile (Join-Path $buildDirFull 'shaderfx.exe') $stageDirFull

# Every DLL in the build directory root (Qt, FFmpeg, KSyntaxHighlighting, DXC).
$dlls = Get-ChildItem -Path $buildDirFull -Filter '*.dll' -File
if ($dlls.Count -eq 0) {
    throw "No .dll files found in $buildDirFull. Fix: build ShaderPlayer (tools/build.ps1) before packaging."
}
foreach ($dll in $dlls) {
    Copy-Item -Path $dll.FullName -Destination $stageDirFull -Force
}

# VC++ redistributable installer.
Copy-RequiredFile (Join-Path $buildDirFull 'vc_redist.x64.exe') $stageDirFull

# Qt plugin directories that windeployqt decided this build needs. Not every one is
# guaranteed present (e.g. "tls" only appears when Qt6Network pulled in TLS support),
# so only copy the ones that exist; nothing here is required to be all seven.
$qtPluginDirs = @('platforms', 'styles', 'imageformats', 'iconengines', 'networkinformation', 'tls', 'generic')
foreach ($dir in $qtPluginDirs) {
    $src = Join-Path $buildDirFull $dir
    if (Test-Path $src) {
        Copy-RequiredDirectory $src (Join-Path $stageDirFull $dir)
    }
}

# Shipped shader set.
Copy-RequiredDirectory (Join-Path $buildDirFull 'shaders') (Join-Path $stageDirFull 'shaders')

# Licensing.
Copy-RequiredFile (Join-Path $repoRoot 'LICENSE') $stageDirFull
Copy-RequiredFile (Join-Path $repoRoot 'THIRD_PARTY_NOTICES.md') $stageDirFull

# Empty layouts directory: WorkspaceManager::ScanDirectory looks here for .ini presets,
# and the built-in Default (index 0) is what a first run applies regardless, so this
# only needs to exist for a preset saved later to have somewhere to land.
New-Item -ItemType Directory -Force (Join-Path $stageDirFull 'layouts') | Out-Null

# --- Assert the stage is clean ------------------------------------------------------
#
# The check that keeps a local video path (config.json's lastOpenedVideo) or a debug
# artefact out of a public download.

$forbiddenNames = @('config.json', 'shader_cache', '*.pdb', '*.ilk', '*.lib', '*.obj')
foreach ($pattern in $forbiddenNames) {
    $hits = Get-ChildItem -Path $stageDirFull -Recurse -Force -Filter $pattern
    if ($hits.Count -gt 0) {
        $hitPaths = ($hits | ForEach-Object { $_.FullName }) -join ', '
        throw "Stage directory contains forbidden item(s) matching '$pattern': $hitPaths. Fix: remove the source of these from $BuildDir before packaging, or narrow the copy step that pulled them in."
    }
}

$hlslCount = (Get-ChildItem -Path (Join-Path $stageDirFull 'shaders') -Filter '*.hlsl' -File).Count
if ($hlslCount -ne 45) {
    throw "Expected 45 .hlsl files in $stageDirFull\shaders, found $hlslCount. Fix: check default_shaders/ and the CMakeLists.txt POST_BUILD copy step (line ~410)."
}

# --- Locate Inno Setup ---------------------------------------------------------------

$isccCandidates = @(
    (Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\ISCC.exe'),
    (Join-Path $env:ProgramFiles 'Inno Setup 6\ISCC.exe'),
    (Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 6\ISCC.exe')
)
$iscc = $isccCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $iscc) {
    $onPath = Get-Command ISCC -ErrorAction SilentlyContinue
    if ($onPath) { $iscc = $onPath.Source }
}
if (-not $iscc) {
    throw "ISCC.exe (Inno Setup 6) not found. Fix: winget install -e --id JRSoftware.InnoSetup"
}

# --- Build the installer --------------------------------------------------------------

$outputDirFull = Join-Path $repoRoot $OutputDir
New-Item -ItemType Directory -Force $outputDirFull | Out-Null

$issPath = Join-Path $repoRoot 'tools\installer\ShaderPlayer.iss'
& $iscc "/DAppVersion=$version" "/DStageDir=$stageDirFull" "/DOutputDir=$outputDirFull" $issPath
if ($LASTEXITCODE -ne 0) {
    throw "ISCC.exe exited with code $LASTEXITCODE. Fix: read the compiler output above for the failing [Files]/[Setup] line."
}

$installerPath = Join-Path $outputDirFull "ShaderPlayer-$version-setup.exe"
if (-not (Test-Path $installerPath)) {
    throw "ISCC.exe reported success but $installerPath does not exist. Fix: check OutputBaseFilename in tools/installer/ShaderPlayer.iss matches 'ShaderPlayer-{#AppVersion}-setup'."
}
$installerSizeMB = [math]::Round((Get-Item $installerPath).Length / 1MB, 1)
Write-Host "Wrote $installerPath ($installerSizeMB MB)"

} finally {
    Pop-Location
}
