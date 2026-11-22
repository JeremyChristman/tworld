# 0001 — The release is one statically linked executable

**Status:** Accepted (jc-1, 2026-07-25) · **Applies to:** `CMakeLists.txt`, `package.ps1`

## Context

Upstream Tile World 2.3.1 builds against a dynamic Qt5. On Windows that produces `tworld2.exe` plus
roughly **27 DLLs** that must sit beside it — the Qt5 core/gui/widgets/xml libraries, the
`platforms/qwindows.dll` plugin in its own subdirectory, SDL2, and the MinGW runtime.

The audience for this fork is Chip's Challenge players, not developers. A download whose folder
contains 28 files, one of which is the program, invites exactly two failure modes: someone copies
`tworld2.exe` somewhere else and it will not start, and someone moves or deletes
`platforms/qwindows.dll` and it will not start *with no error message at all* — Qt aborts before it
can raise a window.

## Decision

**Build against a static Qt (`/mingw64/qt5-static`) and link the Windows platform plugin in**, so
the shipped program is a single executable. `CMakeLists.txt` detects this by the existence of the
`Qt5::QWindowsIntegrationPlugin` target, so an ordinary dynamic build is unaffected and still works
for development.

Two dynamic imports survive and **must ship beside the executable**: `zlib1.dll` and `libzstd.dll`.
Qt's static CMake config pins those two; SDL2 and the platform plugin are baked in. `package.ps1`
copies both and fails the package if either is missing.

> **Amended 2026-11-21 (jc-61):** the two names were MSYS2's, not Qt's, and one changed: CI's zlib
> became `libz-1.dll` between jc-59 and jc-60, while `package.ps1` kept shipping `zlib1.dll`, so
> jc-60's zip could not start (a burned tag). `package.ps1` now reads the DLLs from the exe's import
> table and refuses an import it cannot find.

> **Amended 2026-11-22 (jc-62):** the static Qt is now UCRT64's (`/ucrt64/qt5-static`, the same
> 5.15.19-1, pinned by file in `release.yml`): MSYS2 is retiring MINGW64 and removed its
> `qt5-static` on 2026-11-21. The C runtime moved with it, from `msvcrt.dll` to Windows' Universal C
> Runtime (`api-ms-win-crt-*`, part of Windows 10 and 11; an update on 7 and 8.1). The surviving
> imports are `libz-1.dll` and `libzstd.dll`. The decision itself, one statically linked exe, is
> unchanged; and the corpus showed the runtime change moved no replay (0 of 334 sets).

## Consequences

- The download is four files plus a license, and the program runs from anywhere it is unpacked.
- **Building the release requires the environment's `qt5-static`** (`mingw-w64-ucrt-x86_64-qt5-static`
  since jc-62; `mingw-w64-x86_64-qt5-static` before), which is a large package and not
  what a stock MSYS2 install has. `build.ps1 -Flavor dynamic` exists so that ordinary development
  and CI do not need it.
- A scratch copy of the executable **without those two DLLs dies with `0xC0000135`
  (`STATUS_DLL_NOT_FOUND`) — no window, no stderr, and the process lingers**, so it looks like a
  hang rather than a missing file. Worse, the resulting "System Error" dialog survives killing the
  process and then steals focus. When copying the executable somewhere to test it, copy both DLLs
  with it.
- Static linking makes the binary large and non-reproducible across toolchain updates. The build
  tag compiled in from `fork.h` (ADR 0006), not a hash, is how a build identifies itself.
