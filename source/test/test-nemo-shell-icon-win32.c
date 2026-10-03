/* The shell's icon for a shortcut: a real picture at each size, its target's
 * rather than one generic shape, and cached. One whose target or icon is on a
 * share gets the icon for the target's name, and the views' lookup answers at
 * once and finds the icon off the main thread. */

#include <config.h>

#include <string.h>
#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include <libnemo-private/nemo-lnk.h>
#include <libnemo-private/nemo-shell-icon-win32.h>
#include <libnemo-private/nemo-shortcut-win32.h>

#define COBJMACROS
#include <windows.h>
#include <shlobj.h>

#include "test-scratch.h"

static int failures;

static void
check (gboolean ok, const char *what)
{
	g_print ("%s: %s\n", ok ? "PASS" : "FAIL", what);
	if (!ok) {
		failures++;
	}
}

static gboolean
has_visible_pixels (GdkPixbuf *pixbuf)
{
	const guchar *pixels = gdk_pixbuf_get_pixels (pixbuf);
	gint stride = gdk_pixbuf_get_rowstride (pixbuf);
	gint w = gdk_pixbuf_get_width (pixbuf);
	gint h = gdk_pixbuf_get_height (pixbuf);
	gint x, y, seen = 0;

	if (!gdk_pixbuf_get_has_alpha (pixbuf)) {
		return TRUE;
	}

	for (y = 0; y < h; y++) {
		for (x = 0; x < w; x++) {
			if (pixels[y * stride + x * 4 + 3] > 128) {
				seen++;
			}
		}
	}

	return seen > (w * h) / 10;
}

static gboolean
same_picture (GdkPixbuf *a, GdkPixbuf *b)
{
	gint h = gdk_pixbuf_get_height (a);
	gint stride = gdk_pixbuf_get_rowstride (a);

	return gdk_pixbuf_get_width (a) == gdk_pixbuf_get_width (b) &&
	       h == gdk_pixbuf_get_height (b) &&
	       stride == gdk_pixbuf_get_rowstride (b) &&
	       memcmp (gdk_pixbuf_get_pixels (a), gdk_pixbuf_get_pixels (b), (gsize) stride * h) == 0;
}

/* An address that fails at once, so nothing here waits on the network even
   where the old code would have gone there. */
#define DEAD_SHARE "\\\\10.255.255.1\\share\\"

static int ready_calls;
static int destroyed;

static void
on_ready (gpointer data)
{
	(void) data;
	ready_calls++;
}

static void
on_destroy (gpointer data)
{
	(void) data;
	destroyed++;
}

static char *
make_file (const char *dir, const char *name)
{
	char *path = g_build_filename (dir, name, NULL);

	g_assert (g_file_set_contents (path, "", 0, NULL));

	return path;
}

/* Written by the shell, since the reader here does not write an icon. */
static gboolean
make_with_icon (const char *target, const char *icon_file, int icon_index, const char *lnk_path)
{
	IShellLinkW *link = NULL;
	IPersistFile *pf = NULL;
	gunichar2 *w_target = g_utf8_to_utf16 (target, -1, NULL, NULL, NULL);
	gunichar2 *w_icon = g_utf8_to_utf16 (icon_file, -1, NULL, NULL, NULL);
	gunichar2 *w_lnk = g_utf8_to_utf16 (lnk_path, -1, NULL, NULL, NULL);
	gboolean ok = FALSE;

	if (SUCCEEDED (CoCreateInstance (&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER,
					 &IID_IShellLinkW, (void **) &link)) &&
	    SUCCEEDED (IShellLinkW_SetPath (link, w_target)) &&
	    SUCCEEDED (IShellLinkW_SetIconLocation (link, w_icon, icon_index)) &&
	    SUCCEEDED (IShellLinkW_QueryInterface (link, &IID_IPersistFile, (void **) &pf))) {
		ok = SUCCEEDED (IPersistFile_Save (pf, w_lnk, TRUE));
	}

	if (pf != NULL) {
		IPersistFile_Release (pf);
	}
	if (link != NULL) {
		IShellLinkW_Release (link);
	}
	g_free (w_target);
	g_free (w_icon);
	g_free (w_lnk);

	return ok;
}

