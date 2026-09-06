/* =====================================================================
 * model.h -- The MOCK model.
 *
 * ARCHITECTURAL ROLE
 *   This file stands in for an LLM API call. The spec is explicit that no
 *   network calls are needed, so model_generate() is a deterministic
 *   function of the conversation context -- which is exactly what makes
 *   the harness testable from a shell script.
 *
 *   The important design point: this module NEVER executes anything. When
 *   it wants arithmetic done it emits a tool-call marker in its text:
 *
 *       [[TOOL:calc:2+3*4]]
 *
 *   and it is the harness (harness.c) that notices the marker, runs the
 *   tool, appends the result as a ROLE_TOOL turn, and calls the model a
 *   second time. That separation is the whole lesson of the project.
 * ===================================================================== */
#ifndef MODEL_H
#define MODEL_H

#include <stddef.h>
#include "context.h"

/* Produce the model's reply to the current conversation.
 * Returns a heap-allocated string the CALLER MUST free(), or NULL if
 * allocation failed. */
char *model_generate(const Context *ctx);

/* Look for a [[TOOL:name:argument]] marker in `response`.
 * On success writes the tool name and argument into the caller's buffers
 * and returns 1. Returns 0 when the response contains no tool call. */
int model_extract_tool_call(const char *response,
                            char *name, size_t namesz,
                            char *arg,  size_t argsz);

#endif /* MODEL_H */
