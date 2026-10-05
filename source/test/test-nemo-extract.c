/* The parts of unpacking that can be decided without writing anything: which
 * names are offered an Extract menu at all, what folder an archive unpacks into
 * when each gets its own, how a name that is already taken is stepped along,
 * what a hostile entry path is reduced to, and what switches a command backend
 * is handed. The unpacking itself needs real archives and a real job queue, so
 * it is not covered here. */

#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <gio/gio.h>

#include <libnemo-private/nemo-extract.h>
#include "test-check.h"
#include "test-argv.h"

static void
check_recognition (void)
{
	check (nemo_extract_is_archive_name ("photos.zip"));
	check (nemo_extract_is_archive_name ("photos.tar.gz"));
	check (nemo_extract_is_archive_name ("photos.7z"));
	check (nemo_extract_is_archive_name ("photos.rar"));
	check (nemo_extract_is_archive_name ("notes.txt.gz"));

	/* Case is not part of the answer - archives arrive from every kind of
	   system, and Windows names them however it likes. */
	check (nemo_extract_is_archive_name ("PHOTOS.ZIP"));
	check (nemo_extract_is_archive_name ("Photos.Tar.Gz"));

	check (!nemo_extract_is_archive_name ("photos.txt"));
	check (!nemo_extract_is_archive_name ("photos"));

	/* A suffix and nothing else is a file called that, not an archive. */
	check (!nemo_extract_is_archive_name (".zip"));
	check (!nemo_extract_is_archive_name (".7z"));

	/* ".tar.gz" does have something in front of the suffix, though: it is a
	   hidden file named ".tar" that somebody gzipped. */
	check (nemo_extract_is_archive_name (".tar.gz"));

	/* A split archive: 7z numbers the volumes past the real extension, and
	   numbers the only one as well. */
	check (nemo_extract_is_archive_name ("photos.7z.001"));
	check (nemo_extract_is_archive_name ("photos.7z.017"));
	check (nemo_extract_is_archive_name ("photos.zip.001"));
	check (nemo_extract_is_archive_name ("photos.tar.gz.002"));

	/* Three digits after a dot, and an archive underneath them. Neither on
	   its own is enough. */
	check (!nemo_extract_is_archive_name ("photos.txt.001"));
	check (!nemo_extract_is_archive_name ("photos.7z.01"));
	check (!nemo_extract_is_archive_name ("photos.7z.0011"));
	check (!nemo_extract_is_archive_name ("photos.7z.abc"));
	check (!nemo_extract_is_archive_name (".7z.001"));
}

static void
check_folder_names (void)
{
	char *text;

	text = nemo_extract_folder_name ("photos.zip");
	check (g_strcmp0 (text, "photos") == 0);
	g_free (text);

	/* A two-part suffix comes off whole, so it is not left as "photos.tar". */
	text = nemo_extract_folder_name ("photos.tar.gz");
	check (g_strcmp0 (text, "photos") == 0);
	g_free (text);

	text = nemo_extract_folder_name ("Photos.TAR.XZ");
	check (g_strcmp0 (text, "Photos") == 0);
	g_free (text);

	/* Only the archive suffix goes; anything else in the name stays. */
	text = nemo_extract_folder_name ("backup.2026-01-01.tar");
	check (g_strcmp0 (text, "backup.2026-01-01") == 0);
	g_free (text);

	check (nemo_extract_folder_name ("photos.txt") == NULL);
	check (nemo_extract_folder_name (".zip") == NULL);

	text = nemo_extract_folder_name (".tar.gz");
	check (g_strcmp0 (text, ".tar") == 0);
	g_free (text);

	/* The volume number goes with the suffix, so every part of a split
	   archive names the same folder. */
	text = nemo_extract_folder_name ("photos.7z.001");
	check (g_strcmp0 (text, "photos") == 0);
	g_free (text);

	text = nemo_extract_folder_name ("photos.7z.014");
	check (g_strcmp0 (text, "photos") == 0);
	g_free (text);

	text = nemo_extract_folder_name ("photos.tar.gz.002");
	check (g_strcmp0 (text, "photos") == 0);
	g_free (text);
}

static void
check_unique_names (void)
{
	char *text;

	text = nemo_extract_unique_name ("notes.txt", 1);
	check (g_strcmp0 (text, "notes (1).txt") == 0);
	g_free (text);

	text = nemo_extract_unique_name ("notes.txt", 7);
	check (g_strcmp0 (text, "notes (7).txt") == 0);
	g_free (text);

	/* Nothing to keep on the end, so the number goes on the end. */
	text = nemo_extract_unique_name ("README", 2);
	check (g_strcmp0 (text, "README (2)") == 0);
	g_free (text);

	/* A leading dot starts a hidden name rather than an extension, so the
	   whole of ".bashrc" is the name. */
	text = nemo_extract_unique_name (".bashrc", 1);
	check (g_strcmp0 (text, ".bashrc (1)") == 0);
	g_free (text);

	text = nemo_extract_unique_name (".config.json", 1);
	check (g_strcmp0 (text, ".config (1).json") == 0);
	g_free (text);
}

