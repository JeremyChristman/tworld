<#
Runs the Tile World UNIT tests.

    powershell -ExecutionPolicy Bypass -File test\run-tests.ps1
    powershell -ExecutionPolicy Bypass -File test\run-tests.ps1 -Filter random
    powershell -ExecutionPolicy Bypass -File test\run-tests.ps1 -ResultsPath test-results
    powershell -ExecutionPolicy Bypass -File test\run-tests.ps1 -Lang c

For the end-to-end layer as well, use the runner at the repository root:
`run-tests.ps1`. This script is the unit layer only, and is what package.ps1 gates
on. There are TWO layers, not three -- some unit tests here link more than one
module (series_test.c compiles fileio.c in alongside series.c), which is
integration-flavored, but there is no separate integration runner and nothing
should claim one.

A third instrument sits outside both runners because it needs a level collection
this repository does not contain: test\run-corpus.ps1 replays a whole library of
recorded solutions and diffs one build against another. Run it before shipping any
change to the engine or the file formats -- neither layer here can see a desync.

Each test is a self-contained C file that compiles the code under test directly, so
no configured CMake build tree is needed -- only a C compiler. See
docs\adr\0003-tests-compile-the-source-under-test-directly.md.

Every test is built TWICE, once as C and once as C++, because generic\in.c is
compiled as C by the SDL build and as C++ by the shipped Qt build (through
generic\_in.cpp), and a construct that is valid in only one of them breaks a build
that nobody here runs by hand. See
docs\adr\0004-every-test-is-built-as-c-and-as-cpp.md.

  -Filter       substring match on the test file name; runs only matching tests.
  -ResultsPath  writes <dir>\<test>-<lang>.xml (JUnit XML, which CI renders as
                annotations) and <dir>\<test>-<lang>.json for every run.
  -Lang         c, c++, or both (default). Narrowing this is for debugging only --
                a green run that skipped a language proves half of what it claims.
  -Coverage     compiles with --coverage and leaves the .gcda/.gcno beside the
                objects for coverage.ps1. Not for ordinary runs; it makes the
                binaries slower and writes files.
  -ExtraFlags   appended to every compile. coverage.ps1 uses it; nothing else should.

Exits nonzero if any test run fails, so it can gate a release.
#>
param(
    [string]$Cc,
    [string]$Cxx,
    [string]$Filter,
    [string]$ResultsPath,
    [ValidateSet("c", "c++", "both")]
    [string]$Lang = "both",
    [switch]$Coverage,
    [string]$OutDir,
    [string[]]$ExtraFlags = @()
)
# Native tools write notes to stderr; under "Stop" PowerShell 5.1 turns those into
# terminating NativeCommandErrors even on success. Exit codes are checked explicitly.
$ErrorActionPreference = "Continue"

