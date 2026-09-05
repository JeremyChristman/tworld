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
 * 🔴 BE PRECISE ABOUT WHAT A GREEN REPLAY PROVES, BECAUSE THE FIRST VERSION OF
 * THIS FILE WAS NOT.
 *
 * It proves TWO things, and they are narrower than they sound:
 *
 *   1. Every committed input still parses to completion without crashing,
 *      hanging or aborting. That is a real regression check and it is most of
 *      the value -- a reproducer that used to segfault does not any more.
 *   2. The parser did not MODIFY its own input (see the pristine-copy check
 *      below). Every parser targeted today is a read-only walker, so this
 *      currently passes trivially; it is here to fail loudly the day one of
 *      them starts decoding in place, which would invalidate every caller that
 *      hands it a shared or mapped buffer.
 *
 * It is NOT a memory oracle on its own. An over-read or an over-write past the
 * allocation is invisible to plain C. The memory oracle is ASan, in
 * run-sanitizers.sh and in the fuzz job -- and the allocation below is shaped
 * to help it rather than get in its way.
 *
 * ⚠ AN EARLIER VERSION PUT 64 POISON BYTES ON EACH SIDE OF THE INPUT and
 * claimed that caught over-writes without a sanitizer. Measured, that claim was
 * empty twice over: none of the parsers writes to its input at all, so
 * the fences could never fire; and worse, those 64 bytes were legally
 * allocated, so under ASan they sat exactly where the redzone should be and
 * blunted it -- jc-44's two-byte over-read would have landed in the fence and
 * gone unreported. The buffer is now sized EXACTLY to the input so ASan's
 * redzone begins at the first byte past the end, which is the same reasoning
 * the fuzz targets already documented.
 */

#ifndef TW_CORPUS_H_
#define TW_CORPUS_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

/* mingw-w64 ships dirent.h and sys/stat.h, so this is portable across both
 * toolchains the suite is built with. mingw's struct dirent has NO d_type,
 * which is why directories are filtered with stat() below rather than the
 * cheaper field. */

#define TW_CORPUS_MAXFILE (1 << 20)

/* Why one input's replay did not come back clean. The callback gets this rather
 * than a bare int, because an unreadable file and a parser that scribbled on
 * its input are different problems and reporting both as the same thing sends
 * the next reader somewhere useless. The first version of this file did exactly
 * that -- an unreadable file was announced as memory corruption. */
typedef enum twcorpusverdict {
    TW_CORPUS_OK = 0,
    TW_CORPUS_MODIFIED,		/* the parser wrote into its own input */
    TW_CORPUS_UNREADABLE,	/* present, but could not be read */
    TW_CORPUS_EMPTY,		/* zero bytes -- corpus rot, see below */
    TW_CORPUS_TOOBIG		/* over TW_CORPUS_MAXFILE */
} twcorpusverdict;

static char const *tw_corpus_why(twcorpusverdict v)
{
    switch (v) {
      case TW_CORPUS_OK:	 return "clean";
      case TW_CORPUS_MODIFIED:	 return "the parser MODIFIED its own input buffer";
      case TW_CORPUS_UNREADABLE: return "present but unreadable";
      case TW_CORPUS_EMPTY:	 return "zero bytes -- an empty file replaces a"
				        " real input while still being counted";
      case TW_CORPUS_TOOBIG:	 return "larger than the 1 MB corpus limit";
    }
    return "unknown";
}

typedef struct twcorpusinput {
    unsigned char      *data;	   /* the input, in an EXACTLY-sized allocation */
    int			size;
    char const	       *name;	   /* the file it came from, for messages */
    unsigned char      *ref_;	   /* private: pristine copy, for the no-write check */
} twcorpusinput;

/* Read one corpus file. Returns the verdict; only TW_CORPUS_OK leaves `in`
 * usable, and the caller must treat everything else as a failure rather than a
 * skip.
 *
 * A ZERO-BYTE FILE IS A FAILURE, not an input. It parses trivially, it counts
 * toward the file total, and it satisfies tw_expect_atleast -- so truncating a
 * reproducer to nothing would silently remove the coverage it existed for while
 * every counter still agreed. That is the precise way a regression corpus rots.
 */
