/* =====================================================================
 * memcheck.c -- Counting allocator. Compiled only under
 * -DHARNESS_MEMCHECK; otherwise this file produces an empty object.
 *
 * Each block carries a small header holding its size and a magic value,
 * so free() can subtract the payload from the running total and catch a
 * pointer that did not come from memcheck_malloc.
 * ===================================================================== */
#ifdef HARNESS_MEMCHECK

/* This file must call the REAL allocator, so it deliberately does not
 * include memcheck.h and its #define malloc/free. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MEMCHECK_MAGIC 0x4D454D43uL   /* "MEMC" */

/* A union keeps the payload maximally aligned for any object type. */
typedef union Header {
    struct {
        size_t        size;
        unsigned long magic;
    } meta;
    long double align;
} Header;

static unsigned long g_allocs      = 0;
static unsigned long g_frees       = 0;
static unsigned long g_bad_frees   = 0;
static size_t        g_outstanding = 0;   /* live payload bytes */
static size_t        g_peak        = 0;
static int           g_hook        = 0;

static void memcheck_report(void)
{
    unsigned long live = g_allocs - g_frees;

    /* stderr, so the ledger never contaminates the stdout transcript that
     * the behavioural assertions in test.sh grep through. */
    fprintf(stderr,
            "[memcheck] allocs=%lu frees=%lu outstanding=%lu "
            "bytes_outstanding=%lu peak_bytes=%lu bad_frees=%lu\n",
            g_allocs, g_frees, live,
            (unsigned long)g_outstanding,
            (unsigned long)g_peak,
            g_bad_frees);

    if (live != 0 || g_bad_frees != 0) {
        fprintf(stderr, "[memcheck] FAIL: memory was leaked or misfreed\n");
    } else {
        fprintf(stderr, "[memcheck] OK: every allocation was freed\n");
    }
}

void memcheck_init(void)
{
    if (!g_hook) {
        atexit(memcheck_report);
        g_hook = 1;
    }
}

void *memcheck_malloc(size_t size)
{
    Header *h;

    memcheck_init();    /* belt and braces if main() forgot */

    h = (Header *)malloc(sizeof(Header) + size);
    if (h == NULL) {
        return NULL;                /* accounting must not hide OOM */
    }

    h->meta.size  = size;
    h->meta.magic = MEMCHECK_MAGIC;

    g_allocs++;
    g_outstanding += size;
    if (g_outstanding > g_peak) {
        g_peak = g_outstanding;
    }
    return (void *)(h + 1);         /* hand back the payload */
}

void memcheck_free(void *ptr)
{
    Header *h;

    if (ptr == NULL) {
        return;                     /* free(NULL) is legal and is a no-op */
    }

    h = ((Header *)ptr) - 1;
    if (h->meta.magic != MEMCHECK_MAGIC) {
        /* Either a double free or a pointer we never handed out. */
        g_bad_frees++;
        fprintf(stderr, "[memcheck] bad free: %p was not allocated here\n", ptr);
        return;
    }

    h->meta.magic = 0;              /* poison, so a double free is caught */
    g_frees++;
    g_outstanding -= h->meta.size;
    free(h);
}

#else
/* ISO C forbids an empty translation unit. */
typedef int memcheck_translation_unit_not_empty;
#endif /* HARNESS_MEMCHECK */
