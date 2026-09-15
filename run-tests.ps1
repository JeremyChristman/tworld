<#
Runs the whole test suite -- unit, sanitize, end-to-end, Qt, golden master,
NO_FIX_* matrix -- and then checks the documentation against the tree.

    powershell -ExecutionPolicy Bypass -File run-tests.ps1
    powershell -ExecutionPolicy Bypass -File run-tests.ps1 -Unit
    powershell -ExecutionPolicy Bypass -File run-tests.ps1 -Sanitize
    powershell -ExecutionPolicy Bypass -File run-tests.ps1 -E2E
    powershell -ExecutionPolicy Bypass -File run-tests.ps1 -Golden
    powershell -ExecutionPolicy Bypass -File run-tests.ps1 -NoFix
    powershell -ExecutionPolicy Bypass -File run-tests.ps1 -Docs
    powershell -ExecutionPolicy Bypass -File run-tests.ps1 -ResultsPath test-results
    powershell -ExecutionPolicy Bypass -File run-tests.ps1 -Filter random

This is the entry point. The layers underneath can also be run directly:

    test\run-tests.ps1   the unit layer -- C files that compile the source under
                         test directly, no CMake tree, no built executable.
    test\run-tests.ps1 -Sanitize   the same unit cases under UBSan, trapping.
    test\run-e2e.ps1     the end-to-end layer -- the REAL executable, driven
                         through its GUI-free command line.
    test\run-qt-tests.ps1  the oshw-qt layer (skips cleanly without Qt).
    test\run-golden.ps1  the golden-master engine snapshot.
    test\run-nofix.ps1   the NO_FIX_* differential matrix.
    verify-docs.ps1      not a test layer: the documents checked against the tree.

WHAT EACH LAYER IS FOR, so that a failure points somewhere:

  unit    a defect inside one module: the RNG's arithmetic, the .tws move codec,
          the MS engine's rules, the keyboard arbitration. Fast, and needs only
          a compiler.
  e2e     a defect in how the parts fit together, or in the program's actual
          command-line behavior: directory resolution, .dac parsing, a solution
          being replayed and judged. Needs a built executable, and builds one if
          asked with -Build.
  qt      the one parser no other layer can reach: .ccx metadata, which is read
          only when a Qt main window exists.
  golden  AN ENGINE BEHAVIOR CHANGE. All 903 committed levels through both
          engines, gamestate hashed every tick, compared against a committed
          baseline. Nothing else here can see this class at all.
  nofix   THE DESYNC MACHINERY. For each of the 32 NO_FIX_* toggles that has a
          recorded witness, proves a fix-on build and a fix-off build still
          disagree on it. These are opt-out macros, so a broken one changes no
          shipped behavior and nothing else goes red.

⚠ golden and nofix compile the engines themselves, so they need a compiler but
no built executable and no Qt. Together they take about half a minute -- nofix
is most of it, because it compiles an engine per toggle. Run them after ANY
edit to mslogic.c, lxlogic.c, encoding.c or random.c.

⚠ DO NOT TRUST A DURATION WRITTEN IN THIS FILE; READ THE SUMMARY. It prints
every layer's measured time. The comments here said "about fifteen seconds"
for golden+nofix and "roughly 30 seconds" for sanitize, and an audit timed them
at 27 and 12 -- both wrong, in opposite directions, and nothing noticed.

docs is verify-docs.ps1, run LAST because the unit layer rewrites
docs\test-counts.tsv on a complete run and the documents are checked against
that. It is here because an audit deleted a NO_FIX_* witness row and the only
gate that noticed was this one -- which nothing in the test workflow ran.

THE E2E LAYER NEEDS AN EXECUTABLE and does not build one by itself. Pass -Build
to build the dynamic-Qt flavor first, or point -Exe at one you already have. If
neither is available the e2e layer is SKIPPED with a message rather than failing,
because a missing local build is a setup state, not a defect -- but CI passes
-Build, so nothing is quietly skipped there.

