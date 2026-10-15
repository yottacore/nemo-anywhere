/* Argument quoting for the two Windows shell hand-offs, "Open as Administrator"
 * and "Open in Terminal". Both build a command line that the target process
 * re-splits under MSVCRT rules, and neither can be driven unattended - one puts
 * up a UAC prompt, the other opens a console - so the quoting is checked here
 * instead. Windows-only. */

#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <glib.h>

#include <src/nemo-view-win32.h>

static int failures = 0;

static void
check_quote (const char *arg, const char *want)
{
	char *got = nemo_view_win32_quote_arg (arg);

	if (g_strcmp0 (got, want) != 0) {
		g_printerr ("FAIL %s -> %s, wanted %s\n", arg, got, want);
		failures++;
	}
	g_free (got);
}

static void
check_split (const char *command, const char *want_exe, const char *want_args)
{
	char *args = NULL;
	char *exe = nemo_view_win32_split_terminal_command (command, &args);

	if (g_strcmp0 (exe, want_exe) != 0 || g_strcmp0 (args, want_args) != 0) {
		g_printerr ("FAIL %s -> [%s] [%s], wanted [%s] [%s]\n",
			    command, exe, args ? args : "(none)",
			    want_exe, want_args ? want_args : "(none)");
		failures++;
	}
	g_free (exe);
	g_free (args);
}

int
main (void)
{
	/* Ordinary paths: interior separators are literal, spaces are covered by
	 * the surrounding quotes and nothing else changes. */
	check_quote ("C:\\Users\\someone", "\"C:\\Users\\someone\"");
	check_quote ("C:\\Program Files\\Thing", "\"C:\\Program Files\\Thing\"");

	/* A drive root. The trailing backslash has to be doubled or it escapes the
	 * closing quote, and the target sees one unterminated argument instead of a
	 * path - which is what "Open in Terminal" on C:\ used to send. */
	check_quote ("C:\\", "\"C:\\\\\"");
	check_quote ("\\\\server\\share\\", "\"\\\\server\\share\\\\\"");

	/* A quote in the name, and a backslash run before one. */
	check_quote ("a\"b", "\"a\\\"b\"");
	check_quote ("a\\\"b", "\"a\\\\\\\"b\"");

	/* Nothing to quote is still a quoted empty argument, not an omitted one. */
	check_quote ("", "\"\"");

	/* The terminal preference is one field, so the program and its arguments
	 * have to be told apart afterwards. */
	/* A bare name that is not on PATH: nothing to look up, nothing to split.
	   Not a real terminal, or the answer depends on what the machine has
	   installed - wt.exe here came back as its full WindowsApps path. */
	check_split ("nemo-no-such-terminal.exe", "nemo-no-such-terminal.exe", NULL);
	check_split ("nemo-no-such-terminal.exe -NoLogo", "nemo-no-such-terminal.exe", "-NoLogo");
	check_split ("\"C:\\Program Files\\Thing\\term.exe\"", "C:\\Program Files\\Thing\\term.exe", NULL);
	check_split ("\"C:\\Program Files\\Thing\\term.exe\" --here -x", "C:\\Program Files\\Thing\\term.exe", "--here -x");

	/* An unquoted path with spaces and no arguments still has to work: a
	 * program that can be found is taken whole rather than cut at the space. */
	{
		char *found = g_find_program_in_path ("cmd.exe");

		if (found != NULL) {
			check_split (found, found, NULL);
		}
		g_free (found);
	}

	/* "Open with Explorer" puts a window on the screen, so it is not something
	 * an unattended run can do. Set NEMO_PROBE_EXPLORER to a folder to watch it
	 * open that folder and then pick a file out of it. */
	{
		const char *probe_dir = g_getenv ("NEMO_PROBE_EXPLORER");

		if (probe_dir == NULL) {
			g_print ("SKIP open with Explorer (opens a window; set NEMO_PROBE_EXPLORER to a folder to run it)\n");
		} else {
			char *file = g_build_filename (probe_dir, "explorer-probe.txt", NULL);

			if (!g_file_set_contents (file, "probe\n", -1, NULL)) {
				g_printerr ("FAIL cannot write %s\n", file);
				failures++;
			}

			nemo_view_win32_open_in_explorer (probe_dir, TRUE);
			g_usleep (2 * G_USEC_PER_SEC);
			nemo_view_win32_open_in_explorer (file, FALSE);

			g_print ("probed open with Explorer on %s\n", file);
			g_free (file);
		}
	}

	if (failures == 0) {
		g_print ("view-win32: all checks passed\n");
	}
	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
