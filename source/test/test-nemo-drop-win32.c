/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-drop-win32.c - files dropped in from a program that isn't Explorer.

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

/* A drag that has CF_HDROP and nothing else, as most programs make one, was
 * refused by every window, and one that only allows copy was refused wherever
 * a move would have been picked. Explorer's drags got through since they also
 * have the shell's id list.
 *
 * There's no mouse to drag with here, so the drop target Windows would call is
 * called directly, with the same arguments. Behind it is the toolkit's real
 * one, and a widget that decides like the folder views do. */

#include <config.h>

#define COBJMACROS

#include <string.h>
#include <stdlib.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>
#include <gdk/gdkwin32.h>

#include <windows.h>
#include <ole2.h>
#include <shlobj.h>

#include <libnemo-private/nemo-dnd.h>
#include <libnemo-private/nemo-dnd-win32.h>
#include <libnemo-private/nemo-drop-win32.h>
#include <libnemo-private/nemo-file.h>
#include <libnemo-private/nemo-file-attributes.h>
#include <libnemo-private/nemo-global-preferences.h>

#include "test-scratch.h"
#include "test-check.h"

#define ALL (DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK)

/* A data object with one format in it, as a plain program would make. */
typedef struct {
	IDataObject iface;
	LONG ref;
	UINT cf;
	HGLOBAL block;
} Plain;

static FORMATETC
wanted (UINT cf)
{
	FORMATETC fmt;

	memset (&fmt, 0, sizeof (fmt));
	fmt.cfFormat = (CLIPFORMAT) cf;
	fmt.dwAspect = DVASPECT_CONTENT;
	fmt.lindex = -1;
	fmt.tymed = TYMED_HGLOBAL;

	return fmt;
}

