# 0013 — The mutation kill rate is measured by a committed harness, not by an audit

**Status:** Accepted (2026-09-11) · **Applies to:** `mutate.ps1`, `docs/mutation-baseline.tsv`

## Context

This fork's central quality claim is about its unit suite. `run-tests.ps1` reports 22,734 checks
across 18 runs in 8.2 seconds, `coverage.ps1` reports line and branch percentages against a
committed baseline, and `CLAUDE.md` quotes both. None of that measures **power**: a suite can
execute a line, print a check, and assert nothing about what the line did.

The only measurement of power ever taken here was an outside adversarial audit on 2026-09-08. It
hand-wrote 233 mutations across nine modules and reported 45% killed, with the verdict that "test
power is roughly half what the headline numbers imply." Several specific gaps it named were then
closed. But the audit had three properties that make it a poor instrument to keep:

1. **It was not reproducible.** The harness was improvised and discarded, so the number cannot be
   recomputed, and after the fixes nobody knew what the current figure was.
2. **The mutations were chosen by a person.** Which mutations you pick is where bias enters a kill
   rate, and a rate over hand-picked guards is not the same quantity as a rate over a codebase.
3. **Its harness had a bug**, which it disclosed itself: 14 of 190 rows were reported as survivors
   while having run no tests at all.

That third point deserves to be stated precisely, because it is the reason this ADR exists and the
reason the harness is built the way it is. **That bug failed in the pessimistic direction.** It
invented survivors, made the suite look worse than it was, and somebody went and looked — because a
surprising survivor is something a person investigates.

Every bug available to an automated mutation harness fails the other way:

- a mutant that does not compile, scored as a kill
- `-Werror` turning a legitimate mutant into "invalid", shrinking the denominator
- a hung test that the harness itself killed, scored as a kill
- scratch-directory debris from one mutant failing every later one
- a flaky test failing on a mutant it would have failed on anyway
- an "equivalent mutant" exclusion quietly matching lines nobody proved
- a blended rate quoted against a number it is not comparable to

**Nobody investigates a kill.** A number that is too high is believed, repeated, and eventually
written into the documentation.

## Decision

**`mutate.ps1` at the repository root, built to refuse to produce a number rather than produce a
flattering one.** Specifically:

- **It never touches the working tree.** Everything happens in a scratch copy built from
  `git ls-files`. This also stops `test/run-tests.ps1` writing a mutant's check count into
  `docs/test-counts.tsv`, which `verify-docs.ps1` treats as authoritative.
- **It only mutates lines that survive preprocessing**, taken from `gcc -E` linemarkers rather than
  from a table. `mslogic.c` is 4,969 lines of which 2,181 reach the compiler; the rest are comments,
  blanks, directives and the inactive arms of 32 `NO_FIX_*` toggles. A mutant in an inactive `#else`
  arm cannot fail a test, so counting it as a survivor invents a test gap in this fork's most
  important file. The same pass derives which test compiles which source — a hand-written map was
  already wrong once, omitting `lxlogic.c`, `tworld.c`, `generic/in.c`, `play.c` and `score.cpp`,
  36% of the mutable surface.
- **It classifies from the runner's summary block, never from its exit code.**
  `test/run-tests.ps1` exits 1 for a failed test *and* for a compile failure *and* for a filter that
  matched nothing.
- **It compiles mutants with `-Wno-error`**, so a bounds mutant that makes a comparison provably
  constant does not land in INVALID via `-Wtype-limits` and silently raise the rate.
- **A kill must repeat before it is believed.** `settings_test.c` does real filesystem I/O with
  retries and `Sleep(20)`, and `settings.cpp` is a mutation target; this repository has already
  burned a release tag on a flaky timing test.
- **SURVIVED must be earned**: every expected `(test, language)` run present, all passed, and check
  and skip counts exactly equal to the baseline recorded from the pristine tree.
