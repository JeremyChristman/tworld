<#
Runs the Tile World unit tests.

    powershell -ExecutionPolicy Bypass -File test\run-tests.ps1

Each test is a self-contained C file that compiles the code under test directly,
so no configured CMake build tree is needed -- only a C compiler. Every test is
built TWICE, once as C and once as C++, because generic/in.c is compiled as C by
the SDL build and as C++ by the shipped Qt build (through generic/_in.cpp), and
a construct that is valid in only one of them breaks a build that nobody here
runs by hand.

Exits nonzero if any test fails, so it can gate a release.
#>
param(
    [string]$Cc,
    [string]$Cxx
)
$ErrorActionPreference = "Stop"

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

$stubDir = Join-Path $PSScriptRoot "stub"
$outDir = Join-Path ([System.IO.Path]::GetTempPath()) "tworld-tests"
if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Path $outDir | Out-Null }

$tests = Get-ChildItem -Path $PSScriptRoot -Filter "*_test.c" | Sort-Object Name
if ($tests.Count -eq 0) { throw "no *_test.c files found in $PSScriptRoot" }

$failed = 0
foreach ($test in $tests) {
    foreach ($lang in @("c", "c++")) {
        $compiler = if ($lang -eq "c") { $Cc } else { $Cxx }
        $std = if ($lang -eq "c") { "-std=c99" } else { "-std=c++11" }
        $exe = Join-Path $outDir "$($test.BaseName)-$($lang -replace '\+','p').exe"

        # A test declares any extra compiler flags it needs in a TESTFLAGS
        # comment on one of its first lines, so the knowledge lives with the
        # test rather than in a lookup table here that nobody updates.
        # input_test.c needs -DTWPLUSPLUS because the shipped Qt build defines
        # it (see the top-level CMakeLists) and it governs which entries exist
        # in the keycmds tables -- without it the harness would be exercising
        # a key map the released game does not have.
        $extra = @()
        $decl = Select-String -Path $test.FullName -Pattern 'TESTFLAGS:(.*)' |
                Select-Object -First 1
        if ($decl) {
            $extra = $decl.Matches[0].Groups[1].Value.Trim() -replace '\*/\s*$','' -split '\s+' |
                     Where-Object { $_ -ne '' }
        }

        Write-Host ""
        Write-Host "=== $($test.Name) [$lang] ===" -ForegroundColor Cyan
        if ($extra.Count -gt 0) { Write-Host "    extra flags: $extra" }
        & $compiler $std -Wall -Wextra -I $stubDir $extra -x $lang -o $exe $test.FullName
        if ($LASTEXITCODE -ne 0) {
            Write-Host "COMPILE FAILED" -ForegroundColor Red
            $failed++
            continue
        }
        & $exe
        if ($LASTEXITCODE -ne 0) {
            Write-Host "TESTS FAILED" -ForegroundColor Red
            $failed++
        }
    }
}

Write-Host ""
if ($failed -gt 0) {
    Write-Host "$failed test run(s) FAILED" -ForegroundColor Red
    exit 1
}
Write-Host "all test runs passed" -ForegroundColor Green
exit 0
