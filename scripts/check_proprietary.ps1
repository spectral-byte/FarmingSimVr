# Fails if proprietary GIANTS scripts or third-party mod files are tracked.
# Checks tracked files only, so local ignored copies under dataS/ or reference/ are fine.
$ErrorActionPreference = "Stop"
Set-Location (Split-Path -Parent $PSScriptRoot)

$problems = @(git ls-files -- dataS reference)

# Headers of the removed files. This script is excluded so its own patterns don't match.
$problems += @(git grep -n -I -F -e "GIANTS Software GmbH, Confidential" -e "Copyright (c) StjerneIdioten" -- . ":!scripts/check_proprietary.ps1")
if ($LASTEXITCODE -gt 1) { Write-Error "git grep failed with exit code $LASTEXITCODE." }

if ($problems.Count) {
    Write-Host "Proprietary or third-party files are tracked; remove them:"
    $problems | ForEach-Object { Write-Host "  $_" }
    exit 1
}
Write-Host "No proprietary files tracked."
exit 0
