<#
Assembles the release zip that gets attached to the GitHub release:

    dist\TileWorld-<tag>.zip
        Tile World.exe      the build, stripped
        tw_settings.ini     a STOCK settings file
        zlib1.dll           \  the only two dynamic imports the static Qt build
        libzstd.dll         /  keeps; the game will not start without them
        README.txt
        COPYING

The exe must already be built (see FORK.md -- MSYS2 + static Qt). Point -Exe at
it; the default is the usual build directory.

    powershell -ExecutionPolicy Bypass -File package.ps1
    powershell -ExecutionPolicy Bypass -File package.ps1 -Exe build-jc34\tworld2.exe

The tag comes from FORK_BUILD_TAG in fork.h -- the same #define the program itself
compiles in -- so the zip cannot be named for a build it does not contain, and
packaging FAILS if README.txt's header does not name that same tag.
#>
param(
    [string]$Exe = "build-jc34\tworld2.exe",
    [string]$Dlls = "C:\msys64\mingw64\bin",
    [string]$Strip = "C:\msys64\mingw64\bin\strip.exe"
)
$ErrorActionPreference = "Stop"
$root = $PSScriptRoot

# The build tag is the single source of truth for the release name. Read from fork.h as of jc-34:
# that header is what help.c and TWApp.cpp both compile in, so this script and the binary cannot
# disagree about which build this is. (Before jc-34 it was parsed out of the C++ literal in
# oshw-qt\TWApp.cpp, which stopped being the definition when the tag moved.)
$forkSrc = Get-Content (Join-Path $root "fork.h") -Raw
if ($forkSrc -notmatch '(?m)^\s*#\s*define\s+FORK_BUILD_TAG\s+"(?<tag>[^"]+)"') {
    throw "Could not read FORK_BUILD_TAG out of fork.h"
}
$tag = $Matches['tag']

$exePath = if ([IO.Path]::IsPathRooted($Exe)) { $Exe } else { Join-Path $root $Exe }
if (-not (Test-Path $exePath)) { throw "no executable at $exePath -- build it first (see FORK.md)" }

# Wipe the whole dist folder: a package run that fails partway must not leave the PREVIOUS zip
# sitting next to a half-built staging folder under the same name, looking current.
$dist = Join-Path $root "dist"
if (Test-Path $dist) { Remove-Item -LiteralPath $dist -Recurse -Force }
$pkgDir = Join-Path $dist "TileWorld-$tag"
New-Item -ItemType Directory -Force -Path $pkgDir | Out-Null

# The shipped name has a space and no "tworld" in it -- that filename has caused false conclusions
# before, so it is set here rather than left to whatever the build produced.
if (Test-Path $Strip) {
    & $Strip $exePath -o (Join-Path $pkgDir "Tile World.exe")
    if ($LASTEXITCODE -ne 0) { throw "strip failed (exit $LASTEXITCODE)" }
} else {
    Write-Warning "strip not found at $Strip - shipping the unstripped exe"
    Copy-Item $exePath (Join-Path $pkgDir "Tile World.exe")
}

# Qt's static cmake config pins these two as dynamic imports; SDL2 and the Windows platform
# plugin are baked into the exe. Without them the game does not start at all.
foreach ($dll in "zlib1.dll", "libzstd.dll") {
    $src = Join-Path $Dlls $dll
    if (-not (Test-Path $src)) { throw "missing $dll in $Dlls -- the release needs it" }
    Copy-Item $src (Join-Path $pkgDir $dll)
}

# A stock settings file. Key order and section names match settings.cpp's SECTIONS.
#
# ⚠ EVERY VALUE HERE MUST BE THE VALUE THE CODE ACTUALLY DEFAULTS TO WHEN THE KEY IS ABSENT,
# because shipping this file makes those values explicit for every fresh install. Checked:
#   bgcolor         the stock blue TWTheme falls back to
#   displayccx=1    absent -> getintsetting returns -1 -> setChecked(-1) is ON
#   forceshowtimer  absent -> "-1 > 0" is false -> off
#   showinitstate   absent -> same, off
#   selectedruleset absent -> not Ruleset_Lynx(1) -> MS(2)
#   volume=10       sdlsfx.c starts at SDL_MIX_MAXVOLUME, i.e. level 10. It was written as 8 here
#                   at first, which would have quietly turned every fresh install's sound DOWN.
# Re-check this list whenever a default changes.
@"
[Display]
bgcolor=#285080
displayccx=1
forceshowtimer=0
legacyscores=false
showbuildtag=false
showinitstate=0

[Game]
selectedruleset=2
selectedseries=

