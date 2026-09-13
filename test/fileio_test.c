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
    tw_expect_atleast(32);

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

    return tw_end();
}
