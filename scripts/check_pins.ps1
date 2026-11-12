# Fails when anything the build fetches or CI runs is not pinned to fixed content (spec #280):
#   - every FetchContent_Declare in a CMakeLists.txt or *.cmake file fetches either GIT_TAG <full
#     40-hex commit hash> or a URL with a URL_HASH, with the readable version in a comment beside it
#     (GIT_TAG c2e7...e998c # 0.662). A hash does not work with GIT_SHALLOW (CMake can only shallow
#     clone a branch or tag), so pinned git dependencies are cloned in full;
#   - every Action a workflow uses (other than a local ./ one) is owner/repo@<full commit SHA> with
#     its version in a comment (uses: actions/checkout@3d3c...0b1 # v7);
#   - the workflows install Inno Setup with choco at a stated --version.
# Each problem is reported with its file and line. scripts\test_check_pins.ps1 is this script's own check.
param(
    # The repository root to check (a scratch copy in the test).
    [string]$Root = (Split-Path -Parent $PSScriptRoot)
)
$ErrorActionPreference = "Stop"

$problems = New-Object System.Collections.Generic.List[string]
$sha = '[0-9a-f]{40}'

function Get-RelativePath([string]$path) {
    $path.Substring($Root.TrimEnd('\', '/').Length).TrimStart('\', '/')
}

# Splits a line into its code and its comment (the text after the first #, trimmed; "" when none).
function Split-Comment([string]$line, [string]$marker) {
    $index = $line.IndexOf($marker)
    if ($index -lt 0) { return @($line, "") }
    @($line.Substring(0, $index), $line.Substring($index + 1).Trim())
}

# CMake files, skipping build trees and fetched sources.
function Get-CMakeFiles([string]$dir) {
    foreach ($item in Get-ChildItem -LiteralPath $dir -Force) {
        if ($item.PSIsContainer) {
            if ($item.Name -match '^(build.*|\.deps|_deps|\.git|\.claude|out)$') { continue }
            Get-CMakeFiles $item.FullName
        } elseif ($item.Name -eq "CMakeLists.txt" -or $item.Name -like "*.cmake") {
            $item
        }
    }
}

# --- Fetched dependencies -------------------------------------------------------------------------
function Test-Declaration($file, [int]$line, [string]$name, [hashtable]$keys) {
    $where = "$(Get-RelativePath $file):$line"
    if ($keys.ContainsKey("GIT_REPOSITORY")) {
        $tag = $keys["GIT_TAG"]
        if (-not $tag) {
            $problems.Add("$where the dependency '$name' has no GIT_TAG; pin it to a full commit hash.")
        } elseif ($tag.Value -cnotmatch "^$sha$") {
            $problems.Add("$where the dependency '$name' is fetched at GIT_TAG $($tag.Value); pin it to the full 40-character commit hash, with the version in a comment beside it.")
        } elseif (-not $tag.Comment) {
            $problems.Add("$where the dependency '$name' is pinned to $($tag.Value) with no readable version beside it; add it in a comment (GIT_TAG <hash> # <version>).")
        }
    } elseif ($keys.ContainsKey("URL")) {
        $hash = $keys["URL_HASH"]
        if (-not $hash -or $hash.Value -notmatch '^(SHA256|SHA384|SHA512|SHA3_256|SHA3_384|SHA3_512)=[0-9a-fA-F]+$') {
            $problems.Add("$where the dependency '$name' is fetched from $($keys["URL"].Value) without a content hash; add URL_HASH SHA256=<hash>.")
        } elseif (-not ($keys["URL"].Comment -or $hash.Comment)) {
            $problems.Add("$where the dependency '$name' has no readable version beside its URL_HASH; add it in a comment.")
        }
    } else {
        $problems.Add("$where the dependency '$name' is fetched neither from GIT_REPOSITORY at a commit hash nor from a URL with a URL_HASH.")
    }
}

foreach ($file in Get-CMakeFiles $Root) {
    $lines = @(Get-Content -LiteralPath $file.FullName)
    $inBlock = $false
    for ($i = 0; $i -lt $lines.Count; $i++) {
        $code, $comment = Split-Comment $lines[$i] '#'
        if (-not $inBlock) {
            if ($code -notmatch 'FetchContent_Declare\s*\((.*)$') { continue }
            $inBlock = $true
            $start = $i + 1
            $name = $null
            $keys = @{}
            $code = $Matches[1]
        }
        $closes = $code.Contains(')')
        $tokens = @(($code -replace '\).*$', '').Trim() -split '\s+' | Where-Object { $_ })
        for ($t = 0; $t -lt $tokens.Count; $t++) {
            if (-not $name) { $name = $tokens[$t]; continue }
            if ($tokens[$t] -cmatch '^[A-Z][A-Z0-9_]+$' -and $t + 1 -lt $tokens.Count) {
                $keys[$tokens[$t]] = [pscustomobject]@{ Value = $tokens[$t + 1]; Comment = $comment }
                $t++
            }
        }
        if ($closes) {
            $inBlock = $false
            Test-Declaration $file.FullName $start $name $keys
        }
    }
}

# --- Workflows: Actions and Inno Setup ------------------------------------------------------------
$workflowDir = Join-Path $Root ".github\workflows"
$workflows = @(Get-ChildItem -LiteralPath $workflowDir -File -ErrorAction SilentlyContinue |
    Where-Object { $_.Extension -in ".yml", ".yaml" })
$innoInstalls = 0
foreach ($file in $workflows) {
    $lines = @(Get-Content -LiteralPath $file.FullName)
    for ($i = 0; $i -lt $lines.Count; $i++) {
        $where = "$(Get-RelativePath $file.FullName):$($i + 1)"
        $code, $comment = Split-Comment $lines[$i] ' #'
        if ($code -match '^\s*(-\s+)?uses:\s*["'']?([^"''\s]+)') {
            $reference = $Matches[2]
            if ($reference.StartsWith("./")) { continue }
            if ($reference -cnotmatch "^[^@]+@$sha$") {
                $problems.Add("$where the Action $reference is not referenced by a full commit SHA; use owner/repo@<40-character SHA> # <version>.")
            } elseif (-not $comment) {
                $problems.Add("$where the Action $reference has no readable version beside it; add it in a comment (# v1.2.3).")
            }
        }
        if ($code -match '\bchoco(\.exe)?\s+(install|upgrade)\b.*\binnosetup\b') {
            $innoInstalls++
            if ($code -notmatch '(--version[ =]|--version=)["'']?\d+(\.\d+)+') {
                $problems.Add("$where innosetup is installed without a stated version; add --version=<x.y.z>.")
            }
        }
    }
}
if ($innoInstalls -eq 0) {
    $problems.Add("No workflow installs Inno Setup at a stated version (choco install innosetup --version=<x.y.z>).")
}

if ($problems.Count -gt 0) {
    # One line per problem on stdout, so no console width wraps a file name or a reference.
    Write-Host "Not pinned:"
    foreach ($problem in $problems) { Write-Host "  $problem" }
    exit 1
}
Write-Host "Pins OK: every fetched dependency, Action and the Inno Setup install are pinned."
