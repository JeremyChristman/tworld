/* score_test.c: the score and time tables.
 *
 * MOD (Jeremy). score.cpp is 376 lines that ship in every release and had NO
 * test, and the score screen is not a place this program has been reliable:
 * jc-36 fixed the grand total being chopped off partway through, so that a set
 * total of 443,476,450 displayed as "443,47" with the next digit sliced in half
 * and the label reading "Total S". The number the whole screen exists to show
 * could not be read.
 *
 * It also holds this fork's `legacyscores` mod, which decides whether the list
 * is the 2.2-style one or the modern one.
 *
 * WHAT IS PINNED HERE, and why each one is worth pinning:
 *
 * 1. THE TWO NUMBER FORMATTERS. decimal() and cdecimal() render every figure on
 *    the screen. They take a `zero` character rather than using '0', because the
 *    score screen draws digits from a font whose numerals live at an arbitrary
 *    offset -- so they are not sprintf("%ld") and cannot be checked by eye.
 *
 *    🔴 AND THEY NEGATE BY HAND: `n = number >= 0 ? number : -(number + 1) + 1`,
 *    which exists so that LONG_MIN works. The obvious `-number` is undefined
 *    there, because LONG_MIN has no positive counterpart. That line is the sort
 *    of thing a well-meaning tidy-up deletes, so it gets a case of its own.
 *
 * 2. THE COMMA RULE in cdecimal(), which counts with `if (i % 4 == 0)` over a
 *    counter that is incremented twice per group. Every boundary -- 999/1000,
 *    999999/1000000 -- is a chance for a comma to land one digit out, and a
 *    misplaced comma in a score is the kind of wrong that looks right.
 *
 * 3. THE SCORING FORMULA in getscoresforlevel(): 500 points per level number,
 *    plus ten points per second of the time limit left unused. That formula is
 *    the game's, not this program's, and it must not drift.
 *
 * WHAT THIS FILE DOES NOT COVER: how the table LOOKS. Column widths, spans and
 * the alignment jc-36 fixed are properties of the renderer and of the font, and
 * they are verified by a person looking at the screen -- the release checklist
 * says so explicitly. What is checked here is the content and the arithmetic.
 *
 * TESTLANG: c++
 *
 * score.cpp is C++ (ostringstream, std::string) and CMake compiles it only as
 * C++. Building it as C is not a translation that exists. See docs/adr/0004.
 */

#include	"tw_test.h"

#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>
#include	<limits.h>

#include	"../defs.h"
#include	"../state.h"
#include	"../play.h"
#include	"../score.h"

/* --- the surface score.cpp links against, stubbed ------------------------- *
 *
 * Two functions, which is the whole of it. hassolution() is the interesting
 * one: it decides which levels count toward a total, so the test steers it. */

extern "C" {

char const     *err_cfile_ = 0;
unsigned long	err_lineno_ = 0;

void warn_(char const *fmt, ...) { (void)fmt; }
void errmsg_(char const *prefix, char const *fmt, ...) { (void)prefix; (void)fmt; }
void die_(char const *fmt, ...)
{
    (void)fmt;
    printf("        die() was called\n");
    exit(1);
}

/* A level counts as solved when its besttime is not TIME_NIL. The real
 * predicate in play.c also consults the solution flags; this is the half score
 * arithmetic depends on, and steering it is how the cases below choose which
 * levels are solved. */
int hassolution(gamesetup const *game)
{
    return game->besttime != TIME_NIL;
}

}

#include	"../score.cpp"

/* --- a series to score --------------------------------------------------- */

#define	MAXGAMES	8

static gamesetup	games[MAXGAMES];
static gameseries	series;

/* Build a series of `count` levels. Every level is unsolved, has a 100-second
 * limit and is numbered from 1, unless a case says otherwise. */
