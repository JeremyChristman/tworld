<#
End-to-end tests: the REAL executable, driven from the command line.

    powershell -ExecutionPolicy Bypass -File test\run-e2e.ps1
    powershell -ExecutionPolicy Bypass -File test\run-e2e.ps1 -Exe build-static\tworld2.exe
    powershell -ExecutionPolicy Bypass -File test\run-e2e.ps1 -ResultsPath test-results

Tile World has a complete GUI-free command surface -- -d, -l, -s, -t, -b, -v, -V --
and `-b` (batch verify) runs the whole stack with no window at all: .dac parsing,
.dat loading, level expansion, .tws decoding, and the engine replaying a solution
move for move. That makes a genuine end-to-end test possible on a build server.

🔴 FOUR TRAPS, every one of which makes a naive version of this script PASS WHILE
   TESTING NOTHING. All four were measured, not guessed.

 1. THE EXE IS A WINDOWS-GUI-SUBSYSTEM BINARY (CMakeLists.txt uses add_executable
    WIN32; -mconsole is applied only for Debug). Call it the obvious way --
    `$out = & .\tworld2.exe -b ...` -- and PowerShell DOES NOT WAIT for it and
    captures NOTHING: the call returns in about 11 ms against a real runtime near
    a second, $LASTEXITCODE comes back EMPTY rather than 0 or 1, and $out is the
    empty string. A test written that way asserts nothing and reports success.
    Everything here therefore goes through Start-Process -Wait -PassThru with
    stdout and stderr redirected to files. (With -PassThru but no -Wait,
    $p.ExitCode can still read back empty after WaitForExit(ms); use -Wait.)

 2. THE EXIT CODE IS NOT A VERDICT. batchverify() reaches exit() with the invalid
    count only when -q (silence) is given; without it the program returns
    EXIT_SUCCESS no matter how many solutions failed. And even with -q, "zero
    invalid" and "no solutions were found at all" are both exit 0. So the
    assertions below read STDOUT and require "Valid solutions:   N" with a real
    N; the exit code is checked only where it means something.

 3. THE SET ARGUMENT MUST BE A BARE FILENAME found inside -L, not a path.
    Passing sets\intro-ms.dac fails with "no such data file [series.c:902]".

 4. -r PROTECTS THE .tws, NOT THE REST. A -b -r run still creates save\history
    and, through atexit(shutdownsystem) -> savesettings(), REWRITES
    tw_settings.ini IN THE WORKING DIRECTORY. Every case here runs with its
    working directory set to a scratch folder for that reason, and the last case
    asserts the repository's own tw_settings.ini was not created or touched.

 ⚠ stderr is never empty for the SHIPPED sets: createserieslist() scans all of
   sets\ and warns three times that CHIPS.dat is unavailable, because cc-ms.dac
   and friends name the copyrighted original that is not redistributed here.
   That is correct behavior, not a fault -- see docs\adr\0005. Only the
   synthesized fixture set has a genuinely clean stderr, and only that case
   asserts it.
#>
param(
    [string]$Exe,
    [string]$ResultsPath,
    [string]$Cc
)
$ErrorActionPreference = "Continue"
$repo = Split-Path -Parent $PSScriptRoot

# --------------------------------------------------------------- reporting --

$script:cases = @()
$script:currentCase = $null
$script:checks = 0
$script:failures = 0

function Start-Case([string]$name) {
    if ($script:currentCase) { $script:cases += $script:currentCase }
    $script:currentCase = [ordered]@{ name = $name; status = 'ok'; message = '' }
    Write-Host ""
    Write-Host "  -- $name" -ForegroundColor Cyan
}
function Add-Check([bool]$ok, [string]$message) {
    $script:checks++
    if (-not $ok) {
        $script:failures++
        if ($script:currentCase) {
            $script:currentCase.status = 'fail'
            if (-not $script:currentCase.message) { $script:currentCase.message = $message }
        }
        Write-Host "     FAIL  $message" -ForegroundColor Red
    }
}
function Skip-Case([string]$why) {
    if ($script:currentCase) {
        $script:currentCase.status = 'skip'
        $script:currentCase.message = $why
    }
    Write-Host "     skipped: $why" -ForegroundColor DarkGray
}

