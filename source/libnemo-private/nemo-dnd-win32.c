/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-dnd-win32.c - dragging files out to another program on Windows.

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

/* The toolkit does run a real drag on Windows, but the only file formats it
 * puts in it are its own target names, which nothing outside it reads. There is
 * no way to add a format from outside, and CF_HDROP - the one every program
 * does read - has no name to register it under. So the drag is ours, the same
 * way the clipboard is, and for the same reason.
 *
 * What goes in it is what Explorer puts in its own drags: CF_HDROP, the shell's
 * id list, and the preferred effect. Nemo's own formats ride along beside them
 * under their toolkit names, so a drop back into one of our own windows still
 * arrives as the icon list the drop code has always read.
 */

#include <config.h>

#include "nemo-dnd-win32.h"

#include "nemo-file-operations.h"

#include <string.h>

#define COBJMACROS
#include <windows.h>
#include <ole2.h>
#include <shlobj.h>
#include <gdk/gdkwin32.h>

/* The toolkit's own drag on Windows only reaches our own windows, and the
 * protocol that reaches other programs is behind this switch. Our drags speak
 * that one, so our own windows have to be listening on it too. */
#define OLE_DND_ENV "GDK_WIN32_USE_EXPERIMENTAL_OLE2_DND"

/* Nemo's own payloads, under the names the toolkit registers them as. */
#define ICON_LIST_FORMAT L"x-special/gnome-icon-list"
#define URI_LIST_FORMAT  L"text/uri-list"

/* The shell's own names. The CFSTR_ macros follow whether UNICODE is defined,
 * which it is not here, so they are spelled out wide. */
#define SHELL_ID_LIST_FORMAT L"Shell IDList Array"
#define PREFERRED_EFFECT_FORMAT L"Preferred DropEffect"
#define PERFORMED_EFFECT_FORMAT L"Performed DropEffect"

#ifndef CLR_NONE
#define CLR_NONE 0xFFFFFFFFL
#endif

typedef struct {
	FORMATETC fmt;
	STGMEDIUM medium;
} DragFormat;

typedef struct {
	IDataObject iface;
	LONG ref;
	GArray *formats;	/* DragFormat, in the order they are offered */
	DWORD performed;	/* what the target said it did, if it said */
	gboolean read_own;	/* whether the target asked for one of nemo's formats */
} DataObject;

typedef struct {
	IDropSource iface;
	LONG ref;
	DWORD button;		/* the mouse button that started it */
	gboolean over_own;	/* whether the pointer is on one of our windows */
} DropSource;

/* Blocks and formats */

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

