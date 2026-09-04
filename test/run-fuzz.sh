#!/bin/bash
#
# Builds and runs the libFuzzer targets over the file parsers.
#
#     test/run-fuzz.sh                  # every target, 60s each
#     test/run-fuzz.sh solution         # just the ones whose name matches
#     FUZZ_SECONDS=600 test/run-fuzz.sh # a longer soak
#     FUZZ_SECONDS=0 test/run-fuzz.sh   # replay the committed corpus and stop
#
# WHY FUZZING, AND WHY THIS SURFACE
#
# Every defect this project has shipped a fix for lives in the same place. jc-44
# was three (a .tws stack smash, a .dat pointer advanced by a file-supplied size,
# an RLE guard two bytes short); jc-45 was an unguarded map index out of a .dat;
# jc-46 was signed-shift UB in the .tws seed and time reads. Four releases, one
# root cause: this program's main job is parsing files that strangers made.
#
# Five of those six were found by a person reading a parser, suspecting a
# specific line, and hand-building a test that could observe that one thing.
# That works and it does not scale. The sixth was found by UndefinedBehavior-
# Sanitizer on its first run, in a line nobody had any reason to look at. This
# script is the generalization of that: instead of guessing which line is wrong,
# generate inputs until the program says so itself.
#
# LINUX ONLY, and for a different reason than run-sanitizers.sh
#
# run-sanitizers.sh is Linux-only because mingw-w64 ships no libasan. This one
# is Linux-only for that AND because it ships no libFuzzer, and because
# fuzz_leveldata.c uses fmemopen() to avoid a disk write per execution. There is
# no trap-on-error trick that recovers this one on Windows -- but see
# tw_corpus.h: every input that ever mattered is replayed by the ORDINARY unit
# suite on both platforms, which is where findings actually get pinned.
#
# 🔴 A FUZZER THAT FORGETS WHAT IT FOUND HAS BOUGHT NOTHING.
#
# libFuzzer generates fresh inputs every run, so a green run proves nothing
# durable and a crash found today can vanish tomorrow. The contract here is:
#
#   1. Every run REPLAYS test/fuzz/corpus/<target>/ first. That corpus is
#      committed, and a failure there fails the run before any fuzzing starts.
#   2. Anything new is written to test/fuzz/findings/ (gitignored).
#   3. A finding is not fixed until its input is committed to the corpus and a
#      case in the matching unit test replays it. See test/tw_corpus.h.
#
# Step 3 is the one that is easy to skip and is the entire point.

set -u
cd "$(dirname "${BASH_SOURCE[0]}")/.." || exit 1

FILTER="${1:-}"
CC="${CC:-clang}"
FUZZ_SECONDS="${FUZZ_SECONDS:-60}"
CORPUS_ROOT="test/fuzz/corpus"
FINDINGS="test/fuzz/findings"

# ASan's allocator refuses very large requests rather than returning NULL, which
# would report an allocation the parser asked for as a crash. The targets cap
# their input at 64 KB; this caps the process.
export ASAN_OPTIONS="detect_leaks=1:abort_on_error=1:halt_on_error=1"
export UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=1"

# CMakeLists.txt supplies this for every non-Windows build; the targets compile
# the source directly and never go through CMake. Same reasoning as
# run-sanitizers.sh -- series.c calls stricmp() unconditionally at line 65.
PORT="-Dstricmp=strcasecmp"

if ! command -v "$CC" > /dev/null 2>&1; then
    echo "no $CC on PATH -- libFuzzer needs clang; install it or set CC"
    exit 1
fi

OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT
mkdir -p "$FINDINGS"

fail=0
ran=0

