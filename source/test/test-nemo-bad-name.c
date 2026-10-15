/* A typed name with a character a file name can't have: "/" on POSIX, and on
 * Windows "\/:*?"<>|" and control characters too. GLib on Windows answers most
 * of those with a plain I/O error, and ":" names a stream, so the name is
 * checked before it goes to disk and the refusal says which character it was.
 * On Windows a device name like "CON" or "nul.txt", and a name ending in a dot
 * or a space, are refused the same way. Covers the check itself, a rename, a
 * nested new folder, the shortcut editor, the Rename button in both conflict
 * dialogs, and a copy's name fixed for a FAT drive. */

#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>

#include <libnemo-private/nemo-extract-conflict-dialog.h>
#include <libnemo-private/nemo-file.h>
#include <libnemo-private/nemo-file-conflict-dialog.h>
#include <libnemo-private/nemo-file-utilities.h>
#include <libnemo-private/nemo-global-preferences.h>
#include <libnemo-private/nemo-link-edit.h>

#include "test-scratch.h"
#include "test-check.h"

#ifdef G_OS_WIN32
#define BAD_NAME  "a*b.txt"
#define BAD_CHAR  "*"
#else
#define BAD_NAME  "a/b.txt"
#define BAD_CHAR  "/"
#endif

static gboolean
names_char (const char *message, const char *bad)
{
	g_autofree char *quoted = g_strdup_printf ("\"%s\"", bad);

	return message != NULL && strstr (message, quoted) != NULL;
}

static void
check_table (void)
{
	for (int c = 1; c < 128; c++) {
		char name[] = { 'a', (char) c, 'b', '\0' };
		const char *bad = nemo_file_name_find_bad_char (name);
#ifdef G_OS_WIN32
		gboolean want = c < 0x20 || strchr ("\\/:*?\"<>|", c) != NULL;
#else
		gboolean want = c == '/';
#endif

		if ((bad != NULL) != want || (bad != NULL && bad != name + 1)) {
			g_printerr ("FAIL: character %d %s\n", c, want ? "not found" : "found");
			failures++;
		}
	}
	check (nemo_file_name_find_bad_char (NULL) == NULL);
	check (nemo_file_name_find_bad_char ("") == NULL);
	check (nemo_file_name_find_bad_char ("plain name.txt") == NULL);
	check (nemo_file_name_find_bad_char ("caf\xc3\xa9 \xe2\x9c\x93") == NULL);
}

static void
check_messages (void)
{
	g_autofree char *good = nemo_file_name_bad_char_message ("fine.txt");
	g_autofree char *bad = nemo_file_name_bad_char_message (BAD_NAME);
	g_autofree char *fixed = nemo_file_name_make_valid ("x" BAD_CHAR "y");
	GError *error = NULL;

	check (good == NULL);
	check (names_char (bad, BAD_CHAR));
	check (strstr (bad, BAD_NAME) != NULL);

	check (nemo_file_name_check_chars ("fine.txt", &error));
	check (error == NULL);
	check (!nemo_file_name_check_chars (BAD_NAME, &error));
	check (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_INVALID_FILENAME));
	check (error != NULL && names_char (error->message, BAD_CHAR));
	g_clear_error (&error);

	check (g_strcmp0 (fixed, "x_y") == 0);

#ifdef G_OS_WIN32
	{
		g_autofree char *control = nemo_file_name_bad_char_message ("a\tb");
		g_autofree char *colon = nemo_file_name_bad_char_message ("a:b");

		check (control != NULL && strstr (control, "control character") != NULL);
		check (names_char (colon, ":"));
	}
#endif
}

/* Once a drive refuses a copy's name, only what it can't take changes. A
   letter outside ASCII is 2 or more bytes, each above 0x7f. */
