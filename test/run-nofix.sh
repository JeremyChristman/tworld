#!/bin/bash
#
# Checks the NO_FIX_* differential matrix (test/nofix/nofix-matrix.tsv).
#
# For every toggle with a recorded WITNESS, this builds the engine twice -- with
# the fix on and with the fix off -- replays that one seed through both, and
# asserts three things:
#
#   * the fix-on digest is still what the matrix recorded,
#   * the fix-off digest is still what the matrix recorded, and
#   * THE TWO STILL DIFFER.
#
# The third is the one with teeth. It fails if the fix stops being reachable, if
# the toggle silently stops toggling, or if either behavior changes -- and none
# of those has any other alarm on it in this repository.
#
# 🔴 A BLANK ROW IS NOT A FAILURE. It records that the search did not find an
# input distinguishing that toggle, which is a statement about the search and
# not about the fix. See test/nofix/nofix.c. A blank row is instead checked
# for a NAMED UNIT CASE that fails with its toggle defined -- and the counts of
# both kinds must match the matrix's own #!EXPECT line exactly.
#
# Usage:  test/run-nofix.sh            check every witness in the matrix
#         CC=clang test/run-nofix.sh   with a different compiler
#
# Runs from the repository root. Needs only a C compiler.

set -u

CC="${CC:-gcc}"
root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root" || exit 2

# On the maintainer's Windows box this runs under MSYS2's bash, where gcc is not
# on PATH by default. Prepending it is a no-op on the Linux CI runner, which has
# no such directory. It must go FIRST: the gcc driver cannot spawn cc1 unless its
# own directory is on PATH, and when it cannot it fails with no diagnostic at all.
[ -d /c/msys64/mingw64/bin ] && PATH="/c/msys64/mingw64/bin:$PATH"
export PATH

matrix="test/nofix/nofix-matrix.tsv"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

if [ ! -f "$matrix" ]; then
    echo "no matrix at $matrix" >&2
    exit 2
fi

src="test/nofix/nofix.c mslogic.c encoding.c random.c"
cflags="-std=gnu11 -w -O2 -I."

echo "building the fix-on (default) engine..."
# shellcheck disable=SC2086
if ! $CC $cflags -o "$tmp/default.exe" $src; then
    echo "the default build failed" >&2
    exit 2
fi

pass=0; fail=0; blank=0; checked=0

while IFS=$'\t' read -r toggle seed dig_on dig_off outcome ticks; do
    case "$toggle" in
        ''|'#'*) continue ;;
    esac
    if [ "$seed" = "-" ]; then
        blank=$((blank + 1))
        continue
    fi
    checked=$((checked + 1))

    # shellcheck disable=SC2086
    # The build id is passed as a bare SUFFIX, not a quoted name -- see the long
    # note in nofix.c. Quoting does not survive PowerShell, and passing the full
    # macro name stringifies to "1" because -D$toggle already defines it.
    if ! $CC $cflags -D"$toggle" -DNOFIX_BUILD_ID_TOKEN="${toggle#NO_FIX_}" \
         -o "$tmp/off.exe" $src 2>"$tmp/build.err"; then
        echo "  FAIL  $toggle: the fix-off build does not compile"
        sed 's/^/        /' "$tmp/build.err" | head -5
        fail=$((fail + 1))
        continue
    fi

    # The binary states which toggle it carries. Without this a mistake in the
    # build line would compare the default against itself, every digest would
    # match, and the run would pass while checking nothing.
    id="$("$tmp/off.exe" -id)"
    if [ "$id" != "$toggle" ]; then
        echo "  FAIL  $toggle: the fix-off binary reports itself as '$id'"
        fail=$((fail + 1))
        continue
    fi

    got_on="$("$tmp/default.exe" -one "$seed" | cut -f1)"
    got_off="$("$tmp/off.exe" -one "$seed" | cut -f1)"

    if [ "$got_on" = "$got_off" ]; then
        echo "  FAIL  $toggle: seed $seed no longer tells the two builds apart"
        echo "        both now digest to $got_on"
        echo "        The fix may have stopped being reachable, or the toggle"
        echo "        may have stopped toggling. Do not just re-run the search."
        fail=$((fail + 1))
    elif [ "$got_on" != "$dig_on" ] || [ "$got_off" != "$dig_off" ]; then
        echo "  FAIL  $toggle: seed $seed still distinguishes them, but the"
        echo "        digests moved (engine behavior changed)"
        echo "          fix on : $dig_on -> $got_on"
        echo "          fix off: $dig_off -> $got_off"
        fail=$((fail + 1))
    else
        echo "  ok    $toggle (seed $seed, $outcome/${ticks}t)"
        pass=$((pass + 1))
    fi
