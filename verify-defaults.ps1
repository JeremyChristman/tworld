<#
Checks that the STOCK tw_settings.ini shipped in the release describes the
program that ships with it.

    powershell -ExecutionPolicy Bypass -File verify-defaults.ps1
    powershell -ExecutionPolicy Bypass -File verify-defaults.ps1 -Quiet

WHY THIS EXISTS

package.ps1 writes a hand-authored stock settings file into the release zip, under
a comment that says:

    "Key order and section names match settings.cpp's SECTIONS."
    "EVERY VALUE HERE MUST BE THE VALUE THE CODE ACTUALLY DEFAULTS TO..."
    "Re-check this list whenever a default changes."

That is a manual gate, and it had already failed by the time this script was
written: jc-41 added `lynxtileset` and `mstileset` to settings.cpp's [Display]
row and the shipped file was never updated, so its own comment was untrue.

The consequence is not a crash -- an absent key falls back to the default -- but
the file's whole PURPOSE is to be the complete, editable documentation of every
setting, the thing a downloader opens instead of hunting through a README. A
settings file that silently omits settings is not doing the job it ships to do.

WHAT IS AND IS NOT CHECKED

Checked: the SET OF KEYS in the stock file against settings.cpp's SECTIONS[],
which section each key sits under, the headroom left in SECTION_MAXKEYS, and --
added in jc-55 -- the whole stock file against the C string literal in
settings_test.c's "comes back BYTE FOR BYTE" case, character for character.

That third copy exists because a test that read the file from package.ps1 would
assert only that the round trip reproduces whatever it is handed, which is true
of any input at all. The literal is what makes the assertion mean something. It
promptly drifted the first time it could: jc-55 added two keys, and every case
in settings_test.c stayed green while its literal described the PREVIOUS
release's file. So it is compared here rather than trusted.

Not checked: whether each VALUE equals the program's real fallback. Those
fallbacks live in a dozen different call sites -- getintsetting() returning -1
and being read as "on", SettingOptedIn(), sdlsfx.c starting at
SDL_MIX_MAXVOLUME -- and there is no single place to read them from. The comment
in package.ps1 records the reasoning for each value one by one; that stays a
human responsibility, and this script does not pretend otherwise.

Exit code is 0 only when the key sets agree.
#>
param(
    [switch]$Quiet
)
$ErrorActionPreference = "Continue"
$root = $PSScriptRoot
$problems = @()
$literalDrift = $false

function Say([string]$text) { if (-not $Quiet) { Write-Host $text } }

# --------------------------------------------- what the code declares --------

$settingsSrc = Get-Content (Join-Path $root "settings.cpp") -Raw

if ($settingsSrc -notmatch 'int\s+const\s+SECTION_MAXKEYS\s*=\s*(\d+)\s*;') {
    Write-Host "ERROR: could not find SECTION_MAXKEYS in settings.cpp" -ForegroundColor Red
    exit 1
}
$maxKeys = [int]$Matches[1]

# The SECTIONS[] initializer, one row per section:
#   { "Display", { "bgcolor", "deathcount", ..., nullptr } },
# Comments are stripped first, so that a key mentioned only in a /* ... */ note
# cannot be counted as declared -- this table is heavily commented, and the
# tileset keys in particular are named in the comment right above the row.
$noBlockComments = [regex]::Replace($settingsSrc, '/\*.*?\*/', '', 'Singleline')
$noComments = [regex]::Replace($noBlockComments, '//[^\r\n]*', '')

if ($noComments -notmatch '(?s)SectionSpec\s+const\s+SECTIONS\[\]\s*=\s*\{(.*?)\n\s*\};') {
    Write-Host "ERROR: could not find the SECTIONS[] table in settings.cpp" -ForegroundColor Red
    exit 1
}
$sectionsBody = $Matches[1]

$declared = [ordered]@{}
foreach ($m in [regex]::Matches($sectionsBody, '\{\s*"(?<name>[^"]+)"\s*,\s*\{(?<keys>[^}]*)\}')) {
    $name = $m.Groups['name'].Value
    $keys = @([regex]::Matches($m.Groups['keys'].Value, '"([^"]+)"') | ForEach-Object { $_.Groups[1].Value })
    $declared[$name] = $keys
}
if ($declared.Count -eq 0) {
    Write-Host "ERROR: parsed the SECTIONS[] table but found no sections" -ForegroundColor Red
    exit 1
}

