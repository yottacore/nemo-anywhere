/* The tree side pane lists folders only, and a folder with nothing under it
 * has no expander and never shows an "(Empty)" row.
 *
 * The collapse check is the one that matters most. The "(Empty)" row used to
 * be what kept the expander on a folder once it was closed and unloaded, so
 * taking the row away without care loses the expander on every folder that
 * was ever opened and closed. */

#include <config.h>

#include <gtk/gtk.h>
#include <glib/gstdio.h>

#include <libnemo-private/nemo-directory-notify.h>
#include <libnemo-private/nemo-file.h>
#include <libnemo-private/nemo-global-preferences.h>

#include "nemo-tree-sidebar-model.h"
#ifdef G_OS_WIN32
#include <windows.h>
#endif

#include "test-scratch.h"
#include "test-check.h"

static FMTreeModel *model;
static GtkTreeModel *sort_model;
static GtkTreeView *tree;
static char *root_dir;

static gint
icon_scale (G_GNUC_UNUSED FMTreeModel *m, G_GNUC_UNUSED gpointer data)
{
	return 1;
}

static void
make_dir (const char *relative)
{
	char *path = g_build_filename (root_dir, relative, NULL);
	g_mkdir_with_parents (path, 0755);
	g_free (path);
}

/* Hidden means a leading dot here, and the hidden attribute on Windows, where
 * a dot-file is a separate switch the tree never looks at. */
#ifdef G_OS_WIN32
#define SECRET "secret"
#else
#define SECRET ".secret"
#endif

static void
make_hidden_dir (const char *relative)
{
	make_dir (relative);
#ifdef G_OS_WIN32
	{
		char *path = g_build_filename (root_dir, relative, NULL);
		wchar_t *wide = (wchar_t *) g_utf8_to_utf16 (path, -1, NULL, NULL, NULL);
		SetFileAttributesW (wide, GetFileAttributesW (wide) | FILE_ATTRIBUTE_HIDDEN);
		g_free (wide);
		g_free (path);
	}
#endif
}

/* Walks the rows by name. Looking a file up by uri instead would make a
 * NemoFile the folder has not listed yet, which is not what the tree sees. */
static gboolean
iter_for (const char *relative, GtkTreeIter *iter)
{
	GtkTreeModel *m = GTK_TREE_MODEL (model);
	GtkTreeIter parent, child;
	char **names;
	gboolean found, more;
	int i;

	if (!gtk_tree_model_get_iter_first (m, &parent)) {
		return FALSE;
	}

	found = TRUE;
	names = g_strsplit (relative, "/", -1);
	for (i = 0; found && names[i] != NULL; i++) {
		if (names[i][0] == '\0') {
			continue;
		}
		found = FALSE;
		for (more = gtk_tree_model_iter_children (m, &child, &parent); more && !found;
		     more = gtk_tree_model_iter_next (m, &child)) {
			char *name;

			if (child.user_data == NULL) {
				continue;
			}
			gtk_tree_model_get (m, &child, FM_TREE_MODEL_DISPLAY_NAME_COLUMN, &name, -1);
			if (g_strcmp0 (name, names[i]) == 0) {
				parent = child;
				found = TRUE;
			}
			g_free (name);
		}
	}
	g_strfreev (names);

	if (found) {
		*iter = parent;
	}
	return found;
}

static gboolean
has_child (const char *relative)
{
	GtkTreeIter iter;

	return iter_for (relative, &iter)
	       && gtk_tree_model_iter_has_child (GTK_TREE_MODEL (model), &iter);
}

static int
n_children (const char *relative)
{
	GtkTreeIter iter;

	if (!iter_for (relative, &iter)) {
		return -1;
	}
	return gtk_tree_model_iter_n_children (GTK_TREE_MODEL (model), &iter);
}

static GtkTreePath *
view_path (GtkTreeIter *iter)
{
	GtkTreePath *path, *sorted;

	path = gtk_tree_model_get_path (GTK_TREE_MODEL (model), iter);
	sorted = gtk_tree_model_sort_convert_child_path_to_path (GTK_TREE_MODEL_SORT (sort_model), path);
	gtk_tree_path_free (path);
	return sorted;
}

