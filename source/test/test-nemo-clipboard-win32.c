/* Copying a path put nothing on the Windows clipboard that anyone else could
 * read straight away: the toolkit only advertises the text and hands it over
 * when asked, and in a remote desktop session the redirector asks, gets no
 * reply in time, and puts the client's own clipboard back - so the copy read
 * as having done nothing at all. A file cut or copy went the same way, and took
 * nemo's own paste with it.
 *
 * The checks read the clipboard the way another program would, with no message
 * loop running, which is what the advertise-and-wait path can't handle.
 */

#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <ole2.h>

#include <libnemo-private/nemo-clipboard.h>
#include <libnemo-private/nemo-clipboard-win32.h>

#include "test-scratch.h"
#include "test-check.h"

/* Anything reading the clipboard has to retry: only one process can have it
 * open, and something usually does for a moment after it changes. */
static gboolean
open_clipboard (void)
{
	int tries;

	for (tries = 0; tries < 20; tries++) {
		if (OpenClipboard (NULL)) {
			return TRUE;
		}

		g_usleep (20 * G_TIME_SPAN_MILLISECOND);
	}

	return FALSE;
}

/* Whatever is in CF_UNICODETEXT right now, as utf-8, or NULL. */
static char *
clipboard_text (void)
{
	HANDLE block;
	gpointer text;
	char *ret = NULL;

	if (!open_clipboard ()) {
		return NULL;
	}

	block = GetClipboardData (CF_UNICODETEXT);
	if (block != NULL) {
		text = GlobalLock (block);
		if (text != NULL) {
			ret = g_utf16_to_utf8 (text, -1, NULL, NULL, NULL);
			GlobalUnlock (block);
		}
	}

	CloseClipboard ();
	return ret;
}

/* The paths in CF_HDROP, read with the same call any other program uses. */
static GPtrArray *
clipboard_drop_paths (void)
{
	GPtrArray *paths;
	HDROP drop;
	UINT count, i;

	if (!open_clipboard ()) {
		return NULL;
	}

	drop = (HDROP) GetClipboardData (CF_HDROP);
	if (drop == NULL) {
		CloseClipboard ();
		return NULL;
	}

	paths = g_ptr_array_new_with_free_func (g_free);
	count = DragQueryFileW (drop, 0xFFFFFFFF, NULL, 0);

	for (i = 0; i < count; i++) {
		UINT chars = DragQueryFileW (drop, i, NULL, 0);
		gunichar2 *wide = g_new0 (gunichar2, chars + 1);

		DragQueryFileW (drop, i, (wchar_t *) wide, chars + 1);
		g_ptr_array_add (paths, g_utf16_to_utf8 (wide, -1, NULL, NULL, NULL));
		g_free (wide);
	}

	CloseClipboard ();
	return paths;
}

static DWORD
clipboard_drop_effect (void)
{
	HGLOBAL block;
	DWORD value = 0;

	if (!open_clipboard ()) {
		return 0;
	}

	block = GetClipboardData (RegisterClipboardFormatW (L"Preferred DropEffect"));
	if (block != NULL) {
		DWORD *held = GlobalLock (block);

		if (held != NULL) {
			value = *held;
			GlobalUnlock (block);
		}
	}

	CloseClipboard ();
	return value;
}

static gboolean
clipboard_has_format (const wchar_t *name)
{
	return IsClipboardFormatAvailable (RegisterClipboardFormatW (name));
}

