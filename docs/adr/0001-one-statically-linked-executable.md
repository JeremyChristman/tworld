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

## Consequences

- The download is four files plus a license, and the program runs from anywhere it is unpacked.
- **Building the release requires `mingw-w64-x86_64-qt5-static`**, which is a large package and not
  what a stock MSYS2 install has. `build.ps1 -Flavor dynamic` exists so that ordinary development
  and CI do not need it.
- A scratch copy of the executable **without those two DLLs dies with `0xC0000135`
  (`STATUS_DLL_NOT_FOUND`) — no window, no stderr, and the process lingers**, so it looks like a
  hang rather than a missing file. Worse, the resulting "System Error" dialog survives killing the
  process and then steals focus. When copying the executable somewhere to test it, copy both DLLs
  with it.
- Static linking makes the binary large and non-reproducible across toolchain updates. The build
  tag compiled in from `fork.h` (ADR 0006), not a hash, is how a build identifies itself.
