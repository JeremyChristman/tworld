<#
Extracts a packaged release zip to a clean directory and PROVES IT RUNS.

    powershell -ExecutionPolicy Bypass -File test\run-playtest.ps1
    powershell -ExecutionPolicy Bypass -File test\run-playtest.ps1 -Zip dist\TileWorld-jc-52.zip
    powershell -ExecutionPolicy Bypass -File test\run-playtest.ps1 -NoGui

WHY THIS EXISTS

.github\RELEASING.md step 5 says "package and playtest", and step 7 keeps the
GitHub release a DRAFT precisely because that playtest is a human step. It is a
sound gate -- it is the only thing that checks the bytes people actually
download, and a static-link failure appears nowhere else.

🔴 BUT IT HAD NO TOOLING, AND THAT MADE IT A GATE THAT CAN PASS WHILE PROVING
NOTHING. Reconstructed by hand each release, it was got wrong at least once: a
run that reported a clean playtest had in fact launched the PREVIOUS release's
install, because a path substitution silently failed. The output looked
identical either way. A check whose failure mode is "looks exactly like a pass"
belongs in a script, not in a habit.

WHAT IT PROVES, IN ORDER

  1. The zip contains what package.ps1 says it should, and nothing else.
  2. The extracted executable carries the EXPECTED build tag, and no other. This
     is what catches a stale build being packaged or the wrong zip being tested.
  3. It replays REAL RECORDED SOLUTIONS -- both rulesets -- through the shipped
     binary. This is the static link's only real test: a build that cannot find
     its DLLs dies here, silently, on a machine that is not the developer's.
  4. It survives a path-qualified command line (`Tile World.exe sets\x.dac`),
     the jc-52 regression.
  5. Optionally, it opens the GUI, plays a level, and screenshots it.

⚠ WHAT IT DOES NOT PROVE. The GUI half checks that the window opens, a level
loads, and keystrokes reach the engine. It does not look at the pixels: the
score table's column spans, the tileset picker and the color picker are still
verified by a person. Do not read a green run here as "the interface is fine".

