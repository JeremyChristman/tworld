# 0004 — Every test is built twice, as C and as C++ — unless it says why not

**Status:** Accepted (jc-43) · **Amended** 2026-09-03 with the `TESTLANG:` escape · **Applies to:** `test/run-tests.ps1`

## Context

`generic/in.c` is compiled **as C** by the SDL front end and **as C++** by the Qt front end, which
reaches it through `generic/_in.cpp` — a file whose entire contents are `#include "in.c"`.

This fork builds and ships only the Qt front end. So a construct that is valid in C++ but not in C
compiles cleanly here, ships, and breaks a build nobody runs by hand — and the person who finds out
is whoever next tries to build the SDL flavor, months later, with no idea which change did it.
`generic/dirinput.c` (added in jc-43) has the same property and the same exposure.

## Decision

**`test/run-tests.ps1` builds every test twice by default**, once with `gcc -std=gnu11` and once with
`g++ -std=gnu++11`, and both must pass. That makes the dual-language constraint a property the test
suite enforces, rather than a comment at the top of `in.c` that people read after breaking it.

**A test may narrow itself** with a `TESTLANG:` comment on one of its first lines:

```c
 * TESTLANG: c
```

Narrowing is a **claim that the code under test is never compiled the other way**, and the test file
must say why next to the directive.

## Consequences

- A C++-only construct in `generic/in.c` or `generic/dirinput.c` fails the suite immediately.
- **The rule is about `in.c`, and it does not generalize to the whole tree.** Most modules here are
  compiled only as C by CMake, and several — `fileio.c`, `solution.c`, `mslogic.c`, and anything
  using `err.h`'s `x_alloc` macro — rely on C's implicit `void*` conversion, which C++ rejects
  outright. Building those as C++ does not test the shipped program; it tests a translation that
  never happens. `solution_test.c` and `mslogic_test.c` therefore declare `TESTLANG: c`.
- **The escape is the risk.** A `TESTLANG: c` added to make a red build green would silently retire
  half the guarantee. Two things push back: the directive is validated strictly (an unrecognized
  value is a hard error, which is how a line of prose running on from the directive got caught), and
  the reason is required to live in the file, where a reviewer sees it.
- Running `-Lang c` or `-Lang c++` by hand prints a warning, because a green run that skipped a
  language proves half of what it appears to.
- The doubling is cheap: these are single translation units with no link step worth speaking of.
