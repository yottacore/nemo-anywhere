/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-archive-dialog.c - the Compress dialog.

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

/* Opening Options once pushed the dialog's buttons off a 540 pixel screen.
 * The options now scroll in whatever room the screen leaves. The real dialog
 * gives the closed height, frame included, and what the options want; the room
 * for those is worked out for 540, 768 and 1080 pixel work areas, and the dialog
 * is opened for real on the test screen, where nothing should be cut.
 *
 * Then the link choices: which groups show for the selection, which store
 * choices each format and its options leave, the Junctions defaults, and the
 * volume sizes with their notes. */

#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <gtk/gtk.h>
#ifdef GDK_WINDOWING_X11
#include <signal.h>
#include <gdk/gdkx.h>
#include <X11/Xatom.h>
#endif

#include <libnemo-private/nemo-archive.h>
#include <libnemo-private/nemo-archive-commands.h>
#include <libnemo-private/nemo-archive-host.h>
#include <libnemo-private/nemo-config.h>
#include <libnemo-private/nemo-global-preferences.h>
#include <libnemo-private/nemo-share.h>

#include "arc-scan.h"

#include "nemo-archive-dialog.h"

#include "test-scratch.h"
#include "test-check.h"

typedef struct {
	GtkWidget *dialog;
	GtkWidget *expander;
	int closed_height;
	int natural;
	gboolean ran;
} Probe;

static void
settle (void)
{
	int i;

	for (i = 0; i < 20; i++) {
		while (gtk_events_pending ()) {
			gtk_main_iteration ();
		}
		g_usleep (10000);
	}
}

static GtkWidget *
find_type (GtkWidget *widget, GType type)
{
	GList *children, *l;
	GtkWidget *found = NULL;

	if (G_TYPE_CHECK_INSTANCE_TYPE (widget, type)) {
		return widget;
	}
	if (!GTK_IS_CONTAINER (widget)) {
		return NULL;
	}

	children = gtk_container_get_children (GTK_CONTAINER (widget));
	for (l = children; l != NULL && found == NULL; l = l->next) {
		found = find_type (l->data, type);
	}
	g_list_free (children);

	return found;
}

static GtkWidget *
find_dialog (void)
{
	GList *windows, *l;
	GtkWidget *dialog = NULL;

	windows = gtk_window_list_toplevels ();
	for (l = windows; l != NULL; l = l->next) {
		if (GTK_IS_DIALOG (l->data) && gtk_widget_get_visible (l->data)) {
			dialog = l->data;
		}
	}
	g_list_free (windows);

	return dialog;
}

#ifdef GDK_WINDOWING_X11
static gboolean
has_window_manager (Display *xdisplay)
{
	Atom type;
	int format;
	unsigned long count, after;
	unsigned char *data = NULL;
	gboolean found;

	found = XGetWindowProperty (xdisplay, DefaultRootWindow (xdisplay),
				    XInternAtom (xdisplay, "_NET_SUPPORTING_WM_CHECK", False),
				    0, 1, False, XA_WINDOW, &type, &format, &count, &after,
				    &data) == Success &&
		data != NULL && count == 1;
	if (data != NULL) {
		XFree (data);
	}

	return found;
}
#endif

/* With no window manager the dialog has no frame, and a test that left the
   frame out would pass there and fail on every real desktop. So the private
   display gets one. */
static GPid
start_window_manager (void)
{
	GPid pid = 0;
#ifdef GDK_WINDOWING_X11
	GdkDisplay *display = gdk_display_get_default ();
	char *argv[] = { (char *) "openbox", NULL };
	Display *xdisplay;
	int i;

	if (g_getenv ("NEMO_TEST_OWN_DISPLAY") == NULL || !GDK_IS_X11_DISPLAY (display)) {
		return 0;
	}
	xdisplay = gdk_x11_display_get_xdisplay (display);
	if (has_window_manager (xdisplay)) {
		return 0;
	}
	if (!g_spawn_async (NULL, argv, NULL,
			    G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL,
			    NULL, NULL, &pid, NULL)) {
		g_print ("no openbox; the dialog has no frame here\n");
		return 0;
	}
	for (i = 0; i < 100 && !has_window_manager (xdisplay); i++) {
		g_usleep (50000);
	}
	check (has_window_manager (xdisplay));
#endif
	return pid;
}

static void
check_screen (const Probe *probe, int work_height)
{
	int room = nemo_archive_options_room (work_height, probe->closed_height);
	int shown = MIN (probe->natural, room);

	g_print ("%d px screen: options get %d of %d\n", work_height, shown, probe->natural);

	/* The buttons stay on screen. */
	check (probe->closed_height + shown <= work_height);

	/* Where they fit whole, they are not cut. */
	if (probe->closed_height + probe->natural <= work_height) {
		check (shown == probe->natural);
	}
}

