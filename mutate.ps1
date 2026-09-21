<#
Measures how much of this repository's unit suite is POWER and how much is volume,
by breaking the source on purpose and counting how often the tests notice.

    powershell -ExecutionPolicy Bypass -File mutate.ps1 -SelfTest
    powershell -ExecutionPolicy Bypass -File mutate.ps1
    powershell -ExecutionPolicy Bypass -File mutate.ps1 -Module mslogic.c
    powershell -ExecutionPolicy Bypass -File mutate.ps1 -Sample 200
    powershell -ExecutionPolicy Bypass -File mutate.ps1 -UpdateBaseline

A mutant is a single-token edit to a source line the compiler actually compiles.
KILLED means some test that compiles that file failed because of it. SURVIVED
means every one of them passed anyway, which is a test the suite does not have.

🔴 EVERY BUG THIS SCRIPT CAN HAVE FLATTERS THE SUITE. That asymmetry is the whole
reason for the paranoia below, and it is worth stating plainly before anything
else. The previous measurement of this repository -- a hand-run census by an
outside auditor -- had a harness bug that reported 14 of 190 mutants as SURVIVORS
while having run no tests on them at all. That bug made the suite look WORSE than
it was, and somebody noticed, because a surprising survivor is something a person
goes and reads. Every failure mode available to THIS script points the other way:

  - a mutant that does not compile, counted as a kill
  - -Werror turning a legitimate mutant into "invalid" and shrinking the denominator
  - a test that hung, killed by us, counted as a kill
  - scratch-directory debris from mutant N failing mutant N+1 forever
  - a flaky test failing on a mutant it would have failed on anyway
  - an "equivalent mutant" exclusion matching lines nobody ever proved
  - the blended rate quoted against a number it is not comparable to

Nobody goes and reads a kill. A number that is too high is believed. So this
script is built to REFUSE TO PRODUCE A NUMBER rather than produce a flattering
one, and every guard below exists because of a specific way it could lie.

WHAT THIS MEASURES, AND WHAT IT DOES NOT

The UNIT layer only, compiled the ordinary way. Five more layers exist
(sanitize, e2e, qt, golden, nofix) and a mutant reported SURVIVED here may in
principle be caught by one of them.

⭐ **FOR THE SANITIZE LAYER THAT IS NOW MEASURED RATHER THAN ASSUMED, AND THE
ANSWER IS 0.7%.** `-Escalate` re-ran all 949 plain-pass survivors of the
2026-09-11 census under `-Sanitize`; it caught **seven**. So the survivor list is
NOT meaningfully padded, and SURVIVED can be read as a real gap rather than
hedged. That was worth knowing before anyone spent a week writing tests for it.

⚠ Do not read 0.7% as "the sanitizer is not worth running". It judges only what a
test actually EXECUTES, and it answers a different question than a boundary
mutation asks. Its value on the defect class it was added for is on the record --
this fork's jc-50 bug was invisible to every other local layer.

🔴 THE NUMBER THIS PRINTS IS NOT COMPARABLE TO THE 2026-09-08 AUDIT'S 45%.
That figure came from 233 mutations a person chose by hand, aimed at guards. This
is a mechanical census, so its blended rate is a function of which operators are
enabled and how many sites each one finds -- change the operator mix and the
headline moves without one thing about the test suite changing. Quoting
"45% -> X%" anywhere would be inventing a trend out of a change of method. READ
THE PER-FILE AND PER-OPERATOR COLUMNS. The blended rate is a summary, not a grade,
and it is printed last on purpose.

HOW IT AVOIDS LYING

 1. It never touches the working tree. Everything happens in a scratch copy built
    from `git ls-files`, so a crash mid-run cannot leave a mutated repository --
    and, incidentally, cannot let test\run-tests.ps1 write a mutant's check count
    into docs\test-counts.tsv, which verify-docs.ps1 treats as authoritative.
 2. It only mutates lines that SURVIVE PREPROCESSING. mslogic.c is 4,969 lines,
    of which 2,181 reach the compiler; the rest are comments, blanks, directives
    and the inactive arms of 32 NO_FIX_* toggles. A mutant in an inactive #else
    arm cannot fail a test, so counting it as a survivor invents a test gap in
    this fork's most important file. The line set comes from `gcc -E`, not from a
    table.
 3. It classifies from the runner's own summary block, never from its exit code.
    test\run-tests.ps1 exits 1 for a failed test AND for a compile failure AND for
    a filter that matched nothing. Treating nonzero as KILLED would score most of
    this census off a compiler error.
 4. It compiles mutants with -Wno-error. Without it, a bounds mutant that makes a
    comparison provably constant trips -Wtype-limits, -Werror promotes that to a
    compile failure, and a legitimate mutant lands in INVALID -- which, being
    excluded from the denominator, RAISES the reported rate. A kill has to come
    from a test, not from a warning.
 5. It re-runs every kill once before believing it, because a flaky test failing
    on a mutant is a false kill and the bias only ever points one way.
    settings_test.c does real filesystem I/O with retries and Sleep(20), and
    settings.cpp is a mutation target; this repository has already burned a
    release tag on a flaky timing test.
 6. It checks the scratch tree after every mutant. res_test.c and settings_test.c
    create scratch directories by RELATIVE path and assert on removing them, so
    one mutant that breaks cleanup would fail every later mutant and read as a
    superb kill rate with nothing red anywhere.
 7. It re-verifies against the pristine file whenever a mutant is INVALID or times
    out. If pristine also fails, the environment is broken and the census stops
    rather than classifying anything.
 8. Timeouts are their own bucket, excluded from the numerator. "The test hung"
    is not "the test detected it" -- nothing asserted anything, we killed the
    process -- and a timeout is the verdict most likely to be environmental.

⚠ THE ONE THING IT CANNOT SEE: a #define body produces no preprocessed output of
its own, so macro bodies are never mutated even though they compile at every
expansion site.

-SelfTest RUNS FIRST AND IS NOT OPTIONAL BEFORE BELIEVING A CENSUS. It plants
five mutants whose verdicts are known in advance -- one that must be killed, one
that must not compile, one that must survive because it is on a line the compiler
never sees, one that must hang, and one that must pass the plain suite and die
under the sanitizer -- and refuses to run if any of them comes back with the wrong
answer. Between them they prove the mutation was really applied, the tests really
ran, INVALID is not scoring as KILLED, the compiled-line filter works, the timeout
and process-tree-kill path (otherwise untested code that a long run bets on)
recovers, and the sanitize oracle actually fires. That last one is what makes
-Escalate mean anything: without it, a yield of zero would be indistinguishable
from a sanitize pass that never ran.

TWO OPERATORS, AND THEY ASK DIFFERENT QUESTIONS.

  ROR  the relational boundary itself: `<` <-> `<=`, `>` <-> `>=`, `==` <-> `!=`.
  OFF  the OPERANDS around it: `X > end` becomes `X > (end) + 1` and `(end) - 1`.

🔴 OFF EXISTS BECAUSE ROR IS BLIND IN THE DANGEROUS DIRECTION. For `ptr + k > end`
-- the idiom these parsers are built from -- a boundary shift can only make the
bound STRICTER; accepting one byte more than the record holds means changing an
operand, which ROR cannot do. A blind audit demonstrated the cost: three real
gaps, one a read past a downloaded level record, all invisible to a ROR census
and all found by hand. At encoding.c's optional-field clamp the only ROR edit
available is even a provable no-op, so the census filed the site as a survivor
forever while never asking the question. First OFF run on that file: 56 mutants,
0 invalid, 49 killed, and the audit's own mutation among them.

COST. Roughly 1,270 ROR mutants across the sixteen sources the tests compile, at
about one to eight seconds each depending on how many tests cover the file and
whether an early one kills it. The 2026-09-11 census took about half an hour on
the desktop (this said "an hour and a half" before that was measured). ⚠ OFF
produces about TWICE as many mutants as ROR, since every comparison yields two,
so an OFF census is a run of hours rather than half an hour. That is why the
default stays ROR alone and OFF is asked for by name. That is
why this is a deliberate instrument like coverage.ps1 and test\run-nofix.ps1
-Search, and NOT a seventh layer of run-tests.ps1 -- whose whole default run is
about a minute and gates package.ps1. (This sentence used to say "8.2 seconds",
which was the UNIT layer alone, and named the entry point; an audit timed the
entry point at 71.6. Its summary prints each layer's time now -- read that.)

  -Module        one or more source file names to census (default: all sixteen).
  -Operator      ROR (default), OFF, or both. See the note above: they measure
                 different things, and the committed baseline keeps them in
                 separate rows for that reason. A run records only the operators
                 it measured; rows for the other are kept as they were.
  -Sample        census a deterministic random subset of N mutants. -Seed picks it.
  -ResultsPath   directory for the per-mutant TSV (default: under the run's temp dir).
  -UpdateBaseline  update docs\mutation-baseline.tsv. REFUSED on a sampled or
                 dirty-tree run: a sampled count reads exactly as authoritative
                 as a complete one, and a number measured on a tree nobody else
                 has could never be reproduced. A -Module run IS allowed and
                 rewrites only the rows it measured -- the survivor queue is
                 worked one file at a time, and a refusal there just left the
                 baseline stale, which is worse than partial. Rows carry their
                 own file, operator and commit, and the header names the scope.
  -Escalate <tsv>  take an earlier census's mutants.tsv, re-run only the mutants it
                 recorded as SURVIVED, this time under run-tests.ps1 -Sanitize, and
                 report how many of them that layer catches. Those are not test
                 gaps; what is left over is. Writes escalated.tsv.
  -Split <tsv>     take an earlier census's mutants.tsv and sort the mutants it
                 recorded as SURVIVED by whether any test EXECUTES the line.
                 REACHED means a test runs it and does not assert; UNREACHED means
                 nothing runs it. Different fixes, different costs. Runs
                 coverage.ps1 for the map; compiles and runs no mutants.
  -SelfTest      run the canaries and stop, without censusing.
  -KeepScratch   do not delete the scratch tree on the way out (for debugging).
#>
param(
    [string[]]$Module,
    # 🔴 THE DEFAULT STAYS ROR ALONE, deliberately. The committed baseline and
    # every figure quoted against it are ROR censuses; adding an operator to the
    # default would move the headline without one thing about the suite changing,
    # which is the misreading this script's header exists to prevent. OFF is a
    # separate, longer run recorded in its own baseline rows. See docs/adr/0013.
    [ValidateSet("ROR", "OFF")]
    [string[]]$Operator = @("ROR"),
    [int]$Sample = 0,
    [int]$Seed = 20260911,
    # Path to a mutants.tsv from an earlier census. Re-runs only the mutants that
    # SURVIVED, under the sanitize layer, and reports how many of them that layer
    # catches. See the -Escalate note in the header.
    [string]$Escalate,
    # Path to a mutants.tsv. Sorts the mutants it recorded as SURVIVED by whether
    # any test EXECUTES the line, which decides what the fix is. Runs coverage.ps1
    # to get the map; no mutants are compiled or run.
    [string]$Split,
    # Path to a mutants.tsv. Re-runs the mutants it recorded as SURVIVED against the
    # CURRENT tree, ordinary pass, and reports how many now die. This is the loop for
    # closing a survivor queue: add assertions, recheck, see what died. Combine with
    # -Module to recheck one file in seconds instead of censusing everything.
    [string]$Recheck,
    [string]$ResultsPath,
    [switch]$UpdateBaseline,
    [switch]$SelfTest,
    [switch]$KeepScratch,
    [int]$RunTimeoutSec = 90,
    [double]$MaxInvalidRate = 0.05,
    [string]$Cc,
    [string]$Cxx
)

# Native tools write notes to stderr; under "Stop" PowerShell 5.1 turns those into
# terminating NativeCommandErrors even on success. Exit codes are checked explicitly.
$ErrorActionPreference = "Continue"
$repoRoot = $PSScriptRoot
$utf8NoBom = New-Object Text.UTF8Encoding $false

# ---------------------------------------------------------------------------
# Toolchain
# ---------------------------------------------------------------------------

# Same shape as run-tests.ps1 and coverage.ps1: resolve rather than assume, so a
# machine with gcc already on PATH does not need MSYS2 at a particular place.
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

# The MSYS2 driver cannot spawn cc1 unless its own directory is on PATH, and it
# fails SILENTLY when it cannot -- nonzero exit, not one word of diagnostic.
# run-tests.ps1 documents this at length; we need it too, for the `gcc -E` pass.
$ccDir = Split-Path -Parent $Cc
if ($env:PATH -notlike "*$ccDir*") { $env:PATH = "$ccDir;$env:PATH" }

# ---------------------------------------------------------------------------
# The scratch tree
# ---------------------------------------------------------------------------

function New-ScratchTree([string]$root) {
    Push-Location $root
    try {
        $tracked = & git ls-files
        if ($LASTEXITCODE -ne 0) { throw "git ls-files failed in $root -- this script needs a git checkout" }
    } finally { Pop-Location }
    if (-not $tracked -or $tracked.Count -eq 0) { throw "git ls-files returned nothing" }

    # ⚠ A TEST FILE THAT IS NOT TRACKED IS NOT IN THIS TREE, AND THE CENSUS WILL
    # NOT KNOW IT EXISTS. The scratch tree is built from `git ls-files`, so a test
    # you just wrote and have not `git add`ed is invisible: the source-to-test map
    # will not list it, its assertions will not run, and a -Recheck of the
    # survivors it was written to kill comes back 0% with nothing looking wrong.
    # Measured the first time test\fileio_test.c was written. This direction is
    # the SAFE one -- a missing test makes the suite look weaker, not stronger --
    # so it warns rather than refusing, but it warns loudly.
    Push-Location $root
    try { $untracked = @(& git ls-files --others --exclude-standard -- "test/*_test.c" "test/*_test.cpp") }
    finally { Pop-Location }
    if ($untracked.Count -gt 0) {
        Write-Warning ("{0} test file(s) are UNTRACKED and will be invisible to this run: {1}. git add them first." -f
            $untracked.Count, ($untracked -join ", "))
    }

    $runId = (Get-Date -Format "yyyyMMdd-HHmmss") + "-" + $PID
    $scratch = Join-Path ([IO.Path]::GetTempPath()) "tworld-mutate\$runId\tree"
    New-Item -ItemType Directory -Force -Path $scratch | Out-Null

    foreach ($rel in $tracked) {
        $src = Join-Path $root $rel
        # A tracked file that is absent from the working tree is a deleted-but-not-
        # committed file. Copying nothing would silently census a tree that does not
        # match what the developer has; say so instead.
        if (-not (Test-Path -LiteralPath $src)) { throw "tracked file missing from the working tree: $rel" }
        $dst = Join-Path $scratch $rel
        $dstDir = Split-Path -Parent $dst
        if (-not (Test-Path -LiteralPath $dstDir)) { New-Item -ItemType Directory -Force -Path $dstDir | Out-Null }
        Copy-Item -LiteralPath $src -Destination $dst -Force
    }
    return $scratch
}

# A cheap fingerprint of every file under the tree. Compared after each mutant:
# a new path is debris a test left behind, and a changed length or timestamp on a
# tracked file means something wrote into the tree and we no longer know what we
# are measuring. Both abort rather than continue -- see the res_test.c /
# settings_test.c relative-scratch-directory hazard in the header.
#
# ⚠ CONTENT, NOT TIMESTAMP. Restoring a file byte-for-byte still moves its
# LastWriteTime, so a timestamp comparison flags this script's own restores --
# measured, it fired on mslogic.c and random.c after the self-test had already put
# them back correctly. Hashing all 356 files after all 1,267 mutants would cost
# real time, so the hash is taken once up front and recomputed per file ONLY when
# the timestamp moved. That makes the check exact and nearly free, and it has the
# happy side effect of proving every restore was byte-exact.
function Get-TreeManifest([string]$scratch) {
    $m = @{}
    Get-ChildItem -LiteralPath $scratch -Recurse -File -Force | ForEach-Object {
        $m[$_.FullName.Substring($scratch.Length)] = [ordered]@{
            len = $_.Length
            mtime = $_.LastWriteTimeUtc.Ticks
            hash = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA1).Hash
        }
    }
    return $m
}

function Get-TreeDirs([string]$scratch) {
    $d = @{}
    Get-ChildItem -LiteralPath $scratch -Recurse -Directory -Force | ForEach-Object {
        $d[$_.FullName.Substring($scratch.Length)] = $true
    }
    return $d
}

function Compare-TreeManifest($before, [string]$scratch, $beforeDirs) {
    $problems = @()
    # ⚠ DIRECTORIES TOO, AND THIS IS NOT PEDANTRY. The hazard this check exists for
    # is res_test.c and settings_test.c creating scratch directories by RELATIVE
    # path; a mutant that breaks only the final removedir -- not the file cleanup --
    # leaves an EMPTY directory behind, which a file-only walk cannot see. Every
    # later mutant then runs against a pre-existing directory, and whichever tests
    # assert that createdir() succeeded are scored as kills for the wrong reason.
    $seenDirs = @{}
    Get-ChildItem -LiteralPath $scratch -Recurse -Directory -Force | ForEach-Object {
        $key = $_.FullName.Substring($scratch.Length)
        $seenDirs[$key] = $true
        if (-not $beforeDirs.ContainsKey($key)) { $problems += @{ kind = "debris"; path = $key; what = "left behind (dir)" } }
    }
    foreach ($k in $beforeDirs.Keys) {
        if (-not $seenDirs.ContainsKey($k)) { $problems += @{ kind = "fatal"; path = $k; what = "deleted (dir)" } }
    }

    $seen = @{}
    Get-ChildItem -LiteralPath $scratch -Recurse -File -Force | ForEach-Object {
        $key = $_.FullName.Substring($scratch.Length)
        $seen[$key] = $true
        if (-not $before.ContainsKey($key)) { $problems += @{ kind = "debris"; path = $key; what = "left behind" }; return }
        $b = $before[$key]
        if ($_.Length -ne $b.len) { $problems += @{ kind = "fatal"; path = $key; what = "modified (size)" }; return }
        if ($_.LastWriteTimeUtc.Ticks -ne $b.mtime) {
            $h = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA1).Hash
            if ($h -ne $b.hash) { $problems += @{ kind = "fatal"; path = $key; what = "modified (content)" } }
        }
    }
    foreach ($k in $before.Keys) {
        if (-not $seen.ContainsKey($k)) { $problems += @{ kind = "fatal"; path = $k; what = "deleted" } }
    }
    return $problems
}