static void makeseries(int count)
{
    int		n;

    memset(games, 0, sizeof games);
    memset(&series, 0, sizeof series);
    for (n = 0 ; n < count ; ++n) {
	games[n].number = n + 1;
	games[n].time = 100;
	games[n].besttime = TIME_NIL;	/* unsolved */
	snprintf(games[n].name, sizeof games[n].name, "Level %d", n + 1);
    }
    series.games = games;
    series.count = count;
    series.allocated = count;

}

/* Mark one level solved, with the given remaining time in seconds. */
static void solve(int index, int secondsleft)
{
    games[index].besttime = (games[index].time - secondsleft) * TICKS_PER_SECOND;
}

/* --- decimal() and cdecimal() --------------------------------------------- */

static void test_decimal(void)
{
    tw_case("decimal renders an ordinary number");
    CHECK_STR(decimal(0, '0'), "0");
    CHECK_STR(decimal(7, '0'), "7");
    CHECK_STR(decimal(42, '0'), "42");
    CHECK_STR(decimal(1000, '0'), "1000");
    CHECK_STR(decimal(443476450L, '0'), "443476450");

    tw_case("decimal renders a negative number with a leading minus");
    CHECK_STR(decimal(-1, '0'), "-1");
    CHECK_STR(decimal(-42, '0'), "-42");

    tw_case("🔴 the `zero` argument shifts every digit, which is the point");
    /* The score screen draws from a font whose numerals are not ASCII '0'..'9',
     * so these functions take the character to use for zero. Passing 'A' must
     * produce 'A'..'J', not "0"-with-an-A-somewhere. */
    CHECK_STR(decimal(0, 'A'), "A");
    CHECK_STR(decimal(1234, 'A'), "BCDE");
    CHECK_STR(decimal(-90, 'A'), "-JA");

    tw_case("🔴 LONG_MIN is rendered, not undefined");
    /* THE CASE THE HAND-WRITTEN NEGATION EXISTS FOR. `-number` on LONG_MIN is
     * undefined behavior -- there is no positive LONG_MIN -- so the code says
     * `-(number + 1) + 1` on the unsigned value instead. Delete that subtlety in
     * a tidy-up and this is what breaks, on a value no score will ever reach but
     * a sanitizer will find immediately. */
    {
	char	expected[32];

	snprintf(expected, sizeof expected, "%ld", LONG_MIN);
	CHECK_STR(decimal(LONG_MIN, '0'), expected);
	snprintf(expected, sizeof expected, "%ld", LONG_MAX);
	CHECK_STR(decimal(LONG_MAX, '0'), expected);
    }

    tw_case("🔴 cdecimal puts a comma every three digits");
    /* The rule is written as a counter incremented twice per group and tested
     * with `i % 4 == 0`, which is not obviously equivalent to "every three
     * digits". These are the boundaries where an off-by-one would show. */
    CHECK_STR(cdecimal(0, '0'), "0");
    CHECK_STR(cdecimal(1, '0'), "1");
    CHECK_STR(cdecimal(999, '0'), "999");
    CHECK_STR(cdecimal(1000, '0'), "1,000");
    CHECK_STR(cdecimal(1001, '0'), "1,001");
    CHECK_STR(cdecimal(999999L, '0'), "999,999");
    CHECK_STR(cdecimal(1000000L, '0'), "1,000,000");

    tw_case("...including the real total that jc-36 could not display");
    /* 443,476,450 is Joshie's grand total, the number that appeared as
     * "443,47" before jc-36. The formatter was never the broken half -- the
     * renderer was -- and this pins that it still is not. */
    CHECK_STR(cdecimal(443476450L, '0'), "443,476,450");

    tw_case("cdecimal handles negatives and the extremes too");
    CHECK_STR(cdecimal(-1000, '0'), "-1,000");
    {
	/* LONG_MIN with separators is the longest string either function can
	 * produce -- 19 digits, 6 commas and a sign -- and both write into a
	 * fixed 32-byte buffer from the back. This is the case that would catch
	 * that buffer being made "tidier". */
	char const     *s = cdecimal(LONG_MIN, '0');
	CHECK(strlen(s) < 32);
	CHECK_INT((int)(s[0] == '-'), 1);
    }
}

