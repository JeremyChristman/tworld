# 0003 — Tests `#include` the source under test, instead of linking a build tree

**Status:** Accepted (jc-43 for the first two tests; generalized 2026-09-03) · **Applies to:** everything in `test/`

## Context

This is a 2001-vintage C codebase. Most of what is worth testing is `static`:

- `readpos()` in `encoding.c` is a macro.
- `getchip()`, `chippos()`, `lookupblock()`, `choosecreaturemove()` and the entire creature list in
  `mslogic.c` are file-scope statics.
- `readleveldata()` and `readconfigfile()` in `series.c` are static.
- `nextvalue()` and the shared `lastvalue` in `random.c` are static.

A test that links against a compiled object file can reach none of it. The alternatives were:

1. **Export things for testing.** Change shipped code to make it observable. On a fork tracking
   upstream, every such change is a permanent diff to carry, and it makes the fork's real changes
   harder to see.
2. **Test only through the public surface.** For `mslogic.c` that means a full GUI or a full batch
   verification over a private solution collection — which is how this engine was tested for its
   first forty-three builds, and is why nothing covered a single line of it in isolation.
3. **Compile the source into the test.**

There is also a practical constraint: the shipping build needs MSYS2 with a static Qt, which is a
large install. A contributor who can only run `gcc` should still be able to run the tests.

## Decision

**Each test is a single translation unit that `#include`s the source under test directly.**

```c
#include "tw_test.h"
#include "../random.c"
```

The test therefore sees exactly what the compiler sees when building the real program: the same
statics, the same macros, the same `#ifdef` configuration. `test/run-tests.ps1` compiles each test
with nothing but a compiler and `-I test/stub`, so **no CMake tree, no Qt, and no built executable
are required** for the unit layer.

Where a module reaches outside itself, the test stubs that surface explicitly — `mslogic.c` needs
only nine symbols beyond libc, and `warn_`/`errmsg_`/`die_` are three of them.

## Consequences

- **Static functions are testable, and shipped code needs no test-only exports.**
- **The test compiles in the same preprocessor world as the build, or it proves nothing.** This is
  the sharp edge: `test/run-tests.ps1` pins `-std=gnu11`/`-std=gnu++11` rather than `-std=c99`,
  because strict ANSI leaves `WIN32` undefined and `fileio.c:20` selects `DIRSEP_CHAR` and
  `createdir()` from it. Under `c99` the tests compiled the POSIX branch — which is not the branch
  that ships. Any future flag the real build defines must be mirrored here, per test, through the
  `TESTFLAGS:` comment.
- **A test can accidentally depend on another module's globals.** `solution.c` defines `savedir` and
  `readonly` itself, which is not what `solution.h`'s `extern` declarations suggest; a test that
  helpfully defined them too failed to compile. That is the good outcome, and it is why stubs are
  written only after the linker asks for them.
- **`-Werror` applies to the module, not just the test.** Compiling `mslogic.c` into a test surfaces
  its five pre-existing warnings. They are suppressed per-test with a stated reason rather than
  "fixed" in upstream code — see `CLAUDE.md` §5.
- Compiling a 200 KB `mslogic.c` per test is slower than linking an object, and each test pays for
  it separately. At this suite's size that is a few seconds, and it buys independence: no test can
  corrupt another's copy of the engine's file-scope state.
- The same reasoning is why `test/tw_test.h` and `test/tw_fixture.h` are **header-only**. There is no
  library to link, so there must be nothing to link.
