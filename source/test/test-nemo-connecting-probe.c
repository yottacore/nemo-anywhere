/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-connecting-probe.c - a share that takes its time to mount, and a
   look at the window while it waits.

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

/* Preloaded into the real program by test-nemo-connecting. Every smb:// file
 * answers "not mounted", and a mount of one is held until this file lets it
 * go, so the window waits exactly as long as the probe wants. It types an
 * smb:// address into the location bar, looks at the path bar and the
 * pointer, presses Escape, then tries again and lets the mount fail. One "ok"
 * or "FAIL" line per check goes to $NEMO_CONNECTING_OUT, and "done" at the
 * end. */

#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

#include <gtk/gtk.h>

static FILE      *out;
static GtkWidget *window;
static int        step;
static int        waited;
static int        settled;
static gboolean   sign_seen_early;

static GTask     *held;
static int        mounts;
static int        held_tag;

static gboolean
fake (GFile *file)
{
	return file != NULL && g_file_has_uri_scheme (file, "smb");
}

/* What gvfs would do with a typed smb:// address. GIO's own local VFS reads
   it as a relative path. */
GFile *
g_file_parse_name (const char *parse_name)
{
	GFile *(*real) (const char *);

	if (g_str_has_prefix (parse_name, "smb://")) {
		return g_file_new_for_uri (parse_name);
	}

	real = dlsym (RTLD_NEXT, "g_file_parse_name");
	return real (parse_name);
}

/* Called from the listing threads too, hence nothing but the answer. */
GFileInfo *
g_file_query_info (GFile               *file,
		   const char          *attributes,
		   GFileQueryInfoFlags  flags,
		   GCancellable        *cancellable,
		   GError             **error)
{
	GFileInfo *(*real) (GFile *, const char *, GFileQueryInfoFlags, GCancellable *, GError **);

	if (fake (file)) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_MOUNTED, "Not mounted");
		return NULL;
	}

	real = dlsym (RTLD_NEXT, "g_file_query_info");
	return real (file, attributes, flags, cancellable, error);
}

void
g_file_mount_enclosing_volume (GFile               *location,
			       GMountMountFlags     flags,
			       GMountOperation     *mount_operation,
			       GCancellable        *cancellable,
			       GAsyncReadyCallback  callback,
			       gpointer             user_data)
{
	void (*real) (GFile *, GMountMountFlags, GMountOperation *, GCancellable *,
		      GAsyncReadyCallback, gpointer);

	if (!fake (location)) {
		real = dlsym (RTLD_NEXT, "g_file_mount_enclosing_volume");
		real (location, flags, mount_operation, cancellable, callback, user_data);
		return;
	}

	g_clear_object (&held);
	held = g_task_new (location, cancellable, callback, user_data);
	g_task_set_source_tag (held, &held_tag);
	mounts++;
}

gboolean
g_file_mount_enclosing_volume_finish (GFile         *location,
				      GAsyncResult  *result,
				      GError       **error)
{
	gboolean (*real) (GFile *, GAsyncResult *, GError **);

	if (g_task_is_valid (result, location) && g_task_get_source_tag (G_TASK (result)) == &held_tag) {
		return g_task_propagate_boolean (G_TASK (result), error);
	}

	real = dlsym (RTLD_NEXT, "g_file_mount_enclosing_volume_finish");
	return real (location, result, error);
}

static void
let_go (GQuark domain, int code, const char *message)
{
	GTask *task = g_steal_pointer (&held);

	g_task_return_new_error (task, domain, code, "%s", message);
	g_object_unref (task);
}

static void
report (gboolean passed, const char *what)
{
	fprintf (out, "%s %s\n", passed ? "ok" : "FAIL", what);
	fflush (out);
}

static gboolean
is_a (GtkWidget *widget, const char *type_name)
{
	GType type = g_type_from_name (type_name);

	return type != 0 && widget != NULL && g_type_is_a (G_OBJECT_TYPE (widget), type);
}

typedef struct {
	const char *type_name;
	gboolean    mapped;
	GtkWidget  *found;
} Find;

