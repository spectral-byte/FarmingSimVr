# Verifies that release packaging produced the symbols zip (FS25_VR_v<version>_Symbols.zip) and that it
# holds version.pdb, byte for byte the PDB the linker wrote for the Injector, and that this PDB belongs to the
# Injector the bundle ships (the GUID and age in the DLL's debug directory are the PDB's own). Run it after
# scripts\package_release.ps1; CI does, and so does scripts\test_package_release.ps1.
param(
    [string]$ReleaseDir = (Join-Path (Split-Path -Parent $PSScriptRoot) "build\release"),
    [string]$Pdb = (Join-Path (Split-Path -Parent $PSScriptRoot) "build\version.pdb")
)
$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.IO.Compression.FileSystem
. (Join-Path $PSScriptRoot "symbols_identity.ps1")

$zips = @(Get-ChildItem $ReleaseDir -File -Filter "FS25_VR_v*_Symbols.zip" -ErrorAction SilentlyContinue)
if ($zips.Count -ne 1) {
    Write-Error "Expected exactly one FS25_VR_v*_Symbols.zip in $ReleaseDir, found $($zips.Count). Run scripts\package_release.ps1."
}
$symbolsZip = $zips[0].FullName

if (-not (Test-Path $Pdb)) {
    Write-Error "$Pdb not found, so the symbols zip cannot be compared with the PDB the linker wrote."
}

$zip = [IO.Compression.ZipFile]::OpenRead($symbolsZip)
try {
    $entry = $zip.GetEntry("version.pdb")
    if (-not $entry) {
        $names = ($zip.Entries | ForEach-Object { $_.FullName }) -join ", "
        Write-Error "$symbolsZip does not hold version.pdb at its root (it holds: $names)."
    }
    $stream = $entry.Open()
    try {
        $sha256 = [Security.Cryptography.SHA256]::Create()
        $zippedHash = [BitConverter]::ToString($sha256.ComputeHash($stream)) -replace "-", ""
    } finally {
        $stream.Dispose()
    }
} finally {
    $zip.Dispose()
}

$linkerHash = (Get-FileHash $Pdb -Algorithm SHA256).Hash
if ($zippedHash -ne $linkerHash) {
    Write-Error "version.pdb in $symbolsZip is not the PDB the linker wrote ($Pdb)."
}

# The PDB the linker wrote must also be the one the shipped Injector was linked with: the copy in the bundle's x64
# folder is what the Installer places and the zip bundle carries.
$shipped = @(Get-ChildItem $ReleaseDir -Directory -Filter "FS25_VR_v*" -ErrorAction SilentlyContinue |
    ForEach-Object { Join-Path $_.FullName "x64\version.dll" } | Where-Object { Test-Path $_ })
if ($shipped.Count -ne 1) {
    Write-Error "Expected exactly one FS25_VR_v*\x64\version.dll in $ReleaseDir, found $($shipped.Count). Run scripts\package_release.ps1."
}
Assert-PdbBelongsToDll $Pdb $shipped[0]
$identity = Get-PdbIdentity $Pdb
Write-Host "Symbols zip OK: $($zips[0].Name) holds the linker's version.pdb, which belongs to the shipped Injector (GUID $($identity.Guid), age $($identity.Age))."
