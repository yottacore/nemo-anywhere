/* The context menu items a setting can hide are found by their path in the
 * menu file. A path that no longer matches only logs a debug line, and the
 * setting quietly stops hiding anything, so a menu moved into a submenu has to
 * take its table entries with it. This reads the menu files merged into the
 * window's menus and checks that every path in the table is in one of them. */

#include <config.h>

#include <stdlib.h>
#include <gio/gio.h>

#include "src/nemo-actions.h"
#include "test-check.h"

typedef struct {
	GPtrArray *stack;
	GHashTable *paths;
} Walk;

static void
start_element (G_GNUC_UNUSED GMarkupParseContext *context,
	       const gchar *element,
	       const gchar **names,
	       const gchar **values,
	       gpointer user_data,
	       G_GNUC_UNUSED GError **error)
{
	Walk *walk = user_data;
	const gchar *name = NULL;
	const gchar *action = NULL;
	gchar *path;
	guint i;

	for (i = 0; names[i] != NULL; i++) {
		if (g_strcmp0 (names[i], "name") == 0) {
			name = values[i];
		} else if (g_strcmp0 (names[i], "action") == 0) {
			action = values[i];
		}
	}

	/* The ui manager names an unnamed item after its action. */
	if (name == NULL) {
		name = action;
	}

	if (g_strcmp0 (element, "ui") == 0 || name == NULL) {
		g_ptr_array_add (walk->stack, NULL);
		return;
	}

	path = NULL;
	for (i = walk->stack->len; i > 0; i--) {
		if (walk->stack->pdata[i - 1] != NULL) {
			path = g_strconcat (walk->stack->pdata[i - 1], "/", name, NULL);
			break;
		}
	}
	if (path == NULL) {
		path = g_strconcat ("/", name, NULL);
	}

	g_hash_table_add (walk->paths, path);
	g_ptr_array_add (walk->stack, path);
}

static void
end_element (G_GNUC_UNUSED GMarkupParseContext *context,
	     G_GNUC_UNUSED const gchar *element,
	     gpointer user_data,
	     G_GNUC_UNUSED GError **error)
{
	Walk *walk = user_data;

	g_ptr_array_remove_index (walk->stack, walk->stack->len - 1);
}

/* The window's own menus, the view's, and the icon view's, which add the
   arrange items to the background menu. */
static const char * const menu_files[] = {
	"/org/nemo/nemo-shell-ui.xml",
	"/org/nemo/nemo-directory-view-ui.xml",
	"/org/nemo/nemo-icon-view-ui.xml",
	NULL
};

static void
read_menu_file (const char *resource,
		Walk       *walk)
{
	GMarkupParser parser = { start_element, end_element, NULL, NULL, NULL };
	GMarkupParseContext *context;
	GError *error = NULL;
	GBytes *bytes;

	bytes = g_resources_lookup_data (resource, G_RESOURCE_LOOKUP_FLAGS_NONE, &error);
	if (bytes == NULL) {
		g_printerr ("%s: %s\n", resource, error->message);
		g_error_free (error);
		failures++;
		return;
	}

	context = g_markup_parse_context_new (&parser, 0, walk, NULL);
	if (!g_markup_parse_context_parse (context, g_bytes_get_data (bytes, NULL),
					   g_bytes_get_size (bytes), &error) ||
	    !g_markup_parse_context_end_parse (context, &error)) {
		g_printerr ("%s: %s\n", resource, error->message);
		g_error_free (error);
		failures++;
	}

	g_markup_parse_context_free (context);
	g_bytes_unref (bytes);
}

int
main (void)
{
	Walk walk;
	guint i;

	walk.stack = g_ptr_array_new ();
	walk.paths = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

	for (i = 0; menu_files[i] != NULL; i++) {
		read_menu_file (menu_files[i], &walk);
	}

	check (g_hash_table_size (walk.paths) > 0);

	for (i = 0; i < CONFIGURABLE_MENU_ITEM_COUNT; i++) {
		const gchar *path = CONFIGURABLE_MENU_ITEM_INFO[i].ui_path;

		if (!g_hash_table_contains (walk.paths, path)) {
			g_printerr ("not in the menu file: %s\n", path);
			failures++;
		}
	}

	g_hash_table_destroy (walk.paths);
	g_ptr_array_free (walk.stack, TRUE);

	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
