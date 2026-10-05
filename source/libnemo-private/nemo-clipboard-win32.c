/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-clipboard-win32.c - file cut and copy on the Windows clipboard.

   Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu).

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; version 2 of the
   License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public
   License along with this program; if not, write to the
   Free Software Foundation, Inc., 51 Franklin Street, Suite 500,
   Boston, MA 02110-1335, USA.
*/

/* The toolkit only advertises a file cut or copy and hands it over when
 * somebody asks. Nothing on Windows works that way. Explorer expects CF_HDROP
 * to be sitting there already, and in a remote desktop session the redirector
 * asks the moment the clipboard changes, does not get an answer in time, and
 * puts the client's own clipboard back - which left a copy doing nothing at
 * all, nemo's own paste included. Everything is written up front here, so
 * there is nothing left to ask for.
 *
 * Nemo's own format goes on alongside the Windows ones. It carries the uris,
 * which paste still reads through the toolkit, so nothing on that side needed
 * changing; CF_HDROP is what every other program reads. A private format on
 * its own does not survive the redirector, but it rides along fine next to the
 * formats the redirector does recognize.
 */

#include <config.h>

#include "nemo-clipboard-win32.h"

#include <string.h>

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>	/* DROPFILES */
#include <ole2.h>
#include <gdk/gdkwin32.h>

/* What nemo's own paste reads. Same name the toolkit registers it under. */
#define NEMO_FILES_FORMAT L"x-special/gnome-copied-files"

static UINT
drop_effect_format (void)
{
	return RegisterClipboardFormatW (L"Preferred DropEffect");
}

/* Only one process can have the clipboard open at a time, and on a busy machine
 * somebody usually does - the remote desktop redirector and the clipboard
 * history service both take it for a moment whenever it changes. The call comes
 * straight back as failed, so the only thing to do is ask again. Without this a
 * paste reads as an empty clipboard every few tries. */
static gboolean
open_clipboard (HWND owner)
{
	guint tries;

	for (tries = 0; tries < 20; tries++) {
		if (OpenClipboard (owner)) {
			return TRUE;
		}

		g_usleep (20 * G_TIME_SPAN_MILLISECOND);
	}

	return FALSE;
}

static HWND
window_of (GtkWidget *widget)
{
	GdkWindow *window = gtk_widget_get_window (gtk_widget_get_toplevel (widget));

	return window != NULL ? (HWND) gdk_win32_window_get_handle (window) : NULL;
}

/* Local paths for @uris, or NULL if any of them has none - a place only nemo
 * understands cannot go on the Windows clipboard. */
static gchar **
paths_from_uris (GList *uris)
{
	GPtrArray *paths;
	GList *node;

	paths = g_ptr_array_new ();

	for (node = uris; node != NULL; node = node->next) {
		gchar *path = g_filename_from_uri (node->data, NULL, NULL);

		if (path == NULL) {
			g_ptr_array_set_free_func (paths, g_free);
			g_ptr_array_free (paths, TRUE);
			return NULL;
		}

		g_ptr_array_add (paths, path);
	}

	g_ptr_array_add (paths, NULL);
	return (gchar **) g_ptr_array_free (paths, FALSE);
}

static HGLOBAL
block_from (gconstpointer data, gsize len)
{
	HGLOBAL block;
	gpointer copy;

	block = GlobalAlloc (GMEM_MOVEABLE, len);
	if (block == NULL) {
		return NULL;
	}

	copy = GlobalLock (block);
	memcpy (copy, data, len);
	GlobalUnlock (block);

	return block;
}

/* A DROPFILES header, then the paths as wide strings one after another, each
 * ended with a nul and one more nul closing the list. */
static HGLOBAL
hdrop_block (gchar **paths)
{
	GArray *wide;
	DROPFILES header;
	HGLOBAL block;
	gunichar2 end = 0;
	guint i;

	wide = g_array_new (FALSE, FALSE, sizeof (gunichar2));

	for (i = 0; paths[i] != NULL; i++) {
		glong written = 0;
		gunichar2 *one = g_utf8_to_utf16 (paths[i], -1, NULL, &written, NULL);

		if (one == NULL) {
			g_array_free (wide, TRUE);
			return NULL;
		}

		g_array_append_vals (wide, one, written + 1);
		g_free (one);
	}

	g_array_append_val (wide, end);

	memset (&header, 0, sizeof (header));
	header.pFiles = sizeof (header);
	header.fWide = TRUE;

	block = GlobalAlloc (GMEM_MOVEABLE,
			     sizeof (header) + wide->len * sizeof (gunichar2));
	if (block != NULL) {
		guchar *copy = GlobalLock (block);

		memcpy (copy, &header, sizeof (header));
		memcpy (copy + sizeof (header), wide->data,
			wide->len * sizeof (gunichar2));
		GlobalUnlock (block);
	}

	g_array_free (wide, TRUE);
	return block;
}

