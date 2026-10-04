/* nemo-shell-icon-win32.c - the icon the Windows shell would draw for a shortcut
 *
 * A shortcut's icon is its target's, which only the shell knows how to find:
 * the target may be a program with its own icon, a folder, or a shell item
 * with no path at all. SHGetFileInfo answers with an index into the system
 * image lists, and the list for the wanted size hands back a real icon at
 * that size rather than a scaled one. None of it needs Explorer running.
 *
 * The shell may go to the target to find its icon, so a shortcut to a share
 * gets the icon for its target's name instead, and the views ask on a worker
 * thread so a folder full of shortcuts lists before any of them is found.
 *
 * Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */

#include <config.h>

#include <string.h>

#define COBJMACROS
#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include <commoncontrols.h>
#include <shlobj.h>

#include "nemo-shell-icon-win32.h"
#include "nemo-link-win32.h"
#include "nemo-lnk.h"

/* Exported by GDK's win32 backend, declared only for GTK's own build. */
GdkPixbuf *gdk_win32_icon_to_pixbuf_libgtk_only (HICON    hicon,
						 gdouble *x_hot,
						 gdouble *y_hot);

#define CACHE_LIMIT 2000

/* Both on the main thread only. A cached NULL is a shortcut the shell had no
   icon for, so it is not asked again. A pending key belongs to its lookup. */
static GHashTable *cache = NULL;
static GHashTable *pending = NULL;

static GAsyncQueue *queue = NULL;

typedef struct {
	char               *path;
	char               *key;
	gint                pixel_size;
	GdkPixbuf          *pixbuf;
	NemoShellIconReady  ready;
	gpointer            data;
	GDestroyNotify      destroy;
} Lookup;

static const GUID iid_iimagelist = { 0x46EB5926, 0x582E, 0x4017, { 0x9F, 0xDF, 0xE8, 0x99, 0x8D, 0xAA, 0x09, 0x50 } };

static gint
list_size_for (gint pixel_size)
{
	if (pixel_size <= 16) {
		return 16;
	}
	if (pixel_size <= 32) {
		return 32;
	}
	if (pixel_size <= 48) {
		return 48;
	}
	return 256;
}

static gint
shell_list (gint list_size)
{
	switch (list_size) {
	case 16:
		return SHIL_SMALL;
	case 32:
		return SHIL_LARGE;
	case 48:
		return SHIL_EXTRALARGE;
	default:
		return SHIL_JUMBO;
	}
}

static GdkPixbuf *
pixbuf_from_index (gint index, gint list_size)
{
	IImageList *list = NULL;
	HICON icon = NULL;
	GdkPixbuf *pixbuf = NULL;

	if (SUCCEEDED (SHGetImageList (shell_list (list_size), &iid_iimagelist, (void **) &list)) && list != NULL) {
		if (SUCCEEDED (IImageList_GetIcon (list, index, ILD_TRANSPARENT, &icon)) && icon != NULL) {
			pixbuf = gdk_win32_icon_to_pixbuf_libgtk_only (icon, NULL, NULL);
			DestroyIcon (icon);
		}
		IImageList_Release (list);
	}

	/* The jumbo list pads an icon it only has at 48 with blank space around
	 * it; a smaller size than asked for is better than a small picture in a
	 * large frame. */
	if (pixbuf != NULL && list_size == 256) {
		gint w = gdk_pixbuf_get_width (pixbuf);
		gint h = gdk_pixbuf_get_height (pixbuf);
		const guchar *pixels = gdk_pixbuf_get_pixels (pixbuf);
		gint stride = gdk_pixbuf_get_rowstride (pixbuf);
		gboolean blank_corner = gdk_pixbuf_get_has_alpha (pixbuf);
		gint x, y;

		/* anything drawn in the bottom-right quarter means the full size is real */
		for (y = h * 3 / 4; y < h && blank_corner; y++) {
			for (x = w * 3 / 4; x < w; x++) {
				if (pixels[y * stride + x * 4 + 3] != 0) {
					blank_corner = FALSE;
					break;
				}
			}
		}

		if (blank_corner) {
			GdkPixbuf *smaller = pixbuf_from_index (index, 48);

			if (smaller != NULL) {
				g_object_unref (pixbuf);
				pixbuf = smaller;
			}
		}
	}

	return pixbuf;
}

