<#
Batch-verifies a whole solution collection, and diffs one build against another.

    # record what a build does across every set that has solutions
    powershell -ExecutionPolicy Bypass -File test\run-corpus.ps1 -Exe build-static\tworld2.exe -Out corpus-jc44

    # then, after changing the engine, compare
    powershell -ExecutionPolicy Bypass -File test\run-corpus.ps1 -Exe build-static\tworld2.exe -Out corpus-jc45 -Against corpus-jc44

    # or just diff two recordings you already have
    powershell -ExecutionPolicy Bypass -File test\run-corpus.ps1 -Compare corpus-jc44 -Against corpus-jc45

WHAT THIS IS FOR

This is the instrument for ENGINE work, and it is the only one that answers the
question that matters: *did any recorded solution stop replaying?* The unit
suite cannot answer it -- it drives synthesized levels -- and neither can the
end-to-end layer, which verifies two solutions. This one replays tens of
thousands.

It is what proved jc-45 safe: 290 sets, 18,739 valid and 1,108 invalid under
both builds, with 0 of 303 per-set outputs differing.

🔴 RUN THIS BEFORE SHIPPING ANY CHANGE TO mslogic.c, lxlogic.c, encoding.c,
random.c OR solution.c. A green unit suite says nothing about replay.

⚠ AND KNOW WHAT IT CANNOT SEE. `doturn()` ignores its `cmd` argument whenever
`state.replay >= 0`, and batch mode never enables joystick behavior, so
`input()` is never called during verification. A change to generic\in.c or
generic\dirinput.c produces a byte-identical corpus result no matter how broken
it is. Hand playtesting is the only oracle there. See CLAUDE.md section 3.5.

TWO STREAMS, TWO STANDARDS

Each set is recorded twice: <set>.out is the program's stdout, <set>.err its
stderr. They are compared differently on purpose.

  .out  THE VERDICT. It names which levels were judged invalid, so a swap of
        one valid solution for one invalid -- which the totals would hide
        completely -- shows up here. Compared byte for byte, and a difference
        FAILS the run. This is the desync check.

  .err  THE WARNINGS: bad tiles, invalid creature locations, password
        mismatches. Compared after normalizing two things that change for
        reasons that are not behavior -- err.c stamps __LINE__ into every
        message, so adding a COMMENT to a source file moves it, and the
        scratch directory's name is fresh per run and appears inside
        file-name messages. ADVISORY: it is reported but does not fail the
        run, because no recorded solution's verdict depends on a warning.

The .err comparison was added in jc-51, after a by-hand diff found that 29 of
303 sets printed different warnings between two builds and nothing in this
script noticed. Every one of those turned out to be a moved __LINE__ -- but
"nothing noticed" was the finding.

WHERE THE CORPUS COMES FROM

-Corpus should point at a Chip's Challenge folder containing sets\, data\ and a
save\ with .tws files. With no -Corpus it is $env:TWORLD_CORPUS if that is set,
and otherwise Dropbox\Games\Computer\Chip's Challenge under the CURRENT user's
profile, which is the maintainer's install on either of his PCs. -ShowCorpus
prints the folder this run would use, and exits. Both rules -- the default, and
what makes a folder a corpus -- live in test\corpus-folder.ps1, which corpus.yml
and run-playtest.ps1 dot-source too.

⚠ A SELF-HOSTED RUNNER INSTALLED AS A SERVICE runs as a service account, whose
profile holds no Dropbox, so corpus.yml would report the corpus absent and skip.
Set TWORLD_CORPUS in that runner's environment, or dispatch with corpus_path.
That machine must also have the build toolchain already: corpus.yml installs
only the pinned compiler and relies on the machine's own cmake, ninja and static
Qt (FORK.md item 52). MSYS2 is machine-wide, so a service account sees them.

🔴 THE DEFAULT USED TO BE A LITERAL "C:\Users\Jeremy\...", and the two PCs this
is developed on have different usernames (Jeremy, jerem). On the laptop it named
a folder that does not exist, so the release gate failed there unless -Corpus
was typed by hand (found 2026-11-22). run-playtest.ps1 had the same defect and
was fixed the same way.

THE CORPUS IS COPIED to a scratch directory before anything runs and the copy is
what gets used, because:

  * a -b run creates save\history and rewrites tw_settings.ini in the working
    directory even with -r, which only protects the .tws files; and
  * pointing -L at a directory makes createallmissingseries() WRITE generated
    .dac files into it.

Neither is acceptable against somebody's real collection, so it is never
touched. The copy costs a minute and about 90 MB.

