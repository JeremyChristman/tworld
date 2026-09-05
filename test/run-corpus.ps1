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

-Corpus should point at a Chip's Challenge folder containing sets\, data\ and
save\. The default is the maintainer's install. THE CORPUS IS COPIED to a
scratch directory before anything runs and the copy is what gets used, because:

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
     stdout: "Valid solutions: N".
  3. The set argument must be a BARE filename found inside -L, never a path.
  4. A .tws is named "<dacname>.tws", so the set for save\CCLP1.dat-ms.dac.tws
     is the .dac of that name. Sets with no .tws are skipped -- there is nothing
     to verify.
#>
param(
    [string]$Exe,
    [string]$Corpus = "C:\Users\Jeremy\Dropbox\Games\Computer\Chip's Challenge",
    [string]$Out,
    [string]$Against,
    [string]$Compare,
    [string]$Filter,
    # Reuse an already-copied scratch corpus instead of copying again. The copy
    # is the slow part; this makes a second build's run much faster.
    [string]$ScratchCorpus,
    [switch]$KeepScratch
)
$ErrorActionPreference = "Continue"
$repo = Split-Path -Parent $PSScriptRoot

function Resolve-Out([string]$p) {
    if (-not $p) { return $null }
    if ([IO.Path]::IsPathRooted($p)) { return $p }
    return (Join-Path $repo $p)
}

# ------------------------------------------------------------ diff two runs --

