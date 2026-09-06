<#
Installs the MSYS2 build toolchain on a CI runner, with the COMPILER PINNED to
the exact version recorded in docs\toolchain.lock.

    .github\install-toolchain.ps1
    .github\install-toolchain.ps1 -Extra mingw-w64-x86_64-cmake,mingw-w64-x86_64-qt5

WHY THE COMPILER IS PINNED AND THE REST IS NOT

This program's purpose is byte-exact replay of recorded solutions, and the
compiler is an input to that. Everything else installed here -- cmake, ninja,
Qt, SDL2, pkgconf -- either drives the build or links into the GUI, and none of
them decides what the engine computes. Pinning the one that does is the whole
value; pinning twenty packages would multiply the ways an upstream mirror can
break the build for no additional protection.

⚠ MSYS2 HAS NO NATIVE PINNING. There is no `pacman -S gcc=16.2.0`. Pinning means
fetching that exact package file from repo.msys2.org, which works because the
repository keeps older builds. If the file is ever removed this step FAILS,
loudly, rather than falling back to whatever is current -- a silent fallback
would undo the pin and leave a green run that means nothing, which is the
failure mode this repository treats as the serious kind.

See docs\toolchain.lock, which also explains what to run before bumping it.
#>

[CmdletBinding()]
param(
    [string[]]$Extra = @()
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$pacman = 'C:\msys64\usr\bin\pacman.exe'
if (-not (Test-Path $pacman)) { throw "no MSYS2 at C:\msys64 on this runner" }

$repoRoot = Split-Path -Parent $PSScriptRoot
$lockPath = Join-Path $repoRoot "docs\toolchain.lock"
if (-not (Test-Path $lockPath)) { throw "no toolchain lock at $lockPath" }

# Parse the lock. Comments and blank lines only; nothing clever, so that the
# file stays something a human edits confidently.
$lock = @{}
foreach ($line in (Get-Content $lockPath)) {
    $t = $line.Trim()
    if ($t -eq "" -or $t.StartsWith("#")) { continue }
    $kv = $t -split "=", 2
    if ($kv.Count -eq 2) { $lock[$kv[0].Trim()] = $kv[1].Trim() }
}
foreach ($key in @("gcc_pkgver", "gcc_version")) {
    if (-not $lock.ContainsKey($key)) { throw "$lockPath has no $key" }
}

$pkgver  = $lock["gcc_pkgver"]
$wantver = $lock["gcc_version"]
$pkgfile = "mingw-w64-x86_64-gcc-$pkgver-any.pkg.tar.zst"
$url     = "https://repo.msys2.org/mingw/mingw64/$pkgfile"

Write-Host "pinned compiler: $pkgfile"

# -Sy, not -Syu: refreshing the package database is enough to install from, and
# a full system upgrade on a throwaway runner costs minutes and can leave pacman
# asking for a restart.
& $pacman -Sy --noconfirm
if ($LASTEXITCODE -ne 0) { throw "pacman -Sy failed (exit $LASTEXITCODE)" }

# The pinned compiler, by URL. -U takes a package file; pacman fetches it and
# resolves its dependencies from the refreshed database.
& $pacman -U --noconfirm $url
if ($LASTEXITCODE -ne 0) {
    throw @"
could not install the pinned compiler $pkgfile.

If the package has been removed from repo.msys2.org, pick a current version
from https://repo.msys2.org/mingw/mingw64/ and update docs\toolchain.lock --
but read that file first: bumping the compiler on this project is a deliberate
act with a checklist (golden master, NO_FIX_* matrix, and the solution corpus),
not a version bump to wave through.
"@
}

if ($Extra.Count -gt 0) {
    Write-Host ("also installing: {0}" -f ($Extra -join ", "))
    & $pacman -S --needed --noconfirm @Extra
    if ($LASTEXITCODE -ne 0) { throw "pacman failed installing the extra packages (exit $LASTEXITCODE)" }
}

# 🔴 VERIFY, DO NOT ASSUME. `pacman -U` can be a no-op if a NEWER gcc is already
# present and satisfies a dependency, and an -S of another package can pull the
# compiler forward underneath us. Without this check the pin would be decorative
# and nothing would ever say so.
#
# Note the ordering: this runs AFTER the extra packages, precisely so that a
# dependency dragging gcc forward is caught rather than missed.
$gcc = 'C:\msys64\mingw64\bin\gcc.exe'
if (-not (Test-Path $gcc)) { throw "no gcc at $gcc after installing the toolchain" }

$got = (& $gcc -dumpversion).Trim()
& $gcc --version | Select-Object -First 1 | Write-Host

if ($got -ne $wantver) {
    throw @"
TOOLCHAIN DRIFT: gcc reports $got, docs\toolchain.lock pins $wantver.

Something pulled the compiler forward -- most likely a dependency of one of the
extra packages. The build was NOT run, on purpose: a compiler this project did
not choose is exactly what the lock exists to notice.

Either pin the extra package that dragged it, or update docs\toolchain.lock
after running the checks that file lists.
"@
}

Write-Host "toolchain verified: gcc $got matches docs\toolchain.lock"
