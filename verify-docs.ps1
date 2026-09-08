<#
Checks that what the documentation SAYS is still true.

    powershell -ExecutionPolicy Bypass -File verify-docs.ps1
    powershell -ExecutionPolicy Bypass -File verify-docs.ps1 -Quiet

WHY THIS EXISTS

An independent hostile review of this repository reached a verdict worth
repeating: "world-class on the engineering; the prose is a grade below the code
it describes." Its diagnosis was specific and correct --

    CORRECTIONS PROPAGATE TO CLAUDE.md AND STOP THERE.

fork.h is the single definition of the build tag, and five separate mechanisms
enforce it: ADR 0006 says so, ci.yml greps for it, build.ps1 takes -ExpectTag,
package.ps1 parses it, and both search the compiled binary for the UTF-16LE
bytes. That is what single-sourcing looks like when somebody means it.

Facts had none of that. So SECURITY.md spent five builds telling security
researchers that unslist.c was unreachable dead code -- the third writing of a
claim this project had already disproven twice -- and the NO_FIX_* witness count
sat at "13" in one paragraph while three other places in the same file said 18.

This script is the missing guard. It checks three things a machine can check,
and deliberately does not pretend to check the fourth.

  1. DERIVED COUNTS. Every number below is computed from the tree, then every
     document is scanned for a figure that contradicts it. This is the ADR 0006
     treatment applied to facts.

  2. RETIRED CLAIMS. Statements already proven false, listed in
     docs/retired-claims.tsv, must not be asserted again. See that file for why
     a retired claim is not the same as a banned phrase.

  3. RELATIVE LINKS. Every relative link in the Markdown must resolve.

  ⚠ NOT CHECKED, and it cannot be: a NOVEL false claim. Nothing here can
    evaluate a sentence nobody has written yet. The defense against that is
    editorial -- write a fact in ONE place -- not mechanical. When this script
    catches something, the right fix is usually to delete the duplicate rather
    than correct it.
#>
param(
    [switch]$Quiet
)

$ErrorActionPreference = "Stop"
$repo = $PSScriptRoot

$script:failures = 0
$script:checks = 0

# 🔴 UTF-8, EXPLICITLY. Get-Content on PowerShell 5.1 decodes a BOM-less file as
# the system ANSI codepage, so every em dash in these documents arrives as
# mojibake -- which corrupts the quoted context in a failure message and, worse,
# can make a pattern miss. Read them as what they are.
function Read-Lines([string]$path) {
    return [System.IO.File]::ReadAllLines($path, [System.Text.Encoding]::UTF8)
}

# True when the match at $index lies inside a double-quoted span on the line.
#
# ⚠ THIS IS WHAT MAKES THE RETIRED-CLAIM CHECK USABLE. Every document that
# retires a claim has to be able to say what the claim WAS: CHANGELOG.md quotes
# it, settings.cpp quotes it inside a correction notice, FORK.md quotes it while
# explaining the history. A check that fired on those would be turned off within
# a week. Quoting a dead claim is how you bury it, not how you assert it.
function Test-InsideQuotes([string]$line, [int]$index) {
    $quotes = 0
    for ($i = 0; $i -lt $index -and $i -lt $line.Length; $i++) {
        if ($line[$i] -eq '"') { $quotes++ }
    }
    return ($quotes % 2) -eq 1
}

function Say([string]$text, [string]$color = "Gray") {
    if (-not $Quiet) { Write-Host $text -ForegroundColor $color }
}
function Fail([string]$what, [string]$detail) {
    $script:failures++
    Write-Host ("  FAIL  " + $what) -ForegroundColor Red
    foreach ($line in ($detail -split "`n")) {
        if ($line.Trim()) { Write-Host ("        " + $line) -ForegroundColor Red }
    }
}
function Pass([string]$what) { Say ("  ok    " + $what) "DarkGreen" }

# The documents this script polices. Source files are included for the retired
# claims only: settings.cpp carried one of them in a comment for four builds.
$docFiles = @()
$docFiles += Get-ChildItem -LiteralPath $repo -Filter "*.md" -File
$docFiles += Get-ChildItem -LiteralPath (Join-Path $repo "docs/adr") -Filter "*.md" -File -ErrorAction SilentlyContinue
$docFiles += Get-ChildItem -LiteralPath (Join-Path $repo ".github") -Filter "*.md" -File -ErrorAction SilentlyContinue
$docFiles += Get-ChildItem -LiteralPath $repo -Filter "README.txt" -File
$docFiles = @($docFiles | Where-Object { $_ })

