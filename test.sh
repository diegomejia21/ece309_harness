#!/usr/bin/env bash
# =====================================================================
# test.sh -- Automated verification for the ECE 309 mini-harness.
#
# Covers the two things the project spec asks a test script to prove:
#   (a) STATE MANAGEMENT -- the 5-turn context window really evicts, and
#       the tool-execution path really runs, deterministically, with no
#       human at the keyboard.
#   (b) MEMORY SAFETY    -- no leaks. Uses valgrind when available and
#       falls back to an AddressSanitizer/LeakSanitizer build.
#
# Usage:  bash test.sh
# Exit code 0 means every assertion passed.
# =====================================================================

set -u

BIN=./harness
PASS=0
FAIL=0
SKIP=0

green() { printf '\033[32m%s\033[0m' "$1"; }
red()   { printf '\033[31m%s\033[0m' "$1"; }
yellow(){ printf '\033[33m%s\033[0m' "$1"; }

ok()   { PASS=$((PASS+1)); printf '  [%s] %s\n' "$(green PASS)" "$1"; }
bad()  { FAIL=$((FAIL+1)); printf '  [%s] %s\n' "$(red FAIL)" "$1"
         if [ $# -gt 1 ]; then printf '        %s\n' "$2"; fi; }
skip() { SKIP=$((SKIP+1)); printf '  [%s] %s\n' "$(yellow SKIP)" "$1"; }

# assert_contains <label> <haystack> <needle>
assert_contains() {
  if printf '%s\n' "$2" | grep -qF -- "$3"; then
    ok "$1"
  else
    bad "$1" "expected to find: $3"
  fi
}

# assert_absent <label> <haystack> <needle>
assert_absent() {
  if printf '%s\n' "$2" | grep -qF -- "$3"; then
    bad "$1" "did NOT expect to find: $3"
  else
    ok "$1"
  fi
}

# feed <input-with-\n-escapes>  -> prints the harness output
feed() { printf '%b' "$1" | "$BIN" 2>&1; }

# ---------------------------------------------------------------------
echo "== 0. Build =========================================================="
if make --version >/dev/null 2>&1; then
  make -s harness || { echo "build failed"; exit 1; }
else
  gcc -std=c11 -Wall -Wextra -pedantic -O2 -o harness \
      harness.c context.c model.c tools.c -lm || { echo "build failed"; exit 1; }
fi
[ -x "$BIN" ] && ok "harness compiled" || { bad "harness compiled"; exit 1; }

# ---------------------------------------------------------------------
echo
echo "== 1. Core loop: greeting, echo, safe shutdown ======================="

out=$(feed 'hello\nexit\n')
assert_contains "greeting fires on 'hello'"        "$out" "Hello! I am a mock model"
assert_contains "shutdown message on 'exit'"       "$out" "[harness] shutting down"

status=$(printf '%b' 'hello\nexit\n' | "$BIN" >/dev/null 2>&1; echo $?)
[ "$status" = "0" ] && ok "exit status is 0" || bad "exit status is 0" "got $status"

out=$(feed 'this is a shell\nexit\n')
assert_contains "unknown input is echoed back"     "$out" 'You said: "this is a shell"'
assert_absent   "'shell' does not trigger 'hello'" "$out" "Hello! I am a mock model"

out=$(feed 'quit\n')
assert_contains "'quit' also shuts down"           "$out" "[harness] shutting down"

# EOF with no 'exit' typed must still shut down cleanly.
out=$(printf 'hello' | "$BIN" 2>&1)
assert_contains "EOF ends the loop cleanly"        "$out" "[harness] end of input"
assert_contains "EOF still frees the context"      "$out" "[harness] goodbye"

# Blank lines must not be recorded as turns.
out=$(feed '\n\n\n/history\nexit\n')
assert_contains "blank lines are ignored"          "$out" "0 turn(s) held"

# ---------------------------------------------------------------------
echo
echo "== 2. Tool execution ================================================="

out=$(feed 'calc 2+3*4\nexit\n')
assert_contains "harness announces the tool call"  "$out" "[harness] tool call: calc(2+3*4)"
assert_contains "operator precedence: 2+3*4 = 14"  "$out" "The answer is 14."

out=$(feed 'calc (2+3)*4\nexit\n')
assert_contains "parentheses: (2+3)*4 = 20"        "$out" "The answer is 20."

out=$(feed 'what is 10/4\nexit\n')
assert_contains "decimal division: 10/4 = 2.5"     "$out" "The answer is 2.5."

out=$(feed 'calc -3 + 10\nexit\n')
assert_contains "unary minus: -3+10 = 7"           "$out" "The answer is 7."

out=$(feed '100 - 20 - 30\nexit\n')
assert_contains "bare arithmetic is detected"      "$out" "The answer is 50."

out=$(feed 'calc 7/0\nexit\n')
assert_contains "division by zero is caught"       "$out" "error: division by zero"
assert_absent   "no bogus answer after a tool error" "$out" "The answer is"

out=$(feed 'calc 2+banana\nexit\n')
assert_contains "malformed expression is rejected" "$out" "error: cannot parse expression"

# The tool result must land in context as its own 'tool' turn.
out=$(feed 'calc 1+1\n/history\nexit\n')
assert_contains "tool result stored as a tool turn" "$out" "tool      | 2"

# ---------------------------------------------------------------------
echo
echo "== 3. State management: the 5-turn context window ===================="

# Seven plain exchanges = 14 turns added (user + assistant each time).
# Only the newest 5 may survive.
out=$(feed 'alpha1\nalpha2\nalpha3\nalpha4\nalpha5\nalpha6\nalpha7\nexit\n')

# Look only at the FINAL dump, so earlier echoes of alpha1 in the running
# transcript cannot make the eviction check pass by accident.
final=$(printf '%s\n' "$out" | sed -n '/\[harness\] final context state:/,$p')

assert_contains "window is capped at 5 turns"   "$final" "5 turn(s) held (max 5)"
assert_contains "lifetime counter reached 14"   "$final" "14 total added"
assert_contains "newest turn is retained"       "$final" "alpha7"
assert_absent   "oldest turn was evicted"       "$final" "alpha1"
assert_absent   "second-oldest was evicted"     "$final" "alpha2"

# A short session must NOT be padded out to 5.
out=$(feed 'alpha1\n/history\nexit\n')
assert_contains "partial window reports 2 turns" "$out" "2 turn(s) held (max 5)"

# Meta-commands must not consume context slots.
out=$(feed '/help\n/history\nexit\n')
assert_contains "/help is not stored as a turn"  "$out" "0 turn(s) held"

# A tool exchange costs three turns: user + tool + assistant.
out=$(feed 'calc 1+1\n/history\nexit\n')
assert_contains "tool turn counts toward context" "$out" "3 total added"

# Over-long input must be truncated, not overflow, and the loop survives.
long=$(printf 'x%.0s' $(seq 1 3000))
out=$(printf '%s\nexit\n' "$long" | "$BIN" 2>&1)
assert_contains "3000-char line survives"        "$out" "[harness] shutting down"
assert_contains "over-long line did not corrupt loop" "$out" "[harness] goodbye"

# ---------------------------------------------------------------------
echo
echo "== 4. Memory safety =================================================="

# A session that exercises every allocation path: fills the window past
# capacity (forcing evictions), runs tools, and shuts down normally.
LEAK_INPUT='hello\ncalc 2+3*4\ncalc 7/0\none\ntwo\nthree\nfour\nfive\nsix\nwho are you\n/history\nexit\n'

if command -v valgrind >/dev/null 2>&1; then
  vout=$(printf '%b' "$LEAK_INPUT" | valgrind --leak-check=full \
           --errors-for-leak-kinds=definite,indirect \
           --error-exitcode=99 "$BIN" 2>&1 >/dev/null)
  vstatus=$?
  if [ "$vstatus" = "99" ]; then
    bad "valgrind: no leaks or memory errors" "$(printf '%s\n' "$vout" | tail -20)"
  else
    ok "valgrind: no leaks or memory errors"
  fi
  if printf '%s\n' "$vout" | grep -q "All heap blocks were freed"; then
    ok "valgrind: all heap blocks freed"
  elif printf '%s\n' "$vout" | grep -q "definitely lost: 0 bytes"; then
    ok "valgrind: 0 bytes definitely lost"
  else
    skip "valgrind: heap summary not recognised"
  fi

elif gcc -fsanitize=address -x c /dev/null -o /dev/null 2>/dev/null; then
  # Fallback: build with AddressSanitizer + LeakSanitizer.
  gcc -std=c11 -Wall -Wextra -g -fsanitize=address,undefined \
      -o harness_asan harness.c context.c model.c tools.c -lm 2>/dev/null
  aout=$(printf '%b' "$LEAK_INPUT" | ASAN_OPTIONS=detect_leaks=1 ./harness_asan 2>&1 >/dev/null)
  astatus=$?
  if [ "$astatus" = "0" ] && ! printf '%s\n' "$aout" | grep -q "LeakSanitizer\|ERROR: AddressSanitizer"; then
    ok "AddressSanitizer/LeakSanitizer: clean"
  else
    bad "AddressSanitizer/LeakSanitizer: clean" "$(printf '%s\n' "$aout" | head -20)"
  fi

else
  skip "valgrind / AddressSanitizer not available on this machine"
  echo "        For the strongest check:  sudo apt install valgrind  (Linux/WSL)"
fi

# Portable fallback that ALWAYS runs: the -DHARNESS_MEMCHECK build routes
# every project malloc/free through counting wrappers and prints a ledger
# at exit. Weaker than valgrind -- it cannot see invalid reads or writes --
# but it answers the leak question directly and needs no external tool, so
# the memory group is never left with zero evidence.
if gcc -std=c11 -Wall -Wextra -pedantic -g -DHARNESS_MEMCHECK        -o harness_memcheck harness.c context.c model.c tools.c memcheck.c -lm 2>/dev/null; then

  # Exercise every allocating path: greeting, a tool success, a tool
  # failure, enough turns to force evictions, then a normal shutdown.
  ledger=$(printf '%b' "$LEAK_INPUT" | ./harness_memcheck 2>&1 >/dev/null)
  assert_contains "memcheck: every allocation freed" "$ledger" "outstanding=0"
  assert_contains "memcheck: no bad or double frees"  "$ledger" "bad_frees=0"
  assert_contains "memcheck: verdict OK"              "$ledger" "OK: every allocation was freed"

  # The EOF path (no 'exit' typed) is a separate control path and must
  # free the context too.
  ledger=$(printf 'hello
calc 1+1' | ./harness_memcheck 2>&1 >/dev/null)
  assert_contains "memcheck: EOF path frees context"  "$ledger" "outstanding=0"

  # Empty session: nothing allocated, nothing leaked.
  ledger=$(printf '' | ./harness_memcheck 2>&1 >/dev/null)
  assert_contains "memcheck: empty session is clean"  "$ledger" "outstanding=0"

  # Heavy eviction: 50 turns through a 5-slot window frees ~45 texts.
  many=$(awk 'BEGIN{for(i=1;i<=50;i++) print "msg" i; print "exit"}')
  ledger=$(printf '%s
' "$many" | ./harness_memcheck 2>&1 >/dev/null)
  assert_contains "memcheck: 50-turn eviction is clean" "$ledger" "outstanding=0"
else
  bad "memcheck build" "could not compile the -DHARNESS_MEMCHECK variant"
fi

# ---------------------------------------------------------------------
echo
echo "======================================================================"
printf 'passed: %s   failed: %s   skipped: %s\n' "$PASS" "$FAIL" "$SKIP"
if [ "$FAIL" -eq 0 ]; then
  printf '%s\n' "$(green 'ALL TESTS PASSED')"
  exit 0
else
  printf '%s\n' "$(red 'TESTS FAILED')"
  exit 1
fi
