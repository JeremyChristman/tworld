<#
The MSYS2 ENVIRONMENT this repository builds in, read from docs\toolchain.lock (its `msys_env`
line), so that every script agrees on it and changing environments is one line there.

    . (Join-Path <repo root> "msys-env.ps1")

then use:

    $MsysEnv.Name      ucrt64
    $MsysEnv.Prefix    mingw-w64-ucrt-x86_64         (the package-name prefix)
    $MsysEnv.Root      C:\msys64
    $MsysEnv.Dir       C:\msys64\ucrt64
    $MsysEnv.Bin       C:\msys64\ucrt64\bin
    $MsysEnv.RepoUrl   https://repo.msys2.org/mingw/ucrt64

A script with a -MsysRoot parameter (build.ps1, test\run-qt-tests.ps1) dot-sources this AFTER its
param block, and the root follows it. Without one, the root is C:\msys64.

WHY THIS FILE EXISTS (2026-11-21, jc-62). MSYS2 is retiring its MINGW64 environment: on that day it
removed the static Qt 5 the release links, six minutes before a release needed it. Moving to UCRT64
found the old environment's path written out by hand in thirteen scripts and two workflows. A
script missed in such a move does not fail -- it keeps finding the old compiler, if the old tree is
still installed, and builds or tests with it while everything else uses the new one. One
definition, read by everything, is what makes the next move a one-line change.

It fails LOUDLY: a missing lock, a missing line, or an environment it does not know all throw,
because a guessed default is exactly the silent mismatch this exists to prevent.
#>

$msysEnvLockFile = Join-Path $PSScriptRoot "docs\toolchain.lock"
if (-not (Test-Path -LiteralPath $msysEnvLockFile)) { throw "msys-env.ps1: no toolchain lock at $msysEnvLockFile" }
$msysEnvName = $null
foreach ($msysEnvLine in [IO.File]::ReadAllLines($msysEnvLockFile)) {
    # Lower-cased: MSYS2's names are, and the mirror's folders are case-sensitive (jc-62 review:
    # "UCRT64" passed every check here and then made a RepoUrl that 404s).
    if ($msysEnvLine.Trim() -match '^msys_env\s*=\s*(\S+)$') { $msysEnvName = $Matches[1].ToLowerInvariant() }
}
if (-not $msysEnvName) { throw "msys-env.ps1: $msysEnvLockFile has no msys_env line" }

# MSYS2's own environment names and the package prefix each one uses.
$msysEnvPrefixes = @{
    'ucrt64'  = 'mingw-w64-ucrt-x86_64'
    'mingw64' = 'mingw-w64-x86_64'
    'clang64' = 'mingw-w64-clang-x86_64'
}
if (-not $msysEnvPrefixes.ContainsKey($msysEnvName)) {
    throw ("msys-env.ps1: msys_env=$msysEnvName in $msysEnvLockFile is not an environment this knows " +
           "(one of: " + (($msysEnvPrefixes.Keys | Sort-Object) -join ", ") + ")")
}

# -Scope 0: the dot-sourcing script's OWN -MsysRoot only. Without it Get-Variable searches every
# enclosing scope, so a $MsysRoot left at the prompt would quietly move a script that has no such
# parameter (jc-62 review).
$msysEnvRoot = Get-Variable -Name MsysRoot -Scope 0 -ValueOnly -ErrorAction SilentlyContinue
if (-not $msysEnvRoot) { $msysEnvRoot = 'C:\msys64' }

$MsysEnv = [pscustomobject]@{
    Name    = $msysEnvName
    Prefix  = $msysEnvPrefixes[$msysEnvName]
    Root    = $msysEnvRoot
    Dir     = Join-Path $msysEnvRoot $msysEnvName
    Bin     = Join-Path (Join-Path $msysEnvRoot $msysEnvName) 'bin'
    RepoUrl = "https://repo.msys2.org/mingw/$msysEnvName"
}

<#
The two rules every script needs, kept HERE so there is one copy of each (jc-62 review: the
compiler finder had been pasted into three scripts, and a PATH prepend into six).

Set-PathFirst puts a directory (the environment's bin by default) FIRST on PATH, removing any copy
of it further down first. "Prepend unless already present" left it behind another environment's
bin when both were on PATH, so the gcc driver, or a dynamic build's Qt, loaded the OTHER
environment's DLLs. The comparison is exact, segment by segment (a path containing [ or ] is a
pattern to -like).

Resolve-MsysTool finds a compiler or tool: an explicit path if given; else the environment's own;
else, ONLY when there is no MSYS2 at the root at all, whatever is on PATH. With MSYS2 present but
this environment's tool missing it THROWS -- a PC with only MINGW64 installed, falling back to
MINGW64's gcc on PATH, would otherwise test green on a compiler the lock does not pin.
#>
function Set-PathFirst([string]$Dir = $MsysEnv.Bin) {
    $want = $Dir.TrimEnd('\')
    $rest = @($env:Path -split ';' | Where-Object { $_ -and ($_.TrimEnd('\') -ne $want) })
    $env:Path = (@($Dir) + $rest) -join ';'
}

function Resolve-MsysTool([string]$explicit, [string]$name) {
    if ($explicit) {
        if (-not (Test-Path -LiteralPath $explicit)) { throw "not found: $explicit" }
        return $explicit
    }
    $inEnv = Join-Path $MsysEnv.Bin "$name.exe"
    if (Test-Path -LiteralPath $inEnv) { return $inEnv }
    if (Test-Path -LiteralPath $MsysEnv.Root) {
        throw ("MSYS2 is at $($MsysEnv.Root), but its $($MsysEnv.Name) environment (docs\toolchain.lock) " +
               "has no $name at $inEnv. Install it with: powershell -ExecutionPolicy Bypass -File " +
               ".github\install-toolchain.ps1 -Extra cmake,ninja,SDL2,pkgconf,qt5-base -- refusing to " +
               "use a $name from PATH, which could be another environment's.")
    }
    $onPath = Get-Command $name -ErrorAction SilentlyContinue
    if ($onPath) { return $onPath.Source }
    throw "no $name found: install MSYS2's $($MsysEnv.Name) at $($MsysEnv.Dir), put $name on PATH, or pass it explicitly"
}
