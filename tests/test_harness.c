/* test_harness.c — the assertion macros, checked against themselves.
 *
 * A harness that silently passes everything is worse than no harness, and the
 * failure is invisible: every suite goes green. So CHECK_NEAR's own arithmetic
 * gets asserted here, on the two cases that actually decide test outcomes
 * elsewhere -- the relative branch, and the absolute fallback near zero. */
#include "test.h"
#include "tests.h"

void os_test_harness(void) {
    SECTION("harness");

    /* The relative branch: 1e-9 out of 100 is a relative error of 1e-11. */
    CHECK_NEAR(100.0 + 1e-9, 100.0, 1e-10);

    /* The absolute fallback. Below |want| = 1e-12 the relative form would
     * divide by almost nothing and report a huge error for a difference that
     * does not matter, so the comparison switches to absolute. */
    CHECK_NEAR(1e-15, 0.0, 1e-12);

    /* Exactness is representable: these tolerances are used at 1e-14 and 1e-12
     * by the Snell and Fresnel tests, so zero error must really read as zero. */
    CHECK_NEAR(1.0 / 3.0, 1.0 / 3.0, 0.0);
    CHECK(ls_test_count > 0);
}