THE TRAPS THIS SCRIPT ENCODES

  1. The executable is a WINDOWS-GUI-SUBSYSTEM binary. Called the plain way from
     PowerShell it does not block, returns an EMPTY exit code, and captures
     nothing -- a corpus run written that way "passes" in seconds having
     verified nothing at all. Everything goes through Start-Process -Wait.
  2. Batch verify's exit code is only a verdict with -q, and even then "0
     invalid" and "no solutions at all" are both 0. The verdict is parsed from
     stdout: "Valid solutions: N", where the space is %4d PADDING and is gone
     from 1,000 up ("Valid solutions:1325"). Read-BatchVerdict reads it.
  3. The set argument must be a BARE filename found inside -L, never a path.
  4. A .tws is named "<dacname>.tws", so the set for save\CCLP1.dat-ms.dac.tws
     is the .dac of that name -- spelled as the .dac is ON DISK, because Tile
     World matches the name case-sensitively and Windows does not. Sets with no
     .tws are skipped -- there is nothing to verify.
#>
param(
    [string]$Exe,
    # Left out, this user's own copy (see "WHERE THE CORPUS COMES FROM" above).
    # Given EMPTY, refused: see below.
    [string]$Corpus,
    [string]$Out,
    [string]$Against,
    [string]$Compare,
    [string]$Filter,
    # Reuse an already-copied scratch corpus instead of copying again. The copy
    # is the slow part; this makes a second build's run much faster.
    [string]$ScratchCorpus,
    [switch]$KeepScratch,
    # Exercises Compare-Runs against synthesized recordings. Needs NO corpus,
    # which is the point: it is the one part of this script CI can run.
    [switch]$SelfTest,
    # Print the corpus folder this run would use, and exit. For a person: a
    # script should dot-source test\corpus-folder.ps1, since a child process's
    # stdout goes through the console code page.
    [switch]$ShowCorpus
)
$ErrorActionPreference = "Continue"
$repo = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot "corpus-folder.ps1")   # Resolve-DefaultCorpus, Get-CorpusProblem, ConvertTo-FullCorpusPath, Read-BatchVerdict

# The default corpus: -Corpus, then $env:TWORLD_CORPUS, then the conventional
# folder under this user's profile (Resolve-DefaultCorpus).
#
# ⚠ An EMPTY -Corpus is refused, not defaulted: `-Corpus $env:UNSET` from inside
# PowerShell binds "" and would otherwise replay the maintainer's collection
# while the caller believed theirs was verified (review, 2026-11-22). Only where
# the corpus is actually used: -SelfTest, -Compare and -ScratchCorpus never read it.
$corpusIsRead = $ShowCorpus -or -not ($SelfTest -or $Compare -or $ScratchCorpus)
if ($PSBoundParameters.ContainsKey('Corpus') -and -not $Corpus -and $corpusIsRead) {
    throw "-Corpus was given an empty value; give a folder, or leave it out for the default"
}
if (-not $Corpus) { $Corpus = Resolve-DefaultCorpus }
if ($ShowCorpus) {
    # stdout carries the path and nothing else: a caller captures it. So the
    # refusal is a throw (the error stream), never Write-Host, which from a child
    # powershell.exe lands on stdout (measured: the captured "path" was the error
    # sentence).
    if (-not $Corpus) { throw "no -Corpus given, no TWORLD_CORPUS, and no user profile to find one in" }
    Write-Output $Corpus
    exit 0
}

function Resolve-Out([string]$p) {
    if (-not $p) { return $null }
    if ([IO.Path]::IsPathRooted($p)) { return $p }
    return (Join-Path $repo $p)
}

# ------------------------------------------------------------ diff two runs --

