/* =====================================================================
 * model.c -- Deterministic stand-in for an LLM. See model.h.
 * ===================================================================== */
#include "model.h"
#include "tools.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>

#define TOOL_MARKER_OPEN  "[[TOOL:"
#define TOOL_MARKER_CLOSE "]]"

/* ---------------------------------------------------------------------
 * Small string helpers
 * ------------------------------------------------------------------- */

/* printf-style allocation. Measures once, allocates, formats once.
 * Returns NULL on failure; the caller must free the result. */
static char *msprintf(const char *fmt, ...)
{
    va_list ap;
    int     n;
    char   *buf;

    va_start(ap, fmt);
    n = vsnprintf(NULL, 0, fmt, ap);   /* C99: returns the length needed */
    va_end(ap);
    if (n < 0) {
        return NULL;
    }

    buf = (char *)malloc((size_t)n + 1);
    if (buf == NULL) {
        return NULL;
    }

    va_start(ap, fmt);
    vsnprintf(buf, (size_t)n + 1, fmt, ap);
    va_end(ap);
    return buf;
}

/* Case-insensitive substring search (strcasestr is not standard C). */
static const char *find_ci(const char *hay, const char *needle)
{
    size_t nlen = strlen(needle);
    size_t i;

    if (nlen == 0) {
        return hay;
    }
    for (i = 0; hay[i] != 0; i++) {
        size_t j = 0;
        while (j < nlen &&
               tolower((unsigned char)hay[i + j]) == tolower((unsigned char)needle[j])) {
            j++;
        }
        if (j == nlen) {
            return hay + i;
        }
    }
    return NULL;
}

/* True when word appears in s as a whole word, ignoring case.
 * Prevents "this" from matching "hi", or "shell" from matching "hello". */
static int contains_word(const char *s, const char *word)
{
    const char *p = s;
    size_t      wlen = strlen(word);

    while ((p = find_ci(p, word)) != NULL) {
        int left_ok  = (p == s) || !isalnum((unsigned char)p[-1]);
        int right_ok = !isalnum((unsigned char)p[wlen]);
        if (left_ok && right_ok) {
            return 1;
        }
        p += 1;
    }
    return 0;
}

/* Case-insensitive prefix test. Returns the rest of the string on a
 * match, or NULL. */
static const char *skip_prefix_ci(const char *s, const char *prefix)
{
    size_t n = strlen(prefix);
    size_t i;

    for (i = 0; i < n; i++) {
        if (tolower((unsigned char)s[i]) != tolower((unsigned char)prefix[i])) {
            return NULL;
        }
    }
    return s + n;
}

/* Does the whole line read as bare arithmetic, e.g. 2 + 3*4 ?
 * Requires at least one digit and one operator, and nothing else beyond
 * digits, operators, parentheses, dots and spaces. */
static int is_bare_arithmetic(const char *s)
{
    int has_digit = 0;
    int has_op    = 0;
    size_t i;

    for (i = 0; s[i] != 0; i++) {
        char c = s[i];
        if (isdigit((unsigned char)c)) {
            has_digit = 1;
        } else if (c == '+' || c == '-' || c == '*' || c == '/') {
            has_op = 1;
        } else if (c != '(' && c != ')' && c != '.' && c != ' ' && c != '\t') {
            return 0;           /* a letter or punctuation -> plain text */
        }
    }
    return has_digit && has_op;
}

/* Decide whether the user text is a request for the calculator, and if
 * so point *expr_out at the expression portion. */
static int wants_calculation(const char *text, const char **expr_out)
{
    const char *rest;

    while (*text == ' ' || *text == '\t') {
        text++;
    }

    rest = skip_prefix_ci(text, "/calc ");
    if (rest == NULL) {
        rest = skip_prefix_ci(text, "calc ");
    }
    if (rest == NULL) {
        const char *kw = find_ci(text, "calculate ");
        if (kw != NULL) {
            rest = kw + strlen("calculate ");
        }
    }
    if (rest == NULL) {
        const char *kw = find_ci(text, "what is ");
        if (kw != NULL && is_bare_arithmetic(kw + strlen("what is "))) {
            rest = kw + strlen("what is ");
        }
    }
    if (rest == NULL && is_bare_arithmetic(text)) {
        rest = text;
    }

    if (rest == NULL) {
        return 0;
    }
    while (*rest == ' ' || *rest == '\t') {
        rest++;
    }
    if (*rest == 0) {
        return 0;
    }
    *expr_out = rest;
    return 1;
}

/* ---------------------------------------------------------------------
 * The mock model itself
 * ------------------------------------------------------------------- */
char *model_generate(const Context *ctx)
{
    const Turn *last;
    const char *expr = NULL;

    last = context_last(ctx);
    if (last == NULL) {
        return msprintf("(no input yet)");
    }

    /* --- Second pass: the harness has just handed us a tool result. ---
     * A real model would re-read the whole transcript here; the mock
     * simply reports the value it was given. */
    if (last->role == ROLE_TOOL) {
        if (strncmp(last->text, "error:", 6) == 0) {
            return msprintf("The calculator tool failed -- %s", last->text + 7);
        }
        return msprintf("The answer is %s.", last->text);
    }

    /* --- First pass: react to the user message. --- */

    /* 1. Delegate arithmetic. The model only ASKS; harness.c executes. */
    if (wants_calculation(last->text, &expr)) {
        return msprintf("I should not do arithmetic in my head. "
                        TOOL_MARKER_OPEN "calc:%s" TOOL_MARKER_CLOSE, expr);
    }

    /* 2. Greeting. */
    if (contains_word(last->text, "hello") ||
        contains_word(last->text, "hi")    ||
        contains_word(last->text, "hey")) {
        return msprintf("Hello! I am a mock model running inside a C harness. "
                        "Ask me to calculate something, or type exit to quit.");
    }

    /* 3. Identity. */
    if (find_ci(last->text, "who are you") != NULL ||
        find_ci(last->text, "what are you") != NULL) {
        return msprintf("I am a deterministic mock model. My harness keeps the "
                        "last %d turns of our conversation and runs my tool calls "
                        "for me.", CONTEXT_MAX_TURNS);
    }

    /* 4. Fallback: echo, and expose how much memory the harness is holding
     *    so the behaviour of the context window is visible to the user. */
    return msprintf("You said: \"%s\" (%lu turn(s) in context)",
                    last->text, (unsigned long)context_count(ctx));
}

int model_extract_tool_call(const char *response,
                            char *name, size_t namesz,
                            char *arg,  size_t argsz)
{
    const char *open;
    const char *sep;
    const char *close;
    size_t      n;

    if (response == NULL || name == NULL || arg == NULL ||
        namesz == 0 || argsz == 0) {
        return 0;
    }

    open = strstr(response, TOOL_MARKER_OPEN);
    if (open == NULL) {
        return 0;
    }
    open += strlen(TOOL_MARKER_OPEN);

    close = strstr(open, TOOL_MARKER_CLOSE);
    if (close == NULL) {
        return 0;               /* unterminated marker -> not a tool call */
    }

    sep = memchr(open, ':', (size_t)(close - open));
    if (sep == NULL) {
        return 0;               /* no argument separator */
    }

    /* Copy the name, truncating safely rather than overflowing. */
    n = (size_t)(sep - open);
    if (n >= namesz) {
        n = namesz - 1;
    }
    memcpy(name, open, n);
    name[n] = 0;

    /* Copy the argument. */
    n = (size_t)(close - (sep + 1));
    if (n >= argsz) {
        n = argsz - 1;
    }
    memcpy(arg, sep + 1, n);
    arg[n] = 0;

    return 1;
}