# ------------------------------------------------------------- the runner --

# Runs the game and returns stdout, stderr and the exit code. See trap 1.
function Invoke-TileWorld {
    param(
        [string[]]$Arguments,
        [string]$WorkingDirectory,
        [switch]$NoHome        # for the jc-40 startup-crash regression
    )
    $outFile = [IO.Path]::GetTempFileName()
    $errFile = [IO.Path]::GetTempFileName()
    $savedHome = $env:HOME
    $savedUserProfile = $env:USERPROFILE
    try {
        if ($NoHome) {
            # The ordinary Windows GUI case: HOME simply is not in the
            # environment. That is what turned a missing -S into a null
            # dereference before jc-40.
            Remove-Item Env:\HOME -ErrorAction SilentlyContinue
        }
        $p = Start-Process -FilePath $script:exePath -ArgumentList $Arguments `
                           -WorkingDirectory $WorkingDirectory `
                           -NoNewWindow -Wait -PassThru `
                           -RedirectStandardOutput $outFile `
                           -RedirectStandardError $errFile
        # Coerced to "" rather than left null. Get-Content -Raw on an EMPTY file
        # returns $null, not an empty string, and every later `.Trim()` or
        # `-match` against it then throws "You cannot call a method on a
        # null-valued expression" -- which aborts the whole run at the first
        # command that legitimately printed nothing, and reports the remaining
        # cases as never having run.
        $so = Get-Content -LiteralPath $outFile -Raw -ErrorAction SilentlyContinue
        $se = Get-Content -LiteralPath $errFile -Raw -ErrorAction SilentlyContinue
        return [ordered]@{
            exit   = $p.ExitCode
            stdout = if ($null -eq $so) { "" } else { $so }
            stderr = if ($null -eq $se) { "" } else { $se }
        }
    } finally {
        if ($NoHome -and $savedHome) { $env:HOME = $savedHome }
        if ($savedUserProfile) { $env:USERPROFILE = $savedUserProfile }
        Remove-Item -LiteralPath $outFile, $errFile -Force -ErrorAction SilentlyContinue
    }
}

# --------------------------------------------------------------- the setup --

# Find a built executable. The dynamic build is preferred for testing because it
# is what CI and a developer will have; the static one is what ships.
if (-not $Exe) {
    foreach ($candidate in @("build-dynamic\tworld2.exe", "build-static\tworld2.exe", "build-jc43\tworld2.exe")) {
        $full = Join-Path $repo $candidate
        if (Test-Path $full) { $Exe = $candidate; break }
    }
}
if (-not $Exe) {
    Write-Host "no built executable found. Run:" -ForegroundColor Yellow
    Write-Host "    powershell -ExecutionPolicy Bypass -File build.ps1 -Flavor dynamic"
    exit 1
}
$script:exePath = if ([IO.Path]::IsPathRooted($Exe)) { $Exe } else { Join-Path $repo $Exe }
if (-not (Test-Path $script:exePath)) { throw "no executable at $script:exePath" }
Write-Host "executable: $script:exePath"

# A dynamic build needs Qt's DLLs on PATH. Harmless for a static one.
$mingwBin = "C:\msys64\mingw64\bin"
if (Test-Path $mingwBin) {
    $segments = $env:Path -split ';'
    if ($segments -notcontains $mingwBin) { $env:Path = "$mingwBin;$env:Path" }
}

# The build tag this executable should be claiming. Read from fork.h, which is
# its single definition (docs\adr\0006).
$forkSrc = Get-Content (Join-Path $repo "fork.h") -Raw
if ($forkSrc -notmatch '(?m)^\s*#\s*define\s+FORK_BUILD_TAG\s+"(?<tag>[^"]+)"') {
    throw "could not read FORK_BUILD_TAG out of fork.h"
}
$tag = $Matches['tag']

# Everything runs in a scratch directory. See trap 4.
$scratch = Join-Path ([IO.Path]::GetTempPath()) ("tworld-e2e-" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Force -Path $scratch | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $scratch "save") | Out-Null

