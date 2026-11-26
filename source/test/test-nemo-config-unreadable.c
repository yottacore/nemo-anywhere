/* A settings file that's there at startup but can't be read is never saved
 * over, until a load reads it. "mode" makes it unreadable by its mode,
 * "oversized" makes it bigger than the store reads. Each runs in a process
 * of its own, since only the first load is the one that matters. The
 * warnings each logs are expected. Runs against a throwaway config root. */

#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <gio/gio.h>
#include <glib/gstdio.h>

#ifdef G_OS_UNIX
#include <unistd.h>
#endif
#ifdef __linux__
#include <linux/capability.h>
#include <sys/syscall.h>
#endif

#include <libnemo-private/nemo-config.h>
#include <libnemo-private/nemo-file-utilities.h>

#include "test-scratch.h"
#include "test-check.h"

#define SKIP 77
#define OVER_CAP (9 * 1024 * 1024)   /* the store reads up to 8 MiB */

static const char file_text[] = "preferences.show-hidden-files: true\n";

/* Root reads a file whatever its mode says, unless it gives up the
 * capabilities that let it. */
static gboolean
mode_blocks_reads (void)
{
#ifdef G_OS_UNIX
	if (geteuid () != 0)
		return TRUE;
#endif
#ifdef __linux__
	{
		struct __user_cap_header_struct head = { _LINUX_CAPABILITY_VERSION_3, 0 };
		struct __user_cap_data_struct   data[2];

		if (syscall (SYS_capget, &head, data) != 0)
			return FALSE;
		data[0].effective &= ~((1u << CAP_DAC_OVERRIDE) | (1u << CAP_DAC_READ_SEARCH));
		return syscall (SYS_capset, &head, data) == 0;
	}
#else
	return FALSE;
#endif
}

static gboolean
same_file (const char *path, const char *want, gsize want_len)
{
	char    *text = NULL;
	gsize    len = 0;
	gboolean same = g_file_get_contents (path, &text, &len, NULL) &&
	                len == want_len && memcmp (text, want, len) == 0;

	g_free (text);
	return same;
}

/* What the store reads from the file once it opens again. */
static void
check_saved_values (void)
{
	NemoConfigGroup *prefs, *windows;

	nemo_config_shutdown ();
	nemo_config_init ();
	prefs = nemo_config_get_group ("preferences");
	windows = nemo_config_get_group ("windows");
	check (nemo_config_get_boolean (prefs, "show-hidden-files") == TRUE);
	check (nemo_config_get_boolean (prefs, "sort-directories-first") == FALSE);
	check (nemo_config_get_boolean (windows, "show-dot-files") == TRUE);
}

int
main (int argc, char **argv)
{
	char            *tmp, *dir, *path, *blob = NULL;
	gsize            blob_len = 0;
	gboolean         oversized;
	NemoConfigGroup *prefs, *windows;

	if (argc < 2 || (strcmp (argv[1], "mode") != 0 && strcmp (argv[1], "oversized") != 0)) {
		g_printerr ("Syntax: %s mode|oversized\n", argv[0]);
		return EXIT_FAILURE;
	}
	oversized = strcmp (argv[1], "oversized") == 0;

	tmp = test_scratch_config_home ("nemo-config-unreadable-XXXXXX");

	/* The store only knows its path once it is open. */
	dir = nemo_get_user_directory ();
	path = g_build_filename (dir, "settings.shcl", NULL);
	g_mkdir_with_parents (dir, 0700);

	if (oversized) {
		blob_len = OVER_CAP;
		blob = g_malloc (blob_len);
		memset (blob, '#', blob_len);
		check (g_file_set_contents (path, blob, (gssize) blob_len, NULL));
	} else {
		char *text = NULL;

		check (g_file_set_contents (path, file_text, -1, NULL));
		if (!mode_blocks_reads ()) {
			g_print ("note: the file's mode can't stop reads here\n");
			return SKIP;
		}
		check (g_chmod (path, 0) == 0);
		if (g_file_get_contents (path, &text, NULL, NULL)) {
			g_print ("note: the file still reads at mode 0\n");
			g_free (text);
			g_chmod (path, 0600);
			return SKIP;
		}
	}

	nemo_config_init ();
	prefs = nemo_config_get_group ("preferences");
	windows = nemo_config_get_group ("windows");
	check (nemo_config_get_boolean (prefs, "show-hidden-files") == FALSE);

	nemo_config_set_boolean (prefs, "sort-directories-first", FALSE);
	nemo_config_flush ();
	check (nemo_config_get_boolean (prefs, "sort-directories-first") == FALSE);

	if (oversized) {
		check (same_file (path, blob, blob_len));
		/* Back under the cap, by an edit the monitor hasn't reported. */
		check (g_file_set_contents (path, file_text, -1, NULL));
	} else {
		check (g_chmod (path, 0600) == 0);
		check (same_file (path, file_text, strlen (file_text)));
	}

	/* Once it reads, the next save goes through, with the file's values and
	 * the change made while it couldn't be read. */
	nemo_config_set_boolean (windows, "show-dot-files", TRUE);
	nemo_config_flush ();
	check (nemo_config_get_boolean (prefs, "show-hidden-files") == TRUE);
	check (!same_file (path, file_text, strlen (file_text)));
	check_saved_values ();

	nemo_config_shutdown ();
	g_free (blob);
	g_free (path);
	g_free (dir);
	g_free (tmp);

	if (failures == 0)
		g_print ("nemo-config-unreadable %s: all checks passed\n", argv[1]);
	return failures == 0 ? 0 : 1;
}
