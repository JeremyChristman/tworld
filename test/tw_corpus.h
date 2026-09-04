/* tw_corpus.h: replay a committed fuzz corpus through a parser entry point.
 *
 * MOD (Jeremy). Header-only, C and C++, no dependency on the fuzzer.
 *
 * WHY THIS EXISTS -- and it is the point of the whole fuzzing exercise.
 *
 * A fuzzer that finds a crash and then forgets it has bought nothing. libFuzzer
 * runs on Linux, in CI, on inputs it generates fresh each time; it is a
 * DISCOVERY tool and it is not reproducible. So every input that ever mattered
 * -- a seed, or a crash the fuzzer found -- is committed under
 * test/fuzz/corpus/<target>/ and replayed HERE, by the ordinary unit suite,
 * deterministically, on every platform, with no clang and no sanitizer needed.
 *
 * That is what turns a finding into a regression test. The fuzzer finds it
 * once; this makes sure it stays found.
 *
 * 🔴 THE GUARD BYTES ARE WHAT MAKE THIS A REAL ORACLE ON WINDOWS.
 *
 * Without a sanitizer, "the parser returned" asserts nothing -- a parser that
 * ran off the end of its input and came back is indistinguishable from one that
 * did not. So the input is copied into the middle of a larger allocation with
 * 64 poison bytes on each side, and those bytes are checked afterward. An
 * over-READ still goes unseen without ASan, but every over-WRITE within 64
 * bytes of either end is caught, on the maintainer's machine, with no
 * special toolchain. That is exactly the shape of jc-44's stack smash.
 *
 * The allocation is also sized EXACTLY to the input, so a heap sanitizer has a
 * tight boundary to work with rather than slack left by a fixed buffer.
 */

#ifndef TW_CORPUS_H_
#define TW_CORPUS_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>

/* mingw-w64 ships dirent.h, so this is portable across both toolchains the
 * suite is built with. If that ever stops being true, the corpus replay must
 * fail loudly rather than silently replaying nothing -- see tw_corpus_run(). */

#define TW_CORPUS_FENCE   64
#define TW_CORPUS_POISON  0x5A
#define TW_CORPUS_MAXFILE (1 << 20)

typedef struct twcorpusinput {
    unsigned char      *data;      /* the input itself, exactly size bytes */
    int			size;
    char const	       *name;      /* the file it came from, for messages */
    unsigned char      *block_;    /* private: the fenced allocation */
} twcorpusinput;

/* Read one corpus file into a fenced allocation. Returns 0 if it could not be
 * read at all, which the caller must treat as a failure and not a skip.
 */
static int tw_corpus_load(char const *path, char const *name, twcorpusinput *in)
{
    FILE       *f;
    long	len;
    size_t	got;

    in->data = NULL;
    in->block_ = NULL;
    in->size = 0;
    in->name = name;

    f = fopen(path, "rb");
    if (!f)
	return 0;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return 0; }
    len = ftell(f);
    if (len < 0 || len > TW_CORPUS_MAXFILE) { fclose(f); return 0; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return 0; }

    in->block_ = (unsigned char *)malloc((size_t)len + 2 * TW_CORPUS_FENCE);
    if (!in->block_) { fclose(f); return 0; }
    memset(in->block_, TW_CORPUS_POISON, (size_t)len + 2 * TW_CORPUS_FENCE);

    got = fread(in->block_ + TW_CORPUS_FENCE, 1, (size_t)len, f);
    fclose(f);
    if (got != (size_t)len) { free(in->block_); in->block_ = NULL; return 0; }

    in->data = in->block_ + TW_CORPUS_FENCE;
    in->size = (int)len;
    return 1;
}

/* Non-zero if both fences are still poison. Call after the parser has run. */
static int tw_corpus_fences_intact(twcorpusinput const *in)
{
    int i;

    if (!in->block_)
	return 0;
    for (i = 0 ; i < TW_CORPUS_FENCE ; ++i) {
	if (in->block_[i] != TW_CORPUS_POISON)
	    return 0;
	if (in->block_[TW_CORPUS_FENCE + in->size + i] != TW_CORPUS_POISON)
	    return 0;
    }
    return 1;
}

static void tw_corpus_free(twcorpusinput *in)
{
    free(in->block_);
    in->block_ = NULL;
    in->data = NULL;
    in->size = 0;
}

/* Resolve test/fuzz/corpus/<target> without depending on the working directory.
 *
 * test\run-tests.ps1 invokes each test binary with `& $exe` and never changes
 * directory, so cwd is whatever the caller had -- the repository root for every
 * current invocation (the runner, package.ps1, CI), but that is a convention
 * and not a guarantee. Rather than encode the convention, try the handful of
 * places the corpus can be relative to, and let the caller fail LOUDLY when
 * none of them exist. A corpus replay that silently finds no files is worse
 * than no replay at all: it reports success for work it did not do.
 */
static int tw_corpus_dir(char const *target, char *out, size_t outsize)
{
    static char const *bases[] = { "test/fuzz/corpus", "fuzz/corpus",
				   "../test/fuzz/corpus", "../../test/fuzz/corpus" };
    size_t	i;
    DIR	       *d;

    for (i = 0 ; i < sizeof bases / sizeof *bases ; ++i) {
	if (snprintf(out, outsize, "%s/%s", bases[i], target) >= (int)outsize)
	    continue;
	d = opendir(out);
	if (d) {
	    closedir(d);
	    return 1;
	}
    }
    out[0] = '\0';
    return 0;
}

/* Walk every regular file in `dir`, hand each to `fn`, and return how many were
 * replayed. `fn` receives the fenced input and must run the parser over it; the
 * fence check happens here, after it returns, so a target cannot forget it.
 *
 * Returns -1 if the directory could not be opened at all. The caller decides
 * whether that is a skip (running from an unexpected working directory) or a
 * failure, but it must never be mistaken for "the corpus was clean".
 */
static int tw_corpus_run(char const *dir,
			 void (*fn)(twcorpusinput const *),
			 int (*report)(int ok, char const *name))
{
    DIR		       *d;
    struct dirent      *ent;
    char		path[512];
    twcorpusinput	in;
    int			count = 0;

    d = opendir(dir);
    if (!d)
	return -1;
    while ((ent = readdir(d)) != NULL) {
	if (ent->d_name[0] == '.')
	    continue;
	if (snprintf(path, sizeof path, "%s/%s", dir, ent->d_name) >= (int)sizeof path)
	    continue;
	if (!tw_corpus_load(path, ent->d_name, &in)) {
	    /* A file that is present but unreadable is a failure, not a skip:
	     * silently replaying nothing is how a regression corpus rots. */
	    if (report)
		report(0, ent->d_name);
	    continue;
	}
	fn(&in);
	if (report)
	    report(tw_corpus_fences_intact(&in), in.name);
	tw_corpus_free(&in);
	++count;
    }
    closedir(d);
    return count;
}

#endif