# 🔴 sets\ AND data\ ARE COPIED INTO THE SCRATCH DIRECTORY, NOT USED IN PLACE.
#
# Running the game with -L pointed at the repository writes into it:
# createallmissingseries() generates a .dac for any data\*.dat that has no .dac
# naming it under the other ruleset, so a single `-l` run left
# sets\CCLP2.dat-lynx.dac and sets\CCLXP2.dat-ms.dac sitting in the working tree
# as untracked files. A test suite that dirties the repository it is testing is
# a test suite people learn to run somewhere else.
#
# res\ is read-only to the program (tilesets, fonts, sounds) and is large, so it
# is used in place.
$resDir = Join-Path $repo "res"
$setsDir = Join-Path $scratch "sets"
$dataDir = Join-Path $scratch "data"
$saveDir = Join-Path $scratch "save"
Copy-Item -Path (Join-Path $repo "sets") -Destination $setsDir -Recurse -Force
Copy-Item -Path (Join-Path $repo "data") -Destination $dataDir -Recurse -Force
$stdArgs = @("-R", $resDir, "-L", $setsDir, "-D", $dataDir, "-S", $saveDir)

# The repository's own settings file must not be created or modified by any of
# this. Recorded now, checked at the end.
$repoSettings = Join-Path $repo "tw_settings.ini"
$settingsBefore = if (Test-Path $repoSettings) { (Get-FileHash -LiteralPath $repoSettings -Algorithm SHA256).Hash } else { $null }
# The whole sets\ directory, not just the settings file. The generated-.dac leak
# above was invisible to a check that watched one filename, so this watches the
# file list.
$repoSetsBefore = @(Get-ChildItem -LiteralPath (Join-Path $repo "sets") -File | ForEach-Object { $_.Name } | Sort-Object)

Write-Host "scratch:    $scratch"
Write-Host "build tag:  $tag"