# Removes files and directories a mutant's test run left behind, longest path
# first so children go before their parents.
function Remove-Debris([string]$scratch, $debris) {
    foreach ($p in ($debris | Sort-Object { $_.path.Length } -Descending)) {
        Remove-Item -LiteralPath ($scratch + $p.path) -Recurse -Force -ErrorAction SilentlyContinue
    }
}

# ---------------------------------------------------------------------------
# What each test compiles, and which lines of it the compiler actually sees
# ---------------------------------------------------------------------------

# ⚠ THIS PARSES THE SAME DECLARATIONS run-tests.ps1 PARSES, and that is a
# duplication worth being explicit about. The house rule is that two
# hand-maintained TABLES that must agree will disagree (CLAUDE.md section 8.1).
# This is not a second table: the source of truth is the comment in the test file
# itself, and both scripts read it. What is duplicated is the reading, and the
# consequence of a divergence is bounded -- if the two disagree about a test's
# language or flags, the preprocessor pass and the compile pass would disagree
# about what is compiled, which the self-test's must-survive canary detects.
function Get-TestLanguages([string]$path) {
    $decl = Select-String -Path $path -Pattern 'TESTLANG:(.*)' | Select-Object -First 1
    if (-not $decl) { return @("c", "c++") }
    $wanted = @($decl.Matches[0].Groups[1].Value.Trim() -replace '\*/\s*$','' -split '[\s,]+' |
                Where-Object { $_ -ne '' })
    if ($wanted.Count -eq 0) { throw "$path has an empty TESTLANG declaration" }
    foreach ($w in $wanted) {
        if ($w -ne 'c' -and $w -ne 'c++') { throw "$path declares an unknown TESTLANG '$w'" }
    }
    return $wanted
}

function Get-TestFlags([string]$path) {
    $decl = Select-String -Path $path -Pattern 'TESTFLAGS:(.*)' | Select-Object -First 1
    if (-not $decl) { return @() }
    # @() around the pipeline is load-bearing for the same reason run-tests.ps1
    # says it is: a single-flag declaration otherwise returns a scalar string and
    # every later += becomes string concatenation.
    return @($decl.Matches[0].Groups[1].Value.Trim() -replace '\*/\s*$','' -split '\s+' |
             Where-Object { $_ -ne '' })
}

