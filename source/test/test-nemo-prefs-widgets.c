/* The preferences dialog is built from a resource and wired by widget name, so
 * a renamed or mistyped id only shows up when someone opens the dialog. This
 * loads the same resource and checks the ids the code asks for by name.
 *
 * It also checks the page list the dialog sizes itself from. That list has to
 * name every page: one left out is a page the window is too short for, which
 * is what put Display behind a scrollbar from the moment it opened. */

#include <config.h>

#include <stdlib.h>
#include <gtk/gtk.h>

static int failures = 0;

static const char * const widget_ids[] = {
	"path_separator_combobox",
	"allow_slash_input_checkbutton",
	"vbox_paths",
	"show_hidden_files_checkbutton",
	"show_dot_files_checkbutton",
	"vbox_hidden",
	"date_format_combobox",
	"show_full_path_in_title_bars_checkbutton",
	"show_shortcut_extension_checkbutton",
	"vbox_shortcuts",
	"command_label_0",
	"command_label_1",
	"views_notebook",
	"views_current_page",
	"views_current_tab",
	"list_view_row_shading_checkbutton",
	"rename_selects_whole_name_checkbutton",
	"always_show_tabs_checkbutton",
	"image_folder_switch_checkbutton",
	/* The size spin boxes. The Current tab finds its copies by adding
	   "_current", so a rename there is silent in a different way again. */
	"icon_view_size_spinbutton",
	"icon_view_image_size_spinbutton",
	"compact_view_size_spinbutton",
	"list_view_size_spinbutton",
	"icon_view_size_spinbutton_current",
	"icon_view_image_size_spinbutton_current",
	"compact_view_size_spinbutton_current",
	"list_view_size_spinbutton_current",
	/* The thumbnail cache section, wired in nemo-prefs-file-cache.c. */
	"file_cache_size_spinbutton",
	"file_cache_memory_spinbutton",
	"file_cache_age_spinbutton",
	"file_cache_drop_missing_checkbutton",
	"file_cache_save_checksum_checkbutton",
	"file_cache_usage_label",
	"file_cache_cleanup_button",
	"file_cache_empty_button",
	/* The delete test guard's own switch, grayed when the build forces it. */
	"testguard_all_deletes_checkbutton",
	"close_bar",
	"close_button",
	NULL
};

/* Kept in step with preferences_pages[] in nemo-file-management-properties.c. */
static const char * const page_ids[] = {
	"scrolledwindow2",
	"scrolledwindow1",
	"scrolledwindow3",
	"windows_scrolledwindow",
	"scrolledwindow4",
	"scrolledwindow5",
	"scrolledwindow6",
	"scrolledwindow8",
	"templates_scrolledwindow",
	"scrolledwindow7",
	NULL
};

static gboolean
is_listed (const char *id)
{
	int i;

	for (i = 0; page_ids[i] != NULL; i++) {
		if (g_strcmp0 (page_ids[i], id) == 0) {
			return TRUE;
		}
	}

	return FALSE;
}

/* Every page the stack holds, against every page the sizing measures. */
static void
check_pages_are_all_listed (GtkBuilder *builder)
{
	GtkContainer *stack;
	GList *children, *l;
	int i;

	stack = GTK_CONTAINER (gtk_builder_get_object (builder, "page_stack"));
	if (stack == NULL) {
		g_printerr ("FAIL no page_stack\n");
		failures++;
		return;
	}

	children = gtk_container_get_children (stack);
	for (l = children; l != NULL; l = l->next) {
		const char *id = gtk_buildable_get_name (GTK_BUILDABLE (l->data));

		if (!is_listed (id)) {
			g_printerr ("FAIL page %s is in the stack but not sized for\n",
				    id != NULL ? id : "(unnamed)");
			failures++;
		}
	}
	g_list_free (children);

	for (i = 0; page_ids[i] != NULL; i++) {
		if (gtk_builder_get_object (builder, page_ids[i]) == NULL) {
			g_printerr ("FAIL page %s is sized for but not in the dialog\n",
				    page_ids[i]);
			failures++;
		}
	}
}

int
main (int argc, char *argv[])
{
	GtkBuilder *builder;
	GError *error = NULL;
	int i;

	gtk_init (&argc, &argv);

	builder = gtk_builder_new ();
	if (!gtk_builder_add_from_resource (builder,
					    "/org/nemo/nemo-file-management-properties.glade",
					    &error)) {
		g_printerr ("FAIL cannot load the dialog: %s\n", error->message);
		g_clear_error (&error);
		return EXIT_FAILURE;
	}

	for (i = 0; widget_ids[i] != NULL; i++) {
		if (gtk_builder_get_object (builder, widget_ids[i]) == NULL) {
			g_printerr ("FAIL no widget named %s\n", widget_ids[i]);
			failures++;
		}
	}

	check_pages_are_all_listed (builder);

	g_object_unref (builder);

	if (failures > 0) {
		g_printerr ("%d failure(s)\n", failures);
		return EXIT_FAILURE;
	}

	g_print ("preferences widgets: all checks passed\n");
	return EXIT_SUCCESS;
}
