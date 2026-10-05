/* Exercises the favorites list and its favorites:/// vfs: how stored entries are
 * read and written back, listing an entry that has gone away, the prefix and
 * relative-path answers, what a favorite with an unreachable target does, and a
 * concurrent read while the list is rebuilt. Runs against a throwaway
 * XDG_CONFIG_HOME. */

#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>

#include <libnemo-private/nemo-config.h>
#include <libnemo-private/nemo-favorites.h>
#include <libnemo-private/nemo-favorite-vfs-file.h>
#include <libnemo-private/nemo-favorite-vfs-file-enumerator.h>
#include <libnemo-private/nemo-desktop-thumbnail.h>

#include "test-scratch.h"
#include "test-check.h"

static NemoConfigGroup *root_group;

static void
seed (const char *const *entries)
{
	nemo_config_set_strv (root_group, "favorites", entries);
}

/* stored entry format */

static void
test_entry_parsing (NemoFavorites *favorites)
{
	const char *const colon_name[] = { "text/plain::file:///tmp/notes::draft.txt", NULL };
	const char *const legacy[]     = { "file:///tmp/plain.txt::text/plain", NULL };
	const char *const junk[]       = { "", "file:///tmp/ok.txt::text/plain", NULL };
	const char *const no_mime[]    = { "::file:///tmp/nomime.txt", NULL };
	NemoFavoriteInfo *info;

	/* The mimetype half is stored first, so a name carrying the delimiter is
	 * no longer split in the middle and repointed at another file. */
	seed (colon_name);
	check (nemo_favorites_get_n_favorites (favorites) == 1);
	info = nemo_favorites_find_by_uri (favorites, "file:///tmp/notes::draft.txt");
	check (info != NULL);
	check (info != NULL && g_strcmp0 (info->cached_mimetype, "text/plain") == 0);

	/* Entries written in the old order still read. */
	seed (legacy);
	info = nemo_favorites_find_by_uri (favorites, "file:///tmp/plain.txt");
	check (info != NULL);
	check (info != NULL && g_strcmp0 (info->cached_mimetype, "text/plain") == 0);

	/* An empty entry is dropped rather than hashed as a NULL key. */
	seed (junk);
	check (nemo_favorites_get_n_favorites (favorites) == 1);
	check (nemo_favorites_find_by_uri (favorites, "file:///tmp/ok.txt") != NULL);

	/* Nothing in the mimetype half is not a mimetype of "". */
	seed (no_mime);
	info = nemo_favorites_find_by_uri (favorites, "file:///tmp/nomime.txt");
	check (info != NULL);
	check (info != NULL && info->cached_mimetype == NULL);
}

static void
test_stored_order (NemoFavorites *favorites)
{
	const char *const legacy[] = { "file:///tmp/one.txt::text/plain", NULL };
	char **stored;

	seed (legacy);

	/* Any change rewrites the whole list, in the new order. */
	nemo_favorites_rename (favorites, "file:///tmp/one.txt", "file:///tmp/two.txt");

	stored = nemo_config_get_strv (root_group, "favorites");
	check (g_strv_length (stored) == 1);
	check (stored[0] != NULL &&
	       g_strcmp0 (stored[0], "text/plain::file:///tmp/two.txt") == 0);
	g_strfreev (stored);

	check (nemo_favorites_find_by_uri (favorites, "file:///tmp/two.txt") != NULL);
}

/* Two favorites sharing a basename AND a parent basename must still end up
 * with distinct display names - the name is the favorites:/// identity, so a
 * collision would make one unreachable. */
