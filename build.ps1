<#
Builds Tile World.

    powershell -ExecutionPolicy Bypass -File build.ps1                  # -> build-static\tworld2.exe (what ships)
    powershell -ExecutionPolicy Bypass -File build.ps1 -Flavor dynamic  # -> build-dynamic\tworld2.exe (fast, needs Qt DLLs)
    powershell -ExecutionPolicy Bypass -File build.ps1 -Trace           # -> build-trace\tworld2.exe   (-DTRACE_DESYNC)
    powershell -ExecutionPolicy Bypass -File build.ps1 -Clean
    powershell -ExecutionPolicy Bypass -File build.ps1 -ExpectTag jc-44
    powershell -ExecutionPolicy Bypass -File build.ps1 -Manifest dist\build-manifest.json

This exists because the build used to be four shell lines in FORK.md that had to be typed into an
MSYS2 shell by hand, two of which are not obvious and one of which fails silently if omitted. An
agent -- or a person six months from now -- should not have to reconstruct them.

FLAVORS

  static    Release, Qt5 static (CMAKE_PREFIX_PATH=/mingw64/qt5-static). ONE self-contained exe;
            this is what gets packaged. Needs mingw-w64-x86_64-qt5-static, which a stock MSYS2
            install does not have. See docs\adr\0001-one-statically-linked-executable.md.
  dynamic   Release against the ordinary shared Qt5. Builds much faster and is the right choice for
            "does it still compile"; the exe needs Qt's DLLs on PATH to actually run.
  sdl       The SDL front end (OSHW=sdl). Not built or shipped by this fork and it requires SDL 1.2,
            which MSYS2 does not package -- offered so that a change touching generic\ or oshw.h can
            at least be attempted against the other backend rather than assumed fine.

WHAT THIS SCRIPT DOES THAT YOU WOULD FORGET

  1. Puts MSYS2's mingw64\bin FIRST on PATH. The gcc driver cannot spawn cc1 unless its own
     directory is on PATH, and it fails with a nonzero exit and NOT ONE WORD of diagnostic when it
     cannot -- calling it by full path is not enough.
  2. Removes NoDefaultCurrentDirectoryInExePath from the environment. CMakeLists.txt generates a
     comptime.bat in the build directory and invokes it by bare name; with that variable set (it is
     set by default on modern Windows) the current directory is excluded from the search and the
     custom command fails to find its own script.
  3. Refuses to package a build with any NO_FIX_* macro defined -- see -NoFix below.
#>
param(
    [ValidateSet("static", "dynamic", "sdl")]
    [string]$Flavor = "static",

    # Compile the per-tick desync tracer in (-DTRACE_DESYNC). A no-op in normal builds; this turns
    # it on. Defaults the build directory to build-trace so a traced exe can never be mistaken for
    # a release one.
    [switch]$Trace,

    # Turn engine fixes OFF, by name, for a differential measurement:
    #     -NoFix ROW32_CLONER,CLICK_EARLY
    # Each becomes -DNO_FIX_<NAME>. See docs\adr\0002-engine-fixes-are-opt-out-macros.md. A build
    # made this way is a debugging artifact and is refused by -Manifest and by package.ps1.
    [string[]]$NoFix = @(),

    [string]$BuildDir,
    [string]$MsysRoot = "C:\msys64",
    [switch]$Clean,
    [switch]$Configure,   # configure only, do not compile

    # Fail unless fork.h's FORK_BUILD_TAG is exactly this. The release workflow passes the git tag,
    # which is the only thing that connects "the tag I pushed" to "the tag compiled into the exe".
    [string]$ExpectTag,

    # Write build provenance JSON: tag, flavor, compiler, exe hash and size. Nothing in the binary
    # itself records which toolchain produced it.
    [string]$Manifest,

    [switch]$Strip
)

# Native tools write notes to stderr, and under "Stop" PowerShell 5.1 turns those into terminating
# NativeCommandErrors even when the tool succeeded. Exit codes are checked explicitly instead.
$ErrorActionPreference = "Continue"
$root = $PSScriptRoot

function Fail([string]$message) {
    Write-Host "ERROR: $message" -ForegroundColor Red
    exit 1
}

