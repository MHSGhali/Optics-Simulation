/* cli.h — the command-line face of the single opticsim binary.
 *
 * One program, two faces: given a subcommand it behaves as a batch tool, given
 * nothing it opens the viewer. This half carries no SDL dependency, so the
 * command line still builds and runs on a machine with no windowing library.
 */
#ifndef OPTICSIM_CLI_H
#define OPTICSIM_CLI_H

#include <stdbool.h>

/* True if `s` names a subcommand, so main() can tell `opticsim still` from a
 * stray flag and open the viewer for anything that is not a command. */
bool os_cli_is_command(const char *s);

/* Run the subcommand named by argv[0]. Returns the process exit code. */
int  os_cli_run(int argc, char **argv);

void os_cli_usage(const char *argv0);

#endif /* OPTICSIM_CLI_H */
