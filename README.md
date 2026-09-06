# ECE 309 Project 1 — An LLM Mini-Harness in C

A minimal **agent harness** written in standard C. It wraps a deterministic mock
language model with the things a model cannot do for itself: a conversation
loop, a bounded memory, and real tool execution.

Built with Specification Driven Development — [`SPEC.md`](SPEC.md) was written
first and the C code was generated from it. The prompts and iterations are in
[`vibe_coding_log.md`](vibe_coding_log.md).

## Quick start

```bash
make            # build ./harness
./harness       # interactive session
make test       # run the automated suite (bash test.sh)
```

Or without `make`:

```bash
gcc -std=c11 -Wall -Wextra -pedantic -O2 -o harness harness.c context.c model.c tools.c -lm
```

## What it does

```
$ ./harness
ECE 309 mini-harness (mock model, 5-turn context)
[harness] commands:
  exit | quit   end the session and free all memory
  /history      show the turns currently held in context
  /help         show this message
[harness] try: hello   |   calc 2+3*4   |   what is 10/4
you> hello
model> Hello! I am a mock model running inside a C harness. Ask me to calculate something, or type exit to quit.
you> calc 2+3*4
[harness] tool call: calc(2+3*4)
[harness] tool result: 14
model> The answer is 14.
you> 7/0
[harness] tool call: calc(7/0)
[harness] tool result: error: division by zero
model> The calculator tool failed -- division by zero
you> /history
[context] 5 turn(s) held (max 5), 8 total added
  1. assistant | The answer is 14.
  2. user      | 7/0
  3. tool      | error: division by zero
  4. assistant | The calculator tool failed -- division by zero
  5. user      | /history
you> exit
[harness] shutting down
[harness] final context state:
...
[harness] goodbye
```

Note the middle exchange: the model did **not** compute `2+3*4`. It emitted a
tool request, the harness ran real C code, and the model was called a second
time with the answer in its context. That two-pass turn is the core idea of the
project.

## Architecture

```
             stdin
               |
               v
   +-----------------------+
   |      harness.c        |   the loop, the state machine, shutdown
   |  READ -> THINK -> ACT |
   |        -> SPEAK       |
   +--+-------+--------+---+
      |       |        |
      v       v        v
 context.c  model.c  tools.c
 (memory)   (mock    (real
            LLM)     execution)
```

| File | Role |
|---|---|
| `harness.c` | Main loop and state machine. Owns stdin/stdout and the shutdown path. |
| `context.h/.c` | 5-turn ring buffer. Owns every heap copy of turn text and frees it on eviction. |
| `model.h/.c` | Deterministic mock model. Produces text and *requests* tools; never executes. |
| `tools.h/.c` | Tool registry plus a recursive-descent arithmetic evaluator. |
| `memcheck.h/.c` | Optional allocation accounting (`-DHARNESS_MEMCHECK`). Compiles to nothing when off. |
| `test.sh` | Black-box test suite: 38 assertions including two independent leak checks. |
| `SPEC.md` | The specification the code was generated from. |
| `vibe_coding_log.md` | Prompts, iterations, and what had to be corrected. |

## Design decisions worth defending

**A ring buffer, not a growing list.** A context window is *bounded* — that is
the defining property of the problem. A design that grows with session length
would model it wrongly. Five slots, forever, and eviction is O(1).

**The model requests; the harness executes.** `model.c` emits
`[[TOOL:calc:2+3*4]]` as ordinary text and does nothing else. `harness.c` parses
that marker and calls the tool. This mirrors real function calling and keeps the
trust boundary visible: the thing that generates text is never the thing that
runs code.

**A real parser, not `atof`.** `tool_calc` is a recursive-descent parser so that
precedence, associativity and parentheses are correct. A calculator that
returns 20 for `2+3*4` is worse than no calculator — the only reason to delegate
to a tool is that the tool is *right*.

**Errors are reported, never guessed.** `7/0` yields
`error: division by zero`, and the model reports the failure rather than
inventing a number. `2+banana` is a parse error, not a silent `2`.

**Copy before evict.** `context_add` allocates the new text *before* freeing the
oldest turn, so an allocation failure leaves the history intact instead of
destroying a turn that cannot be replaced.

**No POSIX extensions.** `strdup` and `strcasestr` are POSIX, not standard C, so
both are hand-written. The build is warning-free under
`-std=c11 -Wall -Wextra -pedantic`.

## Tests

```bash
bash test.sh
```

38 assertions across four groups — core loop, tool execution, state management,
and memory safety. Verified on Ubuntu 22.04 / gcc 11.4 / valgrind 3.18.1:
**38 passed, 0 failed, 0 skipped**. The state-management group proves eviction rather than
assuming it: seven exchanges add 14 turns, and the final dump must report
`5 turn(s) held (max 5), 14 total added`, contain `alpha7`, and **not** contain
`alpha1`.

**Two independent leak checks.** The first runs the binary under
`valgrind --leak-check=full` (falling back to `-fsanitize=address,undefined`):

```
==3455== HEAP SUMMARY:
==3455==     in use at exit: 0 bytes in 0 blocks
==3455==   total heap usage: 36 allocs, 36 frees, 9,455 bytes allocated
==3455== All heap blocks were freed -- no leaks are possible
==3455== ERROR SUMMARY: 0 errors from 0 contexts
```

The second **always runs, on any machine**. `make memcheck` builds with
`-DHARNESS_MEMCHECK`, routing every project `malloc`/`free` through counting
wrappers that print a ledger at exit:

```
[memcheck] allocs=34 frees=34 outstanding=0 bytes_outstanding=0 peak_bytes=336 bad_frees=0
[memcheck] OK: every allocation was freed
```

It is weaker than valgrind — it sees only this project's allocations and cannot
detect invalid reads or writes — but it answers the leak question directly and
needs no external tool, so the memory group is never left with zero evidence on
a machine without valgrind. It was validated by deliberately removing the
eviction `free()`, which it caught as `outstanding=9 bytes_outstanding=183`.

Four control paths are checked: normal `exit`, bare EOF, an empty session, and
50 turns through the 5-slot window.

## Requirements coverage

| Spec requirement | Implementation | Test |
|---|---|---|
| 1. Core loop with a mock model | `harness.c:main`, `model.c` | §1, 9 assertions |
| 2. Context management (last 5 turns) | `context.c` ring buffer | §3, 10 assertions |
| 3. Tool execution | `tools.c`, `harness.c:run_turn` | §2, 10 assertions |
| 4. Vibe coding log | `vibe_coding_log.md` | — |
| 5. AI-generated tests + leak check | `test.sh`, `memcheck.c` | §4, 8 assertions |

## Environment

Developed against `gcc -std=c11`. Builds and runs on Linux, WSL (Ubuntu) and
macOS. On Windows, use WSL:

```bash
sudo apt update && sudo apt install gcc make valgrind
```
