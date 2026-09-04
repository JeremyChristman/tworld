/* random_test.c: the game's random-number generator.
 *
 * MOD (Jeremy). Compiles ../random.c directly, so the static nextvalue() and
 * the lastvalue shared state are both reachable.
 *
 * WHY THIS EXISTS. random.c is the one module in Tile World whose correctness
 * is not about being good -- it is about never changing. Its own header comment
 * says so: the generator is here "simply because it is necessary for the game to
 * use the same generator FOREVER", because a solution recorded under one
 * sequence only replays under the same sequence. Every stored .tws in every
 * player's save directory depends on these exact bit manipulations.
 *
 * So the cases below are deliberately of two kinds:
 *
 *   1. GOLDEN SEQUENCES, hardcoded. Captured from this implementation and
 *      checked back against it. They catch any change to the arithmetic,
 *      including a change that is arguably an improvement.
 *   2. INDEPENDENT RESTATEMENTS of the documented rule -- the LCG constants,
 *      "one draw per permutation", "the result is a permutation". A golden
 *      value alone would bless whatever the code happened to do on the day it
 *      was captured; these say what it is supposed to do.
 *
 * ⚠ A FAILURE HERE IS NOT A BUG IN THIS TEST. If these stop matching, stored
 * solutions have stopped replaying, and the change that did it must be reverted
 * rather than the numbers updated. See
 * docs/adr/0009-the-rng-must-never-change.md.
 *
 * ⚠ THE GENERATOR DELIBERATELY DOES NOT MATCH THE ORIGINAL CHIPS.EXE. That was
 * established by reverse engineering: MSCC uses a different generator, and Tile
 * World and SuperCC share this one byte for byte. Making it match MSCC would
 * break every solution ever recorded by either program. There is a case below
 * that asserts the constants are the glibc ones specifically, so that a
 * well-meant "fix" toward MSCC fails loudly instead of quietly.
 */

#include	<time.h>

#include	"tw_test.h"
#include	"../random.c"

/* The linear congruential generator, restated from its documented definition
 * rather than copied from the implementation: multiplier 1103515245, increment
 * 12345, modulus 2^31. These are the standard glibc constants.
 */
static unsigned long reference_lcg(unsigned long v)
{
    return ((v * 1103515245UL) + 12345UL) & 0x7FFFFFFFUL;
}

/* True if the array holds each of 0..n-1 exactly once. */
static int is_permutation(int const *array, int n)
{
    int seen[4];
    int i;
    for (i = 0 ; i < n ; ++i)
	seen[i] = 0;
    for (i = 0 ; i < n ; ++i) {
	if (array[i] < 0 || array[i] >= n)
	    return 0;
	if (seen[array[i]])
	    return 0;
	seen[array[i]] = 1;
    }
    return 1;
}