static void
test_dedup_display_names (NemoFavorites *favorites)
{
	const char *const entries[] = {
		"text/plain::file:///aaa/docs/report.txt",
		"text/plain::file:///bbb/docs/report.txt",
		NULL
	};
	NemoFavoriteInfo *a, *b;

	seed (entries);

	check (nemo_favorites_get_n_favorites (favorites) == 2);

	a = nemo_favorites_find_by_uri (favorites, "file:///aaa/docs/report.txt");
	b = nemo_favorites_find_by_uri (favorites, "file:///bbb/docs/report.txt");
	check (a != NULL && b != NULL);
	check (a != NULL && b != NULL &&
	       g_strcmp0 (a->display_name, b->display_name) != 0);

	/* Each name resolves back to its own uri, not the first-hashed twin. */
	if (a != NULL && b != NULL) {
		NemoFavoriteInfo *ra = nemo_favorites_find_by_display_name (favorites, a->display_name);
		NemoFavoriteInfo *rb = nemo_favorites_find_by_display_name (favorites, b->display_name);
		check (ra != NULL && g_strcmp0 (ra->uri, "file:///aaa/docs/report.txt") == 0);
		check (rb != NULL && g_strcmp0 (rb->uri, "file:///bbb/docs/report.txt") == 0);
	}
}

/* vfs */

static void
test_prefix_matches (void)
{
	GFile *root = g_file_new_for_uri ("favorites:///");
	GFile *child = g_file_new_for_uri ("favorites:///one.txt");
	char *rel;

	/* The two used to be compared the wrong way round, so a real child of the
	 * root answered FALSE. */
	check (g_file_has_prefix (child, root));
	check (!g_file_has_prefix (root, root));
	check (!g_file_has_prefix (root, child));

	rel = g_file_get_relative_path (root, child);
	check (g_strcmp0 (rel, "one.txt") == 0);
	g_free (rel);

	rel = g_file_get_relative_path (root, root);
	check (rel == NULL);
	g_free (rel);

	g_object_unref (root);
	g_object_unref (child);
}

static void
test_enumerator_skips_missing (void)
{
	GFile *root = g_file_new_for_uri ("favorites:///");
	GFileEnumerator *enumerator;
	GList *uris = NULL;
	GError *error = NULL;
	GFileInfo *info;

	uris = g_list_append (uris, (gpointer) "not-a-favorite");

	enumerator = nemo_favorite_vfs_file_enumerator_new (root,
	                                                    G_FILE_ATTRIBUTE_STANDARD_NAME,
	                                                    G_FILE_QUERY_INFO_NONE,
	                                                    uris);

	/* An entry that is no longer in the list used to spin here forever. */
	info = g_file_enumerator_next_file (enumerator, NULL, &error);
	check (info == NULL);
	check (error == NULL);

	g_clear_object (&info);
	g_clear_error (&error);
	g_list_free (uris);
	g_object_unref (enumerator);
	g_object_unref (root);
}

static void
test_enumerate_children (G_GNUC_UNUSED NemoFavorites *favorites, const char *dir)
{
	char *path, *uri, *entry;
	const char *entries[2];
	GFile *root;
	GFileEnumerator *enumerator;
	GFileInfo *info;
	GError *error = NULL;
	int count = 0;

	path = g_build_filename (dir, "listed.txt", NULL);
	check (g_file_set_contents (path, "x", -1, NULL));

	root = g_file_new_for_path (path);
	uri = g_file_get_uri (root);
	g_object_unref (root);

	entry = g_strconcat ("text/plain::", uri, NULL);
	entries[0] = entry;
	entries[1] = NULL;
	seed (entries);

	root = g_file_new_for_uri ("favorites:///");
	enumerator = g_file_enumerate_children (root, "standard::*", G_FILE_QUERY_INFO_NONE,
	                                        NULL, &error);
	check (enumerator != NULL);
	check (error == NULL);

	while (enumerator != NULL &&
	       (info = g_file_enumerator_next_file (enumerator, NULL, NULL)) != NULL) {
		count++;
		check (g_strcmp0 (g_file_info_get_name (info), "listed.txt") == 0);
		g_object_unref (info);
	}

	check (count == 1);

	g_clear_error (&error);
	g_clear_object (&enumerator);
	g_object_unref (root);
	g_free (entry);
	g_free (uri);
	g_remove (path);
	g_free (path);
}

