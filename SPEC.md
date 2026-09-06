# SPEC.md — Technical Specification for the ECE 309 Mini-Harness

**Status:** frozen before implementation. This document is the *input* to the
code-generation step; `harness.c`, `context.c`, `model.c` and `tools.c` are its
*output*. Where the code and this document disagree, this document is the bug
report.

---

## 1. Purpose

Build a terminal program in standard C that acts as an **agent harness** around
a mock language model.

The pedagogical point is the separation of concerns. A language model is a pure
function `text -> text`. It has no memory, cannot run code, and cannot touch the
operating system. Every capability that makes an "AI agent" feel like an agent
is supplied by the harness surrounding it:

| Responsibility | Owner | File |
|---|---|---|
| Own stdin/stdout, run the loop | harness | `harness.c` |
| Remember prior turns, decide what to forget | harness | `context.c` |
| Actually execute tools | harness | `tools.c` |
| Produce text, *request* tools | model | `model.c` |

`model.c` is a deterministic stand-in so that no network or API key is needed
and so the whole system is testable from a shell script.

## 2. Hard constraints

1. **Language:** C, conforming to C11. Must compile cleanly under
   `gcc -std=c11 -Wall -Wextra -pedantic` with **zero warnings**.
2. **Libraries:** the C standard library only — `<stdio.h>`, `<stdlib.h>`,
   `<string.h>`, `<ctype.h>`, `<stdarg.h>`, `<math.h>`. No third-party
   dependencies. No POSIX-only functions (this rules out `strdup` and
   `strcasestr`, which must be hand-written).
3. **Portability:** must build and run in a POSIX environment (Linux, WSL,
   macOS). No platform-specific headers.
4. **I/O:** line-oriented text on stdin/stdout only. No curses, no raw mode.
5. **Memory:** every heap allocation must have exactly one matching `free`.
   The program must be valgrind-clean at exit on every control path,
   including the EOF path.
6. **Failure behaviour:** no crash, no undefined behaviour, and no silent wrong
   answers on malformed input. An error must be *reported*, not guessed at.

## 3. State machine

```
                 +--------------------------------+
                 v                                |
   START --> READ --+-- "exit" | "quit" | EOF --> SHUTDOWN --> END
                    |
                    +-- "" (blank)     -----------> READ
                    +-- "/help"        -----------> META --> READ
                    +-- "/history"     -----------> META --> READ
                    |
                    +-- anything else  -----------> THINK
                                                     |
                          +-- reply has [[TOOL:..]] -+
                          |                          |
                          v                          v (no tool)
                         ACT ---------------------> SPEAK --> READ
```

* **START** — initialise an empty `Context`, print the banner and help.
* **READ** — print `you> `, flush stdout, read one line, strip `\n`, `\r` and
  trailing whitespace.
* **META** — `/help` and `/history` are *harness* commands. They are handled
  locally and are **never** added to the conversation history, because they are
  not part of the dialogue with the model.
* **THINK** — append the user's line as a `user` turn, call `model_generate`.
* **ACT** — the model emitted a tool marker. The harness parses it, executes the
  tool, prints both the call and the result for transparency, appends the result
  as a `tool` turn, and calls `model_generate` a **second** time so the model can
  answer with the value in context.
* **SPEAK** — append the reply as an `assistant` turn, print `model> <reply>`.
* **SHUTDOWN** — print the final context state, free every allocation,
  return `EXIT_SUCCESS`.

A turn therefore costs **2** context slots normally, and **3** when a tool runs.

## 4. Context management (Requirement 2)

**Data structure:** a fixed-size ring buffer of `CONTEXT_MAX_TURNS` (= 5) slots.

```c
typedef struct { Role role; char *text; } Turn;

typedef struct {
    Turn          turns[CONTEXT_MAX_TURNS];
    size_t        count;        /* live turns, 0..CONTEXT_MAX_TURNS */
    size_t        head;         /* slot holding the OLDEST live turn */
    unsigned long total_added;  /* lifetime counter, including evicted */
} Context;
```

**Why a ring buffer and not a linked list or `realloc`'d array:**
the memory ceiling is the whole point. A context window is *bounded*; a design
whose footprint grows with session length would model the problem wrongly. The
ring buffer makes the bound structural — five slots, forever — and makes
eviction O(1).

**Rules:**

* `context_add` copies the text onto the heap. The `Context` owns that copy.
* The copy is made **before** any slot is disturbed. If `malloc` fails the
  history is left exactly as it was, rather than destroying an old turn that
  cannot then be replaced.
* When the buffer is full, adding turn *n* frees the text of the oldest turn and
  advances `head`. This free is the leak-critical line of the program.
* `total_added` counts every turn ever added, including evicted ones. It exists
  so that the test script can prove eviction happened rather than merely
  observing that the count stopped rising.
* `context_at(ctx, 0)` is the **oldest** live turn; indexing is chronological,
  hiding the modular arithmetic from callers.
* `context_free` is idempotent and safe on a zeroed `Context`.

**Observable contract** (asserted by `test.sh`, so the wording is frozen):

```
[context] <N> turn(s) held (max 5), <M> total added
  1. user      | <text>
  2. assistant | <text>
```

## 5. Tool execution (Requirement 3)

