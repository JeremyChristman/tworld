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

  ⚠ AND BE PRECISE ABOUT THE FIRST CHECK: it scans for a figure in ONE OF THESE
    PHRASINGS, not for "any figure that contradicts a fact". That distinction
    was disclosed too weakly until jc-57, when an audit appended eleven false
    claims to CLAUDE.md and SIX went through -- every one a near-miss rewording
    of a form that was caught ("25 of THE 32", "the matrix HOLDS 25 witnesses",
    "9000 digests" with the trailing clause dropped, "99999 checks ACROSS 18
    unit runs", "40 FUZZ targets"). All twelve are caught now, but the class is
    open-ended and ordinary editing is what produces it: rewriting CLAUDE.md's
    "eight targets" into some third phrasing can still retire a check silently.

    🔴 THE OTHER HALF OF THAT LESSON. The first attempt to fix it broadened the
    patterns and immediately failed on THREE CORRECT SENTENCES -- the negated
    "14 of the 32 toggles have no witness", the golden master's unrelated "2 of
    32", and per-file counts like "74 unit checks". A check that cries wolf gets
    deleted rather than fixed, so it is strictly worse than the gap it closed.
    When widening a pattern here, run this script against the REAL documents
    before believing it.
#>
param(
    [switch]$Quiet,

    # 🔴 CANARY DISCIPLINE FOR THIS GATE, added 2026-09-15 after an adversarial
    # audit walked straight through it.
    #
    # A retired claim is matched by a REGEX over prose, and prose can be
    # reworded. SECURITY.md had told researchers three times that unslist.c is
    # dead code, each time in different words; the pattern enumerated the three
    # wordings that had already happened. The audit appended a FOURTH --
    # "unreachable dead code ... never called" -- and this script reported
    # `ok  retired claim stays retired: unslist-unreachable`, exit 0.
    #
    # So every claim now DECLARES the phrasings it must catch, in a fifth column
    # of docs/retired-claims.tsv: `+text` must match, `-text` must not. -SelfTest
    # runs those and fails if any is wrong, which is the same thing
    # mutate.ps1's five canaries do for the census.
    #
    # ⚠ WHAT THIS DOES NOT DO: it cannot catch a phrasing nobody thought of. It
    # makes the check's INTENT explicit and testable, and it turns "someone
    # reworded it" from a silent pass into a one-line addition. A regex over
    # English has no complete answer and this file should not pretend otherwise.
    #
    # 🔴 AND EVERY OTHER CHECK CLASS GETS A PLANTED DEFECT. The same audit found
    # the derived-count check reporting ok on a fact NO document stated, and its
    # recommendation was one canary per class. -SelfTest copies the tracked tree
    # to a scratch directory, confirms the copy verifies CLEAN (the control --
    # without it a canary could "fail" for a reason unrelated to its plant),
    # then plants one defect per class and requires THIS script, run for real
    # against the copy, to fail naming it: a stale count, a fact nothing asserts,
    # a reworded retired claim, a dead link, a deleted truth source.
    [switch]$SelfTest
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

# ------------------------------------------------------------- 0. self test --
#
# Runs FIRST and exits: a gate whose own canaries fail has no business reporting
# on anything else.

if ($SelfTest) {
    $bad = 0

    # --- 0a. the retired-claim probes: does each pattern mean what it says? ---
    Write-Host ""
    Write-Host "-- retired-claim probes (docs/retired-claims.tsv, fifth column) --"
    $selfFile = Join-Path $repo "docs/retired-claims.tsv"
    $ran = 0
    if (-not (Test-Path $selfFile)) {
        Write-Host "  FAIL  docs/retired-claims.tsv is missing" -ForegroundColor Red
        ++$bad
    } else {
        foreach ($line in (Read-Lines $selfFile)) {
            if (-not $line -or $line -match '^\s*#' -or $line -match '^id\s') { continue }
            $parts = $line -split "`t"
            if ($parts.Count -lt 5 -or -not $parts[4]) {
                Write-Host ("  FAIL  {0} declares no probes" -f $parts[0]) -ForegroundColor Red
                Write-Host "        Add a fifth column: +text it must catch;-text it must not."
                ++$bad
                continue
            }
            $id = $parts[0]; $pattern = $parts[1]; $exempt = $parts[2]
            foreach ($probe in ($parts[4] -split ';')) {
                if (-not $probe) { continue }
                $want = $probe.Substring(0, 1)
                $text = $probe.Substring(1)
                ++$ran
                # The same THREE tests the real check applies, in the same order:
                # the pattern, the quoted-span exemption, then the exempt column.
                # (This said "two" and skipped the quote test -- a review caught
                # that a quoted probe would then disagree with the real gate.)
                $pm = [regex]::Match($text, $pattern)
                $hit = $pm.Success
                if ($hit -and (Test-InsideQuotes $text $pm.Index)) { $hit = $false }
                if ($hit -and $exempt -and $text -match $exempt) { $hit = $false }
                if ($want -eq '+' -and -not $hit) {
                    Write-Host ("  FAIL  {0}: this SHOULD be caught and is not:" -f $id) -ForegroundColor Red
                    Write-Host ("          {0}" -f $text) -ForegroundColor Red
                    ++$bad
                } elseif ($want -eq '-' -and $hit) {
                    Write-Host ("  FAIL  {0}: this should NOT be caught and is:" -f $id) -ForegroundColor Red
                    Write-Host ("          {0}" -f $text) -ForegroundColor Red
                    ++$bad
                } else {
                    Say ("  ok    {0}: {1}{2}" -f $id, $want, $text)
                }
            }
        }
        if ($ran -eq 0) {
            Write-Host "  FAIL  no probes ran -- refusing to report a pass" -ForegroundColor Red
            ++$bad
        } else {
            Write-Host ("  {0} probe(s) run" -f $ran)
        }
    }

    # --- 0b. one planted defect per check class, caught by THIS script -------
    #
    # The probes above test a regex in isolation. These test the whole gate --
    # file discovery, the scan, the verdict, the exit code -- by running it for
    # real against a scratch copy of the tracked tree with one thing broken.
    Write-Host ""
    Write-Host "-- planted defects: this script, run against a broken scratch copy --"
    $scratch = Join-Path ([IO.Path]::GetTempPath()) ("tw-verify-docs-selftest-" + $PID)
    $utf8 = New-Object System.Text.UTF8Encoding($false)
    try {
        if (Test-Path -LiteralPath $scratch) { Remove-Item -LiteralPath $scratch -Recurse -Force }
        $null = New-Item -ItemType Directory -Path $scratch
        # The WORKING-TREE copy of every tracked file, so an uncommitted edit to a
        # document -- or to this script -- is what gets tested.
        $tracked = @(& git -C $repo ls-files)
        if ($LASTEXITCODE -ne 0 -or $tracked.Count -eq 0) { throw "git ls-files listed nothing, so there is no tree to copy" }
        foreach ($rel in $tracked) {
            $src = Join-Path $repo $rel
            if (-not (Test-Path -LiteralPath $src -PathType Leaf)) { continue }
            $dst = Join-Path $scratch $rel
            $dir = Split-Path -Parent $dst
            if (-not (Test-Path -LiteralPath $dir)) { $null = New-Item -ItemType Directory -Path $dir -Force }
            [IO.File]::Copy($src, $dst, $true)
        }

        # The same host that is running this, so pwsh on the Linux CI runner and
        # Windows PowerShell 5.1 on the desktop each test themselves.
        $hostExe = (Get-Process -Id $PID).Path
        $childArgs = @("-NoProfile")
        if ($env:OS -eq "Windows_NT") { $childArgs += @("-ExecutionPolicy", "Bypass") }
        $childArgs += @("-File", (Join-Path $scratch "verify-docs.ps1"))
        function Invoke-ScratchCopy {
            $prev = $ErrorActionPreference
            $ErrorActionPreference = "Continue"
            $out = (& $hostExe @childArgs 2>&1 | Out-String)
            $code = $LASTEXITCODE
            $ErrorActionPreference = $prev
            # pwsh can color redirected output with VT escapes; strip them so a
            # FAIL line still starts with FAIL on the Linux runner.
            $out = [regex]::Replace($out, "\x1B\[[0-9;]*[A-Za-z]", "")
            return @{ rc = $code; text = $out }
        }
        # Returns $true only if the edit actually changed the file -- a plant
        # whose anchor text has gone would otherwise "pass" by planting nothing.
        function Edit-Scratch([string]$rel, [scriptblock]$change) {
            $p = Join-Path $scratch $rel
            $before = [IO.File]::ReadAllText($p)
            $after = & $change $before
            if ($after -ceq $before) { return $false }
            [IO.File]::WriteAllText($p, $after, $utf8)
            return $true
        }

        # THE CONTROL. Unless the untouched copy verifies clean, a canary that
        # fails proves nothing about its plant.
        $control = Invoke-ScratchCopy
        if ($control.rc -ne 0) {
            Write-Host ("  FAIL  control: the UNMODIFIED copy does not verify clean (exit {0})" -f $control.rc) -ForegroundColor Red
            Write-Host "        so no planted defect below could mean anything. Run this script" -ForegroundColor Red
            Write-Host "        without -SelfTest and fix what it reports first:" -ForegroundColor Red
            foreach ($l in @($control.text -split "`r?`n" | Where-Object { $_ -match '^\s*FAIL\s' } | Select-Object -First 6)) {
                Write-Host ("        " + $l.Trim()) -ForegroundColor Red
            }
            ++$bad
        } else {
            Write-Host "  ok    control: the unmodified copy verifies clean"

            $canaries = @(
                @{ name = "a stale derived count"
                   expect = "the documented unit checks count is stale"
                   plant = { if (Edit-Scratch "CLAUDE.md" { param($t) [regex]::Replace($t, '(unit runs, )[\d,]+( checks)', '${1}99,999${2}') }) { "CLAUDE.md" } } },
                @{ name = "a fact no document asserts any more"
                   expect = "no document asserts the NO_FIX toggles count"
                   plant = { foreach ($rel in $tracked) {
                                 if ($rel -notmatch '\.(md|txt|tsv|lock|yml)$') { continue }
                                 if (Edit-Scratch $rel { param($t) $t.Replace("behavior toggles", "engine switches") }) { $rel }
                             } } },
                @{ name = "a retired claim, reworded the way the audit reworded it"
                   expect = "a retired claim is being asserted again: unslist-unreachable"
                   plant = { if (Edit-Scratch "SECURITY.md" { param($t) $t + "`nNote for researchers: unslist.c is unreachable dead code and is never called by any shipped build.`n" }) { "SECURITY.md" } } },
                @{ name = "a relative link to nothing"
                   expect = "a relative link does not resolve"
                   plant = { if (Edit-Scratch "AGENTS.md" { param($t) $t + "`nSee [a decision nobody wrote](docs/adr/9999-never-written.md).`n" }) { "AGENTS.md" } } },
                @{ name = "a deleted truth source"
                   expect = "the truth source engine-snapshot.tsv is missing"
                   plant = { $p = Join-Path $scratch "test/golden/engine-snapshot.tsv"
                             if (Test-Path -LiteralPath $p) { Remove-Item -LiteralPath $p -Force; "test/golden/engine-snapshot.tsv" } } }
            )
            foreach ($c in $canaries) {
                $touched = @(& $c.plant)
                if ($touched.Count -eq 0) {
                    Write-Host ("  FAIL  {0}: the plant changed nothing -- its anchor is gone, so this canary would pass vacuously" -f $c.name) -ForegroundColor Red
                    ++$bad
                    continue
                }
                $r = Invoke-ScratchCopy
                foreach ($rel in $touched) { [IO.File]::Copy((Join-Path $repo $rel), (Join-Path $scratch $rel), $true) }
                $fails = @($r.text -split "`r?`n" | Where-Object { $_ -match '^\s*FAIL\s' })
                if ($r.rc -eq 0) {
                    Write-Host ("  FAIL  {0}: planted, and the script still exited 0" -f $c.name) -ForegroundColor Red
                    ++$bad
                } elseif (-not $r.text.Contains($c.expect)) {
                    Write-Host ("  FAIL  {0}: exited {1}, but never said `"{2}`"" -f $c.name, $r.rc, $c.expect) -ForegroundColor Red
                    foreach ($l in $fails) { Write-Host ("        " + $l.Trim()) -ForegroundColor Red }
                    ++$bad
                } elseif ($fails.Count -ne 1) {
                    # Exactly one: otherwise the plant broke something else too, and
                    # the expected message may have come from the collateral damage.
                    Write-Host ("  FAIL  {0}: caught, but with {1} failures instead of exactly one" -f $c.name, $fails.Count) -ForegroundColor Red
                    foreach ($l in $fails) { Write-Host ("        " + $l.Trim()) -ForegroundColor Red }
                    ++$bad
                } else {
                    Write-Host ("  ok    {0} ({1} file(s) planted) -> {2}" -f $c.name, $touched.Count, $c.expect)
                }
            }
        }
    } catch {
        Write-Host ("  FAIL  the self-test could not run: " + $_.Exception.Message) -ForegroundColor Red
        ++$bad
    } finally {
        if (Test-Path -LiteralPath $scratch) { Remove-Item -LiteralPath $scratch -Recurse -Force -ErrorAction SilentlyContinue }
    }

    Write-Host ""
    if ($bad -gt 0) {
        Write-Host ("{0} self-test failure(s): this gate cannot be trusted until they are fixed" -f $bad) -ForegroundColor Red
        exit 1
    }
    Write-Host "self-test: every probe and every planted defect behaved correctly" -ForegroundColor Green
    exit 0
}

# The documents this script polices. Source files are included for the retired
# claims only: settings.cpp carried one of them in a comment for four builds.
$docFiles = @()
$docFiles += Get-ChildItem -LiteralPath $repo -Filter "*.md" -File
$docFiles += Get-ChildItem -LiteralPath (Join-Path $repo "docs/adr") -Filter "*.md" -File -ErrorAction SilentlyContinue
$docFiles += Get-ChildItem -LiteralPath (Join-Path $repo ".github") -Filter "*.md" -File -ErrorAction SilentlyContinue
$docFiles += Get-ChildItem -LiteralPath $repo -Filter "README.txt" -File

# 🔴 AND THE FILES THAT ARE NOT MARKDOWN BUT ARE STILL DOCUMENTATION.
#
# The scan used to be *.md plus README.txt, and an audit found a stale count
# living exactly in the gap: docs/toolchain.lock said "run test\run-nofix.ps1 --
# the 13 NO_FIX_* witnesses" when the answer was 18. That is a present-tense
# instruction to whoever bumps the compiler, in the file whose entire purpose is
# that the bump be done carefully -- the worst possible place for a wrong number
# and the one place nothing was looking.
#
# ⚠ BY CLASS, NOT BY NAME. The first version of this listed
# `docs/toolchain.lock` literally, which fixes the one file an audit happened to
# look at and nothing else -- the same shape of near-miss as patterns that match
# one phrasing. Anything under docs\ that carries prose is in scope now, plus
# the CI workflows, which are full of present-tense claims about what runs.
#
# ⚠ STILL NOT *.ps1, and that is deliberate. The scripts contain the very
# regexes this file matches with, plus flag values and byte counts; matching
# prose patterns against code is how a checker starts crying wolf. Scripts get
# their numbers reviewed as code. Tried, reverted, recorded.
foreach ($pattern in @("*.lock", "*.tsv")) {
    $docFiles += Get-ChildItem -LiteralPath (Join-Path $repo "docs") -Filter $pattern -File -ErrorAction SilentlyContinue
}
$docFiles += Get-ChildItem -LiteralPath (Join-Path $repo ".github/workflows") -Filter "*.yml" -File -ErrorAction SilentlyContinue
# 🔴 THE REGISTRY CANNOT BE SCANNED AGAINST ITSELF. docs\retired-claims.tsv
# necessarily WRITES DOWN every dead claim -- that is the whole file -- so every
# pattern matches its own row and its own header. It was only ever invisible
# because the patterns were narrow enough to miss the header's paraphrase;
# broadening them in 2026-09-15 made the file report itself.
$docFiles = @($docFiles | Where-Object { $_ -and $_.Name -ne 'retired-claims.tsv' })

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

# 🔴 A MISSING TRUTH SOURCE IS A FAILURE, NOT A SKIPPED CHECK.
#
# Every fact below used to be wrapped in a bare `if (Test-Path $source)`, so
# deleting the file a check reads made the check quietly disappear and the script
# print green. Measured by an audit: moving docs\test-counts.tsv out of the way
# took the run from 13 checks to 11 and it still said "the documentation still
# agrees with the code", exit 0.
#
# That is precisely the failure this script was written to stop, committed by the
# script itself -- and the repository already had the right pattern in two other
# places. run-golden.ps1 and run-nofix.ps1 both fail CLOSED on a missing or
# emptied baseline, the latter with "no witnesses in the matrix at all -- refusing
# to report success."
#
# Nothing here is optional: every one of these is committed, and a checkout
# missing one is broken rather than differently configured.
function Need-Source([string]$path, [string]$why) {
    if (Test-Path $path) { return $true }
    $script:checks++
    Fail ("the truth source " + (Split-Path -Leaf $path) + " is missing") `
         ("$why`nWithout it that fact cannot be checked, and a check that cannot run must" +
          " not report success. Restore the file or, if it was retired deliberately," +
          " remove its fact from this script and say why.")
    return $false
}

# The NO_FIX_* differential matrix. This is the number that sat wrong for five
# builds while the same file quoted it correctly three times elsewhere.
$matrix = Join-Path $repo "test/nofix/nofix-matrix.tsv"
if (Need-Source $matrix "It records which NO_FIX_* engine toggles have a differential witness.") {
    $rows = Get-Content $matrix | Where-Object { $_ -and $_ -notmatch '^\s*#' }
    $withWitness = @($rows | Where-Object { ($_ -split "`t")[1] -and ($_ -split "`t")[1] -ne '-' }).Count
    $facts += @{
        id = "NO_FIX witnesses"
        value = $withWitness
        source = "test/nofix/nofix-matrix.tsv"
        # ⚠ BROADENED jc-57, AFTER AN AUDIT WALKED PAST THE NARROW FORMS.
        # Eleven false claims were appended to CLAUDE.md and SIX were not
        # caught -- every one of them a near-miss rephrasing of a form that
        # WAS caught: "25 of THE 32 toggles" (one word), "the matrix holds 25
        # witnesses" (different verb), "9000 digests" (trailing clause
        # dropped), "99999 checks across 18 unit runs" (reordered), "40 fuzz
        # targets" (a word between the number and the noun). The novel-claim
        # limitation is inherent and disclosed in the header; THAT class was
        # not, and is the likelier one, because it is what ordinary editing
        # produces.
        #
        # 🔴 AND THEN NARROWED AGAIN, BECAUSE THE FIRST BROADENING CRIED WOLF
        # ON THREE CORRECT SENTENCES. `(\d+)\s+of\s+(?:the\s+)?32\b` matched
        # all of these, every one of them true:
        #
        #   "14 of the 32 toggles have NO differential witness"  (the negation)
        #   "2 of 32 NO_FIX_* toggles, measured"                 (a DIFFERENT
        #                                    fact: the golden master's reach)
        #
        # A check that fails on correct text gets deleted, not fixed -- so the
        # positive forms now require the sentence to actually ASSERT a witness,
        # and the negation is derived and checked separately below.
        patterns = @(
            '(\d+)\s+have such a witness',
            '(\d+)\s+of\s+(?:the\s+)?32[^.\r\n]{0,60}?\bhave\s+(?:such\s+)?a\s+witness',
            '(\d+)\s+NO_FIX_\*?\s+witness',
            '(\d+)\s+witness(?:es)?\b',
            '(?:holds|records|found)\s+(\d+)\s+witness'
        )
    }
    # The complement, so the sentence that states it is checked rather than
    # merely tolerated by a narrower pattern.
    $facts += @{
        id = "NO_FIX toggles WITHOUT a witness"
        value = $rows.Count - $withWitness
        source = "test/nofix/nofix-matrix.tsv"
        patterns = @(
            '(\d+)\s+of\s+(?:the\s+)?32[^.\r\n]{0,60}?\bhave\s+no\s+(?:differential\s+)?witness',
            '(\d+)\s+toggle\(?s\)?\s+have no witness'
        )
    }
    $facts += @{
        id = "NO_FIX toggles"
        value = $rows.Count
        source = "test/nofix/nofix-matrix.tsv"
        # ⚠ WORDS AS WELL AS DIGITS. Every document that states this writes
        # "thirty-two `NO_FIX_*` behavior toggles", so a digits-only pattern
        # matched NOTHING and the fact reported ok while verifying nothing --
        # found 2026-09-15 by the unasserted-fact check further down, which
        # exists for exactly this. Resolve-Number already knows the words.
        patterns = @('([\w-]+)\s+`?NO_FIX_\*?`?\s+behavior toggles')
    }
}

# The mutation harness’s own canaries. This number was written in THREE places and
# was wrong in two of them: mutate.ps1’s comment said "Four canaries", CLAUDE.md said
# four and enumerated four, AGENTS.md said five, and five is what runs. Nothing
# checked it, so it is derived here from the script itself.
$mutate = Join-Path $repo "mutate.ps1"
if (Need-Source $mutate "It defines the census self-test canaries this count describes.") {
    $canaryCount = @(Get-Content $mutate |
                     Where-Object { $_ -match '^\s*\$canaries \+= @{' }).Count
    $facts += @{
        id = "mutate.ps1 self-test canaries"
        value = $canaryCount
        source = "mutate.ps1"
        patterns = @('[Ii]t plants ([\w-]+) mutants', 'plants ([\w-]+) mutants whose verdicts')
    }
}

# Fuzz targets. This one drifted from six to seven to eight across three files.
$fuzz = @(Get-ChildItem -LiteralPath (Join-Path $repo "test/fuzz") -File |
          Where-Object { $_.Name -like "fuzz_*.c" -or $_.Name -like "fuzz_*.cpp" })
$facts += @{
    id = "fuzz targets"
    value = $fuzz.Count
    source = "test/fuzz/fuzz_*.c[pp]"
    # `(?:fuzz\s+)?` because "There are 40 fuzz targets" walked past the
    # narrow form -- one word between the number and the noun.
    patterns = @('(\d+)\s+(?:fuzz\s+)?targets', 'libFuzzer,\s+\*\*(\w+)\s+targets\*\*')
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
if (Need-Source $snapshot "It is the golden-master baseline; its row count IS the digest figure.") {
    $digestRows = @(Get-Content $snapshot | Where-Object { $_ -and $_ -notmatch '^\s*#' })
    $facts += @{
        id = "golden-master digests"
        value = $digestRows.Count
        source = "test/golden/engine-snapshot.tsv"
        # The bare `digests` form matters: "records 9000 digests" -- the same
        # sentence with its trailing "over 903 levels" clause dropped -- was
        # not caught by either anchored form.
        patterns = @('([\d,]+)\s+(?:golden-master\s+)?digests\b')
    }
}

# The unit suite's totals. 🔴 THE LAST HAND-TYPED NUMBER IN CLAUDE.md, and it
# was wrong: section 5 said 21,008 checks while a full run reported 21,012.
# Nothing was broken by that -- which is the point. A figure nobody can check is
# a figure that drifts quietly until someone reasons from it.
$counts = Join-Path $repo "docs/test-counts.tsv"
if (Need-Source $counts "the unit runner writes it on a complete run; it carries the unit totals.") {
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
            #
            # The reordered form -- "99999 checks across 18 unit runs" -- got
            # past the single anchored pattern, so both orders are matched now.
            # Still anchored to the word "unit" in each, deliberately: a bare
            # `([\d,]+)\s+checks` is what cried wolf.
            #
            # ⚠ `([\d,]+)\s+unit checks` was tried and REMOVED: it matches the
            # per-file figures ("series.c ... 74 unit checks", "It has 40 unit
            # checks and a fuzz target now"), which are correct and are not
            # this total. Both surviving patterns name "unit runs", which only
            # the suite-wide sentence does.
            patterns = @(
                'unit runs,\s+([\d,]+)\s+checks',
                '([\d,]+)\s+checks\s+(?:across|in|over)\s+[\d,]+\s+unit runs'
            )
        }
    }
}

# The files the coverage baseline actually covers. The deleted table in
# CLAUDE.md listed 13 when this said 16.
$baseline = Join-Path $repo "docs/coverage-baseline.tsv"
if (Need-Source $baseline "coverage.ps1 -UpdateBaseline writes it; it carries the per-file coverage.") {
    $covRows = Get-Content $baseline | Where-Object { $_ -and $_ -notmatch '^\s*#' -and $_ -notmatch '^file\s' }
    # 🔴 NO FACT IS DERIVED FROM THIS FILE, DELIBERATELY -- the source is still
    # required above, so its absence is a failure rather than a skip.
    #
    # There used to be one: "coverage baseline lists (\d+) files". No document has
    # ever contained that sentence, because CLAUDE.md makes the opposite promise in
    # bold: "THE NUMBERS LIVE IN docs/coverage-baseline.tsv, AND THERE IS NO COPY OF
    # THEM HERE ON PURPOSE." So the check could only ever match nothing -- and until
    # 2026-09-15 matching nothing printed ok. It was a check of a sentence the
    # project had decided never to write.
    #
    # Removed rather than repaired: writing the count into a document to give this
    # something to verify would recreate exactly the duplicate that section forbids.
    # coverage.ps1 -CheckBaseline is what polices these numbers.
}

# Spelled-out numbers, so "eight targets" is checkable and not just "8".
$numberWords = @{
    "one"=1;"two"=2;"three"=3;"four"=4;"five"=5;"six"=6;"seven"=7;"eight"=8;"nine"=9;"ten"=10;
    "eleven"=11;"twelve"=12;"thirteen"=13;"fourteen"=14;"fifteen"=15;"sixteen"=16;
    "seventeen"=17;"eighteen"=18;"nineteen"=19;"twenty"=20;"thirty-two"=32
}
function Resolve-Number([string]$token) {
    if ($token -match '^\d+$') { return [int]$token }
    # ⚠ THOUSANDS SEPARATORS, added jc-56. This file writes its larger counts as
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
    # 🔴 HOW MANY TIMES THIS FACT WAS ACTUALLY FOUND. Without it the verdict
    # below cannot tell "every document agrees" from "no document says this and
    # the pattern has been dead for months" -- both leave $bad empty and both
    # printed ok. An audit found one of these nine facts matching NO text in any
    # document while reporting green, which is the same vacuous pass Need-Source
    # above exists to refuse.
    $seen = 0
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
                    # ⚠ A QUOTED NUMBER IS A QUOTATION, NOT A CLAIM -- the same
                    # exemption the retired-claim check has had from the start,
                    # and it belongs here for the same reason. FORK.md records
                    # what a false statement SAID while explaining why it was
                    # false ("the 13 NO_FIX_* witnesses", "9000 digests"), and a
                    # check that fires on the correction is a check that gets
                    # deleted. Added jc-57, when writing up an audit's findings
                    # made this file fail on its own account of them.
                    #
                    # The cost is real and worth stating: a false count inside
                    # quotes is now invisible to this check. That is the same
                    # trade docs/retired-claims.tsv already made, and quoting is
                    # how a correction is written -- there is no third option.
                    if (Test-InsideQuotes $line $m.Index) { continue }
                    $claimed = Resolve-Number $m.Groups[1].Value
                    if ($null -ne $claimed) { $seen++ }
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
    } elseif ($seen -eq 0) {
        Fail ("no document asserts the " + $fact.id + " count") `
             ("This fact is derived from " + $fact.source + " and checked against every" +
              " document, and NOTHING matched -- so it verified nothing while reporting" +
              " success.`nEither a document stopped stating it (drop the fact from this" +
              " script, and say why), or the pattern stopped matching the way it is" +
              " written (fix the pattern). Both are silent failures; this is the check" +
              " that makes them loud.")
    } else {
        Pass ($fact.id + " = " + $fact.value + " (from " + $fact.source + ", asserted " + $seen + "x)")
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