static void
expand (const char *relative, gboolean open)
{
	GtkTreeIter iter;
	GtkTreePath *path;

	if (!iter_for (relative, &iter)) {
		g_printerr ("FAIL no row for %s\n", relative);
		failures++;
		return;
	}
	path = view_path (&iter);
	if (open) {
		gtk_tree_view_expand_row (tree, path, FALSE);
	} else {
		gtk_tree_view_collapse_row (tree, path);
	}
	gtk_tree_path_free (path);
}

static void
pump_for (int ms)
{
	gint64 end = g_get_monotonic_time () + ms * 1000;

	while (g_get_monotonic_time () < end) {
		while (g_main_context_iteration (NULL, FALSE));
		g_usleep (2000);
	}
}

typedef gboolean (*Condition) (void);

static gboolean
wait_for (Condition condition)
{
	gint64 end = g_get_monotonic_time () + 10 * G_USEC_PER_SEC;

	while (g_get_monotonic_time () < end) {
		while (g_main_context_iteration (NULL, FALSE));
		if (condition ()) {
			return TRUE;
		}
		g_usleep (2000);
	}
	return FALSE;
}

static gboolean root_listed (void) { return has_child ("") && iter_for ("empty", &(GtkTreeIter){0}) && iter_for ("parent", &(GtkTreeIter){0}); }
static gboolean empty_settled (void) { return !has_child ("empty") && !has_child ("files-only"); }
static gboolean hidden_only_settled (void) { return !has_child ("hidden-only"); }
static gboolean parent_loaded (void) { return n_children ("parent") == 1 && iter_for ("parent/child", &(GtkTreeIter){0}); }
static gboolean child_settled (void) { return !has_child ("parent/child"); }
static gboolean late_listed (void) { return iter_for ("empty/late", &(GtkTreeIter){0}); }
static gboolean hidden_back (void) { return has_child ("hidden-only"); }
static gboolean secret_listed (void) { return iter_for ("hidden-only/" SECRET, &(GtkTreeIter){0}); }
static gboolean reprobe_settled (void) { return !has_child ("reprobe"); }
static gboolean reprobe_back (void) { return has_child ("reprobe"); }

/* A row that says it has children must have some, and the other way round. */
static void
check_consistent (GtkTreeIter *parent)
{
	GtkTreeModel *m = GTK_TREE_MODEL (model);
	GtkTreeIter child;
	gboolean more;
	int n = gtk_tree_model_iter_n_children (m, parent);

	if (gtk_tree_model_iter_has_child (m, parent) != (n > 0)) {
		GtkTreePath *path = gtk_tree_model_get_path (m, parent);
		char *text = gtk_tree_path_to_string (path);
		g_printerr ("FAIL has_child and n_children (%d) disagree at %s\n", n, text);
		failures++;
		g_free (text);
		gtk_tree_path_free (path);
	}

	for (more = gtk_tree_model_iter_children (m, &child, parent); more;
	     more = gtk_tree_model_iter_next (m, &child)) {
		if (child.user_data != NULL) {
			check_consistent (&child);
		}
	}
}