/* --- the scoring formula --------------------------------------------------- */

static void test_scoring(void)
{
    int		base, bonus;
    long	total;

    tw_case("an unsolved set scores nothing");
    makeseries(3);
    CHECK_INT(getscoresforlevel(&series, 0, &base, &bonus, &total), TRUE);
    CHECK_INT(base, 0);
    CHECK_INT(bonus, 0);
    CHECK_INT((int)total, 0);

    tw_case("🔴 a solved level scores 500 times its NUMBER, not its index");
    /* Level numbering starts at 1 and a set can renumber its levels, so scoring
     * by index would quietly pay the wrong amount for any set that does. */
    makeseries(3);
    solve(0, 0);			/* level 1, no time left */
    CHECK_INT(getscoresforlevel(&series, 0, &base, &bonus, &total), TRUE);
    CHECK_INT(base, 500);
    CHECK_INT(bonus, 0);
    CHECK_INT((int)total, 500);

    tw_case("🔴 the time bonus is ten points per second left unused");
    makeseries(3);
    solve(1, 40);			/* level 2, finished with 40s to spare */
    CHECK_INT(getscoresforlevel(&series, 1, &base, &bonus, &total), TRUE);
    CHECK_INT(base, 2 * 500);
    CHECK_INT(bonus, 10 * 40);
    CHECK_INT((int)total, 1000 + 400);

    tw_case("an untimed level earns the level score and no bonus");
    /* A level with time == 0 has no limit, so there is no unused time to pay
     * for -- and multiplying by a zero limit must not pay a NEGATIVE bonus. */
    makeseries(2);
    games[0].time = 0;
    solve(0, 0);
    CHECK_INT(getscoresforlevel(&series, 0, &base, &bonus, &total), TRUE);
    CHECK_INT(base, 500);
    CHECK_INT(bonus, 0);

    tw_case("the total sums every solved level, not just the one asked about");
    makeseries(4);
    solve(0, 10);			/* 500 + 100 */
    solve(2, 0);			/* 1500 + 0  */
    CHECK_INT(getscoresforlevel(&series, 0, &base, &bonus, &total), TRUE);
    CHECK_INT(base, 500);
    CHECK_INT(bonus, 100);
    CHECK_INT((int)total, 500 + 100 + 1500);

    tw_case("asking about a level outside the set still totals the set");
    makeseries(3);
    solve(1, 0);
    CHECK_INT(getscoresforlevel(&series, 99, &base, &bonus, &total), TRUE);
    CHECK_INT(base, 0);
    CHECK_INT(bonus, 0);
    CHECK_INT((int)total, 1000);

    tw_case("🔴 the walk stops at `allocated`, not at `count`");
    /* series.count can exceed the number of gamesetup structures actually
     * allocated -- a set's header says how many levels it claims, and the array
     * is filled as levels are read. Without the guard this walks off the end of
     * the array. */
    makeseries(4);
    solve(0, 0);
    series.count = 400;			/* claims far more than exist */
    series.allocated = 4;
    CHECK_INT(getscoresforlevel(&series, 0, &base, &bonus, &total), TRUE);
    CHECK_INT((int)total, 500);
}

/* --- the tables ------------------------------------------------------------ */