int main(void)
{
    prng gen, other;
    int i, n;
    int array[4];
    int seen3[3];
    int seen4[4];
    unsigned long v;

    tw_begin("random");
    /* See tw_expect_atleast() in tw_test.h. Raise this when cases are added;
     * lowering it to make a run pass is the failure it exists to catch. */
    tw_expect_atleast(7767);

    /* ---------------------------------------------------------------- */
    tw_case("the generator is the glibc LCG, not the one CHIPS.EXE uses");
    /* Walked from a known value rather than asserted about the source text, so
     * that this fails on a changed constant even if the source is reformatted.
     */
    v = 1;
    for (i = 0 ; i < 5 ; ++i)
	v = nextvalue(v);
    CHECK_INT(v, (long)reference_lcg(reference_lcg(reference_lcg(
		    reference_lcg(reference_lcg(1UL))))));
    /* The single most load-bearing property: the result never exceeds 31 bits,
     * which is what makes it identical on a 32-bit and a 64-bit build. An
     * unmasked LCG would agree with this one for a while and then diverge.
     */
    v = 0x7FFFFFFFUL;
    for (i = 0 ; i < 200 ; ++i) {
	v = nextvalue(v);
	CHECK_MSG(v <= 0x7FFFFFFFUL, "nextvalue produced %lu, past 2^31-1", v);
    }

    /* ---------------------------------------------------------------- */
    tw_case("restartprng masks the seed to 31 bits and detaches the sequence");
    restartprng(&gen, 0xFFFFFFFFUL);
    CHECK_INT(gen.initial, 0x7FFFFFFFL);
    CHECK_INT(gen.value, 0x7FFFFFFFL);
    CHECK_INT(gen.shared, FALSE);
    restartprng(&gen, 12345);
    CHECK_INT(gen.initial, 12345);
    CHECK_INT(getinitialseed(&gen), 12345);

    /* ---------------------------------------------------------------- */
    tw_case("random4 reproduces its recorded sequence (the replay guarantee)");
    {
	/* Captured from this implementation. Changing these numbers to make a
	 * failing build pass would be changing the recorded past. */
	static int const golden[12] = { 2,1,2,0,2,1,2,1,1,1,3,0 };
	restartprng(&gen, 12345);
	for (i = 0 ; i < 12 ; ++i)
	    CHECK_INT(random4(&gen), golden[i]);
    }
    {
	static unsigned long const goldenvalues[6] = {
	    0x53DC167EUL, 0x270427DFUL, 0x56651C2CUL,
	    0x0DAA96F5UL, 0x421F1C8AUL, 0x3EAD62FBUL
	};
	restartprng(&gen, 12345);
	for (i = 0 ; i < 6 ; ++i) {
	    random4(&gen);
	    CHECK_INT(gen.value, (long)goldenvalues[i]);
	}
    }

    /* ---------------------------------------------------------------- */
    tw_case("random4 returns only 0 through 3, taking the top two bits");
    restartprng(&gen, 1);
    for (i = 0 ; i < 2000 ; ++i) {
	n = random4(&gen);
	CHECK_MSG(n >= 0 && n <= 3, "random4 returned %d", n);
    }

    /* ---------------------------------------------------------------- */
    tw_case("random4 consumes exactly one value per call");
    /* If a call ever drew twice, every solution recorded before the change
     * would desynchronize on the first creature that used the generator. */
    restartprng(&gen, 4242);
    restartprng(&other, 4242);
    random4(&gen);
    nextrandom(&other);
    CHECK_INT(gen.value, (long)other.value);

    /* ---------------------------------------------------------------- */
    tw_case("randomof3 returns one of its three arguments, and can return each");
    restartprng(&gen, 7);
    seen3[0] = seen3[1] = seen3[2] = 0;
    for (i = 0 ; i < 2000 ; ++i) {
	n = randomof3(&gen, 10, 20, 30);
	CHECK_MSG(n == 10 || n == 20 || n == 30, "randomof3 returned %d", n);
	if (n == 10) seen3[0] = 1;
	if (n == 20) seen3[1] = 1;
	if (n == 30) seen3[2] = 1;
    }
    CHECK_MSG(seen3[0] && seen3[1] && seen3[2],
	      "randomof3 never returned one of its arguments over 2000 draws");
    {
	static int const golden[12] = { 1,2,2,2,3,1,3,3,2,2,3,2 };
	restartprng(&gen, 7);
	for (i = 0 ; i < 12 ; ++i)
	    CHECK_INT(randomof3(&gen, 1, 2, 3), golden[i]);
    }

    /* ---------------------------------------------------------------- */
    tw_case("randomp3 permutes, never loses or duplicates an element");
    restartprng(&gen, 99);
    for (i = 0 ; i < 500 ; ++i) {
	array[0] = 0; array[1] = 1; array[2] = 2;
	randomp3(&gen, array);
	CHECK_MSG(is_permutation(array, 3), "randomp3 produced [%d %d %d]",
		  array[0], array[1], array[2]);
    }
    {
	/* Golden, because the ORDER is what a replay depends on -- an
	 * implementation that shuffled correctly but differently would pass
	 * every property check above and still break every stored solution. */
	static char const *const golden[4] = { "012", "120", "021", "021" };
	char got[4];
	restartprng(&gen, 99);
	for (i = 0 ; i < 4 ; ++i) {
	    array[0] = 0; array[1] = 1; array[2] = 2;
	    randomp3(&gen, array);
	    got[0] = (char)('0' + array[0]);
	    got[1] = (char)('0' + array[1]);
	    got[2] = (char)('0' + array[2]);
	    got[3] = '\0';
	    CHECK_STR(got, golden[i]);
	}
    }

    /* ---------------------------------------------------------------- */
    tw_case("randomp3 draws exactly once, deriving both swaps from one value");
    restartprng(&gen, 555);
    restartprng(&other, 555);
    array[0] = 0; array[1] = 1; array[2] = 2;
    randomp3(&gen, array);
    nextrandom(&other);
    CHECK_INT(gen.value, (long)other.value);

    /* ---------------------------------------------------------------- */
    tw_case("randomp4 permutes, and reaches every position");
    restartprng(&gen, 3);
    seen4[0] = seen4[1] = seen4[2] = seen4[3] = 0;
    for (i = 0 ; i < 2000 ; ++i) {
	array[0] = 0; array[1] = 1; array[2] = 2; array[3] = 3;
	randomp4(&gen, array);
	CHECK_MSG(is_permutation(array, 4), "randomp4 produced [%d %d %d %d]",
		  array[0], array[1], array[2], array[3]);
	seen4[array[0]] = 1;
    }
    CHECK_MSG(seen4[0] && seen4[1] && seen4[2] && seen4[3],
	      "randomp4 never put some element first over 2000 shuffles");

    /* ---------------------------------------------------------------- */
    tw_case("randomp4 reproduces its recorded order, from one draw");
    {
	static char const *const golden[4] = { "0132", "1302", "0312", "2130" };
	char got[5];
	restartprng(&gen, 99);
	for (i = 0 ; i < 4 ; ++i) {
	    array[0] = 0; array[1] = 1; array[2] = 2; array[3] = 3;
	    randomp4(&gen, array);
	    got[0] = (char)('0' + array[0]);
	    got[1] = (char)('0' + array[1]);
	    got[2] = (char)('0' + array[2]);
	    got[3] = (char)('0' + array[3]);
	    got[4] = '\0';
	    CHECK_STR(got, golden[i]);
	}
    }
    restartprng(&gen, 555);
    restartprng(&other, 555);
    array[0] = 0; array[1] = 1; array[2] = 2; array[3] = 3;
    randomp4(&gen, array);
    nextrandom(&other);
    CHECK_INT(gen.value, (long)other.value);

    /* ---------------------------------------------------------------- */
    tw_case("two generators on the same seed are identical, forever");
    restartprng(&gen, 20260903);
    restartprng(&other, 20260903);
    for (i = 0 ; i < 1000 ; ++i)
	CHECK_INT(random4(&gen), random4(&other));

    /* ---------------------------------------------------------------- */
    tw_case("SHARED generators draw from one common sequence, in turn");
    {
	/* 🔴 THE WHOLE SHARED MECHANISM WAS UNTESTED. Every other case here uses
	 * restartprng(), which sets shared = FALSE -- so deleting the
	 * `if (gen->shared)` branch from nextrandom() entirely, and letting every
	 * generator advance privately, left all 7,752 checks passing. Measured.
	 *
	 * The shared sequence is what makes two creatures drawing in the same
	 * tick get DIFFERENT numbers, in an order that a replay reproduces. If
	 * they silently each ran their own sequence, every level with more than
	 * one random creature would desynchronize.
	 *
	 * lastvalue is reachable because this file compiles random.c in. Setting
	 * it to a known 31-bit value first makes the case deterministic; left
	 * alone, the first createprng() seeds from the clock. */
	prng a, b;
	unsigned long start = 0x0BADF00DUL & 0x7FFFFFFFUL;
	lastvalue = start;
	a = createprng();
	b = createprng();
	CHECK_INT(a.shared, TRUE);
	CHECK_INT(b.shared, TRUE);
	CHECK_INT(a.value, (long)start);
	CHECK_INT(b.value, (long)start);

	random4(&a);
	CHECK_INT(a.value, (long)reference_lcg(start));
	random4(&b);
	/* The load-bearing assertion: b continues from where a left off, NOT
	 * from where b started. A private-sequence implementation gives
	 * reference_lcg(start) here for both. */
	CHECK_MSG(b.value == reference_lcg(a.value),
		  "the second shared generator restarted its own sequence: got %lu, wanted %lu",
		  b.value, reference_lcg(a.value));
	CHECK_INT(lastvalue, (long)b.value);
    }

    /* ---------------------------------------------------------------- */
    tw_case("resetprng warms the clock seed up before using it");
    {
	/* resetprng() reseeds from time(NULL) and then applies nextvalue FOUR
	 * times, discarding the results, "to work out any biases in the seed
	 * value" -- a fresh recording's whole sequence hangs off that. Replacing
	 * those four applications with a plain `time(NULL) & 0x7FFFFFFF` also
	 * left every check passing, because nothing asserted anything about the
	 * seeding path at all.
	 *
	 * The clock makes an exact expectation impossible, so the value is
	 * checked against the warm-up applied to any second in the window this
	 * case spans -- which is deterministic enough to bite, since the mutant
	 * produces the raw seed rather than its fourth iterate. */
	prng gen;
	unsigned long before, after, t;
	int matched = 0;

	before = (unsigned long)time(NULL);
	lastvalue = 0x80000000UL;   /* out of range, so the warm-up branch runs */
	resetprng(&gen);
	after = (unsigned long)time(NULL);

	CHECK_INT(gen.shared, TRUE);
	CHECK_INT(gen.initial, (long)gen.value);
	CHECK_MSG(gen.value <= 0x7FFFFFFFUL,
		  "resetprng produced %lu, past 2^31-1", gen.value);
	for (t = before ; t <= after + 1 ; ++t) {
	    if (gen.value == reference_lcg(reference_lcg(reference_lcg(reference_lcg(t))))) {
		matched = 1;
		break;
	    }
	}
	CHECK_MSG(matched,
		  "resetprng's value is not the four-times-iterated clock seed"
		  " (got %lu; the warm-up may have been removed)", gen.value);
    }

    /* ---------------------------------------------------------------- */
    tw_case("resetprng leaves an in-range lastvalue alone");
    {
	/* The other half of the branch: once lastvalue is inside 31 bits -- which
	 * it is after the very first reset -- resetprng must adopt it as-is and
	 * NOT reseed from the clock, or a second generator created mid-game would
	 * jump to an unrelated point in the sequence. */
	prng gen;
	unsigned long known = 0x2468ACEUL;
	lastvalue = known;
	resetprng(&gen);
	CHECK_INT(gen.value, (long)known);
	CHECK_INT(gen.initial, (long)known);
	CHECK_INT(gen.shared, TRUE);
	CHECK_INT(lastvalue, (long)known);
    }

    /* ---------------------------------------------------------------- */
    tw_case("an independent generator is not disturbed by a shared one");
    /* restartprng() clears the shared flag, so the level's own generator must
     * not move when the shared sequence advances underneath it. Chip's own
     * random-force-floor draws and a level's blob draws depend on this
     * separation; jc-13 and jc-14 were both about drawing the wrong number of
     * times from these sequences. */
    restartprng(&gen, 8080);
    v = gen.value;
    {
	prng shared = createprng();
	CHECK_INT(shared.shared, TRUE);
	for (i = 0 ; i < 50 ; ++i)
	    random4(&shared);
    }
    CHECK_INT(gen.value, (long)v);
    CHECK_INT(random4(&gen), (long)(reference_lcg(v) >> 29));

    return tw_end();
}