$sourceFiles = @()
foreach ($pattern in @("*.c", "*.cpp", "*.h")) {
    $sourceFiles += Get-ChildItem -LiteralPath $repo -Filter $pattern -File
}
$sourceFiles += Get-ChildItem -LiteralPath (Join-Path $repo "generic") -Include "*.c","*.h" -File -Recurse -ErrorAction SilentlyContinue
$sourceFiles += Get-ChildItem -LiteralPath (Join-Path $repo "oshw-qt") -Include "*.cpp","*.h" -File -Recurse -ErrorAction SilentlyContinue
$sourceFiles = @($sourceFiles | Where-Object { $_ })

Say ""
Say ("checking " + $docFiles.Count + " document(s) and " + $sourceFiles.Count + " source file(s)")
Say ""

# ---------------------------------------------------------------- 1. counts --
#
# Each fact: a value computed from the tree, and the patterns that assert it in
# prose. The pattern's first capture group is the asserted number; anything that
# matches the pattern and disagrees with the computed value is a failure.
#
# 🔴 ADD A FACT HERE THE MOMENT YOU WRITE A NUMBER INTO A DOCUMENT TWICE. That
# is the entire lesson: the second copy is the one that goes stale.

Say "-- derived counts --"

$facts = @()

# The NO_FIX_* differential matrix. This is the number that sat wrong for five
# builds while the same file quoted it correctly three times elsewhere.
$matrix = Join-Path $repo "test/nofix/nofix-matrix.tsv"
if (Test-Path $matrix) {
    $rows = Get-Content $matrix | Where-Object { $_ -and $_ -notmatch '^\s*#' }
    $withWitness = @($rows | Where-Object { ($_ -split "`t")[1] -and ($_ -split "`t")[1] -ne '-' }).Count
    $facts += @{
        id = "NO_FIX witnesses"
        value = $withWitness
        source = "test/nofix/nofix-matrix.tsv"
        patterns = @(
            '(\d+)\s+have such a witness',
            '(\d+)\s+of\s+32\s+(?:engine\s+)?toggles',
            '(\d+)\s+NO_FIX_\*?\s+witness'
        )
    }
    $facts += @{
        id = "NO_FIX toggles"
        value = $rows.Count
        source = "test/nofix/nofix-matrix.tsv"
        patterns = @('(\d+)\s+`?NO_FIX_\*?`?\s+behavior toggles')
    }
}

# Fuzz targets. This one drifted from six to seven to eight across three files.
$fuzz = @(Get-ChildItem -LiteralPath (Join-Path $repo "test/fuzz") -File |
          Where-Object { $_.Name -like "fuzz_*.c" -or $_.Name -like "fuzz_*.cpp" })
$facts += @{
    id = "fuzz targets"
    value = $fuzz.Count
    source = "test/fuzz/fuzz_*.c[pp]"
    patterns = @('(\d+)\s+targets', 'libFuzzer,\s+\*\*(\w+)\s+targets\*\*')
}

# ADRs.
$adrs = @(Get-ChildItem -LiteralPath (Join-Path $repo "docs/adr") -Filter "0*.md" -File -ErrorAction SilentlyContinue)
$facts += @{
    id = "ADRs"
    value = $adrs.Count
    source = "docs/adr/0*.md"
    patterns = @('(\w+)\s+(?:decisions|ADRs)\b')
}

# The golden-master snapshot's size, straight off the baseline it compares
# against. One row per level per engine.
$snapshot = Join-Path $repo "test/golden/engine-snapshot.tsv"
if (Test-Path $snapshot) {
    $digestRows = @(Get-Content $snapshot | Where-Object { $_ -and $_ -notmatch '^\s*#' })
    $facts += @{
        id = "golden-master digests"
        value = $digestRows.Count
        source = "test/golden/engine-snapshot.tsv"
        patterns = @('([\d,]+)\s+golden-master digests', '([\d,]+)\s+digests over')
    }
}

