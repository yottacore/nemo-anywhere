/* The location entry's context menu is the toolkit's own, reached through
 * nemo's key handler. A Windows report says the menu key does nothing while
 * text is selected, so every case here goes in through the key where the key
 * can be driven at all.
 *
 * The menu is only built once the clipboard has answered what it holds, so a
 * clipboard that never answers means no menu and nothing on screen to say why.
 */

#include <config.h>

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

#include <libnemo-private/nemo-entry.h>
#include "test-check.h"
#include "test-scratch.h"

static struct {
	GtkWidget *menu;
	GMainLoop *loop;
} popup;

static gboolean use_key = TRUE;

static void
popup_seen (GtkEntry *entry, GtkWidget *menu, gpointer data)
{
	popup.menu = menu;
	g_main_loop_quit (popup.loop);
}

static gboolean
give_up (gpointer data)
{
	g_main_loop_quit (popup.loop);

	return TRUE;
}

static GtkWidget *
wait_for_menu (void)
{
	guint timeout;

	/* The clipboard can answer before the loop is even entered, when we are
	   the ones holding it. */
	if (popup.menu == NULL) {
		timeout = g_timeout_add_seconds (5, give_up, NULL);
		g_main_loop_run (popup.loop);
		g_source_remove (timeout);
	}

	while (gtk_events_pending ()) {
		gtk_main_iteration ();
	}

	return popup.menu;
}

static void
dismiss_menu (GtkWidget *menu)
{
	if (menu != NULL && GTK_IS_MENU (menu)) {
		gtk_menu_popdown (GTK_MENU (menu));
	}

	while (gtk_events_pending ()) {
		gtk_main_iteration ();
	}
}

/* Wine loads no keyboard layout, so the win32 keymap knows no menu key and the
   binding cannot match. There is no working around it: the toolkit translates
   the keycode before it looks a binding up. */
static gboolean
menu_key_is_on_the_keymap (GtkWidget *window)
{
	GdkKeymapKey *keys = NULL;
	int n_keys = 0;
	gboolean found;

	found = gdk_keymap_get_entries_for_keyval (gdk_keymap_get_for_display (gtk_widget_get_display (window)),
						   GDK_KEY_Menu, &keys, &n_keys);
	g_free (keys);

	return found && n_keys > 0;
}

static void
send_menu_key (GtkWidget *window)
{
	GdkKeymapKey *keys = NULL;
	GdkDisplay *display;
	GdkDevice *keyboard;
	GdkWindow *gdk_window;
	GdkEvent *event;
	int n_keys = 0;

	display = gtk_widget_get_display (window);
	gdk_window = gtk_widget_get_window (window);
	keyboard = gdk_seat_get_keyboard (gdk_display_get_default_seat (display));

	if (gdk_window == NULL || keyboard == NULL) {
		g_printerr ("FAIL no window or keyboard to send the key through\n");
		failures++;
		return;
	}

	if (!gdk_keymap_get_entries_for_keyval (gdk_keymap_get_for_display (display),
						GDK_KEY_Menu, &keys, &n_keys) ||
	    n_keys < 1) {
		g_printerr ("FAIL the keymap lost its menu key mid-run\n");
		failures++;
		g_free (keys);
		return;
	}

	event = gdk_event_new (GDK_KEY_PRESS);
	event->key.window = g_object_ref (gdk_window);
	event->key.send_event = TRUE;
	event->key.time = GDK_CURRENT_TIME;
	event->key.keyval = GDK_KEY_Menu;
	event->key.hardware_keycode = keys[0].keycode;
	event->key.group = keys[0].group;
	event->key.string = g_strdup ("");
	gdk_event_set_device (event, keyboard);

	gtk_main_do_event (event);

	gdk_event_free (event);
	g_free (keys);
}

/* Cut, copy and delete are insensitive with nothing selected, so counting what
   is live is a version- and language-proof way to ask whether the menu saw the
   selection. Which item sits where is neither. */