static void
test_missing_target (NemoFavorites *favorites)
{
	/* No mimetype half at all, and a scheme nothing is registered for. */
	const char *const ghost[] = { "nemotest://nowhere/ghost.txt", NULL };
	GFile *file;
	GFileInfo *info;
	GError *error = NULL;

	seed (ghost);
	check (nemo_favorites_find_by_uri (favorites, "nemotest://nowhere/ghost.txt") != NULL);

	file = g_file_new_for_uri ("favorites:///ghost.txt");

	/* The stand-in info used to build its icon from a NULL content type, which
	 * cascaded into four criticals. */
	info = g_file_query_info (file, "standard::*", G_FILE_QUERY_INFO_NONE, NULL, &error);
	check (info != NULL);
	check (error == NULL);
	check (info == NULL || g_file_info_get_icon (info) != NULL);
	g_clear_object (&info);
	g_clear_error (&error);

	/* And nothing came back to hang a filesystem attribute on. */
	info = g_file_query_filesystem_info (file, "filesystem::*", NULL, &error);
	check (info == NULL);
	check (error != NULL);
	g_clear_object (&info);
	g_clear_error (&error);

	g_object_unref (file);
}

/* teardown */

/* Both of these used to unref the config group on the way out, dropping a ref
 * they never took - the store's own ref - so the group was freed while still in
 * the store's table and every later user of it read freed memory. */
static void
test_borrowed_settings_group (void)
{
	NemoConfigGroup *favs_group = nemo_config_get_group ("");
	NemoConfigGroup *thumb_group = nemo_config_get_group ("thumbnailers");
	gpointer favs_alive = favs_group;
	gpointer thumb_alive = thumb_group;
	NemoFavorites *throwaway;
	NemoDesktopThumbnailFactory *factory;

	/* Weak pointers rather than ref counts: this has to answer "was the group
	 * destroyed", and it has to answer it without reading freed memory. */
	g_object_add_weak_pointer (G_OBJECT (favs_group), &favs_alive);
	g_object_add_weak_pointer (G_OBJECT (thumb_group), &thumb_alive);

	throwaway = g_object_new (NEMO_TYPE_FAVORITES, NULL);
	g_object_unref (throwaway);

	check (favs_alive != NULL);

	factory = nemo_desktop_thumbnail_factory_new (NEMO_DESKTOP_THUMBNAIL_SIZE_NORMAL);
	g_object_unref (factory);

	check (thumb_alive != NULL);

	/* And the store still hands out the same, usable, group afterwards. */
	if (favs_alive != NULL) {
		check (nemo_config_get_group ("") == favs_group);
		g_object_remove_weak_pointer (G_OBJECT (favs_group), &favs_alive);
	}

	if (thumb_alive != NULL) {
		check (nemo_config_get_group ("thumbnailers") == thumb_group);
		g_object_remove_weak_pointer (G_OBJECT (thumb_group), &thumb_alive);
	}
}

/* A settings change after teardown used to reach a freed object: the handler was
 * never disconnected, and a queued idle was left behind. */
static void
test_no_callbacks_after_dispose (void)
{
	const char *const one[] = { "text/plain::file:///tmp/after-dispose.txt", NULL };
	const char *const two[] = { "text/plain::file:///tmp/after-dispose-2.txt", NULL };
	NemoFavorites *throwaway;

	throwaway = g_object_new (NEMO_TYPE_FAVORITES, NULL);

	/* Queues the idle that announces the change. */
	seed (one);

	g_object_unref (throwaway);

	/* The idle would fire here, on the object just freed. */
	while (g_main_context_iteration (NULL, FALSE))
		;

	/* And the settings handler would still be connected for this one. */
	seed (two);

	while (g_main_context_iteration (NULL, FALSE))
		;
}

