/* Shared by the 2 delete-guard tests, the POSIX one and the Windows one.
 * They ask the same questions of the same guard; only the spellings of a path
 * given to it differ. Needs check(), so include test-check.h first. */

#ifndef TEST_GUARD_COMMON_H
#define TEST_GUARD_COMMON_H

#include <glib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>

#include <libnemo-private/nemo-delete-guard.h>

static char *
make_file (const char *dir, const char *name)
{
	char *path = g_build_filename (dir, name, NULL);

	check (g_file_set_contents (path, "x", -1, NULL));
	return path;
}

static char *
make_dir (const char *dir, const char *name)
{
	char *path = g_build_filename (dir, name, NULL);

	g_mkdir (path, 0700);
	return path;
}

static gboolean
protected_path (const char *path)
{
	GFile *file = g_file_new_for_path (path);
	gboolean protected = nemo_delete_guard_is_protected (file);

	g_object_unref (file);
	return protected;
}

static gboolean
real_folder (const char *path)
{
	GFile *file = g_file_new_for_path (path);
	gboolean real = nemo_delete_guard_is_real_folder (file, NULL, NULL);

	g_object_unref (file);
	return real;
}

static gboolean
exists (const char *path)
{
	GStatBuf st;

	return g_lstat (path, &st) == 0;
}

#endif /* TEST_GUARD_COMMON_H */
