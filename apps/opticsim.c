/* opticsim.c — the one entry point.
 *
 * ONE BINARY, TWO FACES
 *   `opticsim` with no arguments opens the viewer, where every setting is a
 *   control. `opticsim <command> ...` runs the same engine as a batch tool.
 *   There is no second executable and no separate "viewer" build to remember.
 *
 *   This file carries no SDL dependency of its own -- it calls
 *   os_viewer_main() through a plain declaration -- so it compiles and links
 *   identically whether or not a windowing library exists. When SDL is absent
 *   the viewer half is simply not linked in, and asking for it says so instead
 *   of failing to build.
 */
#include "opticsim/cli.h"

#ifdef OS_HAVE_SDL
#include "viewer.h"
#endif

#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    /* Line-buffered so progress interleaves correctly when output is piped. */
    setvbuf(stdout, NULL, _IOLBF, 0);

    if (argc >= 2 && (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help"))) {
        os_cli_usage(argv[0]);
        return 0;
    }

    if (argc >= 2 && os_cli_is_command(argv[1]))
        return os_cli_run(argc - 1, argv + 1);

    /* --capture drives the viewer's own draw path into a hidden window, so it
     * belongs to the viewer half rather than to the command line. */
    bool to_viewer = (argc >= 2 && !strcmp(argv[1], "--capture"));

    if (argc >= 2 && !to_viewer) {
        fprintf(stderr, "%s: unknown command '%s'\n\n", argv[0], argv[1]);
        os_cli_usage(argv[0]);
        return 2;
    }

#ifdef OS_HAVE_SDL
    return os_viewer_main(argc, argv);
#else
    fprintf(stderr, "%s was built without SDL2, so it has no viewer.\n"
                    "Install SDL2 (brew install sdl2) and rebuild, or use a\n"
                    "command:\n\n", argv[0]);
    os_cli_usage(argv[0]);
    return 2;
#endif
}
