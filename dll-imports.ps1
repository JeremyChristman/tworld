<#
dll-imports.ps1 -- which DLLs an executable needs beside it, READ FROM ITS IMPORT TABLE.

Dot-sourced by package.ps1 (what to put in the zip) and test\run-playtest.ps1 (whether the zip
holds it), so the packager and the check on the packager apply ONE rule and cannot drift apart --
CLAUDE.md section 8.1, "two hand-maintained tables that must agree will disagree".

WHY IT EXISTS (jc-61). package.ps1 used to copy two hard-coded names, zlib1.dll and libzstd.dll.
Between jc-59 and jc-60 MSYS2's zlib package renamed its DLL, CI's exe imported libz-1.dll, and the
jc-60 zip shipped zlib1.dll beside an exe that never loads it: 0xC0000135 on every launch, with
every archive check green. The names now come from the exe itself.

THE RULE, for each imported DLL, following each shipped DLL's own imports in turn:
  1. present in the SHIP folder (MSYS2's bin when packaging, the extracted zip when checking)
     -> it ships, and its own imports are followed;
  2. present in MSYS2's bin but NOT in the ship folder -> MISSING (it is an MSYS2 DLL, not Windows);
  3. an API-set name (api-ms-win-*, ext-ms-*), or a file in System32 -> provided by Windows;
  4. anything else -> MISSING.
Rule 2 comes before rule 3 on purpose: "in System32" alone would let a third-party copy that
happens to sit there (an old installer's zlib1.dll) pass as part of Windows, and the zip would
leave it out -- the jc-60 failure again, on a clean machine (jc-61 review).
#>

function Get-DllImports([string]$Objdump, [string]$File) {
    # Native stderr under Windows PowerShell 5.1 with ErrorActionPreference=Stop becomes a
    # terminating NativeCommandError -- one harmless objdump warning would kill the caller with an
    # opaque message before the exit code is even looked at. Run it under Continue and keep only
    # the lines that parse.
    $saved = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $out = & $Objdump -p $File 2>&1
        $code = $LASTEXITCODE
    } finally { $ErrorActionPreference = $saved }
    if ($code -ne 0) { throw "objdump -p failed on $File (exit $code)" }
    @($out | ForEach-Object { if ("$_" -match '^\s*DLL Name:\s*(\S+)\s*$') { $Matches[1] } })
}

# Returns @{ Ship = ordered name->path of every DLL found in $ShipDir; Missing = names that are
# neither there nor Windows' }. Throws only when the import table could not be read at all.
function Resolve-NeededDlls([string]$Objdump, [string]$Exe, [string]$ShipDir, [string]$MsysBin) {
    $imports = Get-DllImports $Objdump $Exe
    # Every Windows executable imports KERNEL32. None at all means the parse broke, and an empty
    # list would read as "needs no DLLs" -- refuse it rather than report success.
    if (-not ($imports | Where-Object { $_ -match '^kernel32\.dll$' })) {
        throw "objdump reported no KERNEL32.dll import for $Exe -- the import table could not be read"
    }
    $ship = [ordered]@{}
    $missing = New-Object System.Collections.ArrayList
    $seen = @{}
    $pending = New-Object System.Collections.Queue
    foreach ($d in $imports) { $pending.Enqueue($d) }
    $system32 = Join-Path $env:WINDIR "System32"
    while ($pending.Count -gt 0) {
        $dll = $pending.Dequeue()
        $key = $dll.ToLowerInvariant()
        if ($seen.ContainsKey($key)) { continue }
        $seen[$key] = $true
        $inShip = Join-Path $ShipDir $dll
        if (Test-Path -LiteralPath $inShip) {
            $ship[$dll] = $inShip
            foreach ($d in (Get-DllImports $Objdump $inShip)) { $pending.Enqueue($d) }
            continue
        }
        if ($MsysBin -and (Test-Path -LiteralPath (Join-Path $MsysBin $dll))) { [void]$missing.Add($dll); continue }
        if ($dll -match '^(api|ext)-ms-') { continue }
        if (Test-Path -LiteralPath (Join-Path $system32 $dll)) { continue }
        [void]$missing.Add($dll)
    }
    return @{ Ship = $ship; Missing = @($missing) }
}