static gboolean
probe_dialog (gpointer data)
{
	Probe *probe = data;
	GtkScrolledWindow *scroll;
	GdkRectangle work, frame;
	int room;

	probe->ran = TRUE;
	probe->dialog = find_dialog ();
	check (probe->dialog != NULL);
	if (probe->dialog == NULL) {
		return G_SOURCE_REMOVE;
	}

	settle ();
	/* The outer height, since the title bar and borders take screen too. Only
	   a session with no window manager has none. */
	gdk_window_get_frame_extents (gtk_widget_get_window (probe->dialog), &frame);
	probe->closed_height = frame.height;
	probe->expander = find_type (probe->dialog, GTK_TYPE_EXPANDER);
	check (probe->expander != NULL);
	if (probe->expander == NULL) {
		gtk_dialog_response (GTK_DIALOG (probe->dialog), GTK_RESPONSE_CANCEL);
		return G_SOURCE_REMOVE;
	}

	scroll = GTK_SCROLLED_WINDOW (gtk_bin_get_child (GTK_BIN (probe->expander)));
	gtk_widget_get_preferred_height (gtk_bin_get_child (GTK_BIN (scroll)), NULL, &probe->natural);

	/* The fixture only means something if the options would not fit whole on
	   the short screen. */
	check (probe->closed_height + probe->natural > 540);

	check_screen (probe, 540);
	check_screen (probe, 768);
	check_screen (probe, 1080);

	/* Opened for real, on a screen tall enough to hold everything. */
	gdk_monitor_get_workarea (gdk_display_get_monitor_at_window (gtk_widget_get_display (probe->dialog),
								     gtk_widget_get_window (probe->dialog)),
				  &work);
	gtk_expander_set_expanded (GTK_EXPANDER (probe->expander), TRUE);
	settle ();
	room = nemo_archive_options_room (work.height, probe->closed_height);
	g_print ("opened on %d px: scroll %d to %d, options want %d\n", work.height,
		 gtk_scrolled_window_get_min_content_height (scroll),
		 gtk_scrolled_window_get_max_content_height (scroll), probe->natural);
	check (gtk_scrolled_window_get_max_content_height (scroll) == room);
	check (gtk_scrolled_window_get_min_content_height (scroll) == MIN (probe->natural, room));

	gdk_window_get_frame_extents (gtk_widget_get_window (probe->dialog), &frame);
	g_print ("frame %d px tall at %d, work area %d to %d\n", frame.height, frame.y,
		 work.y, work.y + work.height);
	check (frame.y >= work.y && frame.y + frame.height <= work.y + work.height);

	gtk_dialog_response (GTK_DIALOG (probe->dialog), GTK_RESPONSE_CANCEL);
	return G_SOURCE_REMOVE;
}

static GtkWidget *
find_named (GtkWidget *widget, const char *name)
{
	GList *children, *l;
	GtkWidget *found = NULL;

	if (g_strcmp0 (gtk_widget_get_name (widget), name) == 0) {
		return widget;
	}
	if (!GTK_IS_CONTAINER (widget)) {
		return NULL;
	}

	children = gtk_container_get_children (GTK_CONTAINER (widget));
	for (l = children; l != NULL && found == NULL; l = l->next) {
		found = find_named (l->data, name);
	}
	g_list_free (children);

	return found;
}

typedef struct {
	gboolean    links_or_folders;
	const char *volume_size;	/* what the size entry should start with */
	gboolean    ran;
} Case;

static GtkWidget *
named (GtkWidget *dialog, const char *name)
{
	GtkWidget *widget = find_named (dialog, name);

	if (widget == NULL) {
		g_printerr ("no widget named %s\n", name);
	}
	check (widget != NULL);

	return widget;
}

static gboolean
is_on (GtkWidget *dialog, const char *name)
{
	GtkWidget *widget = named (dialog, name);

	return widget != NULL && gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget));
}

static gboolean
can_click (GtkWidget *dialog, const char *name)
{
	GtkWidget *widget = named (dialog, name);

	return widget != NULL && gtk_widget_get_sensitive (widget);
}

/* A radio group is shown or hidden as a whole, by the box it's in. */
static gboolean
group_shown (GtkWidget *dialog, const char *name)
{
	GtkWidget *widget = named (dialog, name);

	return widget != NULL && gtk_widget_get_visible (gtk_widget_get_parent (widget));
}

static void
click (GtkWidget *dialog, const char *name)
{
	GtkWidget *widget = named (dialog, name);

	if (widget != NULL) {
		gtk_button_clicked (GTK_BUTTON (widget));
	}
}

static void
set_format (GtkWidget *dialog, const char *id)
{
	check (gtk_combo_box_set_active_id (GTK_COMBO_BOX (named (dialog, "format")), id));
}

static gboolean
tip_says (GtkWidget *dialog, const char *name, const char *words)
{
	GtkWidget *widget = named (dialog, name);
	char *tip = widget != NULL ? gtk_widget_get_tooltip_text (widget) : NULL;
	gboolean says = tip != NULL && strstr (tip, words) != NULL;

	g_free (tip);

	return says;
}

/* The groups show only where there's a link or a folder, and the filesystem
   options are there either way but do nothing for plain files. */
static gboolean
probe_groups (gpointer data)
{
	Case *c = data;
	GtkWidget *dialog = find_dialog ();

	c->ran = TRUE;
	check (dialog != NULL);
	if (dialog == NULL) {
		return G_SOURCE_REMOVE;
	}

	check (group_shown (dialog, "symlinks-follow") == c->links_or_folders);
	check (can_click (dialog, "follow-other") == c->links_or_folders);
	check (!is_on (dialog, "follow-other"));
	check (tip_says (dialog, "symlinks-follow", ".lnk"));
#ifdef G_OS_WIN32
	check (group_shown (dialog, "junctions-follow") == c->links_or_folders);
	check (tip_says (dialog, "junctions-follow", ".lnk"));
	check (find_named (dialog, "follow-nested") == NULL);
#else
	check (find_named (dialog, "junctions-follow") == NULL);
	check (can_click (dialog, "follow-nested") == c->links_or_folders);
	/* Commented out: grayed for the selection, it shows off since
	   2026100516274646, as any box that can't apply does. */
	/* check (is_on (dialog, "follow-nested")); */
	check (is_on (dialog, "follow-nested") == c->links_or_folders);
#endif
	check (is_on (dialog, "symlinks-ignore"));

	gtk_dialog_response (GTK_DIALOG (dialog), GTK_RESPONSE_CANCEL);
	return G_SOURCE_REMOVE;
}

/* A store choice the format and its options can't do is grayed and shows as
   Ignore, and the choice comes back with options that can. 7-Zip plays its
   Windows self here, keeping no links, so a 7z with a password or volumes has
   no store choice left. */