done < "$matrix"

echo ""
echo "NO_FIX_* differential matrix: $pass passed, $fail failed, of $checked witness(es)"
echo "$blank toggle(s) have no witness and were not checked -- see test/nofix/nofix.c"

if [ "$checked" -eq 0 ]; then
    echo "no witnesses in the matrix at all -- refusing to report success" >&2
    exit 2
fi

# 🔴 THE SECOND ORACLE, WHICH THIS SCRIPT DID NOT HAVE. A witness-less toggle can
# still be guarded by a NAMED UNIT CASE: build test/mslogic_test.c with the
# toggle defined and require it to FAIL. test/run-nofix.ps1 has done that since
# the guards were written -- but CI runs THIS script, so in CI the guards were
# never checked and could rot with nothing red. Same logic, same control.
case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*) PORT="" ;;
    *)                    PORT="-Dstricmp=strcasecmp" ;;   # as run-sanitizers.sh
esac
unitflags="-std=gnu11 -w -I test/stub $PORT -x c"
guarded=0; unguarded=""; control_ok=1

echo ""
echo "--- witness-less toggles: is a unit case guarding them? ---"
# THE CONTROL: "fails with the toggle" only means "guarded" if the same test
# PASSES with nothing defined. Otherwise a test that is red for any unrelated
# reason makes every toggle read as guarded.
# shellcheck disable=SC2086
if ! $CC $unitflags -o "$tmp/unit-control" test/mslogic_test.c 2>"$tmp/unit.err"; then
    echo "  FAIL  mslogic_test.c does not compile with no toggle defined"
    head -5 "$tmp/unit.err" | sed 's/^/        /'
    control_ok=0
elif ! "$tmp/unit-control" >"$tmp/unit.log" 2>&1; then
    echo "  FAIL  mslogic_test.c does not pass with NO toggle defined, so a failure"
    echo "        under a toggle proves nothing. Not measuring the unit guards."
    tail -5 "$tmp/unit.log" | sed 's/^/        /'
    control_ok=0
fi
if [ "$control_ok" -eq 0 ]; then
    fail=$((fail + 1))
else
    while IFS=$'\t' read -r toggle seed _rest; do
        case "$toggle" in ''|'#'*) continue ;; esac
        [ "$seed" = "-" ] || continue
        # shellcheck disable=SC2086
        if ! $CC $unitflags -D"$toggle" -o "$tmp/unit-off" test/mslogic_test.c 2>/dev/null; then
            unguarded="$unguarded $toggle(did-not-compile)"
        elif "$tmp/unit-off" >/dev/null 2>&1; then
            unguarded="$unguarded $toggle"
        else
            echo "  ok    $toggle is caught by a unit case"
            guarded=$((guarded + 1))
        fi
    done < "$matrix"
    echo "  $guarded of $blank witness-less toggles are guarded by a named unit case"
    for t in $unguarded; do echo "    unguarded: $t"; done
fi

# 🔴 THE RATCHET -- the matrix declares what it must contain, EXACTLY. An audit
# blanked one witness row and the suite stayed green; a floor instead of an
# exact count would leave room for a gain to hide a later loss. A malformed
# line fails closed: an unparsed number must never default to zero.
expect="$(grep -m1 '^#!EXPECT' "$matrix")"
want_w="$(printf '%s' "$expect" | sed -n 's/.*witnesses=\([0-9][0-9]*\).*/\1/p')"
want_g="$(printf '%s' "$expect" | sed -n 's/.*guarded=\([0-9][0-9]*\).*/\1/p')"
if [ -z "$want_w" ] || [ -z "$want_g" ]; then
    echo "no well-formed #!EXPECT line (witnesses=N guarded=N) in $matrix -- refusing to report success" >&2
    exit 2
fi
mismatch=0
if [ "$checked" -ne "$want_w" ]; then
    echo "  FAIL  ratchet: $want_w witness(es) declared, $checked present"
    mismatch=1
fi
if [ "$control_ok" -eq 1 ] && [ "$guarded" -ne "$want_g" ]; then
    echo "  FAIL  ratchet: $want_g unit guard(s) declared, $guarded present"
    mismatch=1
fi
if [ "$mismatch" -eq 1 ]; then
    echo "        Fewer: restore what was lost. More: raise the #!EXPECT line in the same commit."
    fail=$((fail + 1))
elif [ "$control_ok" -eq 1 ]; then
    echo "  ratchet: $checked witness(es) and $guarded unit guard(s), exactly as declared"
fi

[ "$fail" -eq 0 ] || exit 1
exit 0
