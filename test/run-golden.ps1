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
    [string]$Cc = "gcc",

    # Rewrite the digests even though the tree is dirty. See Assert-CleanTree.
    [switch]$Force
)

# 🔴 A TOOL THAT REWRITES A COMMITTED TRUTH SOURCE MUST REFUSE A DIRTY TREE.
#
# These files are pins: the next run is judged against them. Regenerating one
# from a working tree nobody can reconstruct records a number that can never be
# checked again -- and worse, it LAUNDERS whatever is currently broken into the
# new baseline. An audit did exactly that: mutated the engine, watched the golden
# master go red across 342 digests, ran -Update on the dirty tree, and the gate
# was green afterwards with the mutation still in place.
#
# mutate.ps1 already had this guard and the reasoning; it was never propagated to
# the other three tools that regenerate a pin. -Force is the deliberate override,
# because there are legitimate reasons (a fixture change that genuinely moves
# every digest) and refusing outright would just get the guard deleted.
function Assert-CleanTree([string]$root, [string]$what, [bool]$force) {
    Push-Location $root
    try { $dirty = @(& git status --porcelain) } finally { Pop-Location }
    if ($dirty.Count -eq 0) { return }
    if ($force) {
        Write-Warning ("{0} on a DIRTY tree ({1} changed paths), because -Force was given. Whatever is uncommitted is being baked into the pin." -f $what, $dirty.Count)
        return
    }
    throw ("$what refused: the working tree is dirty ($($dirty.Count) changed paths), so this pin could never be reproduced -- and anything currently broken would be laundered into it. Commit or stash first, or pass -Force if you mean it.")
}


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
        Assert-CleanTree (Split-Path -Parent $PSScriptRoot) "run-golden.ps1 -Update" $Force.IsPresent
        & $out -update @dats
        if ($LASTEXITCODE -ne 0) { throw "the snapshot could not be written" }
        Write-Host ""
        Write-Host "BASELINE REWRITTEN. Read the diff before committing it:" -ForegroundColor Yellow
        Write-Host "  git diff -- test/golden/engine-snapshot.tsv" -ForegroundColor Yellow
        Write-Host "If the digests moved but the outcome and ticks columns did not," -ForegroundColor Yellow
        Write-Host "the digest formula changed, not the engine." -ForegroundColor Yellow
        # 🔴 AND IF THE ENGINE DID CHANGE, THIS BASELINE PROVES NOTHING ABOUT REPLAY.
        # An adversarial audit changed a movement rule, regenerated this file and
        # got an all-green suite -- legitimately, since that is what -Update is for.
        # A rewritten snapshot records that the engine now behaves DIFFERENTLY; it
        # cannot say whether anyone's recorded solutions still work. Only the
        # corpus differential can, and no CI job can run it. Say so at the moment
        # the baseline moves, not in a document nobody is reading right then.
        Write-Host ""
        Write-Host "If ENGINE BEHAVIOR changed, a green golden run now proves nothing about" -ForegroundColor Red
        Write-Host "replay. Before committing this, run the corpus differential by hand:" -ForegroundColor Red
        Write-Host "  test\run-corpus.ps1 (see its header, and CLAUDE.md section 5)" -ForegroundColor Red
        Write-Host "No CI job can run it -- the collection is private (docs/adr/0005)." -ForegroundColor Red
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