static gboolean
probe_stores (gpointer data)
{
	Case *c = data;
	GtkWidget *dialog = find_dialog ();
	GtkWidget *password;
	gboolean seven = nemo_archive_backend_present (NEMO_ARCHIVE_BACKEND_7Z);

	c->ran = TRUE;
	check (dialog != NULL);
	if (dialog == NULL) {
		return G_SOURCE_REMOVE;
	}
	password = named (dialog, "password");

	set_format (dialog, "tar");
	check (can_click (dialog, "symlinks-store"));
	click (dialog, "symlinks-store");
	check (is_on (dialog, "symlinks-store"));

	/* The group's access key goes to the choice that's on, and changes
	   nothing. */
	check (gtk_widget_mnemonic_activate (named (dialog, "symlinks"), FALSE));
	check (is_on (dialog, "symlinks-store"));
	check (gtk_window_get_focus (GTK_WINDOW (dialog)) == named (dialog, "symlinks-store"));

	set_format (dialog, "7z");
	check (can_click (dialog, "symlinks-store"));
	check (is_on (dialog, "symlinks-store"));

	/* Without 7-Zip a 7z takes no password or volumes at all. */
	if (seven) {
		check (tip_says (dialog, "symlinks-store", "one thread"));

		gtk_entry_set_text (GTK_ENTRY (password), "secret");
		check (!can_click (dialog, "symlinks-store"));
		check (is_on (dialog, "symlinks-ignore"));
		gtk_entry_set_text (GTK_ENTRY (password), "");
		check (can_click (dialog, "symlinks-store"));
		check (is_on (dialog, "symlinks-store"));

		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (named (dialog, "split")), TRUE);
		check (!can_click (dialog, "symlinks-store"));
		check (is_on (dialog, "symlinks-ignore"));
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (named (dialog, "split")), FALSE);
		check (is_on (dialog, "symlinks-store"));
	} else {
		g_printerr ("note: no 7z command here, a 7z with a password not checked\n");
	}

	/* A zip with a password is the library's, which keeps links. */
	set_format (dialog, "zip");
	gtk_entry_set_text (GTK_ENTRY (password), "secret");
	check (can_click (dialog, "symlinks-store"));
	check (is_on (dialog, "symlinks-store"));
	check (!tip_says (dialog, "symlinks-store", "one thread"));
	gtk_entry_set_text (GTK_ENTRY (password), "");

	click (dialog, "symlinks-follow");
	check (is_on (dialog, "symlinks-follow"));
	set_format (dialog, "7z");
	gtk_entry_set_text (GTK_ENTRY (password), "secret");
	check (is_on (dialog, "symlinks-follow"));
	gtk_entry_set_text (GTK_ENTRY (password), "");

#ifdef G_OS_WIN32
	/* Junctions takes its default from Symlinks by the table, here the
	   library's: a junction as a symlink. */
	set_format (dialog, "zip");
	click (dialog, "symlinks-store");
	check (is_on (dialog, "junctions-store-symlink"));
	check (!can_click (dialog, "junctions-store-junction"));
	click (dialog, "symlinks-follow");
	check (is_on (dialog, "junctions-follow"));
	click (dialog, "symlinks-ignore");
	check (is_on (dialog, "junctions-ignore"));

	/* A hand change sticks. */
	click (dialog, "junctions-follow");
	click (dialog, "symlinks-store");
	check (is_on (dialog, "junctions-follow"));
	click (dialog, "symlinks-ignore");
	check (is_on (dialog, "junctions-follow"));

	click (dialog, "junctions-store-symlink");
	set_format (dialog, "7z");
	if (seven) {
		check (tip_says (dialog, "junctions-store-symlink", "one thread"));
		gtk_entry_set_text (GTK_ENTRY (password), "secret");
		check (!can_click (dialog, "junctions-store-symlink"));
		check (is_on (dialog, "junctions-ignore"));
		gtk_entry_set_text (GTK_ENTRY (password), "");
	}
	check (is_on (dialog, "junctions-store-symlink"));
#endif

	gtk_dialog_response (GTK_DIALOG (dialog), GTK_RESPONSE_CANCEL);
	return G_SOURCE_REMOVE;
}

/* The decided list, each with its note, the 2 GiB one picked to start with,
   and a remembered size shown as its preset or as it was typed. */
static gboolean
probe_volumes (gpointer data)
{
	Case *c = data;
	GtkWidget *dialog = find_dialog ();
	GtkWidget *combo;
	GtkTreeModel *model;
	GtkTreeIter iter;
	guint i = 0;

	c->ran = TRUE;
	check (dialog != NULL);
	if (dialog == NULL) {
		return G_SOURCE_REMOVE;
	}
	combo = named (dialog, "volume-size");
	model = gtk_combo_box_get_model (GTK_COMBO_BOX (combo));

	if (gtk_tree_model_get_iter_first (model, &iter)) {
		do {
			char *text = NULL;

			gtk_tree_model_get (model, &iter,
					    gtk_combo_box_get_entry_text_column (GTK_COMBO_BOX (combo)),
					    &text, -1);
			check (g_strcmp0 (text, nemo_archive_volume_preset (i, NULL)) == 0);
			g_free (text);
			i++;
		} while (gtk_tree_model_iter_next (model, &iter));
	}
	check (i == 8);
	check (nemo_archive_volume_preset (i, NULL) == NULL);

	check (g_strcmp0 (gtk_entry_get_text (GTK_ENTRY (gtk_bin_get_child (GTK_BIN (combo)))),
			  c->volume_size) == 0);

	gtk_dialog_response (GTK_DIALOG (dialog), GTK_RESPONSE_CANCEL);
	return G_SOURCE_REMOVE;
}

/* The first format that can or can't do what's asked, or NULL. */
static const char *
format_that (NemoArchiveCaps caps, gboolean can)
{
	int i;

	for (i = 0; i < NEMO_ARCHIVE_N_FORMATS; i++) {
		if (nemo_archive_format_available ((NemoArchiveFormat) i) &&
		    ((nemo_archive_format_caps ((NemoArchiveFormat) i) & caps) == caps) == can) {
			return nemo_archive_format_id ((NemoArchiveFormat) i);
		}
	}

	return NULL;
}

static GtkWidget *
compress_button (GtkWidget *dialog)
{
	return gtk_dialog_get_widget_for_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);
}

static gboolean
marked (GtkWidget *widget)
{
	return gtk_style_context_has_class (gtk_widget_get_style_context (widget),
					    GTK_STYLE_CLASS_ERROR);
}

