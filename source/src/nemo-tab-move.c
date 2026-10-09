/* nemo-tab-move.c - moving a tab to another window, which is usually another process.
 *
 * Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

/* A page cannot move into another process, so what moves is its folder, its
 * view and its selection, and the tab here is closed once the other side has
 * taken them. Windows in this process get the same treatment rather than a
 * reparented page, so there is one way it can go wrong, not two. */

#include <config.h>
#include "nemo-tab-move.h"

#include <glib/gi18n.h>

#include <eel/eel-vfs-extensions.h>
#include <libnemo-private/nemo-file.h>

#include "nemo-application.h"
#include "nemo-main-application.h"
#include "nemo-new-process.h"
#include "nemo-view.h"
#include "nemo-window-at-point.h"
#include "nemo-window-pane.h"
#include "nemo-window-private.h"

#define TABS_INTERFACE "org.NemoAnywhere.Tabs"

/* Long enough for a copy that is busy, short enough that one that has hung
   does not hold the menu up for long. */
#define LIST_TIMEOUT_MS 500
#define TAKE_TIMEOUT_MS 3000

static const char tabs_xml[] =
	"<node>"
	"  <interface name='" TABS_INTERFACE "'>"
	"    <method name='ListWindows'>"
	"      <arg type='a(uts)' name='windows' direction='out'/>"
	"    </method>"
	"    <method name='TakeTab'>"
	"      <arg type='u' name='window' direction='in'/>"
	"      <arg type='s' name='uri' direction='in'/>"
	"      <arg type='s' name='view' direction='in'/>"
	"      <arg type='as' name='selected' direction='in'/>"
	"      <arg type='u' name='time' direction='in'/>"
	"    </method>"
	"  </interface>"
	"</node>";

/* Somewhere a tab can go. bus_name names the other copy, as
   nemo_instances_list_others gives it, and is NULL for a window in this
   process. */
typedef struct {
	char *bus_name;
	guint32 id;
	guint64 handle;
	char *title;
} Target;

void
nemo_tab_state_free (NemoTabState *state)
{
	if (state == NULL) {
		return;
	}
	g_free (state->uri);
	g_free (state->view_id);
	g_strfreev (state->selected);
	g_free (state);
}

static gboolean
slot_can_move (NemoWindowSlot *slot)
{
	char *uri = nemo_window_slot_get_location_uri (slot);
	gboolean movable;

	/* A search lives in this process only; elsewhere it is an empty folder. */
	movable = uri != NULL && !eel_uri_is_search (uri);
	g_free (uri);

	return movable;
}

gboolean
nemo_tab_move_is_only_tab (NemoWindowSlot *slot)
{
	return g_list_length (slot->pane->slots) == 1 &&
	       !nemo_window_split_view_showing (nemo_window_slot_get_window (slot));
}

static NemoTabState *
tab_state_from_slot (NemoWindowSlot *slot)
{
	NemoTabState *state;
	GPtrArray *selected;
	GList *files, *l;
	const char *view_id;

	state = g_new0 (NemoTabState, 1);
	state->uri = nemo_window_slot_get_location_uri (slot);

	view_id = nemo_window_slot_get_content_view_id (slot);
	state->view_id = g_strdup (view_id);

	selected = g_ptr_array_new ();
	if (slot->content_view != NULL) {
		files = nemo_view_get_selection (slot->content_view);
		for (l = files; l != NULL; l = l->next) {
			g_ptr_array_add (selected, nemo_file_get_uri (NEMO_FILE (l->data)));
		}
		nemo_file_list_free (files);
	}
	g_ptr_array_add (selected, NULL);
	state->selected = (char **) g_ptr_array_free (selected, FALSE);

	return state;
}

