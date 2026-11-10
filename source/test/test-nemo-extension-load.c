/* An extension has to find the whole API in whatever loads it. With
 * extension_library=static that is the executable itself, which only works if
 * every member of the archive went in and its symbols were exported. This
 * program links the API the same way the exe does, so it checks both. */

#include <config.h>

#include <stdlib.h>
#include <gmodule.h>

#include <libnemo-extension/nemo-menu-provider.h>

typedef void (*InitFunc) (GTypeModule *module);
typedef void (*ListFunc) (const GType **types, int *num_types);

static char *
item_name (GObject *item)
{
	char *name = NULL;

	g_object_get (item, "name", &name, NULL);
	return name;
}

int
main (int argc, char *argv[])
{
	GModule *module;
	InitFunc initialize;
	ListFunc list_types;
	const GType *types;
	int num_types = 0;
	GObject *ext;
	GList *files;
	GList *items;
	char *first;
	char *second;
	int failures = 0;

	if (argc < 2) {
		g_printerr ("usage: %s <extension module>\n", argv[0]);
		return 1;
	}

	/* Bind now, so a missing symbol fails here with a name, not later as a
	   crash. */
	module = g_module_open (argv[1], G_MODULE_BIND_LOCAL);
	if (module == NULL) {
		g_printerr ("FAIL: %s\n", g_module_error ());
		return 1;
	}

	if (!g_module_symbol (module, "nemo_module_initialize", (gpointer *) &initialize) ||
	    !g_module_symbol (module, "nemo_module_list_types", (gpointer *) &list_types)) {
		g_printerr ("FAIL: %s\n", g_module_error ());
		return 1;
	}

	initialize (NULL);
	list_types (&types, &num_types);
	if (num_types != 1) {
		g_printerr ("FAIL: module listed %d types, wanted 1\n", num_types);
		return 1;
	}

	ext = g_object_new (types[0], NULL);
	if (!NEMO_IS_MENU_PROVIDER (ext)) {
		g_printerr ("FAIL: the module's type is not a menu provider\n");
		return 1;
	}

	/* An empty list gets no items at all, and the module never reads it. */
	files = g_list_append (NULL, ext);
	items = nemo_menu_provider_get_file_items (NEMO_MENU_PROVIDER (ext), NULL, files);
	g_list_free (files);
	if (g_list_length (items) != 2) {
		g_printerr ("FAIL: got %u menu items, wanted 2\n", g_list_length (items));
		return 1;
	}

	first = item_name (items->data);
	second = item_name (items->next->data);
	if (g_strcmp0 (first, "TestExt::item") != 0 || g_strcmp0 (second, "TestExt::sep") != 0) {
		g_printerr ("FAIL: items named %s and %s\n", first, second);
		failures++;
	}

	g_free (first);
	g_free (second);
	g_list_free_full (items, g_object_unref);
	g_object_unref (ext);

	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
