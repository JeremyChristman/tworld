/* tw_test.h: the assertion harness every test in this directory uses.
 *
 * MOD (Jeremy). Header-only, C99 and C++11 both, no allocation, no dependency
 * on anything in the game. It has to be a header rather than a library because
 * of how these tests are built: each test is a single translation unit that
 * #includes the source under test directly, compiled twice -- once as C and
 * once as C++ -- with no CMake tree in sight. See
 * docs/adr/0003-tests-compile-the-source-under-test-directly.md.
 *
 * WRITING A TEST
 *
 *     #include "tw_test.h"
 *     #include "../encoding.c"
 *
 *     int main(void) {
 *         tw_begin("encoding");
 *
 *         tw_case("an x past the right edge is rejected");
 *         CHECK_INT(readpos_of(32, 0), POS_INVALID);
 *
 *         tw_case("a whole-map position round-trips");
 *         CHECK_INT(readpos_of(31, 31), 31 + 32 * 31);
 *
 *         return tw_end();
 *     }
 *
 * tw_case() closes the previous case and opens a new one; every check between
 * two calls belongs to the case named by the first. A case with no failing
 * check passes. Naming cases in prose is deliberate -- the name is what a
 * failure report shows somebody who has never read the file, and "case 7" tells
 * them nothing.
 *
 * MACHINE-READABLE OUTPUT
 *
 * Alongside the human summary, each run emits one TAB-separated line per case
 * and one for the run:
 *
 *     TWCASE<TAB>ok|fail|skip<TAB>case name<TAB>first failure message
 *     TWSUMMARY<TAB>suite<TAB>checks<TAB>failures<TAB>skipped
 *
 * test/run-tests.ps1 parses those into JUnit XML and JSON. The markers are on
 * stdout rather than in a file the test opens, because these binaries run under
 * two compilers and on a CI runner, and stdout is the one channel that behaves
 * identically everywhere. A tab is the separator because case names contain
 * spaces and commas and quotes, and none of them contain tabs.
 *
 * They are emitted only when the environment variable TW_TEST_MACHINE is set,
 * which run-tests.ps1 always sets. Running a test binary by hand -- which is
 * how you debug one -- therefore gives clean, readable output, and nobody has
 * to mentally filter a marker line out from between every pair of results.
 *
 * EXIT CODE is 0 only when zero checks failed AND at least one check ran. A
 * test binary that silently stops asserting -- an #if that excluded everything,
 * a fixture that failed to build -- would otherwise report success, which is
 * the failure mode a test suite must never have.
 *
 * SKIPPING. tw_skip("why") marks the OPEN case skipped and is not a failure.
 * Use it for a check that needs a file this repository does not contain; do not
 * use it to park a broken test, which is what a commented-out case is for and
 * what a reviewer can see.
 */

#ifndef	HEADER_tw_test_h_
#define	HEADER_tw_test_h_

#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Chosen to be larger than any case name or failure message this suite
 * produces, and small enough to live in .bss without comment. Truncation is
 * safe -- see tw_copy_() -- and a truncated name in a report is a cosmetic
 * problem, where a heap allocation in a test harness is a real one.
 */
#define	TW_TEXTMAX	256

static const char*	tw_suite_ = "(unnamed)";
static char		tw_casename_[TW_TEXTMAX];
static char		tw_casemsg_[TW_TEXTMAX];
static int		tw_caseopen_ = 0;
static int		tw_casefailed_ = 0;
static int		tw_caseskipped_ = 0;

static int		tw_checks_ = 0;
static int		tw_failures_ = 0;
static int		tw_skipped_ = 0;
static int		tw_cases_ = 0;
static int		tw_floor_ = 0;
static int		tw_casechecks_ = 0;

/* strncpy with the guarantee strncpy does not give: always terminated. Used for
 * every string that reaches the report, so that a case name longer than the
 * buffer truncates instead of running off the end of it.
 */
static inline void tw_copy_(char *dest, const char *src)
{
    size_t n;
    if (!src) { dest[0] = '\0'; return; }
    n = strlen(src);
    if (n >= TW_TEXTMAX) n = TW_TEXTMAX - 1;
    memcpy(dest, src, n);
    dest[n] = '\0';
}

/* A tab inside a case name or message would split a TWCASE line into the wrong
 * number of fields and desynchronize the parser on the other end. Newlines do
 * the same thing one line later, and are likelier -- a message built from a
 * multi-line buffer. Both are flattened to a space at emit time rather than
 * being rejected, because a report is not worth failing a test run over.
 */
static inline void tw_emitfield_(const char *s)
{
    size_t i;
    for (i = 0 ; s[i] ; ++i)
	putchar((s[i] == '\t' || s[i] == '\n' || s[i] == '\r') ? ' ' : s[i]);
}

/* Whether to print the TWCASE/TWSUMMARY marker lines. Read once and cached: a
 * test that changes its own environment mid-run would otherwise emit a report
 * the parser sees only half of, which is harder to diagnose than either
 * consistent answer.
 */
static inline int tw_machine_(void)
{
    static int cached = -1;
    if (cached < 0)
	cached = getenv("TW_TEST_MACHINE") != NULL;
    return cached;
}