static HRESULT STDMETHODCALLTYPE
plain_query_interface (IDataObject *iface, REFIID iid, void **out)
{
	if (IsEqualIID (iid, &IID_IUnknown) || IsEqualIID (iid, &IID_IDataObject)) {
		*out = iface;
		IDataObject_AddRef (iface);
		return S_OK;
	}
	*out = NULL;
	return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE
plain_add_ref (IDataObject *iface)
{
	return (ULONG) InterlockedIncrement (&((Plain *) iface)->ref);
}

static ULONG STDMETHODCALLTYPE
plain_release (IDataObject *iface)
{
	Plain *self = (Plain *) iface;
	LONG left = InterlockedDecrement (&self->ref);

	if (left == 0) {
		GlobalFree (self->block);
		g_free (self);
	}
	return (ULONG) left;
}

static gboolean
plain_has (Plain *self, const FORMATETC *want)
{
	return want->cfFormat == self->cf && (want->tymed & TYMED_HGLOBAL) != 0;
}

static HRESULT STDMETHODCALLTYPE
plain_get_data (IDataObject *iface, FORMATETC *want, STGMEDIUM *out)
{
	Plain *self = (Plain *) iface;
	SIZE_T len;
	gpointer to;
	gconstpointer from;

	if (!plain_has (self, want)) {
		return DV_E_FORMATETC;
	}

	len = GlobalSize (self->block);
	memset (out, 0, sizeof (*out));
	out->tymed = TYMED_HGLOBAL;
	out->hGlobal = GlobalAlloc (GMEM_MOVEABLE, len);
	to = GlobalLock (out->hGlobal);
	from = GlobalLock (self->block);
	memcpy (to, from, len);
	GlobalUnlock (self->block);
	GlobalUnlock (out->hGlobal);

	return S_OK;
}

static HRESULT STDMETHODCALLTYPE
plain_get_data_here (G_GNUC_UNUSED IDataObject *iface, G_GNUC_UNUSED FORMATETC *want, G_GNUC_UNUSED STGMEDIUM *out)
{
	return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE
plain_query_get_data (IDataObject *iface, FORMATETC *want)
{
	return plain_has ((Plain *) iface, want) ? S_OK : DV_E_FORMATETC;
}

static HRESULT STDMETHODCALLTYPE
plain_get_canonical_format_etc (G_GNUC_UNUSED IDataObject *iface, G_GNUC_UNUSED FORMATETC *in, FORMATETC *out)
{
	out->ptd = NULL;
	return DATA_S_SAMEFORMATETC;
}

static HRESULT STDMETHODCALLTYPE
plain_set_data (G_GNUC_UNUSED IDataObject *iface, G_GNUC_UNUSED FORMATETC *want,
		G_GNUC_UNUSED STGMEDIUM *medium, G_GNUC_UNUSED BOOL take)
{
	return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE
plain_enum_format_etc (IDataObject *iface, DWORD direction, IEnumFORMATETC **out)
{
	FORMATETC fmt = wanted (((Plain *) iface)->cf);

	if (direction != DATADIR_GET) {
		return E_NOTIMPL;
	}
	return SHCreateStdEnumFmtEtc (1, &fmt, out);
}

static HRESULT STDMETHODCALLTYPE
plain_advise (G_GNUC_UNUSED IDataObject *iface, G_GNUC_UNUSED FORMATETC *fmt, G_GNUC_UNUSED DWORD flags,
	      G_GNUC_UNUSED IAdviseSink *sink, G_GNUC_UNUSED DWORD *connection)
{
	return OLE_E_ADVISENOTSUPPORTED;
}

static HRESULT STDMETHODCALLTYPE
plain_unadvise (G_GNUC_UNUSED IDataObject *iface, G_GNUC_UNUSED DWORD connection)
{
	return OLE_E_ADVISENOTSUPPORTED;
}

static HRESULT STDMETHODCALLTYPE
plain_enum_advise (G_GNUC_UNUSED IDataObject *iface, G_GNUC_UNUSED IEnumSTATDATA **out)
{
	return OLE_E_ADVISENOTSUPPORTED;
}

static IDataObjectVtbl plain_vtbl = {
	plain_query_interface,
	plain_add_ref,
	plain_release,
	plain_get_data,
	plain_get_data_here,
	plain_query_get_data,
	plain_get_canonical_format_etc,
	plain_set_data,
	plain_enum_format_etc,
	plain_advise,
	plain_unadvise,
	plain_enum_advise
};

static IDataObject *
plain_new (UINT cf, gconstpointer bytes, gsize len)
{
	Plain *self = g_new0 (Plain, 1);
	gpointer to;

	self->iface.lpVtbl = &plain_vtbl;
	self->ref = 1;
	self->cf = cf;
	self->block = GlobalAlloc (GMEM_MOVEABLE | GMEM_ZEROINIT, len);
	to = GlobalLock (self->block);
	memcpy (to, bytes, len);
	GlobalUnlock (self->block);

	return &self->iface;
}

/* CF_HDROP for one path: the header, the path in wide chars, two nuls. */
static IDataObject *
hdrop_only (const char *path)
{
	glong chars = 0;
	gunichar2 *wide = g_utf8_to_utf16 (path, -1, NULL, &chars, NULL);
	gsize len = sizeof (DROPFILES) + (chars + 2) * sizeof (gunichar2);
	guchar *bytes = g_malloc0 (len);
	DROPFILES *header = (DROPFILES *) bytes;
	IDataObject *data;

	header->pFiles = sizeof (DROPFILES);
	header->fWide = TRUE;
	memcpy (bytes + sizeof (DROPFILES), wide, chars * sizeof (gunichar2));

	data = plain_new (CF_HDROP, bytes, len);
	g_free (bytes);
	g_free (wide);

	return data;
}

/* The widget's side, decided as the folder views decide. */

static char *folder_uri;
static gboolean dropping;
static char *dropped;

static gboolean
on_motion (GtkWidget *widget, GdkDragContext *context, G_GNUC_UNUSED int x, G_GNUC_UNUSED int y, guint time)
{
	GdkAtom target = gtk_drag_dest_find_target (widget, context, NULL);

	if (target == GDK_NONE) {
		gdk_drag_status (context, 0, time);
		return TRUE;
	}

	gtk_drag_get_data (widget, context, target, time);
	return TRUE;
}

static gboolean
on_drop (GtkWidget *widget, GdkDragContext *context, G_GNUC_UNUSED int x, G_GNUC_UNUSED int y, guint time)
{
	GdkAtom target = gtk_drag_dest_find_target (widget, context, NULL);

	if (target == GDK_NONE) {
		gtk_drag_finish (context, FALSE, FALSE, time);
		return TRUE;
	}

	dropping = TRUE;
	gtk_drag_get_data (widget, context, target, time);
	return TRUE;
}

static void
on_received (G_GNUC_UNUSED GtkWidget *widget, GdkDragContext *context, G_GNUC_UNUSED int x, G_GNUC_UNUSED int y,
	     GtkSelectionData *data, G_GNUC_UNUSED guint info, guint time)
{
	char **uris = gtk_selection_data_get_uris (data);

	if (dropping) {
		dropping = FALSE;
		g_free (dropped);
		dropped = uris != NULL ? g_strdup (uris[0]) : NULL;
		gtk_drag_finish (context, uris != NULL, FALSE, time);
	} else if (uris != NULL) {
		gchar *source_fs = NULL;
		gboolean can_delete = FALSE;

		gdk_drag_status (context,
				 nemo_drag_default_drop_action_for_uri_list (context, folder_uri, uris[0],
									     &source_fs, &can_delete),
				 time);
		g_free (source_fs);
	} else {
		gdk_drag_status (context, 0, time);
	}

	g_strfreev (uris);
}

static NemoFile *
loaded (const char *path)
{
	g_autofree char *uri = g_filename_to_uri (path, NULL, NULL);
	NemoFile *file = nemo_file_get_by_uri (uri);
	int spins;

	nemo_file_monitor_add (file, file, NEMO_FILE_ATTRIBUTE_INFO);
	for (spins = 0; spins < 5000 && !nemo_file_check_if_ready (file, NEMO_FILE_ATTRIBUTE_INFO); spins++) {
		g_main_context_iteration (NULL, FALSE);
		g_usleep (2000);
	}
	check (nemo_file_check_if_ready (file, NEMO_FILE_ATTRIBUTE_INFO));

	return file;
}

static void
spin (int ms)
{
	gint64 until = g_get_monotonic_time () + ms * 1000;

	while (g_get_monotonic_time () < until) {
		while (g_main_context_iteration (NULL, FALSE)) {
		}
		g_usleep (5000);
	}
}

/* One whole drag, in over the window and dropped. The effects are what the
 * target answered at the way in and at the drop. */
static void
drag (IDropTarget *target, IDataObject *data, DWORD allowed, POINTL pt,
      DWORD *entered, DWORD *done)
{
	DWORD effect = allowed;

	g_clear_pointer (&dropped, g_free);

	check (IDropTarget_DragEnter (target, data, MK_LBUTTON, pt, &effect) == S_OK);
	*entered = effect;

	effect = allowed;
	pt.x++;
	check (IDropTarget_DragOver (target, MK_LBUTTON, pt, &effect) == S_OK);
	check (effect == *entered);

	if (*entered == DROPEFFECT_NONE) {
		check (IDropTarget_DragLeave (target) == S_OK);
		*done = DROPEFFECT_NONE;
		return;
	}

	effect = allowed;
	check (IDropTarget_Drop (target, data, 0, pt, &effect) == S_OK);
	*done = effect;
	spin (50);
}

int
main (int argc, char *argv[])
{
	g_autofree char *dir = NULL, *file = NULL, *file_uri = NULL;
	GtkWidget *window, *box;
	IDropTarget *target;
	IDataObject *data;
	NemoFile *folder;
	DWORD entered, done;
	RECT client;
	POINT at;
	POINTL pt;
	static const char words[] = "some words";

	dir = test_scratch_config_home ("nemo-drop-win32-XXXXXX");
	nemo_dnd_win32_prepare ();
	if (!gtk_init_check (&argc, &argv)) {
		g_print ("SKIP: no display\n");
		return 77;
	}
	/* A service session on Windows has no monitor, and then the toolkit
	   can't find the widget under a drop's screen point. */
	if (gdk_display_get_n_monitors (gdk_display_get_default ()) == 0) {
		g_print ("SKIP: no monitor\n");
		return 77;
	}
	nemo_global_preferences_init ();

	file = g_build_filename (dir, "dragged in.txt", NULL);
	check (g_file_set_contents (file, "x", -1, NULL));
	file_uri = g_filename_to_uri (file, NULL, NULL);
	folder_uri = g_filename_to_uri (dir, NULL, NULL);
	folder = loaded (dir);

	window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	gtk_window_set_default_size (GTK_WINDOW (window), 300, 200);
	box = gtk_event_box_new ();
	gtk_container_add (GTK_CONTAINER (window), box);
	gtk_drag_dest_set (box, 0, NULL, 0, GDK_ACTION_COPY | GDK_ACTION_MOVE | GDK_ACTION_LINK);
	gtk_drag_dest_add_uri_targets (box);
	g_signal_connect (box, "drag-motion", G_CALLBACK (on_motion), NULL);
	g_signal_connect (box, "drag-drop", G_CALLBACK (on_drop), NULL);
	g_signal_connect (box, "drag-data-received", G_CALLBACK (on_received), NULL);
	gtk_widget_show_all (window);
	spin (500);

	target = nemo_drop_win32_target (gtk_widget_get_window (window));
	check (target != NULL);
	if (target == NULL) {
		g_printerr ("no drop target of ours on the window\n");
		return EXIT_FAILURE;
	}

	GetClientRect (GDK_WINDOW_HWND (gtk_widget_get_window (window)), &client);
	at.x = client.right / 2;
	at.y = client.bottom / 2;
	ClientToScreen (GDK_WINDOW_HWND (gtk_widget_get_window (window)), &at);
	pt.x = at.x;
	pt.y = at.y;

	/* CF_HDROP alone, everything allowed: a move, as within one folder. */
	data = hdrop_only (file);
	drag (target, data, ALL, pt, &entered, &done);
	check (entered == DROPEFFECT_MOVE);
	check (done == DROPEFFECT_MOVE);
	check (g_strcmp0 (dropped, file_uri) == 0);
	check ((nemo_drop_win32_offered_actions () & GDK_ACTION_MOVE) != 0);

	/* Copy is all the program allows, so copy it is. */
	drag (target, data, DROPEFFECT_COPY, pt, &entered, &done);
	check (entered == DROPEFFECT_COPY);
	check (done == DROPEFFECT_COPY);
	check (g_strcmp0 (dropped, file_uri) == 0);
	check (nemo_drop_win32_offered_actions () == GDK_ACTION_COPY);
	IDataObject_Release (data);

	/* Text is no file, and is still refused. */
	data = plain_new (CF_TEXT, words, sizeof (words));
	drag (target, data, ALL, pt, &entered, &done);
	check (entered == DROPEFFECT_NONE);
	check (dropped == NULL);
	IDataObject_Release (data);

	/* The program it came from still has its file. Nothing here moves it;
	   the effect only tells the program what to do. */
	check (g_file_test (file, G_FILE_TEST_EXISTS));

	gtk_widget_destroy (window);
	nemo_file_monitor_remove (folder, folder);
	nemo_file_unref (folder);
	g_free (folder_uri);
	g_free (dropped);

	if (failures > 0) {
		g_printerr ("%d check(s) failed\n", failures);
		return EXIT_FAILURE;
	}
	g_print ("PASS\n");
	return EXIT_SUCCESS;
}
