/* fileio_test.c: the path arithmetic.
 *
 * MOD (Jeremy). fileio.c is compiled into FIVE other unit tests -- play, res,
 * series, solution and unslist -- and was tested by none of them. That is the
 * shape that produces a low kill rate without anybody noticing: every line of
 * combinepath() is executed on the way to somewhere else, so coverage reports it
 * as reached, while no assertion anywhere depends on what it returned.
 *
 * Measured before this file existed: a mutation census killed 15 of fileio.c's
 * 41 mutants, and every one of combinepath()'s five bounds survived. They are
 * the bounds on a PATH_MAX buffer, assembled from a directory name and a
 * filename that come from a .dac file, a command line, or a settings file.
 *
 * WHAT IS HERE. The pure path functions only -- combinepath(), skippathname()
 * and isreservedfilename(). The byte I/O and directory-walking halves of
 * fileio.c need a filesystem and are exercised by the tests that already
 * compile this file; nothing here duplicates them.
 *
 * TESTLANG: c
 *
 * fileio.c is compiled only as C by CMake, and relies on C's implicit void*
 * conversion through err.h's x_alloc. See docs/adr/0004.
 */

#include	"tw_test.h"

/* The two directory globals fileio.c's callers keep in res.c and tworld.c.
 * Nothing here reads them; they exist so the translation unit links. */
char	       *resdir = NULL;
char	       *savedir = NULL;

#include	"../fileio.c"

/* --- the error surface, stubbed ---------------------------------------- */

char const     *err_cfile_ = 0;
unsigned long	err_lineno_ = 0;

static int	warn_count = 0;
static int	errmsg_count = 0;

void warn_(char const *fmt, ...) { (void)fmt; ++warn_count; }
void errmsg_(char const *pfx, char const *fmt, ...)
{
    (void)pfx; (void)fmt; ++errmsg_count;
}
void die_(char const *fmt, ...) { (void)fmt; exit(1); }

/* A directory name of exactly n characters, in a buffer with room to spare. */
static void makedir(char *buf, int n)
{
    memset(buf, 'd', (size_t)n);
    buf[n] = '\0';
}