/* A cut or a copy of 2 files, checked from the outside and then read back. */
static void
check_files (GtkWidget *window, const char *dir, gboolean cut)
{
	char *one = g_build_filename (dir, "one.txt", NULL);
	char *two = g_build_filename (dir, "two two.txt", NULL);
	GList *uris = NULL;
	GList *back, *node;
	GPtrArray *paths;
	gboolean got_cut = !cut;

	check (g_file_set_contents (one, "one", -1, NULL));
	check (g_file_set_contents (two, "two", -1, NULL));

	uris = g_list_append (uris, g_filename_to_uri (one, NULL, NULL));
	uris = g_list_append (uris, g_filename_to_uri (two, NULL, NULL));

	check (nemo_clipboard_win32_set_files (window, uris, cut));
	check (nemo_clipboard_win32_has_files ());

	/* What Explorer reads. A name with a space in it is the one that breaks
	 * if the list is ever built as one string rather than nul-separated. */
	paths = clipboard_drop_paths ();
	check (paths != NULL);
	if (paths != NULL) {
		check (paths->len == 2);
		if (paths->len == 2) {
			check (g_strcmp0 (g_ptr_array_index (paths, 0), one) == 0);
			check (g_strcmp0 (g_ptr_array_index (paths, 1), two) == 0);
		}
		g_ptr_array_free (paths, TRUE);
	}

	check (clipboard_drop_effect () == (DWORD) (cut ? DROPEFFECT_MOVE : DROPEFFECT_COPY));

	/* Nemo's own paste still reads this one through the toolkit, so it has to
	 * be published alongside rather than replaced. */
	check (clipboard_has_format (L"x-special/gnome-copied-files"));

	/* And a copy made anywhere else reads back as the same list. */
	back = nemo_clipboard_win32_get_files (&got_cut);
	check (g_list_length (back) == 2);
	check (got_cut == cut);
	for (node = back; node != NULL; node = node->next) {
		check (g_list_find_custom (uris, node->data, (GCompareFunc) g_strcmp0) != NULL);
	}

	g_list_free_full (back, g_free);
	g_list_free_full (uris, g_free);
	g_remove (one);
	g_remove (two);
	g_free (one);
	g_free (two);
}

int
main (int argc, char *argv[])
{
	GtkWidget *window;
	const char *first = "C:\\some\\path\\one.txt";
	const char *second = "C:\\some\\path\\two.txt\r\nC:\\some\\path\\three.txt";
	char *got, *dir;

	if (!gtk_init_check (&argc, &argv)) {
		g_print ("SKIP: no display\n");
		return 77;
	}

	window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	gtk_widget_realize (window);

	nemo_clipboard_set_text (window, first);
	got = clipboard_text ();
	check (g_strcmp0 (got, first) == 0);
	g_free (got);

	/* A second copy has to replace the first, not sit behind it. */
	nemo_clipboard_set_text (window, second);
	got = clipboard_text ();
	check (g_strcmp0 (got, second) == 0);
	g_free (got);

	/* An entry's own copy and cut end up written out rather than advertised.
	 * With only the toolkit's advertised copy, nothing can be read back here. */
	{
		GtkWidget *entry = gtk_entry_new ();

		gtk_container_add (GTK_CONTAINER (window), entry);
		gtk_widget_show (entry);
		nemo_clipboard_win32_watch_editables ();

		gtk_entry_set_text (GTK_ENTRY (entry), "copied from an entry");
		gtk_editable_select_region (GTK_EDITABLE (entry), 0, -1);
		g_signal_emit_by_name (entry, "copy-clipboard");
		while (g_main_context_iteration (NULL, FALSE));
		got = clipboard_text ();
		check (g_strcmp0 (got, "copied from an entry") == 0);
		g_free (got);

		gtk_editable_select_region (GTK_EDITABLE (entry), 0, 6);
		g_signal_emit_by_name (entry, "cut-clipboard");
		while (g_main_context_iteration (NULL, FALSE));
		got = clipboard_text ();
		check (g_strcmp0 (got, "copied") == 0);
		check (g_strcmp0 (gtk_entry_get_text (GTK_ENTRY (entry)), " from an entry") == 0);
		g_free (got);
	}

	dir = test_scratch_dir ("nemo-clip-XXXXXX", NULL);
	if (dir != NULL) {
		check_files (window, dir, FALSE);
		check_files (window, dir, TRUE);

		nemo_clipboard_win32_clear ();
		check (!nemo_clipboard_win32_has_files ());

		g_rmdir (dir);
		g_free (dir);
	} else {
		g_printerr ("SKIP files: no temp directory\n");
	}

	gtk_widget_destroy (window);

	if (failures > 0) {
		g_printerr ("%d failure(s)\n", failures);
		return EXIT_FAILURE;
	}

	g_print ("clipboard win32: all checks passed\n");
	return EXIT_SUCCESS;
}
