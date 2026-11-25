$ErrorActionPreference = "Stop"

if ($env:VSCMD_ARG_TGT_ARCH -eq "x64" -and (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    Write-Host "Already in the MSVC x64 developer environment."
    return
}

$installer = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer"
$vswhere = Join-Path $installer "vswhere.exe"
if (-not (Test-Path $vswhere)) { throw "vswhere.exe not found in $installer. Install Visual Studio or the Build Tools with the C++ x64 tools." }
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw "No Visual Studio with the x64 C++ tools (workload 'Desktop development with C++')." }
$vcvars = Join-Path $vs "VC\Auxiliary\Build\vcvars64.bat"

$env:PATH += ";$installer"
$before = @{}
Get-ChildItem env: | ForEach-Object { $before[$_.Name] = $_.Value }
$after = cmd /c "`"$vcvars`" >nul && set"
if ($LASTEXITCODE) { throw "vcvars64.bat failed ($LASTEXITCODE)" }

$changed = 0
foreach ($line in $after) {
    if ($line -match '^([^=]+)=(.*)$' -and $before[$Matches[1]] -ne $Matches[2]) {
        Set-Item -Path "env:$($Matches[1])" -Value $Matches[2]
        if ($env:GITHUB_ENV) {
            "$($Matches[1])=$($Matches[2])" | Out-File $env:GITHUB_ENV -Append -Encoding utf8
        }
        $changed++
    }
}
Write-Host "Entered the MSVC x64 developer environment from $vs ($changed variables set)."
