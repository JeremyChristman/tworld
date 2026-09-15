<#
Checks (or rebuilds) the NO_FIX_* differential matrix.

    powershell -ExecutionPolicy Bypass -File test\run-nofix.ps1
    powershell -ExecutionPolicy Bypass -File test\run-nofix.ps1 -Search -Seeds 1000000

WHAT THE MATRIX IS FOR

mslogic.c carries 32 NO_FIX_* toggles, each isolating one engine fix from the
desync project (docs/adr/0002). Nothing tested any of them: the golden master
can distinguish only two of the 32 over all 903 committed levels, because a
random walk through a real level never CONSTRUCTS a tank on a cloner or a block
on a teleport.

test\nofix\nofix.c generates tiny rooms packed with exactly that furniture and
searches for an input whose result differs between a fix-on and a fix-off build.
Such an input is a WITNESS: proof the fix is live and reachable. The witnesses
are committed in test\nofix\nofix-matrix.tsv.

DEFAULT MODE (check) replays every recorded witness through both builds and
asserts the two still differ AND that both digests are unchanged. That fails if
a fix stops being reachable, if a toggle stops toggling, or if either behavior
changes.

-Search REDISCOVERS witnesses and rewrites the matrix. It is slow (a full sweep
is roughly half an hour) and it is a deliberate act: read the diff before
committing it. Rows that already have a witness keep it -- a witness is only
replaced when it stops working, so the file stays stable and its diffs stay
readable.

🔴 A BLANK ROW MEANS THE SEARCH DID NOT FIND ONE. It does not mean the fix is
dead code, and it is never grounds for deleting a fix. See the header of
test\nofix\nofix.c.
#>

