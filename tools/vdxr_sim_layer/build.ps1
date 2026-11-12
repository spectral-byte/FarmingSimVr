# Builds XrApiLayer_vdxr_sim.dll next to this script with the MSVC x64 toolchain.
$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
$repo = Resolve-Path "$here\..\.."
$vs = 'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools'
Import-Module "$vs\Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' *> $null

Push-Location $here
try {
    cl /nologo /LD /EHsc /O2 /MT /W4 /utf-8 `
        /I "$repo\build\_deps\openxr-src\include" /I "$repo\build\_deps\openxr-build\include" `
        vdxr_sim_layer.cpp /Fe:XrApiLayer_vdxr_sim.dll /link dxgi.lib
    if ($LASTEXITCODE -ne 0) { throw "cl failed with exit code $LASTEXITCODE" }
    cl /nologo /EHsc /O2 /MT /W4 /utf-8 harness.cpp /Fe:harness.exe
    if ($LASTEXITCODE -ne 0) { throw "cl failed for harness.cpp with exit code $LASTEXITCODE" }
} finally {
    Pop-Location
}
