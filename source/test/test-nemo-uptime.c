/* The About box says how long this copy has been running. Days, hours and
 * minutes, with the zero ones left out and the plurals right. */

#include <config.h>

#include <stdlib.h>
#include <glib.h>

#include <libnemo-private/nemo-file-utilities.h>
#include "test-check.h"

static void
check_is (gint64 seconds, const char *expected)
{
	char *got = nemo_format_uptime (seconds);

	if (g_strcmp0 (got, expected) != 0) {
		g_printerr ("FAIL: %" G_GINT64_FORMAT " -> %s (wanted %s)\n",
			    seconds, got, expected);
		failures++;
	}

	g_free (got);
}

int
main (void)
{
	check_is (0, "less than a minute");
	check_is (59, "less than a minute");
	check_is (60, "1 minute");
	check_is (119, "1 minute");
	check_is (120, "2 minutes");
	check_is (3600, "1 hour");
	check_is (3660, "1 hour, 1 minute");
	check_is (86400, "1 day");
	check_is (86400 + 60, "1 day, 1 minute");
	check_is (2 * 86400 + 3 * 3600 + 4 * 60 + 5, "2 days, 3 hours, 4 minutes");
	check_is (400 * 86400, "400 days");

	/* Nothing noted yet reads as no time at all. */
	check (nemo_get_uptime_seconds () == 0);
	nemo_note_process_start ();
	check (nemo_get_uptime_seconds () >= 0);
	check (nemo_get_uptime_seconds () < 5);

	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
