/* Starting a program on Windows. The command-line split runs everywhere; the
 * two brokers only when NEMO_PROBE_LAUNCH is set, since they start real
 * processes. Each broker is asked on its own, so one cannot pass for the other. */

#include <config.h>

#include <glib.h>
#include <glib/gstdio.h>
#include <libnemo-private/nemo-launch-win32.h>

#include "test-scratch.h"

static int failures;

static void
check (gboolean ok, const char *what)
{
	g_print ("%s: %s\n", ok ? "PASS" : "FAIL", what);
	if (!ok) {
		failures++;
	}
}

static void
check_split (const char *line,
	     const char *want_exe,
	     const char *want_args)
{
	char *args = NULL;
	char *exe = nemo_launch_win32_split_command (line, &args);
	gboolean ok = g_strcmp0 (exe, want_exe) == 0 && g_strcmp0 (args, want_args) == 0;

	check (ok, line);
	if (!ok) {
		g_print ("     got: [%s] [%s]\n  wanted: [%s] [%s]\n",
			 exe, args ? args : "(null)",
			 want_exe, want_args ? want_args : "(null)");
	}

	g_free (exe);
	g_free (args);
}

static void
test_split (void)
{
	check_split ("\"C:\\App\\my app.exe\" \"C:\\a b.txt\"",
		     "C:\\App\\my app.exe", "\"C:\\a b.txt\"");
	check_split ("\"C:\\App\\my app.exe\"", "C:\\App\\my app.exe", NULL);
	check_split ("  \"C:\\App\\a.exe\"   -x  ", "C:\\App\\a.exe", "-x  ");
	check_split ("notepad.exe", "notepad.exe", NULL);
	check_split ("notepad.exe C:\\x.txt", "notepad.exe", "C:\\x.txt");

	/* An unquoted path with spaces: the prefix that exists on disk wins. */
	check_split ("C:\\Windows\\System32\\notepad.exe /A x.txt",
		     "C:\\Windows\\System32\\notepad.exe", "/A x.txt");

	/* Nothing on disk matches, so it splits at the first space and the rest
	 * of the path goes with the arguments. Wrong, but it is what the shell
	 * does too, and only a launch that was never going to work sees it. */
	check_split ("C:\\No Such\\thing.exe -q", "C:\\No", "Such\\thing.exe -q");

	check_split ("\"C:\\unbalanced.exe", "C:\\unbalanced.exe", NULL);
}

/* A path that is not there must be refused here rather than handed over: the
 * shell answers one with a message box, and the call blocks until it is gone. */
static void
test_missing_is_refused (void)
{
	check (!nemo_launch_win32_via_shell ("C:\\no-such-dir\\no-such.exe", NULL, NULL),
	       "a path that does not exist never reaches the shell");
	check (!nemo_launch_win32_via_shell ("C:\\no-such-dir\\no-such.lnk", NULL, NULL),
	       "nor does a shortcut that does not exist");
}

/* Runs @line through @route and waits for the marker file it writes. */
static gboolean
marker_appears (const char *marker,
		gboolean  (*route) (const char *, const char *),
		const char *line)
{
	int waited;

	g_remove (marker);

	if (!route (line, NULL)) {
		return FALSE;
	}

	for (waited = 0; waited < 10000; waited += 200) {
		if (g_file_test (marker, G_FILE_TEST_EXISTS)) {
			return TRUE;
		}
		g_usleep (200 * 1000);
	}

	return FALSE;
}

static gboolean
via_shell_line (const char *line,
		const char *workdir)
{
	char *args = NULL;
	char *exe = nemo_launch_win32_split_command (line, &args);
	gboolean ok = nemo_launch_win32_via_shell (exe, args, workdir);

	g_free (exe);
	g_free (args);
	return ok;
}

static void
test_brokers (void)
{
	char *dir = test_scratch_dir ("nemo-launch-XXXXXX", NULL);
	char *marker = g_build_filename (dir, "started.txt", NULL);
	char *line = g_strdup_printf ("cmd.exe /c echo ok> \"%s\"", marker);

	check (marker_appears (marker, via_shell_line, line), "the desktop shell starts it");
	check (marker_appears (marker, nemo_launch_win32_via_service, line), "the management service starts it");

	g_remove (marker);
	g_free (line);
	g_free (marker);
	g_rmdir (dir);
	g_free (dir);
}

int
main (void)
{
	test_split ();
	test_missing_is_refused ();

	if (g_getenv ("NEMO_PROBE_LAUNCH") != NULL) {
		test_brokers ();
	} else {
		g_print ("SKIP: the brokers, set NEMO_PROBE_LAUNCH to start real processes\n");
	}

	return failures == 0 ? 0 : 1;
}