Exit code is 0 only if every layer that ran passed. A layer that SKIPPED (no
executable, no Qt) does not fail the run -- a missing local setup is not a
defect -- but the summary names it and will not say "all green". The exception
is a run where NOTHING ran, e.g. `-Qt` alone on a machine without Qt: that is
exit 1, because a request for a layer that produced no verdict is not a pass.
#>
param(
    [switch]$Unit,
    [switch]$E2E,
    [switch]$Qt,
    [switch]$Golden,
    [switch]$NoFix,
    [switch]$Sanitize,
    [switch]$Docs,
    # Seconds any one layer may run before it is killed and failed. Every layer
    # takes well under a minute; this catches a hang, it is not a budget.
    [int]$LayerTimeoutSec = 900,
    [switch]$Build,
    [string]$Exe,
    [string]$Filter,
    [string]$ResultsPath,
    [ValidateSet("c", "c++", "both")]
    [string]$Lang = "both"
)
$ErrorActionPreference = "Continue"
$root = $PSScriptRoot

# No switch given means every layer. Naming one narrows to it.
#
# 🔴 GOLDEN AND NOFIX ARE IN THE DEFAULT SET ON PURPOSE, AND THIS WAS A REAL
# HOLE. Both were built specifically to catch an engine change, and for a while
# neither was reachable from this entry point: a contributor could edit
# mslogic.c, run the documented command, get a fully green suite, and never
# touch the only two layers that could have objected. CI caught it, but a green
# local run that means less than it looks is exactly the failure this repository
# treats as the serious kind (see CLAUDE.md section 3, "the traps that make a
# test or a script LIE"). They cost about half a minute together.
if (-not $Unit -and -not $E2E -and -not $Qt -and -not $Golden -and -not $NoFix -and -not $Sanitize -and -not $Docs) {
    $Unit = $true; $E2E = $true; $Qt = $true; $Golden = $true; $NoFix = $true; $Sanitize = $true; $Docs = $true
}

$failed = @()
$ran = @()
# 🔴 A SKIP IS RECORDED, NOT JUST PRINTED. The summary used to list a skipped
# layer under "layers run" and finish "all green" -- so on a machine without Qt5
# an audit broke the .ccx parser's bounds check (CCMetaData.cpp:151, which only
# the Qt layer can reach) and this script called the run all green. The message
# scrolled past four screens up. CI refuses a Qt skip outright; locally a skip
# stays exit 0, but it is named at the bottom where the verdict is read.
$skipped = @()
$timings = @()
# The Qt runner is a separate process, so it reports a skip through this file.
$env:TW_SKIP_REPORT = Join-Path ([IO.Path]::GetTempPath()) ("tw-skips-" + $PID + ".txt")
Remove-Item -LiteralPath $env:TW_SKIP_REPORT -Force -ErrorAction SilentlyContinue
$clock = [Diagnostics.Stopwatch]::StartNew()
# Consume whatever the layer that just ran reported as a skip, and clear the
# file so the next layer's report is its own. Returns how many it found.
function Read-Skips {
    if (-not (Test-Path -LiteralPath $env:TW_SKIP_REPORT)) { return 0 }
    $lines = @(Get-Content -LiteralPath $env:TW_SKIP_REPORT | Where-Object { $_ })
    Remove-Item -LiteralPath $env:TW_SKIP_REPORT -Force -ErrorAction SilentlyContinue
    foreach ($why in $lines) { $script:skipped += $why }
    return $lines.Count
}
# 🔴 EVERY LAYER RUNS UNDER A DEADLINE. They used to be `& powershell ...`, which
# waits forever, and an adversarial audit showed a one-token engine mutation (a
# loop that no longer advances) hangs the suite indefinitely. The unit runner now
# has a per-binary deadline of its own, but golden, nofix, sanitize and e2e all run
# engine code too, so the ceiling lives here, once, for all of them.
#
# Start-Process in the SAME console (-NoNewWindow, no redirects), so a layer's
# output and colors look exactly as before. `$null = $p.Handle` BEFORE waiting,
# or ExitCode reads back empty -- measured in test\run-tests.ps1. On a timeout
# the tree is killed by PID (never by name) and $LASTEXITCODE is set to 124,
# timeout(1)'s convention, so the existing exit checks below need no change.
function Invoke-Layer([string]$name, [string[]]$arguments) {
    $quoted = @($arguments | ForEach-Object { if ($_ -match '\s') { '"' + $_ + '"' } else { $_ } })
    $p = Start-Process -FilePath "powershell.exe" -ArgumentList $quoted -NoNewWindow -PassThru
    $null = $p.Handle
    if (-not $p.WaitForExit($LayerTimeoutSec * 1000)) {
        & taskkill /T /F /PID $p.Id 2>&1 | Out-Null
        $p.WaitForExit(10000) | Out-Null
        Write-Host ""
        Write-Host ("LAYER TIMED OUT: {0} ran longer than {1}s and was killed" -f $name, $LayerTimeoutSec) -ForegroundColor Red
        $global:LASTEXITCODE = 124
        return
    }
    $p.WaitForExit()
    $global:LASTEXITCODE = $p.ExitCode
}
function Stop-Layer([string]$name) {
    $script:timings += ("{0} {1:N1}s" -f $name, $clock.Elapsed.TotalSeconds)
    $clock.Restart()
}

