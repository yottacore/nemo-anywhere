/* The keyboard shortcuts window shows the keys in use, and the shortcut list
 * takes a view's actions before the window has made that view. */

#include <config.h>

#include <string.h>
#include <gtk/gtk.h>

#include <libnemo-private/nemo-shortcuts.h>
#include "../src/nemo-shortcuts-window.h"
#include "test-check.h"

#define SHELL "<Actions>/ShellActions/"
#define DIR_VIEW "<Actions>/DirViewActions/"

/* A callback, since an entry with none is a submenu, which isn't listed. */
static void
activate (G_GNUC_UNUSED GtkAction *action, G_GNUC_UNUSED gpointer data)
{
}

static const GtkActionEntry shell_entries[] = {
	{ "New Window", NULL, "New _window", "<Primary>N", NULL, G_CALLBACK (activate) },
	{ "Back", NULL, "_Back", "<alt>Left", NULL, G_CALLBACK (activate) },
};

static const GtkActionEntry dir_view_entries[] = {
	{ "Trash", NULL, "Mo_ve to Trash", NULL, NULL, G_CALLBACK (activate) },
	{ "Pin File", NULL, "P_in", "<Primary><shift>D", NULL, G_CALLBACK (activate) },
	{ "Unpin File", NULL, "Unp_in", "<Primary><shift>D", NULL, G_CALLBACK (activate) },
	{ "Invert Selection", NULL, "_Invert selection", "<Primary><shift>I", NULL, G_CALLBACK (activate) },
	{ "PropertiesAccel", NULL, "PropertiesAccel", "<Primary>I", NULL, G_CALLBACK (activate) },
	{ "Properties", NULL, "_Properties", "<alt>Return", NULL, G_CALLBACK (activate) },
};

static const GtkActionEntry later_entries[] = {
	{ "Later", NULL, "_Later", "F7", NULL, G_CALLBACK (activate) },
};

static int later_calls;

static void
note_later (void)
{
	GtkActionGroup *group = gtk_action_group_new ("LaterActions");

	later_calls++;
	gtk_action_group_add_actions (group, later_entries, G_N_ELEMENTS (later_entries), NULL);
	nemo_shortcuts_note_actions (group, later_entries, G_N_ELEMENTS (later_entries));
	g_object_unref (group);
}

/* Each key in its canonical name, so "<Primary>N" and "<Primary>n" match. */
static char *
canonical (const char *text)
{
	g_auto (GStrv) tokens = g_strsplit (text, " ", -1);
	GString *out = g_string_new (NULL);
	guint i;

	for (i = 0; tokens[i] != NULL; i++) {
		guint key;
		GdkModifierType mods;

		if (*tokens[i] == '\0') {
			continue;
		}
		gtk_accelerator_parse (tokens[i], &key, &mods);
		if (out->len > 0) {
			g_string_append_c (out, ' ');
		}
		if (key != 0) {
			g_autofree char *name = gtk_accelerator_name (key, mods);

			g_string_append (out, name);
		} else {
			g_string_append (out, tokens[i]);
		}
	}

	return g_string_free (out, FALSE);
}

static gboolean
keys_are (const char *got, const char *want)
{
	g_autofree char *a = canonical (got);
	g_autofree char *b = canonical (want);

	if (strcmp (a, b) != 0) {
		g_printerr ("keys: got \"%s\", want \"%s\"\n", a, b);
		return FALSE;
	}
	return TRUE;
}

static gboolean
text_is (const char *shown, const char *path_a, const char *path_b, const char *want)
{
	const char *paths[] = { path_a, path_b, NULL };
	g_autofree char *got = nemo_shortcuts_keys_text (shown, paths);

	return keys_are (got, want);
}

static gboolean
row_is (GtkBuilder *builder, const char *id, const char *want)
{
	GObject *row = gtk_builder_get_object (builder, id);
	g_autofree char *got = NULL;

	if (row == NULL) {
		g_printerr ("no row %s\n", id);
		return FALSE;
	}
	if (*want == '\0') {
		return !gtk_widget_get_visible (GTK_WIDGET (row));
	}
	g_object_get (row, "accelerator", &got, NULL);
	return gtk_widget_get_visible (GTK_WIDGET (row)) && keys_are (got, want);
}

static void
set (const char *path, const char *accel)
{
	guint key = 0;
	GdkModifierType mods = 0;

	if (accel != NULL) {
		gtk_accelerator_parse (accel, &key, &mods);
	}
	check (gtk_accel_map_change_entry (path, key, mods, TRUE));
}

