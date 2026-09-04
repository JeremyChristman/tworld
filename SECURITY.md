# Security policy

## What this program is, in security terms

Tile World is a single-player desktop game. It listens on no network port, runs with no elevated
privilege, has no plugin system, and stores nothing but level data and your own solutions.

**Its entire meaningful attack surface is the files it parses** — and it parses files that other
people made. That is not incidental to how it is used; it is the normal case. Players routinely
download third-party level sets (`.dat`), set configurations (`.dac`), and solution collections
(`.tws`) from community sites and drop them into the game's folders. None of that content is signed
or reviewed. The parsers live in:

| File | Reads |
|---|---|
| `encoding.c` | `.dat` level records — map layers, trap and cloner wiring, the creature list |
| `series.c` | `.dat` level headers and the `.dac` text configuration |
| `solution.c` | `.tws` solution files, including a five-format variable-length move codec |
| `unslist.c` | the bundled unsolvable-level list |
| `settings.cpp` | `tw_settings.ini` |

Realistic worst case is memory corruption in the user's own session — a crash, or in principle code
execution as that user. There is no privilege boundary to cross and nothing to escalate to. Please
calibrate reports accordingly; that is a real bug worth fixing, and it is not a remote exploit.

## Reporting

**Open a [private security advisory](https://github.com/JeremyChristman/tworld/security/advisories/new).**
That is preferred over a public issue for anything memory-safety related, so a fix can ship before
the details are public. If advisories are unavailable to you, open a normal issue describing the
*class* of problem without a working input file, and say you have details to share privately.

Please include:

- the **build tag** (`jc-N`) from `Help > About`, or the release you downloaded;
- what kind of file triggers it, and the **structural property** that matters — "a level whose lower
  map layer declares a size that runs exactly to the end of the record and ends in `0xFF`" is far
  more useful than an attached binary;
- whether it reproduces in **upstream Tile World 2.3.1**. Most of these parsers are upstream's, and a
  defect that is also upstream's should reach them too. This fork's own changes are marked
  `MOD (Jeremy)` and listed in `FORK.md`.

**Please do not attach a `.dat` or `.tws` from a real level set.** Level sets are third-party content
that this project does not redistribute. A description of the byte pattern is what a fix needs.

There is no bounty, and no formal response-time commitment — this is one person's hobby fork. Expect
a reply in days rather than hours.

## Scope

**In scope**

- Memory-safety defects in any of the parsers above, reachable from a file a user could plausibly
  download: out-of-bounds read or write, integer overflow in a length computation, unterminated
  string, `memcpy` with an attacker-influenced length.
- Anything that writes outside the directories the program was pointed at, including path traversal
  through a `.dac`'s `file=` directive or a solution filename.
- Anything that makes the program destroy a user's saved solutions.

**Out of scope**

- Crashes on files that are simply corrupt, where the program reports the problem and exits. Refusing
  bad input is the intended behavior.
- The absence of a sandbox. This is a game, not a document viewer.
- Vulnerabilities in Qt, SDL2, zlib or zstd themselves — report those upstream. Note that the shipped
  build links Qt statically, so a Qt fix reaches users only through a new Tile World release; see
  `.github/dependabot.yml` for why nothing here watches those versions automatically.
- Issues that require the attacker to already be able to run code as the user.

## Known issues

Defects that are already recorded rather than hidden are listed in **`CLAUDE.md` section 8**, with
their origin (upstream or this fork) and why each is or is not yet fixed. Please check there before
reporting.

## Supported versions

Only the **latest `jc-N` release**. This is a fork maintained by one person; there is no branch to
backport to. Fixes ship in the next tagged build.

## What is done to reduce the risk

- The test suite includes malformed-input cases for the `.tws` codec — truncation at every length of
  every variable-length encoding — and the level-record builder used by the engine tests is written
  from the format specification rather than from the parser, so a misreading in the parser is not
  reproduced in the fixtures.
- CodeQL runs on every push and weekly (`.github/workflows/codeql.yml`).
- ⚠ **Sanitizers and fuzzing are not currently run.** The mingw-w64 toolchain this project builds
  with ships no `libasan` or `libubsan` and no libFuzzer, so ASan/UBSan and a fuzz target over
  `expandleveldata()` and `expandsolution()` need a Linux build that does not exist yet. That is the
  largest known gap in this policy, and the upgrade path is written down in the comment at the top of
  `codeql.yml`.