[Sound]
volume=10
"@ -replace "`r`n", "`n" -replace "`n", "`r`n" |
    ForEach-Object { [IO.File]::WriteAllText((Join-Path $pkgDir "tw_settings.ini"), $_, (New-Object Text.UTF8Encoding $false)) }

# README goes in with CRLF, because it is read in Notepad. Idempotent (LF -> LF -> CRLF).
$readme = [IO.File]::ReadAllText((Join-Path $root "README.txt"))
$readme = $readme -replace "`r`n", "`n" -replace "`n", "`r`n"
[IO.File]::WriteAllText((Join-Path $pkgDir "README.txt"), $readme, (New-Object Text.UTF8Encoding $false))

# GPL binary distribution: the license text travels WITH the binary.
Copy-Item (Join-Path $root "COPYING") (Join-Path $pkgDir "COPYING") -Force

# The README must describe the build it ships with, or the download lies about itself. Anchored to
# the header line: a loose "appears anywhere" test would be satisfied by a revision-history entry.
$readmeHead = (Get-Content (Join-Path $pkgDir "README.txt") -TotalCount 5) -join "`n"
if ($readmeHead -notmatch ("(?m)^.*build\s+" + [regex]::Escape($tag) + "\s*$")) {
    throw "README.txt's header does not name build $tag -- update it before packaging"
}

# The exe must actually BE the build we are naming the zip after.
$exeBytes = [IO.File]::ReadAllBytes((Join-Path $pkgDir "Tile World.exe"))
$needle = [Text.Encoding]::Unicode.GetBytes("[$tag]")      # Qt string literals are UTF-16LE
$found = $false
for ($i = 0; $i -le $exeBytes.Length - $needle.Length -and -not $found; $i++) {
    $match = $true
    for ($j = 0; $j -lt $needle.Length; $j++) { if ($exeBytes[$i+$j] -ne $needle[$j]) { $match = $false; break } }
    if ($match) { $found = $true }
}
if (-not $found) { throw "the executable does not contain the build tag [$tag] -- it is not the build being packaged" }

# Entry names are built BY HAND with forward slashes. On PowerShell 5.1 / .NET Framework both
# Compress-Archive and ZipFile::CreateFromDirectory emit BACKSLASH entry names, which Info-ZIP on
# Linux/macOS does not treat as separators -- it produces files with literal backslashes in their
# names. This is a public download, so it has to open correctly off Windows too.
$zip = Join-Path $dist "TileWorld-$tag.zip"
Add-Type -AssemblyName System.IO.Compression.FileSystem
Add-Type -AssemblyName System.IO.Compression     # ZipArchiveMode lives in this one, not the above
$archive = [IO.Compression.ZipFile]::Open($zip, [IO.Compression.ZipArchiveMode]::Create)
try {
    foreach ($f in (Get-ChildItem -LiteralPath $pkgDir -File | Sort-Object Name)) {
        [IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
            $archive, $f.FullName, "TileWorld-$tag/$($f.Name)",
            [IO.Compression.CompressionLevel]::Optimal) | Out-Null
    }
} finally { $archive.Dispose() }

# Verify the ARCHIVE, not the folder it was built from -- the zip is what people download.
$expected = @{}
foreach ($f in (Get-ChildItem -LiteralPath $pkgDir -File)) {
    $expected["TileWorld-$tag/$($f.Name)"] = (Get-FileHash -LiteralPath $f.FullName -Algorithm SHA256).Hash
}
$check = [IO.Compression.ZipFile]::OpenRead($zip)
try {
    $bad = @($check.Entries | Where-Object { $_.FullName -match '\\' })
    if ($bad.Count -gt 0) { throw "$($bad.Count) zip entry name(s) contain a backslash -- would not extract correctly off Windows" }
    $names = @($check.Entries | ForEach-Object { $_.FullName } | Sort-Object)
    $want = @($expected.Keys | Sort-Object)
    if (Compare-Object $names $want) { throw "zip contents do not match the staged folder.`n  in zip: $($names -join ', ')`n  wanted: $($want -join ', ')" }
    foreach ($e in $check.Entries) {
        $s = $e.Open(); $ms = New-Object IO.MemoryStream; $s.CopyTo($ms); $s.Close()
        $sha = [Security.Cryptography.SHA256]::Create()
        $got = [BitConverter]::ToString($sha.ComputeHash($ms.ToArray())).Replace('-','')
        $ms.Dispose()
        if ($got -ne $expected[$e.FullName]) { throw "zip entry $($e.FullName) does not match the file it was built from" }
    }
} finally { $check.Dispose() }

Write-Host ("Packaged {0} ({1:N0} bytes)" -f $zip, (Get-Item $zip).Length)
Get-ChildItem $pkgDir | ForEach-Object { Write-Host ("    {0,-20} {1,12:N0} bytes" -f $_.Name, $_.Length) }
