# Runs scripts\package_release.ps1 against a scratch copy of the repo and inspects what it leaves in the
# scratch build\release, so the real build\release is never touched. Needs a built Injector and a
# configured build tree (build\version.dll, build\version.pdb, build\CMakeCache.txt); a run that has to
# succeed also needs Inno Setup, because packaging compiles the Installer. Fails (non-zero exit) if any
# assertion fails. No Pester.
#
# Extending: add a Test-Case at the bottom. New-ScratchRepo builds the world, Invoke-Packaging runs the
# script in it, the Assert-* helpers check what it left behind.
param(
    # Only run the cases whose name matches this wildcard (e.g. "*PDB*"); all cases when empty.
    [string]$Filter = ""
)
$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.IO.Compression.FileSystem

$repoRoot = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $repoRoot "build"
foreach ($file in "version.dll", "version.pdb", "CMakeCache.txt") {
    if (-not (Test-Path (Join-Path $buildDir $file))) {
        Write-Error "build\$file not found. Build the Injector first (cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo; cmake --build build --target injector)."
    }
}

$failures = New-Object System.Collections.Generic.List[string]

# A copy of everything package_release.ps1 reads from the repo, with the real build's outputs in its
# build\ folder. CMakeCache.txt still names the real dependency sources, which the licence notices come from.
function New-ScratchRepo {
    $root = Join-Path ([IO.Path]::GetTempPath()) ("fs25vr-package-test-" + [Guid]::NewGuid().ToString("N"))
    New-Item -ItemType Directory -Path "$root\build" -Force | Out-Null
    foreach ($dir in "scripts", "installer", "mods") {
        Copy-Item (Join-Path $repoRoot $dir) (Join-Path $root $dir) -Recurse
    }
    Copy-Item (Join-Path $repoRoot "LICENSE") $root
    foreach ($file in "version.dll", "version.pdb", "CMakeCache.txt") {
        Copy-Item (Join-Path $buildDir $file) "$root\build\$file"
    }
    $root
}

function Remove-ScratchRepo([string]$root) {
    if ($root -and (Test-Path $root)) { Remove-Item $root -Recurse -Force -ErrorAction SilentlyContinue }
}

# Runs the scratch copy of package_release.ps1 in a child shell and returns its exit code and output.
function Invoke-Packaging([string]$root) {
    $out = Join-Path $root "packaging.out.txt"
    $err = Join-Path $root "packaging.err.txt"
    $shell = (Get-Process -Id $PID).Path
    $process = Start-Process -FilePath $shell -PassThru -Wait -NoNewWindow `
        -ArgumentList @("-NoProfile", "-NonInteractive", "-File", "`"$root\scripts\package_release.ps1`"") `
        -RedirectStandardOutput $out -RedirectStandardError $err
    [pscustomobject]@{
        ExitCode = $process.ExitCode
        Output   = ((Get-Content $out -Raw) + (Get-Content $err -Raw))
    }
}

# The four-part version that names every release file (FS25_VR_v<version>...), read from modDesc.xml.
function Get-ReleaseVersion([string]$root) {
    [xml]$modDesc = Get-Content (Join-Path $root "mods\FS25_VRCompanion\modDesc.xml") -Raw
    $modDesc.modDesc.version
}

function Get-ZipEntryNames([string]$zipPath) {
    $zip = [IO.Compression.ZipFile]::OpenRead($zipPath)
    try { @($zip.Entries | ForEach-Object { $_.FullName }) } finally { $zip.Dispose() }
}

function Get-ZipEntryHash([string]$zipPath, [string]$entryName) {
    $zip = [IO.Compression.ZipFile]::OpenRead($zipPath)
    try {
        $stream = $zip.GetEntry($entryName).Open()
        try { [BitConverter]::ToString([Security.Cryptography.SHA256]::Create().ComputeHash($stream)) } finally { $stream.Dispose() }
    } finally { $zip.Dispose() }
}

function Assert-True([bool]$condition, [string]$message) { if (-not $condition) { throw $message } }
function Assert-Exists([string]$path, [string]$what) { Assert-True (Test-Path $path) "$what should exist: $path" }

function Test-Case([string]$name, [scriptblock]$body) {
    if ($Filter -and $name -notlike $Filter) { return }
    $root = New-ScratchRepo
    try {
        & $body $root
        Write-Host "[PASS] $name"
    } catch {
        Write-Host "[FAIL] $name -- $($_.Exception.Message)"
        $failures.Add($name)
    } finally {
        Remove-ScratchRepo $root
    }
}

Test-Case "Packaging produces a symbols zip next to the zip bundle and the Setup exe, holding the linker's PDB" {
    param($root)
    $result = Invoke-Packaging $root
    Assert-True ($result.ExitCode -eq 0) "Packaging exited with $($result.ExitCode):`n$($result.Output)"

    $release = Join-Path $root "build\release"
    $version = Get-ReleaseVersion $root
    Assert-Exists (Join-Path $release "FS25_VR_v$version.zip") "The zip bundle"
    Assert-Exists (Join-Path $release "FS25_VR_v${version}_Setup.exe") "The Installer"
    $symbolsZip = Join-Path $release "FS25_VR_v${version}_Symbols.zip"
    Assert-Exists $symbolsZip "The symbols zip"

    $entries = Get-ZipEntryNames $symbolsZip
    Assert-True ($entries -contains "version.pdb") "The symbols zip should hold version.pdb, it holds: $($entries -join ', ')"
    $linkerPdb = (Get-FileHash (Join-Path $buildDir "version.pdb") -Algorithm SHA256).Hash
    $zippedPdb = (Get-ZipEntryHash $symbolsZip "version.pdb") -replace "-", ""
    Assert-True ($zippedPdb -eq $linkerPdb) "version.pdb in the symbols zip is not the PDB the linker wrote."

    # scripts\test_installer.ps1 stages its newer Installers from the one FS25_VR_v* folder in build\release.
    $stagingFolders = @(Get-ChildItem $release -Directory -Filter "FS25_VR_v*")
    Assert-True ($stagingFolders.Count -eq 1) "build\release should hold exactly one FS25_VR_v* folder, it holds $($stagingFolders.Count)."
}

if ($failures.Count -gt 0) {
    Write-Error "$($failures.Count) release packaging test(s) failed: $($failures -join '; ')"
}
Write-Host "All release packaging tests passed."