static void
check_fs_fix (void)
{
	static const struct {
		const char *fs, *before, *after;
	} cases[] = {
		{ "vfat", "caf\xc3\xa9?.txt", "caf\xc3\xa9_.txt" },
		{ "fuse", "na\xc3\xafve \xe2\x9c\x93.txt", "na\xc3\xafve \xe2\x9c\x93.txt" },
		{ "exfat", "\xd0\xbe\xd1\x82\xd1\x87\xd1\x91\xd1\x82:\x01.txt", "\xd0\xbe\xd1\x82\xd1\x87\xd1\x91\xd1\x82__.txt" },
		{ "msdos", "a<b>  ", "a_b_" },
		{ "ext4", "a?b", "a?b" },
		{ NULL, "a?b", "a?b" },
	};

	for (guint i = 0; i < G_N_ELEMENTS (cases); i++) {
		g_autofree char *name = g_strdup (cases[i].before);
		gboolean changed = nemo_file_name_make_valid_for_fs (name, cases[i].fs);

		if (g_strcmp0 (name, cases[i].after) != 0 || changed != (g_strcmp0 (cases[i].before, cases[i].after) != 0)) {
			g_printerr ("FAIL: \"%s\" on %s came out \"%s\", changed %d\n", cases[i].before,
				    cases[i].fs != NULL ? cases[i].fs : "(none)", name, changed);
			failures++;
		}
	}
}

/* Device names and a trailing dot or space: refused on Windows, fine
   elsewhere. The device list itself is the same on every platform. */
static void
check_windows_names (void)
{
	static const struct {
		const char *name;
		gsize device;
		const char *why, *fixed;
	} cases[] = {
		{ "CON", 3, "\"CON\" is a device name", "_CON" },
		{ "nul.txt", 3, "\"nul\" is a device name", "_nul.txt" },
		{ "Aux.tar.gz", 3, "\"Aux\" is a device name", "_Aux.tar.gz" },
		{ "prn", 3, "\"prn\" is a device name", "_prn" },
		{ "con .txt", 3, "\"con\" is a device name", "_con .txt" },
		{ "COM1", 4, "\"COM1\" is a device name", "_COM1" },
		{ "lpt0.log", 4, "\"lpt0\" is a device name", "_lpt0.log" },
		{ "COM\xc2\xb9.txt", 5, "\"COM\xc2\xb9\" is a device name", "_COM\xc2\xb9.txt" },
		{ "NUL.", 3, "\"NUL\" is a device name", "NUL_" },
		{ "a.", 0, "ends in a dot", "a_" },
		{ "a b ", 0, "ends in a space", "a b_" },
		{ "x?. ", 0, "the character \"?\"", "x___" },
		{ "console", 0, NULL, "console" },
		{ "nul_.txt", 0, NULL, "nul_.txt" },
		{ "COM10", 0, NULL, "COM10" },
		{ "COMx.txt", 0, NULL, "COMx.txt" },
		{ "LPT\xc2\xb4", 0, NULL, "LPT\xc2\xb4" },
		{ ".con", 0, NULL, ".con" },
		{ "a.b", 0, NULL, "a.b" },
		{ "..", 0, NULL, NULL },
		{ ".", 0, NULL, NULL },
	};

	for (guint i = 0; i < G_N_ELEMENTS (cases); i++) {
		const char *name = cases[i].name;
		g_autofree char *message = nemo_file_name_bad_char_message (name);
		gsize device = nemo_file_name_windows_device (name);

		if (device != cases[i].device) {
			g_printerr ("FAIL: \"%s\" device part %u, not %u\n", name, (guint) device, (guint) cases[i].device);
			failures++;
		}
#ifdef G_OS_WIN32
		if (cases[i].why == NULL ? message != NULL : message == NULL || strstr (message, cases[i].why) == NULL) {
			g_printerr ("FAIL: \"%s\" gave \"%s\"\n", name, message != NULL ? message : "(nothing)");
			failures++;
		}
		if (cases[i].fixed != NULL) {
			g_autofree char *fixed = nemo_file_name_make_valid (name);

			if (g_strcmp0 (fixed, cases[i].fixed) != 0) {
				g_printerr ("FAIL: \"%s\" fixed to \"%s\", not \"%s\"\n", name, fixed, cases[i].fixed);
				failures++;
			}
		}
#else
		if (message != NULL && strchr (name, '?') == NULL) {
			g_printerr ("FAIL: \"%s\" refused: %s\n", name, message);
			failures++;
		}
#endif
	}
	check (nemo_file_name_windows_device (NULL) == 0);
	check (nemo_file_name_windows_device ("") == 0);
}

