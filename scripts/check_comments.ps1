param(
    [string]$Root = (Split-Path -Parent $PSScriptRoot),
    [string]$Baseline = "",
    [switch]$Update
)
$ErrorActionPreference = "Stop"

if (-not $Baseline) { $Baseline = Join-Path $Root "scripts\comment_baseline.txt" }
$maxLinesShown = 20

function Get-LanguageOf([string]$path) {
    $name = [IO.Path]::GetFileName($path)
    $extension = [IO.Path]::GetExtension($path).ToLowerInvariant()
    if ($name -eq "CMakeLists.txt") { return "cmake" }
    if ($name -eq ".gitignore") { return "gitignore" }
    switch ($extension) {
        ".c" { "cpp" }
        ".cpp" { "cpp" }
        ".h" { "cpp" }
        ".hpp" { "cpp" }
        ".in" { "cpp" }
        ".hlsl" { "cpp" }
        ".lua" { "lua" }
        ".luau" { "lua" }
        ".ps1" { "powershell" }
        ".psm1" { "powershell" }
        ".yml" { "yaml" }
        ".yaml" { "yaml" }
        ".cmake" { "cmake" }
        ".py" { "python" }
        ".iss" { "inno" }
        ".def" { "def" }
        default { $null }
    }
}

$quoted = '"(?:\\[\s\S]|[^"\\\r\n])*"'
$singleQuoted = "'(?:\\[\s\S]|[^'\\\r\n])*'"
$lineCommentOf = { param($marker) "(?<comment>$marker[^\r\n]*)" }

$patterns = @{
    cpp       = 'R"(?<delimiter>[^()\\\s]{0,16})\([\s\S]*?\)\k<delimiter>"' +
                "|$quoted" +
                "|(?<!\b(?:0[xX][0-9a-fA-F']*|\d[\d']*))$singleQuoted" +
                '|(?<comment>//[^\r\n]*|/\*[\s\S]*?(?:\*/|\z))'
    lua       = '(?<comment>--\[(?<level>=*)\[[\s\S]*?(?:\]\k<level>\]|\z)|--[^\r\n]*)' +
                '|\[(?<level>=*)\[[\s\S]*?\]\k<level>\]' +
                "|$quoted|$singleQuoted" + '|`(?:\\[\s\S]|[^`\\])*`'
    python    = '"""[\s\S]*?"""' + "|'''[\s\S]*?'''|$quoted|$singleQuoted|" + (& $lineCommentOf '#')
    cmake     = '(?<comment>#\[(?<level>=*)\[[\s\S]*?(?:\]\k<level>\]|\z)|#[^\r\n]*)' +
                '|\[(?<level>=*)\[[\s\S]*?\]\k<level>\]' +
                '|"(?:\\[\s\S]|[^"\\])*"|\\[\s\S]'
    yaml      = '(?m)(?<=^|[\s\[,({])(?:"[^"\r\n]*"|''[^''\r\n]*'')|(?<=^|\s)(?<comment>#[^\r\n]*)'
    def       = (& $lineCommentOf ';')
    gitignore = '(?m)^(?<comment>#[^\r\n]*)'
    innoHead  = '(?m)^[ \t]*(?<comment>(?:;|//)[^\r\n]*)'
    innoCode  = "'[^'\r\n]*'" + '|\{#[^}]*\}|(?<comment>//[^\r\n]*|\{[^}]*(?:\}|\z)|\(\*[\s\S]*?(?:\*\)|\z))'
}

function Get-RegexComments([string]$text, [string]$pattern, [int]$from = 0) {
    foreach ($match in [regex]::Matches($text.Substring($from), $pattern)) {
        $group = $match.Groups["comment"]
        if ($group.Success) { [pscustomobject]@{ Start = $from + $group.Index; Text = $group.Value } }
    }
}

function Get-Comments([string]$text, [string]$language) {
    if ($language -eq "powershell") {
        $tokens = $null
        $errors = $null
        [System.Management.Automation.Language.Parser]::ParseInput($text, [ref]$tokens, [ref]$errors) | Out-Null
        foreach ($token in $tokens) {
            if ($token.Kind -eq "Comment") {
                [pscustomobject]@{ Start = $token.Extent.StartOffset; Text = $token.Text }
            }
        }
        return
    }
    if ($language -ne "inno") { return Get-RegexComments $text $patterns[$language] }
    $code = [regex]::Match($text, '(?mi)^\[Code\]')
    $hasSections = [regex]::IsMatch($text, '(?m)^\[\w+\]')
    $codeStart = if ($code.Success) { $code.Index } elseif ($hasSections) { $text.Length } else { 0 }
    Get-RegexComments $text.Substring(0, $codeStart) $patterns["innoHead"]
    Get-RegexComments $text $patterns["innoCode"] $codeStart
}