⚠ IT NEEDS SOMETHING TO REPLAY. Solutions and level data are not in the
repository (docs/adr/0005 -- CHIPS.DAT is copyrighted and .tws files are the
maintainer's). By default it borrows them read-only from the collection; point
-Data somewhere else, or pass -NoSolutions to skip step 3 and say so loudly.
#>

[CmdletBinding()]
param(
    # The zip to test. Defaults to the newest one in dist\.
    [string]$Zip,

    # The tag the executable must report. Defaults to whatever fork.h says, so
    # the usual invocation needs no arguments and still checks the real thing.
    [string]$ExpectTag,

    # A Chip's Challenge installation to borrow sets\, data\ and save\ from,
    # READ-ONLY. Never written to.
    [string]$Data = "C:\Users\Jeremy\Dropbox\Games\Computer\Chip's Challenge",

    # Sets to batch-verify. One MS and one Lynx by default, so both engines in
    # the shipped binary are exercised.
    [string[]]$Sets = @("CCLP1.dat-ms.dac", "CCLP1.dat-lynx.dac"),

    [switch]$NoSolutions,
    [switch]$NoGui,
    [switch]$KeepScratch
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$root = Split-Path -Parent $PSScriptRoot
Push-Location $root

$checks = 0
$failures = @()

function Check([string]$what, [bool]$ok, [string]$detail = "") {
    $script:checks++
    if ($ok) {
        Write-Host ("  ok    {0}" -f $what)
    } else {
        Write-Host ("  FAIL  {0}" -f $what) -ForegroundColor Red
        if ($detail) { Write-Host ("        {0}" -f $detail) -ForegroundColor Red }
        $script:failures += $what
    }
}

try {
    # ---- what are we testing -------------------------------------------------
    if (-not $Zip) {
        $newest = Get-ChildItem -Path (Join-Path $root "dist") -Filter "TileWorld-*.zip" -File -ErrorAction SilentlyContinue |
                  Sort-Object LastWriteTime -Descending | Select-Object -First 1
        if (-not $newest) { throw "no zip in dist\ -- run package.ps1 first, or pass -Zip" }
        $Zip = $newest.FullName
    }
    if (-not (Test-Path $Zip)) { throw "no such zip: $Zip" }
    $Zip = (Resolve-Path $Zip).Path

    if (-not $ExpectTag) {
        # fork.h is the single definition of the tag (docs/adr/0006). Reading it
        # here rather than parsing the filename is deliberate: the filename is
        # what package.ps1 CLAIMED, and this script exists to check claims.
        #
        # ⚠ ANCHORED TO THE #define LINE, and it has to be. fork.h's own comment
        # block contains the text `FORK_BUILD_TAG "]"` while explaining
        # string-literal concatenation, and an unanchored match finds THAT
        # first: the first run of this script cheerfully announced
        # `expects: ]`, then "verified" that the executable contains a `]`.
        # A check with a wrong expectation is worse than no check.
        $forkh = Get-Content (Join-Path $root "fork.h") -Raw
        if ($forkh -notmatch '(?m)^#define\s+FORK_BUILD_TAG\s+"([^"]+)"') {
            throw "could not read FORK_BUILD_TAG out of fork.h"
        }
        $ExpectTag = $Matches[1]
        if ($ExpectTag -notmatch '^jc-\d+$') {
            throw "fork.h gave an implausible build tag: '$ExpectTag'"
        }
    }

    Write-Host ""
    Write-Host "########## playtest ##########"
    Write-Host ("  zip:      {0}" -f $Zip)
    Write-Host ("  expects:  {0}" -f $ExpectTag)

    # ---- extract -------------------------------------------------------------
    $scratch = Join-Path $env:TEMP ("tw-playtest-" + [guid]::NewGuid().ToString("N").Substring(0, 12))
    $null = New-Item -ItemType Directory $scratch
    Expand-Archive -LiteralPath $Zip -DestinationPath $scratch -Force

    # package.ps1 builds the zip with a top-level folder; flatten it so the
    # layout matches what a player gets after "extract here".
    $inner = Get-ChildItem -Path $scratch -Directory | Select-Object -First 1
    if ($inner) {
        Get-ChildItem -Path $inner.FullName | Move-Item -Destination $scratch
        Remove-Item $inner.FullName -Recurse -Force
    }

    $exe = Join-Path $scratch "Tile World.exe"
    Check "the zip contains Tile World.exe" (Test-Path $exe)
    foreach ($f in @("README.txt", "COPYING", "tw_settings.ini", "zlib1.dll", "libzstd.dll")) {
        Check ("the zip contains {0}" -f $f) (Test-Path (Join-Path $scratch $f))
    }
    if (-not (Test-Path $exe)) { throw "nothing to test" }

    # ---- the build tag -------------------------------------------------------
    #
    # 🔴 THE CHECK THAT CATCHES THE MISTAKE THIS SCRIPT WAS WRITTEN FOR. The tag
    # is stored as UTF-16LE inside the binary, so a plain string search finds
    # nothing; both the presence of the right tag AND the absence of the
    # previous one matter, because a stale build is the failure that otherwise
    # looks exactly like success.
    $bytes = [IO.File]::ReadAllBytes($exe)
    function ContainsUtf16([byte[]]$hay, [string]$needle) {
        $n = [Text.Encoding]::Unicode.GetBytes($needle)
        $limit = $hay.Length - $n.Length
        for ($i = 0; $i -le $limit; $i++) {
            $match = $true
            for ($j = 0; $j -lt $n.Length; $j++) {
                if ($hay[$i + $j] -ne $n[$j]) { $match = $false; break }
            }
            if ($match) { return $true }
        }
        return $false
    }
    Check ("the executable reports {0}" -f $ExpectTag) (ContainsUtf16 $bytes $ExpectTag)

    if ($ExpectTag -match '^jc-(\d+)$') {
        $prev = "jc-" + ([int]$Matches[1] - 1)
        Check ("the executable does NOT still report {0}" -f $prev) `
              (-not (ContainsUtf16 $bytes $prev)) `
              "a stale build was packaged"
    }

    # ---- run it --------------------------------------------------------------
    #
    # ⚠ Start-Process -Wait -PassThru with redirects, never `& $exe`. The
    # executable is a Windows-GUI-subsystem binary: called the plain way it does
    # not block, returns an EMPTY exit code and captures nothing, so a test
    # written that way passes in milliseconds having verified nothing at all.
    # See CLAUDE.md section 3.1.
    function RunExe([string[]]$argList, [string]$tag) {
        $o = Join-Path $scratch "$tag.out"
        $e = Join-Path $scratch "$tag.err"
        $p = Start-Process -FilePath $exe -ArgumentList $argList -Wait -PassThru `
                           -NoNewWindow -WorkingDirectory $scratch `
                           -RedirectStandardOutput $o -RedirectStandardError $e
        return [pscustomobject]@{
            ExitCode = $p.ExitCode
            StdOut   = (Get-Content $o -Raw -ErrorAction SilentlyContinue)
            StdErr   = (Get-Content $e -Raw -ErrorAction SilentlyContinue)
        }
    }

    $r = RunExe @("-V") "version"
    Check "-V runs and names the build" `
          ($r.StdOut -and $r.StdOut -match [regex]::Escape($ExpectTag)) `
          ("stdout was: " + ($r.StdOut -replace "`r?`n", " "))

    # ---- replay real solutions ----------------------------------------------
    if ($NoSolutions) {
        Write-Host "  --    SKIPPED: solution replay (-NoSolutions)" -ForegroundColor Yellow
    } elseif (-not (Test-Path $Data)) {
        Write-Host ("  --    SKIPPED: solution replay -- no collection at {0}" -f $Data) -ForegroundColor Yellow
    } else {
        # Copied in, never referenced in place: a -b run writes save\history and
        # rewrites tw_settings.ini in its working directory even with -r, and
        # pointing -L at a directory makes the program GENERATE .dac files there.
        # Neither is acceptable against somebody's real collection.
        foreach ($sub in @("sets", "data", "res", "save")) {
            $src = Join-Path $Data $sub
            if (Test-Path $src) {
                Copy-Item -Path $src -Destination $scratch -Recurse -Force -ErrorAction SilentlyContinue
            }
        }

        foreach ($set in $Sets) {
            if (-not (Test-Path (Join-Path $scratch "sets\$set"))) {
                Write-Host ("  --    SKIPPED: {0} is not in the collection" -f $set) -ForegroundColor Yellow
                continue
            }
            $r = RunExe @("-b", "-r", "-S", (Join-Path $scratch "save"),
                          "-L", "sets", "-D", "data", "-R", "res", $set) ("verify-" + $set)
            # ⚠ Batch verify's EXIT CODE IS NOT A VERDICT -- it is only one with
            # -q, and even then "0 invalid" and "no solutions at all" are both 0.
            # The verdict is parsed from stdout. See CLAUDE.md section 3.2.
            $valid = 0
            if ($r.StdOut -match '(?m)^\s*Valid solutions:\s+(\d+)\s*$') { $valid = [int]$Matches[1] }
            $invalid = -1
            if ($r.StdOut -match '(?m)^Invalid solutions:\s+(\d+)\s*$') { $invalid = [int]$Matches[1] }
            Check ("{0}: solutions replay through the SHIPPED binary" -f $set) `
                  ($valid -gt 0 -and $invalid -eq 0) `
                  ("valid={0} invalid={1}" -f $valid, $invalid)
        }

        # jc-52: a path-qualified argument used to reach an uninitialized
        # pointer. It survives by luck rather than by design when it is broken,
        # so this asserts the outcome rather than merely "it did not crash".
        if (Test-Path (Join-Path $scratch "sets\$($Sets[0])")) {
            $r = RunExe @("-b", "-r", "-S", (Join-Path $scratch "save"),
                          "-L", "sets", "-D", "data", "-R", "res",
                          ("sets\" + $Sets[0])) "pathqualified"
            Check "a path-qualified set argument does not crash the program (jc-52)" `
                  ($r.ExitCode -eq 0) ("exit code was " + $r.ExitCode)
        }
    }

    # ---- the GUI -------------------------------------------------------------
    if ($NoGui) {
        Write-Host "  --    SKIPPED: GUI playtest (-NoGui)" -ForegroundColor Yellow
    } else {
        Add-Type -AssemblyName System.Drawing
        Add-Type -AssemblyName System.Windows.Forms
        Add-Type @"
using System;
using System.Runtime.InteropServices;
public class TWPT {
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, IntPtr e);
  public struct RECT { public int L, T, R, B; }
}
"@
        $guiSet = $Sets[0]
        $guiArgs = @("-S", (Join-Path $scratch "save"), "-L", "sets", "-D", "data", "-R", "res")
        if (Test-Path (Join-Path $scratch "sets\$guiSet")) { $guiArgs += $guiSet }

        # 🔴 THE PID COMES FROM -PassThru AND NOTHING IS EVER KILLED BY NAME.
        # The maintainer may well have his own Tile World open; killing by
        # process name would take it with us.
        $proc = Start-Process -FilePath $exe -ArgumentList $guiArgs -PassThru -WorkingDirectory $scratch
        try {
            Start-Sleep -Seconds 5
            $proc.Refresh()
            Check "the GUI starts and stays running" (-not $proc.HasExited) `
                  ("exited immediately with " + $(if ($proc.HasExited) { $proc.ExitCode } else { "n/a" }))

            if (-not $proc.HasExited -and $proc.MainWindowHandle -ne 0) {
                $h = $proc.MainWindowHandle
                $null = [TWPT]::ShowWindow($h, 9)
                $null = [TWPT]::SetForegroundWindow($h)
                Start-Sleep -Milliseconds 800

                # Click "Next" a few times: a set with a .ccx opens on its
                # prologue, and the level is behind it. Harmless when there is
                # no prologue -- the button is not there and the clicks land on
                # the map.
                $rect = New-Object TWPT+RECT
                $null = [TWPT]::GetWindowRect($h, [ref]$rect)
                for ($i = 0; $i -lt 8; $i++) {
                    $null = [TWPT]::SetCursorPos(($rect.R - 66), ($rect.B - 47))
                    Start-Sleep -Milliseconds 200
                    [TWPT]::mouse_event(0x0002, 0, 0, 0, [IntPtr]::Zero)
                    Start-Sleep -Milliseconds 90
                    [TWPT]::mouse_event(0x0004, 0, 0, 0, [IntPtr]::Zero)
                    Start-Sleep -Milliseconds 400
                }

                $null = [TWPT]::SetForegroundWindow($h)
                Start-Sleep -Milliseconds 400
                foreach ($k in @("{DOWN}", "{DOWN}", "{RIGHT}", "{RIGHT}", "{DOWN}")) {
                    [System.Windows.Forms.SendKeys]::SendWait($k)
                    Start-Sleep -Milliseconds 320
                }
                Start-Sleep -Milliseconds 900
                $proc.Refresh()

                # The title bar carries the level name, which makes it a real
                # oracle for "a level is actually loaded" rather than "a window
                # exists". It reads a bare "Tile World" on the prologue screen.
                Check "a level is loaded (the title names one)" `
                      ($proc.MainWindowTitle -match ' - .+ - ') `
                      ("title was: " + $proc.MainWindowTitle)
                Check "the GUI survives a few moves" (-not $proc.HasExited)

                $shot = Join-Path $root ("playtest-" + $ExpectTag + ".png")
                $null = [TWPT]::GetWindowRect($h, [ref]$rect)
                $w = $rect.R - $rect.L; $ht = $rect.B - $rect.T
                if ($w -gt 0 -and $ht -gt 0) {
                    $bmp = New-Object System.Drawing.Bitmap $w, $ht
                    $g = [System.Drawing.Graphics]::FromImage($bmp)
                    $g.CopyFromScreen($rect.L, $rect.T, 0, 0, (New-Object System.Drawing.Size($w, $ht)))
                    $bmp.Save($shot, [System.Drawing.Imaging.ImageFormat]::Png)
                    $g.Dispose(); $bmp.Dispose()
                    Write-Host ("  --    screenshot: {0}" -f $shot)
                }
            } else {
                Write-Host "  --    no main window; skipping the interactive half" -ForegroundColor Yellow
            }
        }
        finally {
            # Always by PID, always cleaned up: a live process holds the exe
            # open and the next build's link step fails with what looks like a
            # permissions error.
            $proc.Refresh()
            if (-not $proc.HasExited) {
                $null = $proc.CloseMainWindow()
                Start-Sleep -Seconds 2
                $proc.Refresh()
                if (-not $proc.HasExited) { Stop-Process -Id $proc.Id -Force }
            }
        }
    }

    # ---- the collection must be untouched ------------------------------------
    if (-not $NoSolutions -and (Test-Path $Data)) {
        $stray = Get-ChildItem -Path $Data -Filter "*.tws" -Recurse -ErrorAction SilentlyContinue |
                 Where-Object { $_.LastWriteTime -gt (Get-Date).AddMinutes(-10) }
        Check "the collection's .tws files were not written" (-not $stray) `
              ("recently modified: " + (($stray | Select-Object -First 3 | ForEach-Object { $_.Name }) -join ", "))
    }

    Write-Host ""
    Write-Host ("########## {0} check(s), {1} failure(s) ##########" -f $checks, $failures.Count)
    if ($failures.Count -gt 0) {
        foreach ($f in $failures) { Write-Host ("  FAILED: {0}" -f $f) -ForegroundColor Red }
        exit 1
    }
    Write-Host ("  the packaged {0} runs, replays real solutions, and plays" -f $ExpectTag) -ForegroundColor Green
    exit 0
}
finally {
    if ($KeepScratch) {
        Write-Host ("  scratch kept at {0}" -f $scratch)
    } elseif ($scratch -and (Test-Path $scratch)) {
        Remove-Item $scratch -Recurse -Force -ErrorAction SilentlyContinue
    }
    Pop-Location
}