function Compare-Runs([string]$a, [string]$b) {
    foreach ($d in @($a, $b)) {
        if (-not (Test-Path -LiteralPath (Join-Path $d "summary.tsv"))) {
            Write-Host "no summary.tsv in $d" -ForegroundColor Red
            exit 1
        }
    }
    $sa = Get-Content -LiteralPath (Join-Path $a "summary.tsv")
    $sb = Get-Content -LiteralPath (Join-Path $b "summary.tsv")

    # "sets" is every set the run RECORDED; "verdicts" only those that gave one.
    # The coverage warning below compares the first: it used to compare the
    # second, so a recording made before a parser fix (which read fewer
    # verdicts from the same output) was reported as covering different sets
    # while every per-set output was identical (review, 2026-11-22).
    function Totals($lines) {
        $v = 0; $i = 0; $n = 0; $rows = 0
        foreach ($l in $lines) {
            $f = $l -split "`t"
            if ($f.Count -ge 3) { $rows++ }
            if ($f.Count -ge 3 -and $f[1] -ne 'NA') { $v += [int]$f[1]; $i += [int]$f[2]; $n++ }
        }
        return @{ sets = $rows; verdicts = $n; valid = $v; invalid = $i }
    }
    $ta = Totals $sa
    $tb = Totals $sb
    Write-Host ""
    Write-Host "########## corpus comparison ##########"
    Write-Host ("  {0,-28} {1,4} sets ({2,4} with a verdict)  {3,6} valid  {4,5} invalid" -f (Split-Path -Leaf $a), $ta.sets, $ta.verdicts, $ta.valid, $ta.invalid)
    Write-Host ("  {0,-28} {1,4} sets ({2,4} with a verdict)  {3,6} valid  {4,5} invalid" -f (Split-Path -Leaf $b), $tb.sets, $tb.verdicts, $tb.valid, $tb.invalid)
    if ($ta.sets -ne $tb.sets) {
        Write-Host ("  THESE TWO RUNS DID NOT COVER THE SAME SETS ({0} vs {1})." -f $ta.sets, $tb.sets) -ForegroundColor Red
        Write-Host "     A comparison across different set lists is not a differential. Every set" -ForegroundColor Red
        Write-Host "     present in only one of the two runs is listed as a difference below." -ForegroundColor Red
    } elseif ($ta.verdicts -ne $tb.verdicts) {
        Write-Host ("  The same sets, but {0} vs {1} gave a verdict: either a set's output changed (listed" -f $ta.verdicts, $tb.verdicts) -ForegroundColor Yellow
        Write-Host "     below), or the two were recorded by run-corpus.ps1 versions that read verdicts" -ForegroundColor Yellow
        Write-Host "     differently. The per-set comparison below is what decides." -ForegroundColor Yellow
    }

    $differing = @()
    # The per-set stdout is the real comparison: it names WHICH levels were
    # judged invalid, so a swap of one valid for one invalid -- which the totals
    # would hide completely -- shows up here.
    #
    # 🔴 THE UNION OF BOTH LISTINGS, NOT $a's ALONE. This loop used to walk only
    # $a, which left it blind in one direction: a set recorded by the NEW run but
    # absent from the baseline was never looked at, and the run still printed
    # "IDENTICAL ... No recorded solution changed its verdict."
    #
    # Found 2026-09-14 while rehearsing the corpus CI job, by accident -- a
    # 6-set baseline compared against a 289-set head run reported IDENTICAL
    # over 6. The set totals are printed directly above and were never compared
    # to each other. -SelfTest now covers both directions.
    $aOut = @(Get-ChildItem -LiteralPath $a -Filter *.out -File | ForEach-Object { $_.Name })
    $bOut = @(Get-ChildItem -LiteralPath $b -Filter *.out -File | ForEach-Object { $_.Name })
    $names = @($aOut + $bOut | Sort-Object -Unique)
    foreach ($name in $names) {
        $fa = Join-Path $a $name
        $fb = Join-Path $b $name
        if (-not (Test-Path -LiteralPath $fb)) { $differing += "$name (recorded only in $(Split-Path -Leaf $a))"; continue }
        if (-not (Test-Path -LiteralPath $fa)) { $differing += "$name (recorded only in $(Split-Path -Leaf $b))"; continue }
        $ha = (Get-FileHash -LiteralPath $fa -Algorithm SHA256).Hash
        $hb = (Get-FileHash -LiteralPath $fb -Algorithm SHA256).Hash
        if ($ha -ne $hb) { $differing += $name }
    }

    # The per-set STDERR, compared separately and advisorily. It carries the
    # warnings -- bad tiles, invalid creature locations, password mismatches --
    # so a change in what the program COMPLAINS about is a real signal about
    # parser or engine behavior, and nothing else in this repository watches it.
    #
    # Two things in that text change for reasons that are not behavior, and both
    # are normalized away or every run would report hundreds of false hits:
    #
    #   [C:/.../mslogic.c:4416]   err.c stamps __LINE__ into every message, so
    #                             adding a COMMENT to a source file moves it.
    #   tworld-corpus-<32 hex>    the scratch copy's directory name is fresh per
    #                             run and appears inside file-name messages.
    #
    # 🔴 This is ADVISORY and deliberately does NOT change the exit code. A
    # warning is not a verdict: no recorded solution's pass/fail depends on it,
    # and failing a release over a reworded message would train people to ignore
    # this script. Read it, explain it, then ship.
    $errDiffering = @()
    $aErr = @(Get-ChildItem -LiteralPath $a -Filter *.err -File | ForEach-Object { $_.Name })
    $bErr = @(Get-ChildItem -LiteralPath $b -Filter *.err -File | ForEach-Object { $_.Name })
    $errNames = @($aErr + $bErr | Sort-Object -Unique)
    foreach ($name in $errNames) {
        $fa = Join-Path $a $name
        $fb = Join-Path $b $name
        if (-not (Test-Path -LiteralPath $fb)) { $errDiffering += "$name (recorded only in $(Split-Path -Leaf $a))"; continue }
        if (-not (Test-Path -LiteralPath $fa)) { $errDiffering += "$name (recorded only in $(Split-Path -Leaf $b))"; continue }
        $na = ((Get-Content -LiteralPath $fa -Raw) -replace '\[[^\]]*\.c:\d+\]', '[SRC]') -replace 'tworld-corpus-[0-9a-f]{32}', 'tworld-corpus-SCRATCH'
        $nb = ((Get-Content -LiteralPath $fb -Raw) -replace '\[[^\]]*\.c:\d+\]', '[SRC]') -replace 'tworld-corpus-[0-9a-f]{32}', 'tworld-corpus-SCRATCH'
        if ($na -ne $nb) { $errDiffering += $name }
    }
    $errCount = $errNames.Count
    Write-Host ""
    if ($differing.Count -eq 0) {
        Write-Host ("  IDENTICAL: 0 of {0} per-set outputs differ" -f $names.Count) -ForegroundColor Green
        Write-Host "  No recorded solution changed its verdict."
    Write-Host ""
    if ($errDiffering.Count -eq 0) {
        Write-Host ("  warnings: identical across all {0} set(s)" -f $errCount) -ForegroundColor Green
    } else {
        Write-Host ("  warnings: {0} of {1} set(s) print something different (ADVISORY, not a verdict):" -f $errDiffering.Count, $errCount) -ForegroundColor Yellow
        foreach ($d in ($errDiffering | Select-Object -First 12)) {
            Write-Host "    $d" -ForegroundColor Yellow
        }
        if ($errDiffering.Count -gt 12) {
            Write-Host ("    ... and {0} more" -f ($errDiffering.Count - 12)) -ForegroundColor Yellow
        }
        Write-Host "  Explain these before shipping. They are not desyncs, but they are a behavior change."
    }

        return 0
    }
    Write-Host ("  {0} set(s) DIFFER:" -f $differing.Count) -ForegroundColor Red
    foreach ($d in $differing) {
        Write-Host "    $d" -ForegroundColor Red
        $fa = Join-Path $a $d
        $fb = Join-Path $b $d
        if ((Test-Path -LiteralPath $fa) -and (Test-Path -LiteralPath $fb)) {
            # @( ): an EMPTY .out reads as $null, which Compare-Object refuses (a
            # set that went from an error to a verdict showed a PowerShell error
            # here instead of its diff; review, 2026-11-22).
            $diff = Compare-Object @(Get-Content -LiteralPath $fa) @(Get-Content -LiteralPath $fb) | Select-Object -First 6
            foreach ($line in $diff) {
                Write-Host ("      {0} {1}" -f $line.SideIndicator, $line.InputObject) -ForegroundColor Yellow
            }
        }
    }
    Write-Host ""
    Write-Host "  A difference here means a recorded solution changed its verdict." -ForegroundColor Red
    Write-Host "  That is a desync. Do not ship it without understanding exactly why." -ForegroundColor Red
    Write-Host ""
    if ($errDiffering.Count -eq 0) {
        Write-Host ("  warnings: identical across all {0} set(s)" -f $errCount) -ForegroundColor Green
    } else {
        Write-Host ("  warnings: {0} of {1} set(s) print something different (ADVISORY, not a verdict):" -f $errDiffering.Count, $errCount) -ForegroundColor Yellow
        foreach ($d in ($errDiffering | Select-Object -First 12)) {
            Write-Host "    $d" -ForegroundColor Yellow
        }
        if ($errDiffering.Count -gt 12) {
            Write-Host ("    ... and {0} more" -f ($errDiffering.Count - 12)) -ForegroundColor Yellow
        }
        Write-Host "  Explain these before shipping. They are not desyncs, but they are a behavior change."
    }

    return 1
}