static inline void tw_closecase_(void)
{
    const char *status;
    if (!tw_caseopen_)
	return;

    /* 🔴 A CASE THAT ASSERTED NOTHING IS A FAILURE, NOT A PASS.
     *
     * Without this it printed "ok" and emitted a passing TWCASE line, which is
     * the shape a vacuous test takes: a case whose checks all sit inside
     * `for (i = 0 ; i < state.somecount ; ++i)` and whose count regressed to
     * zero reports success while proving nothing at all. The per-file floor
     * (tw_expect_atleast) catches the aggregate version of this, but only if
     * the floor is tight, and it never says WHICH case went quiet. This does.
     *
     * A deliberately empty case is a skip -- tw_skip() says so in one line.
     */
    if (tw_casechecks_ == 0 && !tw_caseskipped_) {
	++tw_failures_;
	tw_casefailed_ = 1;
	printf("        this case ran no checks at all -- it asserts nothing\n");
	if (!tw_casemsg_[0])
	    tw_copy_(tw_casemsg_, "the case ran no checks at all");
    }

    if (tw_casefailed_)
	status = "fail";
    else if (tw_caseskipped_)
	status = "skip";
    else
	status = "ok";

    printf("  %-4s  %s\n", tw_casefailed_ ? "FAIL" : (tw_caseskipped_ ? "skip" : "ok"),
	   tw_casename_);
    if (tw_machine_()) {
	fputs("TWCASE\t", stdout);
	fputs(status, stdout);
	putchar('\t');
	tw_emitfield_(tw_casename_);
	putchar('\t');
	tw_emitfield_(tw_casemsg_);
	putchar('\n');
    }

    tw_caseopen_ = 0;
    tw_casefailed_ = 0;
    tw_caseskipped_ = 0;
    tw_casemsg_[0] = '\0';
}

static inline void tw_begin(const char *suite)
{
    /* Line-buffered, so that a test which CRASHES still shows what it had
     * completed. Under a pipe -- which is exactly how run-tests.ps1 invokes
     * these -- stdout is fully buffered by default, so a segfault in the middle
     * of a run discards every passing case as well, and whoever has to debug it
     * gets nothing at all. mslogic_test.c drives 4,800 lines of engine through
     * synthesized levels; a crash there is the likeliest failure this suite will
     * ever produce. */
    setvbuf(stdout, NULL, _IOLBF, 0);
    tw_suite_ = suite;
    printf("=== %s ===\n", suite);
}

static inline void tw_case(const char *name)
{
    tw_closecase_();
    tw_copy_(tw_casename_, name);
    tw_casemsg_[0] = '\0';
    tw_caseopen_ = 1;
    tw_casechecks_ = 0;
    ++tw_cases_;
}

static inline void tw_skip(const char *why)
{
    if (!tw_caseopen_)
	tw_case("(skipped before any case was opened)");
    tw_caseskipped_ = 1;
    ++tw_skipped_;
    if (!tw_casemsg_[0])
	tw_copy_(tw_casemsg_, why);
}

/* Records one failure. Only the FIRST message of a case is kept for the machine
 * report -- later ones are usually the same defect restated, and the human
 * output below prints every one anyway.
 */
static inline void tw_fail_(const char *file, int line, const char *text)
{
    char buf[TW_TEXTMAX];
    if (!tw_caseopen_)
	tw_case("(failure outside any case)");
    ++tw_failures_;
    tw_casefailed_ = 1;
    snprintf(buf, sizeof buf, "%s:%d: %s", file, line, text);
    printf("        %s\n", buf);
    if (!tw_casemsg_[0])
	tw_copy_(tw_casemsg_, buf);
}

/* The per-file assertion-count FLOOR.
 *
 * 🔴 This is not belt-and-braces, it is the load-bearing part. A test function
 * that stops being called -- an early `return`, a case commented out during
 * debugging and never restored, a helper that now bails before asserting --
 * removes coverage while leaving the suite green, and a green suite is exactly
 * what nobody re-reads. The two tests that predate this header each carried
 * their own `if (checks < N)` guard for that reason; the floor is kept
 * PER FILE and explicit rather than being generalized into "at least one check
 * ran", because the general version passes in every case the specific one is
 * there to catch.
 *
 * Raise the number when you add cases. Never lower it to make a run pass --
 * lowering it is the bug it exists to report.
 */
static inline void tw_expect_atleast(int n)
{
    tw_floor_ = n;
}

