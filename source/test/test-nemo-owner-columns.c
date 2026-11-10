/* What the three owner columns show. Owner is the user name alone, Owner name
 * the display name alone, and Owner - name both when they differ. An empty
 * display name, which is what an empty GECOS field gives, counts as none: it
 * once came out as "alice - ". On Windows the display name is looked up by us,
 * since GIO leaves it empty there. */

#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <gtk/gtk.h>

#include <libnemo-extension/nemo-column.h>
#include <libnemo-private/nemo-column-utilities.h>
#include <libnemo-private/nemo-file.h>
#include <libnemo-private/nemo-file-private.h>

#ifdef G_OS_WIN32
#include <windows.h>
#include <aclapi.h>
#include <lm.h>
#include "test-scratch.h"
#endif

static int failures = 0;

static void
set_owner (NemoFile *file, const char *owner, const char *owner_real)
{
	g_clear_pointer (&file->details->owner, g_ref_string_release);
	g_clear_pointer (&file->details->owner_real, g_ref_string_release);

	if (owner != NULL) {
		file->details->owner = g_ref_string_new_intern (owner);
	}
	if (owner_real != NULL) {
		file->details->owner_real = g_ref_string_new_intern (owner_real);
	}
}

static void
check_attribute (NemoFile *file, const char *attribute, const char *expected)
{
	char *got = nemo_file_get_string_attribute (file, attribute);

	if (g_strcmp0 (got, expected) != 0) {
		g_printerr ("FAIL: %s is '%s', wanted '%s' (owner '%s', real '%s')\n",
			    attribute,
			    got != NULL ? got : "(null)",
			    expected != NULL ? expected : "(null)",
			    file->details->owner != NULL ? file->details->owner : "(null)",
			    file->details->owner_real != NULL ? file->details->owner_real : "(null)");
		failures++;
	}

	g_free (got);
}

static void
check_case (NemoFile *file, const char *owner, const char *owner_real,
	    const char *want_owner, const char *want_name, const char *want_both)
{
	set_owner (file, owner, owner_real);
	check_attribute (file, "owner", want_owner);
	check_attribute (file, "owner_name", want_name);
	check_attribute (file, "owner_and_name", want_both);
}

static gboolean
has_column (GList *columns, const char *wanted)
{
	GList *l;
	gboolean found = FALSE;

	for (l = columns; l != NULL && !found; l = l->next) {
		char *name;

		g_object_get (l->data, "name", &name, NULL);
		found = g_strcmp0 (name, wanted) == 0;
		g_free (name);
	}

	return found;
}

#ifdef G_OS_WIN32
/* Asked the plain way, by the name of whoever runs the test, so the answer
   does not come through the owner SID the way the code under test gets it. */
static char *
current_user_full_name (char **user_name)
{
	wchar_t name[UNLEN + 1];
	DWORD name_len = G_N_ELEMENTS (name);
	USER_INFO_2 *info = NULL;
	char *full_name = NULL;

	*user_name = NULL;
	if (!GetUserNameW (name, &name_len)) {
		return NULL;
	}
	*user_name = g_utf16_to_utf8 ((gunichar2 *) name, -1, NULL, NULL, NULL);

	if (NetUserGetInfo (NULL, name, 2, (LPBYTE *) &info) == NERR_Success) {
		if (info->usri2_full_name != NULL && info->usri2_full_name[0] != L'\0') {
			full_name = g_utf16_to_utf8 ((gunichar2 *) info->usri2_full_name, -1, NULL, NULL, NULL);
		}
		NetApiBufferFree (info);
	}

	return full_name;
}

/* A file an elevated login makes is owned by Administrators, which has no full
   name, so the scratch file is handed to the user who runs the test. Naming
   your own user as owner needs no privilege. */
static gboolean
own_file (const char *path)
{
	HANDLE token;
	TOKEN_USER *user = NULL;
	DWORD len = 0;
	wchar_t *wpath;
	DWORD err = ERROR_INVALID_DATA;

	if (!OpenProcessToken (GetCurrentProcess (), TOKEN_QUERY, &token)) {
		return FALSE;
	}
	GetTokenInformation (token, TokenUser, NULL, 0, &len);
	if (len > 0) {
		user = g_malloc (len);
		if (!GetTokenInformation (token, TokenUser, user, len, &len)) {
			g_clear_pointer (&user, g_free);
		}
	}
	CloseHandle (token);
	if (user == NULL) {
		return FALSE;
	}

	wpath = g_utf8_to_utf16 (path, -1, NULL, NULL, NULL);
	if (wpath != NULL) {
		err = SetNamedSecurityInfoW (wpath, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION,
					     user->User.Sid, NULL, NULL, NULL);
	}
	g_free (wpath);
	g_free (user);

	return err == ERROR_SUCCESS;
}