static gboolean
still_up (gpointer data)
{
	Case *c = data;
	GtkWidget *dialog = find_dialog ();

	c->ran = TRUE;
	check (dialog != NULL);
	if (dialog != NULL) {
		gtk_dialog_response (GTK_DIALOG (dialog), GTK_RESPONSE_CANCEL);
	}

	return G_SOURCE_REMOVE;
}

/* Splitting is a requirement, so a volume size that can't be read keeps
   Compress grayed and the size marked, rather than writing one file. */
static gboolean
probe_unreadable_size (gpointer data)
{
	Case *c = data;
	GtkWidget *dialog = find_dialog ();
	GtkWidget *entry;
	const char *id = format_that (NEMO_ARCHIVE_CAP_SPLIT, TRUE);

	c->ran = TRUE;
	check (dialog != NULL);
	if (dialog == NULL) {
		return G_SOURCE_REMOVE;
	}
	if (id == NULL) {
		g_printerr ("note: nothing here splits, volume sizes not checked\n");
		gtk_dialog_response (GTK_DIALOG (dialog), GTK_RESPONSE_CANCEL);
		return G_SOURCE_REMOVE;
	}

	entry = gtk_bin_get_child (GTK_BIN (named (dialog, "volume-size")));
	set_format (dialog, id);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (named (dialog, "split")), TRUE);
	check (gtk_widget_get_sensitive (compress_button (dialog)));
	check (!marked (entry));

	gtk_entry_set_text (GTK_ENTRY (entry), "lots");
	check (!gtk_widget_get_sensitive (compress_button (dialog)));
	check (marked (entry));

	/* Grouped as the presets show it. */
	gtk_entry_set_text (GTK_ENTRY (entry), "4,000 MiB");
	check (gtk_widget_get_sensitive (compress_button (dialog)));
	check (!marked (entry));

	/* Not splitting, the size doesn't matter. */
	gtk_entry_set_text (GTK_ENTRY (entry), "lots");
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (named (dialog, "split")), FALSE);
	check (gtk_widget_get_sensitive (compress_button (dialog)));
	check (!marked (entry));

	/* Nor with a format that can't split. */
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (named (dialog, "split")), TRUE);
	check (!gtk_widget_get_sensitive (compress_button (dialog)));
	if (format_that (NEMO_ARCHIVE_CAP_SPLIT, FALSE) != NULL) {
		set_format (dialog, format_that (NEMO_ARCHIVE_CAP_SPLIT, FALSE));
		check (gtk_widget_get_sensitive (compress_button (dialog)));
		set_format (dialog, id);
	}

	/* Enter or a response from elsewhere doesn't get past it either. */
	check (!gtk_widget_get_sensitive (compress_button (dialog)));
	g_timeout_add (300, still_up, c);
	c->ran = FALSE;
	gtk_dialog_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);

	return G_SOURCE_REMOVE;
}

static gboolean
options_open (GtkWidget *dialog)
{
	return gtk_expander_get_expanded (GTK_EXPANDER (find_type (dialog, GTK_TYPE_EXPANDER)));
}

/* Everything at its default: Options closed and Reset grayed. A change makes
   Reset do something, and Reset puts it back, password and all. */
static gboolean
probe_reset (gpointer data)
{
	Case *c = data;
	GtkWidget *dialog = find_dialog ();
	GtkWidget *password;
	const char *id = format_that (NEMO_ARCHIVE_CAP_SOLID | NEMO_ARCHIVE_CAP_PASSWORD, TRUE);

	c->ran = TRUE;
	check (dialog != NULL);
	if (dialog == NULL) {
		return G_SOURCE_REMOVE;
	}
	password = named (dialog, "password");

	check (!options_open (dialog));
	check (!can_click (dialog, "reset-options"));

	gtk_expander_set_expanded (GTK_EXPANDER (find_type (dialog, GTK_TYPE_EXPANDER)), TRUE);
	settle ();
	click (dialog, "symlinks-follow");
	check (can_click (dialog, "reset-options"));
	if (id != NULL) {
		set_format (dialog, id);
		click (dialog, "solid");
		check (is_on (dialog, "solid"));
		gtk_entry_set_text (GTK_ENTRY (password), "secret");
	} else {
		g_printerr ("note: no format here makes a solid archive with a password\n");
	}

	click (dialog, "reset-options");
	settle ();
	check (!options_open (dialog));
	check (!can_click (dialog, "reset-options"));
	check (is_on (dialog, "symlinks-ignore"));
	check (!is_on (dialog, "solid"));
	check (gtk_entry_get_text_length (GTK_ENTRY (password)) == 0);

	/* A password alone is enough to reset. */
	if (id != NULL) {
		gtk_entry_set_text (GTK_ENTRY (password), "secret");
		check (can_click (dialog, "reset-options"));
		gtk_entry_set_text (GTK_ENTRY (password), "");
		check (!can_click (dialog, "reset-options"));
	}

	gtk_dialog_response (GTK_DIALOG (dialog), GTK_RESPONSE_CANCEL);
	return G_SOURCE_REMOVE;
}

/* Remembered choices that aren't the defaults open Options by itself, as a
   click would, and Reset drops them from the settings file. */
