/* fork.h: who this build is.
 *
 * MOD (Jeremy, jc-34): the fork's identity, in ONE place.
 *
 * These used to be scattered: the build tag was a C++ QString literal in oshw-qt/TWApp.cpp, the
 * repository URL was typed out again in help.c, and README.txt named the build in its header. A
 * release therefore meant remembering three or four edits, and the one that got forgotten was
 * never the loud one -- an About box quietly claiming the wrong build number looks exactly like a
 * correct one.
 *
 * Plain string macros rather than C++ constants on purpose: help.c is C and TWApp.cpp is C++, and
 * both need these. String-literal concatenation ("[" FORK_BUILD_TAG "]") also keeps them usable
 * inside QStringLiteral(), which requires a genuine literal at compile time.
 *
 * ⚠ package.ps1 reads FORK_BUILD_TAG out of THIS FILE to name the release zip, and then verifies
 * that the compiled executable really contains that tag. If the #define below is ever reformatted,
 * update the regular expression in package.ps1 to match -- otherwise packaging fails loudly, which
 * is the intended failure mode, but it will not be obvious why.
 */

#ifndef	HEADER_fork_h_
#define	HEADER_fork_h_

/* Bumped on every production deploy. Shown in the About box always, and in the window title only
 * when tw_settings.ini opts in with showbuildtag (see TWApp.cpp). */
#define	FORK_BUILD_TAG		"jc-49"

/* Credited in the About box and in README.txt. */
#define	FORK_AUTHOR		"Jeremy Christman"

/* Where this fork lives, and where its bugs go. Deliberately NOT the upstream tracker: the
 * maintainers of the original did not write these changes and should not be answering for them. */
#define	FORK_REPO_URL		"https://github.com/JeremyChristman/tworld"
#define	FORK_ISSUES_URL		FORK_REPO_URL "/issues"

/* The project this fork is built on. */
#define	FORK_UPSTREAM_URL	"https://github.com/SicklySilverMoon/tworld"

#endif
