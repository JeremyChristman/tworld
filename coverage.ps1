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

Only the UNIT layer. The end-to-end tests drive a separately built executable
that carries no instrumentation, so nothing they exercise appears here -- which
means these numbers UNDERSTATE what the suite as a whole reaches. Building the
whole CMake tree with --coverage and running a batch verification would measure
mslogic.c, series.c, solution.c and encoding.c in one pass, and is the obvious
next step for anyone who wants a real number.

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
    [string]$Filter
)
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

    $gcdaFiles = @(Get-ChildItem -LiteralPath $work -Filter *.gcda -File -ErrorAction SilentlyContinue)
    if ($gcdaFiles.Count -eq 0) {
        throw "no .gcda files were produced in $work -- the instrumented binaries did not run"
    }
    Write-Host ("collected {0} coverage data file(s)" -f $gcdaFiles.Count)

    # ⚠ gcov MUST run with the repository root as the working directory. The
    # paths recorded in the .gcno are relative to where the compiler ran, so from
    # anywhere else gcov reports "Cannot open source file generic/dirinput.c" --
    # and then prints numbers anyway, having never read the source. Silent, and
    # wrong in the flattering direction.
    Push-Location $root
    try {
        foreach ($gcda in $gcdaFiles) {
            & $gcov --json-format --branch-probabilities --object-directory $work $gcda.FullName 2>&1 | Out-Null
        }
    } finally {
        Pop-Location
    }

    # gcov writes <name>.gcov.json.gz into the CURRENT directory, which is $root
    # above -- so they land in the repo and must be cleaned up afterwards.
    $jsonFiles = @(Get-ChildItem -LiteralPath $root -Filter *.gcov.json.gz -File -ErrorAction SilentlyContinue)
    if ($jsonFiles.Count -eq 0) {
        throw "gcov produced no JSON output. Check that gcov $(& $gcov --version | Select-Object -First 1) supports --json-format."
    }

    # Union the per-line and per-branch counts across every run. The C and C++
    # builds of a test are separate translation units with separate .gcda, so a
    # line reached only by the C++ build must still count as reached -- taking
    # one run's numbers, or the maximum of the two, would both be wrong.
    $lineHit = @{}   # "file:line"   -> covered anywhere
    $lineAll = @{}
    $brHit   = @{}   # "file:line:i" -> taken anywhere
    $brAll   = @{}

    foreach ($jf in $jsonFiles) {
        $inStream = [IO.File]::OpenRead($jf.FullName)
        $gz = New-Object IO.Compression.GZipStream($inStream, [IO.Compression.CompressionMode]::Decompress)
        $reader = New-Object IO.StreamReader($gz)
        $text = $reader.ReadToEnd()
        $reader.Close(); $gz.Close(); $inStream.Close()

        $data = $text | ConvertFrom-Json
        foreach ($file in $data.files) {
            $name = ($file.file -replace '\\', '/')
            # Anything outside this repository (system headers) and the test
            # scaffolding itself. See the header for why the tests are excluded.
            if ($name -match '^[A-Za-z]:/' -and $name -notmatch [regex]::Escape(($root -replace '\\','/'))) { continue }
            if ($name -match '(^|/)test/') { continue }
            if ($name -match '_test\.c$' -or $name -match 'tw_test\.h$' -or $name -match 'tw_fixture\.h$') { continue }
            $short = $name -replace '^.*?/(?=[^/]+$)', ''
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
    Remove-Item -LiteralPath ($jsonFiles | ForEach-Object { $_.FullName }) -Force -ErrorAction SilentlyContinue

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
    Write-Host "  These cover the UNIT layer only; the end-to-end tests run an uninstrumented"
    Write-Host "  build, so what they reach is not counted. See the header of this script."

    if ($UpdateBaseline) {
        $dir = Split-Path -Parent $baselineFile
        if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }
        $sb = New-Object Text.StringBuilder
        [void]$sb.AppendLine("# Coverage baseline, unit layer only. Regenerate with coverage.ps1 -UpdateBaseline.")
        [void]$sb.AppendLine("# A FLOOR, not a grade. Raising a number here is progress; lowering one needs a reason.")
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
        if ($regressed.Count -gt 0) {
            Write-Host ""
            Write-Host "coverage regressed against docs\coverage-baseline.tsv:" -ForegroundColor Red
            foreach ($m in $regressed) { Write-Host "  - $m" -ForegroundColor Red }
            Write-Host "Add a test, or run -UpdateBaseline and say in the commit why the number moved." -ForegroundColor Yellow
            exit 1
        }
        Write-Host "coverage is at or above the recorded baseline" -ForegroundColor Green
    }
} finally {
    if (Test-Path $work) { Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue }
    # gcov drops these in the repository root; make sure a failure partway does
    # not leave them behind as untracked files.
    Get-ChildItem -LiteralPath $root -Filter *.gcov.json.gz -File -ErrorAction SilentlyContinue |
        Remove-Item -Force -ErrorAction SilentlyContinue
}

exit 0
