/* =====================================================================
 * memcheck.h -- Optional allocation accounting for the harness.
 *
 * WHY THIS EXISTS
 *   valgrind is the right tool for leak detection, but it does not exist
 *   on every machine a student or grader will use (it has no Windows
 *   port, and MinGW's gcc has no AddressSanitizer). A test suite whose
 *   memory group silently reports SKIP is not evidence of anything.
 *
 *   Compiling with -DHARNESS_MEMCHECK redirects every malloc/free in the
 *   project through counting wrappers and installs an atexit() hook that
 *   prints a one-line ledger:
 *
 *       [memcheck] allocs=27 frees=27 outstanding=0 bytes_outstanding=0
 *
 *   test.sh asserts outstanding=0. This is weaker than valgrind -- it
 *   sees only allocations made by THIS project and cannot detect invalid
 *   reads or writes -- but it directly answers "did every malloc get a
 *   matching free", which is the leak question the spec asks, and it runs
 *   everywhere.
 *
 *   Without -DHARNESS_MEMCHECK this header compiles to nothing at all.
 * ===================================================================== */
#ifndef MEMCHECK_H
#define MEMCHECK_H

#ifdef HARNESS_MEMCHECK

#include <stddef.h>

/* Arm the exit-time report. Called once from main(). Registering here
 * rather than lazily on first malloc means the ledger is printed even for
 * a session that allocates nothing at all -- otherwise an empty run would
 * produce no evidence either way, which reads as a pass. */
void  memcheck_init(void);
void *memcheck_malloc(size_t size);
void  memcheck_free(void *ptr);

/* Redirect the whole project's allocator. Must appear after <stdlib.h>
 * is included, which is why every .c file includes this header last. */
#define malloc(sz) memcheck_malloc(sz)
#define free(p)    memcheck_free(p)

#else  /* !HARNESS_MEMCHECK -- compile the instrumentation away entirely */

#define memcheck_init() ((void)0)

#endif /* HARNESS_MEMCHECK */

#endif /* MEMCHECK_H */
