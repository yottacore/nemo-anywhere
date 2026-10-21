/* GLib reads the console code page the first time it formats an errno, and
 * warns when it can't. Under g_test_init that warning is fatal, so a test that
 * only reports 0 failure(s) still exits 3. Natively with no console, Windows
 * says ERROR_INVALID_HANDLE and GLib stays quiet; wine says
 * ERROR_INVALID_ACCESS, so the wine runner hands GLib a charset in LANG.
 * This fails wherever the runner doesn't. */

#include <config.h>

#include <errno.h>
#include <glib.h>

#include "test-check.h"

static int warnings = 0;

static void
count_warning (const gchar *domain, GLogLevelFlags level G_GNUC_UNUSED, const gchar *message, gpointer data G_GNUC_UNUSED)
{
	g_printerr ("%s-WARNING: %s\n", domain, message);
	warnings++;
}

int
main (void)
{
	g_log_set_handler ("GLib", G_LOG_LEVEL_WARNING, count_warning, NULL);

	g_get_console_charset (NULL);
	check (g_strerror (ENOENT) != NULL);
	check (warnings == 0);

	g_printerr ("%d failure(s)\n", failures);
	return failures == 0 ? 0 : 1;
}