static void
find_cb (GtkWidget *widget, gpointer data)
{
	Find *find = data;

	if (find->found != NULL) {
		return;
	}
	if (is_a (widget, find->type_name) && (!find->mapped || gtk_widget_get_mapped (widget))) {
		find->found = widget;
	} else if (GTK_IS_CONTAINER (widget)) {
		gtk_container_forall (GTK_CONTAINER (widget), find_cb, data);
	}
}

static GtkWidget *
find_widget (const char *type_name, gboolean mapped)
{
	Find find = { type_name, mapped, NULL };

	if (window != NULL) {
		find_cb (window, &find);
	}
	return find.found;
}

typedef struct {
	const char *text;
	gboolean    spinning;
} Sign;

static void
spinner_cb (GtkWidget *widget, gpointer data)
{
	Sign *sign = data;
	gboolean active = FALSE;

	if (GTK_IS_SPINNER (widget) && gtk_widget_get_mapped (widget)) {
		g_object_get (widget, "active", &active, NULL);
		sign->spinning = sign->spinning || active;
	}
}

static void
sign_cb (GtkWidget *widget, gpointer data)
{
	Sign *sign = data;

	if (sign->text != NULL) {
		return;
	}
	if (GTK_IS_LABEL (widget) && gtk_widget_get_mapped (widget) &&
	    g_str_has_prefix (gtk_label_get_text (GTK_LABEL (widget)), "Connecting")) {
		sign->text = gtk_label_get_text (GTK_LABEL (widget));
		gtk_container_forall (GTK_CONTAINER (gtk_widget_get_parent (widget)), spinner_cb, sign);
	} else if (GTK_IS_CONTAINER (widget)) {
		gtk_container_forall (GTK_CONTAINER (widget), sign_cb, data);
	}
}

/* The mapped "Connecting..." label, if one shows, and whether a spinner
   turns beside it. */
static const char *
sign_text (gboolean *spinning)
{
	Sign sign = { NULL, FALSE };

	sign_cb (window, &sign);
	*spinning = sign.spinning;
	return sign.text;
}

static void
go_to (const char *address)
{
	GtkWidget *entry = find_widget ("NemoLocationEntry", FALSE);

	gtk_entry_set_text (GTK_ENTRY (entry), address);
	g_signal_emit_by_name (entry, "activate");
}

static void
press_escape (void)
{
	GdkDisplay *display = gtk_widget_get_display (window);
	GdkEvent *event = gdk_event_new (GDK_KEY_PRESS);
	GdkKeymapKey *keys = NULL;
	gint n_keys = 0;

	event->key.window = g_object_ref (gtk_widget_get_window (window));
	event->key.send_event = TRUE;
	event->key.time = GDK_CURRENT_TIME;
	event->key.keyval = GDK_KEY_Escape;
	if (gdk_keymap_get_entries_for_keyval (gdk_keymap_get_for_display (display),
					       GDK_KEY_Escape, &keys, &n_keys) && n_keys > 0) {
		event->key.hardware_keycode = keys[0].keycode;
	}
	g_free (keys);
	gdk_event_set_device (event, gdk_seat_get_keyboard (gdk_display_get_default_seat (display)));

	gtk_main_do_event (event);
	gdk_event_free (event);
}

/* Set, and not the watch. A named cursor reads back as a pixmap one. */
static gboolean
pointer_busy_not_blocked (void)
{
	GdkCursor *cursor = gdk_window_get_cursor (gtk_widget_get_window (window));

	return cursor != NULL && gdk_cursor_get_cursor_type (cursor) != GDK_WATCH;
}

static gboolean
pointer_plain (void)
{
	return gdk_window_get_cursor (gtk_widget_get_window (window)) == NULL;
}

static void
close_dialogs (void)
{
	GList *toplevels = gtk_window_list_toplevels ();
	GList *l;

	for (l = toplevels; l != NULL; l = l->next) {
		if (GTK_IS_MESSAGE_DIALOG (l->data)) {
			gtk_widget_destroy (l->data);
		}
	}
	g_list_free (toplevels);
}

