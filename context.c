/* =====================================================================
 * context.c -- Ring-buffer conversation memory. See context.h.
 * Standard C only: <stdio.h>, <stdlib.h>, <string.h>.
 * ===================================================================== */
#include "context.h"

#include <stdlib.h>
#include <string.h>

/* strdup() is POSIX, not standard C, so we roll our own to keep the
 * build clean under -std=c11 -Wall -Wextra -pedantic. */
static char *xstrdup(const char *s)
{
    size_t n;
    char  *copy;

    if (s == NULL) {
        s = "";
    }
    n = strlen(s) + 1;          /* +1 for the terminating '\0' */
    copy = (char *)malloc(n);
    if (copy == NULL) {
        return NULL;            /* caller decides how to fail */
    }
    memcpy(copy, s, n);
    return copy;
}

void context_init(Context *ctx)
{
    size_t i;

    if (ctx == NULL) {
        return;
    }
    for (i = 0; i < (size_t)CONTEXT_MAX_TURNS; i++) {
        ctx->turns[i].role = ROLE_USER;
        ctx->turns[i].text = NULL;
    }
    ctx->count       = 0;
    ctx->head        = 0;
    ctx->total_added = 0;
}

int context_add(Context *ctx, Role role, const char *text)
{
    char  *copy;
    size_t slot;

    if (ctx == NULL) {
        return -1;
    }

    /* Copy FIRST. If the allocation fails we leave the history untouched
     * rather than destroying an old turn we cannot replace. */
    copy = xstrdup(text);
    if (copy == NULL) {
        return -1;
    }

    if (ctx->count < (size_t)CONTEXT_MAX_TURNS) {
        /* Still filling up: the next free slot sits after the live ones. */
        slot = (ctx->head + ctx->count) % (size_t)CONTEXT_MAX_TURNS;
        ctx->count++;
    } else {
        /* Full: overwrite the oldest turn and slide the window forward.
         * Freeing here is what keeps the harness leak-free over a long
         * session -- exactly what test.sh checks with valgrind/ASan. */
        slot = ctx->head;
        free(ctx->turns[slot].text);
        ctx->turns[slot].text = NULL;
        ctx->head = (ctx->head + 1) % (size_t)CONTEXT_MAX_TURNS;
    }

    ctx->turns[slot].role = role;
    ctx->turns[slot].text = copy;
    ctx->total_added++;
    return 0;
}

size_t context_count(const Context *ctx)
{
    return (ctx == NULL) ? 0 : ctx->count;
}

const Turn *context_at(const Context *ctx, size_t i)
{
    size_t slot;

    if (ctx == NULL || i >= ctx->count) {
        return NULL;
    }
    slot = (ctx->head + i) % (size_t)CONTEXT_MAX_TURNS;
    return &ctx->turns[slot];
}

const Turn *context_last(const Context *ctx)
{
    if (ctx == NULL || ctx->count == 0) {
        return NULL;
    }
    return context_at(ctx, ctx->count - 1);
}

void context_print(const Context *ctx, FILE *out)
{
    size_t i;

    if (ctx == NULL || out == NULL) {
        return;
    }

    /* This exact line is asserted by test.sh -- keep the wording stable. */
    fprintf(out, "[context] %lu turn(s) held (max %d), %lu total added\n",
            (unsigned long)ctx->count,
            CONTEXT_MAX_TURNS,
            ctx->total_added);

    if (ctx->count == 0) {
        fprintf(out, "  (empty)\n");
        return;
    }
    for (i = 0; i < ctx->count; i++) {
        const Turn *t = context_at(ctx, i);
        fprintf(out, "  %lu. %-9s | %s\n",
                (unsigned long)(i + 1), role_name(t->role), t->text);
    }
}

void context_free(Context *ctx)
{
    size_t i;

    if (ctx == NULL) {
        return;
    }
    for (i = 0; i < (size_t)CONTEXT_MAX_TURNS; i++) {
        free(ctx->turns[i].text);   /* free(NULL) is defined and safe */
        ctx->turns[i].text = NULL;
    }
    ctx->count = 0;
    ctx->head  = 0;
    /* total_added is deliberately preserved: it is a lifetime statistic. */
}

const char *role_name(Role r)
{
    switch (r) {
    case ROLE_USER:      return "user";
    case ROLE_ASSISTANT: return "assistant";
    case ROLE_TOOL:      return "tool";
    default:             return "unknown";
    }
}