[CmdletBinding()]
param(
    [switch]$Search,
    [long]$Seeds = 1000000,
    [string]$Cc = "gcc",

    # Rewrite the witness matrix even though the tree is dirty. See Assert-CleanTree.
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

    $matrixPath = Join-Path $root "test\nofix\nofix-matrix.tsv"
    $work = Join-Path $env:TEMP "tw-nofix"
    if (-not (Test-Path $work)) { $null = New-Item -ItemType Directory $work }

    $src = @("test/nofix/nofix.c", "mslogic.c", "encoding.c", "random.c")
    $cflags = @("-std=gnu11", "-w", "-O2", "-I.")

    # 🔴 NEVER `2>&1` A NATIVE COMMAND HERE. Windows PowerShell 5.1 wraps each
    # stderr line from an exe in an ErrorRecord, and under
    # ErrorActionPreference = "Stop" that becomes a TERMINATING error even when
    # the exe exits 0. gcc writes notes and warnings to stderr routinely, so a
    # perfectly successful build throws.
    #
    # This is not a hypothetical: the identical mistake in test\run-qt-tests.ps1
    # turned an intended SKIP into a FAILURE and cost the jc-49 release. It was
    # made again here, in a script written after that was documented. Redirect
    # to a FILE and read the file.
    $buildLog = Join-Path $work "build.log"

    function Build-Variant([string]$toggle, [string]$out) {
        $a = @() + $cflags
        if ($toggle -ne "") {
            $a += "-D$toggle"
            $a += ("-DNOFIX_BUILD_ID_TOKEN=" + ($toggle -replace "^NO_FIX_", ""))
        }
        $a += @("-o", $out) + $src
        # Drop to "Continue" across the native call. Redirecting to a file is
        # NOT enough on 5.1 -- the ErrorRecord is created before the redirect
        # is applied, so the throw happens anyway. Only lowering the preference
        # stops it. Restored immediately afterwards.
        $prev = $ErrorActionPreference
        $ErrorActionPreference = "Continue"
        & $Cc @a 2>&1 | Out-File -FilePath $buildLog -Encoding ascii
        $ok = ($LASTEXITCODE -eq 0)
        $ErrorActionPreference = $prev
        return $ok
    }

    # 🔴 A CRASHING ENGINE IS A DIAGNOSIS, NOT A .NET EXCEPTION.
    #
    # Every digest used to be read as `((& $exe -one $seed) -split "`t")[0]`. An
    # audit widened encoding.c's run-length loop (`i-- && pos < ...` -> `||`), and
    # the FIX-ON build then segfaulted on the first witness seed -- exit
    # 0xC0000005, no output. Splitting nothing gives an empty array, StrictMode
    # throws on [0], and the script died with "Index was outside the bounds of the
    # array". It failed closed, but it named nothing: not that the SHIPPED engine
    # was the one crashing, not which seed, not that no toggle was involved. Read
    # as-is, it points an agent at the matrix for a defect that is in the engine.
    #
    # So every run of a nofix binary comes through here, and a crash comes back
    # as a value the caller can report. `-id` and `-one` both return 0 and print
    # exactly one line, so anything else is a crash.
    function Invoke-Nofix([string]$exe, [string[]]$arguments) {
        $prev = $ErrorActionPreference
        $ErrorActionPreference = "Continue"
        $out = @(& $exe @arguments 2>$null)
        $rc = $LASTEXITCODE
        $ErrorActionPreference = $prev
        $first = ""
        if ($out.Count -gt 0 -and $null -ne $out[0]) { $first = ([string]$out[0]).Trim() }
        return @{
            ok     = ($rc -eq 0 -and $first -ne "")
            rc     = $rc
            text   = $first
            fields = @($first -split "`t")
        }
    }
    function Format-Exit($rc) { return ("exit {0} (0x{0:X8})" -f [int]$rc) }

    # Every toggle mslogic.c actually defines, read from the source rather than
    # kept in a list here -- a list would go stale the first time one is added.
    $toggles = Select-String -Path (Join-Path $root "mslogic.c") -Pattern 'NO_FIX_\w+' -AllMatches |
               ForEach-Object { $_.Matches } | ForEach-Object { $_.Value } |
               Sort-Object -Unique
    Write-Host ("mslogic.c defines {0} NO_FIX_* toggle(s)" -f $toggles.Count)

    $defaultExe = Join-Path $work "default.exe"
    Write-Host "building the fix-on (default) engine..."
    if (-not (Build-Variant "" $defaultExe)) { throw "the default build failed" }

    if ($Search) {
        Assert-CleanTree (Split-Path -Parent $PSScriptRoot) "run-nofix.ps1 -Search" $Force.IsPresent
        $baseline = Join-Path $work "base.scan"
        Write-Host ("scanning {0} seed(s) with the default build (this is the slow part)..." -f $Seeds)
        & $defaultExe -scan 0 $Seeds | Set-Content -Encoding ascii $baseline
        if ($LASTEXITCODE -ne 0) { throw "the baseline scan failed" }

        # Keep witnesses that still work. Re-searching from scratch every time
        # would churn the file for no reason and make its history unreadable.
        $existing = @{}
        if (Test-Path $matrixPath) {
            foreach ($line in Get-Content $matrixPath) {
                if ($line.StartsWith("#") -or $line.Trim() -eq "") { continue }
                $f = $line -split "`t"
                if ($f.Count -ge 4 -and $f[1] -ne "-") { $existing[$f[0]] = $f }
            }
        }

        $rows = @()
        foreach ($t in $toggles) {
            $exe = Join-Path $work "$t.exe"
            if (-not (Build-Variant $t $exe)) {
                Write-Host ("  BUILDFAIL {0}" -f $t) -ForegroundColor Red
                $rows += , @($t, "-", "-", "-", "buildfail", "-")
                continue
            }
            $idRun = Invoke-Nofix $exe @("-id")
            if (-not $idRun.ok -or $idRun.text -ne $t) {
                Write-Host ("  IDMISMATCH {0} reports '{1}' ({2})" -f $t, $idRun.text, (Format-Exit $idRun.rc)) -ForegroundColor Red
                $rows += , @($t, "-", "-", "-", "idmismatch", "-")
                continue
            }

            if ($existing.ContainsKey($t)) {
                $old = $existing[$t]
                $onRun  = Invoke-Nofix $defaultExe @("-one", $old[1])
                $offRun = Invoke-Nofix $exe @("-one", $old[1])
                $a = $onRun.fields
                $b = $offRun.fields
                if ($onRun.ok -and $offRun.ok -and $a.Count -ge 3 -and $a[0] -ne $b[0]) {
                    Write-Host ("  kept  {0} (seed {1})" -f $t, $old[1])
                    $rows += , @($t, $old[1], $a[0], $b[0], $a[1], $a[2])
                    continue
                }
                Write-Host ("  stale {0}: seed {1} no longer distinguishes; re-searching" -f $t, $old[1]) -ForegroundColor Yellow
            }

            $hit = & $exe -diff $baseline 0 $Seeds 1
            if ($LASTEXITCODE -eq 0 -and $hit) {
                $f = $hit -split "`t"
                Write-Host ("  FOUND {0} at seed {1}" -f $t, $f[0]) -ForegroundColor Green
                $rows += , @($t, $f[0], $f[1], $f[2], $f[3], $f[4])
            } else {
                Write-Host ("  none  {0}" -f $t)
                $rows += , @($t, "-", "-", "-", "-", "-")
            }
        }

        $sb = New-Object System.Text.StringBuilder
        # 🔴 KEEP THE EXISTING HEADER, VERBATIM. It is not boilerplate: it is
        # 160 lines recording why each blank row is blank -- the fixtures that
        # were attempted and withdrawn, with the tick counts measured -- plus the
        # #!EXPECT ratchet line. This writer used to emit a fresh six-line header,
        # so the next -Search would have silently deleted all of it.
        $header = @()
        if (Test-Path $matrixPath) {
            foreach ($line in [IO.File]::ReadAllLines($matrixPath)) {
                if (-not $line.StartsWith("#") -and $line.Trim() -ne "") { break }
                $header += $line
            }
        }
        if ($header.Count -eq 0) {
            $header = @(
                "# The NO_FIX_* differential matrix. Rebuild with test\run-nofix.ps1 -Search.",
                "# A WITNESS is one generated input whose result differs between a build with",
                "# the fix on and one with it off -- proof the fix is live and reachable.",
                "# A row of '-' means the search found no such input. That is a statement about",
                "# the SEARCH, not about the fix, and is never grounds for deleting one.",
                "#toggle`tseed`tdigest_fix_on`tdigest_fix_off`toutcome`tticks"
            )
        }
        foreach ($line in $header) { [void]$sb.AppendLine($line) }
        foreach ($r in $rows) { [void]$sb.AppendLine(($r -join "`t")) }
        [IO.File]::WriteAllText($matrixPath, ($sb.ToString() -replace "`r`n", "`n"),
                                (New-Object System.Text.UTF8Encoding($false)))
        $found = ($rows | Where-Object { $_[1] -ne "-" }).Count
        Write-Host ""
        Write-Host ("wrote {0}: {1} of {2} toggle(s) have a witness" -f $matrixPath, $found, $toggles.Count)
        Write-Host "Read the diff before committing it." -ForegroundColor Yellow
        exit 0
    }

    # ---- check mode ----
    if (-not (Test-Path $matrixPath)) { throw "no matrix at $matrixPath" }
    $pass = 0; $fail = 0; $blank = 0; $checked = 0; $onCrashed = 0
    $blankNames = New-Object Collections.ArrayList
    foreach ($line in Get-Content $matrixPath) {
        if ($line.StartsWith("#") -or $line.Trim() -eq "") { continue }
        $f = $line -split "`t"
        if ($f.Count -lt 6) { continue }
        $t = $f[0]
        if ($f[1] -eq "-") { $blank++; [void]$blankNames.Add($f[0]); continue }
        $checked++

        $exe = Join-Path $work "$t.exe"
        if (-not (Build-Variant $t $exe)) {
            Write-Host ("  FAIL  {0}: the fix-off build does not compile" -f $t) -ForegroundColor Red
            $fail++; continue
        }
        $idRun = Invoke-Nofix $exe @("-id")
        if (-not $idRun.ok) {
            Write-Host ("  FAIL  {0}: the fix-off binary crashed before reporting its identity -- {1}" -f $t, (Format-Exit $idRun.rc)) -ForegroundColor Red
            $fail++; continue
        }
        if ($idRun.text -ne $t) {
            Write-Host ("  FAIL  {0}: the fix-off binary reports itself as '{1}'" -f $t, $idRun.text) -ForegroundColor Red
            $fail++; continue
        }
        $onRun  = Invoke-Nofix $defaultExe @("-one", $f[1])
        $offRun = Invoke-Nofix $exe @("-one", $f[1])
        if (-not $onRun.ok) {
            Write-Host ("  FAIL  {0}: the fix-ON (default, shipped) engine CRASHED on seed {1} -- {2}" -f $t, $f[1], (Format-Exit $onRun.rc)) -ForegroundColor Red
            if ($onCrashed -eq 0) {
                Write-Host "        No toggle is involved in that build. The engine itself is broken:" -ForegroundColor Red
                Write-Host "        look at what changed in mslogic.c, encoding.c or random.c, not at the matrix." -ForegroundColor Red
            }
            $onCrashed++; $fail++; continue
        }
        if (-not $offRun.ok) {
            Write-Host ("  FAIL  {0}: the fix-OFF build crashed on seed {1} -- {2} -- while the fix-on build did not" -f $t, $f[1], (Format-Exit $offRun.rc)) -ForegroundColor Red
            $fail++; continue
        }
        $gotOn  = $onRun.fields[0]
        $gotOff = $offRun.fields[0]

        if ($gotOn -eq $gotOff) {
            Write-Host ("  FAIL  {0}: seed {1} no longer tells the two builds apart" -f $t, $f[1]) -ForegroundColor Red
            Write-Host ("        both now digest to {0}" -f $gotOn) -ForegroundColor Red
            $fail++
        } elseif ($gotOn -ne $f[2] -or $gotOff -ne $f[3]) {
            Write-Host ("  FAIL  {0}: still distinguished, but the digests moved" -f $t) -ForegroundColor Red
            Write-Host ("          fix on : {0} -> {1}" -f $f[2], $gotOn) -ForegroundColor Red
            Write-Host ("          fix off: {0} -> {1}" -f $f[3], $gotOff) -ForegroundColor Red
            $fail++
        } else {
            Write-Host ("  ok    {0} (seed {1}, {2}/{3}t)" -f $t, $f[1], $f[4], $f[5])
            $pass++
        }
    }

    Write-Host ""
    Write-Host ("NO_FIX_* differential matrix: {0} passed, {1} failed, of {2} witness(es)" -f $pass, $fail, $checked)
    Write-Host ("{0} toggle(s) have no witness and were not checked -- see test\nofix\nofix.c" -f $blank)
    if ($onCrashed -gt 0) {
        Write-Host ("THE SHIPPED ENGINE CRASHED on {0} of {1} witness seed(s). Every failure above with" -f $onCrashed, $checked) -ForegroundColor Red
        Write-Host "'fix-ON ... CRASHED' is that one engine defect, not that many toggle regressions." -ForegroundColor Red
    }
    if ($checked -eq 0) { throw "no witnesses in the matrix at all -- refusing to report success" }

    # 🔴 THE SECOND ORACLE: a witness-less toggle may still be guarded by a NAMED
    # UNIT CASE, and this is what stops that guard rotting away unnoticed.
    #
    # A blind audit disabled FIX_CHIP_PICKUP_ON_BLOCK -- a shipped engine fix --
    # and every one of the six layers stayed green, golden's 1,806 digests
    # included. Only run-corpus.ps1 caught it, over a private collection on one
    # machine that no CI job runs. Fourteen of the thirty-two toggles were in that
    # state.
    #
    # The fuzz search cannot close them: nofix.c derives a PROFILE from each seed
    # and builds one interaction east of Chip, so a fix whose situation no profile
    # constructs is unreachable at ANY seed budget. Hand-built fixtures in
    # mslogic_test.c close them instead -- but a fixture only guards a fix while
    # it still fails with the fix disabled, and nothing was checking that.
    #
    # So: for every toggle with no witness, build the unit test with -D<toggle>
    # and require it to FAIL. A toggle that is neither witnessed nor caught here
    # is reported as unguarded, by name, every run.
    $unitSrc = Join-Path $PSScriptRoot "mslogic_test.c"
    $stub = Join-Path $PSScriptRoot "stub"
    $unitGuarded = 0
    $unitOpen = @()
    Write-Host ""
    Write-Host "--- witness-less toggles: is a unit case guarding them? ---"

    # 🔴 THE CONTROL. "Fails with the toggle defined" only means "guarded" if the
    # same test PASSES with nothing defined. Without this, a mslogic_test.c that
    # is red for any unrelated reason -- a broken engine, a bad fixture, a
    # compile error -- makes EVERY witness-less toggle read as guarded, and the
    # ratchet below would count that as the matrix getting stronger.
    $controlExe = Join-Path ([IO.Path]::GetTempPath()) "nofixunit-control.exe"
    $controlLog = Join-Path $work "unit-control.log"
    $prev = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    & $Cc -std=gnu11 -w -I $stub -x c -o $controlExe $unitSrc 2>&1 | Out-File -FilePath $controlLog -Encoding ascii
    $controlBuilt = ($LASTEXITCODE -eq 0)
    $controlRc = -1
    if ($controlBuilt) {
        & $controlExe 2>&1 | Out-File -FilePath $controlLog -Encoding ascii
        $controlRc = $LASTEXITCODE
    }
    $ErrorActionPreference = $prev
    Remove-Item -LiteralPath $controlExe -Force -ErrorAction SilentlyContinue
    $controlOk = ($controlBuilt -and $controlRc -eq 0)
    if (-not $controlOk) {
        Write-Host ("  FAIL  mslogic_test.c does not pass with NO toggle defined ({0}), so a failure" -f $(if ($controlBuilt) { Format-Exit $controlRc } else { "did not compile" })) -ForegroundColor Red
        Write-Host "        under a toggle proves nothing. Not measuring the unit guards; see $controlLog" -ForegroundColor Red
        $fail++
        $blankNames = New-Object Collections.ArrayList
    }

    foreach ($t in $blankNames) {
        $exe = Join-Path ([IO.Path]::GetTempPath()) ("nofixunit-" + $t + ".exe")
        $unitLog = Join-Path $work "unit.log"
        # ⚠ SAME TRAP AS Build-Variant, AND I WALKED INTO IT. A disabled fix makes
        # the unit binary print an engine assertion to stderr, and on PowerShell
        # 5.1 under ErrorActionPreference = "Stop" that becomes a TERMINATING
        # error even though a nonzero exit is exactly the result being measured.
        # The file's own header says this; lowering the preference across the
        # native calls is the only thing that works.
        $prev = $ErrorActionPreference
        $ErrorActionPreference = "Continue"
        & $Cc -std=gnu11 -w -I $stub "-D$t" -x c -o $exe $unitSrc 2>&1 | Out-File -FilePath $unitLog -Encoding ascii
        $built = ($LASTEXITCODE -eq 0)
        $rc = 0
        if ($built) {
            & $exe 2>&1 | Out-File -FilePath $unitLog -Encoding ascii
            $rc = $LASTEXITCODE
        }
        $ErrorActionPreference = $prev
        if (-not $built) { $unitOpen += "$t (did not compile)"; continue }
        Remove-Item -LiteralPath $exe -Force -ErrorAction SilentlyContinue
        if ($rc -ne 0) { Write-Host ("  ok    {0} is caught by a unit case" -f $t); $unitGuarded++ }
        else { $unitOpen += $t }
    }
    Write-Host ("  {0} of {1} witness-less toggles are guarded by a named unit case" -f $unitGuarded, $blankNames.Count)
    if ($unitOpen.Count -gt 0) {
        Write-Host "  UNGUARDED -- breaking these is invisible to every layer:" -ForegroundColor Yellow
        foreach ($t in $unitOpen) { Write-Host ("    {0}" -f $t) -ForegroundColor Yellow }
        Write-Host "  See test\nofix\nofix-matrix.tsv for which of these need harness"
        Write-Host "  capability rather than a fixture."
    }

    # 🔴 THE RATCHET. Everything above reports on the rows the matrix HAPPENS to
    # contain, so deleting a row weakened the suite without failing it -- an
    # audit blanked NO_FIX_TANK_ON_CLONER's witness and this script exited 0,
    # having quietly moved the toggle into the "UNGUARDED" list it prints in
    # yellow and then ignores.
    #
    # The matrix now declares its own floor on the line beginning `#!EXPECT`, and
    # that is what makes a deletion visible: the counts stop matching and the
    # only way to make them match again is to edit the declaration, in a diff,
    # on purpose. Same shape as the unit suite's check floors.
    $expectFile = Join-Path $PSScriptRoot "nofix\nofix-matrix.tsv"
    $expectLine = @(Get-Content -LiteralPath $expectFile |
                    Where-Object { $_ -like '#!EXPECT*' }) | Select-Object -First 1
    if (-not $expectLine) {
        Write-Host ""
        Write-Host "nofix-matrix.tsv declares no #!EXPECT line -- refusing to report a pass" -ForegroundColor Red
        Write-Host "  Without it a deleted row silently weakens this layer. Add:" -ForegroundColor Yellow
        Write-Host ("  #!EXPECT`twitnesses={0}`tguarded={1}" -f $checked, $unitGuarded) -ForegroundColor Yellow
        exit 1
    }
    # ⚠ FAIL CLOSED ON A MALFORMED LINE. Defaulting an unparsed number to 0 would
    # make every count "at or above" it -- a ratchet that a typo disarms.
    if ($expectLine -notmatch 'witnesses=(\d+)') {
        Write-Host "the #!EXPECT line has no witnesses=N -- refusing to report a pass: $expectLine" -ForegroundColor Red
        exit 1
    }
    $wantWitnesses = [int]$Matches[1]
    if ($expectLine -notmatch 'guarded=(\d+)') {
        Write-Host "the #!EXPECT line has no guarded=N -- refusing to report a pass: $expectLine" -ForegroundColor Red
        exit 1
    }
    $wantGuarded = [int]$Matches[1]

    # 🔴 EXACT, NOT A FLOOR -- the unit suite's rule, for the unit suite's reason:
    # slack in a floor is room for a later deletion to hide in. Gain a witness
    # without raising the line, lose a different one next month, and a floor
    # reads the pair as no change. So growth fails too, with a different message.
    $problems = @()
    if (-not $controlOk) {
        $problems += "unit-guarded: NOT MEASURABLE -- mslogic_test.c fails with no toggle defined (see above)"
    } elseif ($unitGuarded -ne $wantGuarded) {
        $problems += ("unit-guarded: {0} declared, {1} present" -f $wantGuarded, $unitGuarded)
    }
    if ($checked -ne $wantWitnesses) {
        $problems += ("witnesses: {0} declared, {1} present" -f $wantWitnesses, $checked)
    }
    if ($problems.Count) {
        Write-Host ""
        Write-Host "########## THE NO_FIX MATRIX DOES NOT MATCH ITS #!EXPECT LINE ##########" -ForegroundColor Red
        foreach ($p in $problems) { Write-Host ("  " + $p) -ForegroundColor Red }
        if ($controlOk -and ($checked -lt $wantWitnesses -or $unitGuarded -lt $wantGuarded)) {
            Write-Host "  FEWER than declared: a blanked row or a rotted guard is not a passing run." -ForegroundColor Yellow
            Write-Host "  Restore it, or lower the line deliberately and say why in the commit." -ForegroundColor Yellow
        }
        if ($checked -gt $wantWitnesses -or ($controlOk -and $unitGuarded -gt $wantGuarded)) {
            Write-Host "  MORE than declared: good news -- raise the #!EXPECT line to match, in the" -ForegroundColor Yellow
            Write-Host "  same commit, so the new guard cannot quietly be lost again." -ForegroundColor Yellow
        }
        $fail++
    } else {
        Write-Host ("  ratchet: {0} witness(es) and {1} unit guard(s), exactly as declared" -f
                    $checked, $unitGuarded) -ForegroundColor Green
    }

    if ($fail -gt 0) { exit 1 }
    exit 0
}
finally {
    Pop-Location
}