function Compare-Runs([string]$a, [string]$b) {
    foreach ($d in @($a, $b)) {
        if (-not (Test-Path (Join-Path $d "summary.tsv"))) {
            Write-Host "no summary.tsv in $d" -ForegroundColor Red
            exit 1
        }
    }
    $sa = Get-Content (Join-Path $a "summary.tsv")
    $sb = Get-Content (Join-Path $b "summary.tsv")

    function Totals($lines) {
        $v = 0; $i = 0; $n = 0
        foreach ($l in $lines) {
            $f = $l -split "`t"
            if ($f.Count -ge 3 -and $f[1] -ne 'NA') { $v += [int]$f[1]; $i += [int]$f[2]; $n++ }
        }
        return @{ sets = $n; valid = $v; invalid = $i }
    }
    $ta = Totals $sa
    $tb = Totals $sb
    Write-Host ""
    Write-Host "########## corpus comparison ##########"
    Write-Host ("  {0,-28} {1,4} sets  {2,6} valid  {3,5} invalid" -f (Split-Path -Leaf $a), $ta.sets, $ta.valid, $ta.invalid)
    Write-Host ("  {0,-28} {1,4} sets  {2,6} valid  {3,5} invalid" -f (Split-Path -Leaf $b), $tb.sets, $tb.valid, $tb.invalid)

    $differing = @()
    # The per-set stdout is the real comparison: it names WHICH levels were
    # judged invalid, so a swap of one valid for one invalid -- which the totals
    # would hide completely -- shows up here.
    foreach ($f in (Get-ChildItem -LiteralPath $a -Filter *.out -File)) {
        $other = Join-Path $b $f.Name
        if (-not (Test-Path $other)) { $differing += "$($f.Name) (missing in the other run)"; continue }
        $ha = (Get-FileHash -LiteralPath $f.FullName -Algorithm SHA256).Hash
        $hb = (Get-FileHash -LiteralPath $other -Algorithm SHA256).Hash
        if ($ha -ne $hb) { $differing += $f.Name }
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
    foreach ($f in (Get-ChildItem -LiteralPath $a -Filter *.err -File)) {
        $other = Join-Path $b $f.Name
        if (-not (Test-Path $other)) { $errDiffering += "$($f.Name) (missing in the other run)"; continue }
        $na = ((Get-Content -LiteralPath $f.FullName -Raw) -replace '\[[^\]]*\.c:\d+\]', '[SRC]') -replace 'tworld-corpus-[0-9a-f]{32}', 'tworld-corpus-SCRATCH'
        $nb = ((Get-Content -LiteralPath $other   -Raw) -replace '\[[^\]]*\.c:\d+\]', '[SRC]') -replace 'tworld-corpus-[0-9a-f]{32}', 'tworld-corpus-SCRATCH'
        if ($na -ne $nb) { $errDiffering += $f.Name }
    }
    $errCount = (Get-ChildItem -LiteralPath $a -Filter *.err -File).Count
    Write-Host ""
    if ($differing.Count -eq 0) {
        Write-Host ("  IDENTICAL: 0 of {0} per-set outputs differ" -f (Get-ChildItem -LiteralPath $a -Filter *.out -File).Count) -ForegroundColor Green
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
        if ((Test-Path $fa) -and (Test-Path $fb)) {
            $diff = Compare-Object (Get-Content $fa) (Get-Content $fb) | Select-Object -First 6
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

if ($Compare) {
    if (-not $Against) { throw "-Compare needs -Against as the other run" }
    exit (Compare-Runs (Resolve-Out $Compare) (Resolve-Out $Against))
}

# ------------------------------------------------------------------- record --

if (-not $Exe) { throw "give -Exe the build to verify, or use -Compare with -Against" }
$exePath = if ([IO.Path]::IsPathRooted($Exe)) { $Exe } else { Join-Path $repo $Exe }
if (-not (Test-Path $exePath)) { throw "no executable at $exePath" }
if (-not $Out) { throw "give -Out a directory to record this run into" }
$outDir = Resolve-Out $Out

# A dynamic build needs Qt's DLLs; harmless for a static one.
$mingwBin = "C:\msys64\mingw64\bin"
if (Test-Path $mingwBin) {
    $segments = $env:Path -split ';'
    if ($segments -notcontains $mingwBin) { $env:Path = "$mingwBin;$env:Path" }
}

if ($ScratchCorpus) {
    $scratch = $ScratchCorpus
    if (-not (Test-Path (Join-Path $scratch "save"))) { throw "-ScratchCorpus has no save\ directory: $scratch" }
    Write-Host "reusing scratch corpus: $scratch"
} else {
    foreach ($sub in @("sets", "data", "save")) {
        if (-not (Test-Path (Join-Path $Corpus $sub))) {
            throw "-Corpus has no $sub\ directory: $Corpus"
        }
    }
    $scratch = Join-Path ([IO.Path]::GetTempPath()) ("tworld-corpus-" + [Guid]::NewGuid().ToString("N"))
    Write-Host "copying the corpus to $scratch (the original is never touched)..."
    New-Item -ItemType Directory -Force -Path $scratch | Out-Null
    foreach ($sub in @("sets", "data")) {
        Copy-Item -Path (Join-Path $Corpus $sub) -Destination (Join-Path $scratch $sub) -Recurse -Force
    }
    New-Item -ItemType Directory -Force -Path (Join-Path $scratch "save") | Out-Null
    Copy-Item -Path (Join-Path $Corpus "save\*.tws") -Destination (Join-Path $scratch "save") -Force -ErrorAction SilentlyContinue
}

# res\ is read-only to the program and large; the repo's own copy is fine.
$resDir = Join-Path $repo "res"
$setsDir = Join-Path $scratch "sets"
$dataDir = Join-Path $scratch "data"
$saveDir = Join-Path $scratch "save"

if (Test-Path $outDir) { Get-ChildItem -LiteralPath $outDir -File | Remove-Item -Force }
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$summary = Join-Path $outDir "summary.tsv"
Set-Content -LiteralPath $summary -Value "" -NoNewline

$tws = @(Get-ChildItem -LiteralPath $saveDir -Filter *.tws -File | Sort-Object Name)
if ($Filter) { $tws = @($tws | Where-Object { $_.Name -like "*$Filter*" }) }
Write-Host ("$($tws.Count) solution file(s) to check against " + (Split-Path -Leaf $exePath))

$n = 0; $noDac = 0; $totalValid = 0; $totalInvalid = 0
foreach ($t in $tws) {
    $base = $t.Name -replace '\.tws$', ''       # e.g. CCLP1.dat-ms.dac
    if (-not (Test-Path (Join-Path $setsDir $base))) { $noDac++; continue }
    $n++

    $o = Join-Path $outDir "$base.out"
    $e = Join-Path $outDir "$base.err"
    # Start-Process -Wait: see trap 1 in the header.
    $p = Start-Process -FilePath $exePath -WorkingDirectory $scratch -NoNewWindow -Wait -PassThru `
         -ArgumentList @("-b", "-r", "-R", $resDir, "-L", $setsDir, "-D", $dataDir, "-S", $saveDir, $base) `
         -RedirectStandardOutput $o -RedirectStandardError $e

    $text = Get-Content -LiteralPath $o -Raw -ErrorAction SilentlyContinue
    if ($null -eq $text) { $text = "" }
    $valid = "NA"; $invalid = "NA"
    if ($text -match '(?m)^\s*Valid solutions:\s+(\d+)\s*$')   { $valid = $Matches[1] }
    if ($text -match '(?m)^Invalid solutions:\s+(\d+)\s*$')    { $invalid = $Matches[1] }
    if ($valid -ne "NA")   { $totalValid += [int]$valid }
    if ($invalid -ne "NA") { $totalInvalid += [int]$invalid }

    Add-Content -LiteralPath $summary -Value ("{0}`t{1}`t{2}`t{3}" -f $base, $valid, $invalid, $p.ExitCode)
    if ($n % 50 -eq 0) { Write-Host "  ... $n sets" }
}

Write-Host ""
Write-Host "########## corpus run ##########"
Write-Host ("  {0} set(s) verified, {1} solution file(s) had no matching .dac" -f $n, $noDac)
Write-Host ("  {0} valid, {1} invalid" -f $totalValid, $totalInvalid)
Write-Host ("  recorded in {0}" -f $outDir)

if (-not $KeepScratch -and -not $ScratchCorpus) {
    Remove-Item -LiteralPath $scratch -Recurse -Force -ErrorAction SilentlyContinue
} elseif ($KeepScratch) {
    Write-Host ("  scratch corpus kept at {0} (pass -ScratchCorpus to reuse it)" -f $scratch)
}

if ($Against) {
    exit (Compare-Runs (Resolve-Out $Against) $outDir)
}
exit 0
