/* main.c — test driver. Exit code is the failure count, so CI needs no parsing. */
#include "test.h"
#include "tests.h"

int ls_test_failures = 0;
int ls_test_count = 0;

int main(void) {
    os_test_harness();
    os_test_spectral();
    os_test_glass();
    os_test_lens();
    os_test_trace();
    os_test_ui();
    os_test_camera();
    os_test_inspect();
    os_test_scene3d();
    os_test_scenedesc();
    os_test_env();
    os_test_render_focus();

    printf("\n%d checks, %d failures\n", ls_test_count, ls_test_failures);
    return ls_test_failures;
}
