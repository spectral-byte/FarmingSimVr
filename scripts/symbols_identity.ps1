function Read-FileBytes([IO.FileStream]$stream, [long]$offset, [int]$count) {
    $buffer = New-Object byte[] $count
    $null = $stream.Seek($offset, [IO.SeekOrigin]::Begin)
    $read = 0
    while ($read -lt $count) {
        $n = $stream.Read($buffer, $read, $count - $read)
        if ($n -le 0) { throw "Unexpected end of file reading $count bytes at offset $offset." }
        $read += $n
    }
    ,$buffer
}

# The GUID (as the 16 bytes both files store, in hex) and the age the DLL says its PDB has.
function Get-DllPdbIdentity([string]$dllPath) {
    $stream = [IO.File]::Open($dllPath, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
    try {
        $dos = Read-FileBytes $stream 0 64
        if ($dos[0] -ne 0x4D -or $dos[1] -ne 0x5A) { throw "$dllPath is not a PE file (no MZ header)." }
        $peOffset = [BitConverter]::ToInt32($dos, 0x3C)
        $pe = Read-FileBytes $stream $peOffset 24
        if ($pe[0] -ne 0x50 -or $pe[1] -ne 0x45 -or $pe[2] -ne 0 -or $pe[3] -ne 0) { throw "$dllPath is not a PE file (no PE signature)." }
        $sectionCount = [BitConverter]::ToUInt16($pe, 6)
        $optionalSize = [BitConverter]::ToUInt16($pe, 20)
        $optional = Read-FileBytes $stream ($peOffset + 24) $optionalSize
        $magic = [BitConverter]::ToUInt16($optional, 0)
        $directories = if ($magic -eq 0x20B) { 112 } elseif ($magic -eq 0x10B) { 96 } else { throw "$dllPath has an unknown optional header magic 0x$($magic.ToString('X'))." }
        $debugRva = [BitConverter]::ToUInt32($optional, $directories + 6 * 8)
        $debugSize = [BitConverter]::ToUInt32($optional, $directories + 6 * 8 + 4)
        if ($debugRva -eq 0 -or $debugSize -eq 0) { throw "$dllPath has no debug directory, so it names no PDB." }

        $sections = Read-FileBytes $stream ($peOffset + 24 + $optionalSize) (40 * $sectionCount)
        function ToFileOffset([uint32]$rva) {
            for ($i = 0; $i -lt $sectionCount; $i++) {
                $virtualSize = [BitConverter]::ToUInt32($sections, $i * 40 + 8)
                $virtualAddress = [BitConverter]::ToUInt32($sections, $i * 40 + 12)
                $rawSize = [BitConverter]::ToUInt32($sections, $i * 40 + 16)
                $rawPointer = [BitConverter]::ToUInt32($sections, $i * 40 + 20)
                if ($rva -ge $virtualAddress -and $rva -lt $virtualAddress + [Math]::Max($virtualSize, $rawSize)) { return [long]$rawPointer + ($rva - $virtualAddress) }
            }
            throw "${dllPath}: RVA 0x$($rva.ToString('X')) is in no section."
        }
        $entries = Read-FileBytes $stream (ToFileOffset $debugRva) ([int]$debugSize)
        for ($offset = 0; $offset + 28 -le $entries.Length; $offset += 28) {
            if ([BitConverter]::ToUInt32($entries, $offset + 12) -ne 2) { continue }   # IMAGE_DEBUG_TYPE_CODEVIEW
            $dataSize = [BitConverter]::ToUInt32($entries, $offset + 16)
            $dataPointer = [BitConverter]::ToUInt32($entries, $offset + 24)
            if ($dataSize -lt 24) { continue }
            $codeView = Read-FileBytes $stream $dataPointer ([int]$dataSize)
            if ([Text.Encoding]::ASCII.GetString($codeView, 0, 4) -ne "RSDS") { continue }
            $nameBytes = $codeView[24..($codeView.Length - 1)]
            $nameLength = [Array]::IndexOf($nameBytes, [byte]0)
            return [pscustomobject]@{
                Guid    = [BitConverter]::ToString($codeView, 4, 16) -replace "-", ""
                Age     = [BitConverter]::ToUInt32($codeView, 20)
                PdbPath = [Text.Encoding]::UTF8.GetString($nameBytes, 0, $(if ($nameLength -lt 0) { $nameBytes.Length } else { $nameLength }))
                Offset  = $dataPointer   # where the RSDS record sits in the file: signature, then GUID, then age
            }
        }
        throw "$dllPath has no CodeView (RSDS) debug record, so it names no PDB."
    } finally { $stream.Dispose() }
}

function Get-PdbIdentity([string]$pdbPath) {
    $stream = [IO.File]::Open($pdbPath, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
    try {
        $magic = [Text.Encoding]::ASCII.GetBytes("Microsoft C/C++ MSF 7.00`r`n") + [byte[]](0x1A, 0x44, 0x53, 0, 0, 0)
        $super = Read-FileBytes $stream 0 ([int][Math]::Min($stream.Length, 56))
        if ($super.Length -lt 56 -or -not [Linq.Enumerable]::SequenceEqual([byte[]]$super[0..31], [byte[]]$magic)) {
            throw "$pdbPath is not an MSF 7.00 PDB (the format MSVC's linker writes), so it cannot be matched to a DLL."
        }
        $blockSize = [BitConverter]::ToUInt32($super, 32)
        $directoryBytes = [BitConverter]::ToUInt32($super, 44)
        $blockMapAddress = [BitConverter]::ToUInt32($super, 52)
        if ($blockSize -lt 512 -or ($blockSize -band ($blockSize - 1)) -ne 0) { throw "$pdbPath has an invalid block size $blockSize." }

        $directoryBlockCount = [int][Math]::Ceiling($directoryBytes / [double]$blockSize)
        $blockMap = Read-FileBytes $stream ([long]$blockMapAddress * $blockSize) (4 * $directoryBlockCount)
        $directory = New-Object byte[] ($directoryBlockCount * $blockSize)
        for ($i = 0; $i -lt $directoryBlockCount; $i++) {
            $block = Read-FileBytes $stream ([long][BitConverter]::ToUInt32($blockMap, 4 * $i) * $blockSize) $blockSize
            [Array]::Copy($block, 0, $directory, $i * $blockSize, $blockSize)
        }
        $streamCount = [BitConverter]::ToUInt32($directory, 0)
        if ($streamCount -lt 2) { throw "$pdbPath has no PDB info stream." }
        $sizes = for ($i = 0; $i -lt 2; $i++) { [BitConverter]::ToUInt32($directory, 4 + 4 * $i) }
        $skipBlocks = if ($sizes[0] -eq [uint32]::MaxValue) { 0 } else { [int][Math]::Ceiling($sizes[0] / [double]$blockSize) }
        $firstBlockOfStream1 = [BitConverter]::ToUInt32($directory, 4 + 4 * $streamCount + 4 * $skipBlocks)
        if ($sizes[1] -lt 28) { throw "$pdbPath has a PDB info stream of only $($sizes[1]) bytes." }
        $info = Read-FileBytes $stream ([long]$firstBlockOfStream1 * $blockSize) 28
        return [pscustomobject]@{
            Guid = [BitConverter]::ToString($info, 12, 16) -replace "-", ""
            Age  = [BitConverter]::ToUInt32($info, 8)
        }
    } finally { $stream.Dispose() }
}

function Assert-PdbBelongsToDll([string]$pdbPath, [string]$dllPath) {
    $wanted = Get-DllPdbIdentity $dllPath
    $found = Get-PdbIdentity $pdbPath
    if ($wanted.Guid -ne $found.Guid) {
        Write-Error "$pdbPath does not belong to ${dllPath}: the DLL was linked with a PDB with GUID $($wanted.Guid), this one has GUID $($found.Guid). A crash dump from this DLL could not be read with it."
    }
    if ($wanted.Age -ne $found.Age) {
        Write-Error "$pdbPath does not belong to ${dllPath}: the DLL was linked with a PDB of age $($wanted.Age), this one has age $($found.Age). A crash dump from this DLL could not be read with it."
    }
}