try {

# ============================================================== the cases ==

Start-Case "-V prints this fork's identity, including the build tag"
$r = Invoke-TileWorld -Arguments @("-V") -WorkingDirectory $scratch
Add-Check ($r.exit -eq 0) "-V exited $($r.exit), wanted 0"
Add-Check ($r.stdout -match [regex]::Escape("build $tag")) "-V did not name build $tag"
Add-Check ($r.stdout -match 'unofficial fork|personal fork') "-V did not identify itself as a fork"
Add-Check ($r.stdout -match 'github\.com/JeremyChristman/tworld') "-V did not route bug reports to this fork"
# The AI-assistance disclosure is a deliberate, standing commitment, not
# decoration -- if it ever disappears from the About text, that is a regression.
Add-Check ($r.stdout -match 'Claude') "-V no longer discloses that the fork was written with AI assistance"

Start-Case "-v prints the version number"
# 🔴 KNOWN DEFECT, PINNED HERE ON PURPOSE -- and it is UPSTREAM's, not this
# fork's. `git blame` puts it in the 2.3.1 import (929d9c6).
#
# The option specification at tworld.c:2205 is
#     "abD:dFfHhL:lm:n:PpqR:rS:stVv:c"
# in which `v:` declares that -v TAKES AN ARGUMENT. Its handler
# (`case 'v': puts(VERSION); exit(EXIT_SUCCESS);`) takes none, and the built-in
# usage text says "-v  Display version number and exit". So:
#
#     tworld2 -v        ->  "option requires an argument: -v", usage, exit 1
#     tworld2 -v x      ->  "2.3.1", exit 0
#
# i.e. the documented invocation cannot work, and the one that does needs a
# meaningless extra word. The fix is one character: `v:` -> `v`.
#
# Both behaviors are asserted deliberately. The second is a CHARACTERIZATION
# check: it records what the program does today so that FIXING the defect turns
# this case red and tells whoever fixed it to come here and invert it. That is
# the intended outcome -- do not "repair" this case by deleting it.
$r = Invoke-TileWorld -Arguments @("-v", "x") -WorkingDirectory $scratch
Add-Check ($r.exit -eq 0) "-v with a dummy argument exited $($r.exit), wanted 0"
Add-Check ($r.stdout -match '2\.3') "-v printed '$($r.stdout.Trim())', which does not look like a version"

$r = Invoke-TileWorld -Arguments @("-v") -WorkingDirectory $scratch
Add-Check ($r.exit -ne 0 -and $r.stderr -match 'option requires an argument') `
    "-v ALONE now works. That is the upstream defect being fixed -- good. Change this case to assert exit 0 and a version on stdout, and drop the dummy-argument form above."

Start-Case "-d reports the four directories it resolved"
$r = Invoke-TileWorld -Arguments (@("-d") + $stdArgs) -WorkingDirectory $scratch
Add-Check ($r.exit -eq 0) "-d exited $($r.exit), wanted 0"
Add-Check ($r.stdout -match 'Resource files read from') "-d did not report the resource directory"
Add-Check ($r.stdout -match 'Level sets read from') "-d did not report the level-set directory"
Add-Check ($r.stdout -match 'Solution files saved in') "-d did not report the save directory"
Add-Check ($r.stdout -match ([regex]::Escape($saveDir))) "-d did not honor the -S directory"

Start-Case "jc-40 regression: -R -L -D given, no -S, and no HOME, does not crash"
# Measured on jc-39: this combination left root NULL, and combinepath() called
# strlen(NULL) -- exit 0xC0000005, no window, no output. It is a startup crash,
# not a misconfiguration, and it is the ordinary Windows GUI case.
$r = Invoke-TileWorld -Arguments @("-d", "-R", $resDir, "-L", $setsDir, "-D", $dataDir) `
                      -WorkingDirectory $scratch -NoHome
Add-Check ($r.exit -eq 0) "exited $($r.exit) (0xC0000005 = -1073741819 is the jc-39 crash)"
Add-Check ($r.stdout -match 'Solution files saved in') "no directory report: the program did not get far enough"

Start-Case "-l lists the level sets, and says why CHIPS.dat ones are unusable"
$r = Invoke-TileWorld -Arguments (@("-l") + $stdArgs) -WorkingDirectory $scratch
Add-Check ($r.exit -eq 0) "-l exited $($r.exit), wanted 0"
Add-Check ($r.stdout -match 'intro-ms\.dac') "-l did not list intro-ms.dac"
Add-Check ($r.stdout -match 'CCLP1-MS\.dac') "-l did not list CCLP1-MS.dac"
# Not a fault: cc-ms.dac names the copyrighted original, which is not shipped.
# Asserted so that a future change silently dropping these sets is visible.
Add-Check ($r.stderr -match 'CHIPS\.dat unavailable') "-l no longer explains why the CHIPS.dat sets are unusable"

Start-Case "-s prints a score table, with the grand-total row"
$r = Invoke-TileWorld -Arguments (@("-s", "intro-ms.dac") + $stdArgs) -WorkingDirectory $scratch
Add-Check ($r.exit -eq 0) "-s exited $($r.exit), wanted 0"
Add-Check ($r.stdout -match 'Total Score') "-s printed no Total Score row"
# The text renderer honors column spans; this is the same table the GUI clipped
# before jc-36, and `tworld2 -s` was the oracle that proved it.
Add-Check ($r.stdout -match '(?m)^Level') "-s printed no table header"

Start-Case "the set argument is resolved inside -L, and a bad name fails clearly"
# ⚠ This case used to claim "a set argument must be a bare filename, not a
# path", asserting that `sets\intro-ms.dac` was refused. That was an accident of
# the test's own layout, not a rule: openfileindir() opens a name containing a
# separator DIRECTLY, relative to the working directory, so the argument was
# rejected only because that relative path did not happen to exist. It started
# passing the moment the fixtures moved into a scratch directory that made it
# exist -- which is how the false framing was caught.
#
# What is actually worth pinning: a bare name resolves against -L, and a name
# that resolves nowhere fails loudly rather than silently doing nothing.
$r = Invoke-TileWorld -Arguments (@("-s", "intro-ms.dac") + $stdArgs) -WorkingDirectory $scratch
Add-Check ($r.exit -eq 0) "a bare set name inside -L was not accepted (exit $($r.exit))"
Add-Check ($r.stdout -match 'Total Score') "a bare set name did not produce a score table"

$r = Invoke-TileWorld -Arguments (@("-s", "no-such-set.dac") + $stdArgs) -WorkingDirectory $scratch
Add-Check ($r.exit -ne 0 -or $r.stderr -match 'no such data file') `
    "a set name that resolves nowhere was accepted silently (exit $($r.exit))"

# ---- the batch verifier, against a synthesized set ----------------------

Start-Case "batch verify: one correct and one incorrect solution, judged correctly"
$mk = Join-Path $PSScriptRoot "mkfixture.c"
$cc = $Cc
if (-not $cc) {
    $onPath = Get-Command gcc -ErrorAction SilentlyContinue
    if ($onPath) { $cc = $onPath.Source }
    elseif (Test-Path (Join-Path $mingwBin "gcc.exe")) { $cc = Join-Path $mingwBin "gcc.exe" }
}
if (-not $cc) {
    Skip-Case "no gcc found, so the fixture set cannot be generated"
} else {
    $fx = Join-Path $scratch "fixture"
    foreach ($sub in @("data", "sets", "save")) {
        New-Item -ItemType Directory -Force -Path (Join-Path $fx $sub) | Out-Null
    }
    $mkExe = Join-Path $scratch "mkfixture.exe"
    & $cc -std=gnu11 -Wall -Wextra -Wno-unused-function -o $mkExe $mk
    if ($LASTEXITCODE -ne 0) {
        Add-Check $false "mkfixture.c failed to compile"
    } else {
        & $mkExe $fx | Out-Null
        Add-Check ($LASTEXITCODE -eq 0) "mkfixture exited $LASTEXITCODE"

        $r = Invoke-TileWorld -Arguments @(
            "-b", "-r",
            "-R", $resDir,
            "-L", (Join-Path $fx "sets"),
            "-D", (Join-Path $fx "data"),
            "-S", (Join-Path $fx "save"),
            "fixture-ms.dac") -WorkingDirectory $scratch

        # The verdict comes from stdout, never from the exit code. See trap 2.
        Add-Check ($r.stdout -match '(?m)^\s*Valid solutions:\s+1\s*$') `
            "expected exactly one VALID solution. stdout was:`n$($r.stdout)"
        Add-Check ($r.stdout -match '(?m)^Invalid solutions:\s+1\s*$') `
            "expected exactly one INVALID solution. stdout was:`n$($r.stdout)"
        Add-Check ($r.stdout -match 'Solution for level 2 is invalid') `
            "the invalid solution was not identified as level 2"
        # This set is synthesized, so unlike the shipped sets its stderr really
        # should be empty. A new warning here is a genuine signal.
        Add-Check ([string]::IsNullOrWhiteSpace($r.stderr)) `
            "batch verify wrote unexpected diagnostics:`n$($r.stderr)"

        Start-Case "with -q, the exit code carries the invalid count"
        $r = Invoke-TileWorld -Arguments @(
            "-b", "-q", "-r",
            "-R", $resDir,
            "-L", (Join-Path $fx "sets"),
            "-D", (Join-Path $fx "data"),
            "-S", (Join-Path $fx "save"),
            "fixture-ms.dac") -WorkingDirectory $scratch
        Add-Check ($r.exit -eq 1) "expected exit 1 for one invalid solution, got $($r.exit)"

        Start-Case "a set with no solution file reports that, rather than passing"
        # The failure mode this guards: an empty save directory reporting
        # "0 invalid" and being read as success.
        $emptySave = Join-Path $scratch "emptysave"
        New-Item -ItemType Directory -Force -Path $emptySave | Out-Null
        $r = Invoke-TileWorld -Arguments @(
            "-b", "-r",
            "-R", $resDir,
            "-L", (Join-Path $fx "sets"),
            "-D", (Join-Path $fx "data"),
            "-S", $emptySave,
            "fixture-ms.dac") -WorkingDirectory $scratch
        Add-Check ($r.stdout -match 'No solutions were found') `
            "with no .tws present, expected 'No solutions were found'. stdout was:`n$($r.stdout)"
    }
}