int
main (int argc, char **argv)
{
	GtkActionGroup *shell, *dir_view;
	GtkBuilder *builder;
	GtkWidget *window;
	const char *const *ids;
	GList *list, *l;
	gboolean listed;
	guint i;

	/* Key names need the display's keymap. */
	if (!gtk_init_check (&argc, &argv)) {
		g_print ("SKIP: no display\n");
		return 77;
	}

	/* A group the window hasn't made yet is listed, and noted only once. */
	nemo_shortcuts_add_source (note_later);
	nemo_shortcuts_add_source (note_later);
	check (later_calls == 0);
	list = nemo_shortcuts_list ();
	listed = FALSE;
	for (l = list; l != NULL; l = l->next) {
		listed |= strcmp (((const NemoShortcut *) l->data)->path, "<Actions>/LaterActions/Later") == 0;
	}
	g_list_free (list);
	check (listed);
	check (later_calls == 1);
	check (nemo_shortcuts_lookup ("<Actions>/LaterActions/Later")->key == GDK_KEY_F7);
	g_list_free (nemo_shortcuts_list ());
	check (later_calls == 1);

	shell = gtk_action_group_new ("ShellActions");
	gtk_action_group_add_actions (shell, shell_entries, G_N_ELEMENTS (shell_entries), NULL);
	nemo_shortcuts_note_actions (shell, shell_entries, G_N_ELEMENTS (shell_entries));
	dir_view = gtk_action_group_new ("DirViewActions");
	gtk_action_group_add_actions (dir_view, dir_view_entries, G_N_ELEMENTS (dir_view_entries), NULL);
	nemo_shortcuts_note_actions (dir_view, dir_view_entries, G_N_ELEMENTS (dir_view_entries));

	/* A row's own key gives way to the one in use, and other keys stay. */
	check (text_is ("<Primary>N", SHELL "New Window", NULL, "<Primary>N"));
	set (SHELL "New Window", "<Primary>K");
	check (text_is ("<Primary>N", SHELL "New Window", NULL, "<Primary>K"));
	set (SHELL "New Window", NULL);
	check (text_is ("<Primary>N", SHELL "New Window", NULL, ""));
	check (text_is ("BackSpace <alt>Left", SHELL "Back", NULL, "BackSpace <alt>Left"));
	set (SHELL "Back", "<alt>B");
	check (text_is ("BackSpace <alt>Left", SHELL "Back", NULL, "BackSpace <alt>B"));
	set (SHELL "Back", "BackSpace");
	check (text_is ("BackSpace <alt>Left", SHELL "Back", NULL, "BackSpace"));
	check (text_is ("<alt>0...8", SHELL "Back", NULL, "<alt>0...8"));
	check (text_is ("<Primary>N", SHELL "Not Noted", NULL, "<Primary>N"));

	/* One with no key to start with shows the key it's given. */
	check (text_is ("Delete", DIR_VIEW "Trash", NULL, "Delete"));
	set (DIR_VIEW "Trash", "<Primary>T");
	check (text_is ("Delete", DIR_VIEW "Trash", NULL, "Delete <Primary>T"));
	set (DIR_VIEW "Trash", NULL);

	/* 2 that start with one key each keep their own. */
	set (DIR_VIEW "Pin File", "<Primary>P");
	check (text_is ("<Primary><shift>D", DIR_VIEW "Pin File", DIR_VIEW "Unpin File", "<Primary>P <Primary><shift>D"));
	set (DIR_VIEW "Pin File", "<Primary><shift>D");
	check (text_is ("<Primary><shift>D", DIR_VIEW "Pin File", DIR_VIEW "Unpin File", "<Primary><shift>D"));

	/* The window, from the same file the app builds it from. */
	builder = gtk_builder_new_from_file (NEMO_SHORTCUTS_UI);
	ids = nemo_shortcuts_window_row_ids ();
	for (i = 0; ids[i] != NULL; i++) {
		if (gtk_builder_get_object (builder, ids[i]) == NULL) {
			g_printerr ("nemo-shortcuts.ui has no row %s\n", ids[i]);
			check (FALSE);
		}
	}
	window = nemo_shortcuts_window_take (builder);
	g_assert_nonnull (window);
	gtk_widget_show_all (window);

	set (SHELL "New Window", "<Primary>N");
	nemo_shortcuts_window_show_keys (window, TRUE);
	check (row_is (builder, "shortcut_new_window", "<Primary>N"));
	check (row_is (builder, "shortcut_back", "BackSpace"));
	check (row_is (builder, "shortcut_invert_selection", "<Primary>I <shift><Primary>I"));
	check (row_is (builder, "shortcut_properties", "<alt>Return"));

	/* It follows a change while it's open, and a row with no key goes. */
	set (SHELL "New Window", "<Primary>J");
	check (row_is (builder, "shortcut_new_window", "<Primary>J"));
	set (SHELL "New Window", NULL);
	check (row_is (builder, "shortcut_new_window", ""));
	set (SHELL "New Window", "<Primary>N");
	check (row_is (builder, "shortcut_new_window", "<Primary>N"));

	/* Ctrl+I goes to Properties under the other models, wherever it's moved. */
	set (DIR_VIEW "PropertiesAccel", "<Primary>J");
	check (row_is (builder, "shortcut_invert_selection", "<Primary>J <shift><Primary>I"));
	check (row_is (builder, "shortcut_properties", "<alt>Return"));
	nemo_shortcuts_window_show_keys (window, FALSE);
	check (row_is (builder, "shortcut_invert_selection", "<shift><Primary>I"));
	check (row_is (builder, "shortcut_properties", "<Primary>J <alt>Return"));
	set (DIR_VIEW "PropertiesAccel", "<Primary>I");

	/* A row left out stays out, keys or not. */
	nemo_shortcuts_window_set_unused (window, "shortcut_paste_key", TRUE);
	nemo_shortcuts_window_show_keys (window, FALSE);
	check (row_is (builder, "shortcut_paste_key", ""));
	nemo_shortcuts_window_set_unused (window, "shortcut_paste_key", FALSE);
	nemo_shortcuts_window_show_keys (window, FALSE);
	check (row_is (builder, "shortcut_paste_key", "<Primary><shift>V"));

	gtk_widget_destroy (window);
	g_object_unref (builder);
	g_object_unref (shell);
	g_object_unref (dir_view);

	return failures == 0 ? 0 : 1;
}
