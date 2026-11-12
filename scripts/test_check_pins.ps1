# Runs scripts\check_pins.ps1 against the repository and against scratch copies of its CMakeLists.txt
# and workflows with one pin broken, and inspects its exit code and message. Needs no build. Fails
# (non-zero exit) if any assertion fails. No Pester.
#
# Extending: add a Test-Case at the bottom. New-ScratchRepo copies what the check reads; Edit-File
# breaks one pin in the copy; Invoke-PinCheck runs the script in a child shell.
param(
    # Only run the cases whose name matches this wildcard (e.g. "*Action*"); all cases when empty.
    [string]$Filter = ""
)
$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$script = Join-Path $PSScriptRoot "check_pins.ps1"
$scratch = Join-Path ([IO.Path]::GetTempPath()) ("fs25vr-pins-test-" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $scratch -Force | Out-Null

$failures = New-Object System.Collections.Generic.List[string]

# Copies the files the check reads (CMakeLists.txt and the workflows) into a new scratch root.
function New-ScratchRepo {
    $root = Join-Path $scratch ([Guid]::NewGuid().ToString("N"))
    New-Item -ItemType Directory -Path (Join-Path $root ".github\workflows") -Force | Out-Null
    Copy-Item (Join-Path $repoRoot "CMakeLists.txt") $root
    Copy-Item (Join-Path $repoRoot ".github\workflows\*.yml") (Join-Path $root ".github\workflows")
    $root
}

# Replaces the first match of a regex in a scratch file; throws when it does not match, so a case
# cannot pass because the repository changed shape under it.
function Edit-File([string]$path, [string]$pattern, [string]$replacement) {
    $text = Get-Content $path -Raw
    $regex = [regex]::new($pattern, [Text.RegularExpressions.RegexOptions]::Multiline)
    if (-not $regex.IsMatch($text)) { throw "the pattern '$pattern' is not in $path" }
    Set-Content -Path $path -Value $regex.Replace($text, $replacement, 1) -NoNewline -Encoding utf8
}

function Invoke-PinCheck([string]$root) {
    $stdout = Join-Path $scratch ([Guid]::NewGuid().ToString("N") + ".stdout")
    $stderr = "$stdout.stderr"
    $shell = (Get-Process -Id $PID).Path
    $process = Start-Process -FilePath $shell -PassThru -Wait -NoNewWindow `
        -ArgumentList @("-NoProfile", "-NonInteractive", "-File", "`"$script`"", "-Root", "`"$root`"") `
        -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    [pscustomobject]@{
        ExitCode = $process.ExitCode
        Message  = ((Get-Content $stdout -Raw) + (Get-Content $stderr -Raw))
    }
}

function Assert-Passed($result) {
    if ($result.ExitCode -ne 0) { throw "expected the check to pass, it exited $($result.ExitCode): $($result.Message)" }
}

function Assert-Rejected($result, [string[]]$mentioning) {
    if ($result.ExitCode -eq 0) { throw "expected the check to fail, it passed: $($result.Message)" }
    foreach ($text in $mentioning) {
        if ($result.Message -notlike "*$text*") { throw "expected the message to mention '$text', got: $($result.Message)" }
    }
}

function Test-Case([string]$name, [scriptblock]$body) {
    if ($Filter -and $name -notlike $Filter) { return }
    try {
        & $body
        Write-Host "PASS  $name"
    } catch {
        Write-Host "FAIL  $name -- $($_.Exception.Message)"
        $failures.Add($name)
    }
}

$cmake = "CMakeLists.txt"
$buildAndTest = ".github\workflows\build-and-test.yml"

try {
    Test-Case "the repository is pinned" {
        Assert-Passed (Invoke-PinCheck $repoRoot)
    }

    Test-Case "an unchanged scratch copy is pinned" {
        Assert-Passed (Invoke-PinCheck (New-ScratchRepo))
    }

    Test-Case "a dependency fetched by tag fails and is named" {
        $root = New-ScratchRepo
        Edit-File (Join-Path $root $cmake) '(?<=minhook\.git\s+GIT_TAG )[0-9a-f]{40}' 'v1.3.4'
        Assert-Rejected (Invoke-PinCheck $root) @("minhook", "v1.3.4")
    }

    Test-Case "a dependency fetched by a short commit hash fails" {
        $root = New-ScratchRepo
        Edit-File (Join-Path $root $cmake) '(?<=googletest\.git\s+GIT_TAG )([0-9a-f]{7})[0-9a-f]{33}' '$1'
        Assert-Rejected (Invoke-PinCheck $root) @("googletest")
    }

    Test-Case "a dependency pinned by hash without its readable version fails" {
        $root = New-ScratchRepo
        Edit-File (Join-Path $root $cmake) '(?<=OpenXR-SDK\.git\s+GIT_TAG [0-9a-f]{40})[ \t]*#[^\r\n]*' ''
        Assert-Rejected (Invoke-PinCheck $root) @("OpenXR", "version")
    }

    Test-Case "a dependency fetched as an archive without a content hash fails" {
        $root = New-ScratchRepo
        Edit-File (Join-Path $root $cmake) 'GIT_REPOSITORY https://github.com/luau-lang/luau\.git\s+GIT_TAG [0-9a-f]{40}[^\r\n]*' 'URL https://github.com/luau-lang/luau/archive/refs/tags/0.662.tar.gz'
        Assert-Rejected (Invoke-PinCheck $root) @("luau")
    }

    Test-Case "a dependency fetched as an archive with a content hash passes" {
        $root = New-ScratchRepo
        Edit-File (Join-Path $root $cmake) 'GIT_REPOSITORY https://github.com/luau-lang/luau\.git\s+GIT_TAG [0-9a-f]{40}[^\r\n]*' ("URL https://github.com/luau-lang/luau/archive/refs/tags/0.662.tar.gz # 0.662`n    URL_HASH SHA256=" + ("ab" * 32))
        Assert-Passed (Invoke-PinCheck $root)
    }

    Test-Case "an Action referenced by tag fails and is named with its file" {
        $root = New-ScratchRepo
        Edit-File (Join-Path $root $buildAndTest) 'actions/cache@[0-9a-f]{40}[^\r\n]*' 'actions/cache@v6'
        Assert-Rejected (Invoke-PinCheck $root) @("actions/cache@v6", "build-and-test.yml")
    }

    Test-Case "an Action referenced by commit without its version fails" {
        $root = New-ScratchRepo
        Edit-File (Join-Path $root ".github\workflows\release.yml") '(?<=actions/download-artifact@[0-9a-f]{40})[ \t]*#[^\r\n]*' ''
        Assert-Rejected (Invoke-PinCheck $root) @("actions/download-artifact", "release.yml", "version")
    }

    Test-Case "an Inno Setup install without a version fails" {
        $root = New-ScratchRepo
        Edit-File (Join-Path $root $buildAndTest) 'choco \w+ innosetup[^\r\n]*' 'choco install innosetup -y --no-progress'
        Assert-Rejected (Invoke-PinCheck $root) @("innosetup", "build-and-test.yml")
    }

    Test-Case "workflows without an Inno Setup install fail" {
        $root = New-ScratchRepo
        Edit-File (Join-Path $root $buildAndTest) 'choco \w+ innosetup[^\r\n]*' 'echo no installer'
        Assert-Rejected (Invoke-PinCheck $root) @("Inno Setup")
    }
} finally {
    Remove-Item $scratch -Recurse -Force -ErrorAction SilentlyContinue
}

if ($failures.Count -gt 0) {
    Write-Host "$($failures.Count) test(s) failed."
    exit 1
}
Write-Host "All tests passed."