/* With name_only the shell goes by the name and never opens the file. */
static gboolean
shell_icon_index (const char *path, gboolean name_only, gint *index)
{
	wchar_t *wide = g_utf8_to_utf16 (path, -1, NULL, NULL, NULL);
	SHFILEINFOW info;
	UINT flags = SHGFI_SYSICONINDEX | SHGFI_SMALLICON;
	DWORD_PTR found;

	if (wide == NULL) {
		return FALSE;
	}
	if (name_only) {
		flags |= SHGFI_USEFILEATTRIBUTES;
	}

	memset (&info, 0, sizeof info);
	found = SHGetFileInfoW (wide, name_only ? FILE_ATTRIBUTE_NORMAL : 0, &info, sizeof info, flags);
	g_free (wide);

	*index = info.iIcon;

	return found != 0;
}

static gboolean
expands_to_remote_drive (const char *windows_path)
{
	char *expanded;
	gboolean remote;

	if (windows_path == NULL) {
		return FALSE;
	}

	expanded = nemo_lnk_expand (windows_path);
	remote = nemo_win32_drive_is_remote (expanded);
	g_free (expanded);

	return remote;
}

/* A shortcut to a drive that was shared when it was made records the share as
   well. The drive path is this machine's only when the volume serial matches;
   otherwise the shell may go by the share. Only the drive root is asked, so a
   link or mount point on the way to the target is never followed. */
static gboolean
drive_is_elsewhere (const NemoLnk *lnk)
{
	const char *path = lnk->local_path;
	wchar_t root[4] = { 0, L':', L'\\', 0 };
	DWORD serial = 0, old_mode = 0;
	gboolean same;

	if (lnk->net_share == NULL || path == NULL) {
		return FALSE;
	}
	if (g_str_has_prefix (path, "\\\\?\\")) {
		path += 4;
	}
	if (!lnk->has_serial || !g_ascii_isalpha (path[0]) || path[1] != ':') {
		return TRUE;
	}

	root[0] = (wchar_t) g_ascii_toupper (path[0]);
	/* no "insert a disk" box for an empty drive */
	SetThreadErrorMode (SEM_FAILCRITICALERRORS, &old_mode);
	same = GetVolumeInformationW (root, NULL, 0, &serial, NULL, NULL, NULL, 0) &&
	       serial == lnk->drive_serial;
	SetThreadErrorMode (old_mode, NULL);

	return !same;
}

/* The shell's icon for the shortcut at path, or for its target's name when
   the target or the shortcut's own icon is on a share. A shortcut that cannot
   be read gets none, since what it points at cannot be told. */
static GdkPixbuf *
shortcut_icon (const char *path, gint pixel_size)
{
	NemoLnk lnk;
	gint list_size = list_size_for (pixel_size);
	gboolean on_share, found;
	char *name = NULL;
	gint index = 0;
	GdkPixbuf *pixbuf = NULL;

	if (!nemo_lnk_read (path, &lnk)) {
		return NULL;
	}

	on_share = nemo_lnk_points_at_share (&lnk) ||
		   nemo_win32_drive_is_remote (lnk.local_path) ||
		   drive_is_elsewhere (&lnk) ||
		   expands_to_remote_drive (lnk.env_path) ||
		   expands_to_remote_drive (lnk.icon_location);

	if (on_share) {
		char *target = nemo_lnk_display_target (&lnk);

		if (target != NULL && target[0] != '\0') {
			name = g_path_get_basename (target);
		}
		g_free (target);
	}
	nemo_lnk_clear (&lnk);

	if (on_share) {
		found = name != NULL && shell_icon_index (name, TRUE, &index);
	} else {
		found = shell_icon_index (path, FALSE, &index);
	}
	g_free (name);

	if (found) {
		pixbuf = pixbuf_from_index (index, list_size);
	}

	/* The view asked for a size the shell has no list at: scale down from the
	 * next one up rather than hand back a picture larger than the cell. */
	if (pixbuf != NULL && gdk_pixbuf_get_width (pixbuf) != pixel_size) {
		GdkPixbuf *scaled = gdk_pixbuf_scale_simple (pixbuf, pixel_size, pixel_size, GDK_INTERP_HYPER);

		g_object_unref (pixbuf);
		pixbuf = scaled;
	}

	return pixbuf;
}

