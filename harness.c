/* =====================================================================
 * harness.c -- ECE 309 Project 1: an LLM mini-harness in C.
 *
 * WHAT A HARNESS IS
 *   A model is a pure function from text to text. Everything else that
 *   makes an "AI agent" feel like an agent lives here, in the harness:
 *
 *     * the read-eval-print loop that owns stdin and stdout
 *     * the conversation memory and its eviction policy   (context.c)
 *     * the tool registry and the code that ACTUALLY runs (tools.c)
 *     * the multi-pass turn: model -> tool -> model
 *     * safe shutdown, freeing every allocation on the way out
 *
 * STATE MACHINE
 *   START -> READ -> {  "exit"/"quit"/EOF        -> SHUTDOWN
 *                       "/history", "/help"      -> META, back to READ
 *                       empty line               -> back to READ
 *                       anything else            -> THINK }
 *   THINK -> model_generate()
 *          -> if the reply carries a [[TOOL:...]] marker: ACT
 *          -> else: SPEAK
 *   ACT   -> run the tool, record a tool turn, model_generate() again
 *          -> SPEAK
 *   SPEAK -> record the assistant turn, print it, back to READ
 *   SHUTDOWN -> context_free(), return EXIT_SUCCESS
 *
 * Build:  gcc -std=c11 -Wall -Wextra -o harness *.c -lm
 * ===================================================================== */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "context.h"
#include "model.h"
#include "tools.h"

#include "memcheck.h"  /* no-op unless -DHARNESS_MEMCHECK */

/* One line of user input. Longer lines are truncated safely rather than
 * overflowing (see read_line). */
#define INPUT_MAX 1024

#define PROMPT_USER  "you> "
#define PROMPT_MODEL "model> "

/* Read one line from `in` into `buf`, strip the trailing newline, and
 * drain the rest of the line if the user typed more than INPUT_MAX-1
 * characters. Returns 1 on success, 0 on EOF. */
static int read_line(FILE *in, char *buf, size_t bufsz)
{
    size_t len;

    if (fgets(buf, (int)bufsz, in) == NULL) {
        return 0;                       /* EOF or read error */
    }

    len = strlen(buf);
    if (len > 0 && buf[len - 1] == '\n') {
        buf[len - 1] = 0;               /* strip the newline fgets kept */
    } else {
        /* No newline means the line was longer than our buffer. Discard
         * the remainder so it is not mistaken for the next command. */
        int c;
        while ((c = fgetc(in)) != '\n' && c != EOF) {
            /* discard */
        }
    }

    /* Trim trailing whitespace and a stray CR from CRLF input, so that a
     * file piped in from Windows still matches "exit". */
    len = strlen(buf);
    while (len > 0 &&
           (buf[len - 1] == ' ' || buf[len - 1] == '\t' || buf[len - 1] == '\r')) {
        buf[--len] = 0;
    }
    return 1;
}

static int is_command(const char *line, const char *cmd)
{
    return strcmp(line, cmd) == 0;
}

static void print_help(void)
{
    printf("[harness] commands:\n");
    printf("  exit | quit   end the session and free all memory\n");
    printf("  /history      show the turns currently held in context\n");
    printf("  /help         show this message\n");
    printf("[harness] try: hello   |   calc 2+3*4   |   what is 10/4\n");
}

/* Run one full agent turn for `input`.
 * Returns 0 on success, -1 if we ran out of memory (which the caller
 * treats as a reason to shut down cleanly rather than limp along). */
static int run_turn(Context *ctx, const char *input)
{
    char *reply;
    char  tool_name[32];
    char  tool_arg[INPUT_MAX];
    char  tool_out[TOOL_OUTPUT_MAX];

    /* 1. Record what the user said. The model only ever sees context. */
    if (context_add(ctx, ROLE_USER, input) != 0) {
        return -1;
    }

    /* 2. First model pass. */
    reply = model_generate(ctx);
    if (reply == NULL) {
        return -1;
    }

    /* 3. Did the model ask for a tool? If so, the HARNESS runs it -- the
     *    model never touches the machine itself. */
    if (model_extract_tool_call(reply, tool_name, sizeof tool_name,
                                tool_arg, sizeof tool_arg)) {
        printf("[harness] tool call: %s(%s)\n", tool_name, tool_arg);

        /* tool_dispatch always writes a message, success or failure, so
         * tool_out is safe to use whichever branch we took. */
        (void)tool_dispatch(tool_name, tool_arg, tool_out, sizeof tool_out);
        printf("[harness] tool result: %s\n", tool_out);

        /* The tool result becomes a turn in its own right. */
        if (context_add(ctx, ROLE_TOOL, tool_out) != 0) {
            free(reply);
            return -1;
        }

        /* 4. Second model pass, now that the answer is in context. */
        free(reply);
        reply = model_generate(ctx);
        if (reply == NULL) {
            return -1;
        }
    }

    /* 5. Record and speak. */
    if (context_add(ctx, ROLE_ASSISTANT, reply) != 0) {
        free(reply);
        return -1;
    }
    printf("%s%s\n", PROMPT_MODEL, reply);

    free(reply);        /* every model_generate() is paired with a free() */
    return 0;
}

int main(void)
{
    Context ctx;
    char    line[INPUT_MAX];
    int     status = EXIT_SUCCESS;

    memcheck_init();    /* no-op unless built with -DHARNESS_MEMCHECK */
    context_init(&ctx);

    printf("ECE 309 mini-harness (mock model, %d-turn context)\n",
           CONTEXT_MAX_TURNS);
    print_help();

    /* ---- READ loop ---------------------------------------------------
     * stdout is flushed after the prompt because when the harness is
     * driven by a pipe (test.sh) stdout is fully buffered, and an
     * unflushed prompt would arrive out of order. */
    for (;;) {
        printf("%s", PROMPT_USER);
        fflush(stdout);

        if (!read_line(stdin, line, sizeof line)) {
            printf("\n[harness] end of input\n");
            break;                              /* EOF -> SHUTDOWN */
        }

        if (is_command(line, "exit") || is_command(line, "quit")) {
            printf("[harness] shutting down\n");
            break;                              /* the safe shutdown path */
        }
        if (line[0] == 0) {
            continue;                           /* ignore blank lines */
        }
        if (is_command(line, "/help")) {
            print_help();
            continue;                           /* meta-commands are not */
        }                                       /* stored in context      */
        if (is_command(line, "/history")) {
            context_print(&ctx, stdout);
            continue;
        }

        if (run_turn(&ctx, line) != 0) {
            fprintf(stderr, "[harness] out of memory -- shutting down\n");
            status = EXIT_FAILURE;
            break;
        }
    }

    /* ---- SHUTDOWN ----------------------------------------------------
     * Releasing the context here is what makes the leak check in test.sh
     * meaningful: at exit the heap must be empty. */
    printf("[harness] final context state:\n");
    context_print(&ctx, stdout);
    context_free(&ctx);
    printf("[harness] goodbye\n");
    return status;
}