# ---------------------------------------------------------------- self test --
#
# 🔴 WHY THIS EXISTS. Compare-Runs walked only $a's listing, so a set recorded
# by the NEW run but absent from the baseline was never examined -- and the run
# still printed "IDENTICAL ... No recorded solution changed its verdict." A
# 6-set baseline compared against a 289-set head run reported IDENTICAL over 6,
# with the two set totals printed one line above each other and never compared.
# Case 4 below is that exact bug. Found 2026-09-14 by rehearsing the corpus CI
# job, and only by accident: the second recording was run without -Filter.
#
# This is a gate that reports on gates, so it gets its own tests.

if ($SelfTest) {
    $root = Join-Path $env:TEMP ("run-corpus-selftest-" + [guid]::NewGuid().ToString("N"))

    function New-FakeRun([string]$dir, [hashtable]$sets) {
        New-Item -ItemType Directory -Path $dir -Force | Out-Null
        $lines = @()
        foreach ($k in @($sets.Keys | Sort-Object)) {
            $s = $sets[$k]
            [IO.File]::WriteAllText((Join-Path $dir "$k.out"), $s.out)
            [IO.File]::WriteAllText((Join-Path $dir "$k.err"), $s.err)
            $lines += ("{0}`t{1}`t{2}`t0" -f $k, $s.valid, $s.invalid)
        }
        [IO.File]::WriteAllText((Join-Path $dir "summary.tsv"), (($lines -join "`r`n") + "`r`n"))
    }
    # $valid/$invalid untyped: a summary row can hold "NA" (no verdict).
    function Rec([string]$out, [string]$err, $valid, $invalid) {
        return @{ out = $out; err = $err; valid = $valid; invalid = $invalid }
    }

    $okOut  = "  Valid solutions: 149`r`nInvalid solutions:   0`r`n"
    $badOut = "  Valid solutions: 148`r`nInvalid solutions:   1`r`n"
    $hexA   = "a" * 32
    $hexB   = "b" * 32

    # -Compare is the OLDER run and -Against the newer, matching the recording
    # path's own `Compare-Runs (Resolve-Out $Against) $outDir`. So in every case
    # here $a is "baseline" and $b is "head".
    $cases = @(
        @{ name = "identical runs pass"
           a = @{ "S1" = (Rec $okOut "warn`r`n" 149 0); "S2" = (Rec $okOut "" 38 0) }
           b = @{ "S1" = (Rec $okOut "warn`r`n" 149 0); "S2" = (Rec $okOut "" 38 0) }
           code = 0; expect = "IDENTICAL: 0 of 2 per-set outputs differ" },

        @{ name = "a changed verdict fails"
           a = @{ "S1" = (Rec $okOut  "" 149 0) }
           b = @{ "S1" = (Rec $badOut "" 148 1) }
           code = 1; expect = "1 set(s) DIFFER" },

        @{ name = "a set the NEW run did not record fails"
           a = @{ "S1" = (Rec $okOut "" 149 0); "S2" = (Rec $okOut "" 38 0) }
           b = @{ "S1" = (Rec $okOut "" 149 0) }
           code = 1; expect = "S2.out (recorded only in baseline)" },

        @{ name = "a set ONLY the new run recorded fails (the 2026-09-14 regression)"
           a = @{ "S1" = (Rec $okOut "" 149 0) }
           b = @{ "S1" = (Rec $okOut "" 149 0); "S2" = (Rec $okOut "" 38 0) }
           code = 1; expect = "S2.out (recorded only in head)" },

        @{ name = "the same gap is reported for the warning stream too"
           a = @{ "S1" = (Rec $okOut "" 149 0) }
           b = @{ "S1" = (Rec $okOut "" 149 0); "S2" = (Rec $okOut "" 38 0) }
           code = 1; expect = "S2.err (recorded only in head)" },

        @{ name = "mismatched set counts are called out by name"
           a = @{ "S1" = (Rec $okOut "" 149 0) }
           b = @{ "S1" = (Rec $okOut "" 149 0); "S2" = (Rec $okOut "" 38 0) }
           code = 1; expect = "THESE TWO RUNS DID NOT COVER THE SAME SETS (1 vs 2)" },

        # The same set, the same output, but the OLDER recording's parser read no
        # verdict from it (the %4d bug). Coverage is the same, so this is the
        # yellow advisory and a pass -- not the red warning, and not a failure.
        @{ name = "fewer verdicts from identical outputs is advisory, not a coverage gap"
           a = @{ "S1" = (Rec $okOut "" "NA" "0") }
           b = @{ "S1" = (Rec $okOut "" 149 0) }
           code = 0; expect = "The same sets, but 0 vs 1 gave a verdict" },

        @{ name = "differing warnings are advisory, not a verdict"
           a = @{ "S1" = (Rec $okOut "bad tile at 4,5`r`n" 149 0) }
           b = @{ "S1" = (Rec $okOut "bad tile at 6,7`r`n" 149 0) }
           code = 0; expect = "warnings: 1 of 1 set(s) print something different" },

        @{ name = "a moved __LINE__ is normalized away"
           a = @{ "S1" = (Rec $okOut "[C:/r/mslogic.c:4416] bad tile`r`n" 149 0) }
           b = @{ "S1" = (Rec $okOut "[C:/r/mslogic.c:4501] bad tile`r`n" 149 0) }
           code = 0; expect = "warnings: identical across all 1 set(s)" },

        @{ name = "the scratch directory name is normalized away"
           a = @{ "S1" = (Rec $okOut "cannot read tworld-corpus-$hexA\x.dat`r`n" 149 0) }
           b = @{ "S1" = (Rec $okOut "cannot read tworld-corpus-$hexB\x.dat`r`n" 149 0) }
           code = 0; expect = "warnings: identical across all 1 set(s)" }
    )

    Write-Host ""
    Write-Host "########## run-corpus self test ##########"
    $failures = 0
    $i = 0
    foreach ($c in $cases) {
        $i++
        $da = Join-Path $root ("case$i\baseline")
        $db = Join-Path $root ("case$i\head")
        New-FakeRun $da $c.a
        New-FakeRun $db $c.b
        $text = (& powershell -ExecutionPolicy Bypass -File $PSCommandPath -Compare $da -Against $db 2>&1 | Out-String)
        $code = $LASTEXITCODE
        if ($code -eq $c.code -and $text.Contains($c.expect)) {
            Write-Host ("  ok    {0}" -f $c.name) -ForegroundColor Green
        } else {
            $failures++
            Write-Host ("  FAIL  {0}" -f $c.name) -ForegroundColor Red
            Write-Host ("        expected exit {0}, got {1}" -f $c.code, $code) -ForegroundColor Red
            Write-Host ("        expected to see: {0}" -f $c.expect) -ForegroundColor Red
            Write-Host $text
        }
    }

    # The RECORDING's parser, Read-BatchVerdict (corpus-folder.ps1). tworld.c
    # pads the count with %4d, so from 1,000 up there is no space after the
    # colon; requiring one left Joshie (1,325) and Voting-CCLP5 (1,695) out of
    # every total until 2026-11-22. Case 2 is that exact line.
    $parses = @(
        @{ name = "a padded verdict is read"
           text = "  Valid solutions: 149`r`nInvalid solutions:   0`r`n"; valid = 149; invalid = 0 },
        @{ name = "a 4-digit verdict is read (no space after the colon)"
           text = "  Valid solutions:1325`r`nInvalid solutions:1001`r`n"; valid = 1325; invalid = 1001 },
        @{ name = "no solutions at all is no verdict"
           text = "No solutions were found.`r`n"; valid = $null; invalid = $null },
        # Real output (Cybersmack I): the verdict comes AFTER the invalid-level
        # lines. A pattern that lost its (?m) passed the three cases above and
        # dropped 152 of 323 sets on the real corpus (review, reproduced).
        @{ name = "a verdict after invalid-level lines is read"
           text = "Solution for level 88 is invalid`r`n  Valid solutions:  99`r`nInvalid solutions:   1`r`n"; valid = 99; invalid = 1 }
    )
    foreach ($c in $parses) {
        $v = Read-BatchVerdict $c.text
        if ($v.Valid -eq $c.valid -and $v.Invalid -eq $c.invalid) {
            Write-Host ("  ok    {0}" -f $c.name) -ForegroundColor Green
        } else {
            $failures++
            Write-Host ("  FAIL  {0}" -f $c.name) -ForegroundColor Red
            Write-Host ("        expected valid={0} invalid={1}, got valid={2} invalid={3}" -f
                        $c.valid, $c.invalid, $v.Valid, $v.Invalid) -ForegroundColor Red
        }
    }
    $total = $cases.Count + $parses.Count

    Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue
    Write-Host ""
    if ($failures) {
        Write-Host ("  {0} of {1} self test(s) FAILED" -f $failures, $total) -ForegroundColor Red
        exit 1
    }
    Write-Host ("  all {0} self test(s) passed" -f $total) -ForegroundColor Green
    exit 0
}