- **Timeouts are their own bucket, excluded from the numerator.** "The test hung" is not "the test
  detected it" — nothing asserted anything, the harness killed the process.
- **`-SelfTest` runs four canaries with known answers before any census**: one that must be killed,
  one that must not compile, one that must survive because it sits on a line the compiler never
  sees, and one that must hang. Between them they prove the mutation was really applied, the tests
  really ran, INVALID is not scoring as KILLED, the compiled-line filter works, and the timeout and
  process-tree-kill path — otherwise untested code that a long run bets on — actually recovers.

**Phase 1 ships one operator, ROR**, restricted to boundary shifts (`<`↔`<=`, `>`↔`>=`) and equality
inversion (`==`↔`!=`). Deliberately **not** `<`→`>`: a sign reversal is a gross change that nearly
any test kills, so it pads the numerator with mutants that prove nothing, while every memory-safety
defect this fork has shipped and fixed — jc-44, jc-45, jc-50, jc-51 — was an off-by-one.

🔴 **THAT LAST SENTENCE WAS THE HALF-TRUTH IN THIS ADR, and a blind audit called it (2026-09-20).**
Those off-by-ones were in **operands and buffer sizes**, and a relational boundary shift models only
some of them. For `ptr + k > end`, the dominant idiom in these parsers, ROR can only make the bound
*stricter* — the dangerous direction is unreachable, because loosening means editing `end`, `k` or a
declared array size. The audit found three real gaps there, all invisible to this census; each now
has a case, and `openfileindir()`'s buffer has a `_Static_assert` instead, since no test on Windows
can see a one-byte stack overflow.

**Phase 2 ships `OFF`, offset injection on a comparison's right-hand operand** (2026-09-20).
`X > end` becomes `X > (end) + 1` and `X > (end) - 1`: both directions, because a bound can be wrong
either way and a suite that pins a boundary has to notice both. The left operand is deliberately not
mutated — shifting either side by one covers the same ground, and a backward extent scan is where a
character scanner earns its bugs. It skips an operand that continues onto the next line (a `from`
field with a newline would corrupt the per-mutant TSV), an operand a comment or string stands in or
in front of, an operand containing a tab (same reason as the newline), `NULL` (meaningless, and
ill-typed in the C++ build), anything over 60 characters, and three C++ shapes that are not
comparisons at all: a template argument list, a `::` scope operator, and an iterator endpoint such
as `settings.end()`, which has no `operator+`. Those three were measured, not anticipated —
`settings.cpp` produced 88 mutants of which over 60% could not compile, and produces 40 with none
invalid now. ⚠ `-MaxInvalidRate` did not catch that: it is computed over the whole run, and one
badly-parsed translation unit hid under a tree-wide rate near 2%.

- **It asks the question ROR could not, and the first run proved it.** `encoding.c`: 56 mutants, **0
  invalid**, 49 killed (88%) — including the audit's own mutation, `dataend` → `(dataend) + 1`,
  which the new case kills. Its `- 1` twin survives and is an equivalent mutant (it assigns a value
  already equal).
- **The default stays ROR alone**, and OFF is asked for by name. Every figure quoted against the
  committed baseline is a ROR census; folding a second operator into the default would move the
  headline without one thing about the suite changing — the exact misreading this ADR's
  "Consequences" already warns about. OFF also produces about twice as many mutants, so a census is
  a run of hours rather than half an hour.
- **The baseline keeps one row per file AND operator, each with the commit it was measured at**, and
  a run replaces only the rows of the operators it measured. ⚠ **The committed file still shows the
  pre-Phase-2 shape** — the old `operators` column, eight fields, one header commit — until the next
  `-UpdateBaseline` run rewrites it; the writer reads both, and carries a legacy row's commit over
  from the header rather than leaving the row a field short. Otherwise recording one operator would
  silently delete the other's numbers, and a single header commit would misdate whichever half was
  older.