# The unit suite's totals. 🔴 THE LAST HAND-TYPED NUMBER IN CLAUDE.md, and it
# was wrong: section 5 said 21,008 checks while a full run reported 21,012.
# Nothing was broken by that -- which is the point. A figure nobody can check is
# a figure that drifts quietly until someone reasons from it.
$counts = Join-Path $repo "docs/test-counts.tsv"
if (Test-Path $counts) {
    foreach ($row in (Get-Content $counts | Where-Object { $_ -match '^unit\s' })) {
        $cols = $row -split "`t"
        $facts += @{
            id = "unit runs"
            value = [int]$cols[1]
            source = "docs/test-counts.tsv (written by test\run-tests.ps1)"
            patterns = @('([\d,]+)\s+unit runs')
        }
        $facts += @{
            id = "unit checks"
            value = [int]$cols[2]
            source = "docs/test-counts.tsv (written by test\run-tests.ps1)"
            # ⚠ Anchored to the "unit runs," that precedes it. Written as the
            # obvious '([\d,]+)\s+checks;' this also matched the e2e and Qt
            # clauses in the SAME SENTENCE and reported them both as stale unit
            # counts -- a check that cries wolf about correct text is worse than
            # no check, because the fix people reach for is deleting it.
            patterns = @('unit runs,\s+([\d,]+)\s+checks')
        }
    }
}

# The files the coverage baseline actually covers. The deleted table in
# CLAUDE.md listed 13 when this said 16.
$baseline = Join-Path $repo "docs/coverage-baseline.tsv"
if (Test-Path $baseline) {
    $covRows = Get-Content $baseline | Where-Object { $_ -and $_ -notmatch '^\s*#' -and $_ -notmatch '^file\s' }
    $facts += @{
        id = "coverage-baseline files"
        value = $covRows.Count
        source = "docs/coverage-baseline.tsv"
        patterns = @('coverage baseline lists (\d+) files')
    }
}

# Spelled-out numbers, so "eight targets" is checkable and not just "8".
$numberWords = @{
    "one"=1;"two"=2;"three"=3;"four"=4;"five"=5;"six"=6;"seven"=7;"eight"=8;"nine"=9;"ten"=10;
    "eleven"=11;"twelve"=12;"thirteen"=13;"fourteen"=14;"fifteen"=15;"sixteen"=16;
    "seventeen"=17;"eighteen"=18;"nineteen"=19;"twenty"=20;"thirty-two"=32
}
function Resolve-Number([string]$token) {
    if ($token -match '^\d+$') { return [int]$token }
    # ⚠ THOUSANDS SEPARATORS, added jc-55. This file writes its larger counts as
    # "21,082" and "1,806", and without this the resolver returned $null for
    # them -- which does not fail, it SKIPS. Two facts would have been checked
    # vacuously and reported "ok". The grouping is required to be well formed so
    # that a genuine "3,2" is still refused rather than read as 32.
    if ($token -match '^\d{1,3}(,\d{3})+$') { return [int]($token -replace ',', '') }
    $key = $token.ToLower()
    if ($numberWords.ContainsKey($key)) { return $numberWords[$key] }
    return $null
}

