<#
Runs the whole test suite: unit, then end-to-end.

    powershell -ExecutionPolicy Bypass -File run-tests.ps1
    powershell -ExecutionPolicy Bypass -File run-tests.ps1 -Unit
    powershell -ExecutionPolicy Bypass -File run-tests.ps1 -E2E
    powershell -ExecutionPolicy Bypass -File run-tests.ps1 -ResultsPath test-results
    powershell -ExecutionPolicy Bypass -File run-tests.ps1 -Filter random

This is the entry point. The layers underneath can also be run directly:

    test\run-tests.ps1   the unit layer -- C files that compile the source under
                         test directly, no CMake tree, no built executable.
    test\run-e2e.ps1     the end-to-end layer -- the REAL executable, driven
                         through its GUI-free command line.

WHAT EACH LAYER IS FOR, so that a failure points somewhere:

  unit  a defect inside one module: the RNG's arithmetic, the .tws move codec,
        the MS engine's rules, the keyboard arbitration. Fast, and needs only a
        compiler.
  e2e   a defect in how the parts fit together, or in the program's actual
        command-line behavior: directory resolution, .dac parsing, a solution
        being replayed and judged. Needs a built executable, and builds one if
        asked with -Build.

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
if (-not $Unit -and -not $E2E -and -not $Qt) { $Unit = $true; $E2E = $true; $Qt = $true }

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
