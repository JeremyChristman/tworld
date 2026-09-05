<#
Runs the whole test suite: unit, end-to-end, Qt, golden master, NO_FIX_* matrix.

    powershell -ExecutionPolicy Bypass -File run-tests.ps1
    powershell -ExecutionPolicy Bypass -File run-tests.ps1 -Unit
    powershell -ExecutionPolicy Bypass -File run-tests.ps1 -E2E
    powershell -ExecutionPolicy Bypass -File run-tests.ps1 -Golden
    powershell -ExecutionPolicy Bypass -File run-tests.ps1 -NoFix
    powershell -ExecutionPolicy Bypass -File run-tests.ps1 -ResultsPath test-results
    powershell -ExecutionPolicy Bypass -File run-tests.ps1 -Filter random

This is the entry point. The layers underneath can also be run directly:

    test\run-tests.ps1   the unit layer -- C files that compile the source under
                         test directly, no CMake tree, no built executable.
    test\run-e2e.ps1     the end-to-end layer -- the REAL executable, driven
                         through its GUI-free command line.
    test\run-qt-tests.ps1  the oshw-qt layer (skips cleanly without Qt).
    test\run-golden.ps1  the golden-master engine snapshot.
    test\run-nofix.ps1   the NO_FIX_* differential matrix.

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
no built executable and no Qt. Together they take about fifteen seconds. Run
them after ANY edit to mslogic.c, lxlogic.c, encoding.c or random.c.

THE E2E LAYER NEEDS AN EXECUTABLE and does not build one by itself. Pass -Build
to build the dynamic-Qt flavor first, or point -Exe at one you already have. If
neither is available the e2e layer is SKIPPED with a message rather than failing,
because a missing local build is a setup state, not a defect -- but CI passes
-Build, so nothing is quietly skipped there.

Exit code is 0 only if every layer that ran passed.
#>
param(
    [switch]$Unit,
    [switch]$E2E,
    [switch]$Qt,
    [switch]$Golden,
    [switch]$NoFix,
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
# test or a script LIE"). They cost about fifteen seconds together.
if (-not $Unit -and -not $E2E -and -not $Qt -and -not $Golden -and -not $NoFix) {
    $Unit = $true; $E2E = $true; $Qt = $true; $Golden = $true; $NoFix = $true
}

$failed = @()
$ran = @()

if ($Unit) {
    Write-Host ""
    Write-Host "================= UNIT =================" -ForegroundColor Cyan
    $unitArgs = @("-ExecutionPolicy", "Bypass", "-File", (Join-Path $root "test\run-tests.ps1"))
    if ($Filter)      { $unitArgs += @("-Filter", $Filter) }
    if ($ResultsPath) { $unitArgs += @("-ResultsPath", $ResultsPath) }
    if ($Lang -ne "both") { $unitArgs += @("-Lang", $Lang) }
    & powershell @unitArgs
    $ran += "unit"
    if ($LASTEXITCODE -ne 0) { $failed += "unit" }
}

if ($E2E) {
    Write-Host ""
    Write-Host "================= END TO END =================" -ForegroundColor Cyan

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
            foreach ($candidate in @("build-dynamic\tworld2.exe", "build-static\tworld2.exe", "build-jc43\tworld2.exe")) {
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
            }
        } else {
            $e2eArgs = @("-ExecutionPolicy", "Bypass", "-File", (Join-Path $root "test\run-e2e.ps1"))
            if ($Exe)         { $e2eArgs += @("-Exe", $Exe) }
            if ($ResultsPath) { $e2eArgs += @("-ResultsPath", $ResultsPath) }
            & powershell @e2eArgs
            $ran += "e2e"
            if ($LASTEXITCODE -ne 0) { $failed += "e2e" }
        }
    }
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
    # a machine without it still gets every other layer. It says so loudly; CI
    # has Qt, so a skip cannot quietly become the normal case.
    $qtArgs = @("-ExecutionPolicy", "Bypass", "-File", (Join-Path $root "test\run-qt-tests.ps1"))
    if ($Filter) { $qtArgs += @("-Filter", $Filter) }
    & powershell $qtArgs
    if ($LASTEXITCODE -ne 0) { $failed += "qt" }
    $ran += "qt"
}

if ($Golden) {
    Write-Host ""
    Write-Host "================ GOLDEN ================" -ForegroundColor Cyan
    # The golden-master engine snapshot: all 903 committed levels through BOTH
    # engines, hashed. It is the only layer here that can see an engine
    # behavior change at all -- the unit layer drives synthesized levels and the
    # e2e layer verifies two solutions.
    & powershell @("-ExecutionPolicy", "Bypass", "-File", (Join-Path $root "test\run-golden.ps1"))
    if ($LASTEXITCODE -ne 0) { $failed += "golden" }
    $ran += "golden"
}

if ($NoFix) {
    Write-Host ""
    Write-Host "================ NOFIX =================" -ForegroundColor Cyan
    # The NO_FIX_* differential matrix: for each recorded witness, prove a
    # fix-on build and a fix-off build still disagree. The only check on the
    # desync machinery, and the toggles are opt-out macros -- a broken one
    # changes no shipped behavior and nothing else goes red.
    & powershell @("-ExecutionPolicy", "Bypass", "-File", (Join-Path $root "test\run-nofix.ps1"))
    if ($LASTEXITCODE -ne 0) { $failed += "nofix" }
    $ran += "nofix"
}

Write-Host ""
Write-Host "################ SUITE SUMMARY ################"
Write-Host ("  layers run: {0}" -f $(if ($ran.Count) { $ran -join ", " } else { "none" }))
if ($failed.Count -gt 0) {
    Write-Host ("  FAILED: {0}" -f ($failed -join ", ")) -ForegroundColor Red
    exit 1
}
if ($ran.Count -eq 0) {
    Write-Host "  nothing ran" -ForegroundColor Yellow
    exit 1
}
Write-Host "  all green" -ForegroundColor Green
exit 0