static gboolean
probe_opens_by_itself (gpointer data)
{
	Case *c = data;
	GtkWidget *dialog = find_dialog ();
	NemoConfigGroup *group = nemo_config_get_group (NEMO_ARCHIVE_COMMANDS_GROUP);
	GtkScrolledWindow *scroll;
	GdkRectangle work, frame;

	c->ran = TRUE;
	check (dialog != NULL);
	if (dialog == NULL) {
		return G_SOURCE_REMOVE;
	}

	settle ();
	check (options_open (dialog));
	check (can_click (dialog, "reset-options"));
	check (is_on (dialog, "symlinks-follow"));
	check (g_strcmp0 (gtk_entry_get_text (GTK_ENTRY (gtk_bin_get_child (GTK_BIN (
		named (dialog, "volume-size"))))), "4,095 MiB (max FAT32 size)") == 0);

	/* Opened the way a click opens it: the options scroll in the room the
	   screen has, and the buttons stay on it. */
	scroll = GTK_SCROLLED_WINDOW (gtk_bin_get_child (GTK_BIN (find_type (dialog, GTK_TYPE_EXPANDER))));
	check (gtk_scrolled_window_get_max_content_height (scroll) > 0);
	gdk_monitor_get_workarea (gdk_display_get_monitor_at_window (gtk_widget_get_display (dialog),
								     gtk_widget_get_window (dialog)),
				  &work);
	gdk_window_get_frame_extents (gtk_widget_get_window (dialog), &frame);
	check (frame.y >= work.y && frame.y + frame.height <= work.y + work.height);

	click (dialog, "reset-options");
	settle ();
	check (!options_open (dialog));
	check (is_on (dialog, "symlinks-ignore"));
	check (g_strcmp0 (gtk_entry_get_text (GTK_ENTRY (gtk_bin_get_child (GTK_BIN (
		named (dialog, "volume-size"))))), "2 GiB (old 32-bit tools)") == 0);
	check (!nemo_config_is_set (group, NEMO_ARCHIVE_STATE_KEY_SYMLINKS));
	check (!nemo_config_is_set (group, NEMO_ARCHIVE_STATE_KEY_SPLIT_SIZE));
	check (!nemo_config_is_set (group, NEMO_ARCHIVE_STATE_KEY_LOCK));
	check (!nemo_config_is_set (group, NEMO_ARCHIVE_STATE_KEY_RECOVERY));
	/* Not under Options. */
	check (nemo_config_is_set (group, NEMO_ARCHIVE_STATE_KEY_FORMAT));

	gtk_dialog_response (GTK_DIALOG (dialog), GTK_RESPONSE_CANCEL);
	return G_SOURCE_REMOVE;
}

/* A remembered choice the format turns off still opens Options. It shows off
   and grayed, and comes back with a format that can do it. */
static gboolean
probe_forced_off (gpointer data)
{
	Case *c = data;
	GtkWidget *dialog = find_dialog ();
	const char *can = format_that (NEMO_ARCHIVE_CAP_LOCK, TRUE);

	c->ran = TRUE;
	check (dialog != NULL);
	if (dialog == NULL) {
		return G_SOURCE_REMOVE;
	}

	settle ();
	check (options_open (dialog));
	check (can_click (dialog, "reset-options"));
	check (!is_on (dialog, "lock"));
	check (!can_click (dialog, "lock"));

	if (can != NULL) {
		set_format (dialog, can);
		check (is_on (dialog, "lock"));
		check (can_click (dialog, "lock"));
		set_format (dialog, format_that (NEMO_ARCHIVE_CAP_LOCK, FALSE));
		check (!is_on (dialog, "lock"));
	} else {
		g_printerr ("note: nothing here locks an archive, switching back not checked\n");
	}

	gtk_dialog_response (GTK_DIALOG (dialog), GTK_RESPONSE_CANCEL);
	return G_SOURCE_REMOVE;
}

static void run_case (GSourceFunc probe, Case *c, GList *files, GFile *dir);

/* The size totals, on a tree with made-up mounts around it: nest and nest2
   in the tree's pool, other on a disk of its own, and share a share. Byte
   counts stay under 1000 so each one reads back exactly. */
static char *tree;

static const struct {
	const char *path;
	gsize       bytes;
} tree_files[] = {
	{ "sel/a.bin",        3 },
	{ "sel/sub/b.bin",    5 },
	{ "sel/nest/c.bin",   7 },
	{ "sel/other/d.bin", 11 },
	{ "sel/share/e.bin", 13 },
	{ "outside/f.bin",   17 },
	{ "outside/dir/g.bin", 19 },
	{ "nest2/h.bin",     23 },
};

#ifndef G_OS_WIN32
static const struct {
	const char *path;
	const char *target;
} tree_links[] = {
	{ "sel/flink",   "../outside/f.bin" },
	{ "sel/dlink",   "../outside/dir" },
	{ "sel/tonest",  "../nest2" },
	{ "sel/toshare", "share" },
};
#endif

/* Returns: (transfer full) */
static char *
in_tree (const char *relative)
{
	char *path = g_build_filename (tree, relative, NULL);
	char *c;

	for (c = path; *c != '\0'; c++) {
		if (*c == '/') {
			*c = G_DIR_SEPARATOR;
		}
	}
	return path;
}

static gboolean
make_tree (const char *scratch)
{
	char *made = g_build_filename (scratch, "tree", NULL);
	gsize i;

#ifdef G_OS_WIN32
	tree = made;
#else
	/* The scan goes by real paths, and the made-up mounts have to match. */
	if (g_mkdir_with_parents (made, 0755) != 0) {
		g_free (made);
		return FALSE;
	}
	{
		char *real = realpath (made, NULL);

		g_free (made);
		if (real == NULL) {
			return FALSE;
		}
		tree = g_strdup (real);
		free (real);
	}
#endif

	for (i = 0; i < G_N_ELEMENTS (tree_files); i++) {
		char *path = in_tree (tree_files[i].path);
		char *dir = g_path_get_dirname (path);
		char *bytes = g_malloc0 (tree_files[i].bytes);
		gboolean ok = g_mkdir_with_parents (dir, 0755) == 0 &&
			      g_file_set_contents (path, bytes, (gssize) tree_files[i].bytes, NULL);

		g_free (bytes);
		g_free (dir);
		g_free (path);
		if (!ok) {
			return FALSE;
		}
	}

	/* Windows needs rights for a symlink, and wine makes none, so there
	   the links are left out of both counts alike. */
#ifndef G_OS_WIN32
	for (i = 0; i < G_N_ELEMENTS (tree_links); i++) {
		char *path = in_tree (tree_links[i].path);
		GFile *link = g_file_new_for_path (path);

		check (g_file_make_symbolic_link (link, tree_links[i].target, NULL, NULL));
		g_object_unref (link);
		g_free (path);
	}
#endif
	return TRUE;
}