if ($Compare) {
    if (-not $Against) { throw "-Compare needs -Against as the other run" }
    exit (Compare-Runs (Resolve-Out $Compare) (Resolve-Out $Against))
}

# ------------------------------------------------------------------- record --

if (-not $Exe) { throw "give -Exe the build to verify, or use -Compare with -Against" }
$exePath = if ([IO.Path]::IsPathRooted($Exe)) { $Exe } else { Join-Path $repo $Exe }
if (-not (Test-Path -LiteralPath $exePath -PathType Leaf)) { throw "no executable at $exePath" }
if (-not $Out) { throw "give -Out a directory to record this run into" }
$outDir = Resolve-Out $Out

# 🔴 Start-Process ALSO READS -RedirectStandardOutput, -RedirectStandardError AND
# -WorkingDirectory AS WILDCARD PATHS: one holding [ or ] throws "Unable to find
# the specified file", "Continue" passes over it, and that set gives no verdict in
# BOTH builds and compares IDENTICAL -- the Cybersmack class again (review,
# measured). So a recording's file names replace [ and ] (a set may be named
# anything), and the two folders, which this script or its caller chooses, are
# refused outright, before anything is copied, rather than losing every set.
function Assert-NoWildcard([string]$what, [string]$d) {
    if ($d -match '[\[\]]') { throw "a [ or ] in $what '$d' would make Start-Process lose every set; use a folder without one" }
}
Assert-NoWildcard "-Out" $outDir