/* The shortcut is made to a local file and then moved onto the share, so the
   shell never sees the share while it writes it. */
static gboolean
move_to_share (const char *lnk_path, const char *name)
{
	char *absolute = g_strconcat (DEAD_SHARE, name, NULL);
	gboolean ok = nemo_lnk_set_paths (lnk_path, absolute, NULL, NULL, NULL);

	g_free (absolute);

	return ok;
}

static void
put32 (GByteArray *out, guint32 v)
{
	guint8 b[4] = { v, v >> 8, v >> 16, v >> 24 };

	g_byte_array_append (out, b, 4);
}

static void
set32 (GByteArray *out, guint at, guint32 v)
{
	out->data[at] = v;
	out->data[at + 1] = v >> 8;
	out->data[at + 2] = v >> 16;
	out->data[at + 3] = v >> 24;
}

static guint32
volume_serial (const char *path)
{
	wchar_t root[4] = { (wchar_t) path[0], L':', L'\\', 0 };
	DWORD serial = 0;

	g_assert (GetVolumeInformationW (root, NULL, 0, &serial, NULL, NULL, NULL, 0));

	return serial;
}

/* Windows writes the share beside the drive path when the target's drive is
   shared, and wine never does, so the shell's own record is swapped for one
   with both. Everything else in the file is left as the shell wrote it. */
static gboolean
record_shared_drive (const char *lnk_path, const char *local, guint32 serial)
{
	const char *net = "\\\\10.255.255.1\\C";
	guint8 *bytes = NULL;
	gsize length = 0;
	GByteArray *out, *info;
	guint32 flags, at = 0x4c, old_size, net_start;
	gboolean ok;

	if (!g_file_get_contents (lnk_path, (char **) &bytes, &length, NULL) || length < 0x4c + 4) {
		g_free (bytes);
		return FALSE;
	}
	flags = bytes[20] | bytes[21] << 8 | bytes[22] << 16 | (guint32) bytes[23] << 24;
	if (flags & 0x01) {
		at += 2 + (bytes[at] | bytes[at + 1] << 8);
	}
	if (!(flags & 0x02) || at + 4 > length) {
		g_free (bytes);
		return FALSE;
	}
	old_size = bytes[at] | bytes[at + 1] << 8 | bytes[at + 2] << 16 | (guint32) bytes[at + 3] << 24;
	if (old_size > length - at || !g_str_is_ascii (local)) {
		g_free (bytes);
		return FALSE;
	}

	info = g_byte_array_new ();
	for (int i = 0; i < 7; i++) {
		put32 (info, 0);
	}
	set32 (info, 4, 0x1c);
	set32 (info, 8, 0x03);            /* drive path and share */
	set32 (info, 12, info->len);
	put32 (info, 0x11);
	put32 (info, 3);                  /* fixed drive */
	put32 (info, serial);
	put32 (info, 0x10);
	g_byte_array_append (info, (const guint8 *) "", 1);
	set32 (info, 16, info->len);
	g_byte_array_append (info, (const guint8 *) local, strlen (local) + 1);
	net_start = info->len;
	set32 (info, 20, net_start);
	put32 (info, 0);
	put32 (info, 0);
	put32 (info, 0x14);
	put32 (info, 0);
	put32 (info, 0);
	g_byte_array_append (info, (const guint8 *) net, strlen (net) + 1);
	set32 (info, net_start, info->len - net_start);
	set32 (info, 24, info->len);
	g_byte_array_append (info, (const guint8 *) "", 1);
	set32 (info, 0, info->len);

	out = g_byte_array_new ();
	g_byte_array_append (out, bytes, at);
	g_byte_array_append (out, info->data, info->len);
	g_byte_array_append (out, bytes + at + old_size, length - at - old_size);
	ok = g_file_set_contents (lnk_path, (const char *) out->data, out->len, NULL);

	g_byte_array_free (out, TRUE);
	g_byte_array_free (info, TRUE);
	g_free (bytes);

	return ok;
}

