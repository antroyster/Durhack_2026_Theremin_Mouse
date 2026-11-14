/*
 * check.h - the whole test "framework": plain C, no dependencies.
 * Every check prints PASS or FAIL with the measured number next to the limit.
 * A test program ends with `return check_done();` (non-zero if anything failed).
 */
#ifndef CHECK_H
#define CHECK_H

#include <stdio.h>

static int check_failures;

/* Pass if lo <= measured <= hi. */
static inline void check_range(const char *name, double measured, double lo, double hi) {
    int ok = (measured >= lo) && (measured <= hi);
    if (!ok) check_failures++;
    if (lo == hi) {
        printf("%s  %-52s measured=%g  limit=%g\n", ok ? "PASS" : "FAIL", name, measured, lo);
    } else {
        printf("%s  %-52s measured=%g  limit=%g..%g\n", ok ? "PASS" : "FAIL", name, measured, lo, hi);
    }
}

/* Pass if measured == expected. */
static inline void check_eq(const char *name, double measured, double expected) {
    check_range(name, measured, expected, expected);
}

static inline int check_done(void) {
    if (check_failures) printf("RESULT: %d check(s) FAILED\n", check_failures);
    return check_failures ? 1 : 0;
}

#endif