# A dynamic build needs Qt's DLLs; harmless for a static one.
. (Join-Path $PSScriptRoot "..\msys-env.ps1")   # the environment, from docs\toolchain.lock
$mingwBin = $MsysEnv.Bin
if (Test-Path $mingwBin) {
    Set-PathFirst $mingwBin   # msys-env.ps1: exact, and moves an existing copy to the front (jc-62)
}

if ($ScratchCorpus) {
    # ABSOLUTE, because it is also the child's working directory: a relative
    # "scratch" would have tworld look in scratch\scratch\sets (review). Checked
    # for wildcards AFTER, since the current directory can bring the [ in.
    $scratch = ConvertTo-FullCorpusPath $ScratchCorpus
    Assert-NoWildcard "-ScratchCorpus" $scratch
    $problem = Get-CorpusProblem $scratch
    if ($problem) { throw "-ScratchCorpus: $problem" }
    Write-Host "reusing scratch corpus: $scratch"
} else {
    if (-not $Corpus) { throw "no -Corpus given, no TWORLD_CORPUS, and no user profile to find one in" }
    # corpus-folder.ps1 asks for the folder itself before any Join-Path: on a
    # missing drive Join-Path errors, "Continue" carried on, and this run once
    # verified nothing and exited 0 (measured 2026-11-22).
    $Corpus = ConvertTo-FullCorpusPath $Corpus   # the copies below use it too
    $problem = Get-CorpusProblem $Corpus
    if ($problem) { throw "-Corpus: $problem" }
    $scratch = Join-Path ([IO.Path]::GetTempPath()) ("tworld-corpus-" + [Guid]::NewGuid().ToString("N"))
    Assert-NoWildcard "the scratch folder (from TEMP)" $scratch
    Write-Host "copying the corpus to $scratch (the original is never touched)..."
    # 🔴 A COPY THAT FAILS PARTWAY STOPS THE RUN. Under "Continue" a failed copy --
    # a locked file, an online-only Dropbox file that cannot download -- printed an
    # error and went on, and every set it missed was skipped as "no matching .dac"
    # inside a run that exited 0 (review, 2026-11-22). -ErrorAction Stop, and a
    # catch, because a try without one fails open here (CLAUDE.md section 8.1).
    try {
        New-Item -ItemType Directory -Force -Path $scratch -ErrorAction Stop | Out-Null
        foreach ($sub in @("sets", "data")) {
            Copy-Item -LiteralPath (Join-Path $Corpus $sub) -Destination (Join-Path $scratch $sub) -Recurse -Force -ErrorAction Stop
        }
        New-Item -ItemType Directory -Force -Path (Join-Path $scratch "save") -ErrorAction Stop | Out-Null
        Get-ChildItem -LiteralPath (Join-Path $Corpus "save") -Filter *.tws -File -ErrorAction Stop |
            Copy-Item -Destination (Join-Path $scratch "save") -Force -ErrorAction Stop
    } catch {
        Remove-Item -LiteralPath $scratch -Recurse -Force -ErrorAction SilentlyContinue
        Write-Host ("  THE CORPUS COPY FAILED, so nothing was verified: {0}" -f $_.Exception.Message) -ForegroundColor Red
        exit 1
    }
}