/* The shell honors the icon a shortcut names before anything else, so one
   naming notepad's tells the two routes apart. */
static void
test_share (const char *dir)
{
	const char *notepad = "C:\\Windows\\notepad.exe";
	char *doc = make_file (dir, "doc.nemoshare");
	char *tool = make_file (dir, "tool.exe");
	char *to_doc = g_build_filename (dir, "doc.lnk", NULL);
	char *named = g_build_filename (dir, "named icon.lnk", NULL);
	char *doc_on_share = g_build_filename (dir, "doc on share.lnk", NULL);
	char *tool_on_share = g_build_filename (dir, "tool on share.lnk", NULL);
	char *icon_on_share = g_build_filename (dir, "icon on share.lnk", NULL);
	char *shared_drive = g_build_filename (dir, "shared drive.lnk", NULL);
	char *made_elsewhere = g_build_filename (dir, "made elsewhere.lnk", NULL);
	guint32 serial = volume_serial (doc);
	GdkPixbuf *plain_doc, *named_doc, *shared_doc, *shared_tool, *shared_icon, *on_shared_drive, *elsewhere;

	check (nemo_shortcut_win32_create (doc, to_doc, NULL, NULL, NULL, NULL),
	       "a shortcut to a local document is made");
	check (make_with_icon (doc, notepad, 0, named),
	       "one naming notepad's icon is made");
	check (make_with_icon (doc, notepad, 0, doc_on_share) &&
	       move_to_share (doc_on_share, "doc.nemoshare"),
	       "and the same one moved to a share");
	check (nemo_shortcut_win32_create (tool, tool_on_share, NULL, NULL, NULL, NULL) &&
	       move_to_share (tool_on_share, "tool.exe"),
	       "a shortcut to a program on a share is made");
	check (make_with_icon (notepad, DEAD_SHARE "app.ico", 0, icon_on_share),
	       "a shortcut to notepad with its icon on a share is made");
	check (make_with_icon (doc, notepad, 0, shared_drive) &&
	       record_shared_drive (shared_drive, doc, serial),
	       "one naming notepad's icon, on a drive that is shared, is made");
	check (make_with_icon (doc, notepad, 0, made_elsewhere) &&
	       record_shared_drive (made_elsewhere, doc, serial ^ 0x5a5a5a5a),
	       "and one made on another machine's shared drive");

	plain_doc = nemo_shell_icon_win32_for_path (to_doc, 32, 1);
	named_doc = nemo_shell_icon_win32_for_path (named, 32, 1);
	shared_doc = nemo_shell_icon_win32_for_path (doc_on_share, 32, 1);
	shared_tool = nemo_shell_icon_win32_for_path (tool_on_share, 32, 1);
	shared_icon = nemo_shell_icon_win32_for_path (icon_on_share, 32, 1);
	on_shared_drive = nemo_shell_icon_win32_for_path (shared_drive, 32, 1);
	elsewhere = nemo_shell_icon_win32_for_path (made_elsewhere, 32, 1);

	check (plain_doc != NULL && named_doc != NULL && !same_picture (plain_doc, named_doc),
	       "a local shortcut wears the icon it names");
	check (shared_doc != NULL && named_doc != NULL && !same_picture (shared_doc, named_doc),
	       "a document on a share does not wear it, since its icon comes from its name");
	check (shared_doc != NULL && plain_doc != NULL && same_picture (shared_doc, plain_doc),
	       "a document on a share wears the icon for its name");
	check (shared_tool != NULL && gdk_pixbuf_get_width (shared_tool) == 32,
	       "a program on a share gets the plain program icon");
	check (shared_icon != NULL && shared_tool != NULL && same_picture (shared_icon, shared_tool),
	       "a shortcut whose icon is on a share wears the icon for its target's name");
	check (on_shared_drive != NULL && named_doc != NULL && plain_doc != NULL &&
	       same_picture (on_shared_drive, named_doc) && !same_picture (on_shared_drive, plain_doc),
	       "a local shortcut to a shared drive wears the icon it names");
	check (elsewhere != NULL && plain_doc != NULL && same_picture (elsewhere, plain_doc),
	       "one whose drive is another machine's wears the icon for its name");

	g_clear_object (&on_shared_drive);
	g_clear_object (&elsewhere);
	g_clear_object (&plain_doc);
	g_clear_object (&named_doc);
	g_clear_object (&shared_doc);
	g_clear_object (&shared_tool);
	g_clear_object (&shared_icon);

	g_remove (made_elsewhere);
	g_remove (shared_drive);
	g_remove (icon_on_share);
	g_remove (tool_on_share);
	g_remove (doc_on_share);
	g_remove (named);
	g_remove (to_doc);
	g_remove (tool);
	g_remove (doc);
	g_free (made_elsewhere);
	g_free (shared_drive);
	g_free (icon_on_share);
	g_free (tool_on_share);
	g_free (doc_on_share);
	g_free (named);
	g_free (to_doc);
	g_free (tool);
	g_free (doc);
}