static gboolean rename_finished;
static GError *rename_error;

static void
rename_done (G_GNUC_UNUSED NemoFile *file, G_GNUC_UNUSED GFile *result, GError *error,
	     G_GNUC_UNUSED gpointer data)
{
	g_clear_error (&rename_error);
	rename_error = error != NULL ? g_error_copy (error) : NULL;
	rename_finished = TRUE;
}

/* A refusal answers before nemo_file_rename returns, a real rename later. */
static void
rename_and_wait (const char *path, const char *new_name)
{
	g_autoptr (GFile) location = g_file_new_for_path (path);
	NemoFile *file = nemo_file_get (location);

	rename_finished = FALSE;
	nemo_file_rename (file, new_name, rename_done, NULL);
	for (int spins = 0; !rename_finished && spins < 5000; spins++) {
		g_main_context_iteration (NULL, FALSE);
		g_usleep (2000);
	}
	check (rename_finished);
	nemo_file_unref (file);
}

static void
check_rename (const char *root)
{
	g_autofree char *path = g_build_filename (root, "plain.txt", NULL);

	check (g_file_set_contents (path, "x", -1, NULL));

#ifdef G_OS_WIN32
	/* ":" passes under wine, which takes it as part of the name, and makes
	   a stream on NTFS. Neither may happen. */
	static const char * const refused[] = { "a*b.txt", "a?b", "a<b", "a>b", "a|b", "a\"b", "a:b", "a\tb", "a\\b",
						"CON", "nul.txt", "NUL", "com1.log", "a.", "a ", NULL };

	for (int i = 0; refused[i] != NULL; i++) {
		rename_and_wait (path, refused[i]);
		if (!g_error_matches (rename_error, G_IO_ERROR, G_IO_ERROR_INVALID_FILENAME)) {
			g_printerr ("FAIL: rename to \"%s\": %s\n", refused[i],
				    rename_error != NULL ? rename_error->message : "went through");
			failures++;
		}
		check (g_file_test (path, G_FILE_TEST_IS_REGULAR));
	}
	rename_and_wait (path, "a*b.txt");
	check (rename_error != NULL && names_char (rename_error->message, "*"));
	{
		g_autofree char *stream_host = g_build_filename (root, "a", NULL);

		rename_and_wait (path, "a:b");
		check (!g_file_test (stream_host, G_FILE_TEST_EXISTS));
		rename_and_wait (path, "a.");
		check (!g_file_test (stream_host, G_FILE_TEST_EXISTS));
	}
	rename_and_wait (path, "nul.txt");
	check (rename_error != NULL && strstr (rename_error->message, "\"nul\" is a device name") != NULL);
	{
		g_autofree char *kept = g_build_filename (root, "nul.txt", NULL);

		/* Windows 11 makes a plain file of it, and keeping the name it has
		   is no rename. Wine makes the device. */
		if (g_file_set_contents (kept, "x", -1, NULL) && g_file_test (kept, G_FILE_TEST_IS_REGULAR)) {
			rename_and_wait (kept, "nul.txt");
			check (rename_error == NULL);
		} else {
			g_print ("no plain file named nul.txt here, keeping its name not checked\n");
		}
	}
#else
	/* Legal here, and still taken. */
	rename_and_wait (path, "a*b:c?.txt");
	check (rename_error == NULL);
	{
		g_autofree char *renamed = g_build_filename (root, "a*b:c?.txt", NULL);
		g_autofree char *device = g_build_filename (root, "nul.txt", NULL);

		check (g_file_test (renamed, G_FILE_TEST_IS_REGULAR));
		rename_and_wait (renamed, "nul.txt");
		check (rename_error == NULL);
		check (g_file_test (device, G_FILE_TEST_IS_REGULAR));
	}
#endif
	g_clear_error (&rename_error);
}

