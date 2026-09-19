#!/bin/bash
#
# Refuses a change that LOWERS a test floor, the NO_FIX_* witness count or the
# unit-guard count, unless a commit in the change says so with a trailer:
#
#     Test-Floor-Lowered: <why>
#
# Usage:  .github/check-floor-ratchet.sh <base-commit> [<head-commit>]
#
# 🔴 WHY THIS EXISTS. Every floor in this repository is exact and enforced --
# tw_expect_atleast(N) must equal the checks that ran, and nofix-matrix.tsv's
# #!EXPECT line must equal what the matrix holds -- and all of that compares the
# tree against a number THE SAME TREE CAN REWRITE. An adversarial audit deleted
# the unit case guarding jc-50 (this fork's own headline defect), restored the
# defect, lowered the case's floor by the ten checks it took with it, edited one
# count in CLAUDE.md, and every local gate reported green. The floors did not
# fail; they just made the deletion cost one extra number.
#
# No suite can stop a deliberate deletion. What it CAN do is refuse to let one
# be silent: this compares the change against its base, and a floor that went
# DOWN needs a sentence in the commit message saying why. A vacuous check
# removed, a case moved elsewhere, a test retired with its code -- all fine, all
# declared. A guard quietly deleted to make a red run green is not.
#
# 🔴 IT COMPARES EACH FILE, AND READS ONLY CODE. The first version summed every
# `tw_expect_atleast(N)` it could grep, across all files, from the raw text -- and
# a blind audit walked straight past it: delete jc-50's case, drop its floor from
# 290 to 280, and add the COMMENT `/* ... tw_expect_atleast(10) ... */` to
# random_test.c. The sum was unchanged, the ratchet said "no floor went down",
# and every local layer was green with half of jc-50 restored. So now:
#   * comments and string literals are stripped before anything is read;
#   * each test file is compared with ITSELF, so one file's gain cannot pay for
#     another's loss;
#   * a file's declarations are compared IN ORDER, and a change in how many it
#     has is flagged too -- an extra call in dead code is otherwise a way to
#     keep a number on the page that the binary never applies;
#   * a test that narrows its TESTLANG loses a whole language run (docs/adr/0004),
#     and that is a lowering like any other -- the same audit dropped input_test.c's
#     C++ run and nothing noticed;
#   * test/run-e2e.ps1's `$CheckFloor` is ratcheted with the rest.
#
# ⚠ WHAT IT DOES NOT DO: it cannot tell a good reason from a bad one, and a
# change that deletes one case while adding another of the same size IN THE SAME
# FILE moves nothing. It makes the easy path visible in history; it is not a
# proof, and a determined author can still write real checks that test nothing.

set -euo pipefail

base="${1:-}"
head="${2:-HEAD}"

if [ -z "$base" ] || ! git cat-file -e "${base}^{commit}" 2>/dev/null; then
    echo "::error::check-floor-ratchet: base commit '${base}' is not available -- fetch history (fetch-depth: 0) or pass a real commit"
    exit 2
fi

# C and C++ source with every comment and string/character literal blanked out,
# so a floor can only be read from code. Scanning left to right, whichever of
# the four openers comes first wins, which is what keeps a `/*` inside a string
# (or a quote inside a comment) from confusing it.
strip_c() {
    perl -0777 -pe 's{/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\\n])*"|'"'"'(?:\\.|[^'"'"'\\\n])*'"'"'}{ }gs'
}
# The floor declarations in one test file at one commit, in source order, one per
# line: tw_expect_atleast(N) in the harnessed tests, and the hand-written
# `if (checks < N)` in the two that predate tw_test.h. Empty if the file is absent.
decls() {
    { git show "$1:$2" 2>/dev/null || true; } | strip_c |
        { grep -oE '\btw_expect_atleast[[:space:]]*\([[:space:]]*[0-9]+[[:space:]]*\)|if[[:space:]]*\([[:space:]]*checks[[:space:]]*<[[:space:]]*[0-9]+[[:space:]]*\)' || true; } |
        grep -oE '[0-9]+' || true
}
# How many languages a unit test is built in: 2 unless it declares TESTLANG.
# Mirrors Get-TestLanguages in test/run-tests.ps1 -- the FIRST `TESTLANG:` line,
# anywhere in the file (it lives in a comment, so this reads the raw text), with
# a trailing `*/` dropped and the rest split on spaces and commas.
#
# ⚠ CASE-INSENSITIVE, because PowerShell's -ne and -contains are: the runner
# builds `TESTLANG: C` as C. And the grep must not be allowed to fail -- a review
# found that under `set -euo pipefail` a line with no lowercase match made this
# function return 1 and the whole ratchet exit silently, with no verdict. A line
# naming no language at all counts as ZERO runs, which is a lowering; the unit
# runner refuses such a file outright anyway.
langruns() {
    local line
    line="$({ git show "$1:$2" 2>/dev/null || true; } | grep -m1 -oE 'TESTLANG:.*' || true)"
    if [ -z "$line" ]; then echo 2; return; fi
    printf '%s\n' "${line#TESTLANG:}" | sed 's#\*/[[:space:]]*$##' | tr ', \t' '\n\n\n' |
        { grep -ixE 'c|c\+\+' || true; } | tr 'A-Z' 'a-z' | sort -u | wc -l | tr -d ' '
}
# The test files that carry floors, at one commit.
testfiles() {
    git ls-tree -r --name-only "$1" -- test/ | grep -E '^test/[^/]+\.c$|^test/qt/[^/]+\.cpp$' || true
}
# test/run-e2e.ps1's declared `$CheckFloor`, 0 where it did not exist yet.
e2efloor() {
    { git show "$1:test/run-e2e.ps1" 2>/dev/null || true; } |
        sed -n 's/^\$CheckFloor[[:space:]]*=[[:space:]]*\([0-9][0-9]*\).*/\1/p' | head -1 |
        awk '{ print $1 + 0 } END { if (NR == 0) print 0 }' | head -1
}
# One field of nofix-matrix.tsv's #!EXPECT line; 0 where the line did not exist yet.
expect_field() {
    { git show "$1:test/nofix/nofix-matrix.tsv" 2>/dev/null || true; } |
        sed -n "s/^#!EXPECT.*$2=\([0-9][0-9]*\).*/\1/p" | head -1 | awk '{ print $1 + 0 } END { if (NR == 0) print 0 }' | head -1
}