foreach ($fact in $facts) {
    $script:checks++
    $bad = @()
    foreach ($file in $docFiles) {
        # ⚠ CHANGELOG.md IS HISTORY AND IS EXEMPT FROM COUNT CHECKS. Its release
        # entries record what was true AT THAT RELEASE -- "13 of 32 engine
        # toggles are now provably live" is a correct statement about jc-49 and
        # must not be "corrected" to 18. A changelog that gets retrofitted stops
        # being a record. Retired claims still apply to it; a false statement is
        # false whenever it was written.
        if ($file.Name -eq "CHANGELOG.md") { continue }
        $lineNo = 0
        foreach ($line in (Read-Lines $file.FullName)) {
            $lineNo++
            # ⚠ A DATED LINE IS HISTORY, on the same reasoning as the CHANGELOG
            # exemption above. FORK.md records measurements as they were taken
            # -- "As of 2026-09-06: 15 unit runs / 17,531 checks" -- and that
            # sentence is TRUE, permanently, about that date. "Correcting" it to
            # today's number would destroy the record and make the file claim a
            # measurement nobody took. Only undated counts assert the present.
            if ($line -match '\bAs of \d{4}-\d{2}-\d{2}\b') { continue }
            foreach ($pattern in $fact.patterns) {
                foreach ($m in [regex]::Matches($line, $pattern, 'IgnoreCase')) {
                    $claimed = Resolve-Number $m.Groups[1].Value
                    if ($null -ne $claimed -and $claimed -ne $fact.value) {
                        $bad += ("{0}:{1}  claims {2}, but {3} says {4}`n          {5}" -f `
                                 $file.Name, $lineNo, $claimed, $fact.source, $fact.value, $line.Trim())
                    }
                }
            }
        }
    }
    if ($bad.Count) {
        Fail ("the documented " + $fact.id + " count is stale") ($bad -join "`n")
    } else {
        Pass ($fact.id + " = " + $fact.value + " (from " + $fact.source + ")")
    }
}

# -------------------------------------------------------- 2. retired claims --

Say ""
Say "-- retired claims --"

$claimsFile = Join-Path $repo "docs/retired-claims.tsv"
if (-not (Test-Path $claimsFile)) {
    Fail "docs/retired-claims.tsv is missing" "the retired-claim check cannot run without it"
} else {
    $claims = @()
    foreach ($line in (Read-Lines $claimsFile)) {
        if (-not $line -or $line -match '^\s*#' -or $line -match '^id\s') { continue }
        $parts = $line -split "`t"
        if ($parts.Count -lt 4) { continue }
        $claims += @{ id = $parts[0]; pattern = $parts[1]; exempt = $parts[2]; why = $parts[3] }
    }

    if (-not $claims.Count) {
        Fail "docs\retired-claims.tsv parsed to zero claims" `
             "the file exists but no row survived parsing -- this check would pass vacuously"
    }

    foreach ($claim in $claims) {
        $script:checks++
        $hits = @()
        foreach ($file in ($docFiles + $sourceFiles)) {
            $lineNo = 0
            foreach ($line in (Read-Lines $file.FullName)) {
                $lineNo++
                $m = [regex]::Match($line, $claim.pattern)
                if (-not $m.Success) { continue }
                # Quoting the dead claim is how a document buries it.
                if (Test-InsideQuotes $line $m.Index) { continue }
                # A negation, or the same words meaning something else. See
                # docs\retired-claims.tsv.
                if ($claim.exempt -and $line -match $claim.exempt) { continue }
                $rel = $file.FullName.Substring($repo.Length).TrimStart([char[]]@('\','/'))
                $hits += ("{0}:{1}`n          {2}" -f $rel, $lineNo, $line.Trim())
            }
        }
        if ($hits.Count) {
            Fail ("a retired claim is being asserted again: " + $claim.id) `
                 (($hits -join "`n") + "`n`n        WHAT IS TRUE: " + $claim.why)
        } else {
            Pass ("retired claim stays retired: " + $claim.id)
        }
    }
}

# ----------------------------------------------------------------- 3. links --

Say ""
Say "-- relative links --"

$script:checks++
$broken = @()
foreach ($file in ($docFiles | Where-Object { $_.Extension -eq ".md" })) {
    $lineNo = 0
    foreach ($line in (Read-Lines $file.FullName)) {
        $lineNo++
        foreach ($m in [regex]::Matches($line, '\]\(([^)]+)\)')) {
            $target = $m.Groups[1].Value
            if ($target -match '^(https?:|mailto:|#)') { continue }
            $path = ($target -split '#')[0]
            if (-not $path) { continue }
            $full = Join-Path $file.Directory.FullName $path
            if (-not (Test-Path -LiteralPath $full)) {
                $rel = $file.FullName.Substring($repo.Length).TrimStart([char[]]@('\','/'))
                $broken += ("{0}:{1}  -> {2}" -f $rel, $lineNo, $target)
            }
        }
    }
}
if ($broken.Count) {
    Fail "a relative link does not resolve" ($broken -join "`n")
} else {
    Pass "every relative link resolves"
}

# ------------------------------------------------------------------ verdict --

Say ""
if ($script:failures) {
    Write-Host ("{0} check(s), {1} FAILED" -f $script:checks, $script:failures) -ForegroundColor Red
    Write-Host ""
    Write-Host "Before correcting a duplicate, ask whether it should exist at all." -ForegroundColor Yellow
    Write-Host "A fact written in two places is a fact that will disagree with itself." -ForegroundColor Yellow
    exit 1
}
Write-Host ("{0} check(s), the documentation still agrees with the code" -f $script:checks) -ForegroundColor Green
exit 0