- **`-SelfTest` checks the GENERATOR, not only the pipeline.** A canary proves a mutant that reached
  disk was scored correctly; it says nothing about mutants the scanner never produced or produced
  wrong, which is where OFF's whole risk lives. It is fed a fixed probe whose answer is written into
  the script, including the four exclusions. That check earned itself immediately: it failed on
  first run because the expectation was wrong, not the scanner.

## Consequences

- **The number is reproducible and re-runnable.** About 1,270 ROR mutants over the sixteen sources
  the tests compile, in roughly half an hour; an OFF census is about twice the mutants and a run of
  hours. ⚠ Approximate on purpose — the count moves with any comparison added to a censused source,
  and the run prints the exact figure.
- **🔴 The result is NOT comparable to the 2026-09-08 audit's 45%, and nothing may quote
  "45% → X%".** That figure came from 233 hand-chosen mutations aimed at guards. This is a
  mechanical census whose blended rate is a function of the operator mix and the site distribution —
  enabling a second operator moves the headline without one thing about the test suite changing. The
  script prints this warning above its own summary table, and the per-file and per-operator columns
  are the primary output for the same reason `coverage.ps1` says to read the per-file column.
- **This is a deliberate instrument, not a test layer.** `run-tests.ps1` is 8.2 seconds and gates
  `package.ps1`; this is half an hour. It belongs beside `coverage.ps1` and `test/run-nofix.ps1
  -Search`. There is deliberately no `-CheckBaseline` gate yet: a gate nobody can run in under half
  an hour trains people to ignore it.
- **A `#define` body is never mutated**, because it produces no preprocessed output of its own even
  though it compiles at every expansion site. Stated rather than silent.
- **SURVIVED is a real gap, measured.** The obvious objection to a unit-layer kill rate is that the
  other five layers catch what it misses, making the survivor list padded and not worth acting on.
  `-Escalate` settles it by re-running every survivor under `-Sanitize`: of the first census's **949
  survivors it caught 7, or 0.7%**. The list is the work queue as written. ⚠ That is not a verdict
  on the sanitizer — it judges only what a test executes, and answers a different question than a
  boundary mutation asks; jc-50 was invisible to every other local layer. The seven it did catch are
  guards nothing pins, and are listed in `CLAUDE.md` §5.
- **A canary that has to be built rather than found is itself a result.** The intended
  sanitizer-only canary was reverting jc-50, which `CLAUDE.md` recorded as leaving every local layer
  green except `-Sanitize`. Measured 2026-09-11, that is no longer true of either site: jc-57's
  direct cases for `movelaw_block()` and `movelaw_creature()` now fail the plain pass with real
  assertions. The canary is therefore synthetic — a volatile signed overflow in `nextvalue()` — and
  says so in the code.
- **Survivors whose check count moved are the actionable ones.** A mutant where every test passes
  but the suite's check count *changed* means the tests reached that code and declined to assert on
  it. That costs one integer comparison and gets its own column.
- **A survivor list is two problems, and `-Split` separates them** by joining the survivors against
  gcov's per-line map: **REACHED** (370, 39%) means a test runs the line and does not assert — the
  cheap fix, usually a few lines in an existing test; **UNREACHED** (573, 60%) means nothing runs it,
  where no assertion can help. Spend on REACHED first.
- **Equivalent mutants are derived, not registered.** The plan was a hand-maintained
  `mutants-equivalent.tsv`. `-Split`'s third bucket does better: gcov emits no line record for code
  the compiler folded away, and `mslogic.c:2251` (`if (FALSE && …)`) and `:3452` (`if (TRUE || …)`)
  are upstream short-circuits, so every mutation inside those conditions is provably equivalent. Six
  found mechanically, with no list for anyone to keep current. ⚠ NO-RECORD has a second cause —
  file-scope data, where gcov also emits nothing — so it must never be treated as a synonym for
  unreached; that would file `movelaws[]` under "nothing runs it".