# ---------------------------------------------------------------- toolchain --

$mingwBin = Join-Path $MsysRoot "mingw64\bin"
if (-not (Test-Path $mingwBin)) {
    Fail "no MSYS2 mingw64 at $mingwBin. Install MSYS2, or pass -MsysRoot. Packages needed: mingw-w64-x86_64-{gcc,cmake,ninja,SDL2}, plus qt5-static for the static flavor."
}

# Exact segment comparison, not -like: a directory containing [ or ] is a wildcard PATTERN to -like
# and would silently fail to match, skipping the prepend and reintroducing the silent gcc failure
# this exists to prevent. (Same reasoning as test\run-tests.ps1.)
$segments = $env:Path -split ';'
if ($segments -notcontains $mingwBin) { $env:Path = "$mingwBin;$env:Path" }

# See the header, item 2. Remove-Item on a variable that is not set throws under -ErrorAction Stop,
# so this is guarded rather than wrapped in a try.
if (Test-Path Env:\NoDefaultCurrentDirectoryInExePath) {
    Remove-Item Env:\NoDefaultCurrentDirectoryInExePath
}

$cmake = Join-Path $mingwBin "cmake.exe"
$ninja = Join-Path $mingwBin "ninja.exe"
foreach ($tool in @($cmake, $ninja)) {
    if (-not (Test-Path $tool)) { Fail "missing $tool -- pacman -S mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja" }
}

# --------------------------------------------------------------- build tag --

$forkHeader = Join-Path $root "fork.h"
$forkSrc = Get-Content $forkHeader -Raw
if ($forkSrc -notmatch '(?m)^\s*#\s*define\s+FORK_BUILD_TAG\s+"(?<tag>[^"]+)"') {
    Fail "could not read FORK_BUILD_TAG out of fork.h. If the #define was reformatted, update this regex AND the matching one in package.ps1."
}
$tag = $Matches['tag']