/* "copy" or "cut", then one uri per line - what nemo's paste parses. */
static gchar *
nemo_payload (GList *uris, gboolean cut, gsize *len)
{
	GString *text;
	GList *node;

	text = g_string_new (cut ? "cut" : "copy");

	for (node = uris; node != NULL; node = node->next) {
		g_string_append_c (text, '\n');
		g_string_append (text, node->data);
	}

	*len = text->len;
	return g_string_free (text, FALSE);
}

gboolean
nemo_clipboard_win32_set_text (GtkWidget  *widget,
			       const char *text)
{
	gunichar2 *wide;
	glong wide_len = 0;
	HGLOBAL block;

	wide = g_utf8_to_utf16 (text, -1, NULL, &wide_len, NULL);
	if (wide == NULL) {
		return FALSE;
	}

	block = block_from (wide, (wide_len + 1) * sizeof (gunichar2));
	g_free (wide);

	if (block == NULL) {
		return FALSE;
	}

	if (!open_clipboard (window_of (widget))) {
		GlobalFree (block);
		return FALSE;
	}

	EmptyClipboard ();

	if (SetClipboardData (CF_UNICODETEXT, block) == NULL) {
		CloseClipboard ();
		GlobalFree (block);
		return FALSE;
	}

	CloseClipboard ();	/* the clipboard owns the block now */
	return TRUE;
}

/* An entry or text view copies the way the toolkit always has: it claims the
 * clipboard and hands the text over when asked. That is the write that goes
 * missing under a remote desktop, so the selection is written out here too,
 * once the widget's own handler has run. A cut is read before that handler
 * deletes it, which is why the hook runs first and the write waits. */

typedef struct {
	GtkWidget *widget;
	char *text;
} PendingText;

static char *
selected_text (GtkWidget *widget)
{
	if (GTK_IS_ENTRY (widget) && !gtk_entry_get_visibility (GTK_ENTRY (widget))) {
		return NULL;
	}
	if (GTK_IS_EDITABLE (widget)) {
		gint start, end;

		if (gtk_editable_get_selection_bounds (GTK_EDITABLE (widget), &start, &end)) {
			return gtk_editable_get_chars (GTK_EDITABLE (widget), start, end);
		}
		return NULL;
	}
	if (GTK_IS_TEXT_VIEW (widget)) {
		GtkTextBuffer *buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (widget));
		GtkTextIter start, end;

		if (gtk_text_buffer_get_selection_bounds (buffer, &start, &end)) {
			return gtk_text_buffer_get_text (buffer, &start, &end, FALSE);
		}
	}
	return NULL;
}

static gboolean
write_pending_text (gpointer data)
{
	PendingText *pending = data;

	nemo_clipboard_win32_set_text (pending->widget, pending->text);
	g_object_unref (pending->widget);
	g_free (pending->text);
	g_free (pending);
	return G_SOURCE_REMOVE;
}

static gboolean
editable_copy_hook (G_GNUC_UNUSED GSignalInvocationHint *hint,
		    G_GNUC_UNUSED guint                  n_params,
		    const GValue          *params,
		    G_GNUC_UNUSED gpointer               data)
{
	GtkWidget *widget = g_value_get_object (&params[0]);
	char *text = selected_text (widget);
	PendingText *pending;

	if (text == NULL || *text == '\0') {
		g_free (text);
		return TRUE;
	}
	pending = g_new0 (PendingText, 1);
	pending->widget = g_object_ref (widget);
	pending->text = text;
	g_idle_add_full (G_PRIORITY_HIGH, write_pending_text, pending, NULL);
	/* cppcheck-suppress memleak ; the idle owns pending and frees it */
	return TRUE;
}

void
nemo_clipboard_win32_watch_editables (void)
{
	static const char *names[] = { "copy-clipboard", "cut-clipboard" };
	GType types[] = { GTK_TYPE_ENTRY, GTK_TYPE_TEXT_VIEW };
	guint i, j;

	for (i = 0; i < G_N_ELEMENTS (types); i++) {
		g_type_class_ref (types[i]);	/* the signals exist once the class does */
		for (j = 0; j < G_N_ELEMENTS (names); j++) {
			g_signal_add_emission_hook (g_signal_lookup (names[j], types[i]), 0,
						    editable_copy_hook, NULL, NULL);
		}
	}
}