void
nemo_window_take_tab (NemoWindow         *window,
                      const NemoTabState *state,
                      guint32             event_time)
{
	NemoWindowSlot *slot = nemo_window_get_active_slot (window);
	GList *selection = NULL;
	GFile *location;
	int i, page;

	if (slot == NULL || slot->location != NULL || slot->pending_location != NULL) {
		slot = nemo_window_pane_open_slot (nemo_window_get_active_pane (window),
		                                   NEMO_WINDOW_OPEN_SLOT_APPEND);
	}

	g_free (slot->pending_view_id);
	slot->pending_view_id = state->view_id != NULL && state->view_id[0] != '\0' ?
	                        g_strdup (state->view_id) : NULL;

	for (i = 0; state->selected != NULL && state->selected[i] != NULL; i++) {
		selection = g_list_prepend (selection, nemo_file_get_by_uri (state->selected[i]));
	}
	selection = g_list_reverse (selection);

	location = g_file_new_for_uri (state->uri);
	nemo_window_slot_open_location_full (slot, location, 0, selection, NULL, NULL);
	g_object_unref (location);
	nemo_file_list_free (selection);

	page = gtk_notebook_page_num (GTK_NOTEBOOK (slot->pane->notebook), GTK_WIDGET (slot));
	gtk_notebook_set_current_page (GTK_NOTEBOOK (slot->pane->notebook), page);

	/* A window still being built is shown by whoever built it. */
	if (!gtk_widget_get_visible (GTK_WIDGET (window))) {
		return;
	}
	if (event_time != 0) {
		gtk_window_present_with_time (GTK_WINDOW (window), event_time);
	} else {
		gtk_window_present (GTK_WINDOW (window));
	}
}

static NemoWindow *
window_by_id (guint32 id)
{
	GList *l;

	for (l = gtk_application_get_windows (GTK_APPLICATION (g_application_get_default ()));
	     l != NULL; l = l->next) {
		if (NEMO_IS_WINDOW (l->data) &&
		    gtk_application_window_get_id (GTK_APPLICATION_WINDOW (l->data)) == id) {
			return NEMO_WINDOW (l->data);
		}
	}

	return NULL;
}

/* Only windows someone can see; one that is still being built or is on its
   way out has no business in the list. */
static gboolean
window_listed (gpointer window)
{
	return NEMO_IS_WINDOW (window) && gtk_widget_get_visible (GTK_WIDGET (window)) &&
	       gtk_application_window_get_id (GTK_APPLICATION_WINDOW (window)) != 0;
}

static gboolean
uri_has_scheme (const char *uri)
{
	char *scheme = g_uri_parse_scheme (uri);
	gboolean has = scheme != NULL;

	g_free (scheme);

	return has;
}

static GVariant *
tabs_method_call (const char  *method_name,
                  GVariant    *parameters,
                  G_GNUC_UNUSED gpointer     user_data,
                  GError     **error)
{
	if (g_strcmp0 (method_name, "ListWindows") == 0) {
		GVariantBuilder windows;
		GList *l;

		g_variant_builder_init (&windows, G_VARIANT_TYPE ("a(uts)"));
		for (l = gtk_application_get_windows (GTK_APPLICATION (g_application_get_default ()));
		     l != NULL; l = l->next) {
			const char *title;

			if (!window_listed (l->data)) {
				continue;
			}
			title = gtk_window_get_title (GTK_WINDOW (l->data));
			g_variant_builder_add (&windows, "(uts)",
			                       gtk_application_window_get_id (GTK_APPLICATION_WINDOW (l->data)),
			                       nemo_window_native_handle (GTK_WINDOW (l->data)),
			                       title != NULL ? title : "");
		}
		return g_variant_new ("(a(uts))", &windows);
	}

	if (g_strcmp0 (method_name, "TakeTab") == 0) {
		NemoTabState state = { 0 };
		NemoWindow *window;
		guint32 id, event_time;

		g_variant_get (parameters, "(u&s&s^asu)", &id, &state.uri, &state.view_id,
		               &state.selected, &event_time);

		window = window_by_id (id);
		if (window == NULL || !window_listed (window)) {
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "No window %u here", id);
		} else if (!uri_has_scheme (state.uri)) {
			g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Not a URI");
		} else {
			nemo_window_take_tab (window, &state, event_time);
		}
		g_strfreev (state.selected);
		return NULL;
	}

	g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD, "No method %s", method_name);
	return NULL;
}

void
nemo_tab_move_serve (void)
{
	nemo_instances_serve (tabs_xml, tabs_method_call, NULL);
}

static void
target_free (gpointer data)
{
	Target *target = data;

	g_free (target->bus_name);
	g_free (target->title);
	g_free (target);
}

/* Every window a tab from here could go to, this process's other windows
   first. A copy that does not answer in time is left out. */
