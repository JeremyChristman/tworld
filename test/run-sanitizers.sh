#!/bin/bash
#
# Builds and runs the unit tests under AddressSanitizer and UndefinedBehaviorSanitizer.
#
#     test/run-sanitizers.sh            # every test
#     test/run-sanitizers.sh random     # just the ones whose name matches
#
# WHY THIS IS A SHELL SCRIPT AND NOT PART OF run-tests.ps1
#
# It cannot run on the maintainer's machine at all. This project builds with
# mingw-w64 GCC, which ships NO libasan and NO libubsan -- `-fsanitize=address`
# fails at link time with "cannot find -lasan". So sanitizers are a Linux-only
# capability here, and this is the Linux-side entry point, driven by the
# `sanitizers` job in .github/workflows/ci.yml.
#
# WHY IT MATTERS MORE THAN THE COVERAGE NUMBER
#
# This program's main job is parsing files that strangers made -- .dat level
# sets, .dac configurations, .tws solution collections. Four memory-safety
# defects were found in those parsers in jc-44 and jc-45, every one of them by
# reading the code and then hand-building a test that could observe it. That
# works and it does not scale. ASan observes the whole class directly: it would
# have flagged all four without anybody suspecting them first.
#
# WHAT IT DELIBERATELY DOES NOT DO
#
# Leak detection is OFF. The unit tests leak on purpose in places -- the .tws
# truncation cases abandon a partly-built move list precisely because that is
# what the code under test does on a malformed file -- and a leak in a test
# harness is not the thing this exists to catch. The target is memory ERRORS:
# out-of-bounds, use-after-free, and undefined behavior. Turning leak checking
# on is a worthwhile separate exercise; it is not this one, and pretending
# otherwise would just mean the job stays red and gets ignored.
#
# The tests here compile the source under test directly (docs/adr/0003), so a
# sanitizer build instruments the ENGINE and the PARSERS, not just the harness.

set -u
cd "$(dirname "${BASH_SOURCE[0]}")/.." || exit 1

FILTER="${1:-}"
CC="${CC:-gcc}"
CXX="${CXX:-g++}"

# Errors, not leaks -- see the header. halt_on_error keeps the first report as
# the failure rather than letting a cascade bury it.
export ASAN_OPTIONS="detect_leaks=0:abort_on_error=0:halt_on_error=1"
export UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=1"
# tw_test.h emits its machine-readable markers only when this is set.
export TW_TEST_MACHINE=1

# Overridable so the script's own logic -- the TESTFLAGS/TESTLANG parsing, the
# loop, the reporting -- can be exercised on a toolchain that has no sanitizer
# runtime (SAN= test/run-sanitizers.sh). Do not override it in CI; that would
# turn this job into a second, slower copy of the unit suite.
SAN="${SAN--fsanitize=address,undefined -fno-omit-frame-pointer -g -O1}"
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT

# Never claim a sanitizer verdict the run did not produce. SAN can be emptied to
# exercise this script on a toolchain without a sanitizer runtime, and saying
# "clean under ASan" after that would be exactly the kind of false green the rest
# of this suite exists to prevent.
#
# And the label is DERIVED from the flags rather than assumed. This used to say
# "ASan+UBSan" for any non-empty SAN, which meant overriding SAN with UBSan
# alone -- the one combination that works on Windows, see docs/adr/0010 --
# printed "clean under AddressSanitizer" after never running it. That is the
# exact false green the paragraph above refuses to print.
if [ -z "$SAN" ]; then
    WHAT="NO SANITIZERS (SAN was overridden)"
else
    WHAT=""
    case "$SAN" in *address*)        WHAT="ASan" ;; esac
    case "$SAN" in *undefined*)      WHAT="${WHAT:+$WHAT+}UBSan" ;; esac
    case "$SAN" in *trap-on-error*)  WHAT="$WHAT, trapping (no report)" ;; esac
    [ -z "$WHAT" ] && WHAT="the flags in SAN"
fi

# CMakeLists.txt applies this to every non-Windows build, in its else() branch.
# These tests compile the source under test directly and never go through CMake
# (docs/adr/0003), so the definition has to be repeated here -- without it
# series.c fails to link with an undefined reference to stricmp. Windows has
# stricmp in its CRT, so the script stays runnable there too.
case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*) PORT="" ;;
    *)                    PORT="-Dstricmp=strcasecmp" ;;
esac

fail=0
ran=0

for test in test/*_test.c; do
    name="$(basename "$test" .c)"
    if [ -n "$FILTER" ] && [ "${name#*"$FILTER"}" = "$name" ]; then continue; fi

    # Same TESTFLAGS/TESTLANG contract the PowerShell runner honors, so a test
    # that narrows itself is narrowed here too rather than failing confusingly.
    extra="$(sed -n 's@.*TESTFLAGS:\(.*\)@\1@p' "$test" | head -1 | sed 's@\*/[[:space:]]*$@@')"
    langs="$(sed -n 's@.*TESTLANG:\(.*\)@\1@p' "$test" | head -1 | sed 's@\*/[[:space:]]*$@@' | tr -d ' ')"
    [ -z "$langs" ] && langs="c c++"

    for lang in $langs; do
        if [ "$lang" = "c" ]; then comp="$CC"; std="-std=gnu11"; else comp="$CXX"; std="-std=gnu++11"; fi
        exe="$OUT/$name-${lang//+/p}"

        # shellcheck disable=SC2086
        if ! $comp $std -Wall -Wextra -I test/stub $PORT $extra $SAN -x "$lang" -o "$exe" "$test" 2> "$OUT/$name.cc.log"; then
            echo "=== $name [$lang] : COMPILE FAILED ==="
            head -25 "$OUT/$name.cc.log"
            fail=$((fail + 1))
            continue
        fi

        ran=$((ran + 1))
        if "$exe" > "$OUT/$name.out" 2> "$OUT/$name.err"; then
            checks="$(sed -n 's/^\([0-9]*\) checks.*/\1/p' "$OUT/$name.out" | head -1)"
            echo "  ok    $name [$lang]  ${checks:-?} checks, clean under $WHAT"
        else
            echo "=== $name [$lang] : FAILED ==="
            # A sanitizer report goes to stderr and is the whole point of the run,
            # so it is shown in full rather than summarized away.
            if grep -qE 'ERROR: (Address|Leak)Sanitizer|runtime error:' "$OUT/$name.err"; then
                echo "--- sanitizer report ---"
                sed -n '1,40p' "$OUT/$name.err"
            else
                tail -15 "$OUT/$name.out"
                tail -10 "$OUT/$name.err"
            fi
            fail=$((fail + 1))
        fi
    done
done

echo ""
if [ "$ran" -eq 0 ]; then
    echo "no test binaries ran -- treating that as a failure"
    exit 1
fi
if [ "$fail" -gt 0 ]; then
    echo "$fail sanitizer run(s) FAILED"
    exit 1
fi
if [ -n "$SAN" ]; then
    echo "$ran run(s) clean under $WHAT"
else
    echo "$ran run(s) passed, but WITHOUT SANITIZERS -- this proves nothing about memory safety"
fi
exit 0
