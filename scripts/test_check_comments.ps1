param(
    [string]$Filter = ""
)
$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$script = Join-Path $PSScriptRoot "check_comments.ps1"
$scratch = Join-Path ([IO.Path]::GetTempPath()) ("fs25vr-comments-test-" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $scratch -Force | Out-Null

$failures = New-Object System.Collections.Generic.List[string]
$ran = 0

function New-ScratchRepo([hashtable]$files, [string[]]$baseline = @()) {
    $root = Join-Path $scratch ([Guid]::NewGuid().ToString("N"))
    New-Item -ItemType Directory -Path (Join-Path $root "scripts") -Force | Out-Null
    git -C $root init --quiet
    foreach ($name in $files.Keys) { Write-ScratchFile $root $name $files[$name] }
    if ($null -ne $baseline) {
        [IO.File]::WriteAllText((Join-Path $root "scripts\comment_baseline.txt"), (($baseline -join "`n") + "`n"))
    }
    $root
}

function Write-ScratchFile([string]$root, [string]$name, [string]$text) {
    $path = Join-Path $root $name
    New-Item -ItemType Directory -Path (Split-Path -Parent $path) -Force | Out-Null
    [IO.File]::WriteAllText($path, $text)
}

function Read-Baseline([string]$root) {
    @([IO.File]::ReadAllLines((Join-Path $root "scripts\comment_baseline.txt")) | Where-Object { $_ })
}

function Invoke-CommentCheck([string]$root, [switch]$Update) {
    $stdout = Join-Path $scratch ([Guid]::NewGuid().ToString("N") + ".stdout")
    $stderr = "$stdout.stderr"
    $shell = (Get-Process -Id $PID).Path
    $arguments = @("-NoProfile", "-NonInteractive", "-File", "`"$script`"", "-Root", "`"$root`"")
    if ($Update) { $arguments += "-Update" }
    $process = Start-Process -FilePath $shell -PassThru -Wait -NoNewWindow -ArgumentList $arguments `
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
        if (-not $result.Message.Contains($text)) { throw "expected the message to mention '$text', got: $($result.Message)" }
    }
}

function Assert-Equal($actual, $expected) {
    if ("$actual" -cne "$expected") { throw "expected '$expected', got '$actual'" }
}

function Assert-CountedAs([int]$count, [string]$name, [string]$text) {
    $root = New-ScratchRepo @{ $name = $text } $null
    Assert-Passed (Invoke-CommentCheck $root -Update)
    $expected = if ($count -eq 0) { "" } else { "$count $name" }
    Assert-Equal ((Read-Baseline $root) -join ";") $expected
}

function Test-Case([string]$name, [scriptblock]$body) {
    if ($Filter -and $name -notlike $Filter) { return }
    $script:ran++
    try {
        & $body
        Write-Host "PASS $name"
    } catch {
        $failures.Add($name)
        Write-Host "FAIL $name`n     $($_.Exception.Message)"
    }
}

$hash40 = "0123456789abcdef0123456789abcdef01234567"

try {
    Test-Case "the repository is at its baseline" {
        Assert-Passed (Invoke-CommentCheck $repoRoot)
    }

    Test-Case "a comment in a file without a baseline line is rejected, with its place and text" {
        $root = New-ScratchRepo @{ "src/a.cpp" = "int a;`n// why`nint b;`n" }
        Assert-Rejected (Invoke-CommentCheck $root) @("src/a.cpp has 1 comment lines, 0 allowed", "src/a.cpp:2 // why", "0041")
    }

    Test-Case "a file at its baseline passes" {
        $root = New-ScratchRepo @{ "src/a.cpp" = "int a; // one`n// two`n" } @("2 src/a.cpp")
        Assert-Passed (Invoke-CommentCheck $root)
    }

    Test-Case "a file over its baseline is rejected" {
        $root = New-ScratchRepo @{ "src/a.cpp" = "int a; // one`n// two`n" } @("1 src/a.cpp")
        Assert-Rejected (Invoke-CommentCheck $root) @("src/a.cpp has 2 comment lines, 1 allowed")
    }

    Test-Case "fewer comments elsewhere do not pay for a new one" {
        $root = New-ScratchRepo @{ "a.cpp" = "int a; // one`n"; "b.cpp" = "int b;`n" } @("1 b.cpp")
        Assert-Rejected (Invoke-CommentCheck $root) @("a.cpp has 1 comment lines, 0 allowed")
    }

    Test-Case "a file under its baseline is rejected until the baseline comes down" {
        $root = New-ScratchRepo @{ "a.cpp" = "int a; // one`n" } @("3 a.cpp")
        Assert-Rejected (Invoke-CommentCheck $root) @("a.cpp has 1 comment lines, the baseline says 3", "-Update")
        Assert-Passed (Invoke-CommentCheck $root -Update)
        Assert-Equal ((Read-Baseline $root) -join ";") "1 a.cpp"
        Assert-Passed (Invoke-CommentCheck $root)
    }

    Test-Case "a baseline line of a deleted file has to go" {
        $root = New-ScratchRepo @{ "a.cpp" = "int a;`n" } @("2 gone.cpp")
        Assert-Rejected (Invoke-CommentCheck $root) @("gone.cpp has 0 comment lines, the baseline says 2")
        Assert-Passed (Invoke-CommentCheck $root -Update)
        Assert-Equal ((Read-Baseline $root) -join ";") ""
    }

    Test-Case "updating never raises the baseline" {
        $root = New-ScratchRepo @{ "a.cpp" = "// one`n// two`n"; "b.cpp" = "// new`n" } @("1 a.cpp")
        Assert-Rejected (Invoke-CommentCheck $root -Update) @("a.cpp has 2 comment lines, 1 allowed", "b.cpp has 1 comment lines, 0 allowed")
        Assert-Equal ((Read-Baseline $root) -join ";") "1 a.cpp"
    }

    Test-Case "an ignored file is not checked" {
        $root = New-ScratchRepo @{ ".gitignore" = "build/`n"; "build/a.cpp" = "// generated`n" }
        Assert-Passed (Invoke-CommentCheck $root)
    }

    Test-Case "without a baseline the check fails" {
        $root = New-ScratchRepo @{ "a.cpp" = "int a;`n" } $null
        Assert-Rejected (Invoke-CommentCheck $root) @("There is no baseline")
    }

    Test-Case "C++ comments are told from strings, raw strings and digit separators" {
        Assert-CountedAs 4 "a.cpp" "int a = 1'000; // one`n/* two`n   three */ auto s = `"// no`"; char c = '`"'; int b; // four`n"
        Assert-CountedAs 0 "a.h" "const char* s = R`"luau(`n-- no`n// no`n)luau`"; const int n = 0xFF'00; const char* u = `"http://x`";`n"
    }

    Test-Case "Lua comments are told from strings and long strings" {
        Assert-CountedAs 4 "a.lua" "local a = 1 -- one`n--[[ two`nthree ]] local s = `"-- no`"`nlocal t = [==[`n-- no`n]==]`n-- four`n"
        Assert-CountedAs 0 "a.luau" "--!strict`nlocal a = '--'`n"
    }

    Test-Case "PowerShell comments are told from strings and here-strings" {
        Assert-CountedAs 3 "a.ps1" "#Requires -Version 5`n`$a = '# no' # one`n<# two`nthree #>`n`$b = @'`n# no`n'@`n"
    }

    Test-Case "Python comments are told from strings, and noqa is kept" {
        Assert-CountedAs 1 "a.py" "#!/usr/bin/env python`nimport a  # noqa: E402`nb = '# no'  # one`nc = `"`"`"`n# no`n`"`"`"`n"
    }

    Test-Case "YAML comments count, and the version beside a pinned Action does not" {
        Assert-CountedAs 2 "a.yml" "# one`njobs:`n  - uses: a/b@$hash40 # v1`n  - run: echo '# no' # two`n"
    }

    Test-Case "CMake comments count, and the version beside a pinned hash does not" {
        Assert-CountedAs 3 "CMakeLists.txt" "# one`nset(A `"# no`")`n#[[ two`nthree ]]`nFetchContent_Declare(x GIT_TAG $hash40 # 1.0`n)`n"
    }

    Test-Case "Inno Setup comments count in the sections and in the code" {
        Assert-CountedAs 4 "a.iss" "; one`n[Setup]`nAppName=a;b`n[Code]`nconst A = 'http://no'; // two`n{ three }`n(* four *)`nvar B: {#C};`n"
        Assert-CountedAs 1 "b.iss" "function A: Boolean; // one`n"
    }

    Test-Case "module definition and gitignore comments count" {
        Assert-CountedAs 1 "a.def" "EXPORTS`n  A=B ; one`n"
        Assert-CountedAs 1 ".gitignore" "# one`nbuild/#x`n"
    }

    Test-Case "other files are not checked" {
        Assert-CountedAs 0 "README.md" "<!-- note -->`n# Title`n"
    }
} finally {
    Remove-Item -LiteralPath $scratch -Recurse -Force -ErrorAction SilentlyContinue
}

if ($failures.Count -gt 0) {
    Write-Host "$($failures.Count) of $ran cases failed."
    exit 1
}
Write-Host "All $ran comment check cases passed."