static guint
live_items (GtkWidget *menu)
{
	GList *items, *item;
	guint live = 0;

	items = gtk_container_get_children (GTK_CONTAINER (menu));
	for (item = items; item != NULL; item = item->next) {
		if (gtk_widget_is_sensitive (GTK_WIDGET (item->data))) {
			live++;
		}
	}
	g_list_free (items);

	return live;
}

static guint
ask_and_check (GtkWidget *entry, GtkWidget *window, const char *what)
{
	GtkWidget *menu;
	GList *items;
	guint live;

	popup.menu = NULL;
	if (use_key) {
		send_menu_key (window);
	} else {
		gboolean handled = FALSE;

		g_signal_emit_by_name (entry, "popup-menu", &handled);
	}

	menu = wait_for_menu ();
	if (menu == NULL) {
		g_printerr ("FAIL no context menu %s\n", what);
		failures++;
		return 0;
	}

	check (GTK_IS_MENU (menu));
	check (gtk_widget_get_mapped (menu));

	/* Cut, copy and paste at the very least. An empty menu is a menu that
	   was never filled in, which reads on screen as nothing happening. */
	items = gtk_container_get_children (GTK_CONTAINER (menu));
	if (g_list_length (items) < 3) {
		g_printerr ("FAIL context menu %s has %u items\n", what,
			    g_list_length (items));
		failures++;
	}
	g_list_free (items);

	live = live_items (menu);
	dismiss_menu (menu);

	return live;
}

int
main (int argc, char *argv[])
{
	GtkWidget *window, *entry;
	guint bare, word, all;

	/* A menu open in another test holds the pointer and keyboard. */
	test_own_display (argc, argv, NULL);

	if (!gtk_init_check (&argc, &argv)) {
		g_print ("no display; skipping\n");
		return 77;
	}

	/* Over ssh on Windows a menu has nowhere to map. */
	if (gdk_display_get_n_monitors (gdk_display_get_default ()) == 0) {
		g_print ("SKIP: no monitor\n");
		return 77;
	}

	window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	gtk_window_set_default_size (GTK_WINDOW (window), 500, 60);

	entry = nemo_entry_new ();
	gtk_container_add (GTK_CONTAINER (window), entry);
	gtk_widget_show_all (window);
	gtk_widget_grab_focus (entry);

	use_key = menu_key_is_on_the_keymap (window);
	popup.loop = g_main_loop_new (NULL, FALSE);
	g_signal_connect (entry, "populate-popup", G_CALLBACK (popup_seen), NULL);

	gtk_entry_set_text (GTK_ENTRY (entry), "/usr/share/icons");

	while (gtk_events_pending ()) {
		gtk_main_iteration ();
	}

	/* Caret only, to tell a menu that never comes up at all from one that
	   only stays away when something is selected. */
	gtk_editable_select_region (GTK_EDITABLE (entry), 4, 4);
	check (!gtk_editable_get_selection_bounds (GTK_EDITABLE (entry), NULL, NULL));
	bare = ask_and_check (entry, window, "with the caret in the entry");

	/* One word, the way a double click leaves it. */
	gtk_editable_select_region (GTK_EDITABLE (entry), 5, 10);
	check (gtk_editable_get_selection_bounds (GTK_EDITABLE (entry), NULL, NULL));
	word = ask_and_check (entry, window, "over a selected word");

	/* The reported case: everything selected, which is how the location bar
	   hands the entry over. */
	nemo_entry_select_all (NEMO_ENTRY (entry));
	check (gtk_editable_get_selection_bounds (GTK_EDITABLE (entry), NULL, NULL));
	all = ask_and_check (entry, window, "over the whole path");

	/* A menu that came up without noticing the selection is the same bug
	   wearing a different face. */
	check (word > bare);
	check (all > bare);

	gtk_widget_destroy (window);
	g_main_loop_unref (popup.loop);

	if (failures > 0) {
		return 1;
	}

	/* Half a check is not a pass, so say so where meson can see it. */
	if (!use_key) {
		g_print ("no menu key on this keymap; the key half did not run\n");
		return 77;
	}

	g_print ("entry context menu: all checks passed\n");

	return 0;
}
