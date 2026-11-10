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

# Runs one of the scratch copy's scripts in a child shell and returns its exit code and output.
function Invoke-ScratchScript([string]$root, [string]$script) {
    $out = Join-Path $root "$script.out.txt"
    $err = Join-Path $root "$script.err.txt"
    $shell = (Get-Process -Id $PID).Path
    $process = Start-Process -FilePath $shell -PassThru -Wait -NoNewWindow `
        -ArgumentList @("-NoProfile", "-NonInteractive", "-File", "`"$root\scripts\$script.ps1`"") `
        -RedirectStandardOutput $out -RedirectStandardError $err
    [pscustomobject]@{
        ExitCode = $process.ExitCode
        Output   = ((Get-Content $out -Raw) + (Get-Content $err -Raw))
    }
}

function Invoke-Packaging([string]$root) { Invoke-ScratchScript $root "package_release" }

# What CI runs after packaging, against the scratch release folder and the scratch build's PDB.
function Invoke-SymbolsCheck([string]$root) { Invoke-ScratchScript $root "check_release_symbols" }

# Replaces the symbols zip in the scratch release folder with one holding exactly these files (name -> bytes).
function Set-SymbolsZip([string]$root, [hashtable]$files) {
    $release = Join-Path $root "build\release"
    $zipPath = (Get-ChildItem $release -File -Filter "FS25_VR_v*_Symbols.zip" | Select-Object -First 1).FullName
    Remove-Item $zipPath
    $zip = [IO.Compression.ZipFile]::Open($zipPath, [IO.Compression.ZipArchiveMode]::Create)
    try {
        foreach ($name in $files.Keys) {
            $stream = $zip.CreateEntry($name).Open()
            try { $stream.Write($files[$name], 0, $files[$name].Length) } finally { $stream.Dispose() }
        }
    } finally { $zip.Dispose() }
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

Test-Case "Packaging without the Injector's PDB fails, says what is missing and why, and produces no release files" {
    param($root)
    Remove-Item (Join-Path $root "build\version.pdb")
    $result = Invoke-Packaging $root
    Assert-True ($result.ExitCode -ne 0) "Packaging succeeded without build\version.pdb:`n$($result.Output)"
    Assert-True ($result.Output -match "version\.pdb") "The message should name the missing build\version.pdb:`n$($result.Output)"
    Assert-True ($result.Output -match "(?i)symbols") "The message should say the release needs the Injector's symbols:`n$($result.Output)"

    $release = Join-Path $root "build\release"
    $produced = @(Get-ChildItem $release -File -Filter "FS25_VR_v*" -ErrorAction SilentlyContinue)
    Assert-True ($produced.Count -eq 0) "Nothing should be packaged, but found: $($produced.Name -join ', ')"
}

Test-Case "The symbols check accepts a packaged release and rejects one whose symbols zip is missing, has no PDB or holds another PDB" {
    param($root)
    $packaging = Invoke-Packaging $root
    Assert-True ($packaging.ExitCode -eq 0) "Packaging exited with $($packaging.ExitCode):`n$($packaging.Output)"

    $check = Invoke-SymbolsCheck $root
    Assert-True ($check.ExitCode -eq 0) "The check rejected a freshly packaged release:`n$($check.Output)"

    Set-SymbolsZip $root @{ "readme.txt" = [Text.Encoding]::ASCII.GetBytes("no symbols here") }
    $check = Invoke-SymbolsCheck $root
    Assert-True ($check.ExitCode -ne 0) "The check accepted a symbols zip without version.pdb."
    Assert-True ($check.Output -match "version\.pdb") "The message should name the missing version.pdb:`n$($check.Output)"

    Set-SymbolsZip $root @{ "version.pdb" = [Text.Encoding]::ASCII.GetBytes("not the linker's PDB") }
    $check = Invoke-SymbolsCheck $root
    Assert-True ($check.ExitCode -ne 0) "The check accepted a version.pdb that is not the one the linker wrote."

    Get-ChildItem (Join-Path $root "build\release") -File -Filter "FS25_VR_v*_Symbols.zip" | Remove-Item
    $check = Invoke-SymbolsCheck $root
    Assert-True ($check.ExitCode -ne 0) "The check accepted a release without a symbols zip."
    Assert-True ($check.Output -match "Symbols\.zip") "The message should name the missing symbols zip:`n$($check.Output)"
}

# A PDB belongs to a DLL when both carry the GUID and age the linker stamped them with. These cases make a DLL
# that names another PDB than the one it was linked with, by changing its CodeView record, so that the real PDB
# in the scratch build no longer belongs to it.
. (Join-Path $PSScriptRoot "symbols_identity.ps1")
function Set-DllPdbIdentity([string]$dllPath, [ValidateSet("GUID", "age")][string]$Change) {
    $offset = (Get-DllPdbIdentity $dllPath).Offset
    $bytes = [IO.File]::ReadAllBytes($dllPath)
    if ($Change -eq "GUID") { $bytes[$offset + 4] = $bytes[$offset + 4] -bxor 0xFF } else { $bytes[$offset + 20] = $bytes[$offset + 20] + 1 }
    [IO.File]::WriteAllBytes($dllPath, $bytes)
}

# The console wraps an error message at its own width: compare the message with its line breaks folded away.
function Get-OneLine([string]$text) { $text -replace '\s+', ' ' }

foreach ($change in "GUID", "age") {
    Test-Case "Packaging refuses an Injector whose PDB has another $change, says so, and produces no release files" {
        param($root)
        Set-DllPdbIdentity (Join-Path $root "build\version.dll") $change
        $result = Invoke-Packaging $root
        Assert-True ($result.ExitCode -ne 0) "Packaging succeeded with a version.pdb that has another ${change}:`n$($result.Output)"
        Assert-True ((Get-OneLine $result.Output) -match "does not belong") "The message should say the PDB does not belong to the DLL:`n$($result.Output)"
        Assert-True ((Get-OneLine $result.Output) -match "PDB of $change|PDB with $change") "The message should name the $change that differs:`n$($result.Output)"
        $produced = @(Get-ChildItem (Join-Path $root "build\release") -File -Filter "FS25_VR_v*" -ErrorAction SilentlyContinue)
        Assert-True ($produced.Count -eq 0) "Nothing should be packaged, but found: $($produced.Name -join ', ')"
    }
}

Test-Case "Packaging refuses a version.pdb that is not a PDB the linker writes, and says so" {
    param($root)
    [IO.File]::WriteAllBytes((Join-Path $root "build\version.pdb"), [Text.Encoding]::ASCII.GetBytes("this is not a PDB, only text of the same name"))
    $result = Invoke-Packaging $root
    Assert-True ($result.ExitCode -ne 0) "Packaging succeeded with a version.pdb that is not a PDB:`n$($result.Output)"
    Assert-True ((Get-OneLine $result.Output) -match "not an MSF 7\.00 PDB") "The message should say what is wrong with the file:`n$($result.Output)"
}

foreach ($change in "GUID", "age") {
    Test-Case "The symbols check rejects a release whose Injector was linked with a PDB of another $change than the symbols zip holds" {
        param($root)
        $packaging = Invoke-Packaging $root
        Assert-True ($packaging.ExitCode -eq 0) "Packaging exited with $($packaging.ExitCode):`n$($packaging.Output)"

        $shipped = Get-ChildItem (Join-Path $root "build\release") -Directory -Filter "FS25_VR_v*" | ForEach-Object { Join-Path $_.FullName "x64\version.dll" }
        Set-DllPdbIdentity $shipped $change
        $check = Invoke-SymbolsCheck $root
        Assert-True ($check.ExitCode -ne 0) "The check accepted a symbols zip whose PDB has another $change than the shipped Injector."
        Assert-True ((Get-OneLine $check.Output) -match "does not belong") "The message should say the PDB does not belong to the Injector:`n$($check.Output)"
    }
}

if ($failures.Count -gt 0) {
    Write-Error "$($failures.Count) release packaging test(s) failed: $($failures -join '; ')"
}
Write-Host "All release packaging tests passed."