static GPtrArray *
list_targets (NemoWindow *here)
{
	GPtrArray *targets = g_ptr_array_new_with_free_func (target_free);
	GApplication *app = g_application_get_default ();
	GStrv others;
	GList *l;
	int i;

	for (l = gtk_application_get_windows (GTK_APPLICATION (app)); l != NULL; l = l->next) {
		Target *target;

		if (l->data == here || !window_listed (l->data)) {
			continue;
		}
		target = g_new0 (Target, 1);
		target->id = gtk_application_window_get_id (GTK_APPLICATION_WINDOW (l->data));
		target->handle = nemo_window_native_handle (GTK_WINDOW (l->data));
		target->title = g_strdup (gtk_window_get_title (GTK_WINDOW (l->data)));
		g_ptr_array_add (targets, target);
	}

	others = nemo_main_application_other_instances ();
	for (i = 0; others != NULL && others[i] != NULL; i++) {
		GVariant *reply, *windows;
		GVariantIter iter;
		guint32 id;
		guint64 handle;
		const char *title;

		reply = nemo_instances_call (others[i], TABS_INTERFACE, "ListWindows", NULL,
		                             G_VARIANT_TYPE ("(a(uts))"), LIST_TIMEOUT_MS, NULL);
		if (reply == NULL) {
			continue;
		}
		windows = g_variant_get_child_value (reply, 0);
		g_variant_iter_init (&iter, windows);
		while (g_variant_iter_next (&iter, "(ut&s)", &id, &handle, &title)) {
			Target *target = g_new0 (Target, 1);

			target->bus_name = g_strdup (others[i]);
			target->id = id;
			target->handle = handle;
			target->title = g_strdup (title);
			g_ptr_array_add (targets, target);
		}
		g_variant_unref (windows);
		g_variant_unref (reply);
	}
	g_strfreev (others);

	return targets;
}

static gboolean
close_slot_idle (gpointer data)
{
	NemoWindowSlot *slot = data;

	if (slot->pane != NULL && g_list_find (slot->pane->slots, slot) != NULL) {
		nemo_window_pane_close_slot (slot->pane, slot);
	}
	g_object_unref (slot);

	return G_SOURCE_REMOVE;
}

/* Closed on an idle, since a drag may still be finishing with the page. */
static void
close_slot_later (NemoWindowSlot *slot)
{
	g_idle_add (close_slot_idle, g_object_ref (slot));
}

static gboolean
move_to_target (NemoWindowSlot *slot,
                Target         *target,
                guint32         event_time)
{
	NemoTabState *state = tab_state_from_slot (slot);
	gboolean moved = TRUE;

	if (target->bus_name == NULL) {
		/* Looked up again, since the window may have closed while the menu
		   was up. */
		NemoWindow *window = window_by_id (target->id);

		if (window != NULL && window_listed (window)) {
			nemo_window_take_tab (window, state, event_time);
		} else {
			moved = FALSE;
		}
	} else {
		GVariant *reply;
		GError *error = NULL;

		nemo_window_allow_to_raise (target->handle);
		reply = nemo_instances_call (target->bus_name, TABS_INTERFACE, "TakeTab",
		                             g_variant_new ("(uss^asu)", target->id, state->uri,
		                                            state->view_id != NULL ? state->view_id : "",
		                                            state->selected, event_time),
		                             NULL, TAKE_TIMEOUT_MS, &error);
		if (reply == NULL) {
			/* The tab stays where it is, so nothing is lost. */
			g_warning ("Could not move the tab to %s: %s", target->title, error->message);
			g_clear_error (&error);
			moved = FALSE;
		} else {
			g_variant_unref (reply);
		}
	}

	if (moved) {
		close_slot_later (slot);
	}
	nemo_tab_state_free (state);

	return moved;
}

static void
move_to_new_window (NemoWindowSlot *slot,
                    guint32         event_time)
{
	NemoTabState *state = tab_state_from_slot (slot);
	NemoWindow *window;

	if (nemo_application_window_per_process ()) {
		GFile *location = g_file_new_for_uri (state->uri);
		GError *error = NULL;
		gboolean spawned;

		spawned = nemo_new_process_spawn_tab (location, state->view_id, state->selected, &error);
		g_object_unref (location);
		if (spawned) {
			close_slot_later (slot);
			nemo_tab_state_free (state);
			return;
		}
		g_warning ("Could not start a new process for the window, opening it here: %s",
		           error->message);
		g_error_free (error);
	}

	window = nemo_application_create_window (NEMO_APPLICATION (g_application_get_default ()),
	                                         gtk_widget_get_screen (GTK_WIDGET (slot)));
	nemo_window_take_tab (window, state, event_time);
	gtk_window_present (GTK_WINDOW (window));
	close_slot_later (slot);
	nemo_tab_state_free (state);
}

