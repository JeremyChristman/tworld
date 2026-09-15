<#
Measures how much of the engine and the file formats the unit tests actually reach.

    powershell -ExecutionPolicy Bypass -File coverage.ps1
    powershell -ExecutionPolicy Bypass -File coverage.ps1 -UpdateBaseline
    powershell -ExecutionPolicy Bypass -File coverage.ps1 -CheckBaseline

Builds the unit suite a second time with gcc's --coverage, runs it, and reads the
per-line and per-branch counts back out of gcov.

BRANCH COVERAGE IS THE NUMBER THAT MATTERS. An emulator is mostly conditionals --
"can this creature enter that tile", "is this button down", "is Chip sliding" --
and a line count flatters an unexercised switch enormously: a `switch` with
twenty arms and one exercised arm can read as 100% of its lines. The report shows
both and sorts on branches.

WHAT IS MEASURED, AND WHAT IS NOT

The UNIT layer, plus the Qt layer for oshw-qt\ ONLY. The end-to-end tests drive
a separately built executable that carries no instrumentation, so nothing they
exercise appears here -- which means these numbers UNDERSTATE what the suite as
a whole reaches. Building the whole CMake tree with --coverage and running a
batch verification would measure mslogic.c, series.c, solution.c and encoding.c
in one pass, and is the obvious next step for anyone who wants a real number.

🔴 THE QT RUN INSTRUMENTS oshw-qt\ AND NOTHING ELSE, and that boundary is the
point. A Qt test links most of the game core as well; instrumenting that too
would fold whatever the main window happens to touch into mslogic.c's and
series.c's figures, silently changing the meaning of sixteen rows that have
always meant "what the unit layer reaches". test\run-qt-tests.ps1 -Coverage
applies --coverage per source, and only under oshw-qt\.

⚠ AND IT SKIPS WITHOUT Qt. A machine with no Qt5 gets the unit rows and NO
oshw-qt rows at all -- not zeroed ones. -CheckBaseline reports those as missing
rather than as regressions, because absent is not the same as uncovered.

Test files and the fixture headers are EXCLUDED from the metric. Including them
is not merely noise: dirinput_test.c alone is 203 lines against the 33 lines of
generic/dirinput.c it tests, so counting it would let a big test file inflate the
headline figure while covering nothing new.

⚠ THERE IS NO CI GATE ON THESE NUMBERS, deliberately. Adding a test is SUPPOSED
to move them, and gating every push on a stale figure trains people to ignore a
red X. -CheckBaseline exists so that a release can assert the documented numbers
are still true; wire it in when the numbers have settled, not before.
#>
param(
    [switch]$UpdateBaseline,
    [switch]$CheckBaseline,
    [string]$Cc,
    [string]$Cxx,
    [string]$Filter,

    # Dump the per-line execution map this script already builds, as
    # file<TAB>line<TAB>hit. mutate.ps1 -Split consumes it to sort mutation
    # survivors into "no test reaches this line" and "a test runs it and does not
    # assert on it", which have completely different fixes.
    #
    # 🔴 THE MAP CONTAINS ONLY LINES GCOV HAS A RECORD FOR, and a consumer must
    # treat a MISSING line as unknown rather than as uncovered. gcov emits no
    # record for a file-scope initializer, so movelaws[] -- the array this fork's
    # headline defect indexed out of bounds -- appears nowhere in it. Calling that
    # "unreached" would quietly drop exactly the code most worth testing.
    [string]$LineMapPath,

    # Regenerate the baseline even though the tree is dirty. See Assert-CleanTree.
    [switch]$Force
)

