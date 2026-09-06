# Vibe Coding Log

A record of how this codebase was produced by steering an AI assistant
(Claude, via Claude Code) rather than by hand-writing C. It is deliberately
honest about what went wrong — the iterations are the evidence that SDD was
actually practised, not the parts that worked first time.

---

## 0. Architectural rules given to the AI

These constraints were fixed *before* any code was requested. They are the
"hard constraints" of Specification Driven Development: the fence inside which
the assistant is allowed to improvise.

1. **Standard C11 only.** No third-party libraries. No POSIX-only functions —
   which specifically rules out `strdup` and `strcasestr`, both of which had to
   be hand-written.
2. **Zero warnings** under `gcc -std=c11 -Wall -Wextra -pedantic`. A warning is
   a failed build, not a suggestion.
3. **One module, one responsibility.** The model must not execute anything; the
   harness must not generate text. If a file does both, the design is wrong.
4. **Every `malloc` has exactly one matching `free`,** on every control path,
   including the EOF path.
5. **Deterministic output.** The mock model must be a pure function of the
   context, because a shell script has to assert on its exact strings.
6. **Errors are reported, never guessed.** No `inf`, no silent truncation, no
   fabricated answer after a tool failure.
7. **The spec is the source of truth.** If generated code disagrees with
   `SPEC.md`, the code is wrong — do not retro-fit the spec to the code.

## 1. Session start — establishing the requirements

**Prompt (user):**

> Look for file:///C:/Users/dm/Downloads/Proj1_spec.pdf

**What the assistant did:** the PDF-rendering tool was unavailable
(`pdftoppm is not installed`), so it fell back to `pdftotext -layout` and
extracted all five pages of text, then summarised the five implementation
requirements, the four phases, and the submission format.

**Iteration note:** it also flagged a discrepancy the human needed to resolve —
the deadline in the PDF reads *Sep. 5, 2026* while the session date was
*Sep. 6, 2026*. Worth raising with the instructor.

**Prompt (user):**

> yes, set up the whole project

That is a deliberately thin prompt. The interesting work was the assistant
turning it into `SPEC.md` *before* writing C — which is the whole point of the
exercise. The specification, not the prompt, is what produced the code.

## 2. Design decisions taken during generation

These are the judgement calls where a naive prompt would have produced worse
code. Each is recorded with the reasoning, because "why" is the part that does
not survive in the source.

### 2.1 Ring buffer over linked list

The obvious AI-default for "store the last N turns" is a linked list or a
`realloc`'d array. Both were rejected: a context window is *bounded* by
definition, and a structure whose footprint grows with session length models the
problem wrongly. A fixed 5-slot ring buffer makes the bound structural and
eviction O(1).

### 2.2 Copy-before-evict

First instinct is `free(old); old = strdup(new);`. That is wrong: if the
allocation fails, an irreplaceable turn has already been destroyed.
`context_add` allocates first and only then disturbs a slot, so an OOM leaves
the history exactly as it was.

### 2.3 The model must not compute

The naive mock would answer `calc 2+3*4` directly inside `model.c`. That would
have satisfied a careless reading of Requirement 3 while destroying the lesson.
Instead the model emits a marker in its ordinary text:

```
[[TOOL:calc:2+3*4]]
```

and `harness.c` parses it, runs the tool, appends a `tool` turn, and calls the
model a **second** time. The two-pass turn is the architecture the project is
actually about.

### 2.4 Recursive descent, not `atof`

An assistant asked for "a calculator in C" will reach for `sscanf("%lf %c %lf")`.
That cannot express precedence, so `2+3*4` would return 20. Since the only
justification for delegating to a tool is that the tool is *correct*, the spec
mandated a recursive-descent parser over an explicit grammar, plus an explicit
divide-by-zero branch.

### 2.5 `fflush(stdout)` after the prompt

Not obvious, and it is what makes the whole test suite possible. When stdin is a
pipe, stdout becomes fully buffered and the `you> ` prompts arrive out of order
in the captured transcript. Without the flush, every string assertion in
`test.sh` would be testing scrambled output.

### 2.6 Whole-word keyword matching

A plain `strstr(line, "hi")` matches *this*, *shipping*, *architecture*. The
greeting check therefore uses a hand-written `contains_word` with alphanumeric
boundary tests. `test.sh` locks this in with an explicit assertion that
`this is a shell` does **not** trigger the greeting.

## 3. Iterations and failures

### Iteration 1 — first build: clean

```
$ gcc -std=c11 -Wall -Wextra -pedantic -O2 -o harness harness.c context.c model.c tools.c -lm
=== BUILD OK ===
```