if ($Unit) {
    Write-Host ""
    Write-Host "================= UNIT =================" -ForegroundColor Cyan
    $clock.Restart()
    $unitArgs = @("-ExecutionPolicy", "Bypass", "-File", (Join-Path $root "test\run-tests.ps1"))
    if ($Filter)      { $unitArgs += @("-Filter", $Filter) }
    if ($ResultsPath) { $unitArgs += @("-ResultsPath", $ResultsPath) }
    if ($Lang -ne "both") { $unitArgs += @("-Lang", $Lang) }
    Invoke-Layer "unit" $unitArgs
    $ran += "unit"
    if ($LASTEXITCODE -ne 0) { $failed += "unit" }
    Stop-Layer "unit"
}

if ($Sanitize) {
    Write-Host ""
    Write-Host "=============== SANITIZE ===============" -ForegroundColor Cyan
    $clock.Restart()
    # The whole unit suite again under UndefinedBehaviorSanitizer, trapping.
    #
    # 🔴 THE LAYER THAT WOULD HAVE CAUGHT jc-50 LOCALLY. An adversarial audit
    # reverted that fix -- movelaws[] indexed by a cell's bottom layer, this
    # fork's own headline defect -- and unit, golden and nofix all stayed green.
    # This layer exits 132 on it. About twelve seconds; it reads no new inputs and
    # asserts nothing new, it just watches the SAME cases for undefined
    # behavior, which is where the memory-safety guards are actually observable.
    #
    # ⚠ It is a SEPARATE layer rather than a replacement for the ordinary unit
    # pass, because -w is required (-O1 turns on -Wformat-truncation inside
    # tw_test.h) and losing -Wall -Wextra -Werror would be a bad trade.
    $sanArgs = @("-ExecutionPolicy", "Bypass", "-File", (Join-Path $root "test\run-tests.ps1"), "-Sanitize")
    if ($Filter) { $sanArgs += @("-Filter", $Filter) }
    if ($Lang -ne "both") { $sanArgs += @("-Lang", $Lang) }
    Invoke-Layer "sanitize" $sanArgs
    $ran += "sanitize"
    if ($LASTEXITCODE -ne 0) { $failed += "sanitize" }
    Stop-Layer "sanitize"
}