/* Returns: (transfer full): free with arc_mount_table_free */
static ArcMountTable *
tree_mounts (void)
{
	char *where[5] = { in_tree (""), in_tree ("sel/nest"), in_tree ("nest2"),
			   in_tree ("sel/other"), in_tree ("sel/share") };
#ifdef G_OS_WIN32
	ArcMountEntry entries[] = {
		{ where[0], "\\\\?\\Volume{aaaa}\\", NULL },
		{ where[1], "\\\\?\\Volume{bbbb}\\", NULL },
		{ where[2], "\\\\?\\Volume{cccc}\\", NULL },
		{ where[3], "\\\\?\\Volume{dddd}\\", NULL },
		{ where[4], NULL, NULL },
	};
	gboolean windows = TRUE;
#else
	ArcMountEntry entries[] = {
		{ where[0], "tank/tree", "zfs" },
		{ where[1], "tank/tree/nest", "zfs" },
		{ where[2], "tank/nest2", "zfs" },
		{ where[3], "/dev/sdz9", "ext4" },
		{ where[4], "//server/share", "cifs" },
	};
	gboolean windows = FALSE;
#endif
	ArcMountTable *table = arc_mount_table_new (entries, G_N_ELEMENTS (entries), windows);
	gsize i;

	for (i = 0; i < G_N_ELEMENTS (where); i++) {
		g_free (where[i]);
	}
	return table;
}

static void
saw_core (gpointer             data,
	  const ArcScanReport *report)
{
	*(ArcScanReport *) data = *report;
}

/* The core's own numbers for the selection, every mix walked. */
static gboolean
core_report (ArcScanReport *report)
{
	char *sel = in_tree ("sel");
	const char *paths[] = { sel, NULL };
	gint64 end = g_get_monotonic_time () + (gint64) (30 * test_slowness () * G_USEC_PER_SEC);
	ArcHost host;
	ArcScan *scan;

	memset (report, 0, sizeof *report);
	nemo_archive_host_init (&host);
	scan = arc_scan_new (&host, tree, paths, ARC_FOLLOW_ALL, FALSE, tree_mounts ());
	arc_scan_start (scan, saw_core, report);
	while (!report->done && g_get_monotonic_time () < end) {
		g_main_context_iteration (NULL, FALSE);
		g_usleep (2000);
	}
	arc_scan_free (scan);
	g_free (sel);

	return report->done;
}

static ArcScanReport core;

static char *
size_of (guint64 bytes)
{
	return g_format_size_full (bytes, nemo_global_preferences_get_size_prefix_preference ());
}

/* The bits the controls show. Junctions takes Symlinks' choice by the table
   until it's set by hand, and nested is hidden on Windows, where it's left
   at its default and nothing is nested. */
static guint
follow_shown (GtkWidget *dialog)
{
	guint follow = 0;

	if (is_on (dialog, "symlinks-follow")) {
		follow |= ARC_FOLLOW_SYMLINKS;
	}
#ifdef G_OS_WIN32
	if (is_on (dialog, "junctions-follow")) {
		follow |= ARC_FOLLOW_JUNCTIONS;
	}
	follow |= ARC_FOLLOW_NESTED_FS;
#else
	if (follow & ARC_FOLLOW_SYMLINKS) {
		follow |= ARC_FOLLOW_JUNCTIONS;
	}
	if (is_on (dialog, "follow-nested")) {
		follow |= ARC_FOLLOW_NESTED_FS;
	}
#endif
	if (is_on (dialog, "follow-other")) {
		follow |= ARC_FOLLOW_OTHER_FS;
	}
	return follow;
}

static char *
total_wanted (guint follow)
{
	char *size = size_of (core.totals[follow]);
	char *text = g_strdup_printf ("Total size to include: %s", size);

	g_free (size);
	return text;
}

/* Until the total is the core's for the options as shown, and final. */
static gboolean
wait_for_total (GtkWidget *dialog,
		guint      follow)
{
	GtkLabel *label = GTK_LABEL (named (dialog, "total-size"));
	char *wanted = total_wanted (follow);
	gint64 end = g_get_monotonic_time () + (gint64) (30 * test_slowness () * G_USEC_PER_SEC);
	gboolean there = FALSE;

	while (!there && g_get_monotonic_time () < end) {
		const char *text = gtk_label_get_text (label);

		there = g_str_has_prefix (text, wanted) &&
			(text[strlen (wanted)] == '\0' || text[strlen (wanted)] == ',');
		if (!there) {
			g_main_context_iteration (NULL, FALSE);
			g_usleep (2000);
		}
	}
	if (!there) {
		g_printerr ("follow %u: \"%s\", wanted \"%s\"\n", follow, gtk_label_get_text (label), wanted);
	}
	g_free (wanted);
	return there;
}

/* Each Follow control shows what turning it off would take out, and only
   while it's on. */
static gboolean
change_shown (GtkWidget  *dialog,
	      const char *name,
	      const char *text,
	      guint       follow,
	      ArcFollow   option,
	      gboolean    say)
{
	const char *label = gtk_button_get_label (GTK_BUTTON (named (dialog, name)));
	gboolean same;
	char *wanted;

	if (follow & option) {
		char *size = size_of (core.totals[follow] - core.totals[follow & ~(guint) option]);

		wanted = g_strdup_printf ("%s (+%s)", text, size);
		g_free (size);
	} else {
		wanted = g_strdup (text);
	}
	same = g_strcmp0 (label, wanted) == 0;
	if (!same && say) {
		g_printerr ("follow %u, %s: \"%s\", wanted \"%s\"\n", follow, name, label, wanted);
	}
	g_free (wanted);
	return same;
}

static gboolean
changes_shown (GtkWidget *dialog,
	       guint      follow,
	       gboolean   say)
{
	gboolean all = TRUE;

	all = change_shown (dialog, "symlinks-follow", "Follow", follow, ARC_FOLLOW_SYMLINKS, say) && all;
#ifdef G_OS_WIN32
	all = change_shown (dialog, "junctions-follow", "Follow", follow, ARC_FOLLOW_JUNCTIONS, say) && all;
#else
	all = change_shown (dialog, "follow-nested", "Follow nes_ted filesystems", follow,
			    ARC_FOLLOW_NESTED_FS, say) && all;
#endif
	all = change_shown (dialog, "follow-other", "Follow ot_her filesystems", follow,
			    ARC_FOLLOW_OTHER_FS, say) && all;
	return all;
}