static void
finish (void)
{
	fprintf (out, "done\n");
	fflush (out);
}

/* Each step waits for what the one before set going. Up to ten seconds each. */
static gboolean
tick (G_GNUC_UNUSED gpointer data)
{
	gboolean spinning = FALSE;
	const char *text = window != NULL ? sign_text (&spinning) : NULL;

	if (++waited > 100) {
		report (FALSE, step == 0 ? "the folder never showed" :
			step == 1 || step == 4 ? "the mount never started" :
			step == 3 ? "Escape stops the attempt" :
			"timed out");
		finish ();
		return G_SOURCE_REMOVE;
	}

	switch (step) {
	case 0: {
		GList *toplevels = gtk_window_list_toplevels ();
		GList *l;

		for (l = toplevels; l != NULL; l = l->next) {
			if (is_a (l->data, "NemoWindow") && gtk_widget_get_mapped (l->data)) {
				window = l->data;
			}
		}
		g_list_free (toplevels);
		if (window == NULL || find_widget ("NemoView", TRUE) == NULL ||
		    find_widget ("NemoPathBar", TRUE) == NULL) {
			return G_SOURCE_CONTINUE;
		}
		sign_seen_early = sign_seen_early || text != NULL;
		if (++settled < 10) {
			return G_SOURCE_CONTINUE;
		}
		report (!sign_seen_early, "a local folder shows no connecting sign");
		go_to ("smb://fakehost/share");
		break;
	}
	case 1:
		if (mounts < 1) {
			return G_SOURCE_CONTINUE;
		}
		break;
	case 2:
		/* A few ticks for the window to catch up. */
		if (waited < 3) {
			return G_SOURCE_CONTINUE;
		}
		report (g_strcmp0 (text, "Connecting to fakehost...") == 0 && spinning,
			"the path bar shows a turning spinner and \"Connecting to fakehost...\"");
		report (pointer_busy_not_blocked (), "the pointer shows work in the background, not a blocked app");
		report (gtk_widget_is_sensitive (window) && gtk_grab_get_current () == NULL,
			"the window takes input while it waits");
		press_escape ();
		break;
	case 3:
		if (held != NULL && !g_cancellable_is_cancelled (g_task_get_cancellable (held))) {
			return G_SOURCE_CONTINUE;
		}
		report (held != NULL, "Escape stops the attempt");
		if (held != NULL) {
			let_go (G_IO_ERROR, G_IO_ERROR_CANCELLED, "Cancelled");
		}
		break;
	case 4:
		if (waited < 3) {
			return G_SOURCE_CONTINUE;
		}
		report (text == NULL && find_widget ("NemoPathBar", TRUE) != NULL && pointer_plain (),
			"the sign and the pointer go back once stopped");
		go_to ("smb://someone@otherhost:445/share");
		break;
	case 5:
		if (mounts < 2) {
			return G_SOURCE_CONTINUE;
		}
		break;
	case 6:
		if (waited < 3) {
			return G_SOURCE_CONTINUE;
		}
		report (g_strcmp0 (text, "Connecting to otherhost...") == 0,
			"the path bar shows \"Connecting to otherhost...\"");
		let_go (G_IO_ERROR, G_IO_ERROR_TIMED_OUT, "Timed out");
		break;
	case 7:
		if (waited < 5) {
			return G_SOURCE_CONTINUE;
		}
		report (text == NULL, "the sign goes when the attempt fails");
		close_dialogs ();
		finish ();
		return G_SOURCE_REMOVE;
	}

	step++;
	waited = 0;
	return G_SOURCE_CONTINUE;
}

__attribute__((constructor)) static void
probe_start (void)
{
	const char *path = g_getenv ("NEMO_CONNECTING_OUT");

	if (path == NULL) {
		return;
	}
	out = fopen (path, "w");
	if (out == NULL) {
		return;
	}
	g_timeout_add (100, tick, NULL);
}