if ($ExpectTag) {
    if ($tag -ne $ExpectTag) {
        Fail "fork.h says FORK_BUILD_TAG is `"$tag`" but $ExpectTag was expected. Bump the #define in fork.h -- it is the ONLY place the tag is defined (docs\adr\0006)."
    }
    Write-Host "build tag: $tag (matches -ExpectTag)" -ForegroundColor Green
} else {
    Write-Host "build tag: $tag"
}

# ------------------------------------------------------------- build layout --

if (-not $BuildDir) {
    # A -NoFix build gets its OWN directory, the same way -Trace does. Belt to the
    # braces of always emitting CMAKE_C_FLAGS below: even if the cache were
    # mishandled again, a debugging build with engine fixes disabled cannot end up
    # sitting in build-static\ where package.ps1 would find it.
    if ($Trace)            { $BuildDir = "build-trace" }
    elseif ($NoFix.Count)  { $BuildDir = "build-nofix" }
    else                   { $BuildDir = "build-$Flavor" }
}
$buildPath = if ([IO.Path]::IsPathRooted($BuildDir)) { $BuildDir } else { Join-Path $root $BuildDir }

if ($Clean -and (Test-Path $buildPath)) {
    Write-Host "removing $buildPath"
    Remove-Item -LiteralPath $buildPath -Recurse -Force
}

# ------------------------------------------------------------ configuration --

$oshw = if ($Flavor -eq "sdl") { "sdl" } else { "qt" }
$exeName = if ($Flavor -eq "sdl") { "tworld.exe" } else { "tworld2.exe" }

$cflags = @()
if ($Trace) { $cflags += "-DTRACE_DESYNC" }
foreach ($name in $NoFix) {
    $clean = $name.Trim()
    if (-not $clean) { continue }
    # Accept both "ROW32_CLONER" and "NO_FIX_ROW32_CLONER" so neither spelling is a silent no-op --
    # a mistyped macro does not fail the build, it just quietly leaves the fix compiled in, which is
    # exactly the wrong failure mode for a differential measurement.
    #
    # ⚠ UPPERCASED FIRST, and matched with -cnotmatch. PowerShell's -match is
    # CASE-INSENSITIVE, so `-NoFix row32_cloner` sailed through a
    # '^NO_FIX_[A-Z0-9_]+$' check and produced -DNO_FIX_row32_cloner -- a
    # DIFFERENT C macro. The build succeeded, the fix stayed compiled in, and the
    # differential measurement quietly measured nothing.
    $clean = $clean.ToUpperInvariant()
    if ($clean -cnotmatch '^NO_FIX_') { $clean = "NO_FIX_$clean" }
    if ($clean -cnotmatch '^NO_FIX_[A-Z0-9_]+$') { Fail "not a valid fix macro name: $name" }
    # Shape is not enough: -NoFix ROW32_CLONR builds happily and disables nothing.
    # The macro has to actually exist in the source, or the measurement is a lie.
    $sources = Get-ChildItem -Path $root -Filter *.c -File
    $sources += Get-ChildItem -Path $root -Filter *.h -File
    if (-not (Select-String -Path $sources.FullName -Pattern "\b$clean\b" -Quiet)) {
        Fail "no such fix macro in the source: $clean. Check the spelling against the NO_FIX_* names in mslogic.c."
    }
    $cflags += "-D$clean"
}

$cmakeArgs = @(
    "-S", $root,
    "-B", $buildPath,
    "-G", "Ninja",
    "-DCMAKE_BUILD_TYPE=Release",
    "-DOSHW=$oshw"
)
if ($Flavor -eq "static") {
    $qtStatic = Join-Path $MsysRoot "mingw64\qt5-static"
    if (-not (Test-Path $qtStatic)) {
        Fail "no static Qt at $qtStatic -- pacman -S mingw-w64-x86_64-qt5-static, or build with -Flavor dynamic."
    }
    # CMake wants forward slashes here; a backslash path is treated as containing escapes.
    $cmakeArgs += "-DCMAKE_PREFIX_PATH=$($qtStatic -replace '\\','/')"
}
# 🔴 EMITTED UNCONDITIONALLY, EVEN WHEN EMPTY. CMake CACHES CMAKE_C_FLAGS, and
# omitting the argument on a later configure keeps the cached value rather than
# clearing it. So `build.ps1 -NoFix ROW32_CLONER` followed by a plain
# `build.ps1 -Manifest ...` in the same directory used to produce a RELEASE
# executable with an engine fix compiled out, a clean provenance manifest (the
# -NoFix guard below only sees the current invocation), and a passing -ExpectTag
# check. That is precisely the "a default nobody ships is a second, untested
# engine" failure ADR 0002 exists to prevent. Passing an empty string resets it.
$cmakeArgs += "-DCMAKE_C_FLAGS=$($cflags -join ' ')"
$cmakeArgs += "-DCMAKE_CXX_FLAGS=$($cflags -join ' ')"
# Written unconditionally: editors, clangd and static-analysis tools all read it, and it costs
# nothing. It lands in the build directory, which is gitignored.
$cmakeArgs += "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"

Write-Host ""
Write-Host "=== configure ($Flavor$(if ($Trace) { ', TRACE_DESYNC' })$(if ($NoFix.Count) { ", $($NoFix.Count) fix(es) disabled" })) ===" -ForegroundColor Cyan
& $cmake @cmakeArgs
if ($LASTEXITCODE -ne 0) { Fail "cmake configure failed (exit $LASTEXITCODE)" }

if ($Configure) {
    Write-Host "configured $buildPath (not built: -Configure)" -ForegroundColor Green
    exit 0
}

Write-Host ""
Write-Host "=== build ===" -ForegroundColor Cyan
& $cmake --build $buildPath
if ($LASTEXITCODE -ne 0) { Fail "build failed (exit $LASTEXITCODE)" }

$exePath = Join-Path $buildPath $exeName
if (-not (Test-Path $exePath)) { Fail "build reported success but produced no $exeName in $buildPath" }

# The tag is verified against the BINARY, not just against fork.h, because those are two independent
# facts: an incremental build that did not recompile TWApp.cpp would leave the old tag inside a
# freshly linked exe. Qt string literals are UTF-16LE, so that is what is searched for.
# Decoded as UTF-16 and searched as a string rather than with a nested byte loop.
# The hand-rolled O(n*m) scan was tens of millions of interpreted iterations per
# build against a statically linked Qt executable -- it worked, and it cost more
# than the compile of some files. Both alignments are checked because a Qt string
# literal is 2-byte aligned but nothing here guarantees the file offset is.
$bytes = [IO.File]::ReadAllBytes($exePath)
$needle = "[$tag]"
$found = [Text.Encoding]::Unicode.GetString($bytes).Contains($needle)
if (-not $found -and $bytes.Length -gt 1) {
    $found = [Text.Encoding]::Unicode.GetString($bytes, 1, $bytes.Length - 1).Contains($needle)
}
if (-not $found) {
    if ($Flavor -eq "sdl") {
        # The SDL front end has no TWApp.cpp, so the bracketed literal legitimately is not there.
        Write-Host "note: the SDL build carries no [$tag] literal; skipping the binary tag check"
    } else {
        Fail "the built exe does not contain the build tag [$tag]. Try -Clean: an incremental build can relink without recompiling the file that carries it."
    }
}

if ($Strip) {
    $stripExe = Join-Path $mingwBin "strip.exe"
    if (-not (Test-Path $stripExe)) { Fail "no strip.exe at $stripExe" }
    & $stripExe $exePath
    if ($LASTEXITCODE -ne 0) { Fail "strip failed (exit $LASTEXITCODE)" }
}

$exe = Get-Item $exePath
Write-Host ""
Write-Host ("built {0} ({1:N0} bytes)" -f $exe.FullName, $exe.Length) -ForegroundColor Green

# ---------------------------------------------------------------- manifest --

if ($Manifest) {
    if ($NoFix.Count -gt 0) {
        Fail "refusing to write a provenance manifest for a build with $($NoFix.Count) engine fix(es) disabled. That build is a debugging artifact and must never be packaged or released (docs\adr\0002)."
    }
    $gccVersion = (& (Join-Path $mingwBin "gcc.exe") --version | Select-Object -First 1)
    # Guarded with Get-Command rather than by reading $LASTEXITCODE afterwards: if
    # git is not installed at all, `& git` raises a non-terminating
    # CommandNotFoundException and $LASTEXITCODE still holds the 0 left by the
    # gcc call above -- so the manifest recorded an empty commit rather than
    # "unknown", which is a worse lie than saying nothing.
    $commit = "unknown"
    if (Get-Command git -ErrorAction SilentlyContinue) {
        $rev = (& git -C $root rev-parse HEAD 2>$null)
        if ($LASTEXITCODE -eq 0 -and $rev) { $commit = "$rev".Trim() }
    }

    $manifestPath = if ([IO.Path]::IsPathRooted($Manifest)) { $Manifest } else { Join-Path $root $Manifest }
    $manifestDir = Split-Path -Parent $manifestPath
    if ($manifestDir -and -not (Test-Path $manifestDir)) { New-Item -ItemType Directory -Force -Path $manifestDir | Out-Null }

    $record = [ordered]@{
        tag         = $tag
        flavor      = $Flavor
        trace       = [bool]$Trace
        commit      = $commit
        exe         = $exe.Name
        sizeBytes   = $exe.Length
        sha256      = (Get-FileHash -LiteralPath $exePath -Algorithm SHA256).Hash
        compiler    = "$gccVersion".Trim()
        cmake       = ((& $cmake --version | Select-Object -First 1) -replace '^cmake version ', '').Trim()
        builtUtc    = (Get-Date).ToUniversalTime().ToString("yyyy-MM-ddTHH:mm:ssZ")
        buildDir    = $BuildDir
    }
    # BOM-less. Set-Content -Encoding UTF8 on PowerShell 5.1 emits a byte-order
    # mark, and a BOM makes strict JSON parsers -- Python's json.load among them
    # -- reject the file outright. A provenance manifest that tooling cannot read
    # is not provenance.
    [IO.File]::WriteAllText($manifestPath, ($record | ConvertTo-Json),
                            (New-Object Text.UTF8Encoding $false))
    Write-Host "wrote $manifestPath"
}

exit 0