/* The total can match a stale report where 2 mixes come to the same, so
   the changes are waited for too. */
static void
check_sizes (GtkWidget *dialog)
{
	guint follow = follow_shown (dialog);
	gint64 end = g_get_monotonic_time () + (gint64) (30 * test_slowness () * G_USEC_PER_SEC);

	check (wait_for_total (dialog, follow));
	while (!changes_shown (dialog, follow, FALSE) && g_get_monotonic_time () < end) {
		g_main_context_iteration (NULL, FALSE);
		g_usleep (2000);
	}
	if (!changes_shown (dialog, follow, TRUE)) {
		failures++;
	}
	/* A final total stays put. */
	check (wait_for_total (dialog, follow));
}

static gboolean
still_counting (gpointer data)
{
	check (nemo_archive_dialog_scans_for_test () == 1);
	return still_up (data);
}

static gboolean
probe_sizes (gpointer data)
{
	Case *c = data;
	GtkWidget *dialog = find_dialog ();
	GtkWidget *name;
	const char *label;
	char *shares;

	c->ran = TRUE;
	check (dialog != NULL);
	if (dialog == NULL) {
		return G_SOURCE_REMOVE;
	}
	check (nemo_archive_dialog_scans_for_test () == 1);

	/* The defaults: nested on, the rest off. */
	check_sizes (dialog);

	/* Wider, then wider again with a format change in the middle. */
	click (dialog, "symlinks-follow");
	check_sizes (dialog);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (named (dialog, "follow-other")), TRUE);
	set_format (dialog, "tar");
	check_sizes (dialog);
	check (follow_shown (dialog) == (ARC_FOLLOW_ALL));

	/* Left out, and said so. */
	label = gtk_label_get_text (GTK_LABEL (named (dialog, "total-size")));
	shares = g_strdup_printf (", not counting %u on a share", core.shares);
	check (core.shares > 0);
	check (strstr (label, shares) != NULL);
	g_free (shares);

	/* Narrower. */
#ifdef G_OS_WIN32
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (named (dialog, "junctions-ignore")), TRUE);
#else
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (named (dialog, "follow-nested")), FALSE);
#endif
	check_sizes (dialog);

	/* Stored is not followed. */
	click (dialog, "symlinks-store");
	check (is_on (dialog, "symlinks-store"));
	check_sizes (dialog);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (named (dialog, "follow-other")), FALSE);
	check_sizes (dialog);

	/* Compress that goes back to the dialog keeps counting. */
	name = named (dialog, "name");
	gtk_entry_set_text (GTK_ENTRY (name), "");
	g_timeout_add (300, still_counting, c);
	c->ran = FALSE;
	gtk_dialog_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);

	return G_SOURCE_REMOVE;
}

/* Plain files have nothing to follow. */
static gboolean
probe_plain_sizes (gpointer data)
{
	Case *c = data;
	GtkWidget *dialog = find_dialog ();

	c->ran = TRUE;
	check (dialog != NULL);
	if (dialog == NULL) {
		return G_SOURCE_REMOVE;
	}

	check (wait_for_total (dialog, 0));
	gtk_dialog_response (GTK_DIALOG (dialog), GTK_RESPONSE_CANCEL);
	return G_SOURCE_REMOVE;
}

/* Nothing on a share is walked before Compress. */
static gboolean
probe_on_share (gpointer data)
{
	Case *c = data;
	GtkWidget *dialog = find_dialog ();

	c->ran = TRUE;
	check (dialog != NULL);
	if (dialog == NULL) {
		return G_SOURCE_REMOVE;
	}

	check (nemo_archive_dialog_scans_for_test () == 0);
	check (g_strcmp0 (gtk_label_get_text (GTK_LABEL (named (dialog, "total-size"))),
			  "Total size to include: not counted on a share") == 0);
	gtk_dialog_response (GTK_DIALOG (dialog), GTK_RESPONSE_CANCEL);
	return G_SOURCE_REMOVE;
}

/* The window it was opened from closes under it, with a report due. */
static gboolean
close_parent (gpointer data)
{
	GtkWidget *parent = data;

	check (nemo_archive_dialog_scans_for_test () == 1);
	gtk_widget_destroy (parent);
	return G_SOURCE_REMOVE;
}

static void
check_scan_sizes (const char *scratch)
{
	char *share;
	const char *shares[2] = { NULL, NULL };
	char *sel_path, *on_share_path;
	GFile *dir, *sel, *on_share;
	GList *files;
	GtkWidget *parent;
	Case folder = { TRUE, NULL, FALSE };
	Case plain = { FALSE, NULL, FALSE };
	guint mix;

	check (make_tree (scratch));
	share = in_tree ("sel/share");
	shares[0] = share;
	nemo_share_set_roots_for_test (shares);
	nemo_archive_dialog_set_mounts_for_test (tree_mounts);

	check (core_report (&core));
	/* Something for every option to add, or the comparisons prove little. */
	check (core.totals[ARC_FOLLOW_OTHER_FS] > core.totals[0]);
#ifndef G_OS_WIN32
	check (core.totals[ARC_FOLLOW_NESTED_FS] > core.totals[0]);
	check (core.totals[ARC_FOLLOW_SYMLINKS] > core.totals[0]);
#endif
	for (mix = 0; mix < ARC_MIX_COUNT; mix++) {
		check (core.totals[mix] < 1000);
	}

	dir = g_file_new_for_path (tree);
	sel_path = in_tree ("sel");
	sel = g_file_new_for_path (sel_path);
	files = g_list_prepend (NULL, sel);

	folder.ran = FALSE;
	g_timeout_add (300, probe_sizes, &folder);
	nemo_archive_dialog_show (NULL, files, dir, FALSE, TRUE);
	check (folder.ran);
	check (nemo_archive_dialog_scans_for_test () == 0);

	run_case (probe_plain_sizes, &plain, files, dir);
	check (nemo_archive_dialog_scans_for_test () == 0);

	/* The dialog is gone by the time the call ends, and a destroy of it
	   then is only seen as a critical from GTK. */
	parent = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	gtk_widget_show (parent);
	g_timeout_add (240, close_parent, parent);
	g_log_set_always_fatal (G_LOG_FATAL_MASK | G_LOG_LEVEL_CRITICAL);
	nemo_archive_dialog_show (GTK_WINDOW (parent), files, dir, FALSE, TRUE);
	g_log_set_always_fatal (G_LOG_FATAL_MASK);
	check (nemo_archive_dialog_scans_for_test () == 0);
	g_list_free (files);

	on_share_path = in_tree ("sel/share/e.bin");
	on_share = g_file_new_for_path (on_share_path);
	files = g_list_prepend (NULL, on_share);
	run_case (probe_on_share, &plain, files, dir);
	g_list_free (files);

	nemo_archive_dialog_set_mounts_for_test (NULL);
	nemo_share_set_roots_for_test (NULL);
	g_object_unref (on_share);
	g_object_unref (sel);
	g_object_unref (dir);
	g_free (on_share_path);
	g_free (sel_path);
	g_free (share);
}