static HGLOBAL
block_copy (HGLOBAL block)
{
	SIZE_T len;
	HGLOBAL copy;
	gconstpointer from;

	len = GlobalSize (block);
	from = GlobalLock (block);
	copy = from != NULL ? block_from (from, len) : NULL;
	GlobalUnlock (block);

	return copy;
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

/* An id list runs to a zero-length item, and its own two bytes count. */
static guint
pidl_size (LPCITEMIDLIST pidl)
{
	const BYTE *walk = (const BYTE *) pidl;
	guint total = 0;

	while (((const SHITEMID *) walk)->cb != 0) {
		total += ((const SHITEMID *) walk)->cb;
		walk += ((const SHITEMID *) walk)->cb;
	}

	return total + sizeof (USHORT);
}

/* The shell's own file drag format: a table of offsets, the parent folder's id
 * list, then one per file. The desktop stands in as the parent, which is what
 * makes the children below it the fully qualified lists we already have.
 *
 * NULL if any path will not parse, and then the format is simply not offered -
 * CF_HDROP is what the programs this is for read anyway. */
static HGLOBAL
shell_id_list_block (gchar **paths)
{
	GPtrArray *pidls;
	HGLOBAL block = NULL;
	guchar *out;
	UINT *offsets;
	guint count, header_len, total, at, i;
	static const USHORT desktop = 0;	/* an id list with nothing in it */

	pidls = g_ptr_array_new ();

	for (i = 0; paths[i] != NULL; i++) {
		gunichar2 *wide = g_utf8_to_utf16 (paths[i], -1, NULL, NULL, NULL);
		LPITEMIDLIST pidl = NULL;

		if (wide == NULL ||
		    FAILED (SHParseDisplayName ((PCWSTR) wide, NULL, &pidl, 0, NULL))) {
			g_free (wide);
			goto give_up;
		}

		g_free (wide);
		g_ptr_array_add (pidls, pidl);
	}

	count = pidls->len;
	if (count == 0) {
		goto give_up;
	}

	/* cidl, the parent's offset, then one offset per file. */
	header_len = sizeof (UINT) * (count + 2);
	total = header_len + sizeof (desktop);

	for (i = 0; i < count; i++) {
		total += pidl_size (g_ptr_array_index (pidls, i));
	}

	block = GlobalAlloc (GMEM_MOVEABLE, total);
	if (block == NULL) {
		goto give_up;
	}

	out = GlobalLock (block);
	offsets = (UINT *) out;
	offsets[0] = count;

	at = header_len;
	offsets[1] = at;
	memcpy (out + at, &desktop, sizeof (desktop));
	at += sizeof (desktop);

	for (i = 0; i < count; i++) {
		LPCITEMIDLIST pidl = g_ptr_array_index (pidls, i);
		guint len = pidl_size (pidl);

		offsets[i + 2] = at;
		memcpy (out + at, pidl, len);
		at += len;
	}

	GlobalUnlock (block);

give_up:
	for (i = 0; i < pidls->len; i++) {
		CoTaskMemFree (g_ptr_array_index (pidls, i));
	}
	g_ptr_array_free (pidls, TRUE);

	return block;
}

/* The data object */

static void
format_add (DataObject *self, UINT cf, HGLOBAL block)
{
	DragFormat entry;

	if (block == NULL) {
		return;
	}

	memset (&entry, 0, sizeof (entry));
	entry.fmt.cfFormat = (CLIPFORMAT) cf;
	entry.fmt.dwAspect = DVASPECT_CONTENT;
	entry.fmt.lindex = -1;
	entry.fmt.tymed = TYMED_HGLOBAL;
	entry.medium.tymed = TYMED_HGLOBAL;
	entry.medium.hGlobal = block;

	g_array_append_val (self->formats, entry);
}

static DragFormat *
format_find (DataObject *self, const FORMATETC *want)
{
	guint i;

	for (i = 0; i < self->formats->len; i++) {
		DragFormat *entry = &g_array_index (self->formats, DragFormat, i);

		if (entry->fmt.cfFormat == want->cfFormat &&
		    entry->fmt.dwAspect == want->dwAspect &&
		    (entry->fmt.tymed & want->tymed) != 0) {
			return entry;
		}
	}

	return NULL;
}

static HRESULT STDMETHODCALLTYPE
data_query_interface (IDataObject *iface, REFIID iid, void **out)
{
	if (out == NULL) {
		return E_INVALIDARG;
	}

	if (IsEqualIID (iid, &IID_IUnknown) || IsEqualIID (iid, &IID_IDataObject)) {
		*out = iface;
		IDataObject_AddRef (iface);
		return S_OK;
	}

	*out = NULL;
	return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE
data_add_ref (IDataObject *iface)
{
	return InterlockedIncrement (&((DataObject *) iface)->ref);
}

static ULONG STDMETHODCALLTYPE
data_release (IDataObject *iface)
{
	DataObject *self = (DataObject *) iface;
	LONG left = InterlockedDecrement (&self->ref);
	guint i;

	if (left > 0) {
		return left;
	}

	for (i = 0; i < self->formats->len; i++) {
		ReleaseStgMedium (&g_array_index (self->formats, DragFormat, i).medium);
	}

	g_array_free (self->formats, TRUE);
	g_free (self);

	return 0;
}

static HRESULT STDMETHODCALLTYPE
data_get_data (IDataObject *iface, FORMATETC *want, STGMEDIUM *out)
{
	DataObject *self = (DataObject *) iface;
	DragFormat *entry;

	if (want == NULL || out == NULL) {
		return E_INVALIDARG;
	}

	entry = format_find (self, want);
	if (entry == NULL) {
		return DV_E_FORMATETC;
	}

	/* Only another nemo asks for these, and it moves the files itself, so a
	 * move must not also remove them here. Covers a second copy of nemo in
	 * its own process as well as this one. */
	if (want->cfFormat == RegisterClipboardFormatW (ICON_LIST_FORMAT) ||
	    want->cfFormat == RegisterClipboardFormatW (URI_LIST_FORMAT)) {
		self->read_own = TRUE;
	}

	memset (out, 0, sizeof (*out));
	out->tymed = entry->medium.tymed;

	switch (entry->medium.tymed) {
	case TYMED_HGLOBAL:
		out->hGlobal = block_copy (entry->medium.hGlobal);
		if (out->hGlobal == NULL) {
			return E_OUTOFMEMORY;
		}
		return S_OK;

	case TYMED_ISTREAM:
		out->pstm = entry->medium.pstm;
		IStream_AddRef (out->pstm);
		return S_OK;

	default:
		return DV_E_TYMED;
	}
}

static HRESULT STDMETHODCALLTYPE
data_get_data_here (G_GNUC_UNUSED IDataObject *iface, G_GNUC_UNUSED FORMATETC *want, G_GNUC_UNUSED STGMEDIUM *out)
{
	return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE
data_query_get_data (IDataObject *iface, FORMATETC *want)
{
	if (want == NULL) {
		return E_INVALIDARG;
	}

	return format_find ((DataObject *) iface, want) != NULL ? S_OK : DV_E_FORMATETC;
}

static HRESULT STDMETHODCALLTYPE
data_get_canonical_format_etc (G_GNUC_UNUSED IDataObject *iface, G_GNUC_UNUSED FORMATETC *in, FORMATETC *out)
{
	if (out == NULL) {
		return E_INVALIDARG;
	}

	out->ptd = NULL;
	return DATA_S_SAMEFORMATETC;
}

/* Targets write back through this - the drag image helper stores the picture
 * here, and a shell target reports what it did with the files. Anything handed
 * over with fRelease is ours to free from now on. */
static HRESULT STDMETHODCALLTYPE
data_set_data (IDataObject *iface, FORMATETC *want, STGMEDIUM *medium, BOOL take)
{
	DataObject *self = (DataObject *) iface;
	DragFormat *entry;
	DragFormat fresh;
	STGMEDIUM mine;

	if (want == NULL || medium == NULL) {
		return E_INVALIDARG;
	}

	if (take) {
		mine = *medium;
	} else if (medium->tymed == TYMED_HGLOBAL) {
		memset (&mine, 0, sizeof (mine));
		mine.tymed = TYMED_HGLOBAL;
		mine.hGlobal = block_copy (medium->hGlobal);
		if (mine.hGlobal == NULL) {
			return E_OUTOFMEMORY;
		}
	} else {
		return E_NOTIMPL;
	}

	if (want->cfFormat == RegisterClipboardFormatW (PERFORMED_EFFECT_FORMAT) &&
	    mine.tymed == TYMED_HGLOBAL) {
		const DWORD *said = GlobalLock (mine.hGlobal);

		if (said != NULL) {
			self->performed = *said;
		}
		GlobalUnlock (mine.hGlobal);
	}

	entry = format_find (self, want);
	if (entry != NULL) {
		ReleaseStgMedium (&entry->medium);
		entry->medium = mine;
		return S_OK;
	}

	memset (&fresh, 0, sizeof (fresh));
	fresh.fmt = *want;
	fresh.medium = mine;
	g_array_append_val (self->formats, fresh);

	return S_OK;
}

static HRESULT STDMETHODCALLTYPE
data_enum_format_etc (IDataObject *iface, DWORD direction, IEnumFORMATETC **out)
{
	DataObject *self = (DataObject *) iface;
	FORMATETC *list;
	HRESULT hr;
	guint i;

	if (out == NULL) {
		return E_INVALIDARG;
	}

	if (direction != DATADIR_GET) {
		*out = NULL;
		return E_NOTIMPL;
	}

	list = g_new0 (FORMATETC, self->formats->len);
	for (i = 0; i < self->formats->len; i++) {
		list[i] = g_array_index (self->formats, DragFormat, i).fmt;
	}

	hr = SHCreateStdEnumFmtEtc (self->formats->len, list, out);
	g_free (list);

	return hr;
}

static HRESULT STDMETHODCALLTYPE
data_advise (G_GNUC_UNUSED IDataObject *iface, G_GNUC_UNUSED FORMATETC *fmt, G_GNUC_UNUSED DWORD flags,
	     G_GNUC_UNUSED IAdviseSink *sink, G_GNUC_UNUSED DWORD *connection)
{
	return OLE_E_ADVISENOTSUPPORTED;
}

static HRESULT STDMETHODCALLTYPE
data_unadvise (G_GNUC_UNUSED IDataObject *iface, G_GNUC_UNUSED DWORD connection)
{
	return OLE_E_ADVISENOTSUPPORTED;
}

static HRESULT STDMETHODCALLTYPE
data_enum_advise (G_GNUC_UNUSED IDataObject *iface, G_GNUC_UNUSED IEnumSTATDATA **out)
{
	return OLE_E_ADVISENOTSUPPORTED;
}

static IDataObjectVtbl data_vtbl = {
	data_query_interface,
	data_add_ref,
	data_release,
	data_get_data,
	data_get_data_here,
	data_query_get_data,
	data_get_canonical_format_etc,
	data_set_data,
	data_enum_format_etc,
	data_advise,
	data_unadvise,
	data_enum_advise
};

/* The drop source */

static HRESULT STDMETHODCALLTYPE
source_query_interface (IDropSource *iface, REFIID iid, void **out)
{
	if (out == NULL) {
		return E_INVALIDARG;
	}

	if (IsEqualIID (iid, &IID_IUnknown) || IsEqualIID (iid, &IID_IDropSource)) {
		*out = iface;
		IDropSource_AddRef (iface);
		return S_OK;
	}

	*out = NULL;
	return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE
source_add_ref (IDropSource *iface)
{
	return InterlockedIncrement (&((DropSource *) iface)->ref);
}

static ULONG STDMETHODCALLTYPE
source_release (IDropSource *iface)
{
	DropSource *self = (DropSource *) iface;
	LONG left = InterlockedDecrement (&self->ref);

	if (left == 0) {
		g_free (self);
	}

	return left > 0 ? left : 0;
}

/* Whether the window under the pointer is one of ours. Asked on the way past
 * rather than at the drop, because by then the button is already up and the
 * answer is the same - it is what decides whether a move has to remove the
 * originals or whether our own drop code already did. */
static gboolean
pointer_over_own_window (void)
{
	POINT at;
	HWND under;
	DWORD owner = 0;

	if (!GetCursorPos (&at)) {
		return FALSE;
	}

	under = WindowFromPoint (at);
	if (under == NULL) {
		return FALSE;
	}

	GetWindowThreadProcessId (under, &owner);

	return owner == GetCurrentProcessId ();
}

static HRESULT STDMETHODCALLTYPE
source_query_continue (IDropSource *iface, BOOL escape, DWORD keys)
{
	DropSource *self = (DropSource *) iface;

	if (escape) {
		return DRAGDROP_S_CANCEL;
	}

	if ((keys & self->button) != 0) {
		self->over_own = pointer_over_own_window ();
		return S_OK;
	}

	return DRAGDROP_S_DROP;
}

static HRESULT STDMETHODCALLTYPE
source_give_feedback (G_GNUC_UNUSED IDropSource *iface, G_GNUC_UNUSED DWORD effect)
{
	return DRAGDROP_S_USEDEFAULTCURSORS;
}

static IDropSourceVtbl source_vtbl = {
	source_query_interface,
	source_add_ref,
	source_release,
	source_query_continue,
	source_give_feedback
};

/* The drag image */

/* Cairo's ARGB32 is already the byte order and premultiplication a 32-bit dib
 * wants, so the rows copy straight across. */
static HBITMAP
bitmap_from_surface (cairo_surface_t *surface, SIZE *size)
{
	cairo_surface_t *image;
	cairo_t *cr;
	BITMAPINFO info;
	HBITMAP bitmap;
	void *bits = NULL;
	int width, height, stride, row;
	HDC screen;

	width = cairo_image_surface_get_width (surface);
	height = cairo_image_surface_get_height (surface);

	if (cairo_surface_get_type (surface) != CAIRO_SURFACE_TYPE_IMAGE ||
	    cairo_image_surface_get_format (surface) != CAIRO_FORMAT_ARGB32) {
		double x1, y1, x2, y2;

		cr = cairo_create (surface);
		cairo_clip_extents (cr, &x1, &y1, &x2, &y2);
		cairo_destroy (cr);

		width = (int) (x2 - x1);
		height = (int) (y2 - y1);

		if (width <= 0 || height <= 0) {
			return NULL;
		}

		image = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, width, height);
		cr = cairo_create (image);
		cairo_set_source_surface (cr, surface, -x1, -y1);
		cairo_paint (cr);
		cairo_destroy (cr);
	} else {
		image = cairo_surface_reference (surface);
	}

	if (width <= 0 || height <= 0) {
		cairo_surface_destroy (image);
		return NULL;
	}

	memset (&info, 0, sizeof (info));
	info.bmiHeader.biSize = sizeof (info.bmiHeader);
	info.bmiHeader.biWidth = width;
	info.bmiHeader.biHeight = -height;	/* top down, the way cairo has it */
	info.bmiHeader.biPlanes = 1;
	info.bmiHeader.biBitCount = 32;
	info.bmiHeader.biCompression = BI_RGB;

	screen = GetDC (NULL);
	bitmap = CreateDIBSection (screen, &info, DIB_RGB_COLORS, &bits, NULL, 0);
	ReleaseDC (NULL, screen);

	if (bitmap == NULL || bits == NULL) {
		cairo_surface_destroy (image);
		return NULL;
	}

	cairo_surface_flush (image);
	stride = cairo_image_surface_get_stride (image);

	for (row = 0; row < height; row++) {
		memcpy ((guchar *) bits + (gsize) row * width * 4,
			cairo_image_surface_get_data (image) + (gsize) row * stride,
			(gsize) width * 4);
	}

	cairo_surface_destroy (image);

	size->cx = width;
	size->cy = height;

	return bitmap;
}

static void
attach_drag_image (IDataObject *data, cairo_surface_t *icon, int hot_x, int hot_y)
{
	IDragSourceHelper *helper = NULL;
	SHDRAGIMAGE image;
	HBITMAP bitmap;

	if (icon == NULL) {
		return;
	}

	memset (&image, 0, sizeof (image));

	bitmap = bitmap_from_surface (icon, &image.sizeDragImage);
	if (bitmap == NULL) {
		return;
	}

	image.ptOffset.x = CLAMP (hot_x, 0, image.sizeDragImage.cx);
	image.ptOffset.y = CLAMP (hot_y, 0, image.sizeDragImage.cy);
	image.hbmpDragImage = bitmap;
	image.crColorKey = CLR_NONE;

	if (FAILED (CoCreateInstance (&CLSID_DragDropHelper, NULL, CLSCTX_INPROC_SERVER,
				      &IID_IDragSourceHelper, (void **) &helper)) ||
	    FAILED (IDragSourceHelper_InitializeFromBitmap (helper, &image, data))) {
		DeleteObject (bitmap);		/* only ours until it is taken */
	}

	if (helper != NULL) {
		IDragSourceHelper_Release (helper);
	}
}

/* Putting it together */

/* Local paths for a uri-list payload, or NULL if any line has none - a place
 * only nemo understands cannot be dragged into another program. */
static gchar **
paths_from_uri_list (const char *uri_list)
{
	GPtrArray *paths;
	gchar **lines;
	guint i;

	if (uri_list == NULL) {
		return NULL;
	}

	paths = g_ptr_array_new ();
	lines = g_strsplit_set (uri_list, "\r\n", -1);

	for (i = 0; lines[i] != NULL; i++) {
		gchar *path;

		if (lines[i][0] == '\0') {
			continue;
		}

		path = g_filename_from_uri (lines[i], NULL, NULL);
		if (path == NULL) {
			g_strfreev (lines);
			g_ptr_array_set_free_func (paths, g_free);
			g_ptr_array_free (paths, TRUE);
			return NULL;
		}

		g_ptr_array_add (paths, path);
	}

	g_strfreev (lines);

	if (paths->len == 0) {
		g_ptr_array_free (paths, TRUE);
		return NULL;
	}

	g_ptr_array_add (paths, NULL);
	return (gchar **) g_ptr_array_free (paths, FALSE);
}

static DWORD
effects_from_actions (GdkDragAction actions)
{
	DWORD effects = 0;

	if (actions & GDK_ACTION_COPY) {
		effects |= DROPEFFECT_COPY;
	}
	if (actions & GDK_ACTION_MOVE) {
		effects |= DROPEFFECT_MOVE;
	}
	if (actions & GDK_ACTION_LINK) {
		effects |= DROPEFFECT_LINK;
	}

	return effects;
}

static GdkDragAction
action_from_effect (DWORD effect)
{
	if (effect & DROPEFFECT_MOVE) {
		return GDK_ACTION_MOVE;
	}
	if (effect & DROPEFFECT_COPY) {
		return GDK_ACTION_COPY;
	}
	if (effect & DROPEFFECT_LINK) {
		return GDK_ACTION_LINK;
	}

	return 0;
}

/* The drag loop swallows the button release - it never reaches the widget the
 * drag started from, which is left believing the button is still down, so the
 * next click there does nothing. The toolkit's own drag ends by handing the
 * widget a release of its own; this is the same thing. */
static void
release_grab (GtkWidget *widget, guint button)
{
	GdkWindow *window;
	GdkDevice *pointer;
	GdkEvent *release;
	GdkModifierType state = 0;
	int x = 0, y = 0, root_x = 0, root_y = 0;

	if (widget == NULL) {
		return;
	}

	window = gtk_widget_get_window (widget);
	if (window == NULL) {
		return;
	}

	pointer = gdk_seat_get_pointer (gdk_display_get_default_seat (gtk_widget_get_display (widget)));
	if (pointer == NULL) {
		return;
	}

	gdk_window_get_device_position (window, pointer, &x, &y, &state);
	gdk_device_get_position (pointer, NULL, &root_x, &root_y);

	release = gdk_event_new (GDK_BUTTON_RELEASE);
	release->button.window = g_object_ref (window);
	release->button.send_event = TRUE;
	release->button.time = GDK_CURRENT_TIME;
	release->button.x = x;
	release->button.y = y;
	release->button.x_root = root_x;
	release->button.y_root = root_y;
	release->button.axes = NULL;
	release->button.state = state;
	release->button.button = button;
	gdk_event_set_device (release, pointer);

	gtk_main_do_event (release);
	gdk_event_free (release);
}

/* A move onto another program leaves the originals here: the other side copied
 * them and is waiting for us to take our copies away. Only reached when the
 * target did not do the move itself, and never for a drop back into nemo, where
 * our own drop code has already moved them. */
static void
remove_moved_sources (GtkWidget *widget, const char *uri_list)
{
	GtkWidget *toplevel;
	GList *files = NULL;
	gchar **lines;
	guint i;

	lines = g_strsplit_set (uri_list, "\r\n", -1);

	for (i = 0; lines[i] != NULL; i++) {
		GFile *one;

		if (lines[i][0] == '\0') {
			continue;
		}

		one = g_file_new_for_uri (lines[i]);

		/* The target may have moved it already, and a rename is a move
		 * either way - nothing to take away in that case. */
		if (g_file_query_exists (one, NULL)) {
			files = g_list_prepend (files, one);
		} else {
			g_object_unref (one);
		}
	}

	g_strfreev (lines);

	if (files == NULL) {
		return;
	}

	files = g_list_reverse (files);
	toplevel = widget != NULL ? gtk_widget_get_toplevel (widget) : NULL;

	nemo_file_operations_delete (files,
				     GTK_IS_WINDOW (toplevel) ? GTK_WINDOW (toplevel) : NULL,
				     NULL, NULL);

	g_list_free_full (files, g_object_unref);
}

/* Returns: (transfer full): an IDataObject; drop it with IDataObject_Release */
gpointer
nemo_dnd_win32_data_object (const char    *uri_list,
			    const char    *icon_list,
			    G_GNUC_UNUSED GdkDragAction  actions)
{
	DataObject *self;
	gchar **paths;

	paths = paths_from_uri_list (uri_list);
	if (paths == NULL) {
		return NULL;
	}

	self = g_new0 (DataObject, 1);
	self->iface.lpVtbl = &data_vtbl;
	self->ref = 1;
	self->formats = g_array_new (FALSE, FALSE, sizeof (DragFormat));

	format_add (self, CF_HDROP, hdrop_block (paths));
	format_add (self, RegisterClipboardFormatW (SHELL_ID_LIST_FORMAT),
		    shell_id_list_block (paths));

	/* No preferred effect goes in. Explorer does not put one in its own drags
	 * either, and a target that sees one obeys it - saying copy there made a
	 * drag onto another folder on the same drive copy when it should move.
	 * What the drag allows is carried by the effects mask instead. */

	if (icon_list != NULL) {
		format_add (self, RegisterClipboardFormatW (ICON_LIST_FORMAT),
			    block_from (icon_list, strlen (icon_list)));
	}

	format_add (self, RegisterClipboardFormatW (URI_LIST_FORMAT),
		    block_from (uri_list, strlen (uri_list)));

	g_strfreev (paths);

	if (self->formats->len == 0) {
		IDataObject_Release (&self->iface);
		return NULL;
	}

	return &self->iface;
}

gboolean
nemo_dnd_win32_enabled (void)
{
	static int answer = -1;

	if (answer < 0) {
		const char *off = g_getenv ("NEMO_NO_SHELL_DRAG");

		answer = (off == NULL || *off == '\0') ? 1 : 0;
	}

	return answer != 0;
}

GdkDragAction
nemo_dnd_win32_action_for_keys (guint keys)
{
	/* A right drag always asks, as in Explorer, whatever else is down, and
	 * a middle one does as on Linux. */
	if (keys & (MK_RBUTTON | MK_MBUTTON)) {
		return GDK_ACTION_ASK;
	}
	if ((keys & MK_CONTROL) && (keys & MK_SHIFT)) {
		return GDK_ACTION_LINK;
	}
	if (keys & MK_CONTROL) {
		return GDK_ACTION_COPY;
	}
	if (keys & MK_SHIFT) {
		return GDK_ACTION_MOVE;
	}
	if (keys & MK_ALT) {
		return GDK_ACTION_ASK;
	}

	return 0;
}

static gboolean
held (int key)
{
	return (GetAsyncKeyState (key) & 0x8000) != 0;
}

GdkDragAction
nemo_dnd_win32_modifier_action (void)
{
	/* Async, not GetKeyState: that reads our own thread's message queue, and
	 * during a drag from another program the keys go to whoever holds the
	 * capture, so our queue may never have seen them. It reads the buttons
	 * as they are on the mouse, before any swap. */
	gboolean swapped = GetSystemMetrics (SM_SWAPBUTTON) != 0;
	guint keys = 0;

	if (held (VK_CONTROL)) {
		keys |= MK_CONTROL;
	}
	if (held (VK_SHIFT)) {
		keys |= MK_SHIFT;
	}
	if (held (VK_MENU)) {
		keys |= MK_ALT;
	}
	if (held (swapped ? VK_LBUTTON : VK_RBUTTON)) {
		keys |= MK_RBUTTON;
	}
	if (held (VK_MBUTTON)) {
		keys |= MK_MBUTTON;
	}

	return nemo_dnd_win32_action_for_keys (keys);
}

void
nemo_dnd_win32_prepare (void)
{
	if (nemo_dnd_win32_enabled ()) {
		g_setenv (OLE_DND_ENV, "1", TRUE);
	}
}

gboolean
nemo_dnd_win32_drag (GtkWidget       *widget,
		     GdkDragAction    actions,
		     const char      *uri_list,
		     const char      *icon_list,
		     cairo_surface_t *icon,
		     int              hot_x,
		     int              hot_y,
		     GdkDragAction   *performed)
{
	static gboolean ole_ready = FALSE;
	IDataObject *data;
	DropSource *source;
	DWORD effect = DROPEFFECT_NONE;
	HRESULT hr;
	gboolean own, moved_by_target;
	guint button;

	if (performed != NULL) {
		*performed = 0;
	}

	if (!nemo_dnd_win32_enabled ()) {
		return FALSE;
	}

	data = nemo_dnd_win32_data_object (uri_list, icon_list, actions);
	if (data == NULL) {
		return FALSE;
	}

	/* The toolkit only takes the process as far as COM. A drag needs the ole
	 * layer on top of it, and it stays up for good once it is. */
	if (!ole_ready) {
		ole_ready = SUCCEEDED (OleInitialize (NULL));

		if (!ole_ready) {
			IDataObject_Release (data);
			return FALSE;
		}
	}

	attach_drag_image (data, icon, hot_x, hot_y);

	source = g_new0 (DropSource, 1);
	source->iface.lpVtbl = &source_vtbl;
	source->ref = 1;
	/* Not the middle button: Windows' drag does not reliably end on its release. */
	if (GetKeyState (VK_RBUTTON) & 0x8000) {
		source->button = MK_RBUTTON;
		button = GDK_BUTTON_SECONDARY;
	} else {
		source->button = MK_LBUTTON;
		button = GDK_BUTTON_PRIMARY;
	}

	hr = DoDragDrop (data, &source->iface, effects_from_actions (actions), &effect);

	own = source->over_own || ((DataObject *) data)->read_own;
	moved_by_target = ((DataObject *) data)->performed == DROPEFFECT_MOVE;

	if (performed != NULL && hr == DRAGDROP_S_DROP) {
		*performed = action_from_effect (effect);
	}

	IDropSource_Release (&source->iface);
	IDataObject_Release (data);

	release_grab (widget, button);

	if (hr == DRAGDROP_S_DROP && (effect & DROPEFFECT_MOVE) && !own && !moved_by_target) {
		remove_moved_sources (widget, uri_list);
	}

	/* cppcheck-suppress memleak ; source is freed by its own Release, through the vtable */
	return TRUE;
}