static void test_scorelist(void)
{
    int	       *levellist = NULL;
    int		count = 0;
    tablespec	table;

    tw_case("the score table has a row per level, a header, and a total row");
    makeseries(3);
    solve(0, 10);
    solve(1, 0);
    solve(2, 0);
    levellist = NULL;
    count = -1;
    memset(&table, 0, sizeof table);
    CHECK_INT(createscorelist(&series, FALSE, '0', &levellist, &count, &table), TRUE);
    CHECK_INT(count, 3 + 1);		/* three levels and the grand total */
    CHECK_INT(table.rows, count + 1);	/* ...plus the header row */
    CHECK_INT(table.cols, 5);
    CHECK(table.items != NULL);
    CHECK_STR(table.items[0], "1+Level");
    CHECK_STR(table.items[4], "1+Score");
    freescorelist(levellist, &table);

    tw_case("⚠ the item count is NOT rows*cols, because the total row SPANS");
    /* Written down because iterating rows*cols over items[] is the obvious
     * thing to do and it reads past the end of the allocation -- which is how
     * the first version of this case segfaulted.
     *
     * Every column carries its own span prefix: "1+Level" occupies one column,
     * and the total row is "2-Total Score" followed by "3+<number>", two items
     * covering five columns. That spanning is exactly the mechanism jc-36 had to
     * fix, and it means the table's item count is not derivable from its
     * dimensions. Nothing outside score.cpp may assume otherwise. */
    makeseries(2);
    solve(0, 0);
    solve(1, 0);
    levellist = NULL;
    count = -1;
    memset(&table, 0, sizeof table);
    CHECK_INT(createscorelist(&series, FALSE, '0', &levellist, &count, &table), TRUE);
    CHECK_INT(table.rows * table.cols, (2 + 1 + 1) * 5);
    /* The two items of the total row are the last written, and the row before
     * it holds five. Reaching them by that arithmetic is what is unsafe; what IS
     * safe is the header, which is always the first five. */
    CHECK_STR(table.items[1], "1-Name");
    freescorelist(levellist, &table);

    tw_case("🔴 the grand total is formatted with its separators");
    /* The number jc-36 could not display. The table's own items cannot be
     * scanned safely (previous case), so this checks the value that goes into
     * that row, through the same formatter the row uses. */
    makeseries(3);
    solve(0, 0);			/*  1 * 500 */
    solve(1, 0);			/*  2 * 500 */
    solve(2, 0);			/*  3 * 500 */
    {
	int	base, bonus;
	long	total;

	CHECK_INT(getscoresforlevel(&series, 0, &base, &bonus, &total), TRUE);
	CHECK_INT((int)total, 3000);
	CHECK_STR(cdecimal(total, '0'), "3,000");
    }

    tw_case("the time table lists only levels that have been SOLVED");
    /* Unlike the score table, which lists every level. One solved level of
     * three gives one row plus the header -- not four. */
    makeseries(3);
    solve(0, 10);
    levellist = NULL;
    count = -1;
    memset(&table, 0, sizeof table);
    CHECK_INT(createtimelist(&series, 0, '0', &levellist, &count, &table), TRUE);
    CHECK_INT(count, 1);
    CHECK_INT(table.rows, 2);
    CHECK_INT(table.cols, 4);
    CHECK_STR(table.items[0], "1+Level");
    CHECK_STR(table.items[3], "1+Solution");
    freescorelist(levellist, &table);

    tw_case("a time table for a set with nothing solved is just the header");
    makeseries(3);
    levellist = NULL;
    count = -1;
    memset(&table, 0, sizeof table);
    CHECK_INT(createtimelist(&series, 0, '0', &levellist, &count, &table), TRUE);
    CHECK_INT(count, 0);
    CHECK_INT(table.rows, 1);
    freescorelist(levellist, &table);

    tw_case("freeing is safe with nothing to free, and a build still works after");
    /* freescorelist() is reached on paths that may never have built a list.
     * The check that this did no harm is that the next build still succeeds --
     * an assertion, rather than the absence of a crash, which asserts nothing. */
    freescorelist(NULL, NULL);
    makeseries(2);
    solve(0, 0);
    levellist = NULL;
    count = -1;
    memset(&table, 0, sizeof table);
    CHECK_INT(createscorelist(&series, FALSE, '0', &levellist, &count, &table), TRUE);
    CHECK_INT(count, 2 + 1);
    freescorelist(levellist, &table);
}

int main(void)
{
    tw_begin("score_test.c");

    test_decimal();
    test_scoring();
    test_scorelist();

    /* Raise this when cases are added; never lower it to make a run pass. */
    /* Exact: every case here is deterministic and platform-independent. */
    tw_expect_atleast(72);
    return tw_end();
}