# 🔴 A TOOL THAT REWRITES A COMMITTED TRUTH SOURCE MUST REFUSE A DIRTY TREE.
#
# These files are pins: the next run is judged against them. Regenerating one
# from a working tree nobody can reconstruct records a number that can never be
# checked again -- and worse, it LAUNDERS whatever is currently broken into the
# new baseline. An audit did exactly that: mutated the engine, watched the golden
# master go red across 342 digests, ran -Update on the dirty tree, and the gate
# was green afterwards with the mutation still in place.
#
# mutate.ps1 already had this guard and the reasoning; it was never propagated to
# the other three tools that regenerate a pin. -Force is the deliberate override,
# because there are legitimate reasons (a fixture change that genuinely moves
# every digest) and refusing outright would just get the guard deleted.
function Assert-CleanTree([string]$root, [string]$what, [bool]$force) {
    Push-Location $root
    try { $dirty = @(& git status --porcelain) } finally { Pop-Location }
    if ($dirty.Count -eq 0) { return }
    if ($force) {
        Write-Warning ("{0} on a DIRTY tree ({1} changed paths), because -Force was given. Whatever is uncommitted is being baked into the pin." -f $what, $dirty.Count)
        return
    }
    throw ("$what refused: the working tree is dirty ($($dirty.Count) changed paths), so this pin could never be reproduced -- and anything currently broken would be laundered into it. Commit or stash first, or pass -Force if you mean it.")
}

$ErrorActionPreference = "Continue"
$root = $PSScriptRoot
$baselineFile = Join-Path $root "docs\coverage-baseline.tsv"

function Resolve-Tool([string]$explicit, [string]$name) {
    if ($explicit) {
        if (-not (Test-Path $explicit)) { throw "not found: $explicit" }
        return $explicit
    }
    $onPath = Get-Command $name -ErrorAction SilentlyContinue
    if ($onPath) { return $onPath.Source }
    $msys = Join-Path "C:\msys64\mingw64\bin" "$name.exe"
    if (Test-Path $msys) { return $msys }
    throw "no $name found: put it on PATH, install MSYS2 at C:\msys64, or pass -Cc/-Cxx"
}

$Cc = Resolve-Tool $Cc "gcc"
$Cxx = Resolve-Tool $Cxx "g++"
$gcov = Resolve-Tool $null "gcov"

$mingwBin = Split-Path -Parent $Cc
$segments = $env:Path -split ';'
if ($segments -notcontains $mingwBin) { $env:Path = "$mingwBin;$env:Path" }