**Protocol.** The model never executes anything. When it wants a computation it
emits a marker inside its ordinary text output:

```
[[TOOL:<name>:<argument>]]
```

The harness scans each reply for this marker. This mirrors how real function
calling works — the model emits a *request*, and the surrounding program decides
whether and how to honour it. Keeping execution on the harness side is what makes
the trust boundary explicit.

**Registry.** `tool_dispatch(name, arg, out, outsz)` routes by name. Adding a
tool means writing one function with that signature and adding one line to the
dispatch table; `harness.c` does not change.

**The `calc` tool.** A recursive-descent parser over:

```
expr   := term   { ('+' | '-') term }
term   := factor { ('*' | '/') factor }
factor := ('+' | '-') factor | '(' expr ')' | NUMBER
```

`atof`/`sscanf` are explicitly rejected: they cannot express operator precedence,
so `2+3*4` would come out as 20. Correctness is the entire justification for
having a tool at all — a calculator that guesses is worse than no calculator.

Required behaviour:

| Input | Result | Why it is tested |
|---|---|---|
| `2+3*4` | `14` | precedence |
| `(2+3)*4` | `20` | parentheses |
| `100 - 20 - 30` | `50` | left associativity |
| `10/4` | `2.5` | decimal output |
| `-3 + 10` | `7` | unary minus |
| `7/0` | `error: division by zero` | must not produce `inf` or crash |
| `2+banana` | `error: cannot parse expression` | trailing junk is an error |

Integral results print without a decimal tail (`14`, not `14.000000`).

**Trigger conditions.** The mock model requests `calc` when the user's line
starts with `calc ` or `/calc `, contains `calculate `, contains `what is `
followed by an arithmetic expression, or consists purely of digits, operators,
parentheses, dots and spaces.

## 6. The mock model (Requirement 1)

`model_generate(const Context *ctx)` returns a **heap-allocated** string that the
caller must free. It is a pure function of the context — same context in, same
text out — which is precisely what makes `test.sh` able to assert on exact
strings.

Dispatch order (first match wins):

1. Last turn is a `tool` turn → report the value: `The answer is <v>.`
   If the value begins with `error:`, report the failure instead. The model must
   **not** invent a number when the tool failed.
2. The user's line requests a calculation → emit `[[TOOL:calc:<expr>]]`.
3. The line contains `hello`, `hi` or `hey` **as a whole word** → greeting.
   Whole-word matching is required: `shell` must not trigger the greeting.
4. The line contains `who are you` / `what are you` → identity response.
5. Otherwise → `You said: "<line>" (<n> turn(s) in context)`.

Returning ownership of the string (rather than writing into a caller buffer) is
deliberate: reply length is not knowable in advance, and it puts a real
allocate/free pair on the hot path for the leak test to exercise.

## 7. Input handling

* Buffer size `INPUT_MAX` = 1024. Input is read with `fgets`, never `gets`.
* A line longer than the buffer is **truncated**, and the remainder is drained
  with `fgetc` so the tail is not misread as the next command.
* Trailing `\r` is stripped, so a CRLF file piped in from Windows still matches
  `exit`.
* Blank lines are ignored without touching the context.
* stdout is flushed after every prompt. When stdin is a pipe, stdout is fully
  buffered, and an unflushed prompt would appear out of order in the captured
  transcript — which would break the test script.

## 8. Testing (Requirement 5)

`test.sh` drives the compiled binary through pipes and asserts on its output.
It must cover:

* **Core loop** — greeting; echo fallback; `exit` and `quit`; exit status 0;
  EOF with no `exit` typed; blank lines ignored; `shell` not matching `hello`.
* **Tools** — every row of the table in §5, plus: the tool result is stored as a
  `tool` turn, and no answer is fabricated after a tool error.
* **State management** — seven plain exchanges add 14 turns; the final dump must
  report `5 turn(s) held (max 5), 14 total added`, must contain `alpha7`, and
  must **not** contain `alpha1` or `alpha2`. The assertion reads only the final
  dump, so earlier echoes in the running transcript cannot make it pass by
  accident. Also: a short session is not padded to 5; `/help` consumes no slot;
  a tool exchange costs 3 slots; a 3000-character line does not corrupt the loop.
* **Memory** — a session that fills the window past capacity, runs a successful
  tool and a failing tool, then exits, run under `valgrind --leak-check=full`
  with `--error-exitcode=99`. If valgrind is absent, fall back to a
  `-fsanitize=address,undefined` build. If neither is available, report SKIP
  loudly — never a silent pass.

Exit code 0 iff every assertion passed.

## 9. Out of scope

No networking or real API calls; no multi-line input; no persistence between
runs; no configuration file; no concurrency; no Unicode-aware text handling
beyond byte-level pass-through.

## 10. Traceability

| Spec requirement | Where it lives | Proven by |
|---|---|---|
| 1. Core loop + mock model | `harness.c:main`, `model.c` | test.sh §1 |
| 2. Context management | `context.c` | test.sh §3 |
| 3. Tool execution | `tools.c`, `harness.c:run_turn` | test.sh §2 |
| 4. Vibe coding log | `vibe_coding_log.md` | — |
| 5. AI-generated testing | `test.sh` | test.sh §4 |