int
main (int argc, char *argv[])
{
	GtkWidget *window;
	GtkCellRenderer *renderer;
	GIcon *icon;
	char *uri;

	root_dir = test_scratch_config_home ("nemo-tree-folders-test-XXXXXX");

	if (!gtk_init_check (&argc, &argv)) {
		g_printerr ("SKIP no display\n");
		return 77;
	}
	nemo_global_preferences_init ();

	make_dir ("tree/parent/child");
	make_dir ("tree/empty");
	make_dir ("tree/reprobe");
	make_dir ("tree/files-only");
	make_hidden_dir ("tree/hidden-only/" SECRET);
	{
		char *file = g_build_filename (root_dir, "tree", "files-only", "a.txt", NULL);
		char *top = g_build_filename (root_dir, "tree", "top.txt", NULL);
		check (g_file_set_contents (file, "x", 1, NULL));
		check (g_file_set_contents (top, "x", 1, NULL));
		g_free (file);
		g_free (top);
	}
	{
		char *tree_dir = g_build_filename (root_dir, "tree", NULL);
		g_free (root_dir);
		root_dir = tree_dir;
	}

	model = fm_tree_model_new ();
	g_signal_connect (model, "get-icon-scale", G_CALLBACK (icon_scale), NULL);
	sort_model = gtk_tree_model_sort_new_with_model (GTK_TREE_MODEL (model));
	tree = GTK_TREE_VIEW (gtk_tree_view_new_with_model (sort_model));
	renderer = gtk_cell_renderer_text_new ();
	gtk_tree_view_insert_column_with_attributes (tree, -1, "Name", renderer,
						     "text", FM_TREE_MODEL_DISPLAY_NAME_COLUMN, NULL);

	window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	gtk_window_set_default_size (GTK_WINDOW (window), 300, 400);
	gtk_container_add (GTK_CONTAINER (window), GTK_WIDGET (tree));
	gtk_widget_show_all (window);

	uri = g_filename_to_uri (root_dir, NULL, NULL);
	icon = g_themed_icon_new ("folder");
	fm_tree_model_add_root_uri (model, uri, "tree", icon, NULL);
	g_object_unref (icon);
	g_free (uri);

	expand ("", TRUE);
	check (wait_for (root_listed));

	/* Folders only. */
	check (!iter_for ("top.txt", &(GtkTreeIter){0}));

	/* Known to be empty before anyone opens them. */
	check (wait_for (empty_settled));
	check (has_child ("parent"));

	/* Hidden folders count for the look-ahead, so this one keeps its
	 * expander until it is opened and found to show nothing. */
	check (has_child ("hidden-only"));
	expand ("hidden-only", TRUE);
	check (wait_for (hidden_only_settled));
	check (n_children ("hidden-only") == 0);

	/* No dummy row left beside the real one once loaded. */
	expand ("parent", TRUE);
	check (wait_for (parent_loaded));
	check (wait_for (child_settled));

	/* Closing a folder unloads it. It must keep its expander. */
	expand ("parent", FALSE);
	pump_for (200);
	check (has_child ("parent"));
	check (n_children ("parent") == 1);
	expand ("parent", TRUE);
	check (wait_for (parent_loaded));

	/* A folder that gained a sub-folder after it was read as empty. The tree
	 * asks for this before expanding to reach a location. */
	make_dir ("empty/late");
	{
		GtkTreeIter iter;
		check (iter_for ("empty", &iter));
		fm_tree_model_expect_children (model, &iter);
	}
	check (has_child ("empty"));
	expand ("empty", TRUE);
	check (wait_for (late_listed));

	/* Same thing with nobody asking. A copy the app made into a folder it had
	 * already read as empty arrives as an added file, and the tree has to go
	 * and look at the folder again by itself. */
	check (wait_for (reprobe_settled));
	make_dir ("reprobe/child");
	{
		char *path = g_build_filename (root_dir, "reprobe", NULL);
		char *uri = g_filename_to_uri (path, NULL, NULL);
		NemoFile *folder = nemo_file_get_by_uri (uri);

		/* What the tree is told. The copy itself invalidates the folder's
		   item count, and the re-read of it comes out here. */
		nemo_file_changed (folder);

		nemo_file_unref (folder);
		g_free (uri);
		g_free (path);
	}
	check (wait_for (reprobe_back));

	/* Showing hidden files brings back an expander hidden folders earn. */
	fm_tree_model_set_show_hidden_files (model, TRUE);
	check (wait_for (hidden_back));
	pump_for (200);
	check (has_child ("hidden-only"));

	/* And takes it away again with the folder open. The folder closes when
	 * its last row goes, so nothing would load it again to find out. */
	expand ("hidden-only", TRUE);
	check (wait_for (secret_listed));
	fm_tree_model_set_show_hidden_files (model, FALSE);
	check (wait_for (hidden_only_settled));
	pump_for (200);
	check (n_children ("hidden-only") == 0);

	{
		GtkTreeIter root;
		check (iter_for ("", &root));
		check_consistent (&root);
	}

	gtk_widget_destroy (window);
	g_object_unref (sort_model);
	g_object_unref (model);

	if (failures > 0) {
		g_printerr ("%d check(s) failed\n", failures);
		return 1;
	}
	return 0;
}
