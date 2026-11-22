<#
corpus-folder.ps1 -- where the default solution corpus is, and what makes a folder one.

Dot-sourced by test\run-corpus.ps1, by test\run-playtest.ps1 (its fallback when
neither -Data nor TWORLD_PLAYTEST_DATA is given) and by .github\workflows\
corpus.yml's probe -- change it with all three in mind:

    . .\test\corpus-folder.ps1
    $p = Resolve-DefaultCorpus
    $problem = Get-CorpusProblem $p      # $null when $p is usable
    $full = ConvertTo-FullCorpusPath $p  # a relative path made absolute, safely

🔴 ONE COPY, IN-PROCESS, ON PURPOSE. The probe first read a literal default out
of run-corpus.ps1 with a regex; when the default became computed (2026-11-22),
it asked a child powershell.exe for it instead -- and Windows PowerShell 5.1
carries a child's stdout through the console code page, so a path with a
character outside it came back with "?" in it and the probe called a real
collection absent (review). A function in the caller's own process has nothing
to encode. The probe's "present" and the script's refusal also used to be two
separate copies of the same three-folder test; they are this one now.
#>

# The corpus to use when -Corpus is not given: $env:TWORLD_CORPUS, then the
# conventional folder under this user's profile -- the order run-playtest.ps1
# uses. The default used to be a literal "C:\Users\Jeremy\...", which did not
# exist on the laptop, whose profile is C:\Users\jerem.
# Returns $null when there is neither.
function Resolve-DefaultCorpus {
    if ($env:TWORLD_CORPUS) { return $env:TWORLD_CORPUS }
    $profileDir = if ($env:USERPROFILE) { $env:USERPROFILE } else { $HOME }
    if ($profileDir) { return (Join-Path $profileDir "Dropbox\Games\Computer\Chip's Challenge") }
    return $null
}

# A RELATIVE path made absolute, the way the engine resolves it. ⚠ In a current
# folder whose name holds [ or ], Windows PowerShell 5.1 resolves a relative
# -LiteralPath with those brackets ESCAPED (`[) and finds nothing, and Resolve-Path
# hands back the escaped form too (measured 2026-11-22). This resolver does not.
# A FULLY QUALIFIED path ("C:\x", "\\server\share") is returned as it is, and so
# is anything the resolver cannot place (a drive that does not exist throws
# there): Get-CorpusProblem then says "not a folder" about it. ⚠ "C:x" is NOT
# fully qualified -- it is relative to C:'s current folder -- although
# [IO.Path]::IsPathRooted says it is rooted (review, measured), so that test is
# not used here.
function ConvertTo-FullCorpusPath([string]$Path) {
    if (-not $Path) { return $Path }
    if ($Path -match '^[A-Za-z]:[\\/]' -or $Path -match '^[\\/]{2}') { return $Path }
    try { return $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Path) } catch { return $Path }
}

# Why $Path cannot be replayed as a corpus, or $null when it can: a folder with
# sets\, data\ and a save\ holding at least one .tws.
#
# 🔴 THE FOLDER ITSELF IS ASKED FIRST. On a drive that does not exist, Join-Path
# ERRORS rather than returning a path, and under "Continue" the caller carries on
# past it: run-corpus.ps1 once verified nothing that way and exited 0 (measured
# 2026-11-22). Test-Path is simply false there.
# ⚠ -LiteralPath and -PathType Container throughout: [ and ] in a folder name are
# otherwise wildcards, and a stray FILE named save would otherwise pass.
function Get-CorpusProblem([string]$Path) {
    if (-not $Path) { return "no corpus folder was given" }
    $Path = ConvertTo-FullCorpusPath $Path
    # A read error is its own answer, under the caller's "Continue" or "Stop"
    # alike: Test-Path itself RAISES on an unreachable \\server (measured), which
    # under the probe's "Stop" threw instead of reporting the corpus absent.
    try {
        if (-not (Test-Path -LiteralPath $Path -PathType Container -ErrorAction Stop)) { return "not a folder: $Path" }
        foreach ($sub in @("sets", "data", "save")) {
            if (-not (Test-Path -LiteralPath (Join-Path $Path $sub) -PathType Container -ErrorAction Stop)) {
                return "no $sub\ directory in $Path"
            }
        }
        $tws = Get-ChildItem -LiteralPath (Join-Path $Path "save") -Filter *.tws -File -ErrorAction Stop |
               Select-Object -First 1
    } catch {
        return ("cannot read {0}: {1}" -f $Path, $_.Exception.Message)
    }
    if (-not $tws) { return "no .tws solution file in $(Join-Path $Path 'save')" }
    return $null
}