static void
menu_move_to_window_cb (GtkMenuItem *item,
                        G_GNUC_UNUSED gpointer     user_data)
{
	NemoWindowSlot *slot = g_object_get_data (G_OBJECT (item), "slot");
	Target *target = g_object_get_data (G_OBJECT (item), "target");

	move_to_target (slot, target, gtk_get_current_event_time ());
}

static void
menu_move_to_new_window_cb (GtkMenuItem *item,
                            G_GNUC_UNUSED gpointer     user_data)
{
	NemoWindowSlot *slot = g_object_get_data (G_OBJECT (item), "slot");

	move_to_new_window (slot, gtk_get_current_event_time ());
}

/* Returns: (transfer floating) */
GtkWidget *
nemo_tab_move_menu_item_new (NemoWindowSlot *slot)
{
	NemoWindow *window = nemo_window_slot_get_window (slot);
	GtkWidget *item, *submenu, *child;
	GPtrArray *targets;
	guint i;

	item = gtk_menu_item_new_with_mnemonic (_("Move tab _to"));
	if (!slot_can_move (slot)) {
		gtk_widget_set_sensitive (item, FALSE);
		return item;
	}

	submenu = gtk_menu_new ();
	targets = list_targets (window);
	for (i = 0; i < targets->len; i++) {
		Target *target = g_ptr_array_index (targets, i);

		/* Titles are folder names, which can hold an underscore. */
		child = gtk_menu_item_new_with_label (target->title);
		g_object_set_data_full (G_OBJECT (child), "slot", g_object_ref (slot), g_object_unref);
		g_object_set_data_full (G_OBJECT (child), "target", target, target_free);
		g_signal_connect (child, "activate", G_CALLBACK (menu_move_to_window_cb), NULL);
		gtk_menu_shell_append (GTK_MENU_SHELL (submenu), child);
	}
	if (targets->len > 0) {
		gtk_menu_shell_append (GTK_MENU_SHELL (submenu), gtk_separator_menu_item_new ());
	}
	/* The items own the targets now. */
	/* cppcheck-suppress nullPointer ; a NULL free func is allowed, and is the point */
	g_ptr_array_set_free_func (targets, NULL);
	g_ptr_array_unref (targets);

	child = gtk_menu_item_new_with_mnemonic (_("_New window"));
	g_object_set_data_full (G_OBJECT (child), "slot", g_object_ref (slot), g_object_unref);
	g_signal_connect (child, "activate", G_CALLBACK (menu_move_to_new_window_cb), NULL);
	gtk_menu_shell_append (GTK_MENU_SHELL (submenu), child);

	/* The only tab going to a window of its own would just be this window
	   again, somewhere else. */
	gtk_widget_set_sensitive (child, !nemo_tab_move_is_only_tab (slot));

	gtk_menu_item_set_submenu (GTK_MENU_ITEM (item), submenu);

	return item;
}

void
nemo_tab_move_tear_off (NemoWindowSlot *slot)
{
	GPtrArray *targets;
	guint64 *handles;
	gboolean moved;
	int found;
	guint i;

	if (!slot_can_move (slot)) {
		return;
	}

	targets = list_targets (nemo_window_slot_get_window (slot));
	handles = g_new0 (guint64, targets->len + 1);
	for (i = 0; i < targets->len; i++) {
		handles[i] = ((Target *) g_ptr_array_index (targets, i))->handle;
	}

	found = nemo_window_at_pointer (gtk_widget_get_display (GTK_WIDGET (slot)),
	                                handles, (int) targets->len);
	moved = found >= 0 &&
	        move_to_target (slot, g_ptr_array_index (targets, found), gtk_get_current_event_time ());
	if (!moved && !nemo_tab_move_is_only_tab (slot)) {
		move_to_new_window (slot, gtk_get_current_event_time ());
	}

	g_free (handles);
	g_ptr_array_unref (targets);
}