function Test-ReadByTool([string]$language, [string]$comment, [int]$start, [string]$line) {
    if ($start -eq 0 -and $comment.StartsWith("#!")) { return $true }
    switch ($language) {
        "python" { return $comment -match '^#\s*noqa\b' }
        "powershell" { return $comment -match '^#requires\b' }
        "lua" { return $comment -cmatch '^--!(strict|nonstrict|nocheck|native|optimize)\b' }
        "cmake" { return $line -cmatch '\bGIT_TAG\s+[0-9a-f]{40}\s*\)?\s*#' -or $line -cmatch '\bURL(_HASH)?\s+\S+\s*\)?\s*#' }
        "yaml" { return $line -cmatch '\buses:\s*["'']?[^\s@]+@[0-9a-f]{40}["'']?\s+#' }
    }
    $false
}

function Get-CommentLines([string]$path, [string]$language) {
    $text = [IO.File]::ReadAllText($path)
    foreach ($comment in @(Get-Comments $text $language)) {
        $lineStart = if ($comment.Start -eq 0) { 0 } else { $text.LastIndexOf("`n", $comment.Start - 1) + 1 }
        $lineEnd = $text.IndexOf("`n", $comment.Start)
        if ($lineEnd -lt 0) { $lineEnd = $text.Length }
        $line = $text.Substring($lineStart, $lineEnd - $lineStart)
        if (Test-ReadByTool $language $comment.Text $comment.Start $line) { continue }
        $number = [regex]::Matches($text.Substring(0, $comment.Start), "`n").Count + 1
        foreach ($part in $comment.Text -split "`r?`n") {
            [pscustomobject]@{ Line = $number; Text = $part.Trim() }
            $number++
        }
    }
}

$allowed = @{}
$hasBaseline = Test-Path -LiteralPath $Baseline
if ($hasBaseline) {
    foreach ($entry in [IO.File]::ReadAllLines($Baseline)) {
        if (-not $entry.Trim()) { continue }
        if ($entry -notmatch '^(\d+) (.+)$') { throw "$Baseline has a line that is not '<count> <path>': $entry" }
        $allowed[$Matches[2]] = [int]$Matches[1]
    }
} elseif (-not $Update) {
    throw "There is no baseline at $Baseline."
}

$paths = @(git -C $Root -c core.quotepath=off ls-files --cached --others --exclude-standard | Sort-Object -Unique)
if ($LASTEXITCODE -ne 0) { throw "git ls-files failed in $Root." }

$found = @{}
$over = New-Object System.Collections.Generic.List[string]
$under = New-Object System.Collections.Generic.List[string]
foreach ($path in $paths) {
    $language = Get-LanguageOf $path
    $full = Join-Path $Root $path
    if (-not $language -or -not (Test-Path -LiteralPath $full -PathType Leaf)) { continue }
    $lines = @(Get-CommentLines $full $language)
    $limit = if ($allowed.ContainsKey($path)) { $allowed[$path] } else { 0 }
    $found[$path] = $lines.Count
    if ($hasBaseline -and $lines.Count -gt $limit) {
        $over.Add("$path has $($lines.Count) comment lines, $limit allowed:")
        foreach ($line in ($lines | Select-Object -First $maxLinesShown)) { $over.Add("    ${path}:$($line.Line) $($line.Text)") }
        if ($lines.Count -gt $maxLinesShown) { $over.Add("    and $($lines.Count - $maxLinesShown) more") }
    }
}
foreach ($path in ($allowed.Keys | Sort-Object)) {
    $count = if ($found.ContainsKey($path)) { $found[$path] } else { 0 }
    if ($count -lt $allowed[$path]) { $under.Add("$path has $count comment lines, the baseline says $($allowed[$path])") }
}

if ($Update) {
    $entries = New-Object System.Collections.Generic.List[string]
    foreach ($path in $found.Keys) {
        $count = $found[$path]
        if ($hasBaseline) {
            $limit = if ($allowed.ContainsKey($path)) { $allowed[$path] } else { 0 }
            $count = [Math]::Min($count, $limit)
        }
        if ($count -gt 0) { $entries.Add("$count $path") }
    }
    $sorted = [string[]]($entries | Sort-Object { ($_ -split ' ', 2)[1] })
    [IO.File]::WriteAllText($Baseline, (($sorted -join "`n") + "`n"))
    Write-Host "Baseline written: $($entries.Count) files with comments."
    if ($over.Count -eq 0) { exit 0 }
}

if ($over.Count -gt 0) {
    Write-Host "Code carries no comments (docs/adr/0041-code-carries-no-comments.md). Delete these, or move the decision to an ADR:"
    foreach ($entry in $over) { Write-Host "  $entry" }
    exit 1
}
if ($under.Count -gt 0) {
    Write-Host "Comments were removed, so the baseline has to come down. Run scripts\check_comments.ps1 -Update and commit the result:"
    foreach ($entry in $under) { Write-Host "  $entry" }
    exit 1
}
$total = ($found.Values | Measure-Object -Sum).Sum
Write-Host "Comments OK: $total comment lines left in the baseline, none added."