int main(void)
{
    static char	dest[PATH_MAX * 4];
    static char	dir[PATH_MAX * 4];
    char	path[64];
    int		n;

    tw_begin("fileio");
    /* 🔴 THE BUFFER CONVENTION, which the header got wrong by one until
     * 2026-09-15 and which nothing pinned.
     *
     * getpathbufferlen() is a LENGTH LIMIT (PATH_MAX). getpathbuffer() hands
     * out one MORE byte than that, and combinepath() relies on it: an absolute
     * path of exactly the limit is accepted and written WITH its terminator.
     * The header used to promise a buffer "of size getpathbufferlen()", which a
     * new caller could have believed and been overrun by one byte.
     *
     * ⚠ The fix is NOT to widen getpathbufferlen(). tworld.c formats save-file
     * names with sprintf("%.*s", getpathbufferlen(), ...), which writes that
     * many characters plus a terminator -- widen the limit and that becomes the
     * overflow this case exists to prevent. The limit is pinned here; the
     * buffer's extra byte is pinned by the Linux ASan job, not by an assertion:
     * the first case below writes lim + 1 bytes into a getpathbuffer(), so
     * shrinking that allocation by one is a heap-buffer-overflow report there.
     * ⚠ No plain assertion can see an allocation's size -- an earlier draft of
     * this case "checked" it by re-deriving strlen, which cannot fail.
     */
    tw_case("🔴 an absolute path of EXACTLY the length limit is accepted");
    {
	int const lim = getpathbufferlen();
	char *buf = getpathbuffer();
	char *absolute = malloc((size_t)lim + 2);
	int i;

	CHECK_MSG(buf != NULL && absolute != NULL, "allocation failed");
	absolute[0] = DIRSEP_CHAR;
	for (i = 1 ; i < lim ; ++i) absolute[i] = 'a';
	absolute[lim] = '\0';
	CHECK_INT((int)strlen(absolute), lim);

	CHECK_MSG(combinepath(buf, "", absolute) == TRUE,
		  "an absolute path of exactly getpathbufferlen() (%d) characters was"
		  " refused; the bound moved", lim);
	CHECK_INT((int)strlen(buf), lim);

	tw_case("...and one character MORE than the limit is refused");
	absolute[lim] = 'a';
	absolute[lim + 1] = '\0';
	CHECK_MSG(combinepath(buf, "", absolute) == FALSE,
		  "an absolute path of %d characters was accepted; that overruns even a"
		  " getpathbuffer()", lim + 1);

	free(absolute);
	free(buf);
    }

    tw_expect_atleast(51);

    tw_case("an absolute path ignores the directory entirely");
    {
	/* `if (path[0] == DIRSEP_CHAR)` is the whole absolute-path branch.
	 * Invert it and an absolute path gets the directory prepended, which on
	 * Windows produces a path that silently resolves somewhere else. */
	sprintf(path, "%cabsolute%cthing", DIRSEP_CHAR, DIRSEP_CHAR);
	CHECK_INT(combinepath(dest, "some_dir", path), TRUE);
	CHECK_STR(dest, path);
    }

    tw_case("a relative path is appended to the directory");
    {
	strcpy(path, "file.dat");
	CHECK_INT(combinepath(dest, "setdir", path), TRUE);
	CHECK_MSG(strncmp(dest, "setdir", 6) == 0,
		  "the directory was dropped from a relative path: got '%.80s'", dest);
	CHECK_MSG(dest[6] == DIRSEP_CHAR,
		  "no separator between directory and file: got '%.80s'", dest);
	CHECK_STR(dest + 7, "file.dat");
    }

    tw_case("🔴 a directory already ending in a separator does not get a second");
    {
	/* `if (dest[n - 1] != DIRSEP_CHAR) dest[n++] = DIRSEP_CHAR;` -- invert
	 * it and every path gains a doubled separator, or loses the one it
	 * needed. Both spellings open the wrong file. */
	sprintf(dir, "setdir%c", DIRSEP_CHAR);
	strcpy(path, "file.dat");
	CHECK_INT(combinepath(dest, dir, path), TRUE);
	CHECK_MSG(strstr(dest, "  ") == NULL, "unexpected padding in '%.80s'", dest);
	sprintf(path, "setdir%cfile.dat", DIRSEP_CHAR);
	CHECK_STR(dest, path);
    }

    tw_case("🔴 combining in place, with dest and dir the same buffer");
    {
	/* `if (dest != dir) memcpy(dest, dir, n);` guards a self-copy. Invert it
	 * and the copy is SKIPPED in the ordinary two-buffer case, so dest holds
	 * whatever it held before and the directory is lost.
	 *
	 * Both directions matter, so both are here: the case above proves the
	 * copy happens when the buffers differ, and this one proves the function
	 * still works when they do not. */
	sprintf(dir, "setdir");
	strcpy(path, "file.dat");
	CHECK_INT(combinepath(dir, dir, path), TRUE);
	sprintf(dest, "setdir%cfile.dat", DIRSEP_CHAR);
	CHECK_STR(dir, dest);
    }

    tw_case("🔴 a directory of exactly PATH_MAX is refused");
    {
	/* `if (n >= PATH_MAX)`, where PATH_MAX is the only length that separates
	 * `>=` from `>`.
	 *
	 * ⚠ THE RETURN VALUE IS NOT THE ORACLE HERE. Both forms return FALSE for
	 * a directory this long -- the relaxed one just gets as far as the
	 * combined-length check a few lines down and fails there instead. What
	 * differs is that it copies the whole directory into dest and appends a
	 * separator FIRST. So the oracle is dest: an input that is going to be
	 * refused must not be half-assembled into the caller's buffer on the way
	 * out. Poisoning dest is the only way to see it. */
	makedir(dir, PATH_MAX);
	strcpy(path, "f");
	memset(dest, '#', 8);
	dest[8] = '\0';
	CHECK_MSG(combinepath(dest, dir, path) == FALSE,
		  "a directory of exactly PATH_MAX characters was accepted");
	CHECK_INT(errno, ENAMETOOLONG);
	CHECK_MSG(dest[0] == '#' && dest[7] == '#',
		  "an over-long directory was copied into the destination buffer"
		  " before being refused");
    }

    tw_case("🔴 a combined path of exactly PATH_MAX is accepted, one more is not");
    {
	/* `if (m + n + 1 > PATH_MAX)`. The exact fit is the only input that
	 * tells `>` from `>=`, and refusing it rejects legitimate level sets
	 * rather than crashing -- the quiet half of an off-by-one. */
	strcpy(path, "file.dat");
	n = (int)strlen(path);

	/* The check runs AFTER the separator has been appended, so with a
	 * directory of d characters it reads m + (d + 1) + 1 > PATH_MAX. The
	 * exact fit -- the only input that tells `>` from `>=` -- is therefore
	 * d == PATH_MAX - m - 2. */
	makedir(dir, PATH_MAX - n - 2);
	CHECK_MSG(combinepath(dest, dir, path) == TRUE,
		  "a combined path that exactly fits PATH_MAX was refused");
	CHECK_INT((int)strlen(dest), PATH_MAX - 1);

	makedir(dir, PATH_MAX - n - 1);
	CHECK_MSG(combinepath(dest, dir, path) == FALSE,
		  "a combined path one character too long was accepted");
	CHECK_INT(errno, ENAMETOOLONG);
    }

    tw_case("🔴 openfileindir refuses a joined path one byte too long for its STACK buffer");
    {
	/* openfileindir() joins into `char buf[PATH_MAX + 1]`: the directory,
	 * a separator, the name and a terminator, m + n + 2 bytes. So
	 * `m + n + 1 > PATH_MAX` is exactly the bound, and one byte looser writes
	 * one byte past a stack array. combinepath() had its exact-fit case
	 * above; this twin had none, and an adversarial audit loosened it with
	 * every layer green -- UBSan included, because the overflowing write is a
	 * memcpy and -fsanitize=bounds instruments indexed accesses, not library
	 * calls. (It is NOT that UBSan ignores arrays: it catches shrinking
	 * state.h's traps[256], measured. See openfileindir()'s own comment, where
	 * a _Static_assert now ties the buffer's size to the guard.)
	 *
	 * Both forms fail to open a file that does not exist, so the return value
	 * is not the oracle. Neither is errno, as it turned out: a review caught
	 * that on Linux the OPERATING SYSTEM refuses a path of PATH_MAX characters
	 * with ENAMETOOLONG too (its limit counts the terminator), so at exactly
	 * this length the kernel and the guard give the same answer there.
	 *
	 * The oracle is whether the joined path ever reached fileopen(), which
	 * copies it into file->name before calling fopen(). Refused by the guard,
	 * name stays NULL; past the guard, name holds the joined path -- on every
	 * platform, whatever fopen() then says. */
	fileinfo	opened;

	strcpy(path, "file.dat");
	n = (int)strlen(path);

	makedir(dir, PATH_MAX - n);        /* n + D + 1 == PATH_MAX + 1 */
	clearfileinfo(&opened);
	errno = 0;
	CHECK_MSG(openfileindir(&opened, dir, path, "rb", NULL) == FALSE,
		  "a joined path one byte too long was opened");
	CHECK_MSG(opened.name == NULL,
		  "a joined path one byte too long for the stack buffer was assembled"
		  " and handed to fileopen() -- the copy into buf[] overran it first");
	CHECK_INT(errno, ENAMETOOLONG);
	if (opened.name && opened.alloc) free(opened.name);

	/* The exact fit gets PAST the guard -- and then fails for the ordinary
	 * reason, since no such file exists -- so the case above is about the
	 * bound and not a guard that refuses every long path. */
	makedir(dir, PATH_MAX - n - 1);    /* n + D + 1 == PATH_MAX */
	clearfileinfo(&opened);
	CHECK_MSG(openfileindir(&opened, dir, path, "rb", NULL) == FALSE,
		  "a file that does not exist was opened");
	CHECK_MSG(opened.name != NULL && (int)strlen(opened.name) == PATH_MAX,
		  "a joined path that exactly fits the buffer never reached fileopen()"
		  " (name %s) -- the guard refused it", opened.name ? "set, wrong length" : "NULL");
	if (opened.name && opened.alloc) free(opened.name);
    }

    tw_case("skippathname returns the file part, or the whole name");
    {
	sprintf(dir, "a%cb%cc.dat", DIRSEP_CHAR, DIRSEP_CHAR);
	CHECK_STR(skippathname(dir), "c.dat");
	CHECK_STR(skippathname("plain.dat"), "plain.dat");
	CHECK_STR(skippathname(""), "");
    }

    tw_case("the reserved device names are recognized, case-insensitively");
    {
	CHECK_INT(isreservedfilename("CON"), TRUE);
	CHECK_INT(isreservedfilename("con"), TRUE);
	CHECK_INT(isreservedfilename("NUL.dat"), TRUE);
	CHECK_INT(isreservedfilename("com9"), TRUE);
	CHECK_INT(isreservedfilename("levels.dat"), FALSE);
	CHECK_INT(isreservedfilename(""), FALSE);
    }

    tw_case("🔴 a base name that exactly fills the scratch buffer is refused");
    {
	/* `for (n = 0 ; name[n] && name[n] != '.' ; ++n) if (n >= sizeof base - 1)`
	 * over `char base[16]`.
	 *
	 * ⚠ THE RETURN VALUE CANNOT SEE THIS BOUND, and that is worth saying
	 * plainly rather than leaving the case looking weak. Both forms answer
	 * FALSE for every name, because no reserved device name is anywhere near
	 * this long. What differs is memory: relax `>=` to `>` and a base of
	 * EXACTLY SIXTEEN characters gets base[15] written inside the loop, then
	 * falls out and writes base[16] = '\0' -- one past a sixteen-byte array.
	 *
	 * 🔴 SIXTEEN, NOT FIFTEEN, AND THE DIFFERENCE IS THE WHOLE CASE. A
	 * fifteen-character base never reaches the bound at all: the loop ends on
	 * the terminator first, both forms write base[15] = '\0' in bounds, and
	 * nothing happens. Measured -- with fifteen here, the mutant survived the
	 * sanitize layer too, which reads exactly like "the sanitizer cannot see
	 * it" and is really "the input never drove it".
	 *
	 * 🔴 THE ORACLE IS THE SANITIZE LAYER, NOT THIS ASSERTION. What this case
	 * contributes is the INPUT. "Not detected" and "not exercised" are
	 * different diagnoses; this fixes the second, and run-tests.ps1 -Sanitize
	 * supplies the first. */
	CHECK_INT(isreservedfilename("abcdefghijklmnop"), FALSE);     /* 16 */
	CHECK_INT(isreservedfilename("abcdefghijklmnop.dat"), FALSE); /* 16 + ext */
	CHECK_INT(isreservedfilename("abcdefghijklmno"), FALSE);      /* 15 */
	CHECK_INT(isreservedfilename("abcdefghijklmn"), FALSE);       /* 14 */
	CHECK_INT(isreservedfilename("abcdefghijklmnopqrstuvwxyz"), FALSE);
    }

    tw_case("🔴 getpathforfileindir's two length bounds, at the exact byte");
    {
	/* combinepath() joins into a buffer the CALLER owns; this function
	 * allocates its own with getpathbuffer(), which is PATH_MAX + 1 bytes,
	 * and it had no case of any kind. Both of its bounds sit exactly where
	 * that extra byte runs out, and all four one-off twins survived every
	 * layer -- so a guard on a name out of a .dac or a command line was
	 * being carried by nothing.
	 *
	 * ⭐ Unlike most of this file, the RETURN VALUE is the oracle here: one
	 * direction refuses a name that fits (a level set that will not open),
	 * the other accepts one that does not and overruns the allocation by a
	 * byte. Both are visible without a sanitizer, which is why this case is
	 * short where the ones above are long.
	 *
	 *   no directory:  strcpy() writes m + 1, so m == PATH_MAX exactly fits
	 *   a directory:   n + 1 + m + 1 bytes, so m + n + 1 == PATH_MAX fits */
	int const lim = getpathbufferlen();
	char *name = malloc((size_t)lim + 4);
	char *got;
	int i;

	CHECK_MSG(name != NULL, "allocation failed");
	for (i = 0 ; i < lim + 3 ; ++i)
	    name[i] = 'n';

	name[lim] = '\0';				/* exactly PATH_MAX */
	got = getpathforfileindir(NULL, name);
	CHECK_MSG(got != NULL,
		  "a %d-character filename with no directory was refused;"
		  " it fits a getpathbuffer() exactly", lim);
	if (got) {
	    CHECK_INT((int)strlen(got), lim);
	    free(got);
	}

	name[lim] = 'n';				/* one more */
	name[lim + 1] = '\0';
	errno = 0;		/* or a leftover 38 would pass the check below */
	got = getpathforfileindir(NULL, name);
	CHECK_MSG(got == NULL,
		  "a %d-character filename was accepted; it overruns a"
		  " getpathbuffer() by one byte", lim + 1);
	CHECK_INT(errno, ENAMETOOLONG);
	free(got);

	/* With a directory the separator and the terminator both count. A
	 * four-character directory leaves lim - 5 for the name. */
	strcpy(dir, "dirn");
	n = (int)strlen(dir);
	name[lim - n - 1] = '\0';
	got = getpathforfileindir(dir, name);
	CHECK_MSG(got != NULL,
		  "a directory of %d and a name of %d -- exactly PATH_MAX"
		  " joined -- was refused", n, lim - n - 1);
	if (got) {
	    CHECK_INT((int)strlen(got), lim);
	    free(got);
	}

	name[lim - n - 1] = 'n';			/* one more */
	name[lim - n] = '\0';
	errno = 0;
	got = getpathforfileindir(dir, name);
	CHECK_MSG(got == NULL,
		  "a joined path of %d characters was accepted; a"
		  " getpathbuffer() holds %d plus a terminator", lim + 1, lim);
	CHECK_INT(errno, ENAMETOOLONG);
	free(got);

	free(name);
    }

    return tw_end();
}