gboolean
nemo_clipboard_win32_set_files (GtkWidget *widget,
				GList     *uris,
				gboolean   cut)
{
	gchar **paths;
	gchar *joined, *payload;
	gunichar2 *wide;
	glong wide_len = 0;
	gsize payload_len = 0;
	DWORD effect;
	HGLOBAL drop, effect_block, text_block, nemo_block;

	if (uris == NULL) {
		return FALSE;
	}

	paths = paths_from_uris (uris);
	if (paths == NULL || paths[0] == NULL) {
		g_strfreev (paths);
		return FALSE;
	}

	/* Pasted into anything that takes text, the paths are what is wanted. */
	joined = g_strjoinv ("\r\n", paths);
	wide = g_utf8_to_utf16 (joined, -1, NULL, &wide_len, NULL);
	g_free (joined);

	payload = nemo_payload (uris, cut, &payload_len);

	drop = hdrop_block (paths);
	effect = cut ? DROPEFFECT_MOVE : DROPEFFECT_COPY;
	effect_block = block_from (&effect, sizeof (effect));
	text_block = wide != NULL ? block_from (wide, (wide_len + 1) * sizeof (gunichar2)) : NULL;
	nemo_block = block_from (payload, payload_len);

	g_strfreev (paths);
	g_free (wide);
	g_free (payload);

	if (drop == NULL || effect_block == NULL || nemo_block == NULL) {
		goto give_up;
	}

	if (!open_clipboard (window_of (widget))) {
		goto give_up;
	}

	EmptyClipboard ();

	SetClipboardData (CF_HDROP, drop);
	SetClipboardData (drop_effect_format (), effect_block);
	SetClipboardData (RegisterClipboardFormatW (NEMO_FILES_FORMAT), nemo_block);
	if (text_block != NULL) {
		SetClipboardData (CF_UNICODETEXT, text_block);
	}

	CloseClipboard ();	/* the clipboard owns the blocks now */
	return TRUE;

give_up:
	if (drop != NULL) {
		GlobalFree (drop);
	}
	if (effect_block != NULL) {
		GlobalFree (effect_block);
	}
	if (text_block != NULL) {
		GlobalFree (text_block);
	}
	if (nemo_block != NULL) {
		GlobalFree (nemo_block);
	}
	return FALSE;
}

gboolean
nemo_clipboard_win32_has_files (void)
{
	return IsClipboardFormatAvailable (CF_HDROP);
}

/* Returns: (transfer full): free with g_list_free_full (list, g_free) */
GList *
nemo_clipboard_win32_get_files (gboolean *cut)
{
	HDROP drop;
	HGLOBAL effect_block;
	GList *uris = NULL;
	UINT count, i;

	if (cut != NULL) {
		*cut = FALSE;
	}

	if (!IsClipboardFormatAvailable (CF_HDROP) || !open_clipboard (NULL)) {
		return NULL;
	}

	drop = (HDROP) GetClipboardData (CF_HDROP);
	count = drop != NULL ? DragQueryFileW (drop, 0xFFFFFFFF, NULL, 0) : 0;

	for (i = 0; i < count; i++) {
		UINT chars = DragQueryFileW (drop, i, NULL, 0);
		gunichar2 *wide = g_new0 (gunichar2, chars + 1);
		gchar *path, *uri;

		DragQueryFileW (drop, i, (wchar_t *) wide, chars + 1);
		path = g_utf16_to_utf8 (wide, -1, NULL, NULL, NULL);
		g_free (wide);

		if (path == NULL) {
			continue;
		}

		uri = g_filename_to_uri (path, NULL, NULL);
		g_free (path);

		if (uri != NULL) {
			uris = g_list_prepend (uris, uri);
		}
	}

	/* Explorer writes copy as COPY|LINK, so read the move bit rather than
	 * comparing the whole value. */
	effect_block = GetClipboardData (drop_effect_format ());
	if (effect_block != NULL && cut != NULL) {
		DWORD *effect = GlobalLock (effect_block);

		if (effect != NULL) {
			*cut = (*effect & DROPEFFECT_MOVE) != 0;
			GlobalUnlock (effect_block);
		}
	}

	CloseClipboard ();
	return g_list_reverse (uris);
}

void
nemo_clipboard_win32_clear (void)
{
	if (open_clipboard (NULL)) {
		EmptyClipboard ();
		CloseClipboard ();
	}
}