Say "settings.cpp declares:"
foreach ($name in $declared.Keys) {
    # +1 for the nullptr terminator, which is inside the array and is why a row
    # that fills every slot loses its terminator and walks into the next
    # SectionSpec. See the comment above SECTION_MAXKEYS.
    $used = $declared[$name].Count + 1
    Say ("  [{0,-8}] {1,2} key(s), {2} of {3} slots used" -f $name, $declared[$name].Count, $used, $maxKeys)
    if ($used -gt $maxKeys) {
        $problems += "[$name] uses $used of $maxKeys slots in SECTION_MAXKEYS -- the nullptr terminator no longer fits, and savesettings() will walk into the next section"
    } elseif ($used -eq $maxKeys) {
        $problems += "[$name] fills SECTION_MAXKEYS exactly ($used of $maxKeys). There is no room for the terminator plus another key: raise SECTION_MAXKEYS before adding one."
    }
}

# --------------------------------------------- what the package ships --------

$packageSrc = Get-Content (Join-Path $root "package.ps1") -Raw
if ($packageSrc -notmatch '(?s)@"\r?\n(?<ini>\[Display\].*?)\r?\n"@') {
    Write-Host "ERROR: could not find the stock tw_settings.ini here-string in package.ps1." -ForegroundColor Red
    Write-Host "       If it was reformatted, update the regular expression in this script." -ForegroundColor Red
    exit 1
}
$iniText = $Matches['ini']

$shipped = [ordered]@{}
$currentSection = $null
foreach ($line in ($iniText -split "`r?`n")) {
    $line = $line.Trim()
    if (-not $line) { continue }
    if ($line -match '^\[(.+)\]$') {
        $currentSection = $Matches[1]
        if (-not $shipped.Contains($currentSection)) { $shipped[$currentSection] = @() }
        continue
    }
    if ($line -match '^([^=]+)=') {
        if (-not $currentSection) {
            $problems += "the stock file has a key before its first section heading: $line"
            continue
        }
        $shipped[$currentSection] += $Matches[1].Trim()
    }
}

Say ""
Say "package.ps1 ships:"
foreach ($name in $shipped.Keys) {
    Say ("  [{0,-8}] {1,2} key(s)" -f $name, $shipped[$name].Count)
}

# --------------------------------------------------------- the comparison ---

Say ""
foreach ($name in $declared.Keys) {
    if (-not $shipped.Contains($name)) {
        $problems += "the stock file has no [$name] section, but settings.cpp declares one"
        continue
    }
    $missing = @($declared[$name] | Where-Object { $shipped[$name] -notcontains $_ })
    foreach ($key in $missing) {
        $problems += "[$name] $key is declared in settings.cpp but MISSING from the stock tw_settings.ini in package.ps1"
    }
}

foreach ($name in $shipped.Keys) {
    if (-not $declared.Contains($name)) {
        # Not fatal on its own: [Other] is where unrecognized keys are parked.
        # But shipping one is still wrong, since nothing would read it back into
        # a known setting.
        $problems += "the stock file has a [$name] section that settings.cpp does not declare"
        continue
    }
    $extra = @($shipped[$name] | Where-Object { $declared[$name] -notcontains $_ })
    foreach ($key in $extra) {
        $problems += "[$name] $key is shipped in the stock tw_settings.ini but is NOT declared in settings.cpp -- it would be parked under [Other] on the first save"
    }
}