# A private directory per run. The .gcno lands beside the executable at compile
# time and the .gcda is written next to it when the binary runs, so the runner is
# pointed here with -OutDir and everything stays together.
$work = Join-Path ([IO.Path]::GetTempPath()) ("tworld-cov-" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Force -Path $work | Out-Null

try {
    Write-Host "building and running the unit suite with --coverage..." -ForegroundColor Cyan
    $args = @("-ExecutionPolicy", "Bypass", "-File", (Join-Path $root "test\run-tests.ps1"),
              "-Coverage", "-OutDir", $work)
    if ($Filter) { $args += @("-Filter", $Filter) }
    & powershell @args | Out-Null
    if ($LASTEXITCODE -ne 0) {
        # Deliberately not fatal. A coverage report for a suite with a failing
        # test is still a useful report, and pretending otherwise means nobody
        # can measure anything until everything is green.
        Write-Warning "the unit suite FAILED under coverage instrumentation. The numbers below still describe what ran, but treat them as provisional."
    }

    # And the Qt layer, which is the only thing that reaches oshw-qt\ at all.
    # Same -Coverage/-OutDir contract; it instruments ONLY the oshw-qt sources,
    # so the sixteen rows above keep meaning "what the unit layer reaches".
    #
    # ⚠ IT SKIPS CLEANLY WITHOUT Qt and exits 0 when it does, so a machine with
    # no Qt5 still gets the unit numbers -- and gets no oshw-qt rows, which
    # -CheckBaseline would then report as missing rather than as zero. That is
    # the honest outcome: absent is not the same as uncovered.
    Write-Host "building and running the Qt layer with --coverage..." -ForegroundColor Cyan
    $qtArgs = @("-ExecutionPolicy", "Bypass", "-File", (Join-Path $root "test\run-qt-tests.ps1"),
                "-Coverage", "-OutDir", $work)
    if ($Filter) { $qtArgs += @("-Filter", $Filter) }
    & powershell @qtArgs | Out-Null
    if ($LASTEXITCODE -ne 0) {
        Write-Warning "the Qt layer FAILED under coverage instrumentation. Same caveat as above."
    }

    # -Recurse: run-tests.ps1 puts its objects straight in $work, but run-qt-tests.ps1
    # gives each test its own subdirectory.
    $gcdaFiles = @(Get-ChildItem -LiteralPath $work -Filter *.gcda -File -Recurse -ErrorAction SilentlyContinue)
    if ($gcdaFiles.Count -eq 0) {
        throw "no .gcda files were produced in $work -- the instrumented binaries did not run"
    }
    Write-Host ("collected {0} coverage data file(s)" -f $gcdaFiles.Count)

    # ⚠ gcov MUST run with the repository root as the working directory. The
    # paths recorded in the .gcno are relative to where the compiler ran, so from
    # anywhere else gcov reports "Cannot open source file generic/dirinput.c" --
    # and then prints numbers anyway, having never read the source. Silent, and
    # wrong in the flattering direction.
    # Union the per-line and per-branch counts across every run. The C and C++
    # builds of a test are separate translation units with separate .gcda, so a
    # line reached only by the C++ build must still count as reached -- taking
    # one run's numbers, or the maximum of the two, would both be wrong.
    $lineHit = @{}   # "file:line"   -> covered anywhere
    $lineAll = @{}
    $brHit   = @{}   # "file:line:i" -> taken anywhere
    $brAll   = @{}
    $jsonSeen = 0

    # 🔴 EACH gcov RUN IS DRAINED BEFORE THE NEXT ONE STARTS, and that is the
    # whole reason this loop is shaped this way rather than "run them all, then
    # read them all".
    #
    # gcov names its output after the SOURCE file -- CCMetaData.cpp.gcov.json.gz
    # -- not after the .gcda. Two translation units that compiled the same source
    # therefore write the SAME filename, and the second silently overwrites the
    # first. Reading them afterwards unions whatever happened to survive.
    #
    # Measured: CCMetaData.cpp is built into both ccmetadata_test and
    # mainwnd_test. Run alone it reports 92.1% of lines; in the combined run it
    # reported 8.8%, because mainwnd_test's gcov output landed last. The same
    # collision applies to the C and C++ builds of any one unit test -- which is
    # exactly the union the comment above has always claimed to perform.
    Push-Location $root
    try {
        foreach ($gcda in $gcdaFiles) {
            # The .gcno sits beside its .gcda, which is not always $work itself.
            & $gcov --json-format --branch-probabilities --object-directory $gcda.DirectoryName $gcda.FullName 2>&1 | Out-Null

            # gcov writes into the CURRENT directory, which is $root -- so these
            # land in the repository and must not be left behind.
            $jsonFiles = @(Get-ChildItem -LiteralPath $root -Filter *.gcov.json.gz -File -ErrorAction SilentlyContinue)
            foreach ($jf in $jsonFiles) {
                ++$jsonSeen
                $inStream = [IO.File]::OpenRead($jf.FullName)
                $gz = New-Object IO.Compression.GZipStream($inStream, [IO.Compression.CompressionMode]::Decompress)
                $reader = New-Object IO.StreamReader($gz)
                $text = $reader.ReadToEnd()
                $reader.Close(); $gz.Close(); $inStream.Close()

                $data = $text | ConvertFrom-Json
                foreach ($file in $data.files) {
                    $name = ($file.file -replace '\\', '/')
                    # Anything outside this repository (system headers) and the
                    # test scaffolding itself. See the header for why the tests
                    # are excluded.
                    if ($name -match '^[A-Za-z]:/' -and $name -notmatch [regex]::Escape(($root -replace '\\','/'))) { continue }
                    if ($name -match '(^|/)test/') { continue }
                    if ($name -match '_test\.c$' -or $name -match 'tw_test\.h$' -or $name -match 'tw_fixture\.h$') { continue }
                    $short = $name -replace [regex]::Escape(($root -replace '\\','/') + '/'), ''

                    foreach ($line in $file.lines) {
                        $key = "$short`:$($line.line_number)"
                        $lineAll[$key] = $true
                        if ($line.count -gt 0) { $lineHit[$key] = $true }
                        $i = 0
                        foreach ($b in $line.branches) {
                            $bkey = "$key`:$i"
                            $brAll[$bkey] = $true
                            if ($b.count -gt 0) { $brHit[$bkey] = $true }
                            ++$i
                        }
                    }
                }
            }
            if ($jsonFiles.Count -gt 0) {
                Remove-Item -LiteralPath ($jsonFiles | ForEach-Object { $_.FullName }) -Force -ErrorAction SilentlyContinue
            }
        }
    } finally {
        Pop-Location
    }

    if ($jsonSeen -eq 0) {
        throw "gcov produced no JSON output. Check that gcov $(& $gcov --version | Select-Object -First 1) supports --json-format."
    }

    if ($LineMapPath) {
        $mapLines = @(
            "# Per-line execution map, unioned across the C and C++ builds of every unit test.",
            "# Generated by coverage.ps1 -LineMapPath. hit=1 means some test executed the line.",
            "# A line ABSENT from this file is UNKNOWN, not uncovered: gcov emits no record for",
            "# file-scope initializers, so tables like movelaws[] never appear here.",
            "file`tline`thit"
        )
        foreach ($key in ($lineAll.Keys | Sort-Object)) {
            $f = $key -replace ':\d+$', ''
            $n = [int]($key -replace '^.*:', '')
            $h = if ($lineHit.ContainsKey($key)) { 1 } else { 0 }
            $mapLines += ("{0}`t{1}`t{2}" -f $f, $n, $h)
        }
        [IO.File]::WriteAllText($LineMapPath, (($mapLines -join "`n") + "`n"), (New-Object Text.UTF8Encoding $false))
        Write-Host "per-line map written: $LineMapPath ($($lineAll.Count) lines)"
    }

    # Roll the per-line keys up per file.
    $files = @{}
    foreach ($key in $lineAll.Keys) {
        $file = $key -replace ':\d+$', ''
        if (-not $files.ContainsKey($file)) {
            $files[$file] = [ordered]@{ lines = 0; linesHit = 0; branches = 0; branchesHit = 0 }
        }
        $files[$file].lines++
        if ($lineHit.ContainsKey($key)) { $files[$file].linesHit++ }
    }
    foreach ($bkey in $brAll.Keys) {
        $file = $bkey -replace ':\d+:\d+$', ''
        if (-not $files.ContainsKey($file)) { continue }
        $files[$file].branches++
        if ($brHit.ContainsKey($bkey)) { $files[$file].branchesHit++ }
    }

    if ($files.Count -eq 0) { throw "gcov produced JSON but no first-party source files survived filtering" }

    $rows = @()
    foreach ($file in ($files.Keys | Sort-Object)) {
        $f = $files[$file]
        $linePct = if ($f.lines) { [math]::Round(100.0 * $f.linesHit / $f.lines, 1) } else { 0 }
        $branchPct = if ($f.branches) { [math]::Round(100.0 * $f.branchesHit / $f.branches, 1) } else { 0 }
        $rows += [pscustomobject]@{
            File = $file
            Lines = $f.lines; LinesHit = $f.linesHit; LinePct = $linePct
            Branches = $f.branches; BranchesHit = $f.branchesHit; BranchPct = $branchPct
        }
    }

    Write-Host ""
    Write-Host "########## coverage (unit layer only) ##########"
    Write-Host ("  {0,-28} {1,7}  {2,8}" -f "file", "lines", "branches")
    foreach ($r in ($rows | Sort-Object -Property BranchPct)) {
        Write-Host ("  {0,-28} {1,6:N1}%  {2,6:N1}%   ({3}/{4} lines, {5}/{6} branches)" -f
            $r.File, $r.LinePct, $r.BranchPct, $r.LinesHit, $r.Lines, $r.BranchesHit, $r.Branches)
    }
    $totLines = ($rows | Measure-Object -Property Lines -Sum).Sum
    $totLinesHit = ($rows | Measure-Object -Property LinesHit -Sum).Sum
    $totBr = ($rows | Measure-Object -Property Branches -Sum).Sum
    $totBrHit = ($rows | Measure-Object -Property BranchesHit -Sum).Sum
    $overallLine = if ($totLines) { [math]::Round(100.0 * $totLinesHit / $totLines, 1) } else { 0 }
    $overallBr = if ($totBr) { [math]::Round(100.0 * $totBrHit / $totBr, 1) } else { 0 }
    Write-Host ("  {0,-28} {1,6:N1}%  {2,6:N1}%   OVERALL" -f "", $overallLine, $overallBr)
    Write-Host ""
    Write-Host "  The UNIT layer plus the Qt layer's reach into oshw-qt\; the end-to-end tests run"
    Write-Host "  an uninstrumented build, so what they reach is not counted, and the core sources"
    Write-Host "  are NOT instrumented by the Qt run. See the header of this script."
    if ($UpdateBaseline) {
        Assert-CleanTree $root "coverage.ps1 -UpdateBaseline" $Force.IsPresent
        $dir = Split-Path -Parent $baselineFile
        if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }
        $sb = New-Object Text.StringBuilder
        [void]$sb.AppendLine("# Coverage baseline. Regenerate with coverage.ps1 -UpdateBaseline.")
        [void]$sb.AppendLine("# A FLOOR, not a grade. Raising a number here is progress; lowering one needs a reason.")
        [void]$sb.AppendLine("#")
        [void]$sb.AppendLine("# The UNIT layer, plus the Qt layer for the oshw-qt/ rows -- nothing else reaches")
        [void]$sb.AppendLine("# those, and the Qt run does NOT instrument the core, so every other row still")
        [void]$sb.AppendLine("# means exactly what it always did. The end-to-end tests run an uninstrumented")
        [void]$sb.AppendLine("# build and are not counted anywhere here.")
        [void]$sb.AppendLine("file`tlines`tlinesHit`tbranches`tbranchesHit")
        foreach ($r in ($rows | Sort-Object File)) {
            [void]$sb.AppendLine(("{0}`t{1}`t{2}`t{3}`t{4}" -f $r.File, $r.Lines, $r.LinesHit, $r.Branches, $r.BranchesHit))
        }
        # LF, not CRLF. .gitattributes normalizes this file to LF anyway, so
        # writing CRLF only earns a "CRLF will be replaced by LF" warning on
        # every single commit that touches it.
        [IO.File]::WriteAllText($baselineFile, ($sb.ToString() -replace "`r`n", "`n"),
                                (New-Object Text.UTF8Encoding $false))
        Write-Host "wrote $baselineFile" -ForegroundColor Green
    }

    if ($CheckBaseline) {
        if (-not (Test-Path $baselineFile)) {
            Write-Host "no baseline at $baselineFile -- run with -UpdateBaseline first" -ForegroundColor Red
            exit 1
        }
        $baseline = @{}
        foreach ($line in (Get-Content $baselineFile)) {
            if (-not $line.Trim()) { continue }
            if ($line.StartsWith('#')) { continue }
            $parts = $line -split "`t"
            if ($parts.Count -lt 5) { continue }
            # The column header. ⚠ Tested by VALUE, not with $line.StartsWith("file`t"):
            # a backtick inside SINGLE quotes is a literal backtick, not a tab, so
            # that spelling never matched, the header row reached the cast below,
            # and [int]"branches" threw. The throw was non-terminating, so the
            # loop simply continued and -CheckBaseline reported success having
            # compared nothing -- a gate that always passes.
            if ($parts[0] -eq 'file') { continue }
            $b = 0; $bh = 0
            if (-not [int]::TryParse($parts[3], [ref]$b) -or -not [int]::TryParse($parts[4], [ref]$bh)) {
                Write-Host "unreadable row in $baselineFile : $line" -ForegroundColor Red
                Write-Host "Regenerate it with coverage.ps1 -UpdateBaseline." -ForegroundColor Yellow
                exit 1
            }
            $baseline[$parts[0]] = @{ branches = $b; branchesHit = $bh }
        }
        if ($baseline.Count -eq 0) {
            # A baseline that parsed to nothing must not read as "no regressions".
            Write-Host "no usable rows in $baselineFile -- refusing to report a pass" -ForegroundColor Red
            exit 1
        }
        $regressed = @()
        foreach ($r in $rows) {
            if (-not $baseline.ContainsKey($r.File)) { continue }
            $wasPct = if ($baseline[$r.File].branches) { 100.0 * $baseline[$r.File].branchesHit / $baseline[$r.File].branches } else { 0 }
            # Compared as a PERCENTAGE, not as a count: adding code legitimately
            # raises the branch total, and a count comparison would call that a
            # regression while coverage was actually unchanged.
            if ($r.BranchPct -lt [math]::Round($wasPct, 1) - 0.05) {
                $regressed += ("{0}: branch coverage fell from {1:N1}% to {2:N1}%" -f $r.File, $wasPct, $r.BranchPct)
            }
        }
        # 🔴 A BASELINE ROW WITH NO MEASUREMENT IS NOT A PASS. The loop above
        # walks the MEASURED rows and skips anything absent from the baseline,
        # which is right for a newly added file -- but the reverse is a hole:
        # the oshw-qt rows exist only when the Qt layer ran, and a machine where
        # it SKIPPED (no Qt5) would compare nothing for them and report success.
        # That is the "gate that always passes" shape this same function already
        # carries a note about, and it is a failure here for the same reason:
        # -CheckBaseline exists so a release can assert these numbers are still
        # true, and a number nobody measured cannot be asserted.
        $measured = @{}
        foreach ($r in $rows) { $measured[$r.File] = $true }
        $unmeasured = @($baseline.Keys | Where-Object { -not $measured.ContainsKey($_) } | Sort-Object)
        if ($unmeasured.Count -gt 0) {
            Write-Host ""
            Write-Host "these files are in the baseline but were NOT measured:" -ForegroundColor Red
            foreach ($m in $unmeasured) { Write-Host "  - $m" -ForegroundColor Red }
            Write-Host "  Causes, in order of likelihood: -Filter was given, so this run measured" -ForegroundColor Yellow
            Write-Host "  only part of the suite and cannot check a whole baseline; or the layer that" -ForegroundColor Yellow
            Write-Host "  covers them did not run -- the Qt layer SKIPS when Qt5 is absent, and the" -ForegroundColor Yellow
            Write-Host "  oshw-qt rows come only from it; or the file is gone." -ForegroundColor Yellow
            Write-Host "  Absent is not the same as uncovered, so this is NOT scored as a regression." -ForegroundColor Yellow
            Write-Host "  It is reported as an unanswered question, because -CheckBaseline exists so a" -ForegroundColor Yellow
            Write-Host "  release can assert these numbers, and a number nobody measured is not one." -ForegroundColor Yellow
            exit 1
        }
        if ($regressed.Count -gt 0) {
            Write-Host ""
            Write-Host "coverage regressed against docs\coverage-baseline.tsv:" -ForegroundColor Red
            foreach ($m in $regressed) { Write-Host "  - $m" -ForegroundColor Red }
            Write-Host "Add a test, or run -UpdateBaseline and say in the commit why the number moved." -ForegroundColor Yellow
            exit 1
        }
        Write-Host "coverage is at or above the recorded baseline" -ForegroundColor Green
    }
} catch {
    # 🔴 WITHOUT THIS, -CheckBaseline COULD PASS HAVING COMPARED NOTHING.
    # Measured 2026-09-15: one failing statement planted at the top of this block
    # and `coverage.ps1 -CheckBaseline` exited 0. A statement-terminating error
    # inside a try with no catch jumps to `finally` and resumes after the try,
    # under this script's "Continue" preference -- and after the try is `exit 0`.
    # mutate.ps1 silently lost a census baseline to exactly this shape.
    Write-Host ""
    Write-Host ("coverage.ps1 FAILED: " + $_.Exception.Message) -ForegroundColor Red
    if ($_.InvocationInfo) { Write-Host ("  at " + $_.InvocationInfo.PositionMessage) -ForegroundColor Red }
    exit 1
} finally {
    if (Test-Path $work) { Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue }
    # gcov drops these in the repository root; make sure a failure partway does
    # not leave them behind as untracked files.
    Get-ChildItem -LiteralPath $root -Filter *.gcov.json.gz -File -ErrorAction SilentlyContinue |
        Remove-Item -Force -ErrorAction SilentlyContinue
}

exit 0
