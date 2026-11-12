# Enters the MSVC x64 developer environment: finds the newest Visual Studio (or Build Tools) with the x64 C++
# tools, runs its vcvars64.bat and copies every environment variable it sets or changes into this process,
# so cl, link and the Windows SDK are found by the commands that follow in the same PowerShell.
# Under GitHub Actions it also writes them to $env:GITHUB_ENV, so the job's later steps get them too.
# Usage: scripts\enter_msvc_env.ps1   (then cmake --preset local, ...)
$ErrorActionPreference = "Stop"

# Already in it (a second run, or a Developer PowerShell): nothing to do, and PATH does not grow again.
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

# vcvars64.bat runs vswhere itself, so its folder goes on PATH first.
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
