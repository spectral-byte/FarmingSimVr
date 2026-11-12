# Builds the VDXR simulation layer (XrApiLayer_vdxr_sim.dll + its manifest) and harness.exe with the MSVC x64 toolchain.
# Outputs go to <BuildDir>\vdxr_sim_layer (git-ignored). The OpenXR headers come from the repository's CMake build, so
# configure the repository first (any target fetches them).
#   .\build.ps1                     # <repo>\build
#   .\build.ps1 -BuildDir <path>    # another CMake build folder
param([string]$BuildDir = (Join-Path $PSScriptRoot '..\..\build'))
$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
$BuildDir = (Resolve-Path $BuildDir).Path
$out = Join-Path $BuildDir 'vdxr_sim_layer'

# The OpenXR SDK source: build\_deps\openxr-src, or the folder FETCHCONTENT_SOURCE_DIR_OPENXR points at.
$openxrSrc = Join-Path $BuildDir '_deps\openxr-src'
$cache = Join-Path $BuildDir 'CMakeCache.txt'
if (Test-Path $cache) {
    $line = Select-String -Path $cache -Pattern '^FETCHCONTENT_SOURCE_DIR_OPENXR:[A-Z]+=(.+)$' | Select-Object -First 1
    if ($line -and $line.Matches[0].Groups[1].Value.Trim()) { $openxrSrc = $line.Matches[0].Groups[1].Value.Trim() }
}
$openxrInclude = Join-Path $openxrSrc 'include'
if (-not (Test-Path (Join-Path $openxrInclude 'openxr\openxr.h'))) {
    throw "OpenXR headers not found in $openxrInclude. Configure the repository's CMake build in $BuildDir first."
}
$includes = @('/I', $openxrInclude)
$generated = Join-Path $BuildDir '_deps\openxr-build\include'
if (Test-Path $generated) { $includes += @('/I', $generated) }

if (-not (Get-Command cl -ErrorAction SilentlyContinue)) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $vs = if (Test-Path $vswhere) {
        & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    }
    if (-not $vs) { throw "No Visual Studio with the C++ x64 tools found; run this from a Developer PowerShell." }
    Import-Module (Join-Path $vs 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
    Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' *> $null
}

New-Item -ItemType Directory -Force $out | Out-Null
Push-Location $out
try {
    cl /nologo /LD /EHsc /O2 /MT /W4 /utf-8 @includes `
        (Join-Path $here 'vdxr_sim_layer.cpp') /Fe:XrApiLayer_vdxr_sim.dll /link dxgi.lib
    if ($LASTEXITCODE -ne 0) { throw "cl failed for vdxr_sim_layer.cpp with exit code $LASTEXITCODE" }
    cl /nologo /EHsc /O2 /MT /W4 /utf-8 (Join-Path $here 'harness.cpp') /Fe:harness.exe
    if ($LASTEXITCODE -ne 0) { throw "cl failed for harness.cpp with exit code $LASTEXITCODE" }
    # The manifest names the DLL relative to itself, so it sits next to it.
    Copy-Item (Join-Path $here 'XrApiLayer_vdxr_sim.json') $out
} finally {
    Pop-Location
}
Write-Host "Built into $out"
