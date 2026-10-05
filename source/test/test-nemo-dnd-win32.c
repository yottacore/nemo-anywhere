/* A drag out of nemo put nothing in it that another program could read: the
 * toolkit offers its own target names and no file format, and there is no way
 * to add one from outside it. The drag is ours now, and what matters is what
 * the object it hands over holds.
 *
 * The checks read that object the way a drop target does - enumerate the
 * formats, ask for one, parse it back - with no drag running and no second
 * window, which is the part of this that can be checked at all.
 */

#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>

#define COBJMACROS
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <ole2.h>

#include <libnemo-private/nemo-dnd-win32.h>

#include "test-scratch.h"
#include "test-check.h"

static FORMATETC
wanted (UINT format)
{
	FORMATETC fmt;

	memset (&fmt, 0, sizeof (fmt));
	fmt.cfFormat = (CLIPFORMAT) format;
	fmt.dwAspect = DVASPECT_CONTENT;
	fmt.lindex = -1;
	fmt.tymed = TYMED_HGLOBAL;

	return fmt;
}

/* Whether the object offers @format, read from its enumerator rather than by
 * asking - a target picks what to ask for out of that list. */
static gboolean
offers (IDataObject *data, UINT format)
{
	IEnumFORMATETC *e = NULL;
	FORMATETC one;
	gboolean found = FALSE;

	if (FAILED (IDataObject_EnumFormatEtc (data, DATADIR_GET, &e)) || e == NULL) {
		return FALSE;
	}

	while (IEnumFORMATETC_Next (e, 1, &one, NULL) == S_OK) {
		if (one.cfFormat == format) {
			found = TRUE;
		}
	}

	IEnumFORMATETC_Release (e);
	return found;
}

/* The paths CF_HDROP holds, read with the same calls any other program uses. */
static char **
hdrop_paths (IDataObject *data)
{
	FORMATETC fmt = wanted (CF_HDROP);
	STGMEDIUM medium;
	GPtrArray *paths;
	UINT count, i;

	memset (&medium, 0, sizeof (medium));

	if (FAILED (IDataObject_GetData (data, &fmt, &medium))) {
		return NULL;
	}

	paths = g_ptr_array_new ();
	count = DragQueryFileW ((HDROP) medium.hGlobal, 0xFFFFFFFF, NULL, 0);

	for (i = 0; i < count; i++) {
		UINT chars = DragQueryFileW ((HDROP) medium.hGlobal, i, NULL, 0);
		gunichar2 *wide = g_new0 (gunichar2, chars + 1);

		DragQueryFileW ((HDROP) medium.hGlobal, i, (wchar_t *) wide, chars + 1);
		g_ptr_array_add (paths, g_utf16_to_utf8 (wide, -1, NULL, NULL, NULL));
		g_free (wide);
	}

	ReleaseStgMedium (&medium);

	g_ptr_array_add (paths, NULL);
	return (char **) g_ptr_array_free (paths, FALSE);
}

/* Whatever an hglobal format holds, as bytes plus its length. */
static char *
raw_format (IDataObject *data, UINT format, gsize *len)
{
	FORMATETC fmt = wanted (format);
	STGMEDIUM medium;
	char *out;
	gpointer from;

	memset (&medium, 0, sizeof (medium));

	if (FAILED (IDataObject_GetData (data, &fmt, &medium))) {
		return NULL;
	}

	*len = GlobalSize (medium.hGlobal);
	from = GlobalLock (medium.hGlobal);
	out = g_memdup2 (from, *len);
	GlobalUnlock (medium.hGlobal);

	ReleaseStgMedium (&medium);
	return out;
}

static char *
uri_for (const char *path)
{
	return g_filename_to_uri (path, NULL, NULL);
}

int
main (void)
{
	char *dir, *one, *two, *uri_one, *uri_two, *uri_list;
	IDataObject *data;
	char **paths;

	dir = test_scratch_dir ("nemo-dnd-XXXXXX", NULL);
	g_assert (dir != NULL);

	one = g_build_filename (dir, "one.txt", NULL);
	two = g_build_filename (dir, "two with space.txt", NULL);
	g_assert (g_file_set_contents (one, "1", -1, NULL));
	g_assert (g_file_set_contents (two, "2", -1, NULL));

	uri_one = uri_for (one);
	uri_two = uri_for (two);
	uri_list = g_strdup_printf ("%s\r\n%s\r\n", uri_one, uri_two);

	/* What another program is handed. */
	data = nemo_dnd_win32_data_object (uri_list, "icons here", GDK_ACTION_COPY);
	check (data != NULL);

	if (data != NULL) {
		char *raw;
		gsize len = 0;

		check (offers (data, CF_HDROP));
		check (offers (data, RegisterClipboardFormatW (L"text/uri-list")));
		check (offers (data, RegisterClipboardFormatW (L"x-special/gnome-icon-list")));

		/* The one every program reads, holding both names, spaces and all. */
		paths = hdrop_paths (data);
		check (paths != NULL);
		if (paths != NULL) {
			check (g_strv_length (paths) == 2);
			check (g_strcmp0 (paths[0], one) == 0);
			check (g_strcmp0 (paths[1], two) == 0);
			g_strfreev (paths);
		}

		/* Nemo's own payload has to come back byte for byte or our own
		 * windows read something else than they always have. */
		raw = raw_format (data, RegisterClipboardFormatW (L"x-special/gnome-icon-list"), &len);
		check (raw != NULL);
		check (len == strlen ("icons here"));
		check (raw != NULL && strncmp (raw, "icons here", len) == 0);
		g_free (raw);

		/* A preferred effect would be obeyed by the target, which turned a
		 * drag onto another folder on the same drive into a copy. */
		check (!offers (data, RegisterClipboardFormatW (L"Preferred DropEffect")));

		/* Asking twice has to answer twice: a target may well do that, and
		 * a block handed away once is gone. */
		paths = hdrop_paths (data);
		check (paths != NULL && g_strv_length (paths) == 2);
		g_strfreev (paths);

		/* A format nobody put in is refused rather than answered empty. */
		check (!offers (data, CF_WAVE));

		IDataObject_Release (data);
	}

	data = nemo_dnd_win32_data_object (uri_list, NULL, GDK_ACTION_MOVE);
	check (data != NULL);
	if (data != NULL) {
		/* Nothing was passed for it, so it is not offered at all. */
		check (!offers (data, RegisterClipboardFormatW (L"x-special/gnome-icon-list")));
		check (offers (data, CF_HDROP));

		IDataObject_Release (data);
	}

	/* A place with no local path cannot go to another program, and saying so is
	 * what sends the caller back to the toolkit's own drag. */
	check (nemo_dnd_win32_data_object ("trash:///\r\n", NULL, GDK_ACTION_COPY) == NULL);
	check (nemo_dnd_win32_data_object ("", NULL, GDK_ACTION_COPY) == NULL);
	check (nemo_dnd_win32_data_object (NULL, NULL, GDK_ACTION_COPY) == NULL);

	g_remove (one);
	g_remove (two);
	g_rmdir (dir);

	g_free (one);
	g_free (two);
	g_free (uri_one);
	g_free (uri_two);
	g_free (uri_list);
	g_free (dir);

	if (failures > 0) {
		g_printerr ("%d check(s) failed\n", failures);
		return 1;
	}

	g_print ("OK\n");
	return 0;
}
