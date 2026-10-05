# Verifies that a CMake build tree uses the shipped configuration:
# RelWithDebInfo with full inlining (/Ob2) and debug info (/Zi).
param(
    [string]$BuildDir = "build"
)
$ErrorActionPreference = "Stop"

$cache = Join-Path $BuildDir "CMakeCache.txt"
if (-not (Test-Path $cache)) {
    Write-Error "$cache not found. Configure first: cmake -B $BuildDir -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo"
}

function Get-CacheValue([string]$name) {
    $line = Select-String -Path $cache -Pattern "^${name}:[A-Z]+=(.*)$" | Select-Object -First 1
    if ($line) { $line.Matches[0].Groups[1].Value } else { "" }
}

$buildType = Get-CacheValue "CMAKE_BUILD_TYPE"
if ($buildType -ne "RelWithDebInfo") {
    Write-Error "CMAKE_BUILD_TYPE is '$buildType', expected RelWithDebInfo."
}
foreach ($var in "CMAKE_CXX_FLAGS_RELWITHDEBINFO", "CMAKE_C_FLAGS_RELWITHDEBINFO") {
    $flags = Get-CacheValue $var
    if ($flags -notmatch '(^|\s)/Ob2(\s|$)' -or $flags -notmatch '(^|\s)/Zi(\s|$)') {
        Write-Error "$var is '$flags'; expected full inlining (/Ob2) with debug info (/Zi)."
    }
}
Write-Host "Build configuration OK: RelWithDebInfo with /Ob2 and /Zi."