static inline int tw_end(void)
{
    tw_closecase_();
    printf("%d checks, %d failure%s, %d skipped, across %d case%s\n",
	   tw_checks_, tw_failures_, tw_failures_ == 1 ? "" : "s",
	   tw_skipped_, tw_cases_, tw_cases_ == 1 ? "" : "s");
    if (tw_machine_()) {
	/* The suite name goes through tw_emitfield_ like every other field. It
	 * is the caller's pointer rather than a copy, so nothing here has
	 * sanitized it, and a tab in it would desynchronize the summary line the
	 * same way one in a case name would. */
	fputs("TWSUMMARY\t", stdout);
	tw_emitfield_(tw_suite_);
	printf("\t%d\t%d\t%d\n", tw_checks_, tw_failures_, tw_skipped_);
    }

    /* Zero checks is a failure, not a pass. See the header. */
    if (tw_checks_ == 0) {
	printf("        no checks ran at all -- treating that as a failure\n");
	return 1;
    }
    if (tw_checks_ < tw_floor_) {
	printf("        only %d checks ran, but this file declares a floor of %d.\n"
	       "        Cases have stopped running. Do NOT lower the floor.\n",
	       tw_checks_, tw_floor_);
	return 1;
    }
    return tw_failures_ ? 1 : 0;
}

#ifdef __cplusplus
}
#endif

/* --- the check macros ---------------------------------------------------
 *
 * Every one of these counts a check, so a case that runs no macro reports zero
 * checks and tw_end() fails the run.
 *
 * They evaluate each argument EXACTLY ONCE into a local, so that CHECK_INT on a
 * call with a side effect -- advancing a PRNG, consuming a byte from a fixture
 * -- means what it reads. The obvious two-evaluation spelling silently doubles
 * such a call and the test then measures something nobody wrote.
 */

#define	TW_CHECK_MSG_(cond, file, line, ...)				\
    do {								\
	++tw_checks_;							\
	++tw_casechecks_;						\
	if (!(cond)) {							\
	    char tw_msg_[TW_TEXTMAX];					\
	    snprintf(tw_msg_, sizeof tw_msg_, __VA_ARGS__);		\
	    tw_fail_(file, line, tw_msg_);				\
	}								\
    } while (0)

/* Plain truth. */
#define	CHECK(cond)							\
    TW_CHECK_MSG_((cond), __FILE__, __LINE__, "expected true: %s", #cond)

/* Truth, with your own words for what it means when it is false. */
#define	CHECK_MSG(cond, ...)						\
    TW_CHECK_MSG_((cond), __FILE__, __LINE__, __VA_ARGS__)

/* Integers, printed as signed long so that every integer type in this codebase
 * -- int, unsigned char, unsigned long positions -- reports readably.
 */
#define	CHECK_INT(actual, expected)					\
    do {								\
	long tw_a_ = (long)(actual);					\
	long tw_e_ = (long)(expected);					\
	TW_CHECK_MSG_(tw_a_ == tw_e_, __FILE__, __LINE__,		\
		      "%s: expected %ld, got %ld", #actual, tw_e_, tw_a_); \
    } while (0)

#define	CHECK_NE_INT(actual, unwanted)					\
    do {								\
	long tw_a_ = (long)(actual);					\
	long tw_u_ = (long)(unwanted);					\
	TW_CHECK_MSG_(tw_a_ != tw_u_, __FILE__, __LINE__,		\
		      "%s: expected anything but %ld", #actual, tw_u_);	\
    } while (0)

/* Strings. A null on either side is a failure rather than a crash: a test that
 * segfaults reports nothing at all, and "the function returned NULL" is exactly
 * the finding worth keeping.
 */
#define	CHECK_STR(actual, expected)					\
    do {								\
	const char *tw_a_ = (actual);					\
	const char *tw_e_ = (expected);					\
	/* %.100s, not %s. The message buffer is TW_TEXTMAX and the strings in
	 * this codebase are routinely char[256] -- so an unbounded conversion
	 * lets GCC prove the message CAN be truncated, and -Werror=
	 * format-truncation then fails the build at the call site rather than
	 * here. Truncation would be harmless (the report is prose), but the
	 * alternative was suppressing the warning per test file, which would
	 * also hide the real ones. Two strings at 100 characters each still fit
	 * with room for the expression text. */				\
	TW_CHECK_MSG_(tw_a_ && tw_e_ && !strcmp(tw_a_, tw_e_),		\
		      __FILE__, __LINE__,				\
		      "%s: expected \"%.100s\", got \"%.100s\"", #actual, \
		      tw_e_ ? tw_e_ : "(null)", tw_a_ ? tw_a_ : "(null)"); \
    } while (0)

/* Bytes. Reports the offset of the first difference, which is the only part of
 * a 1,024-byte mismatch anybody reads.
 */
#define	CHECK_MEM(actual, expected, size)				\
    do {								\
	const unsigned char *tw_a_ = (const unsigned char *)(actual);	\
	const unsigned char *tw_e_ = (const unsigned char *)(expected);	\
	size_t tw_n_ = (size_t)(size);					\
	size_t tw_i_ = 0;						\
	while (tw_i_ < tw_n_ && tw_a_[tw_i_] == tw_e_[tw_i_])		\
	    ++tw_i_;							\
	TW_CHECK_MSG_(tw_i_ == tw_n_, __FILE__, __LINE__,		\
		      "%s: differs at byte %lu: expected 0x%02X, got 0x%02X", \
		      #actual, (unsigned long)tw_i_,			\
		      tw_i_ < tw_n_ ? tw_e_[tw_i_] : 0,			\
		      tw_i_ < tw_n_ ? tw_a_[tw_i_] : 0);		\
    } while (0)

#endif