static void
check_nested (void)
{
	GError *error = NULL;
#ifdef G_OS_WIN32
	char **parts = nemo_new_folder_path_split ("top\\a*b\\c", &error);

	check (parts == NULL);
	check (error != NULL && names_char (error->message, "*"));
	g_clear_error (&error);
	parts = nemo_new_folder_path_split ("top\\a:b", &error);
	check (parts == NULL);
	check (error != NULL && names_char (error->message, ":"));
	g_clear_error (&error);
	parts = nemo_new_folder_path_split ("top\\Con\\c", &error);
	check (parts == NULL);
	check (error != NULL && strstr (error->message, "\"Con\" is a device name") != NULL);
	g_clear_error (&error);
	parts = nemo_new_folder_path_split ("top\\a \\c", &error);
	check (parts == NULL);
	check (error != NULL && strstr (error->message, "ends in a space") != NULL);
#else
	char **parts = nemo_new_folder_path_split ("top/a*b:c", &error);

	check (parts != NULL && g_strv_length (parts) == 2 && g_strcmp0 (parts[1], "a*b:c") == 0);
	check (error == NULL);
#endif
	g_clear_error (&error);
	g_strfreev (parts);
}

static void
check_shortcut_name (const char *root)
{
	g_autofree char *path = g_build_filename (root, "s.lnk", NULL);
	GError *error = NULL;

	check (g_file_set_contents (path, "", -1, NULL));
	check (!nemo_link_edit_shortcut (path, BAD_NAME, FALSE, NULL, NULL, NULL, &error));
	check (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_INVALID_FILENAME));
	check (error != NULL && names_char (error->message, BAD_CHAR));
	check (g_file_test (path, G_FILE_TEST_IS_REGULAR));
	g_clear_error (&error);
}

static void
find_widgets (GtkWidget *widget, gpointer data)
{
	GList **found = data;

	*found = g_list_prepend (*found, widget);
	if (GTK_IS_CONTAINER (widget)) {
		gtk_container_forall (GTK_CONTAINER (widget), find_widgets, data);
	}
}

static GtkWidget *
find_entry (GtkWidget *dialog)
{
	GList *all = NULL, *l;
	GtkWidget *entry = NULL;

	find_widgets (dialog, &all);
	for (l = all; l != NULL && entry == NULL; l = l->next) {
		if (GTK_IS_ENTRY (l->data)) {
			entry = l->data;
		}
	}
	g_list_free (all);
	return entry;
}

/* The line under the entry that says why, found by what it says. */
static GtkWidget *
find_hint (GtkWidget *dialog, const char *text)
{
	GList *all = NULL, *l;
	GtkWidget *hint = NULL;

	find_widgets (dialog, &all);
	for (l = all; l != NULL && hint == NULL; l = l->next) {
		if (GTK_IS_LABEL (l->data) && g_strcmp0 (gtk_label_get_text (l->data), text) == 0) {
			hint = l->data;
		}
	}
	g_list_free (all);
	return hint;
}

