# Releasing

Every behavior change here ships publicly. The GitHub link is handed to people who have never met the
maintainer, so a release is an executable **plus** the files needed to run, configure and understand
it.

## The invariant

> **Two executables must never report the same build tag.**

`FORK_BUILD_TAG` in [`fork.h`](../fork.h) is compiled into the binary and is what `Help > About` and
`-V` claim about the bytes in front of you. Once a tag is published, that tag describes those exact
bytes forever.

`fork.h` is the **only** definition ([ADR 0006](../docs/adr/0006-fork-h-owns-the-build-tag.md)):
`help.c` reads it, `oshw-qt/TWApp.cpp` builds `"[" FORK_BUILD_TAG "]"` from it, and `package.ps1`
parses the `#define` to name the zip. It used to live in three places at once, and the one that got
forgotten was never the loud one — an About box quietly claiming the wrong build looks exactly like a
correct one.

Two mechanical guards back this up:

- `build.ps1 -ExpectTag jc-N` fails unless `fork.h` agrees with the tag you name.
- `build.ps1` and `package.ps1` both search the **built binary** for the UTF-16LE bytes of `[jc-N]`.
  An incremental build can relink without recompiling the file that carries the tag, so agreeing
  about `fork.h` is not the same as agreeing about the executable.

## Checklist

1. **Bump `FORK_BUILD_TAG` in `fork.h`.** Nothing else.

2. **Update `README.txt`.** It ships inside the zip and is a per-release deliverable, not a one-time
   write:
   - the **header line** must name the new build — `package.ps1` **refuses to package** otherwise,
     and the check is anchored to the header specifically, so a revision-history entry mentioning the
     tag will not satisfy it;
   - add a section 7 entry saying what changed **and what that accomplished**, in plain English;
   - if the release adds or changes a **setting**, document it in section 6 — what it does, valid
     values, the default, and the menu path that sets it. A shipped setting nobody can find out about
     is not shipped. Say what a changed default used to be.

3. **Update [`FORK.md`](../FORK.md)** with the engineering detail — the reasoning, what broke first,
   what was measured — and **[`CHANGELOG.md`](../CHANGELOG.md)** with the summary entry.

   **Work that does not ship in the executable rides along with the next release that does.** Tests,
   documentation and developer tooling do not earn a build tag of their own: a tag should stay
   attached to something a user can observe. Accumulate that work under `## Unreleased` in
   `CHANGELOG.md` and fold it into the new version's heading when a real change is finally cut.

4. **Verify.**
   ```powershell
   powershell -ExecutionPolicy Bypass -File verify-defaults.ps1
   powershell -ExecutionPolicy Bypass -File run-tests.ps1 -Build
   ```
   Both must be green, and `run-tests.ps1` must run **both** layers — a unit-only run proves nothing
   about the program starting.

   **If the change touched the engine**, the unit suite is not enough. Batch-verify a solution
   corpus, from a scratch working directory:
   ```powershell
   .\build-static\tworld2.exe -b -r -S <savedir> <set>.dac
   ```
   reading **stdout**, not the exit code. ⚠ Remember that a corpus run cannot see input-layer changes
   at all (`CLAUDE.md` §3.5): `doturn()` ignores its `cmd` argument during replay and batch mode
   never enables joystick behavior. For anything in `generic/`, hand playtesting is the only oracle.

5. **Package and playtest.**
   ```powershell
   powershell -ExecutionPolicy Bypass -File build.ps1 -ExpectTag jc-N -Manifest dist\build-manifest.json
   powershell -ExecutionPolicy Bypass -File package.ps1
   ```
   Then extract `dist\TileWorld-jc-N.zip` somewhere clean and **play the game from the zip** — not
   the executable in your build directory. Open a set, play a level, change a setting, check
   `Help > About`. Reviews audit artifacts; this audits reality.

   ⚠ This step is where a static-link failure surfaces, and nowhere else. CI builds the *dynamic*
   flavor, so a missing `zlib1.dll`, a Qt static-plugin problem, or a binary that dies with
   `0xC0000135` before it can draw a window will reach you only here.

6. **Commit, push, tag.**
   ```powershell
   git add -A
   git commit -m "jc-N: <what changed and what it accomplished>"
   git push origin main
   git tag jc-N
   git push origin jc-N
   ```

7. **Publish.** The tag push runs the release workflow, which re-verifies, re-packages, and creates a
   **draft** release with the zip attached. Download that asset, launch it once, then publish.

   The draft is deliberate: CI builds on a different machine than the maintainer's, and the playtest
   gate is a human act CI cannot perform.

8. **Send the link:** `https://github.com/JeremyChristman/tworld/releases/tag/jc-N`

## What ships

`package.ps1` produces `dist\TileWorld-<tag>.zip` containing exactly six files:

| File | Rule |
|---|---|
| `Tile World.exe` | the build being released, stripped. The shipped name has a space and no "tworld" in it — set deliberately, because that filename has caused false conclusions before |
| `tw_settings.ini` | a stock settings file. **Every value must be what the code defaults to when the key is absent** — `verify-defaults.ps1` checks the key set against `settings.cpp`; the values are still a human responsibility |
| `zlib1.dll` | pinned as a dynamic import by Qt's static config. The game will not start without it |
| `libzstd.dll` | likewise |
| `README.txt` | re-updated every release (step 2) |
| `COPYING` | GPLv2 travels with the binary, as the license requires |

One zip rather than loose assets, so nobody downloads the executable without the other five.

The packager verifies the **archive**, not the folder it was built from: entry names must contain no
backslash (PowerShell 5.1 writes them by default, and Info-ZIP on Linux and macOS does not treat
those as separators — this is a public download that has to open off Windows), the file set must be
exactly those six, and every entry must hash-match its source.

## Build provenance

```powershell
powershell -ExecutionPolicy Bypass -File build.ps1 -Manifest dist\build-manifest.json
```

Records the tag, flavor, git commit, the executable's SHA-256 and size, and the exact compiler and
CMake versions. Nothing in the binary itself reveals which toolchain produced it, and with the build
tag switched off — its default for downloaders — the SHA-256 is how you confirm which build is which.

⚠ `-Manifest` **refuses** to write for a build made with `-NoFix`. A build with an engine fix
compiled out is a debugging artifact and must never be packaged: it would carry a `jc-N` tag
describing behavior it does not have ([ADR 0002](../docs/adr/0002-engine-fixes-are-opt-out-macros.md)).

## After the release

Deploy it, rather than assuming the GitHub link is the delivery: copy the executable and its two DLLs
into the Chip's Challenge folder that actually gets played, and confirm the game starts there. A
release nobody installed is a release nobody has tested in the place it matters.
