<#
Builds and runs the golden-master engine snapshot (test/golden/golden.c).

    powershell -ExecutionPolicy Bypass -File test\run-golden.ps1
    powershell -ExecutionPolicy Bypass -File test\run-golden.ps1 -Update

WHAT THIS IS FOR

It drives every level in every committed .dat through BOTH engines and hashes
the resulting gamestate, so an engine change that alters behavior shows up on
every push. Before it existed, nothing in CI could detect one: the whole
automated replay gate was a single end-to-end case with one valid and one
invalid solution.

🔴 READ test\golden\golden.c BEFORE TRUSTING OR CHANGING ANYTHING HERE. In
particular it records, with measurements, that this detects only 2 of the 32
NO_FIX_* engine toggles, and that raising the tick count or the walk count was
tried and bought nothing. It is a smoke alarm, not an audit, and it does NOT
replace test\run-corpus.ps1 -- 18,640 recorded human solutions reach places a
random walker never will.

-Update REWRITES THE BASELINE, and that is a deliberate act. Read the diff and
justify every line of it in the commit message, exactly as for a corpus
differential. The outcome and ticks columns are there to make that reading
quick: if the digests moved but outcome/ticks did not, the DIGEST FORMULA
changed rather than the engine.
#>

[CmdletBinding()]
param(
    [switch]$Update,
    [string]$Cc = "gcc"
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$root = Split-Path -Parent $PSScriptRoot
Push-Location $root
try {
    # MSYS2's bin must be FIRST on PATH: the gcc driver cannot spawn cc1 unless
    # its own directory is on PATH, and when it cannot it fails with a nonzero
    # exit and no diagnostic at all. Same reason build.ps1 does this.
    $mingw = "C:\msys64\mingw64\bin"
    if (Test-Path $mingw) { $env:PATH = "$mingw;$env:PATH" }

    $out = Join-Path $env:TEMP "tw-golden.exe"
    $src = @(
        "test/golden/golden.c", "mslogic.c", "lxlogic.c", "encoding.c", "random.c"
    )

    # -Wno-unused-value / -Wno-unused-variable are for mslogic.c's OWN
    # pre-existing warnings (the _assert comma expression, and `value` in
    # resetdata). See CLAUDE.md section 5. They are not cover for this file.
    Write-Host "building the golden-master driver..."
    $cflags = @(
        "-std=gnu11", "-Wall", "-Wextra", "-Werror",
        "-Wno-unused-value", "-Wno-unused-variable",
        "-O2", "-I."
    )
    & $Cc @cflags -o $out @src
    if ($LASTEXITCODE -ne 0) { throw "the golden-master driver did not build" }

    # Every committed .dat. Sorted, so the row order is stable across machines
    # -- Get-ChildItem's order is not guaranteed and the baseline is positional.
    $dats = Get-ChildItem -Path (Join-Path $root "data") -Filter *.dat -File |
            Sort-Object Name | ForEach-Object { "data/" + $_.Name }
    if (-not $dats -or $dats.Count -eq 0) {
        throw "no .dat files under data\ -- refusing to report success on an empty run"
    }
    Write-Host ("level files: {0}" -f ($dats -join ", "))

    if ($Update) {
        & $out -update @dats
        if ($LASTEXITCODE -ne 0) { throw "the snapshot could not be written" }
        Write-Host ""
        Write-Host "BASELINE REWRITTEN. Read the diff before committing it:" -ForegroundColor Yellow
        Write-Host "  git diff -- test/golden/engine-snapshot.tsv" -ForegroundColor Yellow
        Write-Host "If the digests moved but the outcome and ticks columns did not," -ForegroundColor Yellow
        Write-Host "the digest formula changed, not the engine." -ForegroundColor Yellow
        exit 0
    }

    & $out -check @dats
    $rc = $LASTEXITCODE
    if ($rc -ne 0) {
        Write-Host ""
        Write-Host "The golden master changed. That is a QUESTION, not a verdict:" -ForegroundColor Red
        Write-Host "which engine rule did you alter, and did you mean to?" -ForegroundColor Red
        Write-Host "If you meant it, re-run with -Update and justify the diff." -ForegroundColor Red
        exit $rc
    }
    exit 0
}
finally {
    Pop-Location
}
