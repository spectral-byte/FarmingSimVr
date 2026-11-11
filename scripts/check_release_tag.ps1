# Verifies that a release tag names the Companion Mod's version, and says what the release workflow
# needs to know about it. A release is named by its tag: v1.2.0, or v1.2.0-pre.1 for a pre-release
# (any suffix after the version). Its assets carry the Companion Mod's four-part version (modDesc.xml,
# for example 1.2.0.0) with the last ".0" dropped, so the tag must equal that: v1.2.0 for 1.2.0.0.
# A mod version that does not end in ".0" has no release name on that rule and cannot be released.
#
# When GITHUB_OUTPUT is set (GitHub Actions), writes these outputs for later steps, and only once every
# check has passed:
#   version      the tag's version without the leading v or the suffix (1.2.0)
#   mod_version  the Companion Mod's version (1.2.0.0), the one packaging puts in the asset names
#   prerelease   true when the tag has a suffix, else false
# scripts\test_check_release_tag.ps1 is this script's own check.
param(
    [Parameter(Mandatory = $true)]
    [string]$Tag,
    [string]$ModDescPath = (Join-Path (Split-Path -Parent $PSScriptRoot) "mods\FS25_VRCompanion\modDesc.xml")
)
$ErrorActionPreference = "Stop"

# Numbers have no leading zeros; a suffix is dot-separated lowercase letters and digits.
$number = '(0|[1-9][0-9]*)'
$tagPattern = "^v($number\.$number\.$number)(-[a-z0-9]+(\.[a-z0-9]+)*)?$"
if ($Tag -cnotmatch $tagPattern) {
    Write-Error "The tag '$Tag' is not a version tag. Expected vMAJOR.MINOR.PATCH, optionally followed by a pre-release suffix (v1.2.0, v1.2.0-pre.1)."
}
$tagVersion = $Matches[1]
$prerelease = [bool]$Matches[5]

if (-not (Test-Path $ModDescPath)) {
    Write-Error "$ModDescPath not found."
}
[xml]$modDesc = Get-Content $ModDescPath -Raw
$modVersion = $modDesc.modDesc.version
if ([string]::IsNullOrWhiteSpace($modVersion)) {
    Write-Error "No <version> element found in $ModDescPath."
}
if ($modVersion -notmatch "^$number\.$number\.$number\.0$") {
    Write-Error "The Companion Mod's version is '$modVersion' (in $ModDescPath). A release needs four numbers ending in .0 (for example 1.2.0.0), because its assets are named with the last .0 dropped."
}
$releaseVersion = $modVersion.Substring(0, $modVersion.Length - 2)
if ($tagVersion -ne $releaseVersion) {
    Write-Error "The tag '$Tag' is version $tagVersion, but the Companion Mod's version is $modVersion (in $ModDescPath), which is released as v$releaseVersion. Fix the tag or the mod version so they agree."
}

Write-Host "Release tag OK: $Tag is Companion Mod version $modVersion (assets are named FS25_VR_v$releaseVersion*), prerelease=$($prerelease.ToString().ToLower())."
if ($env:GITHUB_OUTPUT) {
    "version=$releaseVersion", "mod_version=$modVersion", "prerelease=$($prerelease.ToString().ToLower())" |
        Out-File $env:GITHUB_OUTPUT -Append -Encoding utf8
}
