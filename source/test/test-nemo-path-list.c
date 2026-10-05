/* Text for "Copy Path(s)": one path per line, native separators, and the local
 * line ending - a list pasted into cmd.exe or notepad has to arrive as separate
 * lines. A location with no local path contributes its uri, so the text is
 * never silently shorter than what was selected. */

#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <gio/gio.h>

#include <libnemo-private/nemo-file-utilities.h>
#include "test-check.h"

#ifdef G_OS_WIN32
#define EOL "\r\n"
#define LOCAL_URI_A "file:///C:/tmp/one.txt"
#define LOCAL_PATH_A "C:\\tmp\\one.txt"
#define LOCAL_URI_B "file:///C:/tmp/two dir/three.txt"
#define LOCAL_PATH_B "C:\\tmp\\two dir\\three.txt"
#else
#define EOL "\n"
#define LOCAL_URI_A "file:///tmp/one.txt"
#define LOCAL_PATH_A "/tmp/one.txt"
#define LOCAL_URI_B "file:///tmp/two dir/three.txt"
#define LOCAL_PATH_B "/tmp/two dir/three.txt"
#endif

#define REMOTE_URI "smb://box/share/four.txt"

/* Builds the list from uris, runs it through the helper, frees the list. */
static char *
text_for_uris_with (const char * const *uris, char separator)
{
	GList *locations = NULL;
	char *text;
	int i;

	for (i = 0; uris[i] != NULL; i++) {
		locations = g_list_prepend (locations, g_file_new_for_uri (uris[i]));
	}

	locations = g_list_reverse (locations);
	text = nemo_build_path_list_text (locations, separator);
	g_list_free_full (locations, g_object_unref);

	return text;
}

static char *
text_for_uris (const char * const *uris)
{
	return text_for_uris_with (uris, G_DIR_SEPARATOR);
}

int
main (void)
{
	char *text;

	/* One item: the native path, and nothing around it - no quotes, no
	 * trailing line ending. Pasting it has to give a path a shell accepts. */
	{
		const char * const uris[] = { LOCAL_URI_A, NULL };

		text = text_for_uris (uris);
		check (g_strcmp0 (text, LOCAL_PATH_A) == 0);
		g_free (text);
	}

	/* Several items keep selection order, one per line, still with no
	 * trailing separator. A space in a name is passed through as it is. */
	{
		const char * const uris[] = { LOCAL_URI_A, LOCAL_URI_B, NULL };

		text = text_for_uris (uris);
		check (g_strcmp0 (text, LOCAL_PATH_A EOL LOCAL_PATH_B) == 0);
		g_free (text);
	}

	/* A remote location has no local path at all. It contributes its uri
	 * rather than dropping out, so the line count matches the selection. */
	{
		const char * const uris[] = { LOCAL_URI_A, REMOTE_URI, NULL };

		text = text_for_uris (uris);
		check (g_strcmp0 (text, LOCAL_PATH_A EOL REMOTE_URI) == 0);
		g_free (text);
	}

	/* Nothing selected is not an empty line on the clipboard - it is
	 * nothing at all, so the caller leaves the clipboard alone. */
	{
		const char * const uris[] = { NULL };

		text = text_for_uris (uris);
		check (text == NULL);
		g_free (text);
	}

#ifdef G_OS_WIN32
	/* The second clipboard item asks for the other separator. A uri is not
	 * touched by it - its slashes were never separators. */
	{
		const char * const uris[] = { LOCAL_URI_A, REMOTE_URI, NULL };

		text = text_for_uris_with (uris, '/');
		check (g_strcmp0 (text, "C:/tmp/one.txt" EOL REMOTE_URI) == 0);
		g_free (text);
	}
#endif

	if (failures > 0) {
		g_printerr ("%d check(s) failed\n", failures);
		return EXIT_FAILURE;
	}

	g_print ("OK\n");
	return EXIT_SUCCESS;
}