# res\ is read-only to the program and large; the repo's own copy is fine.
$resDir = Join-Path $repo "res"
$setsDir = Join-Path $scratch "sets"
$dataDir = Join-Path $scratch "data"
$saveDir = Join-Path $scratch "save"

$tws = @(Get-ChildItem -LiteralPath $saveDir -Filter *.tws -File | Sort-Object Name)
if ($Filter) { $tws = @($tws | Where-Object { $_.Name -like "*$Filter*" }) }

# Two sets that differ only by [ ] against _ would share one recording, and the
# second would silently overwrite the first (review). Refused before anything is
# replayed and before -Out is cleared, so a good recording there survives; only
# sets that will actually be replayed (they have a .dac) count.
# 🔴 THE SET IS NAMED AS ITS .dac IS SPELLED ON DISK, NOT AS ITS .tws IS. Tile
# World picks the set with strcmp (series.c), so "CC1.dat-ms.dac" matches nothing
# when the file is CC1.DAT-ms.dac -- while Windows opens the .tws under either
# spelling, which is why the game itself never noticed. Five saves in the
# collection differ from their .dac only by case (CC1 MS and Lynx, IsaacD1-Old,
# No_Good_Can_Come_of_This, QuicksForPieGuy): "no such data file" in every build,
# so this gate had never verified them, CC1 included (desktop goose/gander pass,
# 2026-11-22). A PowerShell hashtable's keys ignore case, so this lookup is what
# finds the real spelling; the RECORDING keeps the .tws's name, so recordings
# made before and after compare set for set.
$dacNames = @{}
foreach ($d in @(Get-ChildItem -LiteralPath $setsDir -Filter *.dac -File)) { $dacNames[$d.Name] = $d.Name }

$recNames = @{}
foreach ($t in $tws) {
    $b = $t.Name -replace '\.tws$', ''
    if (-not $dacNames.ContainsKey($b)) { continue }
    $r = $b -replace '[\[\]]', '_'
    if ($recNames.ContainsKey($r)) {
        $where = if ($KeepScratch -or $ScratchCorpus) { " (the scratch corpus is at $scratch)" } else { "" }
        if (-not $KeepScratch -and -not $ScratchCorpus) { Remove-Item -LiteralPath $scratch -Recurse -Force -ErrorAction SilentlyContinue }
        throw "'$b' and '$($recNames[$r])' would share the recording name '$r'$where"
    }
    $recNames[$r] = $b
}

if (Test-Path -LiteralPath $outDir) { Get-ChildItem -LiteralPath $outDir -File | Remove-Item -Force }
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$summary = Join-Path $outDir "summary.tsv"
Set-Content -LiteralPath $summary -Value "" -NoNewline

Write-Host ("$($tws.Count) solution file(s) to check against " + (Split-Path -Leaf $exePath))

