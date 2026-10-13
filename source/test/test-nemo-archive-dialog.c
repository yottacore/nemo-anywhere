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
#include <libnemo-private/nemo-config.h>
#include <libnemo-private/nemo-global-preferences.h>

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
	check (is_on (dialog, "follow-nested"));
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

	g_list_free (files);
	g_object_unref (file);
	g_object_unref (dir);
	g_free (tmp);
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
