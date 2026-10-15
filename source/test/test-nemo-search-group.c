/* The folder rows that grouped search results hang off. These stand for folders
 * nobody opened, so the checks here are mostly about what must NOT happen to
 * them: no unload on collapse, no leftover row once the last match goes. */

#include <config.h>

#include <gtk/gtk.h>
#include <glib/gstdio.h>

#include <libnemo-private/nemo-config.h>
#include <libnemo-private/nemo-global-preferences.h>

#include "nemo-list-model.h"

#include "test-scratch.h"
#include "test-check.h"

static NemoFile *
file_at (const char *root, const char *relative)
{
	char *path, *uri;
	NemoFile *file;

	path = g_build_filename (root, relative, NULL);
	uri = g_filename_to_uri (path, NULL, NULL);
	file = nemo_file_get_by_uri (uri);

	g_free (uri);
	g_free (path);

	return file;
}

static char *
name_of (NemoListModel *model, GtkTreeIter *iter, int name_column)
{
	GValue value = G_VALUE_INIT;
	char *name;

	gtk_tree_model_get_value (GTK_TREE_MODEL (model), iter, name_column, &value);
	name = g_value_dup_string (&value);
	g_value_unset (&value);

	return name;
}

int
main (int argc, char *argv[])
{
	NemoListModel *model;
	NemoColumn *column;
	NemoDirectory *group_a, *group_b, *again;
	NemoFile *dir_a, *dir_b, *match_a, *match_b;
	GtkTreeIter group_iter, child_iter;
	gboolean created;
	char *tmp, *name;
	int name_column;

	tmp = test_scratch_config_home ("nemo-search-group-test-XXXXXX");

	gtk_init (&argc, &argv);
	nemo_global_preferences_init ();

	{
		char *sub = g_build_filename (tmp, "one", "two", NULL);
		char *other = g_build_filename (tmp, "three", NULL);
		char *leaf = g_build_filename (sub, "hit.txt", NULL);
		char *leaf2 = g_build_filename (other, "hit.txt", NULL);

		g_mkdir_with_parents (sub, 0755);
		g_mkdir_with_parents (other, 0755);
		check (g_file_set_contents (leaf, "x", 1, NULL));
		check (g_file_set_contents (leaf2, "x", 1, NULL));

		g_free (sub);
		g_free (other);
		g_free (leaf);
		g_free (leaf2);
	}

	model = g_object_new (NEMO_TYPE_LIST_MODEL, NULL);
	column = nemo_column_new ("name", "name", "Name", "");
	name_column = nemo_list_model_add_column (model, column);

	dir_a = file_at (tmp, "one/two");
	dir_b = file_at (tmp, "three");
	match_a = file_at (tmp, "one/two/hit.txt");
	match_b = file_at (tmp, "three/hit.txt");

	group_a = nemo_list_model_add_search_group (model, dir_a, "one/two", &created);
	check (group_a != NULL);
	check (created);

	/* Asking twice gives the same row back rather than a second one. */
	again = nemo_list_model_add_search_group (model, dir_a, "one/two", &created);
	check (again == group_a);
	check (!created);
	check (gtk_tree_model_iter_n_children (GTK_TREE_MODEL (model), NULL) == 1);

	group_b = nemo_list_model_add_search_group (model, dir_b, "three", &created);
	check (group_b != NULL && group_b != group_a);
	check (gtk_tree_model_iter_n_children (GTK_TREE_MODEL (model), NULL) == 2);

	/* The row reads as the path under the folder searched, not as its own name. */
	check (nemo_list_model_get_tree_iter_from_file (model, dir_a, NULL, &group_iter));
	name = name_of (model, &group_iter, name_column);
	check (g_strcmp0 (name, "one/two") == 0);
	g_free (name);

	/* A match filed under its group shows as a child of that row. */
	check (nemo_list_model_add_file (model, match_a, group_a));
	check (nemo_list_model_add_file (model, match_b, group_b));

	check (nemo_list_model_get_tree_iter_from_file (model, dir_a, NULL, &group_iter));
	check (gtk_tree_model_iter_n_children (GTK_TREE_MODEL (model), &group_iter) == 1);
	check (gtk_tree_model_iter_children (GTK_TREE_MODEL (model), &child_iter, &group_iter));
	name = name_of (model, &child_iter, name_column);
	check (g_strcmp0 (name, "hit.txt") == 0);
	g_free (name);

	/* And is found again only under its group, not at the top level. */
	check (nemo_list_model_get_tree_iter_from_file (model, match_a, group_a, &child_iter));
	check (!nemo_list_model_get_tree_iter_from_file (model, match_a, NULL, &child_iter));

	/* Collapsing a group must not throw its matches away - the folder was never
	   loaded, so there is nothing to reload them from. */
	check (nemo_list_model_get_tree_iter_from_file (model, dir_a, NULL, &group_iter));
	nemo_list_model_unload_subdirectory (model, &group_iter);
	check (nemo_list_model_get_tree_iter_from_file (model, dir_a, NULL, &group_iter));
	check (gtk_tree_model_iter_n_children (GTK_TREE_MODEL (model), &group_iter) == 1);
	check (gtk_tree_model_iter_children (GTK_TREE_MODEL (model), &child_iter, &group_iter));
	name = name_of (model, &child_iter, name_column);
	check (g_strcmp0 (name, "hit.txt") == 0);
	g_free (name);

	/* An emptied group is reported so, and goes without taking the other with it. */
	check (!nemo_list_model_search_group_is_empty (model, group_a));
	nemo_list_model_remove_file (model, match_a, group_a);
	check (nemo_list_model_search_group_is_empty (model, group_a));
	nemo_list_model_remove_search_group (model, group_a);
	check (gtk_tree_model_iter_n_children (GTK_TREE_MODEL (model), NULL) == 1);
	check (!nemo_list_model_get_tree_iter_from_file (model, dir_a, NULL, &group_iter));

	/* Clearing walks groups and their matches together. */
	nemo_list_model_clear (model);
	check (nemo_list_model_is_empty (model));

	nemo_file_unref (dir_a);
	nemo_file_unref (dir_b);
	nemo_file_unref (match_a);
	nemo_file_unref (match_b);
	g_object_unref (column);
	g_object_unref (model);

	nemo_config_shutdown ();
	g_free (tmp);

	if (failures == 0)
		g_print ("nemo-search-group: all checks passed\n");

	return failures == 0 ? 0 : 1;
}