Start-Case "nothing wrote to the repository's own tw_settings.ini"
# -r protects the .tws only; savesettings() still runs at exit and writes into
# the WORKING directory. Every case above ran in the scratch folder for exactly
# this reason, and this is what proves it worked.
$settingsAfter = if (Test-Path $repoSettings) { (Get-FileHash -LiteralPath $repoSettings -Algorithm SHA256).Hash } else { $null }
Add-Check ($settingsBefore -eq $settingsAfter) `
    "the repository's tw_settings.ini changed during the run (before=$settingsBefore after=$settingsAfter)"
$scratchSettings = Join-Path $scratch "tw_settings.ini"
Add-Check (Test-Path $scratchSettings) `
    "no tw_settings.ini appeared in the scratch directory, so this check proved nothing about where the file lands"

Start-Case "nothing added a generated .dac to the repository's sets directory"
$repoSetsAfter = @(Get-ChildItem -LiteralPath (Join-Path $repo "sets") -File | ForEach-Object { $_.Name } | Sort-Object)
$added = @(Compare-Object $repoSetsBefore $repoSetsAfter | Where-Object { $_.SideIndicator -eq '=>' } |
           ForEach-Object { $_.InputObject })
Add-Check ($added.Count -eq 0) `
    ("the run generated $($added.Count) file(s) in the repository's sets\: " + ($added -join ', ') +
     ". createallmissingseries() writes a .dac for any .dat lacking one, so -L must never point at the working tree.")

} finally {
    if ($script:currentCase) { $script:cases += $script:currentCase }
    Remove-Item -LiteralPath $scratch -Recurse -Force -ErrorAction SilentlyContinue
}

# ================================================================ results ==

Write-Host ""
Write-Host "########## end-to-end summary ##########"
foreach ($c in $script:cases) {
    $mark = switch ($c.status) { 'ok' { ' ' } 'skip' { '-' } default { '!' } }
    Write-Host ("  {0} {1,-6} {2}" -f $mark, $c.status, $c.name)
}
$skipped = @($script:cases | Where-Object { $_.status -eq 'skip' }).Count
Write-Host ("  {0} case(s), {1} checks, {2} failures, {3} skipped" -f
    $script:cases.Count, $script:checks, $script:failures, $skipped)

if ($ResultsPath) {
    $dir = if ([IO.Path]::IsPathRooted($ResultsPath)) { $ResultsPath } else { Join-Path $repo $ResultsPath }
    if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }
    function Escape-Xml([string]$s) {
        if ($null -eq $s) { return "" }
        return $s.Replace('&','&amp;').Replace('<','&lt;').Replace('>','&gt;').Replace('"','&quot;').Replace("'",'&apos;')
    }
    $sb = New-Object Text.StringBuilder
    [void]$sb.AppendLine('<?xml version="1.0" encoding="UTF-8"?>')
    [void]$sb.AppendLine(('<testsuite name="e2e" tests="{0}" failures="{1}" skipped="{2}">' -f
        $script:cases.Count, $script:failures, $skipped))
    foreach ($c in $script:cases) {
        $name = Escape-Xml $c.name
        if ($c.status -eq 'fail') {
            [void]$sb.AppendLine(('  <testcase classname="e2e" name="{0}"><failure message="{1}"/></testcase>' -f $name, (Escape-Xml $c.message)))
        } elseif ($c.status -eq 'skip') {
            [void]$sb.AppendLine(('  <testcase classname="e2e" name="{0}"><skipped message="{1}"/></testcase>' -f $name, (Escape-Xml $c.message)))
        } else {
            [void]$sb.AppendLine(('  <testcase classname="e2e" name="{0}"/>' -f $name))
        }
    }
    [void]$sb.AppendLine('</testsuite>')
    [IO.File]::WriteAllText((Join-Path $dir "e2e.xml"), $sb.ToString(), (New-Object Text.UTF8Encoding $false))
    ($script:cases | ConvertTo-Json -Depth 5) | Set-Content -LiteralPath (Join-Path $dir "e2e.json") -Encoding UTF8
}

if ($script:failures -gt 0) {
    Write-Host "END-TO-END TESTS FAILED" -ForegroundColor Red
    exit 1
}
Write-Host "all end-to-end cases passed" -ForegroundColor Green
exit 0
