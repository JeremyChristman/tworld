<#
Builds and runs the Qt-linked unit tests.

    powershell -ExecutionPolicy Bypass -File test\run-qt-tests.ps1
    powershell -ExecutionPolicy Bypass -File test\run-qt-tests.ps1 -Filter ccmeta

WHY THIS IS A SEPARATE RUNNER FROM test\run-tests.ps1

That one compiles the source under test directly with plain gcc and no Qt
(docs\adr\0003), which is most of why it is fast and why it needs no build tree.
Some of what ships cannot be tested that way: everything in oshw-qt\ is built on
QString, QDomDocument and QColor, so it needs Qt5 headers and Qt5Xml/Gui/Core on
the link line.

Rather than complicate the fast runner with a Qt code path that most tests do
not want, Qt-linked tests live in test\qt\ and run from here.

WHAT THIS UNLOCKED

Before it, NOTHING in oshw-qt\ had automated coverage of any kind -- CLAUDE.md
said so in as many words. The first test written against it covers
CCMetaData.cpp, the .ccx parser, which is the one file in that directory that
reads untrusted input rather than drawing a widget.

🔴 AND .ccx IS UNREACHABLE ANY OTHER WAY. readextensions() (TWMainWnd.cpp)
returns immediately when g_pMainWnd is null, which is exactly the batch case --
so no corpus run, no end-to-end case and no fuzz target has ever parsed a .ccx,
and none can. A Qt-linked unit test is the only way to cover it at all. That is
the argument for this runner existing, not the convenience of it.

⚠ SKIPPING IS NOT PASSING. If Qt cannot be found this reports SKIPPED and exits
0, so a machine without Qt still gets the other seventeen thousand checks -- but
it says so loudly, and CI runs on a machine that HAS Qt, so the skip can never
become the normal case unnoticed. A runner that silently reported success with
nothing built would be exactly the false green the rest of this suite exists to
prevent.

A test declares the extra sources it needs in a TESTSRC: comment on one of its
first lines, the same way test\run-tests.ps1 reads TESTFLAGS -- so the knowledge
lives with the test rather than in a table here that nobody updates.
#>
param(
    [string]$Filter,
    [string]$MsysRoot = "C:\msys64"
)
$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$qtDir    = Join-Path $PSScriptRoot "qt"
$binDir   = Join-Path $MsysRoot "mingw64\bin"

if (-not (Test-Path $qtDir)) {
    Write-Host "no test\qt directory - nothing to run" -ForegroundColor Yellow
    exit 0
}

# The gcc driver cannot spawn cc1 unless its own directory is on PATH, and it
# fails with a nonzero exit and NO diagnostic when it cannot. The same directory
# also holds the Qt DLLs the built test needs at run time. build.ps1 does this
# for the first reason; this script needs both.
$env:PATH = "$binDir;$env:PATH"

$gxx       = Join-Path $binDir "g++.exe"
$pkgconfig = Join-Path $binDir "pkg-config.exe"

function Skip([string]$why) {
    Write-Host ""
    Write-Host "########## Qt tests SKIPPED ##########" -ForegroundColor Yellow
    Write-Host "  $why"
    Write-Host "  These cover oshw-qt\ - the .ccx parser in particular, which"
    Write-Host "  nothing else in the suite can reach. CI runs them; this machine did not."
    exit 0
}

if (-not (Test-Path $gxx))       { Skip "no g++ at $gxx" }
if (-not (Test-Path $pkgconfig)) { Skip "no pkg-config at $pkgconfig" }

# Qt5Gui is needed for QColor, which CCMetaData.h includes. Ask pkg-config
# rather than hardcoding include paths, so this keeps working across Qt updates.
$modules = "Qt5Xml Qt5Gui Qt5Core"
# 🔴 DO NOT PUT `2>$null` ON THESE, and do not let ErrorActionPreference stay
# "Stop" across them. This is the PowerShell 5.1 trap CLAUDE.md warns about:
# redirecting a NATIVE command's stderr wraps each line in a NativeCommandError,
# and with ErrorActionPreference = "Stop" that THROWS -- so a machine without Qt
# never reached the Skip below, it aborted the whole script with a failure.
#
# That is not hypothetical. It broke the jc-49 RELEASE workflow, whose runner
# installs qt5-static for the shipping build and has no pkg-config metadata for
# Qt5Xml. pkg-config printed "Package Qt5Xml was not found", PowerShell turned
# that into a terminating error, and run-tests.ps1 reported a failed layer for a
# machine that should simply have skipped.
$prevEAP = $ErrorActionPreference
$ErrorActionPreference = "Continue"
$cflags = & $pkgconfig --cflags $modules.Split(" ")
$cflagsOk = ($LASTEXITCODE -eq 0)
$libs = & $pkgconfig --libs $modules.Split(" ")
$libsOk = ($LASTEXITCODE -eq 0)
$ErrorActionPreference = $prevEAP

