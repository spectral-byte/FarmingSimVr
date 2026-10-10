# Verifies that the built Injector does not import the Microsoft C/C++ runtime DLLs
# (VCRUNTIME140*.dll, MSVCP140*.dll, CONCRT140.dll, api-ms-win-crt-*.dll), i.e. that it is built
# against the static runtime and needs no VC++ redistributable on the player's machine.
# Reads the PE import tables (regular and delay-load) directly, so it needs no
# Visual Studio tools and checks the built artifact rather than a CMake flag.
param(
    [string]$Dll = "build\dxgi.dll"
)
$ErrorActionPreference = "Stop"

if (-not (Test-Path $Dll)) {
    Write-Error "$Dll not found. Build the Injector first: cmake --build build --target dxgi"
}

$bytes = [System.IO.File]::ReadAllBytes((Resolve-Path $Dll))

function Read-U16([int]$offset) { [BitConverter]::ToUInt16($bytes, $offset) }
function Read-U32([int]$offset) { [BitConverter]::ToUInt32($bytes, $offset) }

if ((Read-U16 0) -ne 0x5A4D) { Write-Error "$Dll is not a PE file (missing MZ header)." }
$peOffset = Read-U32 0x3C
if ((Read-U32 $peOffset) -ne 0x00004550) { Write-Error "$Dll is not a PE file (missing PE signature)." }

$coff = $peOffset + 4
$sectionCount = Read-U16 ($coff + 2)
$optionalSize = Read-U16 ($coff + 16)
$optional = $coff + 20
if ((Read-U16 $optional) -ne 0x20B) { Write-Error "$Dll is not a 64-bit (PE32+) image." }

$sections = foreach ($i in 0..($sectionCount - 1)) {
    $s = $optional + $optionalSize + 40 * $i
    [pscustomobject]@{
        VirtualAddress = Read-U32 ($s + 12)
        VirtualSize    = Read-U32 ($s + 8)
        RawSize        = Read-U32 ($s + 16)
        RawPointer     = Read-U32 ($s + 20)
    }
}

function ConvertTo-FileOffset([uint32]$rva) {
    foreach ($s in $sections) {
        $size = [Math]::Max($s.VirtualSize, $s.RawSize)
        if ($rva -ge $s.VirtualAddress -and $rva -lt $s.VirtualAddress + $size) {
            return [int]($rva - $s.VirtualAddress + $s.RawPointer)
        }
    }
    Write-Error "RVA 0x$($rva.ToString('X')) is outside every section of $Dll."
}

function Read-AsciiZ([int]$offset) {
    $end = $offset
    while ($bytes[$end] -ne 0) { $end++ }
    [System.Text.Encoding]::ASCII.GetString($bytes, $offset, $end - $offset)
}

# Data directory 1 = import table (20-byte descriptors, name RVA at +12);
# data directory 13 = delay-load import table (32-byte descriptors, name RVA at +4).
function Get-ImportedDlls([int]$directoryIndex, [int]$descriptorSize, [int]$nameFieldOffset) {
    $dir = $optional + 112 + 8 * $directoryIndex
    $tableRva = Read-U32 $dir
    if ($tableRva -eq 0) { return }
    $offset = ConvertTo-FileOffset $tableRva
    while ($true) {
        $isTerminator = $true
        foreach ($b in 0..($descriptorSize - 1)) {
            if ($bytes[$offset + $b] -ne 0) { $isTerminator = $false; break }
        }
        if ($isTerminator) { break }
        Read-AsciiZ (ConvertTo-FileOffset (Read-U32 ($offset + $nameFieldOffset)))
        $offset += $descriptorSize
    }
}

$imports = @(Get-ImportedDlls 1 20 12) + @(Get-ImportedDlls 13 32 4) |
    Sort-Object -Unique

Write-Host "$Dll imports:"
foreach ($name in $imports) { Write-Host "  $name" }

$forbidden = @($imports | Where-Object {
    # Every redistributable DLL: VCRUNTIME140_1/_THREADS, MSVCP140_1/_2/_atomic_wait/_codecvt_ids, ...
    $_ -match '^(VCRUNTIME140(_\w+)?|MSVCP140(_\w+)?|CONCRT140|api-ms-win-crt-.+)\.dll$'
})
if ($forbidden.Count -gt 0) {
    Write-Error ("$Dll imports the C/C++ runtime DLLs: " + ($forbidden -join ", ") +
        ". Build the Injector against the static runtime (/MT)," +
        " otherwise players need the VC++ redistributable.")
}
Write-Host "Injector import check OK: no VC++ runtime DLL imports."
