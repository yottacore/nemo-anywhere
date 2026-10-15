/* Mechanism check for the replace-folder symlink defect (remove_target_
 * recursively). That function is static and only reached through a modal
 * Replace-conflict dialog, so it cannot be driven unattended; this instead
 * pins the two GIO facts the fix relies on:
 *
 *   1. enumerate-children with NOFOLLOW still opens a symlink-to-directory
 *      and lists the TARGET's entries - the premise that made the old,
 *      ungated recursion delete outside the replaced folder.
 *   2. a NOFOLLOW type query on that same symlink reports SYMBOLIC_LINK,
 *      not DIRECTORY - so the added gate skips it and only real dirs recurse.
 *
 * If GIO ever changed either, the fix's reasoning would break and this
 * fails loudly. */

#include <config.h>

#include <unistd.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>

#include "test-scratch.h"
#include "test-check.h"

int
main (void)
{
	char  *root, *target, *victim, *link;
	GFile *link_gf;

	root   = test_scratch_dir ("nemo-symlink-XXXXXX", NULL);
	target = g_build_filename (root, "external", NULL);
	link   = g_build_filename (root, "folder-being-replaced", NULL);
	g_mkdir (target, 0700);

	/* a file living in the external target - this is what must NOT be seen
	 * as a child to be recursed into and deleted */
	victim = g_build_filename (target, "precious.txt", NULL);
	check (g_file_set_contents (victim, "keep me", -1, NULL));

	if (symlink (target, link) != 0) {
		g_printerr ("SKIP: symlink() unavailable\n");
		g_free (victim); g_free (link); g_free (target); g_free (root);
		return 77;
	}

	link_gf = g_file_new_for_path (link);

	/* Fact 1: NOFOLLOW enumerate still descends into the target. */
	{
		GFileEnumerator *e = g_file_enumerate_children (
			link_gf, G_FILE_ATTRIBUTE_STANDARD_NAME,
			G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, NULL);
		int seen = 0;
		GFileInfo *fi;

		check (e != NULL);
		while (e && (fi = g_file_enumerator_next_file (e, NULL, NULL)) != NULL) {
			if (g_strcmp0 (g_file_info_get_name (fi), "precious.txt") == 0)
				seen = 1;
			g_object_unref (fi);
		}
		if (e) {
			g_file_enumerator_close (e, NULL, NULL);
			g_object_unref (e);
		}
		check (seen == 1);   /* the dangerous premise really holds */
	}

	/* Fact 2: the gate the fix adds sees a symlink, not a directory. */
	{
		GFileInfo *ti = g_file_query_info (
			link_gf, G_FILE_ATTRIBUTE_STANDARD_TYPE,
			G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, NULL);

		check (ti != NULL);
		check (ti != NULL &&
		       g_file_info_get_file_type (ti) == G_FILE_TYPE_SYMBOLIC_LINK);
		check (ti != NULL &&
		       g_file_info_get_file_type (ti) != G_FILE_TYPE_DIRECTORY);
		g_clear_object (&ti);
	}

	g_object_unref (link_gf);
	g_free (victim); g_free (link); g_free (target); g_free (root);

	if (failures == 0)
		g_print ("nemo-symlink-recurse: all checks passed\n");
	return failures == 0 ? 0 : 1;
}
