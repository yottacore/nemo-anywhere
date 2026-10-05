/* The archive:// address Mount archive opens. gvfs takes the archive's own URI
 * as the host part, escaped twice, and a name with a space or a percent sign in
 * it is where a single escape goes wrong. Mounting itself needs a running gvfs
 * and is not covered here. */

#include <config.h>

#include <string.h>
#include <gio/gio.h>

#include <libnemo-private/nemo-file-utilities.h>
#include "test-check.h"

/* Undo both escapes and compare with the archive's own URI. */
static void
check_round_trip (const char *path)
{
	g_autoptr (GFile) archive = g_file_new_for_path (path);
	g_autoptr (GFile) mount = nemo_archive_mount_location (archive);
	g_autofree char *archive_uri = g_file_get_uri (archive);
	g_autofree char *mount_uri = g_file_get_uri (mount);
	g_autofree char *scheme = g_file_get_uri_scheme (mount);
	g_autofree char *host = NULL;
	g_autofree char *once = NULL;
	g_autofree char *twice = NULL;
	const char *start;
	const char *end;

	check (g_strcmp0 (scheme, "archive") == 0);
	check (g_str_has_prefix (mount_uri, "archive://"));
	if (!g_str_has_prefix (mount_uri, "archive://")) {
		return;
	}

	start = mount_uri + strlen ("archive://");
	end = strchr (start, '/');
	host = end != NULL ? g_strndup (start, end - start) : g_strdup (start);

	/* One escape would leave a bare ':' or '/' in the host. */
	check (strpbrk (host, ":/ ") == NULL);

	once = g_uri_unescape_string (host, NULL);
	twice = once != NULL ? g_uri_unescape_string (once, NULL) : NULL;

	check (g_strcmp0 (twice, archive_uri) == 0);
	if (g_strcmp0 (twice, archive_uri) != 0) {
		g_printerr ("  %s\n  -> %s\n  -> %s\n", path, mount_uri, twice);
	}
}

int
main (void)
{
	g_autoptr (GFile) plain = g_file_new_for_path ("/tmp/t.zip");
	g_autoptr (GFile) mount = nemo_archive_mount_location (plain);
	g_autofree char *mount_uri = g_file_get_uri (mount);

	/* The form gvfs was seen to mount. */
	check (g_strcmp0 (mount_uri, "archive://file%253A%252F%252F%252Ftmp%252Ft.zip/") == 0);

	check_round_trip ("/tmp/t.zip");
	check_round_trip ("/tmp/with space/My Stuff.zip");
	check_round_trip ("/tmp/100% done #2.tar.gz");
	check_round_trip ("/tmp/caf\xc3\xa9/r\xc3\xa9sum\xc3\xa9.7z");

	if (failures > 0) {
		g_printerr ("%d check(s) failed\n", failures);
		return 1;
	}

	g_print ("archive mount address: all checks passed\n");
	return 0;
}
