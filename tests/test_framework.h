/*==============================================================================
 * Minimal Test Framework for VEX DEFENDER
 *
 * Simple assert-based testing with pass/fail counting.
 * Designed for host-side compilation (GCC/Clang/MSVC) -- NOT for SNES.
 *
 * Usage pattern:
 *   1. Include this header in your test file
 *   2. Define test functions using TEST(name) macro
 *   3. Use TEST_ASSERT_* macros to check conditions
 *   4. Group related tests with TEST_SUITE("name")
 *   5. Call test functions from main() in test_main.c
 *
 * All assertion macros follow the same pattern:
 *   - Increment tf_total (total test count)
 *   - Evaluate the condition
 *   - On failure: increment tf_fail, print diagnostic with file line number
 *   - On success: increment tf_pass (silently)
 *
 * The do { ... } while(0) wrapper around each macro ensures they behave
 * as single statements when used in if/else blocks without braces.
 *
 * Counters (tf_pass, tf_fail, tf_total) are extern globals defined in
 * test_main.c so they can be shared across all test files compiled into
 * the single compilation unit.
 *============================================================================*/

#ifndef TEST_FRAMEWORK_H
#define TEST_FRAMEWORK_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*=== Test Counters (defined in test_main.c) ===*/
/* These are global counters shared across all test files.
 * They accumulate across all test suites for a final summary. */
extern int tf_pass;   /* Number of assertions that passed */
extern int tf_fail;   /* Number of assertions that failed */
extern int tf_total;  /* Total number of assertions evaluated */

/*=== Assertion Macros ===*/

/* TEST_ASSERT - Boolean condition check.
 * Fails if cond evaluates to false (0).
 * msg: Human-readable description of what was expected. */
#define TEST_ASSERT(cond, msg) do { \
    tf_total++; \
    if (!(cond)) { \
        tf_fail++; \
        printf("  FAIL: %s (line %d)\n", msg, __LINE__); \
    } else { \
        tf_pass++; \
    } \
} while(0)

/* TEST_ASSERT_EQ - Equality check for integer/numeric values.
 * Fails if a != b.  Prints expected vs actual values on failure.
 * a: Actual value.  b: Expected value.  msg: Description. */
#define TEST_ASSERT_EQ(a, b, msg) do { \
    tf_total++; \
    if ((a) != (b)) { \
        tf_fail++; \
        printf("  FAIL: %s - expected %d, got %d (line %d)\n", \
               msg, (int)(b), (int)(a), __LINE__); \
    } else { \
        tf_pass++; \
    } \
} while(0)

/* TEST_ASSERT_NEQ - Inequality check.
 * Fails if a == b (values should be different).
 * a: Actual value.  b: Value that should NOT match.  msg: Description. */
#define TEST_ASSERT_NEQ(a, b, msg) do { \
    tf_total++; \
    if ((a) == (b)) { \
        tf_fail++; \
        printf("  FAIL: %s - expected != %d (line %d)\n", \
               msg, (int)(b), __LINE__); \
    } else { \
        tf_pass++; \
    } \
} while(0)

/* TEST_ASSERT_STR - String equality check using strcmp().
 * Fails if the two strings are not identical.
 * a: Actual string.  b: Expected string.  msg: Description. */
#define TEST_ASSERT_STR(a, b, msg) do { \
    tf_total++; \
    if (strcmp(a, b) != 0) { \
        tf_fail++; \
        printf("  FAIL: %s - expected \"%s\", got \"%s\" (line %d)\n", \
               msg, b, a, __LINE__); \
    } else { \
        tf_pass++; \
    } \
} while(0)

/* TEST_ASSERT_GT - Greater-than check.
 * Fails if a is not strictly greater than b.
 * a: Actual value.  b: Threshold.  msg: Description. */
#define TEST_ASSERT_GT(a, b, msg) do { \
    tf_total++; \
    if (!((a) > (b))) { \
        tf_fail++; \
        printf("  FAIL: %s - %d not > %d (line %d)\n", \
               msg, (int)(a), (int)(b), __LINE__); \
    } else { \
        tf_pass++; \
    } \
} while(0)

/* TEST_ASSERT_GE - Greater-than-or-equal check.
 * Fails if a is less than b.
 * a: Actual value.  b: Minimum.  msg: Description. */
#define TEST_ASSERT_GE(a, b, msg) do { \
    tf_total++; \
    if (!((a) >= (b))) { \
        tf_fail++; \
        printf("  FAIL: %s - %d not >= %d (line %d)\n", \
               msg, (int)(a), (int)(b), __LINE__); \
    } else { \
        tf_pass++; \
    } \
} while(0)

/* TEST_ASSERT_LE - Less-than-or-equal check.
 * Fails if a exceeds b.
 * a: Actual value.  b: Maximum.  msg: Description. */
#define TEST_ASSERT_LE(a, b, msg) do { \
    tf_total++; \
    if (!((a) <= (b))) { \
        tf_fail++; \
        printf("  FAIL: %s - %d not <= %d (line %d)\n", \
               msg, (int)(a), (int)(b), __LINE__); \
    } else { \
        tf_pass++; \
    } \
} while(0)

/* TEST_ASSERT_NULL - Null pointer check.
 * Fails if p is not NULL.
 * p: Pointer to check.  msg: Description. */
#define TEST_ASSERT_NULL(p, msg) do { \
    tf_total++; \
    if ((void*)(p) != NULL) { \
        tf_fail++; \
        printf("  FAIL: %s - expected NULL (line %d)\n", msg, __LINE__); \
    } else { \
        tf_pass++; \
    } \
} while(0)

/* TEST_ASSERT_NOT_NULL - Non-null pointer check.
 * Fails if p is NULL.
 * p: Pointer to check.  msg: Description. */
#define TEST_ASSERT_NOT_NULL(p, msg) do { \
    tf_total++; \
    if ((void*)(p) == NULL) { \
        tf_fail++; \
        printf("  FAIL: %s - got NULL (line %d)\n", msg, __LINE__); \
    } else { \
        tf_pass++; \
    } \
} while(0)

/*=== Suite Management ===*/
/* TEST_SUITE - Print a header for a group of related tests.
 * Used for visual organization in test output.
 * name: Display name for the test suite (e.g., "RPG Stats"). */
#define TEST_SUITE(name) do { \
    printf("\n[%s]\n", name); \
} while(0)

/*=== Test Function Declaration Macro ===*/
/* TEST - Declare a static test function with void return and no parameters.
 * Usage: TEST(test_add_xp) { ... TEST_ASSERT_EQ(...); ... }
 * Static linkage prevents name collisions in the single-compilation-unit build. */
#define TEST(name) static void name(void)

#endif /* TEST_FRAMEWORK_H */