if ($E2E) {
    Write-Host ""
    Write-Host "================= END TO END =================" -ForegroundColor Cyan
    $clock.Restart()

    if ($Build) {
        # The dynamic flavor deliberately: it builds in a fraction of the time
        # of the static one and exercises the same code. The static build is
        # what SHIPS, and package.ps1 is what verifies that one.
        & powershell -ExecutionPolicy Bypass -File (Join-Path $root "build.ps1") -Flavor dynamic
        if ($LASTEXITCODE -ne 0) {
            Write-Host "build failed, so the end-to-end layer cannot run" -ForegroundColor Red
            $failed += "e2e (build)"
            $ran += "e2e"
        }
    }

    if ($failed -notcontains "e2e (build)") {
        # ⚠ `Test-Path (if (...) {...} else {...})` is NOT valid PowerShell: an if
        # STATEMENT cannot be used where an argument expression is expected, and
        # 5.1 reports it as "The term 'if' is not recognized as the name of a
        # cmdlet". That failure was non-terminating, so $haveExe stayed $false,
        # the end-to-end layer was SKIPPED, and the suite still printed "all
        # green" -- with -Exe pointing at a perfectly good executable. Resolve
        # the path into a variable first.
        $haveExe = $false
        if ($Exe) {
            $exeFull = if ([IO.Path]::IsPathRooted($Exe)) { $Exe } else { Join-Path $root $Exe }
            $haveExe = Test-Path $exeFull
        } else {
            # Only what build.ps1 writes -- see the same list in test\run-e2e.ps1
            # for why a frozen build-jcNN directory must never be a fallback.
            foreach ($candidate in @("build-dynamic\tworld2.exe", "build-static\tworld2.exe")) {
                if (Test-Path (Join-Path $root $candidate)) { $haveExe = $true; break }
            }
        }

        if (-not $haveExe) {
            Write-Host ""
            if ($Exe) {
                # Asking for a SPECIFIC executable and not getting it is a
                # failure, never a skip. The caller named a file; silently
                # testing nothing and reporting green is the worst answer
                # available, and it is exactly what happened here once.
                Write-Host "FAILED: -Exe '$Exe' does not exist ($exeFull)." -ForegroundColor Red
                $failed += "e2e (no such -Exe)"
                $ran += "e2e"
            } else {
                # No executable and none asked for: a setup state, not a defect.
                # Said loudly, because a silent skip is how a suite comes to test
                # less than people think.
                Write-Host "SKIPPED: no built executable found." -ForegroundColor Yellow
                Write-Host "  Build one and re-run, or pass -Build:" -ForegroundColor Yellow
                Write-Host "    powershell -ExecutionPolicy Bypass -File run-tests.ps1 -Build"
                $skipped += "e2e (no built executable; pass -Build)"
            }
        } else {
            $e2eArgs = @("-ExecutionPolicy", "Bypass", "-File", (Join-Path $root "test\run-e2e.ps1"))
            if ($Exe)         { $e2eArgs += @("-Exe", $Exe) }
            if ($ResultsPath) { $e2eArgs += @("-ResultsPath", $ResultsPath) }
            Invoke-Layer "e2e" $e2eArgs
            $e2eExit = $LASTEXITCODE
            # run-e2e.ps1 reports a STALE executable as a skip, not a result.
            if ((Read-Skips) -eq 0) {
                $ran += "e2e"
                if ($e2eExit -ne 0) { $failed += "e2e" }
            }
        }
    }
    Stop-Layer "e2e"
}

if ($Qt) {
    Write-Host ""
    Write-Host "================== QT ==================" -ForegroundColor Cyan
    # The oshw-qt layer. Small, and it covers the one thing no other layer can
    # reach: .ccx metadata, which readextensions() parses ONLY when a main
    # window exists -- so batch mode, the e2e cases and the fuzz targets all
    # skip it by construction.
    #
    # run-qt-tests.ps1 reports SKIPPED and exits 0 when Qt is not installed, so
    # a machine without it still gets every other layer. It says so loudly, it
    # writes the reason to $env:TW_SKIP_REPORT so the summary below can name it,
    # and CI's Qt step throws on a skip, so one cannot quietly become normal.
    $clock.Restart()
    $qtArgs = @("-ExecutionPolicy", "Bypass", "-File", (Join-Path $root "test\run-qt-tests.ps1"))
    if ($Filter) { $qtArgs += @("-Filter", $Filter) }
    Invoke-Layer "qt" $qtArgs
    if ($LASTEXITCODE -ne 0) { $failed += "qt" }
    if ((Read-Skips) -eq 0) { $ran += "qt" }
    Stop-Layer "qt"
}