/* concurrent read */

static gint stress_stop;

static gpointer
stress_reader (G_GNUC_UNUSED gpointer data)
{
	while (!g_atomic_int_get (&stress_stop)) {
		GFile *root = g_file_new_for_uri ("favorites:///");
		GFileEnumerator *enumerator;

		enumerator = g_file_enumerate_children (root, "standard::name",
		                                        G_FILE_QUERY_INFO_NONE, NULL, NULL);

		if (enumerator != NULL) {
			GFileInfo *info;

			while ((info = g_file_enumerator_next_file (enumerator, NULL, NULL)) != NULL)
				g_object_unref (info);

			g_object_unref (enumerator);
		}

		g_object_unref (root);
	}

	return NULL;
}

/* The vfs runs on GIO worker threads while the main thread throws the whole
 * table away and rebuilds it. Without the list's lock this segfaults, or trips
 * g_hash_table_iter_next's version assertion, within a couple of hundred
 * rebuilds. A lock taken in the wrong order hangs here rather than passing. */
static void
test_concurrent_reload (NemoFavorites *favorites)
{
	const char *const one[] = { "text/plain::file:///tmp/stress-one.txt", NULL };
	const char *const two[] = { "text/plain::file:///tmp/stress-two.txt",
	                            "text/plain::file:///tmp/stress-three.txt", NULL };
	GThread *readers[4];
	guint i;

	g_atomic_int_set (&stress_stop, 0);

	for (i = 0; i < G_N_ELEMENTS (readers); i++)
		readers[i] = g_thread_new ("fav-stress", stress_reader, NULL);

	for (i = 0; i < 200; i++)
		seed ((i & 1) ? one : two);

	g_atomic_int_set (&stress_stop, 1);

	for (i = 0; i < G_N_ELEMENTS (readers); i++)
		g_thread_join (readers[i]);

	/* Still usable afterwards. */
	seed (two);
	check (nemo_favorites_get_n_favorites (favorites) == 2);
}

/* No arguments runs the lot, which is how meson calls it. Naming one or more
 * runs just those - handy when an earlier one is expected to take the process
 * down with it. */
static gboolean
want (const char *name, int argc, char *argv[])
{
	int i;

	if (argc < 2)
		return TRUE;

	for (i = 1; i < argc; i++) {
		if (g_strcmp0 (argv[i], name) == 0)
			return TRUE;
	}

	return FALSE;
}

int
main (int argc, char *argv[])
{
	char *tmp;
	NemoFavorites *favorites;

	tmp = test_scratch_config_home ("nemo-favorites-test-XXXXXX");

	gtk_init (&argc, &argv);

	/* The stand-in-info path is only worth testing if a critical still counts
	 * as a failure. */
	g_log_set_always_fatal (G_LOG_LEVEL_CRITICAL);

	nemo_config_init ();

	root_group = nemo_config_get_group ("");
	favorites = nemo_favorites_get_default ();

	if (want ("entry-parsing", argc, argv))
		test_entry_parsing (favorites);
	if (want ("stored-order", argc, argv))
		test_stored_order (favorites);
	if (want ("dedup", argc, argv))
		test_dedup_display_names (favorites);
	if (want ("prefix", argc, argv))
		test_prefix_matches ();
	if (want ("enumerator", argc, argv))
		test_enumerator_skips_missing ();
	if (want ("children", argc, argv))
		test_enumerate_children (favorites, tmp);
	if (want ("missing-target", argc, argv))
		test_missing_target (favorites);
	if (want ("borrowed-group", argc, argv))
		test_borrowed_settings_group ();
	if (want ("after-dispose", argc, argv))
		test_no_callbacks_after_dispose ();
	if (want ("concurrent", argc, argv))
		test_concurrent_reload (favorites);

	nemo_config_shutdown ();
	g_free (tmp);

	if (failures == 0)
		g_print ("nemo-favorites: all checks passed\n");

	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