static void
run_case (GSourceFunc probe, Case *c, GList *files, GFile *dir)
{
	c->ran = FALSE;
	g_timeout_add (300, probe, c);
	nemo_archive_dialog_show (NULL, files, dir, FALSE, c->links_or_folders);
	check (c->ran);
}

int
main (int argc, char *argv[])
{
	g_autofree char *path = NULL;
	GFile *dir, *file;
	GList *files;
	Probe probe = { 0 };
	Case plain = { FALSE, "2 GiB (old 32-bit tools)", FALSE };
	Case folder = { TRUE, "2 GiB (old 32-bit tools)", FALSE };
	NemoConfigGroup *group;
	GPid window_manager;
	char *tmp;

	test_own_display (argc, argv, NULL);
	tmp = test_scratch_config_home ("nemo-archive-dialog-test-XXXXXX");

	if (!gtk_init_check (&argc, &argv)) {
		g_print ("SKIP: no display\n");
		g_free (tmp);
		return 77;
	}
	/* A session with no monitor, such as a service session on Windows, has
	   nothing to place a menu or size a dialog against. */
	if (gdk_display_get_n_monitors (gdk_display_get_default ()) == 0) {
		g_print ("SKIP: no monitor\n");
		g_free (tmp);
		return 77;
	}

	window_manager = start_window_manager ();
	nemo_global_preferences_init ();

	path = g_build_filename (tmp, "photo.jpg", NULL);
	check (g_file_set_contents (path, "x", -1, NULL));
	dir = g_file_new_for_path (tmp);
	file = g_file_new_for_path (path);
	files = g_list_prepend (NULL, file);

	/* With the link choices showing, which is the taller dialog. */
	g_timeout_add (300, probe_dialog, &probe);
	nemo_archive_dialog_show (NULL, files, dir, FALSE, TRUE);
	check (probe.ran);

	run_case (probe_groups, &plain, files, dir);
	run_case (probe_groups, &folder, files, dir);

	nemo_archive_set_7z_keeps_links (FALSE);
	run_case (probe_stores, &folder, files, dir);
#ifndef G_OS_WIN32
	nemo_archive_set_7z_keeps_links (TRUE);
#endif

	run_case (probe_volumes, &plain, files, dir);
	group = nemo_config_get_group (NEMO_ARCHIVE_COMMANDS_GROUP);
	nemo_config_set_string (group, NEMO_ARCHIVE_STATE_KEY_SPLIT_SIZE, "4480 MiB");
	plain.volume_size = "4,480 MiB (DVD)";
	run_case (probe_volumes, &plain, files, dir);
	nemo_config_set_string (group, NEMO_ARCHIVE_STATE_KEY_SPLIT_SIZE, "1 GiB");
	plain.volume_size = "1 GiB";
	run_case (probe_volumes, &plain, files, dir);
	nemo_config_reset (group, NEMO_ARCHIVE_STATE_KEY_SPLIT_SIZE);

	run_case (probe_unreadable_size, &plain, files, dir);
	check (!nemo_config_is_set (group, NEMO_ARCHIVE_STATE_KEY_SPLIT));

	run_case (probe_reset, &folder, files, dir);

	nemo_config_set_string (group, NEMO_ARCHIVE_STATE_KEY_SPLIT_SIZE, "4095 MiB");
	nemo_config_set_enum (group, NEMO_ARCHIVE_STATE_KEY_SYMLINKS, ARC_LINK_FOLLOW);
	nemo_config_set_boolean (group, NEMO_ARCHIVE_STATE_KEY_LOCK, TRUE);
	nemo_config_set_string (group, NEMO_ARCHIVE_STATE_KEY_FORMAT, "tar");
	run_case (probe_opens_by_itself, &folder, files, dir);
	nemo_config_reset (group, NEMO_ARCHIVE_STATE_KEY_FORMAT);

	/* Locking is the only remembered choice, and the format can't lock. */
	nemo_config_set_boolean (group, NEMO_ARCHIVE_STATE_KEY_LOCK, TRUE);
	nemo_config_set_string (group, NEMO_ARCHIVE_STATE_KEY_FORMAT,
				format_that (NEMO_ARCHIVE_CAP_LOCK, FALSE));
	run_case (probe_forced_off, &folder, files, dir);
	check (nemo_config_get_boolean (group, NEMO_ARCHIVE_STATE_KEY_LOCK));
	nemo_config_reset (group, NEMO_ARCHIVE_STATE_KEY_LOCK);
	nemo_config_reset (group, NEMO_ARCHIVE_STATE_KEY_FORMAT);

	check_scan_sizes (tmp);

	g_list_free (files);
	g_object_unref (file);
	g_object_unref (dir);
	g_free (tmp);
	g_free (tree);
#ifdef GDK_WINDOWING_X11
	if (window_manager != 0) {
		kill (window_manager, SIGTERM);
		g_spawn_close_pid (window_manager);
	}
#else
	(void) window_manager;
#endif

	if (failures > 0) {
		g_printerr ("%d check(s) failed\n", failures);
		return EXIT_FAILURE;
	}
	g_print ("PASS\n");
	return EXIT_SUCCESS;
}