# Runs the preprocessor over every test and reads the linemarkers back, giving
# BOTH the source-to-test map and the set of lines that survive preprocessing.
# Deriving both from one pass is the point: a hand-written map of which test
# compiles which file was already wrong once -- it omitted lxlogic.c, tworld.c,
# generic/in.c, play.c and score.cpp, 36% of the mutable surface, silently.
# Turns whatever a gcc linemarker says into a path relative to the scratch tree,
# or $null when the file is not part of the tree (a system header). Handles both
# marker spellings: the escaped-backslash Windows form gcc uses for the file named
# on the command line, and the forward-slash resolved form it uses for includes.
function Get-RepoRelativePath([string]$markerPath, [string]$scratch) {
    $p = $markerPath -replace '\\\\', '\'
    try { $full = [IO.Path]::GetFullPath($p) } catch { return $null }
    $root = [IO.Path]::GetFullPath($scratch).TrimEnd('\')
    if (-not $full.StartsWith($root + '\', [StringComparison]::OrdinalIgnoreCase)) { return $null }
    return $full.Substring($root.Length + 1) -replace '\\', '/'
}

function Get-CompiledMap([string]$scratch) {
    $map = @{}        # source -> hashtable of line -> $true
    $covering = @{}   # source -> list of test base names
    $testFiles = Get-ChildItem -Path (Join-Path $scratch "test") -Filter "*_test.c" | Sort-Object Name

    foreach ($t in $testFiles) {
        # 🔴 @() AT THE CALL SITE, NOT JUST INSIDE THE FUNCTION. A PowerShell
        # function that returns a one-element array UNROLLS IT TO A SCALAR on the
        # way out, and `@flags` then splats a STRING -- which splats its
        # CHARACTERS. Measured: input_test.c's single -DTWPLUSPLUS reached gcc as
        # seven separate input files named D, T, W, P, L, U and S.
        #
        # run-tests.ps1 documents the same trap for its inline pipeline (a single
        # TESTFLAGS entry there silently concatenated into
        # `-Wno-unused-function-Werror`, dropping the -Werror). It survives a
        # function boundary too, and `.Count` reads 1 in both cases, so the obvious
        # debug print cannot tell them apart.
        $langs = @(Get-TestLanguages $t.FullName)
        $flags = @(Get-TestFlags $t.FullName)
        foreach ($lang in $langs) {
            if ($lang -eq "c") { $compiler = $Cc; $std = "-std=gnu11" }
            else               { $compiler = $Cxx; $std = "-std=gnu++11" }
            $stubDir = Join-Path $scratch "test\stub"
            # ⚠ Capture stderr rather than discarding it. A gcc that fails with no
            # diagnostic is a documented trap in this repository (CLAUDE.md section
            # 2: the driver cannot spawn cc1 without its own directory on PATH, and
            # says nothing at all when it cannot). A throw that cannot say why would
            # be the same failure wearing a different hat.
            # Namespaced per run, like the scratch tree. A fixed shared path lets a
            # second census running in another window report ITS preprocessor error
            # as the reason this one failed -- and this run is long enough that
            # starting a second one is a normal thing to do.
            $errFile = Join-Path ([IO.Path]::GetTempPath()) "tworld-mutate-cpp-$PID.err"
            $out = & $compiler -E $std -I $stubDir @flags -x $lang $t.FullName 2>$errFile
            if ($LASTEXITCODE -ne 0) {
                # ⚠ Redirecting a native command's stderr in PowerShell 5.1 wraps it
                # in a NativeCommandError, so the file holds PowerShell's own record
                # formatting interleaved with the compiler's message. Keep the
                # compiler's lines; a diagnostic buried in ~~~~ underlines is the
                # same as no diagnostic.
                $why = (Get-Content -LiteralPath $errFile -ErrorAction SilentlyContinue |
                        Where-Object { $_ -match '\S' -and $_ -notmatch '^(At |\s*\+|\s*$)' } |
                        Select-Object -First 4) -join "; "
                throw "preprocessing $($t.Name) as $lang failed (exit $LASTEXITCODE): $why -- the census cannot know what is compiled."
            }

            $file = ""; $line = 0
            foreach ($text in $out) {
                if ($text -match '^#\s+(\d+)\s+"([^"]*)"') {
                    $line = [int]$Matches[1]; $file = $Matches[2]; continue
                }
                # ⚠ $line++ MATTERS HERE. This branch catches a `#` line that is NOT
                # a linemarker -- a #pragma, which gcc passes through -E as output
                # while it still consumes a source line. Skipping without counting
                # would shift every later line number in that file by one, silently
                # attributing mutants to the wrong lines and admitting the line after
                # each inactive arm. This fork has no pragmas today; it is one token
                # to not have this bug the day someone adds one.
                if ($text.StartsWith("#")) { $line++; continue }
                # ⚠ THE BLANK-LINE FILTER IS LOAD-BEARING. For a short skip gcc keeps
                # the line numbering by emitting blank lines rather than a fresh
                # linemarker. Counting those as present marks the INACTIVE arm of
                # every #if/#else as compiled -- which is 15 arms in mslogic.c alone,
                # each of them a guaranteed survivor and a fake test gap.
                if ($text.Trim().Length -gt 0) {
                    # ⚠ DO NOT PATTERN-MATCH THE MARKER PATH. The test says
                    # `#include "../mslogic.c"`, but gcc does not echo that back: it
                    # emits the RESOLVED path, absolute, with forward slashes and no
                    # ".." left in it -- while the marker for the test file itself
                    # keeps Windows separators, escaped as `\\`. Looking for "/../"
                    # therefore matched nothing at all and the census silently found
                    # zero sources. Resolve both forms and ask whether the result is
                    # inside the scratch tree.
                    $rel = Get-RepoRelativePath $file $scratch
                    if ($rel -and ($rel -like "*.c" -or $rel -like "*.cpp") -and $rel -notlike "test/*") {
                        if (-not $map.ContainsKey($rel)) { $map[$rel] = @{} }
                        $map[$rel][$line] = $true
                        if (-not $covering.ContainsKey($rel)) { $covering[$rel] = New-Object Collections.ArrayList }
                        if (-not $covering[$rel].Contains($t.BaseName)) { [void]$covering[$rel].Add($t.BaseName) }
                    }
                }
                $line++
            }
        }
    }
    return @{ lines = $map; covering = $covering }
}

# ---------------------------------------------------------------------------
# Telling code from everything that only looks like code
# ---------------------------------------------------------------------------

# Returns a char array parallel to $text: 'c' where a mutation operator may
# apply, '.' everywhere else (comments, string and character literals,
# preprocessor directives).
#
# This is deliberately NOT a C tokenizer. Measured across all sixteen sources
# this fork compiles: zero escaped-quote character literals, zero strings
# containing // or /* , zero digraphs or trigraphs, zero #if 0 blocks, and six
# backslash line continuations (five in tworld.c, one in settings.cpp). A
# six-state scanner with escape handling covers every construct that is actually
# present, and the invariant below catches it if that ever stops being true.
function Get-CodeMask([string]$text) {
    $n = $text.Length
    $mask = New-Object char[] $n
    $i = 0
    $atLineStart = $true
    while ($i -lt $n) {
        $ch = $text[$i]
        $next = if ($i + 1 -lt $n) { $text[$i + 1] } else { [char]0 }

        if ($atLineStart -and ($ch -eq ' ' -or $ch -eq "`t")) {
            $mask[$i] = 'c'; $i++; continue
        }
        # A preprocessor directive runs to the end of the line, and keeps running
        # across a backslash-newline continuation.
        if ($atLineStart -and $ch -eq '#') {
            while ($i -lt $n) {
                if ($text[$i] -eq "`n") {
                    # Continued only if the last non-CR character was a backslash.
                    $j = $i - 1
                    if ($j -ge 0 -and $text[$j] -eq "`r") { $j-- }
                    if ($j -ge 0 -and $text[$j] -eq '\') { $mask[$i] = '.'; $i++; continue }
                    break
                }
                $mask[$i] = '.'; $i++
            }
            continue
        }
        $atLineStart = $false

        if ($ch -eq "`n") { $mask[$i] = 'c'; $i++; $atLineStart = $true; continue }

        if ($ch -eq '/' -and $next -eq '/') {
            while ($i -lt $n -and $text[$i] -ne "`n") { $mask[$i] = '.'; $i++ }
            continue
        }
        if ($ch -eq '/' -and $next -eq '*') {
            $mask[$i] = '.'; $mask[$i + 1] = '.'; $i += 2
            while ($i -lt $n) {
                if ($text[$i] -eq '*' -and $i + 1 -lt $n -and $text[$i + 1] -eq '/') {
                    $mask[$i] = '.'; $mask[$i + 1] = '.'; $i += 2; break
                }
                if ($text[$i] -eq "`n") { $atLineStart = $true }
                $mask[$i] = '.'; $i++
            }
            continue
        }
        if ($ch -eq '"' -or $ch -eq "'") {
            $quote = $ch
            $mask[$i] = '.'; $i++
            while ($i -lt $n) {
                if ($text[$i] -eq '\') {
                    $mask[$i] = '.'; $i++
                    if ($i -lt $n) { $mask[$i] = '.'; $i++ }
                    continue
                }
                if ($text[$i] -eq $quote) { $mask[$i] = '.'; $i++; break }
                # An unterminated literal would otherwise eat the rest of the file
                # and silently remove it from the census.
                if ($text[$i] -eq "`n") { throw "unterminated $quote literal at offset $i" }
                $mask[$i] = '.'; $i++
            }
            continue
        }
        $mask[$i] = 'c'; $i++
    }
    # THE INVARIANT. Every character got exactly one classification and none were
    # skipped, dropped or double-counted. This catches every index bug the scanner
    # can have, costs one comparison, and needs no maintaining.
    for ($k = 0; $k -lt $n; $k++) {
        if ($mask[$k] -ne 'c' -and $mask[$k] -ne '.') { throw "classifier left offset $k unclassified" }
    }
    return $mask
}

# ---------------------------------------------------------------------------
# Mutant enumeration
# ---------------------------------------------------------------------------

# ROR -- relational operator replacement, restricted to BOUNDARY SHIFTS and
# equality inversion.
#
# Deliberately NOT `<` -> `>`. A sign reversal is a gross change that nearly any
# test kills, so it pads the numerator with mutants that prove nothing. Every
# memory-safety defect this fork has shipped and fixed -- jc-44, jc-45, jc-50,
# jc-51 -- was an off-by-one, which is what a boundary shift models.
#
# 🔴 BUT ONLY IN ONE DIRECTION, AND THAT LIMIT BELONGS HERE RATHER THAN IN A
# FOOTNOTE. For `ptr + k > end` -- the idiom these parsers are made of -- a
# boundary shift can only make the bound STRICTER. Loosening it means mutating
# an operand (`end` -> `end + 1`, `k` -> `k - 1`, or a declared buffer size), and
# nothing here does that, so the census cannot ask the question those four
# defects were. A blind audit (2026-09-20) found three real gaps in exactly that
# blind spot; they have cases now, but they were found BY HAND. Worse, at
# encoding.c's optional-field clamp the only ROR edit available (`>` -> `>=`) is
# a provable no-op, so the census files that site as a survivor forever while
# never touching the mutation that matters. See docs/adr/0013, "Phase 2".
function Get-RorMutants([string]$text, $mask, $compiledLines, [string]$file) {
    $out = New-Object Collections.ArrayList
    $n = $text.Length
    $line = 1
    $col = 1
    $i = 0
    while ($i -lt $n) {
        $ch = $text[$i]
        if ($ch -eq "`n") { $line++; $col = 1; $i++; continue }
        if ($mask[$i] -ne 'c' -or -not $compiledLines.ContainsKey($line)) { $i++; $col++; continue }

        $next = if ($i + 1 -lt $n) { $text[$i + 1] } else { [char]0 }
        $prev = if ($i -gt 0) { $text[$i - 1] } else { [char]0 }
        $from = $null; $to = $null

        if ($ch -eq '<') {
            if ($next -eq '<') { $i += 2; $col += 2; continue }        # shift
            elseif ($next -eq '=') { $from = '<='; $to = '<' }
            else { $from = '<'; $to = '<=' }
        } elseif ($ch -eq '>') {
            if ($prev -eq '-') { $i++; $col++; continue }              # arrow
            elseif ($next -eq '>') { $i += 2; $col += 2; continue }    # shift
            elseif ($next -eq '=') { $from = '>='; $to = '>' }
            else { $from = '>'; $to = '>=' }
        } elseif ($ch -eq '=' -and $next -eq '=') {
            # Only a bare `==`. <=, >=, != and the compound assignments are handled
            # at their first character or skipped here.
            if ('<>!=+-*/%&|^' -notmatch [regex]::Escape($prev) -or $prev -eq [char]0) {
                $from = '=='; $to = '!='
            } else { $i += 2; $col += 2; continue }
        } elseif ($ch -eq '!' -and $next -eq '=') {
            $from = '!='; $to = '=='
        }

        if ($from) {
            [void]$out.Add([ordered]@{
                file = $file; line = $line; col = $col; offset = $i
                operator = "ROR"; from = $from; to = $to
            })
            $i += $from.Length; $col += $from.Length
        } else {
            $i++; $col++
        }
    }
    return $out
}

# OFF -- offset injection on a comparison's RIGHT-HAND OPERAND.
#
# 🔴 WHY THIS EXISTS, AND WHY ROR COULD NEVER HAVE COVERED IT. A blind audit
# (2026-09-20) put it plainly: for `ptr + k > end`, the idiom these parsers are
# built from, a relational boundary shift can only make the bound STRICTER. The
# dangerous direction -- accepting one more byte than the record holds -- lives
# in the OPERANDS, and ROR cannot reach it. Three real gaps sat in that blind
# spot, one of them a demonstrated read past a downloaded level record, and all
# three had to be found by hand. Worse, at encoding.c's optional-field clamp the
# only ROR edit available (`>` -> `>=`) is a provable no-op, so the census filed
# that site as a survivor forever while never asking the question that mattered.
#
# So: at every comparison, wrap the right-hand operand and shift it by one, in
# both directions. `data + size > dataend` becomes `... > (dataend) + 1`, which
# is the audit's own mutation. Both directions, because a bound can be wrong
# either way: one loosens, the other tightens, and a test suite that pins a
# boundary has to notice both. The LEFT operand is deliberately not mutated --
# shifting either side by one covers the same ground, and a backward extent scan
# is where a character scanner earns its bugs.
#
# ⚠ WHAT IT DELIBERATELY SKIPS, each for a measured reason:
#   * an operand that does not end on its own line -- the per-mutant TSV is
#     tab-separated and a `from` field with a newline in it corrupts the record;
#   * an operand with a comment or string literal in or in FRONT of it: the scan
#     stops at the first character the mask does not call code, so `n > /* c */ 8`
#     yields nothing rather than something mangled;
#   * an operand containing a TAB, for the same tab-separated-record reason;
#   * an angle bracket belonging to a C++ template argument list rather than a
#     comparison -- see the template scan at the top of this function;
#   * `NULL` -- `(NULL) + 1` is meaningless and, in the C++ build of a test,
#     ill-typed, so it would be pure INVALID noise rather than a question;
#   * anything longer than 60 characters, which is a scan that has gone wrong
#     more often than it is a real operand.
# ⚠ -MaxInvalidRate IS NOT THE BACKSTOP IT LOOKS LIKE, and this comment said it
# was. It is computed over the WHOLE run: settings.cpp alone was 60% INVALID
# from the template misparse while the tree-wide rate sat near 2%, under the cap,
# so nothing refused it. It catches a generator that is broken everywhere, not
# one that is broken in one translation unit. Read the per-file INVALID column.
function Get-OffMutants([string]$text, $mask, $compiledLines, [string]$file) {
    $out = New-Object Collections.ArrayList
    $n = $text.Length

    # 🔴 TEMPLATE ANGLE BRACKETS ARE NOT COMPARISONS, and in a C++ source that
    # distinction is most of the INVALID rate. Measured before this existed:
    # `map<string, string> settings;` produced `map<(string) + 1, string>`, and
    # `static_cast<char>(...)` a 48-character "operand" -- 54 of settings.cpp's 88
    # OFF mutants could not compile, over 60%, so -MaxInvalidRate aborted a
    # per-file census outright while the whole-tree rate stayed near 2% and
    # nothing refused it. (ROR has a smaller version of this, documented in
    # CLAUDE.md as settings.cpp's 22 INVALID. ROR is deliberately left alone:
    # changing which mutants it generates would move the committed baseline it
    # is compared against.)
    #
    # The rule is syntactic and conservative: an identifier immediately followed
    # by `<`, with a matching `>` on the SAME line, is a template argument list,
    # and every angle bracket inside it is skipped. It can only DROP mutants,
    # never invent one -- and this codebase spaces its real comparisons.
    $tmpl = New-Object 'bool[]' $n
    for ($p = 0; $p -lt $n; $p++) {
        if ($text[$p] -ne '<' -or $mask[$p] -ne 'c') { continue }
        if ($p -eq 0) { continue }
        $pc = $text[$p - 1]
        if (-not ($pc -match '[A-Za-z0-9_]')) { continue }
        $depth = 0
        for ($q = $p; $q -lt $n; $q++) {
            $qc = $text[$q]
            if ($qc -eq "`n") { break }
            # ⚠ TWO UNSPACED COMPARISONS ON ONE LINE ARE NOT A TEMPLATE, and this
            # heuristic ate both of them: `if (a<b && c>d)` produced ZERO mutants,
            # silently, with no INVALID to notice -- the failure mode the note
            # above calls the worst available here. A template argument list does
            # not contain a logical operator, a statement separator, a brace or an
            # assignment, so finding one means the span is an expression and the
            # angle brackets are comparisons.
            # ⚠ THE FIRST VERSION OF THIS BROKE ONLY ON `& | ; ?`, which still ate
            # `if (a<b) { c = d>e; }` and `if (i<n) arr[i] = g(a>b);` -- found by a
            # later review, after the `&&` shape had been fixed and pinned. Both
            # shapes are in the probe now.
            if ($qc -eq '&' -or $qc -eq '|' -or $qc -eq ';' -or $qc -eq '?' -or
                $qc -eq '{' -or $qc -eq '}' -or $qc -eq '=') { break }
            # ⚠ THE SCAN MUST OBEY THE MASK AND THE ARROW, and it did neither at
            # first. A `>` inside a string or comment, or the one in `->`, closed
            # a range that was never a template -- so `if (i<v->count)` produced
            # NO mutants at all and `if (a<b) printf("a>b\n");` likewise. That is
            # the worst shape of generator bug available here: it deletes a real
            # comparison from the census silently, with no INVALID to notice,
            # just a slightly smaller denominator. Zero instances in this tree
            # today (both forms are spaced), and excluded before there is one.
            if ($mask[$q] -ne 'c') { break }
            if ($qc -eq '<') { $depth++ }
            elseif ($qc -eq '>') {
                if ($q -gt 0 -and $text[$q - 1] -eq '-') { continue }
                $depth--
                if ($depth -eq 0) {
                    for ($r = $p; $r -le $q; $r++) {
                        if ($text[$r] -eq '<' -or $text[$r] -eq '>') { $tmpl[$r] = $true }
                    }
                    break
                }
            }
        }
    }

    $line = 1
    $col = 1
    $i = 0
    while ($i -lt $n) {
        $ch = $text[$i]
        if ($ch -eq "`n") { $line++; $col = 1; $i++; continue }
        if ($mask[$i] -ne 'c' -or -not $compiledLines.ContainsKey($line)) { $i++; $col++; continue }

        $next = if ($i + 1 -lt $n) { $text[$i + 1] } else { [char]0 }
        $prev = if ($i -gt 0) { $text[$i - 1] } else { [char]0 }

        # Locating the comparison is the same problem ROR solves, and the same
        # exclusions apply: a shift operator, a `->` arrow, a compound assignment.
        $oplen = 0
        if (($ch -eq '<' -or $ch -eq '>') -and $tmpl[$i]) { $i++; $col++; continue }
        if ($ch -eq '<') {
            if ($next -eq '<') { $i += 2; $col += 2; continue }
            elseif ($next -eq '=') { $oplen = 2 } else { $oplen = 1 }
        } elseif ($ch -eq '>') {
            if ($prev -eq '-') { $i++; $col++; continue }
            elseif ($next -eq '>') { $i += 2; $col += 2; continue }
            elseif ($next -eq '=') { $oplen = 2 } else { $oplen = 1 }
        } elseif ($ch -eq '=' -and $next -eq '=') {
            if ('<>!=+-*/%&|^' -notmatch [regex]::Escape($prev) -or $prev -eq [char]0) { $oplen = 2 }
            else { $i += 2; $col += 2; continue }
        } elseif ($ch -eq '!' -and $next -eq '=') {
            $oplen = 2
        }
        if ($oplen -eq 0) { $i++; $col++; continue }

        # The operand runs to whatever ends the expression: a closing bracket we
        # did not open, a separator at depth zero, or the end of the line.
        $s = $i + $oplen
        while ($s -lt $n -and ($text[$s] -eq ' ' -or $text[$s] -eq "`t")) { $s++ }
        $e = $s
        $depth = 0
        $clean = $true
        while ($e -lt $n) {
            $c2 = $text[$e]
            if ($c2 -eq "`n" -or $c2 -eq "`r") { break }
            if ($mask[$e] -ne 'c') { $clean = $false; break }
            if ($c2 -eq '(' -or $c2 -eq '[') { $depth++ }
            elseif ($c2 -eq ')' -or $c2 -eq ']') { if ($depth -eq 0) { break }; $depth-- }
            elseif ($depth -eq 0) {
                # `::` is a scope operator, not the end of the expression: without
                # this, `first == string::npos` yielded the operand `string` and
                # `first == (string) + 1`, which is not a question about anything.
                if ($c2 -eq ':' -and $e + 1 -lt $n -and $text[$e + 1] -eq ':') { $e += 2; continue }
                if ($c2 -eq ';' -or $c2 -eq ',' -or $c2 -eq '?' -or $c2 -eq ':' -or $c2 -eq '{') { break }
                # 🔴 STOP AT ANYTHING THAT BINDS LOOSER THAN THE COMPARISON, or the
                # parentheses this operator adds change the PARSE rather than the
                # bound. `n > flags & 0xff` is `(n > flags) & 0xff` in C, so the
                # operand is `flags`; wrapping to `n > (flags & 0xff) + 1` asks a
                # different question than the one the mutant's own from/to fields
                # record -- and it compiles, so it would be scored as if it were
                # the recorded one. Bitwise and/or/xor, a second relational and
                # the two logical operators all bind looser. ⚠ SHIFTS DO NOT --
                # they bind tighter and are consumed above; this comment claimed
                # they were looser, and the code followed the comment.
                # Latent: zero instances across the sixteen sources, like the TAB
                # case, and unlike it this one used to be unexcluded.
                # ⚠ `->` IS NOT A RELATIONAL OPERATOR, and the rule above had to
                # learn that a second time: with `>` in the break set,
                # `if (i<v->count)` stopped the operand at the arrow and produced
                # `v-`, which is not even an expression. Caught by the same probe
                # harness that found the shapes above.
                if ($c2 -eq '>' -and $e -gt 0 -and $text[$e - 1] -eq '-') { $e++; continue }
                # 🔴 AND A SHIFT BINDS TIGHTER THAN A RELATIONAL, so it is part of
                # the operand rather than the end of it: `n > x << 2` is
                # `n > (x << 2)`. Breaking here instead produced `from = x` and
                # `to = (x) + 1`, which compiles as `n > ((x) + 1) << 2` -- a
                # mutant RECORDED as a one-off that actually moves the bound by
                # four, i.e. exactly the mislabeled question this break set exists
                # to prevent. Consume it, the way `::` and `->` are consumed.
                if (($c2 -eq '<' -and $e + 1 -lt $n -and $text[$e + 1] -eq '<') -or
                    ($c2 -eq '>' -and $e + 1 -lt $n -and $text[$e + 1] -eq '>')) { $e += 2; continue }
                if ($c2 -eq '&' -or $c2 -eq '|' -or $c2 -eq '^' -or
                    $c2 -eq '<' -or $c2 -eq '>' -or
                    ($c2 -eq '=' -and $e + 1 -lt $n -and $text[$e + 1] -eq '=') -or
                    ($c2 -eq '!' -and $e + 1 -lt $n -and $text[$e + 1] -eq '=')) { break }
            }
            $e++
        }
        $operand = if ($clean -and $e -gt $s) { $text.Substring($s, $e - $s).TrimEnd(' ', "`t") } else { "" }

        # ⚠ A TAB CORRUPTS THE RECORD EXACTLY AS A NEWLINE DOES, and only the
        # newline was excluded. The per-mutant TSV is tab-separated, so a `from`
        # field containing one silently adds a column, and -Escalate/-Recheck then
        # mis-key or drop that row instead of failing loudly. Measured zero
        # instances across the sixteen sources today: latent, not live, and
        # excluded before it is not.
        # ⚠ AND A C++ ITERATOR ENDPOINT IS NOT ADDITIVE. `i == settings.end()` on a
        # std::map yields `(settings.end()) + 1`, and a bidirectional iterator has
        # no `operator+` -- 12 of settings.cpp's remaining INVALID mutants were
        # exactly this, measured. `++` would compile, but "one past end()" is not
        # the question this operator asks, and a mutant nobody can read the point
        # of is worse than one that never existed.
        if (-not $clean -or $depth -ne 0 -or $operand.Length -eq 0 -or
            $operand.Contains("`t") -or
            $operand -match '(\.|->)c?r?(begin|end)\(\)$' -or
            $operand.Length -gt 60 -or $operand -eq "NULL") {
            $i += $oplen; $col += $oplen; continue
        }

        foreach ($delta in @("+ 1", "- 1")) {
            [void]$out.Add([ordered]@{
                file = $file; line = $line; col = $col + ($s - $i)
                offset = $s
                operator = "OFF"; from = $operand; to = "($operand) $delta"
            })
        }
        $i += $oplen; $col += $oplen
    }
    return $out
}

# Checks the mutant GENERATORS against a fixed probe, before any tree is built.
#
# 🔴 IT RUNS ON EVERY INVOCATION, not only under -SelfTest, because -Split and
# -Recheck match recorded rows against a FRESH enumeration: they depend on the
# generator producing byte-identical operands to the run that wrote the TSV, and
# they used to return before the self-test block ever ran. A mismatch there
# surfaced as "the tree has moved under that TSV", which blames the tree for a
# scanner change. Costs milliseconds.
function Test-MutantGenerators {
    
    # 🔴 THE GENERATORS ARE CHECKED BEFORE ANYTHING ELSE RUNS. A canary proves a
    # mutant that reached disk was scored correctly; it says nothing about the
    # mutants the scanner never produced, or produced wrong. OFF's whole risk
    # lives in its extent scan -- an operand read one character short compiles
    # into a different question, and one read long does not compile at all -- so
    # it is fed a snippet whose answer is written down here, in the file, and the
    # census does not run if the answer changes. Costs milliseconds.
    $probe = @'
    int f(int n, char *p, char *end, int size)
    {
    if (p + size > end)
    	return 1;
    if (n < 16 && p)
    	return 2;
    if (p != NULL)
    	return 3;
    if (n > /* a comment */ 8)
    	return 4;
    if (n ==
    	    12)
    	return 5;
    if (first == string::npos)
    	return 6;
    if (i == settings.end())
    	return 7;
    if (i<v->count)
    	return 8;
    if (n > flags & 0xff)
    	return 9;
    if (n > x << 2)
	return 10;
    map<string, string> m;
    return n >> 2;
    }
'@ -replace "`r`n", "`n"
    $probeLines = @{}
    for ($k = 1; $k -le ($probe -split "`n").Count; $k++) { $probeLines[$k] = $true }
    $got = Get-OffMutants $probe (Get-CodeMask $probe) $probeLines "probe.c"
    $gotKeys = @($got | ForEach-Object { "$($_.line):$($_.from)->$($_.to)" })
    foreach ($want in @("3:end->(end) + 1", "3:end->(end) - 1",
                        "5:16->(16) + 1", "5:16->(16) - 1")) {
        if ($gotKeys -notcontains $want) {
            throw "self-test: the OFF generator did not produce '$want' on its probe. Got: $($gotKeys -join '; ')"
        }
    }
    # And the four exclusions, each of which is a real defect if it regresses:
    # NULL (meaningless and ill-typed in C++), an operand a comment stands in
    # front of, an operand continued on the next line, and a right SHIFT read as
    # a comparison.
    if ($gotKeys -match 'NULL') { throw "self-test: the OFF generator mutated a NULL comparison" }
    if ($gotKeys -match 'comment') { throw "self-test: the OFF generator swallowed a comment into an operand" }
    # ⚠ `n > /* a comment */ 8` yields NOTHING, and that is the intended answer
    # rather than a near miss: the scan stops at the first character the mask
    # does not call code, so a comment between the operator and its operand
    # suppresses the mutant instead of producing a mangled one. Measured when
    # this probe first ran -- the expectation written here was wrong, not the
    # scanner. Cheap to lose; a comment in that position is rare.
    if ($gotKeys -match ':8->') { throw "self-test: the OFF generator mutated across a comment" }
    # The three C++ shapes, each of which was measured producing INVALID mutants
    # before it was excluded: a scope operator read as the end of an expression,
    # a bidirectional iterator asked for `+ 1`, and a template argument list read
    # as two comparisons.
    # ⚠ `-not ($array -match ...)`, NEVER `$array -notmatch ...`. Against an
    # array those operators FILTER rather than answer: `-notmatch` returns every
    # element that does not match, so the condition is true whenever any element
    # does not -- which is always, here. Cost one self-test run to spot.
    if (-not ($gotKeys -match ':string::npos->\(string::npos\) \+ 1')) {
        throw "self-test: the OFF generator no longer reads through the :: scope operator -- first == string::npos must mutate the whole operand. Got: $($gotKeys -join '; ')"
    }
    if ($gotKeys -match 'settings\.end') { throw "self-test: the OFF generator mutated an iterator endpoint, which has no operator+" }
    if ($gotKeys -match ':string->') { throw "self-test: the OFF generator read a template argument list as a comparison" }
    # The two shapes a review found the scanner losing or mangling: an arrow
    # inside an unspaced comparison, and an operand followed by an operator that
    # binds looser than the comparison itself.
    if (-not ($gotKeys -match ':v->count->\(v->count\) \+ 1')) {
        throw "self-test: the OFF generator lost or truncated if (i<v->count) -- the arrow is not a relational operator. Got: $($gotKeys -join '; ')"
    }
    if (-not ($gotKeys -match ':flags->\(flags\) \+ 1')) {
        throw "self-test: n > flags & 0xff must mutate flags alone -- & binds looser than >, so wrapping further changes the parse. Got: $($gotKeys -join '; ')"
    }
    # The other direction, and the one this file got backwards once: a SHIFT binds
    # TIGHTER than the comparison, so the whole shift expression is the operand.
    # Truncating it to `x` produced a mutant recorded as a one-off that actually
    # moved the bound by four.
    if (-not ($gotKeys -match ':x << 2->\(x << 2\) \+ 1')) {
        throw "self-test: n > x << 2 must mutate the whole shift expression -- a shift binds tighter than >. Got: $($gotKeys -join '; ')"
    }
    # Two unspaced comparisons on one line: not a template, and both must survive
    # the template pre-scan. This shape used to yield nothing at all.
    foreach ($pair in @("int f(void){ if (a<b && c>d) return 1; return 0; }",
                        "int f(void){ if (a<b) { c = d>e; } return 0; }",
                        "int f(void){ if (i<n) arr[i] = g(a>b); return 0; }")) {
        $pairLines = @{ 1 = $true }
        $pairGot = @(Get-OffMutants $pair (Get-CodeMask $pair) $pairLines "pair.c")
        if ($pairGot.Count -ne 4) {
            throw ("self-test: two unspaced comparisons on one line must yield four OFF mutants, got {0} -- the template pre-scan is eating real comparisons. Line: {1}  Mutants: {2}" -f
                   $pairGot.Count, $pair, (@($pairGot | ForEach-Object { "$($_.from)->$($_.to)" }) -join '; '))
        }
    }

    # 🔴 ROR IS PROBED TOO, because this function's name and its own argument
    # apply to it at least as strongly: the committed baseline and the standing
    # survivor queue are ROR, and -Recheck matches them against a fresh
    # enumeration. A change to ROR's prev-character exclusions or its arrow and
    # shift skips would surface as "the tree has moved under that TSV", which is
    # the false accusation this whole check exists to prevent.
    $rorGot = Get-RorMutants $probe (Get-CodeMask $probe) $probeLines "probe.c"
    $rorKeys = @($rorGot | ForEach-Object { "$($_.from)->$($_.to)" })
    foreach ($want in @(">->>=", "<-><=", "!=->==", "==->!=")) {
        if ($rorKeys -notcontains $want) {
            throw "self-test: the ROR generator no longer produces '$want' on the probe. Got: $($rorKeys -join '; ')"
        }
    }
    if ($rorKeys -contains ">>->>>=") { throw "self-test: the ROR generator read a right shift as a comparison" }
    # On `if (i<v->count)` ROR must mutate the comparison and NOT the arrow: one
    # mutant on that line, and it has to be the `<`.
    $arrowRor = @($rorGot | Where-Object { $_.line -eq 18 })
    if ($arrowRor.Count -ne 1 -or $arrowRor[0].from -ne '<') {
        throw ("self-test: on `if (i<v->count)` ROR must produce exactly the < mutant and leave the arrow alone; got {0}: {1}" -f
               $arrowRor.Count, (@($arrowRor | ForEach-Object { "$($_.from)->$($_.to)" }) -join '; '))
    }
    Write-Host ("  {0,-16} {1} mutant(s) on the probe" -f "ROR generator", $rorGot.Count)
    if ($gotKeys -match ':12->') { throw "self-test: the OFF generator mutated an operand that continues on the next line" }
    if ($gotKeys -match ':2->') { throw "self-test: the OFF generator read a right shift as a comparison" }
    # The `&&` case has to stop at the operator, not run to the end of the line.
    if ($gotKeys -contains "5:16 && p->(16 && p) + 1") { throw "self-test: the OFF generator ran an operand past an && " }
    Write-Host ("  {0,-16} {1} mutant(s) on the probe, exclusions all held" -f "OFF generator", $got.Count)
    
}

# 🔴 THE SPLICE ASSERTS ITS OWN COORDINATES. With ROR, `from` was one or two
# operator characters and a coordinate bug was self-evident; OFF's `from` is a
# multi-character operand produced by a hand-rolled extent scan with a whitespace
# skip and a TrimEnd, so an off-by-one there would splice mid-identifier and
# record a mutant as one question while asking another. The only feedback would
# be a raised per-file INVALID rate, which this file documents as an unreliable
# backstop. One comparison, every mutant, the same shape Get-CodeMask ends with.
function New-MutatedText([string]$text, $m) {
    if ($m.offset + $m.from.Length -gt $text.Length -or
        $text.Substring($m.offset, $m.from.Length) -ne $m.from) {
        throw ("mutant coordinates do not match the source: {0}:{1}:{2} expected '{3}' at offset {4}, found '{5}'" -f
            $m.file, $m.line, $m.col, $m.from, $m.offset,
            $text.Substring([Math]::Min($m.offset, $text.Length),
                            [Math]::Min($m.from.Length, [Math]::Max(0, $text.Length - $m.offset))))
    }
    return $text.Substring(0, $m.offset) + $m.to + $text.Substring($m.offset + $m.from.Length)
}

# ---------------------------------------------------------------------------
# Running the unit tests, with a timeout and a tree kill
# ---------------------------------------------------------------------------

$script:summaryRe = '^\s*(?:!|\s)?\s*(\S+_test\.c)\s+(c\+\+|c)\s+([\d,]+) checks, (\d+) failures, (\d+) skipped\s+\[([a-z\-]+)\]\s*$'

# Invokes test\run-tests.ps1 INSIDE the scratch tree and returns its summary rows.
#
# ⚠ Start-Process -PassThru WITHOUT -Wait, because we need a deadline and -Wait
# has none. run-e2e.ps1 warns that ExitCode can read back empty in that shape;
# the second, argument-less WaitForExit() below is what settles it. We do not read
# the exit code for the verdict anyway -- see rule 3 in the header -- only to
# distinguish "the runner threw" from "a test failed".
#
# ⚠ -Filter IS A SUBSTRING MATCH, and `input_test` is a substring of
# `dirinput_test`, so a mutant covered by input_test also compiles and runs
# dirinput_test in both languages. There is no filter string that selects
# input_test alone. That costs time, not correctness -- Get-TestVerdict looks only
# at rows whose test name matches exactly -- and it is left alone rather than
# "fixed" by teaching this script to compile tests itself, which would duplicate
# the TESTLANG/TESTFLAGS/PATH handling that run-tests.ps1 owns.
#
# ⚠ KILLING THE CHILD IS NOT ENOUGH. If the test binary hangs and we kill only the
# powershell that launched it, the grandchild keeps running, and on Windows a
# running exe holds its own path open -- so the NEXT mutant's link into that same
# path fails, reads as a compile failure, and every remaining mutant in a
# multi-hour census is scored off a stale file lock. taskkill /T takes the tree.
# By PID from -PassThru, never by process name.
function Invoke-UnitTests([string]$scratch, [string]$filter, [string]$outDir, [int]$timeoutSec, [bool]$sanitize = $false) {
    $runner = Join-Path $scratch "test\run-tests.ps1"
    $stdout = Join-Path $outDir "run.out"
    $stderr = Join-Path $outDir "run.err"

    # -ExtraFlags -Wno-error: appended after run-tests.ps1's own -Werror, so it
    # wins. A non-empty -ExtraFlags also stops the runner writing
    # docs\test-counts.tsv, which is belt-and-braces on top of the scratch tree.
    $argList = @(
        "-ExecutionPolicy", "Bypass", "-NoProfile", "-File", "`"$runner`"",
        "-Filter", "`"$filter`"",
        "-OutDir", "`"$outDir`"",
        "-ExtraFlags", "-Wno-error"
    )
    # The sanitize pass appends -w -O1 AFTER -Werror and after -ExtraFlags, so the
    # -Wno-error above is redundant here and harmless. Kept unconditional so the two
    # passes differ in exactly one argument.
    if ($sanitize) { $argList += "-Sanitize" }
    $p = Start-Process -FilePath "powershell.exe" -ArgumentList $argList `
                       -WorkingDirectory $scratch -NoNewWindow -PassThru `
                       -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    $timedOut = $false
    if (-not $p.WaitForExit($timeoutSec * 1000)) {
        $timedOut = $true
        & taskkill /T /F /PID $p.Id 2>&1 | Out-Null
        $p.WaitForExit(10000) | Out-Null
    } else {
        $p.WaitForExit() | Out-Null
    }

    $text = Get-Content -LiteralPath $stdout -ErrorAction SilentlyContinue
    if ($null -eq $text) { $text = @() }
    $rows = @()
    $inSummary = $false
    foreach ($l in $text) {
        if ($l -match 'unit test summary') { $inSummary = $true; continue }
        if (-not $inSummary) { continue }
        if ($l -match $script:summaryRe) {
            $rows += [ordered]@{
                test = $Matches[1]; lang = $Matches[2]
                checks = [int]($Matches[3] -replace ',', '')
                failures = [int]$Matches[4]; skipped = [int]$Matches[5]
                status = $Matches[6]
            }
        }
    }
    return @{ rows = $rows; timedOut = $timedOut; exit = $p.ExitCode; stdout = $text }
}

# ---------------------------------------------------------------------------
# Verdicts
# ---------------------------------------------------------------------------

# Compares one test's rows against what the pristine tree produced for that same
# test. SURVIVED is the verdict that has to be earned: every expected (test,
# language) run present, every one passed, and the check and skip counts EXACTLY
# equal to baseline.
#
# Requiring the exact check count rather than "nonzero" buys a second thing for
# one integer comparison: a mutant where everything passes but the count MOVED
# means the suite reached the mutated code and declined to assert on it. That is
# the most actionable single output this tool produces, so it gets its own column.
function Get-TestVerdict($rows, $expected, [string]$testName) {
    $mine = @($rows | Where-Object { $_.test -eq "$testName.c" })
    if ($mine.Count -eq 0) { return @{ verdict = "ERROR"; note = "no summary row for $testName" } }
    foreach ($r in $mine) {
        if ($r.status -eq 'compile-failed') { return @{ verdict = "INVALID"; note = "$testName [$($r.lang)] did not compile" } }
    }
    foreach ($r in $mine) {
        if ($r.status -eq 'failed' -or $r.status -eq 'no-result' -or $r.status -eq 'ub-trapped') {
            return @{ verdict = "KILLED"; note = "$testName [$($r.lang)] $($r.status)" }
        }
        if ($r.status -ne 'passed') { return @{ verdict = "ERROR"; note = "unknown status '$($r.status)'" } }
    }
    if ($mine.Count -ne $expected.Count) {
        return @{ verdict = "ERROR"; note = "$testName ran $($mine.Count) of $($expected.Count) expected languages" }
    }
    $drift = $false
    foreach ($r in $mine) {
        $b = $expected | Where-Object { $_.lang -eq $r.lang } | Select-Object -First 1
        if (-not $b) { return @{ verdict = "ERROR"; note = "$testName [$($r.lang)] has no baseline" } }
        # A skip count that moved means a test quietly stopped running -- res_test.c
        # and unslist_test.c call tw_skip() when their resource files are not
        # readable from the working directory, which is NOT a failure.
        if ($r.skipped -ne $b.skipped) { return @{ verdict = "ERROR"; note = "$testName [$($r.lang)] skipped $($r.skipped), baseline $($b.skipped)" } }
        if ($r.checks -ne $b.checks) { $drift = $true }
    }
    return @{ verdict = "PASSED"; note = ""; drift = $drift }
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

Write-Host ""
Write-Host "########## mutation census ##########" -ForegroundColor Cyan
Write-Host "C compiler  : $Cc"
Write-Host "C++ compiler: $Cxx"

Push-Location $repoRoot
$head = (& git rev-parse HEAD).Trim()
$dirty = @(& git status --porcelain)
Pop-Location
Write-Host "repository  : $head$(if ($dirty.Count) { ' (DIRTY -- ' + $dirty.Count + ' changed paths)' })"

$scratch = New-ScratchTree $repoRoot
$runDir = Split-Path -Parent $scratch
$outDir = Join-Path $runDir "obj"
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
Write-Host "scratch tree: $scratch"

if (-not $ResultsPath) { $ResultsPath = Join-Path $runDir "results" }
New-Item -ItemType Directory -Force -Path $ResultsPath | Out-Null

try {
    # The generators answer for themselves before any tree is built: every mode
    # depends on them, and -Split and -Recheck used to reach neither this check
    # nor the canaries.
    Test-MutantGenerators
    # --- what is compiled, and by whom -------------------------------------
    Write-Host ""
    Write-Host "--- preprocessing every test to find the compiled line set ---"
    $sw = [Diagnostics.Stopwatch]::StartNew()
    $compiled = Get-CompiledMap $scratch
    $sw.Stop()
    $sources = $compiled.lines.Keys | Sort-Object
    Write-Host ("  {0} sources, {1} compiled lines, in {2:N1}s" -f $sources.Count,
        (($sources | ForEach-Object { $compiled.lines[$_].Count }) | Measure-Object -Sum).Sum, $sw.Elapsed.TotalSeconds)
    foreach ($s in $sources) {
        Write-Host ("    {0,-22} {1,5} lines   tests: {2}" -f $s, $compiled.lines[$s].Count, ($compiled.covering[$s] -join ", "))
    }

    if ($Module) {
        # Accept both the repo-relative key and the bare leaf name. The help text
        # says "source file names", which makes `-Module in.c` the natural thing to
        # type for generic/in.c -- and being told it "matched none of the compiled
        # sources" when the file is right there in the list above is a bad answer.
        $sources = @($sources | Where-Object {
            $leaf = Split-Path -Leaf $_
            ($Module -contains $_) -or ($Module -contains $leaf)
        })
        if ($sources.Count -eq 0) { throw "-Module matched none of the compiled sources" }
    }

    # --- baseline gate ------------------------------------------------------
    # Run the whole suite twice. A tree that is not green TWICE is not a baseline,
    # and the second run is also what measures per-test cost so covering tests can
    # be tried cheapest-first.
    Write-Host ""
    # In escalate mode every verdict is taken under the sanitize layer, so the
    # baseline has to be taken there too. The counts happen to be identical today
    # (22,734 either way), but assuming that is how a harness starts lying.
    $useSanitize = [bool]$Escalate
    # -Split compiles and runs no mutants at all -- it is a join between an existing
    # census and an existing coverage map -- so the gate that exists to stop a red
    # tree being scored has nothing to protect and is skipped.
    if (-not $Split) {
    Write-Host ("--- baseline gate (the pristine tree must be green, twice{0}) ---" -f $(if ($useSanitize) { ", under -Sanitize" } else { "" }))
    $baseline = @{}
    $cost = @{}
    for ($pass = 1; $pass -le 2; $pass++) {
        $allTests = Get-ChildItem -Path (Join-Path $scratch "test") -Filter "*_test.c" | Sort-Object Name
        foreach ($t in $allTests) {
            $s = [Diagnostics.Stopwatch]::StartNew()
            $r = Invoke-UnitTests $scratch $t.BaseName $outDir $RunTimeoutSec $useSanitize
            $s.Stop()
            $mine = @($r.rows | Where-Object { $_.test -eq "$($t.BaseName).c" })
            if ($mine.Count -eq 0) { throw "baseline pass $pass : $($t.BaseName) produced no summary row" }
            foreach ($row in $mine) {
                if ($row.status -ne 'passed') { throw "baseline pass $pass : $($t.BaseName) [$($row.lang)] is $($row.status). Fix the tree before censusing it." }
            }
            if ($pass -eq 1) {
                $baseline[$t.BaseName] = $mine
            } else {
                foreach ($row in $mine) {
                    $b = $baseline[$t.BaseName] | Where-Object { $_.lang -eq $row.lang } | Select-Object -First 1
                    if (-not $b -or $b.checks -ne $row.checks) {
                        throw "baseline is not reproducible: $($t.BaseName) [$($row.lang)] reported $($b.checks) then $($row.checks) checks"
                    }
                }
                $cost[$t.BaseName] = $s.Elapsed.TotalSeconds
            }
        }
        Write-Host ("  pass {0}: {1} tests green" -f $pass, $baseline.Count)
    }
    $pristineManifest = Get-TreeManifest $scratch
    $pristineDirs = Get-TreeDirs $scratch
    }

    # --- enumerate ----------------------------------------------------------
    #
    # 🔴 A RE-RUN TAKES ITS OPERATORS FROM THE TSV IT WAS HANDED, not from the
    # default. -Escalate, -Recheck and -Split all match a recorded row against a
    # FRESH enumeration, so an OFF census fed back in while $Operator still said
    # ROR matched nothing -- and the failure blamed the tree ("the tree has moved
    # under that TSV; re-run the census instead"), sending the reader off to
    # repeat a run of hours over a flag they simply had not repeated. Caught in
    # review, on the exact loop this operator was built to enable: censusing with
    # OFF and then working the survivor queue with -Recheck.
    # ⚠ -Split FIRST, because -Split RUNS first. The order here used to prefer
    # -Escalate, so `-Split off.tsv -Recheck ror.tsv` recovered the operators of
    # the file it was NOT about to use, and the -Split that did run then blamed
    # the tree. Two mode flags at once has never meant anything; say so instead.
    $modeFlags = @($Split, $Escalate, $Recheck | Where-Object { $_ })
    if ($modeFlags.Count -gt 1) {
        throw "-Split, -Escalate and -Recheck are three different runs over one recorded census; pass exactly one."
    }
    $priorTsv = $modeFlags | Select-Object -First 1
    if ($priorTsv) {
        if (-not (Test-Path -LiteralPath $priorTsv)) { throw "no such mutants.tsv: $priorTsv" }
        $priorOps = @(Import-Csv -LiteralPath $priorTsv -Delimiter "`t" |
                      ForEach-Object { $_.operator } |
                      Where-Object { $_ } | Sort-Object -Unique)
        $added = @($priorOps | Where-Object { $Operator -notcontains $_ })
        # ⚠ VALIDATE BEFORE ASSIGNING. $Operator carries the param block's
        # ValidateSet, and PowerShell re-validates on every assignment -- so an
        # unknown operator in the handed-back file (hand-edited, from a newer
        # build, or a column shifted by a malformed row) threw "the value
        # System.String[] is not a valid value for the Operator variable", naming
        # neither the file nor the value. Say what is wrong instead.
        # 🔴 DERIVED FROM THE PARAMETER'S OWN ValidateSet, never typed twice. A
        # hand-maintained copy here would become the authority on what this build
        # knows, and the day an operator is added to the param block and not to
        # the copy, a TSV THIS BUILD JUST WROTE gets rejected as unknown -- the
        # false accusation the surrounding message exists to avoid.
        # ⚠ FROM $MyInvocation, NOT Get-Command -Name $PSCommandPath. -Name takes a
        # WILDCARD pattern and there is no -LiteralPath, so a checkout under a path
        # containing [ or ] resolves nothing, $known comes back empty, and the run
        # dies claiming it cannot read its own ValidateSet -- a message describing
        # the wrong problem entirely. This reads the same attribute from inside the
        # script, with no path lookup at all.
        $known = @($MyInvocation.MyCommand.Parameters['Operator'].Attributes |
                   Where-Object { $_ -is [System.Management.Automation.ValidateSetAttribute] } |
                   ForEach-Object { $_.ValidValues })
        if ($known.Count -eq 0) { throw "cannot read the Operator parameter's ValidateSet; refusing to guess what operators this build knows" }
        $unknown = @($added | Where-Object { $known -notcontains $_ })
        if ($unknown.Count -gt 0) {
            throw "$priorTsv records operator(s) this build does not know: $($unknown -join ', '). Known operators: $($known -join ', ')."
        }
        if ($added.Count -gt 0) {
            $Operator = @($Operator + $added | Sort-Object -Unique)
            Write-Host ("  operator(s) taken from the recorded run: {0}" -f ($added -join ", "))
        }
    }
    Write-Host ""
    Write-Host "--- enumerating mutants ---"
    $pristineText = @{}
    $mutants = New-Object Collections.ArrayList
    foreach ($s in $sources) {
        $path = Join-Path $scratch $s
        $text = [IO.File]::ReadAllText($path)
        $pristineText[$s] = $text
        $mask = Get-CodeMask $text
        if ($Operator -contains "ROR") {
            $found = Get-RorMutants $text $mask $compiled.lines[$s] $s
            foreach ($m in $found) { [void]$mutants.Add($m) }
            Write-Host ("    {0,-22} {1,5} ROR" -f $s, $found.Count)
        }
        if ($Operator -contains "OFF") {
            $found = Get-OffMutants $text $mask $compiled.lines[$s] $s
            foreach ($m in $found) { [void]$mutants.Add($m) }
            Write-Host ("    {0,-22} {1,5} OFF" -f $s, $found.Count)
        }
    }
    Write-Host ("  {0} mutants total" -f $mutants.Count)

    if ($Sample -gt 0 -and $Sample -lt $mutants.Count) {
        $rng = New-Object Random $Seed
        $mutants = [Collections.ArrayList]@($mutants | Sort-Object { $rng.Next() } | Select-Object -First $Sample)
        Write-Host ("  sampled down to {0} (seed {1})" -f $mutants.Count, $Seed)
    }

    # --- split ---------------------------------------------------------------
    # Sorts an earlier census's survivors by whether any test EXECUTES the line.
    #
    # WHY THIS IS WORTH A MODE. A survivor is "a mutation nothing noticed", which
    # sounds like one problem and is two, with different fixes and very different
    # costs:
    #
    #   REACHED  - a test runs the line and does not assert enough to notice the
    #              change. The fix is usually a few lines in a test that already
    #              exists. These are the cheap ones.
    #   UNREACHED- no test runs the line at all. No assertion can help; the fix is
    #              a new case that gets there first.
    #
    # 🔴 AND A THIRD BUCKET THAT MUST NOT BE FOLDED INTO "UNREACHED". gcov emits no
    # line record for a file-scope initializer, so a mutation inside movelaws[] --
    # the table this fork's headline defect indexed out of bounds -- has NO RECORD
    # rather than a zero count. Calling that unreached would quietly file the most
    # dangerous code in the tree under "nothing runs it, deprioritize".
    if ($Split) {
        if (-not (Test-Path -LiteralPath $Split)) { throw "-Split: no such file: $Split" }
        $prior = @(Import-Csv -LiteralPath $Split -Delimiter "`t")
        $priorSurv = @($prior | Where-Object { $_.verdict -eq "SURVIVED" })
        if ($priorSurv.Count -eq 0) { throw "-Split: $Split records no SURVIVED mutants" }

        $index = @{}
        foreach ($m in $mutants) { $index["$($m.file)|$($m.line)|$($m.col)|$($m.from)|$($m.to)"] = $m }
        $todo = New-Object Collections.ArrayList
        $missing = 0
        foreach ($row in $priorSurv) {
            $key = "$($row.file)|$($row.line)|$($row.col)|$($row.from)|$($row.to)"
            if ($index.ContainsKey($key)) { [void]$todo.Add($index[$key]) } else { $missing++ }
        }
        if ($missing -gt 0) {
            throw "-Split: $missing of $($priorSurv.Count) recorded survivors do not match a mutant enumerated from the current source. The tree has moved under that TSV."
        }

        Write-Host ""
        Write-Host "--- building the per-line execution map (coverage.ps1, a few minutes) ---"
        $map = Join-Path $runDir "linemap.tsv"
        $cov = Join-Path $repoRoot "coverage.ps1"
        # ⚠ gcov must run with the repository root as the working directory, or it
        # reports "Cannot open source file generic/dirinput.c" and coverage.ps1
        # throws. coverage.ps1 documents this; honor it from here too.
        Push-Location $repoRoot
        try {
            & powershell.exe -ExecutionPolicy Bypass -NoProfile -File $cov -LineMapPath $map | Out-Null
            if ($LASTEXITCODE -ne 0) { throw "coverage.ps1 failed (exit $LASTEXITCODE); cannot split without a line map" }
        } finally { Pop-Location }
        if (-not (Test-Path -LiteralPath $map)) { throw "coverage.ps1 wrote no line map at $map" }

        # ⚠ STRIP THE COMMENT BLOCK BEFORE PARSING, DO NOT FILTER AFTER. The map
        # leads with four # lines, and Import-Csv takes the FIRST line of a file as
        # its header -- so it read the comment as the header, produced one garbage
        # row, and the split reported all 949 survivors as NO-RECORD without
        # anything looking wrong. Filtering afterwards cannot fix a header that was
        # already mis-taken.
        $hit = @{}
        $mapRows = @(Get-Content -LiteralPath $map | Where-Object { $_ -notmatch '^\s*#' }) |
                   ConvertFrom-Csv -Delimiter "`t"
        foreach ($row in $mapRows) { $hit["$($row.file):$($row.line)"] = ($row.hit -eq "1") }
        if ($hit.Count -lt 100) { throw "the line map parsed to only $($hit.Count) records; refusing to split against it" }
        Write-Host ("  {0} line records" -f $hit.Count)

        $splitTsv = Join-Path $ResultsPath "survivors-split.tsv"
        [IO.File]::WriteAllText($splitTsv, "file`tline`tcol`toperator`tfrom`tto`treach`r`n", $utf8NoBom)
        $tallyS = @{}
        foreach ($m in $todo) {
            $key = "$($m.file):$($m.line)"
            $reach = if (-not $hit.ContainsKey($key)) { "NO-RECORD" }
                     elseif ($hit[$key]) { "REACHED" } else { "UNREACHED" }
            # Keyed by file AND operator, like the census summary above: a
            # per-file rate that blends a ROR boundary shift with an OFF operand
            # shift is one number answering two questions.
            $sk = "$($m.file)`t$($m.operator)"
            if (-not $tallyS.ContainsKey($sk)) { $tallyS[$sk] = @{} }
            if (-not $tallyS[$sk].ContainsKey($reach)) { $tallyS[$sk][$reach] = 0 }
            $tallyS[$sk][$reach]++
            [IO.File]::AppendAllText($splitTsv, ("{0}`t{1}`t{2}`t{3}`t{4}`t{5}`t{6}`r`n" -f
                $m.file, $m.line, $m.col, $m.operator, $m.from, $m.to, $reach), $utf8NoBom)
        }

        Write-Host ""
        Write-Host "########## survivors, split by whether a test reaches the line ##########" -ForegroundColor Cyan
        Write-Host "  REACHED   a test runs it and does not assert -- usually a few lines in an existing test"
        Write-Host "  UNREACHED no test runs it -- needs a new case that gets there first"
        Write-Host "  NO-RECORD gcov has no line record (file-scope data). NOT a synonym for unreached."
        Write-Host ""
        Write-Host ("  {0,-22} {1,4} {2,10} {3,10} {4,10} {5,9}" -f "file", "op", "survivors", "REACHED", "UNREACHED", "NO-REC")
        $tR = 0; $tU = 0; $tN = 0
        foreach ($sk in ($tallyS.Keys | Sort-Object)) {
            $t = $tallyS[$sk]
            $f, $op = $sk -split "`t", 2
            $g = { param($k) if ($t.ContainsKey($k)) { $t[$k] } else { 0 } }
            $r = & $g "REACHED"; $u = & $g "UNREACHED"; $nr = & $g "NO-RECORD"
            $tR += $r; $tU += $u; $tN += $nr
            Write-Host ("  {0,-22} {1,4} {2,10} {3,10} {4,10} {5,9}" -f $f, $op, ($r + $u + $nr), $r, $u, $nr)
        }
        $tot = $tR + $tU + $tN
        Write-Host ""
        Write-Host ("  {0} survivors: {1} REACHED ({2:P0}), {3} UNREACHED ({4:P0}), {5} NO-RECORD." -f
            $tot, $tR, $(if ($tot) { $tR / $tot } else { 0 }), $tU, $(if ($tot) { $tU / $tot } else { 0 }), $tN)
        Write-Host "  Start with REACHED: the test already gets there, it just does not look."
        Write-Host ""
        Write-Host "  per-mutant detail: $splitTsv"
        return
    }

    # --- self-test ----------------------------------------------------------
    # Five canaries with known answers. The census does not run if any is wrong.
    Write-Host ""
    Write-Host "--- self-test ---"
    $canaries = @()

    # 1. MUST BE KILLED. random.c's multiplier is pinned by 7,767 checks including a
    #    recorded-sequence replay; nothing about this can pass.
    $rndPath = Join-Path $scratch "random.c"
    $rndText = [IO.File]::ReadAllText($rndPath)
    if ($rndText -notmatch '1103515245') { throw "self-test: random.c no longer contains the LCG multiplier the must-kill canary uses" }
    $canaries += @{ name = "must-kill"; file = "random.c"; expect = "KILLED"
                    text = ($rndText -replace '1103515245', '1103515247') }

    # 2. MUST NOT COMPILE. Proves INVALID is not being scored as KILLED, which is
    #    the single most damaging thing this script could get wrong.
    if ($rndText -notmatch 'static unsigned long nextvalue') { throw "self-test: random.c's nextvalue signature changed; the must-be-invalid canary needs a new anchor" }
    $canaries += @{ name = "must-be-invalid"; file = "random.c"; expect = "INVALID"
                    text = ($rndText -replace 'static unsigned long nextvalue', 'static unsigned long @@@ nextvalue') }

    # 3. MUST SURVIVE, and must survive BY CONSTRUCTION rather than by anyone's
    #    judgment: the line is inside the inactive arm of a NO_FIX_* toggle, so the
    #    compiler never sees it. This independently exercises the compiled-line
    #    filter -- if the blank-line handling in Get-CompiledMap regressed, this
    #    canary is what says so.
    #
    #    ⚠ NOT "one of the known equivalent mutants". That would be circular: if the
    #    equivalence judgment is wrong, the canary certifies the bug.
    $msPath = Join-Path $scratch "mslogic.c"
    $msText = [IO.File]::ReadAllText($msPath)
    $msLines = $msText -split "`n"
    $inactive = $null
    for ($k = 0; $k -lt $msLines.Count; $k++) {
        $ln = $k + 1
        if (-not $compiled.lines["mslogic.c"].ContainsKey($ln) -and $msLines[$k] -match '^\s+&&\s*!\w' ) { $inactive = $k; break }
    }
    if ($null -eq $inactive) {
        # 🔴 THROW, DO NOT SKIP. "No uncompiled line could be found" is the
        # SIGNATURE of the regression this canary guards: if the blank-line filter
        # in Get-CompiledMap breaks so that every line looks compiled, $inactive is
        # null. Warning and carrying on would disable the check in precisely the
        # case it exists for, then print "all canaries correct" and census a tree
        # whose inactive #else arms are all being treated as mutable.
        throw "self-test: no inactive preprocessor arm found for the must-survive canary. Either mslogic.c changed shape, or the compiled-line filter is broken -- which is the thing this canary exists to catch."
    } else {
        $copy = $msLines.Clone()
        $copy[$inactive] = $copy[$inactive] -replace '!', ''
        $canaries += @{ name = "must-survive"; file = "mslogic.c"; expect = "SURVIVED"
                        text = ($copy -join "`n"); detail = "mslogic.c:$($inactive + 1), not compiled" }
    }

    # 4. MUST TIME OUT. Without this the timeout, the taskkill /T tree kill and the
    #    lock-recovery path are untested code that a multi-hour run bets on, and the
    #    first real hang wedges the census.
    $lcg = 'return ((value * 1103515245UL) + 12345UL) & 0x7FFFFFFFUL;'
    if (-not $rndText.Contains($lcg)) { throw "self-test: random.c's generator body changed; the must-timeout canary needs a new anchor" }
    #    ⚠ PINNED TO ONE TEST, AND THAT MATTERS. random.c is compiled into four
    #    tests; running the shortened deadline against all of them means a healthy
    #    test that happens to exceed it reports TIMEOUT, and the canary passes
    #    GREEN without the infinite loop ever executing -- a self-test that
    #    certifies itself. random_test is the one pinned here, so the only way to
    #    see TIMEOUT is for the hang to actually happen.
    $canaries += @{ name = "must-timeout"; file = "random.c"; expect = "TIMEOUT"; only = "random_test"
                    text = $rndText.Replace($lcg, 'for (;;) { } return 0;') }

    # 5. MUST SURVIVE THE PLAIN PASS AND DIE UNDER THE SANITIZER. This is the canary
    #    that makes -Escalate mean anything: without it, a yield of zero is
    #    indistinguishable from a sanitize pass that never ran.
    #
    #    ⚠ IT IS SYNTHETIC ON PURPOSE, AND THAT IS THE SECOND-BEST OUTCOME. The
    #    right canary would have been reverting jc-50 -- this fork's own headline
    #    defect, movelaws[] indexed by a cell's bottom layer -- which CLAUDE.md
    #    records as leaving every local layer green except -Sanitize. Measured
    #    2026-09-11, that is no longer true of EITHER site: jc-57 added direct cases
    #    for movelaw_block() and movelaw_creature(), and the plain pass now fails
    #    both reverts with real assertions ("movelaw_block(MOVELAWCOUNT): expected 0,
    #    got 101"). The gap closed, so the canary had to be built rather than found.
    #
    #    A volatile write forces the multiply to happen at -O1; the result is read by
    #    nobody, so the plain pass sees 7,767 unchanged checks while UBSan traps on
    #    the signed overflow.
    $canaries += @{ name = "needs-sanitizer"; file = "random.c"; expect = "SURVIVED"
                    expectSanitized = "KILLED"; only = "random_test"
                    text = $rndText.Replace($lcg,
                        "{ static volatile int sink; int q = (int)value | 1; sink = q * 2147483647; }`n    $lcg") }

    foreach ($c in $canaries) {
        $path = Join-Path $scratch $c.file
        $pristine = $pristineText[$c.file]
        if ($null -eq $pristine) { $pristine = [IO.File]::ReadAllText($path); $pristineText[$c.file] = $pristine }
        if ($c.text -eq $pristine) { throw "self-test: the $($c.name) canary did not change $($c.file)" }
        [IO.File]::WriteAllText($path, $c.text, $utf8NoBom)
        try {
            $verdict = "SURVIVED"
            $note = ""
            $timeout = if ($c.expect -eq "TIMEOUT") { 30 } else { $RunTimeoutSec }
            # 🔴 @() AROUND THE WHOLE `if`, and this is the THIRD time this exact
            # PowerShell rule has bitten in this file. An `if` used as an expression
            # UNROLLS a one-element array to a scalar, so this was the string
            # "random_test" rather than a list containing it -- and $canaryTests[0]
            # then indexed the string and asked the runner for a test named "r".
            # foreach over a scalar string iterates once with the whole value, so
            # every canary that only looped kept working and only the indexing
            # exposed it. Same rule that sent -DTWPLUSPLUS to gcc one letter at a
            # time; see Get-CompiledMap.
            $canaryTests = @(if ($c.only) { @($c.only) } else { $compiled.covering[$c.file] })
            # The canaries run in whichever layer this invocation is about to measure
            # in, so they prove THAT layer's plumbing -- except the needs-sanitizer
            # canary, whose whole claim is about the plain pass and which therefore
            # always takes its first reading there.
            $mainSanitize = if ($c.expectSanitized) { $false } else { $useSanitize }
            foreach ($testName in $canaryTests) {
                $r = Invoke-UnitTests $scratch $testName $outDir $timeout $mainSanitize
                if ($r.timedOut) { $verdict = "TIMEOUT"; $note = "$testName"; break }
                $v = Get-TestVerdict $r.rows $baseline[$testName] $testName
                if ($v.verdict -eq "KILLED" -or $v.verdict -eq "INVALID" -or $v.verdict -eq "ERROR") {
                    $verdict = $v.verdict; $note = $v.note; break
                }
            }
            # A canary that declares expectSanitized is asserting the DIFFERENCE
            # between the two layers, so it has to run both. Checking only the
            # sanitize half would pass just as happily if the plain half were also
            # failing, which is the thing it exists to rule out.
            $sanVerdict = ""
            if ($c.expectSanitized) {
                $rs = Invoke-UnitTests $scratch $canaryTests[0] $outDir $RunTimeoutSec $true
                if ($rs.timedOut) { $sanVerdict = "TIMEOUT" }
                else {
                    $vs = Get-TestVerdict $rs.rows $baseline[$canaryTests[0]] $canaryTests[0]
                    $sanVerdict = if ($vs.verdict -eq "PASSED") { "SURVIVED" } else { $vs.verdict }
                    if ($vs.note) { $note = "sanitize: $($vs.note)" }
                }
            }
        } finally {
            [IO.File]::WriteAllText($path, $pristine, $utf8NoBom)
        }
        $ok = ($verdict -eq $c.expect)
        if ($c.expectSanitized) { $ok = $ok -and ($sanVerdict -eq $c.expectSanitized) }
        $color = if ($ok) { "Green" } else { "Red" }
        $shown = if ($c.expectSanitized) { "$verdict/$sanVerdict" } else { $verdict }
        $want = if ($c.expectSanitized) { "$($c.expect)/$($c.expectSanitized)" } else { $c.expect }
        Write-Host ("  {0,-16} expected {1,-18} got {2,-18} {3}" -f $c.name, $want, $shown, $note) -ForegroundColor $color
        if (-not $ok) {
            throw "self-test FAILED on the $($c.name) canary. The census is not run: a harness that gets this wrong reports a number nobody can check."
        }
    }
    Write-Host "  all canaries correct" -ForegroundColor Green

    if ($SelfTest) {
        Write-Host ""
        Write-Host "-SelfTest given; stopping before the census."
        return
    }

    # --- escalation ---------------------------------------------------------
    # Re-runs the mutants an earlier census recorded as SURVIVED, this time under
    # the sanitize layer, and reports how many of them it catches.
    #
    # WHY THIS EXISTS. SURVIVED from the plain pass means only "the plain pass did
    # not notice". The mutants this fork cares most about are memory-safety bounds,
    # and the plain pass structurally cannot see a bad read that does not happen to
    # change a value a test asserts on. Left unescalated, the survivor list -- which
    # is the work queue for every test anyone writes next -- is padded with mutants
    # a layer we already run on every push would have caught.
    if ($Escalate -or $Recheck) {
        # Two modes, one loop. -Escalate re-runs the survivors under the sanitize
        # layer to ask "would a layer we already run have caught this?". -Recheck
        # re-runs them on the ordinary pass to ask "does it die NOW?", which is the
        # loop for closing a survivor queue: add assertions, recheck, see what died.
        # Identical machinery one flag apart, so the two cannot drift.
        $srcTsv = if ($Escalate) { $Escalate } else { $Recheck }
        $underSanitize = [bool]$Escalate
        if (-not (Test-Path -LiteralPath $srcTsv)) { throw "no such file: $srcTsv" }
        $prior = @(Import-Csv -LiteralPath $srcTsv -Delimiter "`t")
        $priorSurv = @($prior | Where-Object { $_.verdict -eq "SURVIVED" })
        if ($priorSurv.Count -eq 0) { throw "$srcTsv records no SURVIVED mutants" }
        # -Module narrows $sources before enumeration, so a recheck of one file only
        # considers rows the enumeration still contains.
        if ($Module) { $priorSurv = @($priorSurv | Where-Object { $sources -contains $_.file }) }

        # Matched against a FRESH enumeration rather than trusting the recorded
        # offsets. If the source has moved under the TSV, the identities stop
        # matching and that is something to stop on, not to silently skip: an
        # escalation run against stale coordinates would mutate the wrong tokens and
        # report a yield for mutants that no longer exist.
        $index = @{}
        foreach ($m in $mutants) { $index["$($m.file)|$($m.line)|$($m.col)|$($m.from)|$($m.to)"] = $m }
        $todo = New-Object Collections.ArrayList
        $missing = 0
        foreach ($row in $priorSurv) {
            $key = "$($row.file)|$($row.line)|$($row.col)|$($row.from)|$($row.to)"
            if ($index.ContainsKey($key)) { [void]$todo.Add($index[$key]) } else { $missing++ }
        }
        if ($missing -gt 0) {
            throw "-Escalate: $missing of $($priorSurv.Count) recorded survivors do not match a mutant enumerated from the current source. The tree has moved under that TSV; re-run the census instead."
        }

        Write-Host ""
        Write-Host ("--- {0} {1} recorded survivors, {2} ---" -f $(if ($underSanitize) { "escalating" } else { "rechecking" }), $todo.Count, $(if ($underSanitize) { "sanitize layer" } else { "ordinary pass" }))
        $esc = Join-Path $ResultsPath $(if ($underSanitize) { "escalated.tsv" } else { "rechecked.tsv" })
        [IO.File]::WriteAllText($esc, ("file`tline`tcol`toperator`tfrom`tto`t" + $(if ($underSanitize) { "sanitizeVerdict" } else { "verdict" }) + "`tkiller`tnote`r`n"), $utf8NoBom)
        $caught = @{}
        $escTally = @{}
        $done = 0
        $started = Get-Date

        foreach ($m in $todo) {
            $done++
            $path = Join-Path $scratch $m.file
            $pristine = $pristineText[$m.file]
            $mutated = New-MutatedText $pristine $m
            if ($mutated -eq $pristine) { throw "escalation: mutant $($m.file):$($m.line) produced identical text" }
            [IO.File]::WriteAllText($path, $mutated, $utf8NoBom)

            $verdict = "SURVIVED"; $note = ""; $killer = ""
            $order = @($compiled.covering[$m.file] | Sort-Object { $cost[$_] })
            try {
                foreach ($testName in $order) {
                    $r = Invoke-UnitTests $scratch $testName $outDir $RunTimeoutSec $underSanitize
                    if ($r.timedOut) { $verdict = "TIMEOUT"; $note = $testName; break }
                    $v = Get-TestVerdict $r.rows $baseline[$testName] $testName
                    if ($v.verdict -eq "KILLED") {
                        # Same re-run discipline as the census: a flake is a false
                        # kill and the bias only points one way.
                        $again = Invoke-UnitTests $scratch $testName $outDir $RunTimeoutSec $underSanitize
                        $v2 = Get-TestVerdict $again.rows $baseline[$testName] $testName
                        if ($v2.verdict -ne "KILLED") { $verdict = "FLAKY"; $note = "$($v.note) then $($v2.verdict)" }
                        else { $verdict = "KILLED"; $killer = $testName; $note = $v.note }
                        break
                    }
                    if ($v.verdict -eq "INVALID" -or $v.verdict -eq "ERROR") { $verdict = $v.verdict; $note = $v.note; break }
                }
            } finally {
                [IO.File]::WriteAllText($path, $pristine, $utf8NoBom)
            }

            $problems = @(Compare-TreeManifest $pristineManifest $scratch $pristineDirs)
            if ($problems.Count -gt 0) {
                $fatal = @($problems | Where-Object { $_.kind -eq "fatal" })
                if ($fatal.Count -gt 0) {
                    throw "the scratch tree changed while escalating $($m.file):$($m.line) -- $(($fatal | ForEach-Object { "$($_.what): $($_.path)" }) -join '; ')."
                }
                Remove-Debris $scratch $problems
                $still = @(Compare-TreeManifest $pristineManifest $scratch $pristineDirs)
                if ($still.Count -gt 0) { throw "could not restore the scratch tree after $($m.file):$($m.line)." }
            }

            # File AND operator, for the reason the census summary gives.
            $ek = "$($m.file)`t$($m.operator)"
            if (-not $escTally.ContainsKey($ek)) { $escTally[$ek] = @{} }
            if (-not $escTally[$ek].ContainsKey($verdict)) { $escTally[$ek][$verdict] = 0 }
            $escTally[$ek][$verdict]++
            [IO.File]::AppendAllText($esc, ("{0}`t{1}`t{2}`t{3}`t{4}`t{5}`t{6}`t{7}`t{8}`r`n" -f
                $m.file, $m.line, $m.col, $m.operator, $m.from, $m.to, $verdict, $killer, $note), $utf8NoBom)

            if ($done % 25 -eq 0 -or $done -eq $todo.Count) {
                $rate = ((Get-Date) - $started).TotalSeconds / $done
                $left = [TimeSpan]::FromSeconds($rate * ($todo.Count - $done))
                Write-Host ("  {0,5}/{1}  {2,-9} {3}:{4}  (~{5:hh\:mm\:ss} left)" -f
                    $done, $todo.Count, $verdict, $m.file, $m.line, $left)
            }
        }

        Write-Host ""
        Write-Host $(if ($underSanitize) { "########## sanitizer yield over plain-pass survivors ##########" } else { "########## recheck: which recorded survivors die now ##########" }) -ForegroundColor Cyan
        Write-Host ("  {0,-22} {1,4} {2,10} {3,8} {4,8} {5,7}" -f "file", "op", "rerun", "died", "still", "rate")
        $tc = 0; $ts = 0
        foreach ($ek in ($escTally.Keys | Sort-Object)) {
            $t = $escTally[$ek]
            $f, $op = $ek -split "`t", 2
            $g = { param($k) if ($t.ContainsKey($k)) { $t[$k] } else { 0 } }
            $k = & $g "KILLED"; $s = & $g "SURVIVED"
            $tc += $k; $ts += $s
            $n = $k + $s
            $y = if ($n -gt 0) { "{0:P0}" -f ($k / $n) } else { "n/a" }
            Write-Host ("  {0,-22} {1,4} {2,10} {3,8} {4,8} {5,7}" -f $f, $op, $n, $k, $s, $y)
        }
        Write-Host ""
        Write-Host ("  {0} of {1} recorded survivors now die ({2:P1})." -f
            $tc, ($tc + $ts), $(if (($tc + $ts) -gt 0) { $tc / ($tc + $ts) } else { 0 }))
        Write-Host $(if ($underSanitize) { "  Those are NOT test gaps. The remaining survivors are the real work queue." } else { "  The rest are still open." })
        Write-Host ""
        Write-Host "  per-mutant detail: $esc"
        return
    }

    # --- the census ---------------------------------------------------------
    Write-Host ""
    Write-Host ("--- censusing {0} mutants ---" -f $mutants.Count)
    $results = New-Object Collections.ArrayList
    $tally = @{}
    $done = 0
    $started = Get-Date
    $tsv = Join-Path $ResultsPath "mutants.tsv"
    [IO.File]::WriteAllText($tsv, "file`tline`tcol`toperator`tfrom`tto`tverdict`tkiller`tcheckDrift`tdebris`tnote`r`n", $utf8NoBom)

    foreach ($m in $mutants) {
        $done++
        $path = Join-Path $scratch $m.file
        $pristine = $pristineText[$m.file]
        $mutated = New-MutatedText $pristine $m

        # Rule 1 of not lying: prove the edit happened. This is the exact bug the
        # previous census had -- rows reported as survivors with nothing applied.
        if ($mutated -eq $pristine) { throw "mutant $($m.file):$($m.line) produced identical text" }
        [IO.File]::WriteAllText($path, $mutated, $utf8NoBom)
        $onDisk = [IO.File]::ReadAllText($path)
        if ($onDisk -ne $mutated) { throw "mutant $($m.file):$($m.line) did not reach disk intact" }

        $verdict = "SURVIVED"; $note = ""; $killer = ""; $drift = $false
        $order = @($compiled.covering[$m.file] | Sort-Object { $cost[$_] })
        try {
            foreach ($testName in $order) {
                $r = Invoke-UnitTests $scratch $testName $outDir $RunTimeoutSec
                if ($r.timedOut) { $verdict = "TIMEOUT"; $note = $testName; break }
                $v = Get-TestVerdict $r.rows $baseline[$testName] $testName
                if ($v.verdict -eq "KILLED") {
                    # Rule 5: believe a kill only if it repeats. A flake failing on a
                    # mutant is a false KILL, and the bias only points one way.
                    $again = Invoke-UnitTests $scratch $testName $outDir $RunTimeoutSec
                    $v2 = Get-TestVerdict $again.rows $baseline[$testName] $testName
                    if ($v2.verdict -ne "KILLED") { $verdict = "FLAKY"; $note = "$($v.note) then $($v2.verdict)" }
                    else { $verdict = "KILLED"; $killer = $testName; $note = $v.note }
                    break
                }
                if ($v.verdict -eq "INVALID" -or $v.verdict -eq "ERROR") { $verdict = $v.verdict; $note = $v.note; break }
                if ($v.drift) { $drift = $true }
            }
        } finally {
            [IO.File]::WriteAllText($path, $pristine, $utf8NoBom)
        }

        # Rule 7: an INVALID or a TIMEOUT might be the environment rather than the
        # mutant. Ask the pristine file the same question; if it also misbehaves,
        # the well is poisoned and nothing after this point would mean anything.
        if ($verdict -eq "INVALID" -or $verdict -eq "TIMEOUT" -or $verdict -eq "ERROR") {
            $check = Invoke-UnitTests $scratch $order[0] $outDir $RunTimeoutSec
            $cv = Get-TestVerdict $check.rows $baseline[$order[0]] $order[0]
            if ($check.timedOut -or $cv.verdict -ne "PASSED") {
                throw "after a $verdict verdict on $($m.file):$($m.line), the PRISTINE tree no longer passes $($order[0]) ($($cv.verdict) $($cv.note)). The environment is broken; the census is void."
            }
        }

        # Rule 6: debris from one mutant must not be allowed to fail every later one.
        # 🔴 DEBRIS IS A RESULT, NOT AN ENVIRONMENT FAILURE -- BUT IT STILL MUST NOT
        # PERSIST. Measured on the first real run: a settings.cpp mutant broke
        # settings_test.c's cleanup and left tw_settings_test_dir\tw_settings.ini in
        # the tree at mutant 950 of 1,267. Aborting there was too blunt; a mutation
        # that breaks directory cleanup is an ORDINARY outcome for that file, and
        # throwing discarded 949 good verdicts over it.
        #
        # What must not happen is the NEXT mutant running against a polluted tree, so
        # the debris is removed and the tree re-checked. If it cannot be restored,
        # THAT is fatal. Anything other than debris -- a tracked file modified or
        # deleted -- is fatal immediately: it means something wrote into the tree and
        # we no longer know what we are measuring.
        $debris = $false
        $problems = @(Compare-TreeManifest $pristineManifest $scratch $pristineDirs)
        if ($problems.Count -gt 0) {
            $fatal = @($problems | Where-Object { $_.kind -eq "fatal" })
            if ($fatal.Count -gt 0) {
                throw "the scratch tree changed while censusing $($m.file):$($m.line) -- $(($fatal | ForEach-Object { "$($_.what): $($_.path)" }) -join '; '). The census is void from here."
            }
            Remove-Debris $scratch $problems
            $still = @(Compare-TreeManifest $pristineManifest $scratch $pristineDirs)
            if ($still.Count -gt 0) {
                throw "could not restore the scratch tree after $($m.file):$($m.line) -- $(($still | ForEach-Object { "$($_.what): $($_.path)" }) -join '; '). The census is void from here."
            }
            $debris = $true
        }

        # Keyed by file AND operator: a blended per-file number would hide exactly
        # what the operators are for, which is that they ask different questions.
        $tkey = "$($m.file)`t$($m.operator)"
        if (-not $tally.ContainsKey($tkey)) { $tally[$tkey] = @{} }
        if (-not $tally[$tkey].ContainsKey($verdict)) { $tally[$tkey][$verdict] = 0 }
        $tally[$tkey][$verdict]++

        [void]$results.Add([ordered]@{
            file = $m.file; line = $m.line; col = $m.col; operator = $m.operator
            from = $m.from; to = $m.to; verdict = $verdict; killer = $killer
            checkDrift = $drift; debris = $debris; note = $note
        })
        # 🔴 APPENDED AS IT IS PRODUCED, NOT WRITTEN AT THE END. Every guard in this
        # loop aborts by throwing, and a violation at mutant 1,200 of 1,267 used to
        # discard all 1,200 rows -- including the one fact worth having, which is
        # WHICH mutant left the debris or broke the tree.
        [IO.File]::AppendAllText($tsv, ("{0}`t{1}`t{2}`t{3}`t{4}`t{5}`t{6}`t{7}`t{8}`t{9}`t{10}`r`n" -f
            $m.file, $m.line, $m.col, $m.operator, $m.from, $m.to, $verdict, $killer, $drift, $debris, $note), $utf8NoBom)

        if ($done % 25 -eq 0 -or $done -eq $mutants.Count) {
            $rate = ((Get-Date) - $started).TotalSeconds / $done
            $left = [TimeSpan]::FromSeconds($rate * ($mutants.Count - $done))
            Write-Host ("  {0,5}/{1}  {2,-9} {3}:{4}  (~{5:hh\:mm\:ss} left)" -f
                $done, $mutants.Count, $verdict, $m.file, $m.line, $left)
        }

        # An INVALID rate this high is the signature of a broken generator, not of a
        # codebase. Without a cap it is invisible -- it just quietly shrinks the
        # denominator and raises the headline.
        #
        # ⚠ The 50-mutant arming threshold used to mean `-Sample 40` ran with the
        # detector never armed -- and a small sample is exactly the shape someone
        # reaches for while DEVELOPING a new operator, which is when the generator is
        # most likely to be broken. The end-of-census check below covers that case.
        if ($done -ge 50) {
            $inv = @($results | Where-Object { $_.verdict -eq "INVALID" }).Count
            if (($inv / $done) -gt $MaxInvalidRate) {
                throw "$inv of $done mutants did not compile ($('{0:P1}' -f ($inv / $done))), over the -MaxInvalidRate of $('{0:P1}' -f $MaxInvalidRate). That is a generator bug, not a result."
            }
        }
    }

    # 🔴 PER FILE AND OPERATOR AS WELL AS OVERALL, because the whole-run rate is
    # not the backstop it looks like. Measured on this operator's first C++ run:
    # settings.cpp sat at more than 60% INVALID from a template misparse while
    # the tree-wide rate stayed near 2%, under the cap, and nothing refused it.
    # The documented answer -- "read the per-file INVALID column" -- makes the
    # gate depend on somebody reading a table after a run of hours. This reads it
    # for them. The 10-mutant floor keeps a two-mutant file from tripping it.
    foreach ($tk in ($tally.Keys | Sort-Object)) {
        $t = $tally[$tk]
        $f, $op = $tk -split "`t", 2
        $ti = if ($t.ContainsKey("INVALID")) { $t["INVALID"] } else { 0 }
        $tn = 0
        foreach ($v in $t.Values) { $tn += $v }
        if ($tn -ge 10 -and ($ti / $tn) -gt $MaxInvalidRate) {
            throw "$ti of $tn $op mutants in $f did not compile ($('{0:P1}' -f ($ti / $tn))), over the -MaxInvalidRate of $('{0:P1}' -f $MaxInvalidRate). That is a generator bug in one translation unit, which the whole-run rate below can hide. Rows written to $tsv."
        }
    }

    $inv = @($results | Where-Object { $_.verdict -eq "INVALID" }).Count
    if ($results.Count -gt 0 -and ($inv / $results.Count) -gt $MaxInvalidRate) {
        throw "$inv of $($results.Count) mutants did not compile ($('{0:P1}' -f ($inv / $results.Count))), over the -MaxInvalidRate of $('{0:P1}' -f $MaxInvalidRate). That is a generator bug, not a result. Rows written to $tsv."
    }

    # --- report -------------------------------------------------------------
    Write-Host ""
    Write-Host "########## per-file kill rate ##########" -ForegroundColor Cyan
    Write-Host "  Read THIS table. The blended figure below is a summary, not a grade,"
    Write-Host "  and it is NOT comparable to the 2026-09-08 audit's 45% -- that was 233"
    Write-Host "  mutations chosen by hand; this is a mechanical census whose headline"
    Write-Host "  moves with the operator mix alone."
    Write-Host ""
    Write-Host ("  {0,-22} {1,4} {2,7} {3,7} {4,8} {5,8} {6,6} {7,6} {8,7}" -f
        "file", "op", "killed", "lived", "invalid", "timeout", "flaky", "error", "rate")
    $tot = @{ KILLED = 0; SURVIVED = 0; INVALID = 0; TIMEOUT = 0; FLAKY = 0; ERROR = 0 }
    # Per operator as well as overall: the blended line below is printed once for
    # each, because one number spanning both is the misreading this whole file
    # argues against -- ROR and OFF ask different questions of the same suite.
    $perOp = @{}
    foreach ($tk in ($tally.Keys | Sort-Object)) {
        $t = $tally[$tk]
        $f, $op = $tk -split "`t", 2
        if (-not $perOp.ContainsKey($op)) {
            $perOp[$op] = @{ KILLED = 0; SURVIVED = 0; INVALID = 0; TIMEOUT = 0; FLAKY = 0; ERROR = 0 }
        }
        $g = { param($k) if ($t.ContainsKey($k)) { $t[$k] } else { 0 } }
        $k = & $g "KILLED"; $s = & $g "SURVIVED"; $i = & $g "INVALID"
        $o = & $g "TIMEOUT"; $fl = & $g "FLAKY"; $e = & $g "ERROR"
        foreach ($key in @("KILLED","SURVIVED","INVALID","TIMEOUT","FLAKY","ERROR")) {
            $tot[$key] += (& $g $key)
            $perOp[$op][$key] += (& $g $key)
        }
        $den = $k + $s
        $rate = if ($den -gt 0) { "{0:P0}" -f ($k / $den) } else { "n/a" }
        Write-Host ("  {0,-22} {1,4} {2,7} {3,7} {4,8} {5,8} {6,6} {7,6} {8,7}" -f $f, $op, $k, $s, $i, $o, $fl, $e, $rate)
    }
    Write-Host ""
    # 🔴 ONE BLENDED LINE PER OPERATOR, NEVER ONE ACROSS BOTH. The blended figure
    # is the number people quote, and a run of `-Operator ROR,OFF` printing a
    # single percentage would hand them exactly the reading this script spends a
    # header arguing against: the two operators ask different questions, and an
    # average of the answers means nothing. Caught in review, on the run the
    # per-operator tally had just been introduced for.
    foreach ($op in ($perOp.Keys | Sort-Object)) {
        $p = $perOp[$op]
        $pden = $p.KILLED + $p.SURVIVED
        Write-Host ("  blended [{0}]: {1} killed of {2} scored ({3:P1}). Excluded from the denominator: {4} invalid, {5} timed out, {6} flaky, {7} errored." -f
            $op, $p.KILLED, $pden, $(if ($pden) { $p.KILLED / $pden } else { 0 }), $p.INVALID, $p.TIMEOUT, $p.FLAKY, $p.ERROR)
    }
    $debrisCount = @($results | Where-Object { $_.debris }).Count
    if ($debrisCount -gt 0) {
        Write-Host ("  {0} mutants left files or directories behind in the tree (cleaned up and recorded). These broke a test's own cleanup path." -f $debrisCount) -ForegroundColor Yellow
    }
    $driftCount = @($results | Where-Object { $_.checkDrift }).Count
    if ($driftCount -gt 0) {
        Write-Host ("  {0} survivors changed the suite's check count -- the tests REACHED that code and declined to assert on it. Start there." -f $driftCount) -ForegroundColor Yellow
    }
    Write-Host ""
    Write-Host "  per-mutant detail: $tsv"

    if ($UpdateBaseline) {
        if ($Sample -gt 0) { throw "-UpdateBaseline refused: this was a sampled run. A partial census reads exactly as authoritative as a complete one." }
        # 🔴 A -Module RUN MAY WRITE NOW, and only the rows it measured. That
        # refusal predated the row-per-(file, operator) schema, when a partial run
        # would have rewritten the whole file as though it were complete. It is
        # now the opposite of useful: the survivor queue is worked ONE FILE AT A
        # TIME (about 100 seconds each), and refusing to record that meant the
        # baseline could only be refreshed by a run of hours -- so it simply went
        # stale, which is what a reader is pointed at as authoritative.
        # Every row carries its own file, operator and commit, so a partial
        # refresh cannot pass itself off as a whole one.
        if ($dirty.Count -gt 0) { throw "-UpdateBaseline refused: the working tree is dirty, so this number could never be reproduced." }
        $bl = Join-Path $repoRoot "docs\mutation-baseline.tsv"

        # 🔴 ONE ROW PER FILE AND OPERATOR, EACH CARRYING ITS OWN COMMIT, and rows
        # for operators this run did not measure are KEPT. A full OFF census takes
        # hours where a ROR one takes half an hour, so the two are recorded by
        # separate runs -- and a writer that rewrote the whole file would silently
        # delete the other operator's numbers every time. The per-row commit is
        # what stops the merge from lying: rows measured at different commits say
        # so individually, rather than hiding under one header line.
        # ⚠ ROWS WRITTEN BEFORE THE COMMIT COLUMN EXISTED CARRY THE OLD HEADER'S
        # COMMIT FORWARD, rather than being left one field short. A merged file
        # with two row widths is a file the next reader has to guess at, and the
        # commit those rows were measured at is normally sitting in the header
        # they came with. ⚠ NORMALLY: a hand-edited or truncated header has no
        # commit line to read, and then the column says "unknown" -- which is the
        # honest answer there, and is WARNED about rather than written quietly.
        $kept = @()
        $oldCommit = "unknown"
        if (Test-Path -LiteralPath $bl) {
            $blLines = [IO.File]::ReadAllLines($bl)
            foreach ($ln in $blLines) {
                if ($ln -match '^#\s*(?:commit|last run):\s*([0-9a-f]{7,40})') { $oldCommit = $Matches[1]; break }
            }
            foreach ($ln in $blLines) {
                if ($ln.StartsWith("#") -or $ln.StartsWith("file`t") -or -not $ln.Trim()) { continue }
                $cells = $ln -split "`t"
                # Replace a row only if THIS run measured that file and operator;
                # everything else is carried through untouched.
                if ($cells.Count -lt 2) { continue }
                if ($Operator -contains $cells[1]) {
                    # A FULL run of this operator replaces every row it has, so a
                    # row for a source that is no longer censused disappears --
                    # which is how a renamed or dropped file stops inflating a
                    # total somebody sums from this table. A -Module run replaces
                    # only what it measured and carries the rest through.
                    if (-not $Module) { continue }
                    if ($sources -contains $cells[0]) { continue }
                }
                if ($cells.Count -lt 9) {
                    if ($oldCommit -eq "unknown") {
                        Write-Host ("  WARNING: {0} {1} rows carried forward with commit 'unknown' -- the old header had no readable commit line." -f $cells[0], $cells[1]) -ForegroundColor Yellow
                    }
                    $kept += ($ln + "`t" + $oldCommit)
                } else { $kept += $ln }
            }
        }
        # The header says what this run actually covered, so a partial refresh
        # reads as one at a glance rather than only in the per-row commits.
        $runScope = ($Operator -join ',')
        if ($Module) { $runScope += ", only " + ($sources -join ', ') }

        $rows = @()
        foreach ($tk in ($tally.Keys | Sort-Object)) {
            $t = $tally[$tk]
            $f, $op = $tk -split "`t", 2
            $g = { param($k) if ($t.ContainsKey($k)) { $t[$k] } else { 0 } }
            $rows += ("{0}`t{1}`t{2}`t{3}`t{4}`t{5}`t{6}`t{7}`t{8}" -f $f, $op,
                (& $g "KILLED"), (& $g "SURVIVED"), (& $g "INVALID"), (& $g "TIMEOUT"),
                (& $g "FLAKY"), (& $g "ERROR"), $head)
        }
        $out = @(
            "# Mutation census, unit layer only. Regenerate with mutate.ps1 -UpdateBaseline.",
            "# NOT comparable to the 2026-09-08 hand-run audit: that was 233 hand-chosen",
            "# mutations, this is a mechanical census whose blended rate moves with the",
            "# operator mix. Read the per-file rows, and read the OPERATOR column with",
            "# them: ROR asks whether a relational boundary is pinned, OFF asks whether",
            "# the OPERANDS around it are -- the direction ROR structurally cannot reach.",
            "# A row is replaced only by a run of ITS operator; each carries the commit",
            "# it was measured at.",
            "# last run: $head ($runScope)",
            "file`toperator`tkilled`tsurvived`tinvalid`ttimeout`tflaky`terror`tcommit"
        ) + (@($kept + $rows) | Sort-Object)
        # LF explicitly; see the note in test\run-tests.ps1 about WriteAllLines.
        [IO.File]::WriteAllText($bl, (($out -join "`n") + "`n"), $utf8NoBom)
        Write-Host "  baseline written: $bl"
    }

    if ($tot.ERROR -gt 0) { exit 1 }
    exit 0
} catch {
    # 🔴 WITHOUT THIS CATCH, A FAILED STATEMENT ANYWHERE ABOVE EXITED 0.
    #
    # Found 2026-09-15, the hard way. From 2026-09-13 the comment beside the
    # baseline write held a literal CARRIAGE RETURN where "test\run" was meant (a
    # `\r` escape collapsed on the way in). PowerShell reads a bare CR as a line
    # break, so "un-tests.ps1 about WriteAllLines." ran as a command. That is a
    # STATEMENT-terminating error, not a script-terminating one like `throw`:
    # inside a try with no catch it jumps to `finally`, is printed, and execution
    # RESUMES AFTER THE TRY -- which is the end of the file. So a complete census
    # wrote no baseline and returned exit 0, and the only sign was one red
    # paragraph under a table of results. Measured, not reasoned: a flag checked
    # after the write, still inside the try, never ran either.
    #
    # Every such error now lands here and fails the run. (`throw` already exited
    # nonzero; it lands here too, so both read the same way.) CI's hygiene job
    # also refuses a bare CR in any tracked text file.
    Write-Host ""
    Write-Host ("mutate.ps1 FAILED: " + $_.Exception.Message) -ForegroundColor Red
    if ($_.InvocationInfo) { Write-Host ("  at " + $_.InvocationInfo.PositionMessage) -ForegroundColor Red }
    exit 1
} finally {
    # 🔴 DELETE THE TREE AND THE OBJECTS, NEVER $runDir ITSELF. The default
    # -ResultsPath lives under $runDir, and a `finally` that removed the whole
    # thing deleted the census's only durable output -- after printing its path.
    # PowerShell runs `finally` on `exit`, so a successful half-hour run ended by
    # handing back a path to a directory it had just removed.
    if ($KeepScratch) {
        Write-Host "scratch kept at $runDir"
    } else {
        Remove-Item -LiteralPath $scratch -Recurse -Force -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath $outDir -Recurse -Force -ErrorAction SilentlyContinue
    }
}