if (-not $cflagsOk -or -not $cflags) { Skip "pkg-config does not know $modules - Qt5 development files not installed" }
if (-not $libsOk -or -not $libs)     { Skip "pkg-config gave no libs for $modules" }

$tests = Get-ChildItem -Path $qtDir -Filter "*_test.cpp" | Sort-Object Name
if ($Filter) { $tests = $tests | Where-Object { $_.Name -like "*$Filter*" } }
if (-not $tests) {
    if ($Filter) { throw "no *_test.cpp in $qtDir matched filter '$Filter'" }
    throw "no *_test.cpp files found in $qtDir"
}

$objDir = Join-Path ([IO.Path]::GetTempPath()) ("tw-qt-tests-" + [guid]::NewGuid().ToString("N").Substring(0,8))
New-Item -ItemType Directory -Force -Path $objDir | Out-Null

$results = @()
$failed  = 0

foreach ($test in $tests) {
    # Extra sources, declared by the test itself. Paths are relative to test\qt\.
    $extraSrc = @()
    $decl = Select-String -Path $test.FullName -Pattern 'TESTSRC:(.*)' |
            Select-Object -First 1
    if ($decl) {
        $extraSrc = @(($decl.Matches[0].Groups[1].Value -replace '\*/\s*$','').Trim() -split '\s+' |
                      Where-Object { $_ } |
                      ForEach-Object { Join-Path $qtDir $_ })
    }

    $exe = Join-Path $objDir ($test.BaseName + ".exe")
    $log = Join-Path $objDir ($test.BaseName + ".cc.log")

    # -std=gnu++11 to match what CMake builds oshw-qt with; gnu, not strict
    # ANSI, for the same reason the C runner uses gnu11 (CLAUDE.md 3.3).
    $argsList = @("-std=gnu++11", "-Wall", "-Wextra") +
                ($cflags -split '\s+' | Where-Object { $_ }) +
                @("-o", $exe, $test.FullName) + $extraSrc +
                ($libs -split '\s+' | Where-Object { $_ })

    & $gxx $argsList 2> $log
    if ($LASTEXITCODE -ne 0) {
        Write-Host "=== $($test.Name) : COMPILE FAILED ===" -ForegroundColor Red
        Get-Content $log -TotalCount 25 | ForEach-Object { Write-Host "  $_" }
        $results += [pscustomobject]@{ Name = $test.Name; Checks = 0; Failures = 0; Status = "compile-failed" }
        $failed++
        continue
    }

    # From the REPOSITORY ROOT: the .ccx cases read the real data\*.ccx files
    # that ship here, and the test resolves them relative to the working
    # directory. Running from anywhere else would silently test fewer files.
    Push-Location $repoRoot
    try {
        $env:TW_TEST_MACHINE = "1"
        $output = & $exe 2>&1
        $exit = $LASTEXITCODE
    } finally {
        Remove-Item Env:\TW_TEST_MACHINE -ErrorAction SilentlyContinue
        Pop-Location
    }

    $checks = 0; $fails = 0
    foreach ($line in $output) {
        if ($line -match '^\s*(\d+) checks, (\d+) failures') {
            $checks = [int]$Matches[1]; $fails = [int]$Matches[2]
        }
    }
    $output | ForEach-Object { Write-Host $_ }

    if ($exit -ne 0) { $failed++ }
    $results += [pscustomobject]@{
        Name = $test.Name; Checks = $checks; Failures = $fails
        Status = $(if ($exit -eq 0) { "passed" } else { "failed" })
    }
}

Remove-Item -LiteralPath $objDir -Recurse -Force -ErrorAction SilentlyContinue

Write-Host ""
Write-Host "########## Qt test summary ##########"
$total = 0
foreach ($r in $results) {
    $mark = if ($r.Status -eq "passed") { " " } else { "!" }
    Write-Host ("  {0} {1,-28} {2,6} checks, {3} failures  [{4}]" -f `
                $mark, $r.Name, $r.Checks, $r.Failures, $r.Status)
    $total += $r.Checks
}
Write-Host ("  {0} run(s), {1:N0} checks total" -f $results.Count, $total)

if ($failed -gt 0) {
    Write-Host "$failed Qt test run(s) FAILED" -ForegroundColor Red
    exit 1
}
Write-Host "all Qt test runs passed" -ForegroundColor Green
exit 0
