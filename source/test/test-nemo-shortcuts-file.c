/* Shortcuts shared between windows. Each window is a process of its own and
 * saves its whole map, so a change another window writes to the file is
 * taken here, and a save here keeps it. A change made here and not saved yet
 * survives both. The other window is stood in for by writing the file the
 * way GTK does. */

#include <config.h>

#include <string.h>
#include <gtk/gtk.h>

#include <libnemo-private/nemo-shortcuts.h>
#include "test-scratch.h"
#include "test-check.h"

#define PATH_A "<Actions>/TestActions/Alpha"
#define PATH_B "<Actions>/TestActions/Bravo"
#define PATH_C "<Actions>/TestActions/Charlie"

static char *file;
static int   unsaved_changes;	/* "changed" signals that would queue a save */

static guint
key_of (const char *path)
{
	GtkAccelKey key;

	return gtk_accel_map_lookup_entry (path, &key) ? key.accel_key : 0;
}

static void
other_window_writes (guint a, guint b, guint c)
{
	GString *text = g_string_new ("; nemo GtkAccelMap rc-file         -*- scheme -*-\n;\n");
	const char *paths[] = { PATH_A, PATH_B, PATH_C };
	guint keys[] = { a, b, c };
	guint i;

	/* 0 is a line left at its default, which GTK writes commented. */
	for (i = 0; i < G_N_ELEMENTS (paths); i++) {
		if (keys[i] != 0) {
			char *name = gtk_accelerator_name (keys[i], 0);

			g_string_append_printf (text, "(gtk_accel_path \"%s\" \"%s\")\n", paths[i], name);
			g_free (name);
		} else {
			g_string_append_printf (text, "; (gtk_accel_path \"%s\" \"F1\")\n", paths[i]);
		}
	}
	check (g_file_set_contents (file, text->str, -1, NULL));
	g_string_free (text, TRUE);
}

static gboolean
wait_for (const char *path, guint key)
{
	gint64 until = g_get_monotonic_time () + (gint64) (5 * G_USEC_PER_SEC * test_slowness ());

	while (g_get_monotonic_time () < until) {
		while (g_main_context_iteration (NULL, FALSE)) {
		}
		if (key_of (path) == key) {
			return TRUE;
		}
		g_usleep (20000);
	}

	return FALSE;
}

static void
settle (void)
{
	gint64 until = g_get_monotonic_time () + G_USEC_PER_SEC / 2;

	while (g_get_monotonic_time () < until) {
		while (g_main_context_iteration (NULL, FALSE)) {
		}
		g_usleep (20000);
	}
}

static gboolean
file_has (const char *path, guint key)
{
	char *contents = NULL, *name, *line;
	gboolean found;

	if (!g_file_get_contents (file, &contents, NULL, NULL)) {
		return FALSE;
	}
	name = gtk_accelerator_name (key, 0);
	line = g_strdup_printf ("\n(gtk_accel_path \"%s\" \"%s\")\n", path, name);
	found = strstr (contents, line) != NULL;
	g_free (line);
	g_free (name);
	g_free (contents);

	return found;
}

static void
map_changed (G_GNUC_UNUSED GtkAccelMap *map, G_GNUC_UNUSED char *path,
	     G_GNUC_UNUSED guint key, G_GNUC_UNUSED GdkModifierType mods,
	     G_GNUC_UNUSED gpointer data)
{
	if (!nemo_shortcuts_file_taking ()) {
		unsaved_changes++;
	}
}

int
main (int argc, char **argv)
{
	char *dir;
	int before;

	/* Key names need the display's keymap. */
	if (!gtk_init_check (&argc, &argv)) {
		g_print ("SKIP: no display\n");
		return 77;
	}

	dir = test_scratch_dir ("nemo-shortcuts-file-XXXXXX", NULL);
	g_assert_nonnull (dir);
	file = g_build_filename (dir, "accels", NULL);

	gtk_accel_map_add_entry (PATH_A, GDK_KEY_F1, 0);
	gtk_accel_map_add_entry (PATH_B, GDK_KEY_F2, 0);
	gtk_accel_map_add_entry (PATH_C, GDK_KEY_F3, 0);

	/* Start up the way the app does: load, then watch. */
	other_window_writes (GDK_KEY_F6, GDK_KEY_F11, 0);
	gtk_accel_map_load (file);
	nemo_shortcuts_file_watch (file);
	check (key_of (PATH_A) == GDK_KEY_F6);
	check (key_of (PATH_B) == GDK_KEY_F11);
	g_signal_connect (gtk_accel_map_get (), "changed", G_CALLBACK (map_changed), NULL);

	/* Changed here, waiting out the save delay. */
	gtk_accel_map_change_entry (PATH_B, GDK_KEY_F7, 0, TRUE);
	check (unsaved_changes == 1);

	/* Another window changes C. Its file still has B as it was when this
	   window read it, which isn't a change, so B stays as it is here. */
	other_window_writes (GDK_KEY_F6, GDK_KEY_F11, GDK_KEY_F8);
	check (wait_for (PATH_C, GDK_KEY_F8));
	check (key_of (PATH_B) == GDK_KEY_F7);
	check (key_of (PATH_A) == GDK_KEY_F6);
	check (unsaved_changes == 1);

	/* Saved before the file's change is seen: the save takes it first. */
	other_window_writes (GDK_KEY_F9, GDK_KEY_F11, GDK_KEY_F8);
	nemo_shortcuts_file_save ();
	check (key_of (PATH_A) == GDK_KEY_F9);
	check (file_has (PATH_A, GDK_KEY_F9));
	check (file_has (PATH_B, GDK_KEY_F7));
	check (file_has (PATH_C, GDK_KEY_F8));

	/* Its own save changes nothing here. */
	before = unsaved_changes;
	settle ();
	check (unsaved_changes == before);
	check (key_of (PATH_A) == GDK_KEY_F9 && key_of (PATH_B) == GDK_KEY_F7 && key_of (PATH_C) == GDK_KEY_F8);

	/* A file with some lines missing takes nothing back, and a change in it
	   still counts. */
	check (g_file_set_contents (file, "(gtk_accel_path \"" PATH_C "\" \"F10\")\n", -1, NULL));
	check (wait_for (PATH_C, GDK_KEY_F10));
	check (key_of (PATH_A) == GDK_KEY_F9 && key_of (PATH_B) == GDK_KEY_F7);

	/* A key turned off in another window. */
	check (g_file_set_contents (file, "(gtk_accel_path \"" PATH_A "\" \"\")\n", -1, NULL));
	check (wait_for (PATH_A, 0));
	check (unsaved_changes == before);

	g_free (file);
	g_free (dir);

	return failures == 0 ? 0 : 1;
}