static void
check_sanitize (void)
{
	char *text;

	text = nemo_extract_sanitize_path ("photos/holiday.jpg");
	check (g_strcmp0 (text, "photos/holiday.jpg") == 0);
	g_free (text);

	/* An absolute path in an archive is still unpacked inside the folder
	   the person picked, never at the root. */
	text = nemo_extract_sanitize_path ("/etc/passwd");
	check (g_strcmp0 (text, "etc/passwd") == 0);
	g_free (text);

	/* The classic escape: every ".." goes, so nothing climbs out. */
	text = nemo_extract_sanitize_path ("../../../etc/passwd");
	check (g_strcmp0 (text, "etc/passwd") == 0);
	g_free (text);

	text = nemo_extract_sanitize_path ("photos/../../secrets/key");
	check (g_strcmp0 (text, "photos/secrets/key") == 0);
	g_free (text);

	/* A drive letter would leave the folder altogether. */
	text = nemo_extract_sanitize_path ("C:\\Windows\\System32\\evil.dll");
	check (g_strcmp0 (text, "Windows/System32/evil.dll") == 0);
	g_free (text);

	/* Backslashes are separators inside an archive whatever the platform,
	   because that is what the tools that wrote them meant. */
	text = nemo_extract_sanitize_path ("photos\\holiday.jpg");
	check (g_strcmp0 (text, "photos/holiday.jpg") == 0);
	g_free (text);

	/* Doubled, trailing and single-dot components are noise. */
	text = nemo_extract_sanitize_path ("photos//./holiday.jpg");
	check (g_strcmp0 (text, "photos/holiday.jpg") == 0);
	g_free (text);

	text = nemo_extract_sanitize_path ("photos/");
	check (g_strcmp0 (text, "photos") == 0);
	g_free (text);

	/* Nothing usable is left, so there is nothing to unpack. */
	check (nemo_extract_sanitize_path ("..") == NULL);
	check (nemo_extract_sanitize_path ("/") == NULL);
	check (nemo_extract_sanitize_path ("../..") == NULL);
	check (nemo_extract_sanitize_path ("C:") == NULL);
}

static void
check_commands (void)
{
	char **argv;

	argv = nemo_extract_build_command (NEMO_EXTRACT_BACKEND_7Z, "7z",
					   "/tmp/photos.7z", "/tmp/out", NULL);
	check (!has_unexpanded (argv));
	check (g_strcmp0 (argv[0], "7z") == 0);
	/* "x" and not "e": the paths stored in the archive are the point. */
	check (g_strcmp0 (argv[1], "x") == 0);
	check (has_arg (argv, "-y"));
	check (has_arg (argv, "-o/tmp/out"));
	/* An archive named "a?.7z" is that one archive, not a pattern. */
	check (has_arg (argv, "-spd"));
	/* Answered rather than asked, or an encrypted archive stops for a
	   password on a console that is not there. */
	check (has_arg (argv, "-p"));
	check (has_arg (argv, "--"));
	check (has_arg (argv, "/tmp/photos.7z"));
	g_strfreev (argv);

	argv = nemo_extract_build_command (NEMO_EXTRACT_BACKEND_7Z, "7z",
					   "/tmp/photos.7z", "/tmp/out", "hunter2");
	check (has_arg (argv, "-phunter2"));
	g_strfreev (argv);

	argv = nemo_extract_build_command (NEMO_EXTRACT_BACKEND_RAR, "unrar",
					   "/tmp/photos.rar", "/tmp/out", NULL);
	check (!has_unexpanded (argv));
	check (g_strcmp0 (argv[1], "x") == 0);
	check (has_arg (argv, "-y"));
	check (has_arg (argv, "-p-"));
	/* rar reads the last argument as a destination only when it ends in a
	   separator; without one it would be taken for a file to extract. */
	check (has_prefix_arg (argv, "/tmp/out" G_DIR_SEPARATOR_S));
	g_strfreev (argv);

	argv = nemo_extract_build_command (NEMO_EXTRACT_BACKEND_RAR, "unrar",
					   "/tmp/photos.rar", "/tmp/out", "hunter2");
	check (has_arg (argv, "-phunter2"));
	check (!has_arg (argv, "-p-"));
	g_strfreev (argv);

	/* libarchive is not a command, so there is no command line to build. */
	check (nemo_extract_build_command (NEMO_EXTRACT_BACKEND_LIBARCHIVE, "x",
					   "/tmp/photos.zip", "/tmp/out", NULL) == NULL);
	check (nemo_extract_build_command (NEMO_EXTRACT_BACKEND_NONE, "x",
					   "/tmp/photos.zip", "/tmp/out", NULL) == NULL);
}

int
main (void)
{
	check_recognition ();
	check_folder_names ();
	check_unique_names ();
	check_sanitize ();
	check_commands ();

	if (failures > 0) {
		g_printerr ("%d check(s) failed\n", failures);
		return EXIT_FAILURE;
	}

	g_print ("OK\n");
	return EXIT_SUCCESS;
}