static twcorpusverdict tw_corpus_load(char const *path, char const *name,
				      twcorpusinput *in)
{
    FILE       *f;
    long	len;
    size_t	got;

    in->data = NULL;
    in->ref_ = NULL;
    in->size = 0;
    in->name = name;

    f = fopen(path, "rb");
    if (!f)
	return TW_CORPUS_UNREADABLE;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return TW_CORPUS_UNREADABLE; }
    len = ftell(f);
    if (len < 0)		 { fclose(f); return TW_CORPUS_UNREADABLE; }
    if (len == 0)		 { fclose(f); return TW_CORPUS_EMPTY; }
    if (len > TW_CORPUS_MAXFILE) { fclose(f); return TW_CORPUS_TOOBIG; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return TW_CORPUS_UNREADABLE; }

    /* EXACTLY len bytes, no slack. See the header: this is what lets ASan put
     * its redzone immediately after the last byte the parser may legally read. */
    in->data = (unsigned char *)malloc((size_t)len);
    in->ref_ = (unsigned char *)malloc((size_t)len);
    if (!in->data || !in->ref_) {
	free(in->data); free(in->ref_);
	in->data = NULL; in->ref_ = NULL;
	fclose(f);
	return TW_CORPUS_UNREADABLE;
    }

    got = fread(in->data, 1, (size_t)len, f);
    fclose(f);
    if (got != (size_t)len) {
	free(in->data); free(in->ref_);
	in->data = NULL; in->ref_ = NULL;
	return TW_CORPUS_UNREADABLE;
    }
    memcpy(in->ref_, in->data, (size_t)len);

    in->size = (int)len;
    return TW_CORPUS_OK;
}

/* TW_CORPUS_OK if the parser left its input byte-for-byte as it found it. */
static twcorpusverdict tw_corpus_unmodified(twcorpusinput const *in)
{
    if (!in->data || !in->ref_)
	return TW_CORPUS_UNREADABLE;
    return memcmp(in->data, in->ref_, (size_t)in->size) == 0
		? TW_CORPUS_OK : TW_CORPUS_MODIFIED;
}

static void tw_corpus_free(twcorpusinput *in)
{
    free(in->data);
    free(in->ref_);
    in->data = NULL;
    in->ref_ = NULL;
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

/* Walk every regular FILE in `dir`, hand each to `fn`, and return how many were
 * replayed. The no-write check runs here, after `fn` returns, so a target
 * cannot forget it.
 *
 * Subdirectories are skipped and NOT counted. run-fuzz.sh already uses
 * `find -type f`; without the matching filter here, someone organizing the
 * corpus into a `crashes/` folder got two spurious failures announcing memory
 * corruption. mingw's struct dirent has no d_type, hence stat().
 *
 * Returns -1 if the directory could not be opened at all -- distinct from 0,
 * which means it opened and held nothing. Neither may be mistaken for "clean".
 */
static int tw_corpus_run(char const *dir,
			 void (*fn)(twcorpusinput const *),
			 void (*report)(twcorpusverdict v, char const *name))
{
    DIR		       *d;
    struct dirent      *ent;
    struct stat		st;
    char		path[512];
    twcorpusinput	in;
    twcorpusverdict	v;
    int			count = 0;

    d = opendir(dir);
    if (!d)
	return -1;
    while ((ent = readdir(d)) != NULL) {
	if (ent->d_name[0] == '.')
	    continue;
	if (snprintf(path, sizeof path, "%s/%s", dir, ent->d_name) >= (int)sizeof path) {
	    /* Not silently skipped: a name this long is a mistake worth seeing,
	     * and the header's whole doctrine is that the replay never passes
	     * over something quietly. */
	    if (report)
		report(TW_CORPUS_UNREADABLE, ent->d_name);
	    continue;
	}
	if (stat(path, &st) == 0 && (st.st_mode & S_IFMT) == S_IFDIR)
	    continue;

	v = tw_corpus_load(path, ent->d_name, &in);
	if (v != TW_CORPUS_OK) {
	    if (report)
		report(v, ent->d_name);
	    continue;
	}
	fn(&in);
	if (report)
	    report(tw_corpus_unmodified(&in), in.name);
	tw_corpus_free(&in);
	++count;
    }
    closedir(d);
    return count;
}

#endif
