/* The settings dialog is built from a resource and wired by widget name, so
 * a renamed or mistyped id only shows up when someone opens the dialog. This
 * loads the same resource and checks the ids the code asks for by name.
 *
 * It also checks the page list the dialog sizes itself from. That list has to
 * name every page: one left out is a page the window is too short for, which
 * is what put Display behind a scrollbar from the moment it opened.
 *
 * And a few strings that were given word for word, so an edit to the layout
 * can't reword them by accident. */

#include <config.h>

#include <stdlib.h>
#include <gtk/gtk.h>

#include <libnemo-private/nemo-tab-list.h>

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
	"views_stack",
	"views_current_page",
	"page_sidebar",
	"remember_folder_settings_checkbutton",
	"inherit_view_alignment",
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

/* Every page with settings on it, sub-tabs included. The dialog finds them by
   walking the stack now, so this checks the walk sees what is meant to be
   there, and that each is a scrolled window, which is what it measures. */
static const char * const page_ids[] = {
	"scrolledwindow2",
	"scrolledwindow1",
	"behavior_files_scrolledwindow",
	"behavior_programs_scrolledwindow",
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

/* Wording given word for word: the guard's switch, and the terminal field's
   tip on both its label and the entry. */
static const struct {
	const char *id;
	const char *property;
	const char *text;
} wording[] = {
	{ "testguard_all_deletes_checkbutton", "label",
	  "Paranoid mode for move, overwrite, trash, and delete" },
	{ "testguard_all_deletes_checkbutton", "tooltip-text",
	  "This enables an alternate code path that is hard to accidentally OK, on every "
	  "such action even if the action isn't triggered by a user. Every pre-release has "
	  "this enabled by default - by strict release rules, rather than actual need." },
	{ "command_label_1", "tooltip-text", "If empty, uses your default terminal." },
	{ "terminal_exec_entry", "tooltip-text", "If empty, uses your default terminal." },
};

static void
check_wording (GtkBuilder *builder)
{
	size_t i;

	for (i = 0; i < G_N_ELEMENTS (wording); i++) {
		GObject *widget = gtk_builder_get_object (builder, wording[i].id);
		char *text = NULL;

		if (widget == NULL) {
			g_printerr ("FAIL no widget named %s\n", wording[i].id);
			failures++;
			continue;
		}
		g_object_get (widget, wording[i].property, &text, NULL);
		if (g_strcmp0 (text, wording[i].text) != 0) {
			g_printerr ("FAIL %s %s is \"%s\"\n", wording[i].id, wording[i].property,
				    text != NULL ? text : "(none)");
			failures++;
		}
		g_free (text);
	}
}

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

/* Every page the stacks hold, against every page listed above. */
static void
check_stack (GtkContainer *stack)
{
	GList *children, *l;

	children = gtk_container_get_children (stack);
	for (l = children; l != NULL; l = l->next) {
		const char *id = gtk_buildable_get_name (GTK_BUILDABLE (l->data));

		if (GTK_IS_STACK (l->data)) {
			check_stack (l->data);
			continue;
		}
		if (!is_listed (id)) {
			g_printerr ("FAIL page %s is in the stack but not sized for\n",
				    id != NULL ? id : "(unnamed)");
			failures++;
		}
		if (!GTK_IS_SCROLLED_WINDOW (l->data)) {
			g_printerr ("FAIL page %s is not a scrolled window\n",
				    id != NULL ? id : "(unnamed)");
			failures++;
		}
	}
	g_list_free (children);
}

static void
check_pages_are_all_listed (GtkBuilder *builder)
{
	GtkContainer *stack;
	int i;

	stack = GTK_CONTAINER (gtk_builder_get_object (builder, "page_stack"));
	if (stack == NULL) {
		g_printerr ("FAIL no page_stack\n");
		failures++;
		return;
	}

	check_stack (stack);

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

	g_type_ensure (NEMO_TYPE_TAB_LIST);
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
	check_wording (builder);

	g_object_unref (builder);

	if (failures > 0) {
		g_printerr ("%d failure(s)\n", failures);
		return EXIT_FAILURE;
	}

	g_print ("settings widgets: all checks passed\n");
	return EXIT_SUCCESS;
}
