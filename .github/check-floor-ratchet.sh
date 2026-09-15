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
# ⚠ WHAT IT DOES NOT DO: it cannot tell a good reason from a bad one, and a
# change that deletes one case while adding another of the same size moves no
# total. It makes the easy path visible in history; it is not a proof.

set -euo pipefail

base="${1:-}"
head="${2:-HEAD}"

if [ -z "$base" ] || ! git cat-file -e "${base}^{commit}" 2>/dev/null; then
    echo "::error::check-floor-ratchet: base commit '${base}' is not available -- fetch history (fetch-depth: 0) or pass a real commit"
    exit 2
fi

# The sum of every declared check floor: tw_expect_atleast(N) in the harnessed
# tests, and the hand-written `if (checks < N)` in the two that predate it.
floors() {
    { git grep -hoE 'tw_expect_atleast\([0-9]+\)|if \(checks < [0-9]+\)' "$1" -- 'test/*.c' 'test/qt/*.cpp' || true; } |
        grep -oE '[0-9]+' | awk '{ s += $1 } END { print s + 0 }'
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
report "sum of test check floors"  "$(floors "$base")"  "$(floors "$head")"
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
