# 0010 — CI builds on Windows with MSYS2, and tests the dynamic Qt flavor

**Status:** Accepted (2026-09-03) · **Applies to:** `.github/workflows/ci.yml`

## Context

The product is a Windows desktop program. Every script in this repository is PowerShell, targeting
**Windows PowerShell 5.1** specifically — and several of them exist only because of 5.1's behavior:
`package.ps1` builds zip entry names by hand because both `Compress-Archive` and
`ZipFile::CreateFromDirectory` emit backslash separators there, and every script avoids redirecting a
native command's stderr because 5.1 wraps those lines in `NativeCommandError`.

A Linux CI job would run none of that. It would compile some C and tell you nothing about the thing
that ships.

There is a second, sharper reason. The end-to-end tests drive the real executable, and the single
worst trap in this repository is that **the executable is a Windows-GUI-subsystem binary**: called
the obvious way from PowerShell it does not block, returns an empty exit code, and captures no
output, so a naive test passes in 11 milliseconds having asserted nothing. That trap only exists on
Windows, only under PowerShell, and only for this subsystem setting. CI has to be where it would be
caught, not where it cannot occur.

## Decision

**CI runs on `windows-latest`, with MSYS2 installed to `C:\msys64`** — the same path the maintainer's
machines use, so `build.ps1` and `test/run-tests.ps1` exercise their own default path handling rather
than having it bypassed by configuration.

Three jobs, deliberately separated by cost:

| Job | Runner | Needs | Catches |
|---|---|---|---|
| `hygiene` | ubuntu-latest | nothing | a committed `.tws`/`CHIPS.dat`, a malformed `FORK_BUILD_TAG`, a `README.txt` header that does not name the build |
| `unit` | windows-latest | gcc only | a defect inside one module |
| `build-and-e2e` | windows-latest | gcc, cmake, ninja, Qt5, SDL2 | a build break, or a defect in how the parts fit together |

**The e2e job builds the DYNAMIC Qt flavor, not the static one that ships.** The static build needs
`mingw-w64-x86_64-qt5-static`, a large package, and produces a ~40 MB binary; the dynamic build runs
the same code in a fraction of the time. What the static link changes is packaging, not behavior, and
`package.ps1` is what verifies that.

## Consequences

- **A PowerShell 7 construct fails CI**, which is the point: `&&`, `||`, ternary and `??` are all
  parse errors on 5.1, and the maintainer runs 5.1.
- **CI cannot catch a static-link-only failure.** Missing `zlib1.dll`/`libzstd.dll`, or a Qt static
  plugin problem, surfaces only when `package.ps1` runs. That is a real gap, and it is why the
  release checklist has a human extract the zip and play the game.
- **The unit job is separate so that it is the one that tells you what broke.** It needs one package
  and finishes in a fraction of the Qt job's time; if both fail, read the unit result first.
- **`msys2/setup-msys2` is pinned to a tag, not a commit SHA**, unlike every other action here. That
  is a known gap, recorded at the call site and in `dependabot.yml`. It must be pinned properly
  before any *release* workflow is made to depend on a third-party action, because that job produces
  bytes strangers run.
- Sanitizers are unavailable on this path: mingw-w64 GCC ships no `libasan` or `libubsan`, and no
  libFuzzer. ASan/UBSan and a fuzz target over the `.dat` and `.tws` parsers need a Linux build that
  does not exist yet. The upgrade path is written at the top of `codeql.yml`, and the gap is stated
  in `SECURITY.md` rather than left implied.