Zero warnings on the first compile. Manual smoke test confirmed precedence
(`2+3*4 = 14`), decimals (`10/4 = 2.5`), the divide-by-zero path, and 5-turn
eviction.

### Iteration 2 — a tooling failure, not a code failure

Writing `model.c` in a single shell heredoc failed:

```
/usr/bin/bash: -c: line 256: unexpected EOF while looking for matching `''
```

The file was never created. **Cause:** the heredoc payload exceeded what the
shell invocation would carry in one piece, and the truncated text left an
unbalanced quote. **Fix:** split the file into three appended chunks
(`cat > model.c`, then `cat >> model.c` twice) and verify with `wc -l` after
each. Not a C problem at all — a reminder that AI-assisted workflows fail at the
*plumbing* as often as at the logic.

### Iteration 3 — an escape-sequence bug the compiler caught

A later patch to add a `[harness] final context state:` line was applied through
a Python one-liner. The `\n` in the replacement string was interpreted as a real
newline before it reached the file, splitting a C string literal across two
lines:

```
harness.c:199:12: error: missing terminating " character
  199 |     printf("[harness] final context state:
```

**Fix:** re-applied the patch building the escape explicitly as
`chr(92) + 'n'` so no layer could collapse it, then deleted the orphaned `");`
line left behind by the first attempt. Rebuilt clean.

**The lesson, and it is the one Phase 3 of the assignment is about:** the
compiler is the ground truth. Paste the error back, fix, rebuild — do not
assume generated code is correct because it looks correct.

### Iteration 4 — tightening the state-management assertion

The first draft of the eviction test grepped the whole session transcript for
`alpha1`. That is a false-negative trap: `alpha1` legitimately appears earlier in
the running output, when it was echoed back live. The assertion would have
failed even on correct code.

**Fix:** `sed -n '/\[harness\] final context state:/,$p'` narrows the search to
the final dump only, so the test measures what it claims to measure. The
`total_added` counter was added to `Context` for the same reason — it lets the
test prove that 14 turns were added while only 5 were kept, rather than merely
observing that the count stopped rising.

### Iteration 5 — leak check unavailable locally

`test.sh` prefers `valgrind`, then falls back to `-fsanitize=address,undefined`.
On the development machine (Windows + MSYS2/MinGW) neither is available:

```
$ gcc -fsanitize=address ...
collect2.exe: error: ld returned 5 exit status
```

The script reports `SKIP` **loudly** rather than passing silently — a leak check
that quietly does nothing is worse than no leak check, because it reads as
evidence. Run `bash test.sh` under WSL/Ubuntu with `sudo apt install valgrind`
to execute this group.

## 4. Prompts used for the test script (Phase 4)

The assignment's template prompt is a starting point, not a finish line:

> "I have a compiled C program named harness. Write a very simple Bash script
> (for Linux/Mac) that automatically sends the word 'hello', followed by the
> word 'exit', into the program to test if it works."

That produces a script that proves almost nothing — it cannot fail. The prompt
was therefore extended with explicit obligations:

> Write `test.sh` as a black-box suite over the compiled `harness`. Requirements:
> a `PASS`/`FAIL` counter and a non-zero exit code on any failure; assertions for
> `exit`, `quit`, and bare EOF; a check that `shell` does not trigger the
> greeting; the full arithmetic table including `7/0` and `2+banana`; a state
> test that runs seven exchanges and asserts the final dump reports
> `5 turn(s) held (max 5), 14 total added` while containing `alpha7` and not
> `alpha1`; a 3000-character input line; and a valgrind leak check with an
> AddressSanitizer fallback that reports SKIP rather than passing silently.

Result: 30 assertions, all passing.

## 5. Final state

```
$ bash test.sh
== 0. Build ==========================================================
  [PASS] harness compiled
== 1. Core loop: greeting, echo, safe shutdown =======================
  [PASS] greeting fires on 'hello'
  ... (9 assertions)
== 2. Tool execution =================================================
  ... (10 assertions)
== 3. State management: the 5-turn context window ====================
  ... (10 assertions)
== 4. Memory safety ==================================================
  [SKIP] leak check (install valgrind, or use a gcc/clang with -fsanitize=address)
======================================================================
passed: 30   failed: 0   skipped: 1
ALL TESTS PASSED
```

## 6. What I would tell a junior developer to do differently

* Write the specification first. Every bug above was a *plumbing* or *escaping*
  bug; none was a design bug, because the design was settled in `SPEC.md` before
  a line of C existed.
* Freeze the strings the tests assert on, and say so in the spec. `context_print`
  has a documented output contract for exactly this reason.
* Distrust a green test suite until you have seen it go red. The eviction test
  was rewritten specifically because the first version could have passed for the
  wrong reason.
* Treat compiler warnings as build failures from the first commit; `-Wextra
  -pedantic` was set before any code was generated, not bolted on afterwards.

---

## 7. Follow-up session: closing the memory-safety gap

The suite shipped with its memory group reporting `SKIP` — 30 assertions passing
and one unverified. That is the weakest possible state for a test suite to be
in, because a skipped check *reads* like a passed one at a glance. Two fixes
were applied.

### 7.1 A portable leak checker (`memcheck.c`)

**Prompt:**

> The leak check skips on Windows because MinGW has neither valgrind nor
> AddressSanitizer. Add a portable fallback: a `-DHARNESS_MEMCHECK` build that
> routes every project `malloc`/`free` through counting wrappers and prints a
> ledger at exit. It must compile to *nothing* when the flag is absent, and
> `test.sh` must assert on the ledger so the memory group always produces
> evidence.

Design: each block carries a header holding its size and a magic value, so
`free` can subtract the payload from the running total and reject a pointer it
never issued. The magic is poisoned on free, which catches double frees. The
ledger goes to **stderr** so it cannot contaminate the stdout transcript the
behavioural assertions grep through.

Honest limitation, stated in the header comment: this sees only *this project's*
allocations and cannot detect invalid reads or writes. It is strictly weaker
than valgrind. It answers one question — did every `malloc` get a matching
`free` — and it runs everywhere.

### 7.2 Negative control — proving the checker can fail

A leak detector that cannot go red is decoration. The eviction `free()` in
`context_add` was deliberately deleted and the instrumented binary re-run:

```
[memcheck] allocs=21 frees=12 outstanding=9 bytes_outstanding=183 peak_bytes=354 bad_frees=0
[memcheck] FAIL: memory was leaked or misfreed
```

Caught, exactly as intended. The source was then restored and verified against
`git diff` to confirm the only remaining change was the added include.

### 7.3 A real bug the new tests found

Adding the memcheck assertions immediately turned one red:

```
[FAIL] memcheck: empty session is clean
       expected to find: outstanding=0
```

**Cause:** `atexit(memcheck_report)` was registered lazily, on the first
`malloc`. A session that allocates nothing therefore printed **no ledger at
all** — and an assertion looking for `outstanding=0` in empty output fails.
Worse, had the assertion been written the other way round it would have *passed*
on no evidence whatsoever.

**Fix:** an explicit `memcheck_init()` called from the top of `main`, expanding
to `((void)0)` when the feature is compiled out. The report is now armed before
any work happens.

This is the second time in this project a test failed for a reason worth
keeping (the first was the `alpha1` transcript-scoping bug in §3, Iteration 4).
Both were found by writing the assertion first and distrusting the green.

### 7.4 Running valgrind for real

No Ubuntu WSL distro was installed, so verification ran in a container instead:

```bash
docker run --rm -v "C:/Users/dm/ece309_harness:/src" -w /src ubuntu:22.04 \
  bash -c "apt-get install -y gcc valgrind && cd /tmp && cp /src/* . && bash test.sh"
```

First attempt failed on a Windows path-mangling quirk in MSYS
(`the working directory 'C:/Program Files/Git/src' is invalid`), fixed with
`MSYS_NO_PATHCONV=1`.

Second attempt failed with `test.sh: line 15: $'\r': command not found` — the
**exact** CRLF failure that `.gitattributes` was added to prevent, reproduced
live. The committed files were LF, but the Windows working copy had CRLF and the
container mounted the working copy, not a clone. Working tree normalised to LF.
(A first attempt to detect the CRs with `grep -q $'\r'` silently matched nothing
and reported all files clean; `od -c` showed 243 CRs in `test.sh`. Verify with a
tool that counts, not one that merely answers yes/no.)

Final result on Ubuntu 22.04, gcc 11.4.0, valgrind 3.18.1:

```
==3455== HEAP SUMMARY:
==3455==     in use at exit: 0 bytes in 0 blocks
==3455==   total heap usage: 36 allocs, 36 frees, 9,455 bytes allocated
==3455== All heap blocks were freed -- no leaks are possible
==3455== ERROR SUMMARY: 0 errors from 0 contexts (suppressed: 0 from 0)

passed: 38   failed: 0   skipped: 0
ALL TESTS PASSED
```

Four control paths were checked under valgrind independently: normal `exit`,
bare EOF with no `exit` typed, an empty session, and 50 turns forced through the
5-slot window. All clean.