/* A bad name grays Rename and says why. A good one undoes both. */
static void
check_rename_button (GtkWidget *dialog, const char *bad_name)
{
	GtkWidget *entry = find_entry (dialog);
	GtkWidget *rename = gtk_dialog_get_widget_for_response (GTK_DIALOG (dialog), CONFLICT_RESPONSE_RENAME);
	g_autofree char *message = nemo_file_name_bad_char_message (bad_name);
	GtkWidget *hint;

	check (entry != NULL && rename != NULL);
	if (entry == NULL || rename == NULL) {
		return;
	}

	gtk_entry_set_text (GTK_ENTRY (entry), bad_name);
	hint = find_hint (dialog, message);
	check (hint != NULL && gtk_widget_get_visible (hint));
	check (gtk_widget_get_visible (rename) && !gtk_widget_get_sensitive (rename));

	gtk_entry_set_text (GTK_ENTRY (entry), "other name.txt");
	check (hint == NULL || !gtk_widget_get_visible (hint));
	check (gtk_widget_get_visible (rename) && gtk_widget_get_sensitive (rename));
}

/* The entry gets the name last, so a filled entry means the dialog is done. */
static void
wait_for_name (NemoFileConflictDialog *dialog)
{
	for (int spins = 0; spins < 5000; spins++) {
		g_autofree char *name = nemo_file_conflict_dialog_get_new_name (dialog);

		if (name != NULL && *name != '\0') {
			return;
		}
		g_main_context_iteration (NULL, FALSE);
		g_usleep (2000);
	}
	g_printerr ("FAIL: the copy conflict dialog never filled in its name\n");
	failures++;
}

static void
check_dialogs (const char *root)
{
	g_autofree char *from = g_build_filename (root, "from", NULL);
	g_autofree char *to = g_build_filename (root, "to", NULL);
	g_autofree char *src_path = g_build_filename (from, "a.txt", NULL);
	g_autofree char *dest_path = g_build_filename (to, "a.txt", NULL);
	g_autoptr (GFile) src = g_file_new_for_path (src_path);
	g_autoptr (GFile) dest = g_file_new_for_path (dest_path);
	g_autoptr (GFile) dest_dir = g_file_new_for_path (to);
	GtkWidget *dialog;

	check (g_mkdir_with_parents (from, 0700) == 0);
	check (g_mkdir_with_parents (to, 0700) == 0);
	check (g_file_set_contents (src_path, "new", -1, NULL));
	check (g_file_set_contents (dest_path, "old", -1, NULL));

	dialog = nemo_file_conflict_dialog_new (NULL, src, dest, dest_dir);
	wait_for_name (NEMO_FILE_CONFLICT_DIALOG (dialog));
	check_rename_button (dialog, BAD_NAME);
#ifdef G_OS_WIN32
	check_rename_button (dialog, "NUL");
	check_rename_button (dialog, "a.txt.");
#endif
	gtk_widget_destroy (dialog);

	dialog = nemo_extract_conflict_dialog_new (NULL, "photos.zip", "a.txt", FALSE, 3, 0, dest, dest_dir);
	check_rename_button (dialog, BAD_NAME);
#ifdef G_OS_WIN32
	check_rename_button (dialog, "con.txt");
#endif
	gtk_widget_destroy (dialog);
}

int
main (int argc, char **argv)
{
	g_autofree char *home = test_scratch_config_home ("nemo-bad-name-home-XXXXXX");
	g_autofree char *root = test_scratch_dir ("nemo-bad-name-XXXXXX", NULL);
	gboolean have_display;

	if (home == NULL || root == NULL) {
		g_printerr ("FAIL: could not make a temp dir\n");
		return EXIT_FAILURE;
	}

	have_display = gtk_init_check (&argc, &argv);
	nemo_global_preferences_init ();

	check_table ();
	check_messages ();
	check_windows_names ();
	check_fs_fix ();
	check_rename (root);
	check_nested ();
	check_shortcut_name (root);
	if (have_display) {
		check_dialogs (root);
	} else {
		g_print ("no display, dialogs not checked\n");
	}

	if (failures == 0) {
		g_print ("bad name: all checks passed\n");
	}
	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