static gboolean
wait_for (int *counter, int want)
{
	gint64 deadline = g_get_monotonic_time () + 30 * G_USEC_PER_SEC;

	while (*counter < want && g_get_monotonic_time () < deadline) {
		if (!g_main_context_iteration (NULL, FALSE)) {
			g_usleep (10000);
		}
	}

	return *counter >= want;
}

/* What the views use: nothing waits on the shell. */
static void
test_lookup (const char *dir, const char *to_notepad)
{
	char *not_a_shortcut = g_build_filename (dir, "plain.lnk", NULL);
	GdkPixbuf *first, *second, *found, *direct, *none;

	g_assert (g_file_set_contents (not_a_shortcut, "not a shortcut", -1, NULL));

	first = nemo_shell_icon_win32_lookup (to_notepad, 40, 7, on_ready, NULL, on_destroy);
	check (first == NULL && ready_calls == 0, "the first lookup answers at once, with nothing yet");
	second = nemo_shell_icon_win32_lookup (to_notepad, 40, 7, on_ready, NULL, on_destroy);
	check (second == NULL && destroyed == 1, "a second one while the first is under way is not queued");

	check (wait_for (&ready_calls, 1) && ready_calls == 1 && destroyed == 2,
	       "the main thread is told once when the icon is found");

	found = nemo_shell_icon_win32_lookup (to_notepad, 40, 7, on_ready, NULL, on_destroy);
	check (found != NULL && gdk_pixbuf_get_width (found) == 40 && destroyed == 3,
	       "the next lookup returns it from the cache");
	direct = nemo_shell_icon_win32_for_path (to_notepad, 40, 7);
	check (found != NULL && direct == found, "the same cache as the direct ask");

	none = nemo_shell_icon_win32_lookup (not_a_shortcut, 32, 1, on_ready, NULL, on_destroy);
	check (none == NULL && wait_for (&destroyed, 4), "a file that is not a shortcut is finished with");
	check (ready_calls == 1, "and nothing is redrawn for it");
	none = nemo_shell_icon_win32_lookup (not_a_shortcut, 32, 1, on_ready, NULL, on_destroy);
	check (none == NULL && destroyed == 5, "and it is not asked again");

	g_clear_object (&found);
	g_clear_object (&direct);
	g_remove (not_a_shortcut);
	g_free (not_a_shortcut);
}