static void
unref_if_set (gpointer object)
{
	if (object != NULL) {
		g_object_unref (object);
	}
}

static void
remember (char *key, GdkPixbuf *pixbuf)
{
	if (cache == NULL) {
		cache = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, unref_if_set);
	} else if (g_hash_table_size (cache) >= CACHE_LIMIT) {
		g_hash_table_remove_all (cache);
	}

	g_hash_table_replace (cache, key, pixbuf != NULL ? g_object_ref (pixbuf) : NULL);
}

static gboolean
recall (const char *key, GdkPixbuf **pixbuf)
{
	gpointer value;

	if (cache == NULL || !g_hash_table_lookup_extended (cache, key, NULL, &value)) {
		return FALSE;
	}

	*pixbuf = value != NULL ? g_object_ref (value) : NULL;

	return TRUE;
}

static char *
key_for (const gchar *path, gint pixel_size, gint64 mtime)
{
	return g_strdup_printf ("%s|%d|%lld", path, pixel_size, (long long) mtime);
}

static gboolean
lookup_done (gpointer user_data)
{
	Lookup *lookup = user_data;

	g_hash_table_remove (pending, lookup->key);
	remember (lookup->key, lookup->pixbuf);
	lookup->key = NULL;

	if (lookup->pixbuf != NULL && lookup->ready != NULL) {
		lookup->ready (lookup->data);
	}
	if (lookup->destroy != NULL) {
		lookup->destroy (lookup->data);
	}

	g_clear_object (&lookup->pixbuf);
	g_free (lookup->path);
	g_free (lookup);

	return G_SOURCE_REMOVE;
}

static gpointer
lookup_thread (gpointer unused)
{
	(void) unused;

	/* The shell wants COM on the calling thread, and some icon handlers
	   only work in a single-threaded apartment. */
	CoInitializeEx (NULL, COINIT_APARTMENTTHREADED);

	for (;;) {
		Lookup *lookup = g_async_queue_pop (queue);

		lookup->pixbuf = shortcut_icon (lookup->path, lookup->pixel_size);
		g_idle_add (lookup_done, lookup);
	}

	return NULL;
}

GdkPixbuf *
nemo_shell_icon_win32_for_path (const gchar *path,
				gint         pixel_size,
				gint64       mtime)
{
	char *key;
	GdkPixbuf *pixbuf;

	g_return_val_if_fail (path != NULL, NULL);
	g_return_val_if_fail (pixel_size > 0, NULL);

	key = key_for (path, pixel_size, mtime);

	if (recall (key, &pixbuf)) {
		g_free (key);
		return pixbuf;
	}

	pixbuf = shortcut_icon (path, pixel_size);
	remember (key, pixbuf);

	return pixbuf;
}

GdkPixbuf *
nemo_shell_icon_win32_lookup (const gchar        *path,
			      gint                pixel_size,
			      gint64              mtime,
			      NemoShellIconReady  ready,
			      gpointer            data,
			      GDestroyNotify      destroy)
{
	Lookup *lookup;
	char *key;
	GdkPixbuf *pixbuf = NULL;

	g_return_val_if_fail (path != NULL, NULL);
	g_return_val_if_fail (pixel_size > 0, NULL);

	key = key_for (path, pixel_size, mtime);

	if (pending == NULL) {
		pending = g_hash_table_new (g_str_hash, g_str_equal);
	}

	if (recall (key, &pixbuf) || g_hash_table_contains (pending, key)) {
		g_free (key);
		if (destroy != NULL) {
			destroy (data);
		}
		return pixbuf;
	}

	if (queue == NULL) {
		queue = g_async_queue_new ();
		g_thread_unref (g_thread_new ("shell-icons", lookup_thread, NULL));
	}

	lookup = g_new0 (Lookup, 1);
	lookup->path = g_strdup (path);
	lookup->key = key;
	lookup->pixel_size = pixel_size;
	lookup->ready = ready;
	lookup->data = data;
	lookup->destroy = destroy;

	g_hash_table_add (pending, lookup->key);
	g_async_queue_push (queue, lookup);

	return NULL;
}

void
nemo_shell_icon_win32_clear_cache (void)
{
	if (cache != NULL) {
		g_hash_table_remove_all (cache);
	}
}
