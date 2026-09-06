/* =====================================================================
 * tools.c -- Deterministic tool implementations.
 *
 * The calculator is a textbook recursive-descent parser over this grammar:
 *
 *   expr   := term   { ('+' | '-') term }
 *   term   := factor { ('*' | '/') factor }
 *   factor := ('+' | '-') factor | '(' expr ')' | NUMBER
 *
 * Recursive descent is used instead of atof()/sscanf() because the whole
 * point of the tool is that it must be RIGHT: precedence, associativity
 * and division-by-zero all have to behave correctly.
 * ===================================================================== */
#include "tools.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

/* Parser state threaded through the recursive functions. `err` is set
 * once and then short-circuits the rest of the parse. */
typedef struct {
    const char *p;      /* current read position */
    int         err;    /* 0 = ok, 1 = syntax, 2 = divide by zero */
} Parser;

static double parse_expr(Parser *ps);   /* forward declaration */

static void skip_spaces(Parser *ps)
{
    while (*ps->p == ' ' || *ps->p == '\t') {
        ps->p++;
    }
}

/* factor := ('+'|'-') factor | '(' expr ')' | NUMBER */
static double parse_factor(Parser *ps)
{
    double value = 0.0;
    int    digits = 0;

    skip_spaces(ps);

    /* Unary sign, e.g. "-3" or "--3". */
    if (*ps->p == '+') {
        ps->p++;
        return parse_factor(ps);
    }
    if (*ps->p == '-') {
        ps->p++;
        return -parse_factor(ps);
    }

    /* Parenthesised sub-expression. */
    if (*ps->p == '(') {
        ps->p++;
        value = parse_expr(ps);
        skip_spaces(ps);
        if (*ps->p == ')') {
            ps->p++;
        } else {
            ps->err = 1;        /* unbalanced parenthesis */
        }
        return value;
    }

    /* A number: integer part, optional fractional part. Parsed by hand so
     * we know exactly how many digits we consumed. */
    while (isdigit((unsigned char)*ps->p)) {
        value = value * 10.0 + (double)(*ps->p - '0');
        ps->p++;
        digits++;
    }
    if (*ps->p == '.') {
        double scale = 0.1;
        ps->p++;
        while (isdigit((unsigned char)*ps->p)) {
            value += (double)(*ps->p - '0') * scale;
            scale *= 0.1;
            ps->p++;
            digits++;
        }
    }
    if (digits == 0) {
        ps->err = 1;            /* expected a number, found something else */
    }
    return value;
}

/* term := factor { ('*'|'/') factor } */
static double parse_term(Parser *ps)
{
    double left = parse_factor(ps);

    for (;;) {
        skip_spaces(ps);
        if (*ps->p == '*') {
            ps->p++;
            left *= parse_factor(ps);
        } else if (*ps->p == '/') {
            double right;
            ps->p++;
            right = parse_factor(ps);
            /* The reason a tool beats an LLM: this case is handled, not guessed. */
            if (right == 0.0) {
                ps->err = 2;
                return 0.0;
            }
            left /= right;
        } else {
            return left;
        }
    }
}

/* expr := term { ('+'|'-') term } */
static double parse_expr(Parser *ps)
{
    double left = parse_term(ps);

    for (;;) {
        skip_spaces(ps);
        if (*ps->p == '+') {
            ps->p++;
            left += parse_term(ps);
        } else if (*ps->p == '-') {
            ps->p++;
            left -= parse_term(ps);
        } else {
            return left;
        }
    }
}

/* Print 14 as "14" but 4.5 as "4.5" -- integral results should not carry
 * a spurious ".000000" tail. */
static void format_number(double v, char *out, size_t outsz)
{
    double rounded = (v < 0.0) ? -floor(-v + 0.5) : floor(v + 0.5);

    if (fabs(v - rounded) < 1e-9 && fabs(v) < 1e15) {
        snprintf(out, outsz, "%.0f", rounded);
    } else {
        snprintf(out, outsz, "%.10g", v);
    }
}

int tool_calc(const char *expr, char *out, size_t outsz)
{
    Parser ps;
    double value;
    char   num[64];

    if (out == NULL || outsz == 0) {
        return -1;
    }
    if (expr == NULL || *expr == '\0') {
        snprintf(out, outsz, "error: empty expression");
        return -1;
    }

    ps.p   = expr;
    ps.err = 0;
    value  = parse_expr(&ps);

    skip_spaces(&ps);
    /* Trailing junk ("2+3 banana") is a syntax error, not a silent pass. */
    if (ps.err == 0 && *ps.p != '\0') {
        ps.err = 1;
    }

    if (ps.err == 2) {
        snprintf(out, outsz, "error: division by zero");
        return -1;
    }
    if (ps.err != 0) {
        snprintf(out, outsz, "error: cannot parse expression '%s'", expr);
        return -1;
    }

    format_number(value, num, sizeof num);
    snprintf(out, outsz, "%s", num);
    return 0;
}

int tool_dispatch(const char *name, const char *arg, char *out, size_t outsz)
{
    if (out == NULL || outsz == 0) {
        return -1;
    }
    if (name != NULL && strcmp(name, "calc") == 0) {
        return tool_calc(arg, out, outsz);
    }
    snprintf(out, outsz, "error: unknown tool '%s'", (name != NULL) ? name : "(null)");
    return -1;
}