fail=0
report() {   # name before after
    if [ "$3" -lt "$2" ]; then
        echo "  LOWERED  $1: $2 -> $3"
        fail=1
    else
        echo "  ok       $1: $2 -> $3"
    fi
}

echo "comparing ${head} against ${base}"

# Each test file against itself. New files are free; a file that vanished, a
# declaration that went down, or a change in how many a file declares is not.
compared=0
while IFS= read -r f; do
    [ -n "$f" ] || continue
    compared=$((compared + 1))
    b="$(decls "$base" "$f" | tr '\n' ' ' | sed 's/ *$//')"
    if ! git cat-file -e "${head}:${f}" 2>/dev/null; then
        echo "  LOWERED  $f: the file is gone (floors: ${b:-none})"
        fail=1
        continue
    fi
    h="$(decls "$head" "$f" | tr '\n' ' ' | sed 's/ *$//')"
    if [ "$(wc -w <<< "$b")" -ne "$(wc -w <<< "$h")" ]; then
        echo "  CHANGED  $f: floor declarations went from [${b}] to [${h}] -- a floor"
        echo "           added or removed in place must say why"
        fail=1
    else
        read -r -a bb <<< "$b"
        read -r -a hh <<< "$h"
        for i in "${!bb[@]}"; do
            if [ "${hh[$i]}" -lt "${bb[$i]}" ]; then
                echo "  LOWERED  $f: floor ${bb[$i]} -> ${hh[$i]}"
                fail=1
            fi
        done
    fi
    # Not through report(): that prints an "ok" line per file, and piping it to
    # hide those would run it in a subshell and throw its `fail=1` away.
    case "$f" in
        test/qt/*) ;;   # the Qt layer builds C++ only; TESTLANG is not read there
        *)  br="$(langruns "$base" "$f")"
            hr="$(langruns "$head" "$f")"
            if [ "$hr" -lt "$br" ]; then
                echo "  LOWERED  $f: built in $br language(s) -> $hr (TESTLANG narrowed)"
                fail=1
            fi ;;
    esac
done <<< "$(testfiles "$base")"
[ "$fail" -eq 0 ] && echo "  ok       ${compared} test file(s): no floor lowered, no language run dropped"

report "end-to-end check floor"    "$(e2efloor "$base")" "$(e2efloor "$head")"
report "NO_FIX_* witnesses"        "$(expect_field "$base" witnesses)" "$(expect_field "$head" witnesses)"
report "NO_FIX_* unit guards"      "$(expect_field "$base" guarded)"   "$(expect_field "$head" guarded)"

if [ "$fail" -eq 0 ]; then
    echo "no floor went down"
    exit 0
fi

reason="$(git log --format=%B "${base}..${head}" | sed -n 's/^Test-Floor-Lowered:[[:space:]]*\(..*\)$/\1/p' | head -1)"
if [ -n "$reason" ]; then
    echo "a floor went down, and the change says why: ${reason}"
    exit 0
fi

echo "::error::a test floor went DOWN and no commit in ${base}..${head} says why."
echo "If this is deliberate -- a vacuous check removed, a case moved, a test retired"
echo "with the code it tested -- add a trailer to the commit message:"
echo "    Test-Floor-Lowered: <one sentence on why>"
echo "If it is not deliberate, a guard was deleted. Put it back."
exit 1
