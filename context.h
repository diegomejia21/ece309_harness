/* =====================================================================
 * context.h -- Conversation memory for the ECE 309 mini-harness.
 *
 * ARCHITECTURAL ROLE
 *   A real LLM has no memory between calls. The *harness* is what gives
 *   the illusion of memory: it stores the recent turns and replays them
 *   to the model on every request. Because a model's context window is
 *   finite, the harness must also decide what to THROW AWAY.
 *
 *   This module implements that policy as a fixed-size ring buffer that
 *   keeps only the most recent CONTEXT_MAX_TURNS turns. Every turn owns
 *   a heap copy of its text, so eviction must free it -- this is the
 *   part the memory-leak test in test.sh exercises.
 * ===================================================================== */
#ifndef CONTEXT_H
#define CONTEXT_H

#include <stdio.h>
#include <stddef.h>

/* Requirement 2 of the spec: "a minimal conversation history
 * (e.g., the last 5 turns)". Change this one number to resize memory. */
#define CONTEXT_MAX_TURNS 5

/* Who produced a turn. ROLE_TOOL is what makes this a *harness* rather
 * than a chat program: tool output is a first-class participant that
 * gets fed back into the model on the next pass. */
typedef enum {
    ROLE_USER = 0,
    ROLE_ASSISTANT,
    ROLE_TOOL
} Role;

typedef struct {
    Role  role;
    char *text;   /* owned by the Context; freed on evict/free */
} Turn;

typedef struct {
    Turn          turns[CONTEXT_MAX_TURNS];
    size_t        count;        /* live turns, always <= CONTEXT_MAX_TURNS */
    size_t        head;         /* slot index of the OLDEST live turn      */
    unsigned long total_added;  /* lifetime counter, incl. evicted turns   */
} Context;

/* Put a Context into a valid empty state. Must be called first. */
void context_init(Context *ctx);

/* Append a turn, copying `text`. Evicts (and frees) the oldest turn once
 * the buffer is full. Returns 0 on success, -1 if allocation failed. */
int context_add(Context *ctx, Role role, const char *text);

/* Number of turns currently held (0..CONTEXT_MAX_TURNS). */
size_t context_count(const Context *ctx);

/* Turn `i` in chronological order: i == 0 is the OLDEST live turn.
 * Returns NULL if `i` is out of range. Caller must not free the result. */
const Turn *context_at(const Context *ctx, size_t i);

/* Most recent turn, or NULL when the history is empty. */
const Turn *context_last(const Context *ctx);

/* Human-readable dump used by the `/history` meta-command and by the
 * automated state-management assertions in test.sh. */
void context_print(const Context *ctx, FILE *out);

/* Release every heap allocation the Context owns and reset it. Safe to
 * call twice (it is idempotent). */
void context_free(Context *ctx);

/* "user" / "assistant" / "tool" -- for printing. */
const char *role_name(Role r);

#endif /* CONTEXT_H */