int
main (int argc, char *argv[])
{
	char *dir, *to_notepad, *to_folder, *folder;
	GdkPixbuf *small, *large, *jumbo, *again, *folder_icon, *missing;
	const int sizes[] = { 16, 32, 48, 256 };
	int i;

	gtk_init_check (&argc, &argv);

	dir = test_scratch_dir ("nemo-shell-icon-XXXXXX", NULL);
	g_assert (dir != NULL);

	folder = g_build_filename (dir, "a folder", NULL);
	g_mkdir (folder, 0700);
	to_notepad = g_build_filename (dir, "notepad.lnk", NULL);
	to_folder = g_build_filename (dir, "folder.lnk", NULL);

	check (nemo_shortcut_win32_create ("C:\\Windows\\notepad.exe", to_notepad, NULL, NULL, NULL, NULL),
	       "a shortcut to notepad is made");
	check (nemo_shortcut_win32_create (folder, to_folder, NULL, NULL, NULL, NULL),
	       "a shortcut to a folder is made");

	for (i = 0; i < G_N_ELEMENTS (sizes); i++) {
		GdkPixbuf *pixbuf = nemo_shell_icon_win32_for_path (to_notepad, sizes[i], 1);
		char *what = g_strdup_printf ("an icon comes back at %d", sizes[i]);

		check (pixbuf != NULL, what);
		g_free (what);

		if (pixbuf != NULL) {
			what = g_strdup_printf ("and it is %dx%d with something drawn in it (got %dx%d)",
						sizes[i], sizes[i], gdk_pixbuf_get_width (pixbuf), gdk_pixbuf_get_height (pixbuf));
			check (gdk_pixbuf_get_width (pixbuf) == sizes[i] && has_visible_pixels (pixbuf), what);
			g_free (what);
			g_object_unref (pixbuf);
		}
	}

	small = nemo_shell_icon_win32_for_path (to_notepad, 16, 1);
	again = nemo_shell_icon_win32_for_path (to_notepad, 16, 1);
	check (small != NULL && small == again, "the second ask is answered from the cache");

	large = nemo_shell_icon_win32_for_path (to_notepad, 24, 1);
	check (large != NULL && gdk_pixbuf_get_width (large) == 24, "24 is scaled down from the 32 list");

	jumbo = nemo_shell_icon_win32_for_path (to_notepad, 128, 1);
	check (jumbo != NULL && gdk_pixbuf_get_width (jumbo) == 128 && has_visible_pixels (jumbo),
	       "128 is scaled down from the jumbo list and still has a picture in it");

	folder_icon = nemo_shell_icon_win32_for_path (to_folder, 32, 1);
	check (folder_icon != NULL && large != NULL && !same_picture (folder_icon, large),
	       "a shortcut to a folder gets a different picture from one to a program");

	/* The shell may answer a path that is not there with nothing, or with the
	   generic unknown-file icon, and both are fine. What it may not do is hand
	   back something that is neither, or an icon at the wrong size. */
	missing = nemo_shell_icon_win32_for_path ("Q:\\no\\such\\thing.lnk", 32, 1);
	check (missing == NULL ||
	       (GDK_IS_PIXBUF (missing) && gdk_pixbuf_get_width (missing) == 32),
	       "a missing file gets nothing, or a 32-wide icon");

	CoInitializeEx (NULL, COINIT_APARTMENTTHREADED);
	test_share (dir);
	test_lookup (dir, to_notepad);

	g_clear_object (&small);
	g_clear_object (&again);
	g_clear_object (&large);
	g_clear_object (&jumbo);
	g_clear_object (&folder_icon);
	g_clear_object (&missing);

	g_remove (to_notepad);
	g_remove (to_folder);
	g_rmdir (folder);
	g_rmdir (dir);
	g_free (to_folder);
	g_free (to_notepad);
	g_free (folder);
	g_free (dir);

	return failures == 0 ? 0 : 1;
}