# 🔴 Start-Process JOINS -ArgumentList WITH SPACES AND QUOTES NOTHING (Windows
# PowerShell 5.1), so every path and set name is quoted here. Unquoted, the set
# "Cybersmack I.dat-ms.dac" reached tworld as "Cybersmack" plus a stray argument:
# "no such data file", no verdict, and this gate had never verified that set
# (found 2026-11-22, from a review's guess). None of these ends in a backslash,
# which would escape the closing quote.
function ConvertTo-QuotedArg([string]$s) { '"' + $s + '"' }

$n = 0; $noDac = 0; $totalValid = 0; $totalInvalid = 0; $withVerdict = 0; $noVerdict = @()
foreach ($t in $tws) {
    $base = $t.Name -replace '\.tws$', ''       # e.g. CCLP1.dat-ms.dac
    if (-not $dacNames.ContainsKey($base)) { $noDac++; continue }
    $dac = $dacNames[$base]                      # the .dac's own spelling: see $dacNames
    $n++

    $rec = $base -replace '[\[\]]', '_'          # the recording's file name: see Assert-NoWildcard
    $o = Join-Path $outDir "$rec.out"
    $e = Join-Path $outDir "$rec.err"
    # Start-Process -Wait: see trap 1 in the header.
    $argv = @("-b", "-r", "-R", $resDir, "-L", $setsDir, "-D", $dataDir, "-S", $saveDir) | ForEach-Object {
        if ($_.StartsWith("-")) { $_ } else { ConvertTo-QuotedArg $_ }
    }
    $argv += ConvertTo-QuotedArg $dac
    $p = Start-Process -FilePath $exePath -WorkingDirectory $scratch -NoNewWindow -Wait -PassThru `
         -ArgumentList $argv -RedirectStandardOutput $o -RedirectStandardError $e

    $text = Get-Content -LiteralPath $o -Raw -ErrorAction SilentlyContinue
    if ($null -eq $text) { $text = "" }
    # Read-BatchVerdict (corpus-folder.ps1): "  Valid solutions:%4d" has no space
    # at all from 1,000 up, which left Joshie and Voting-CCLP5 out of every total.
    $verdict = Read-BatchVerdict $text
    $valid   = if ($null -ne $verdict.Valid)   { [string]$verdict.Valid }   else { "NA" }
    $invalid = if ($null -ne $verdict.Invalid) { [string]$verdict.Invalid } else { "NA" }
    if ($valid -ne "NA")   { $totalValid += [int]$valid; $withVerdict++ } else { $noVerdict += $base }
    if ($invalid -ne "NA") { $totalInvalid += [int]$invalid }

    Add-Content -LiteralPath $summary -Value ("{0}`t{1}`t{2}`t{3}" -f $base, $valid, $invalid, $p.ExitCode)
    if ($n % 50 -eq 0) { Write-Host "  ... $n sets" }
}

Write-Host ""
Write-Host "########## corpus run ##########"
# "Replayed" counts sets launched, "verified" only those that gave a verdict:
# the first number overstated coverage when it was the only one (review).
Write-Host ("  {0} set(s) replayed, {1} solution file(s) had no matching .dac" -f $n, $noDac)
Write-Host ("  {0} set(s) verified (gave a verdict); {1} gave none (their .err says why)" -f $withVerdict, ($n - $withVerdict))
Write-Host ("  {0} valid, {1} invalid" -f $totalValid, $totalInvalid)
# ⚠ NAMED, because a set that gives no verdict in BOTH builds compares IDENTICAL:
# that is how Cybersmack I went unverified. A few never can (no data file), so
# this cannot fail the run; a new name here is the signal to read its .err.
if ($noVerdict.Count) { Write-Host ("  no verdict: {0}" -f ($noVerdict -join ", ")) }
Write-Host ("  recorded in {0}" -f $outDir)

if (-not $KeepScratch -and -not $ScratchCorpus) {
    Remove-Item -LiteralPath $scratch -Recurse -Force -ErrorAction SilentlyContinue
} elseif ($KeepScratch) {
    Write-Host ("  scratch corpus kept at {0} (pass -ScratchCorpus to reuse it)" -f $scratch)
}

# 🔴 A RUN THAT VERIFIED NOTHING IS NOT A PASS. A corpus on a missing drive used
# to reach this line having copied nothing, print "0 set(s) verified" and exit 0
# (measured 2026-11-22) -- trap 2's "no solutions at all" again, one level up.
# It counts sets that gave a VERDICT ("Valid solutions: N"), not sets launched:
# an exe that cannot start fails every set the same way, and two such builds
# compare IDENTICAL (review). Some sets legitimately give none (no data file),
# so it is "none at all" that fails, a -Filter that matches nothing included.
if ($withVerdict -eq 0) {
    Write-Host "  NO SET GAVE A VERDICT. This run checked nothing, which is not a pass." -ForegroundColor Red
    exit 1
}

if ($Against) {
    exit (Compare-Runs (Resolve-Out $Against) $outDir)
}
exit 0
