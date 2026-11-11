# Runs scripts\check_release_tag.ps1 against scratch modDesc files and inspects its exit code, its
# message and the outputs it hands to the release workflow (the file named by GITHUB_OUTPUT). Needs no
# build. Fails (non-zero exit) if any assertion fails. No Pester.
#
# Extending: add a Test-Case at the bottom. Invoke-TagCheck runs the script in a child shell.
param(
    # Only run the cases whose name matches this wildcard (e.g. "*pre-release*"); all cases when empty.
    [string]$Filter = ""
)
$ErrorActionPreference = "Stop"

$script = Join-Path $PSScriptRoot "check_release_tag.ps1"
$scratch = Join-Path ([IO.Path]::GetTempPath()) ("fs25vr-tag-test-" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $scratch -Force | Out-Null

$failures = New-Object System.Collections.Generic.List[string]

# Writes a modDesc.xml holding this version (or no <version> element when it is $null) and returns its path.
function New-ModDesc([AllowNull()][string]$version) {
    $path = Join-Path $scratch ([Guid]::NewGuid().ToString("N") + "-modDesc.xml")
    $versionElement = if ($null -ne $version) { "<version>$version</version>" } else { "" }
    Set-Content -Path $path -Value "<?xml version=`"1.0`" encoding=`"utf-8`" standalone=`"no`"?><modDesc descVersion=`"96`">$versionElement</modDesc>" -Encoding utf8
    $path
}

# Runs the check in a child shell. Outputs is the hashtable of key=value lines it wrote to GITHUB_OUTPUT.
function Invoke-TagCheck([string]$tag, [string]$modDescPath) {
    $outputFile = Join-Path $scratch ([Guid]::NewGuid().ToString("N") + ".out")
    $stdout = "$outputFile.stdout"
    $stderr = "$outputFile.stderr"
    $shell = (Get-Process -Id $PID).Path
    $env:GITHUB_OUTPUT = $outputFile
    try {
        $process = Start-Process -FilePath $shell -PassThru -Wait -NoNewWindow `
            -ArgumentList @("-NoProfile", "-NonInteractive", "-File", "`"$script`"", "-Tag", "`"$tag`"", "-ModDescPath", "`"$modDescPath`"") `
            -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    } finally {
        Remove-Item Env:\GITHUB_OUTPUT
    }
    $outputs = @{}
    if (Test-Path $outputFile) {
        foreach ($line in Get-Content $outputFile) {
            if ($line -match '^([^=]+)=(.*)$') { $outputs[$Matches[1]] = $Matches[2] }
        }
    }
    [pscustomobject]@{
        ExitCode = $process.ExitCode
        Message  = ((Get-Content $stdout -Raw) + (Get-Content $stderr -Raw))
        Outputs  = $outputs
    }
}

function Assert-Passed($result) {
    if ($result.ExitCode -ne 0) { throw "expected the check to pass, it exited $($result.ExitCode): $($result.Message)" }
}

function Assert-Rejected($result, [string[]]$mentioning) {
    if ($result.ExitCode -eq 0) { throw "expected the check to fail, it passed" }
    foreach ($text in $mentioning) {
        if ($result.Message -notlike "*$text*") { throw "expected the message to mention '$text', got: $($result.Message)" }
    }
    # The workflow must not go on to build from anything a failed check left behind.
    if ($result.Outputs.Count -ne 0) { throw "a failed check must write no outputs, it wrote: $($result.Outputs.Keys -join ', ')" }
}

function Assert-Output($result, [string]$name, [string]$expected) {
    $actual = $result.Outputs[$name]
    if ($actual -ne $expected) { throw "output '$name' is '$actual', expected '$expected'" }
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

try {
    Test-Case "a tag equal to the mod version without its last .0 passes" {
        $r = Invoke-TagCheck "v1.2.0" (New-ModDesc "1.2.0.0")
        Assert-Passed $r
        Assert-Output $r "version" "1.2.0"
        Assert-Output $r "mod_version" "1.2.0.0"
        Assert-Output $r "prerelease" "false"
    }

    Test-Case "a tag with a pre-release suffix passes and marks a pre-release" {
        $r = Invoke-TagCheck "v1.2.0-pre.1" (New-ModDesc "1.2.0.0")
        Assert-Passed $r
        Assert-Output $r "version" "1.2.0"
        Assert-Output $r "mod_version" "1.2.0.0"
        Assert-Output $r "prerelease" "true"
    }

    Test-Case "a tag whose version differs from the mod version fails and names both" {
        $r = Invoke-TagCheck "v1.3.0" (New-ModDesc "1.2.0.0")
        Assert-Rejected $r @("v1.3.0", "1.2.0.0", "1.2.0")
    }

    Test-Case "a pre-release suffix does not hide a version mismatch" {
        $r = Invoke-TagCheck "v1.3.0-pre.1" (New-ModDesc "1.2.0.0")
        Assert-Rejected $r @("1.3.0", "1.2.0.0")
    }

    Test-Case "a mod version that does not end in .0 cannot be released" {
        $r = Invoke-TagCheck "v1.2.0" (New-ModDesc "1.2.0.1")
        Assert-Rejected $r @("1.2.0.1")
    }

    Test-Case "a mod version that is not four numbers fails" {
        $r = Invoke-TagCheck "v1.2.0" (New-ModDesc "1.2.0")
        Assert-Rejected $r @("1.2.0")
    }

    Test-Case "a modDesc without a version fails" {
        $r = Invoke-TagCheck "v1.2.0" (New-ModDesc $null)
        Assert-Rejected $r @("version")
    }

    foreach ($tag in "1.2.0", "v1.2", "v1.2.0.0", "v1.2.0-", "v1.2.0-Pre", "vX.Y.Z", "latest", "v01.2.0") {
        Test-Case "the tag '$tag' is not a version tag" {
            $r = Invoke-TagCheck $tag (New-ModDesc "1.2.0.0")
            Assert-Rejected $r @($tag)
        }
    }
} finally {
    Remove-Item $scratch -Recurse -Force -ErrorAction SilentlyContinue
}

if ($failures.Count -gt 0) {
    Write-Host "$($failures.Count) test(s) failed."
    exit 1
}
Write-Host "All tests passed."
