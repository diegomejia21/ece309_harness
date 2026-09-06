/* =====================================================================
 * tools.h -- The tool layer of the harness.
 *
 * ARCHITECTURAL ROLE
 *   Language models predict text; they do not compute. Arithmetic is the
 *   canonical example of a task an LLM should DELEGATE. The harness owns
 *   the actual execution: it parses the model's tool request, runs real
 *   deterministic C code, and hands the result back as a tool turn.
 *
 *   Every tool has the same signature so that tool_dispatch() can route
 *   by name -- adding a tool means writing one function and one line in
 *   the dispatch table, not touching the main loop.
 * ===================================================================== */
#ifndef TOOLS_H
#define TOOLS_H

#include <stddef.h>

/* Plenty for a formatted number or an error sentence. */
#define TOOL_OUTPUT_MAX 256

/* Evaluate an infix arithmetic expression and write a human-readable
 * result into `out`.
 *
 * Supports: + - * / , parentheses, unary +/-, decimals, and whitespace.
 * Operator precedence and left-associativity are honoured, so
 * "2+3*4" is 14, not 20.
 *
 * Returns 0 on success. On failure returns -1 and writes an
 * "error: ..." message into `out` (never left uninitialised). */
int tool_calc(const char *expr, char *out, size_t outsz);

/* Route a named tool call to its implementation.
 * Returns 0 on success, -1 on tool error or unknown tool name. */
int tool_dispatch(const char *name, const char *arg, char *out, size_t outsz);

#endif /* TOOLS_H */