for target in test/fuzz/fuzz_*.c; do
    name="$(basename "$target" .c)"
    short="${name#fuzz_}"
    if [ -n "$FILTER" ] && [ "${short#*"$FILTER"}" = "$short" ]; then continue; fi

    corpus="$CORPUS_ROOT/$short"
    mkdir -p "$corpus"
    exe="$OUT/$name"

    if ! $CC -std=gnu11 -g -O1 -I test/stub $PORT \
            -fsanitize=fuzzer,address,undefined \
            -fno-omit-frame-pointer \
            -o "$exe" "$target" 2> "$OUT/$name.cc.log"; then
        echo "=== $short : COMPILE FAILED ==="
        head -30 "$OUT/$name.cc.log"
        fail=$((fail + 1))
        continue
    fi

    # 1. Replay the committed corpus. -runs=0 means "run each input once and
    #    stop", so this is deterministic and fast, and it is what would catch a
    #    previously-fixed defect coming back.
    seeds=$(find "$corpus" -type f 2>/dev/null | wc -l)
    if [ "$seeds" -gt 0 ]; then
        if ! "$exe" -runs=0 "$corpus" > "$OUT/$name.replay" 2>&1; then
            echo "=== $short : THE COMMITTED CORPUS FAILED ==="
            echo "    A previously-found defect is back, or a fix regressed."
            tail -30 "$OUT/$name.replay"
            fail=$((fail + 1))
            continue
        fi
        echo "  ok    $short  replayed $seeds committed input(s) clean"
    else
        echo "  --    $short  no committed corpus yet"
    fi

    if [ "$FUZZ_SECONDS" -eq 0 ]; then
        ran=$((ran + 1))
        continue
    fi

    # 2. Fuzz.
    #
    # 🔴 THE SCRATCH DIRECTORY COMES FIRST, AND THAT IS NOT COSMETIC. libFuzzer
    # writes every new coverage-increasing input into the FIRST corpus directory
    # it is given. Passing the committed corpus first would mean an ordinary
    # local run silently added dozens of files to the repository -- `git status`
    # dirty after running the tests, and eventually a corpus nobody curated.
    # So new inputs land in scratch and the committed corpus is read-only here.
    # Growing it is a deliberate act: copy the input in, and add a case.
    #
    # -max_total_time bounds the run so it can sit in CI; -artifact_prefix needs
    # the trailing slash or libFuzzer concatenates it onto the filename.
    ran=$((ran + 1))
    mkdir -p "$OUT/new-$short"
    if "$exe" "$OUT/new-$short" "$corpus" \
            -max_total_time="$FUZZ_SECONDS" -max_len=65536 -rss_limit_mb=2048 \
            -artifact_prefix="$FINDINGS/" -print_final_stats=1 \
            > "$OUT/$name.fuzz" 2>&1; then
        execs=$(sed -n 's/.*stat::number_of_executed_units: *\([0-9]*\).*/\1/p' "$OUT/$name.fuzz" | tail -1)
        echo "  ok    $short  ${FUZZ_SECONDS}s, ${execs:-?} executions, no findings"
    else
        echo "=== $short : FINDING ==="
        # The reproducer libFuzzer just wrote is the whole value of the run.
        sed -n '/ERROR/,/SUMMARY/p' "$OUT/$name.fuzz" | head -40
        echo "--- reproducer(s) written to $FINDINGS/ ---"
        ls -la "$FINDINGS" 2>/dev/null | tail -5
        echo "    To fix properly: commit the input to $corpus/ and add a case to"
        echo "    the matching unit test so it is replayed everywhere. See test/tw_corpus.h."
        fail=$((fail + 1))
    fi
done

echo ""
if [ "$ran" -eq 0 ]; then
    echo "no fuzz targets ran -- treating that as a failure"
    exit 1
fi
if [ "$fail" -gt 0 ]; then
    echo "$fail fuzz target(s) FAILED"
    exit 1
fi
if [ "$FUZZ_SECONDS" -eq 0 ]; then
    echo "$ran target(s): committed corpus replayed clean (no fuzzing -- FUZZ_SECONDS=0)"
else
    echo "$ran target(s) clean after ${FUZZ_SECONDS}s each"
fi
exit 0