if ($Golden) {
    Write-Host ""
    Write-Host "================ GOLDEN ================" -ForegroundColor Cyan
    $clock.Restart()
    # The golden-master engine snapshot: all 903 committed levels through BOTH
    # engines, hashed. It is the only layer here that can see an engine
    # behavior change at all -- the unit layer drives synthesized levels and the
    # e2e layer verifies two solutions.
    Invoke-Layer "golden" @("-ExecutionPolicy", "Bypass", "-File", (Join-Path $root "test\run-golden.ps1"))
    if ($LASTEXITCODE -ne 0) { $failed += "golden" }
    $ran += "golden"
    Stop-Layer "golden"
}

if ($NoFix) {
    Write-Host ""
    Write-Host "================ NOFIX =================" -ForegroundColor Cyan
    $clock.Restart()
    # The NO_FIX_* differential matrix: for each recorded witness, prove a
    # fix-on build and a fix-off build still disagree. The only check on the
    # desync machinery, and the toggles are opt-out macros -- a broken one
    # changes no shipped behavior and nothing else goes red.
    Invoke-Layer "nofix" @("-ExecutionPolicy", "Bypass", "-File", (Join-Path $root "test\run-nofix.ps1"))
    if ($LASTEXITCODE -ne 0) { $failed += "nofix" }
    $ran += "nofix"
    Stop-Layer "nofix"
}

if ($Docs) {
    Write-Host ""
    Write-Host "================= DOCS =================" -ForegroundColor Cyan
    $clock.Restart()
    # ⚠ A FILTERED UNIT RUN DOES NOT REWRITE docs\test-counts.tsv, so after
    # `-Filter` this compares the documents against the last complete run.
    # That is still the right comparison; it just is not news about this one.
    Invoke-Layer "docs" @("-ExecutionPolicy", "Bypass", "-File", (Join-Path $root "verify-docs.ps1"), "-Quiet")
    if ($LASTEXITCODE -ne 0) { $failed += "docs" }
    $ran += "docs"
    Stop-Layer "docs"
}

Remove-Item -LiteralPath $env:TW_SKIP_REPORT -Force -ErrorAction SilentlyContinue

Write-Host ""
Write-Host "################ SUITE SUMMARY ################"
Write-Host ("  layers run: {0}" -f $(if ($ran.Count) { $ran -join ", " } else { "none" }))
if ($timings.Count) { Write-Host ("  time:       {0}" -f ($timings -join ", ")) }
foreach ($s in $skipped) { Write-Host ("  NOT RUN:    {0}" -f $s) -ForegroundColor Yellow }
if ($failed.Count -gt 0) {
    Write-Host ("  FAILED: {0}" -f ($failed -join ", ")) -ForegroundColor Red
    exit 1
}
if ($ran.Count -eq 0) {
    # Exit 1 even when the cause is a skip: every requested layer produced no
    # verdict, and a run that verified nothing must not read as a pass.
    if ($skipped.Count -gt 0) {
        Write-Host "  nothing ran -- every requested layer SKIPPED (see NOT RUN above)" -ForegroundColor Yellow
    } else {
        Write-Host "  nothing ran" -ForegroundColor Yellow
    }
    exit 1
}
if ($skipped.Count -gt 0) {
    Write-Host ("  green, but {0} layer(s) did NOT run -- this is not an all-green result" -f $skipped.Count) -ForegroundColor Yellow
    exit 0
}
Write-Host "  all green" -ForegroundColor Green
exit 0
