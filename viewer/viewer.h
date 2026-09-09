/* viewer.h — the window's entry point, declared without mentioning SDL.
 *
 * apps/opticsim.c calls this to open the viewer. Keeping the declaration free
 * of SDL types is what lets the single binary's main() live outside the viewer
 * and still be covered by check-sdl-purity.
 */
#ifndef OPTICSIM_VIEWER_H
#define OPTICSIM_VIEWER_H

/* Opens the window and runs until the user closes it. Returns a process exit
 * code. Only defined when the viewer was built; see apps/opticsim.c. */
int os_viewer_main(int argc, char **argv);

#endif /* OPTICSIM_VIEWER_H */