# Resolve the toolchain rather than assuming a path: the desktop has MSYS2 at
# C:\msys64, and a machine that has gcc on PATH should not need it at all.
function Resolve-Tool([string]$explicit, [string]$name) {
    if ($explicit) {
        if (-not (Test-Path $explicit)) { throw "compiler not found: $explicit" }
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
Write-Host "C compiler  : $Cc"
Write-Host "C++ compiler: $Cxx"

# The MSYS2 driver cannot spawn cc1/cc1plus unless its own directory is on PATH,
# and it fails SILENTLY when it cannot -- nonzero exit, not one word of
# diagnostic. Calling gcc by full path is therefore not enough on a machine
# where MSYS2 is installed but not on PATH, which is the normal state of both of
# these PCs. Prepend it for the duration of this script only.
# Exact segment comparison, not -like: a path containing [ or ] is a wildcard
# pattern to -like and would silently fail to match, skipping the prepend and
# reintroducing the very silent failure this exists to prevent.
foreach ($dir in @((Split-Path -Parent $Cc), (Split-Path -Parent $Cxx))) {
    $segments = $env:Path -split ';'
    if ($segments -notcontains $dir) { $env:Path = "$dir;$env:Path" }
}

# Turns on the TWCASE/TWSUMMARY marker lines in tw_test.h. Always set here, so the
# runner always has structured results to report even without -ResultsPath; a human
# running a test binary by hand gets clean output because they will not have set it.
$env:TW_TEST_MACHINE = "1"

$stubDir = Join-Path $PSScriptRoot "stub"

# $objDir, NOT $outDir. PowerShell variable names are CASE-INSENSITIVE, so a local
# named $outDir IS the $OutDir parameter: the "use the parameter" branch was a
# self-assignment and the "use the default" branch silently OVERWROTE the caller's
# argument. It happens to work here only because nothing reads $OutDir again. This
# is a documented house trap (it cost a whole coverage run in the sibling repo,
# where a local $jvmArgs ate the $JvmArgs parameter and the profiler was never
# attached), so it is spelled differently rather than left armed.
if ($OutDir) {
    $objDir = $OutDir
} else {
    $objDir = Join-Path ([System.IO.Path]::GetTempPath()) "tworld-tests"
}
if (-not (Test-Path $objDir)) { New-Item -ItemType Directory -Path $objDir -Force | Out-Null }

$tests = Get-ChildItem -Path $PSScriptRoot -Filter "*_test.c" | Sort-Object Name
if ($Filter) { $tests = @($tests | Where-Object { $_.Name -like "*$Filter*" }) }
if ($tests.Count -eq 0) {
    if ($Filter) { throw "no *_test.c in $PSScriptRoot matched filter '$Filter'" }
    throw "no *_test.c files found in $PSScriptRoot"
}

$languages = @("c", "c++")
if ($Lang -ne "both") { $languages = @($Lang) }
if ($Lang -ne "both") {
    Write-Warning "running only the '$Lang' half of the suite. Both are required before a release; see docs\adr\0004."
}

# Which languages a given test must be built in.
#
# The default is BOTH, and that default is the point: generic\in.c is compiled as C
# by the SDL build and as C++ by the shipped Qt build (through generic\_in.cpp), so
# a construct valid in only one of them would break a build nobody here runs by
# hand. See docs\adr\0004-every-test-is-built-as-c-and-as-cpp.md.
#
# But that rule is about in.c, not about the whole codebase. Most modules here are
# compiled ONLY as C by CMake, and several of them -- fileio.c, solution.c, and
# anything using err.h's x_alloc -- rely on C's implicit void* conversion, which
# C++ rejects outright. Building those as C++ does not test the shipped program; it
# tests a translation that never happens.
#
# So a test may narrow itself with a TESTLANG: comment on one of its first lines,
# the same way TESTFLAGS: works. Narrowing is a claim that the code under test is
# never compiled the other way, and it belongs in the test file next to the reason.
function Get-TestLanguages([string]$path, [string[]]$allowed) {
    $decl = Select-String -Path $path -Pattern 'TESTLANG:(.*)' | Select-Object -First 1
    if (-not $decl) { return $allowed }
    $wanted = @($decl.Matches[0].Groups[1].Value.Trim() -replace '\*/\s*$','' -split '[\s,]+' |
                Where-Object { $_ -ne '' })
    # An EMPTY declaration is a hard error, not an empty set. `TESTLANG:` with
    # nothing after it -- what a line wrap or a half-finished edit produces --
    # otherwise returned no languages, and the caller then skipped the whole file
    # in DarkGray while the run stayed green and the summary just said one fewer
    # run than yesterday. A test file silently leaving the suite is the single
    # worst thing this runner could do quietly.
    if ($wanted.Count -eq 0) {
        throw "$path has an empty TESTLANG declaration. Write 'TESTLANG: c', 'TESTLANG: c++' or 'TESTLANG: c c++', on its own line."
    }
    foreach ($w in $wanted) {
        if ($w -ne 'c' -and $w -ne 'c++') { throw "$path declares an unknown TESTLANG '$w' (want c, c++, or both)" }
    }
    return @($allowed | Where-Object { $wanted -contains $_ })
}

if ($ResultsPath) {
    $repoRoot = Split-Path -Parent $PSScriptRoot
    $resultsDir = if ([IO.Path]::IsPathRooted($ResultsPath)) { $ResultsPath } else { Join-Path $repoRoot $ResultsPath }

    # 🔴 THIS USED TO BE `Remove-Item -Recurse -Force $resultsDir`, AND THAT WAS A
    # LOADED GUN. -ResultsPath is an ordinary string, often a variable in CI, and
    # `.` is a completely reasonable thing to type for "put them here" -- which
    # resolved to the repository root and recursively force-deleted the entire
    # fork. `..` would have taken every repository beside it.
    #
    # Two defenses, because either alone is thin:
    #  1. Refuse any path that is the repo root, the test directory, or an
    #     ancestor of either. A results directory is never one of those.
    #  2. Delete only the files THIS script writes, never the directory. Stale
    #     reports still have to go -- a test that dies before emitting anything
    #     writes no file, and last run's green XML must not survive a run in
    #     which that test crashed -- but that is a targeted delete, not a sweep.
    $resolvedResults = [IO.Path]::GetFullPath($resultsDir).TrimEnd('\')
    foreach ($protected in @($repoRoot, $PSScriptRoot)) {
        $p = [IO.Path]::GetFullPath($protected).TrimEnd('\')
        if ($resolvedResults -eq $p -or $p.StartsWith($resolvedResults + '\', [StringComparison]::OrdinalIgnoreCase)) {
            throw "-ResultsPath '$ResultsPath' resolves to '$resolvedResults', which contains this repository. Refusing: this script deletes report files under that path. Use a subdirectory such as 'test-results'."
        }
    }

    if (-not (Test-Path $resultsDir)) {
        New-Item -ItemType Directory -Force -Path $resultsDir | Out-Null
    } else {
        Get-ChildItem -LiteralPath $resultsDir -File -Filter '*.xml' -ErrorAction SilentlyContinue |
            Remove-Item -Force -ErrorAction SilentlyContinue
        Get-ChildItem -LiteralPath $resultsDir -File -Filter '*.json' -ErrorAction SilentlyContinue |
            Remove-Item -Force -ErrorAction SilentlyContinue
    }
}

function Escape-Xml([string]$s) {
    if ($null -eq $s) { return "" }
    $s = $s.Replace('&', '&amp;').Replace('<', '&lt;').Replace('>', '&gt;').Replace('"', '&quot;')
    # Control characters are not escapable -- XML 1.0 has no representation for
    # them at all, so `&#1;` is just as invalid as a raw 0x01 and the whole
    # report becomes unparseable ("hexadecimal value 0x01, is an invalid
    # character"). A CHECK_STR or CHECK_MSG message can carry arbitrary bytes,
    # so they are transliterated rather than escaped. Tab, newline and carriage
    # return are legal and are left alone -- tw_test.h already flattens those
    # for its own field separators.
    return ($s -replace '[\x00-\x08\x0B\x0C\x0E-\x1F]', '?')
}

$failed = 0
$runs = @()

foreach ($test in $tests) {
    $testLanguages = Get-TestLanguages $test.FullName $languages
    if ($testLanguages.Count -eq 0) {
        Write-Host ""
        Write-Host "=== $($test.Name) === skipped: its TESTLANG excludes '$Lang'" -ForegroundColor DarkGray
        continue
    }
    foreach ($lang in $testLanguages) {
        $compiler = if ($lang -eq "c") { $Cc } else { $Cxx }
        # gnu11/gnu++11, NOT c99/c++11. This is not a style preference and it is not
        # negotiable: -std=c99 sets __STRICT_ANSI__, under which MinGW defines _WIN32
        # but NOT bare WIN32 -- and fileio.c:20 branches on `#ifdef WIN32` to choose
        # DIRSEP_CHAR and createdir(). Under a strict-ANSI dialect the tests would
        # compile the POSIX branch, which is not the branch that ships, so a green run
        # would be evidence about code no Windows user ever executes. (It happens not
        # to compile at all -- "too many arguments to function 'mkdir'" -- which is the
        # lucky outcome; a module that differed more quietly would just lie.)
        #
        # CMake leaves gcc at its gnu default for C and sets CMAKE_CXX_STANDARD 11 with
        # extensions on, i.e. gnu++11. These match that.
        $std = if ($lang -eq "c") { "-std=gnu11" } else { "-std=gnu++11" }
        $langTag = $lang -replace '\+', 'p'
        $exe = Join-Path $objDir "$($test.BaseName)-$langTag.exe"

        # A test declares any extra compiler flags it needs in a TESTFLAGS
        # comment on one of its first lines, so the knowledge lives with the
        # test rather than in a lookup table here that nobody updates.
        # input_test.c needs -DTWPLUSPLUS because the shipped Qt build defines
        # it (see the top-level CMakeLists) and it governs which entries exist
        # in the keycmds tables -- without it the harness would be exercising
        # a key map the released game does not have.
        # @() around the pipeline is load-bearing. A TESTFLAGS line declaring exactly
        # ONE flag makes the pipeline return a scalar string rather than an array, and
        # every later `$extra +=` is then STRING CONCATENATION -- which produced
        # `-Wno-unused-function-Werror`, a single unknown option that gcc merely warns
        # about while silently dropping the -Werror it was supposed to carry. A flag
        # that quietly stops applying is exactly the failure this suite exists to
        # prevent, so pin the type rather than trusting the element count.
        $extra = @()
        $decl = Select-String -Path $test.FullName -Pattern 'TESTFLAGS:(.*)' |
                Select-Object -First 1
        if ($decl) {
            $extra = @($decl.Matches[0].Groups[1].Value.Trim() -replace '\*/\s*$','' -split '\s+' |
                       Where-Object { $_ -ne '' })
        }
        # -Werror on the TESTS only, never on the release build. The test files and the
        # modules they compile are warning-clean today, so this costs nothing now and
        # stops the first new warning from becoming the hundredth. The release build
        # deliberately does not carry it: a future gcc inventing a new warning must
        # never be able to stop a release going out.
        if (-not $Coverage) { $extra += "-Werror" }
        if ($Coverage) {
            # --coverage on both the compile and the link. The .gcno lands beside the
            # exe and the .gcda is written when it runs, which is why -OutDir exists:
            # coverage.ps1 points it somewhere it can find them again.
            $extra += "--coverage"
            $extra += "-O0"
        }
        if ($ExtraFlags.Count -gt 0) { $extra += $ExtraFlags }

        Write-Host ""
        Write-Host "=== $($test.Name) [$lang] ===" -ForegroundColor Cyan
        if ($extra.Count -gt 0) { Write-Host "    extra flags: $extra" }

        $record = [ordered]@{
            test = $test.Name; language = $lang
            compiled = $false; checks = 0; failures = 0; skipped = 0
            cases = @(); status = "compile-failed"
        }

        & $compiler $std -Wall -Wextra -I $stubDir $extra -x $lang -o $exe $test.FullName
        if ($LASTEXITCODE -ne 0) {
            Write-Host "COMPILE FAILED" -ForegroundColor Red
            $failed++
            $runs += $record
            continue
        }
        $record.compiled = $true

        # stdout only, deliberately. On PowerShell 5.1, redirecting a native command's
        # stderr with 2>&1 wraps every line in a NativeCommandError; the tests print
        # their markers to stdout, and letting stderr flow straight to the console is
        # both simpler and closer to what a person running the binary would see.
        $output = & $exe
        $exit = $LASTEXITCODE
        $output | ForEach-Object { if ($_ -notmatch '^TW(CASE|SUMMARY)\t') { Write-Host $_ } }

        foreach ($line in $output) {
            if ($line -match '^TWCASE\t([^\t]*)\t([^\t]*)\t(.*)$') {
                $record.cases += [ordered]@{ status = $Matches[1]; name = $Matches[2]; message = $Matches[3] }
            } elseif ($line -match '^TWSUMMARY\t([^\t]*)\t(\d+)\t(\d+)\t(\d+)$') {
                $record.suite = $Matches[1]
                $record.checks = [int]$Matches[2]
                $record.failures = [int]$Matches[3]
                $record.skipped = [int]$Matches[4]
            }
        }

        # A test that exits 0 but emitted no summary never reached tw_end() -- it
        # crashed after its last check, or called exit() itself. Without this it would
        # read as a pass with no numbers, which is the one shape a suite must never
        # report. (The older tests in this directory predate tw_test.h and legitimately
        # print their own line; they are matched below rather than failed.)
        if (-not $record.Contains('suite')) {
            $legacy = $output | Select-String -Pattern '^(\d+) checks, (\d+) failures$' | Select-Object -First 1
            if ($legacy) {
                $record.suite = $test.BaseName
                $record.checks = [int]$legacy.Matches[0].Groups[1].Value
                $record.failures = [int]$legacy.Matches[0].Groups[2].Value
            } else {
                Write-Host "NO RESULT REPORTED (crashed, or exited before tw_end)" -ForegroundColor Red
                $record.status = "no-result"
                $failed++
                $runs += $record
                continue
            }
        }

        if ($exit -ne 0 -or $record.failures -gt 0) {
            Write-Host "TESTS FAILED" -ForegroundColor Red
            $record.status = "failed"
            $failed++
        } else {
            $record.status = "passed"
        }
        $runs += $record

        if ($ResultsPath) {
            $base = Join-Path $resultsDir "$($test.BaseName)-$langTag"
            $sb = New-Object Text.StringBuilder
            [void]$sb.AppendLine('<?xml version="1.0" encoding="UTF-8"?>')
            [void]$sb.AppendLine(('<testsuite name="{0}" tests="{1}" failures="{2}" skipped="{3}">' -f
                (Escape-Xml "$($test.BaseName) [$lang]"), $record.cases.Count, $record.failures, $record.skipped))
            foreach ($case in $record.cases) {
                $cn = Escape-Xml "$($test.BaseName).$langTag"
                $name = Escape-Xml $case.name
                if ($case.status -eq 'fail') {
                    [void]$sb.AppendLine(('  <testcase classname="{0}" name="{1}"><failure message="{2}"/></testcase>' -f $cn, $name, (Escape-Xml $case.message)))
                } elseif ($case.status -eq 'skip') {
                    [void]$sb.AppendLine(('  <testcase classname="{0}" name="{1}"><skipped message="{2}"/></testcase>' -f $cn, $name, (Escape-Xml $case.message)))
                } else {
                    [void]$sb.AppendLine(('  <testcase classname="{0}" name="{1}"/>' -f $cn, $name))
                }
            }
            [void]$sb.AppendLine('</testsuite>')
            [IO.File]::WriteAllText("$base.xml", $sb.ToString(), (New-Object Text.UTF8Encoding $false))
            # Written BOM-less like the XML beside it. Set-Content -Encoding UTF8
            # on PowerShell 5.1 emits a byte-order mark, and a BOM makes Python's
            # json.load on a utf-8-opened file fail outright -- an unhelpful way
            # for a machine-readable report to greet whatever consumes it.
            [IO.File]::WriteAllText("$base.json", ($record | ConvertTo-Json -Depth 5),
                                    (New-Object Text.UTF8Encoding $false))
        }
    }
}

# Every discovered test file must have produced at least one run. Without this,
# any path that skips a file -- a malformed directive, a `continue` added later,
# a language filter that matches nothing -- reduces the suite silently and the
# run still reports "all test runs passed". The count is derived rather than
# hardcoded, so adding a test file needs no bookkeeping here.
foreach ($test in $tests) {
    if (-not ($runs | Where-Object { $_.test -eq $test.Name })) {
        Write-Host "$($test.Name) produced NO RUNS AT ALL -- it was skipped, not tested" -ForegroundColor Red
        $failed++
    }
}

Write-Host ""
Write-Host "########## unit test summary ##########"
$totalChecks = 0
foreach ($r in $runs) {
    $totalChecks += $r.checks
    $mark = if ($r.status -eq 'passed') { ' ' } else { '!' }
    Write-Host ("  {0} {1,-22} {2,-4} {3,5} checks, {4} failures, {5} skipped  [{6}]" -f
        $mark, $r.test, $r.language, $r.checks, $r.failures, $r.skipped, $r.status)
}
Write-Host ("  {0} run(s), {1:N0} checks total" -f $runs.Count, $totalChecks)

if ($failed -gt 0) {
    Write-Host "$failed test run(s) FAILED" -ForegroundColor Red
    exit 1
}
Write-Host "all test runs passed" -ForegroundColor Green
exit 0