/* A file on disk, with the owner name as GIO would give it and the display
   name left for the lookup, the way a fresh info load leaves it. */
static void
check_real_file (const char *path, const char *owner,
		 const char *want_name, const char *want_both)
{
	GFile *location = g_file_new_for_path (path);
	NemoFile *file = nemo_file_get (location);

	set_owner (file, owner, NULL);
	file->details->win32_owner_real_read = FALSE;
	check_attribute (file, "owner_name", want_name);
	/* Once more from the cache. */
	file->details->win32_owner_real_read = FALSE;
	g_clear_pointer (&file->details->owner_real, g_ref_string_release);
	check_attribute (file, "owner_and_name", want_both);

	nemo_file_unref (file);
	g_object_unref (location);
}

static void
check_windows_lookup (void)
{
	g_autoptr(GError) error = NULL;
	g_autofree char *dir = NULL;
	g_autofree char *path = NULL;
	g_autofree char *user_name = NULL;
	g_autofree char *full_name = NULL;
	g_autofree char *both = NULL;
	g_autofree char *system_file = NULL;
	const char *windir;

	dir = test_scratch_dir ("nemo-owner-XXXXXX", &error);
	if (dir == NULL) {
		g_printerr ("FAIL: no scratch dir: %s\n", error->message);
		failures++;
		return;
	}
	path = g_build_filename (dir, "mine.txt", NULL);
	if (!g_file_set_contents (path, "x", 1, &error)) {
		g_printerr ("FAIL: cannot write %s: %s\n", path, error->message);
		failures++;
		return;
	}

	full_name = current_user_full_name (&user_name);
	if (user_name == NULL) {
		g_printerr ("FAIL: no current user name\n");
		failures++;
		return;
	}
	if (full_name == NULL) {
		g_print ("note: %s has no full name, so only the empty case is checked\n", user_name);
	}
	if (own_file (path)) {
		both = full_name != NULL ? g_strdup_printf ("%s - %s", user_name, full_name)
					 : g_strdup (user_name);
		check_real_file (path, user_name, full_name, both);
	} else {
		g_print ("note: %s can't be given to %s, so its full name isn't checked\n", path, user_name);
	}

	/* Owned by a service, not a user, so there is no name to find. */
	windir = g_getenv ("SystemRoot");
	system_file = g_build_filename (windir != NULL ? windir : "C:\\Windows", "notepad.exe", NULL);
	if (g_file_test (system_file, G_FILE_TEST_EXISTS)) {
		check_real_file (system_file, "TrustedInstaller", NULL, "TrustedInstaller");
	}
}
#endif

int
main (int argc, char *argv[])
{
	NemoFile *file;
	GList *columns;

	/* NemoFile's class setup watches the icon theme, which wants a screen. */
	if (!gtk_init_check (&argc, &argv)) {
		g_print ("SKIP: no display\n");
		return 77;
	}

	file = nemo_file_get_by_uri ("file:///nemo-owner-columns-test/file.txt");

	check_case (file, "alice", "Alice Smith", "alice", "Alice Smith", "alice - Alice Smith");
	check_case (file, "alice", "", "alice", NULL, "alice");
	check_case (file, "alice", NULL, "alice", NULL, "alice");
	/* GIO falls back to the user name when there is no GECOS field at all. */
	check_case (file, "alice", "alice", "alice", "alice", "alice");
	check_case (file, NULL, NULL, NULL, NULL, NULL);

	nemo_file_unref (file);

#ifdef G_OS_WIN32
	check_windows_lookup ();
#endif

	columns = nemo_get_all_columns ();
	if (!has_column (columns, "owner")) {
		g_printerr ("FAIL: no owner column\n");
		failures++;
	}
	if (!has_column (columns, "owner_name") || !has_column (columns, "owner_and_name")) {
		g_printerr ("FAIL: an owner name column is missing\n");
		failures++;
	}
	nemo_column_list_free (columns);

	if (failures > 0) {
		g_printerr ("%d check(s) failed\n", failures);
		return EXIT_FAILURE;
	}

	g_print ("OK\n");
	return EXIT_SUCCESS;
}
