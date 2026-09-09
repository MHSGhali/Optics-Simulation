/* tests.h — the suite roster.
 *
 * One function per suite, called in dependency order by main.c: a failure in
 * the glass tables should be read before a failure in the lens that uses them,
 * and a lens failure before the camera that mounts it. Adding a suite means
 * adding a declaration here and a call there, and nothing else. */
#ifndef OPTICSIM_TESTS_H
#define OPTICSIM_TESTS_H

void os_test_harness(void);   /* the harness proves itself before it judges anything */
void os_test_spectral(void);  /* vendored spectral/colour core + ls_spectrum_at */
void os_test_glass(void);     /* Sellmeier catalogue audit + model-glass solve */
void os_test_lens(void);      /* paraxial optics against closed forms */
void os_test_trace(void);     /* real sequential ray tracing, Snell, iris, clipping */
void os_test_ui(void);        /* toolbar rules and the cross-section, with no window */
void os_test_camera(void);    /* pupils, camera rays, exposure, determinism */
void os_test_inspect(void);   /* the settings model: bounds, clamping, restarts */
void os_test_scene3d(void);   /* the scene view's projection, and depth of field */
void os_test_scenedesc(void); /* the editable scene: ids, clamps, lumens, pairing */
void os_test_env(void);       /* the ambient dome: lux in, closed forms out */
void os_test_render_focus(void); /* does the picture agree with the panel? */

#endif /* OPTICSIM_TESTS_H */