# ------------------------------------------- and the test's own copy of it ---
#
# 🔴 THERE IS A THIRD COPY OF THE STOCK FILE, and until jc-55 nothing checked it.
# settings_test.c's "the shipped stock file comes back BYTE FOR BYTE" case holds
# the whole file as a C string literal, because that is what makes the assertion
# a real one -- a test that read package.ps1 would only be asserting that the
# round trip reproduces whatever it is handed, which it does for any input.
#
# The cost is a duplicate, and it drifted the first time it could: jc-55 added
# two keys to package.ps1 and settings.cpp, and every case in settings_test.c
# still passed while its literal described the previous release's file. So the
# duplicate stays -- and is compared here, character for character. ADR 0006
# makes fork.h the single definition of the build tag and CI enforces it; where
# a second copy genuinely earns its place, a check is what earns it.
$testPath = Join-Path $root "test\settings_test.c"
if (-not (Test-Path $testPath)) {
    $problems += "test\settings_test.c is missing -- the stock file's byte-for-byte case cannot be checked"
} else {
    $testSrc = [System.IO.File]::ReadAllText($testPath, [System.Text.Encoding]::UTF8)
    if ($testSrc -notmatch '(?s)BYTE FOR BYTE.*?loadtext\(plat\((?<lit>.*?)\)\);') {
        $problems += "could not find the stock-file literal in test\settings_test.c. If that case was renamed or reshaped, update the regular expression in this script rather than deleting the check."
    } else {
        # A C string literal split across lines: join the quoted runs, then turn
        # the only escape it uses back into a real newline.
        $literal = ""
        foreach ($m in [regex]::Matches($Matches['lit'], '"((?:[^"\\]|\\.)*)"')) {
            $literal += $m.Groups[1].Value
        }
        $literal = $literal -replace '\\n', "`n"

        # Both sides to LF, both without a trailing newline. The shipped file
        # deliberately ends without one -- the here-string stops after volume=10
        # -- and the test's own next case is about the writer restoring it.
        $normShipped = ($iniText -replace "`r`n", "`n").TrimEnd("`n")
        $normTest = ($literal -replace "`r`n", "`n").TrimEnd("`n")

        if ($normShipped -ne $normTest) {
            $literalDrift = $true
            $problems += "settings_test.c's stock-file literal does not match the file package.ps1 ships"

            # Report the KEYS that differ, not the lines. A single added line
            # shifts every line after it, so a positional diff reports twenty
            # mismatches for a one-key change and buries the actual one.
            $shippedKeys = @($normShipped -split "`n" | Where-Object { $_ -match '^\s*[^\[\s][^=]*=' })
            $testKeys = @($normTest -split "`n" | Where-Object { $_ -match '^\s*[^\[\s][^=]*=' })
            foreach ($line in @($shippedKeys | Where-Object { $testKeys -notcontains $_ })) {
                $problems += "  package.ps1 ships `"$line`"; settings_test.c's literal does not have it"
            }
            foreach ($line in @($testKeys | Where-Object { $shippedKeys -notcontains $_ })) {
                $problems += "  settings_test.c's literal has `"$line`"; package.ps1 does not ship it"
            }
            # ⚠ Parenthesized deliberately. Written as `$a -join "x" -eq $b -join "x"`
            # PowerShell binds -eq tighter than the second -join and the test is
            # always true, which is exactly what the first version of this line did.
            $shippedSorted = (($shippedKeys | Sort-Object) -join "`n")
            $testSorted = (($testKeys | Sort-Object) -join "`n")
            if ($shippedSorted -eq $testSorted) {
                $problems += "  the same keys and values, so the difference is in ORDER, blank lines or section headings"
            }
        } else {
            Say ("settings_test.c's byte-for-byte literal matches, {0} line(s)" -f ($normTest -split "`n").Count)
        }
    }
}

# Section ORDER, checked separately and reported as a note rather than a failure:
# the parser does not care (headings are decoration -- docs\adr\0007), but the
# package.ps1 comment claims the order matches, and a claim that stops being true
# is worth knowing about.
$declaredOrder = @($declared.Keys)
$shippedOrder = @($shipped.Keys | Where-Object { $declared.Contains($_) })
if (($declaredOrder -join ',') -ne ($shippedOrder -join ',')) {
    Say ("NOTE: section order differs (code: {0}; shipped: {1}). Harmless -- headings are decoration -- but package.ps1's comment claims they match." -f
        ($declaredOrder -join ', '), ($shippedOrder -join ', '))
}

if ($problems.Count -gt 0) {
    Write-Host ""
    Write-Host "the shipped stock tw_settings.ini does not match settings.cpp:" -ForegroundColor Red
    foreach ($p in $problems) { Write-Host "  - $p" -ForegroundColor Red }
    Write-Host ""
    Write-Host "Fix the here-string in package.ps1 (add the key with the value the code actually" -ForegroundColor Yellow
    Write-Host "defaults to when it is absent), and document it in README.txt section 6." -ForegroundColor Yellow
    if ($literalDrift) {
        Write-Host ""
        Write-Host "And update the string literal in settings_test.c's `"comes back BYTE FOR BYTE`"" -ForegroundColor Yellow
        Write-Host "case to match, along with the settings.size() counts in that case and the next." -ForegroundColor Yellow
        Write-Host "That copy is deliberate -- it is what makes the assertion mean something -- so" -ForegroundColor Yellow
        Write-Host "bring it into line rather than deleting the check that found this." -ForegroundColor Yellow
    }
    exit 1
}

Write-Host ""
Write-Host "the stock tw_settings.ini declares every setting the code does" -ForegroundColor Green
exit 0
