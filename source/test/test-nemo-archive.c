/* The parts of archive creation that can be decided without writing anything:
 * which program would be reached for given a format and a set of options, what
 * switches it would be handed, how the name and the extension move together,
 * and how a volume size is read. The writing itself needs real files and a real
 * job queue, so it is not covered here. */

#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <gio/gio.h>

#include <libnemo-private/nemo-archive.h>
#include <libnemo-private/nemo-global-preferences.h>

#include "test-scratch.h"
#include "test-check.h"
#include "test-argv.h"

static void
check_extensions (void)
{
	char *text;

	/* Changing the format swaps the extension rather than stacking one on
	   top of the other, and the two-part ones are read whole. */
	text = nemo_archive_apply_extension ("photos.tar.gz", NEMO_ARCHIVE_FORMAT_ZIP);
	check (g_strcmp0 (text, "photos.zip") == 0);
	g_free (text);

	text = nemo_archive_apply_extension ("photos.zip", NEMO_ARCHIVE_FORMAT_TAR_XZ);
	check (g_strcmp0 (text, "photos.tar.xz") == 0);
	g_free (text);

	/* A dot in the name that is not an archive suffix stays put. */
	text = nemo_archive_apply_extension ("report.v2", NEMO_ARCHIVE_FORMAT_7Z);
	check (g_strcmp0 (text, "report.v2.7z") == 0);
	g_free (text);

	/* A file actually called ".zip" has no base to strip. */
	text = nemo_archive_strip_extension (".zip");
	check (g_strcmp0 (text, ".zip") == 0);
	g_free (text);

	/* Case does not matter on the way in, but the suffix we write is ours. */
	text = nemo_archive_apply_extension ("Notes.ZIP", NEMO_ARCHIVE_FORMAT_ZIP);
	check (g_strcmp0 (text, "Notes.zip") == 0);
	g_free (text);
}

/* Builds a list of GFile * from uris. */
static GList *
files_for_uris (const char * const *uris)
{
	GList *files = NULL;
	int i;

	for (i = 0; uris[i] != NULL; i++) {
		files = g_list_prepend (files, g_file_new_for_uri (uris[i]));
	}

	return g_list_reverse (files);
}

/* The name offered for a selection. One item is named after itself; several
 * borrow the folder's name only when they are the whole folder. */
static void
check_names (void)
{
	static const char * const one[] = { "file:///tmp/photos/holiday.jpg", NULL };
	static const char * const one_folder[] = { "file:///tmp/photos", NULL };
	static const char * const several[] = {
		"file:///tmp/photos/one.jpg", "file:///tmp/photos/two.jpg", NULL
	};
	static const char * const root[] = { "file:///", NULL };
	GList *files;
	char *text;

	/* A single file, extension and all, keeps its whole name - "holiday.jpg"
	   compresses to "holiday.jpg.zip", not "holiday.zip". */
	files = files_for_uris (one);
	text = nemo_archive_suggest_name (files, FALSE, NEMO_ARCHIVE_FORMAT_ZIP);
	check (g_strcmp0 (text, "holiday.jpg.zip") == 0);
	g_free (text);
	g_list_free_full (files, g_object_unref);

	/* A single folder is named after itself, and the two-part suffix is
	   carried whole. */
	files = files_for_uris (one_folder);
	text = nemo_archive_suggest_name (files, FALSE, NEMO_ARCHIVE_FORMAT_TAR_GZ);
	check (g_strcmp0 (text, "photos.tar.gz") == 0);
	g_free (text);
	g_list_free_full (files, g_object_unref);

	/* Everything in the folder: the archive takes the folder's name. */
	files = files_for_uris (several);
	text = nemo_archive_suggest_name (files, TRUE, NEMO_ARCHIVE_FORMAT_ZIP);
	check (g_strcmp0 (text, "photos.zip") == 0);
	g_free (text);

	/* The same files as a part of the folder: no name, so the user picks
	   one rather than getting the folder's by accident. */
	text = nemo_archive_suggest_name (files, FALSE, NEMO_ARCHIVE_FORMAT_ZIP);
	check (text == NULL);
	g_free (text);
	g_list_free_full (files, g_object_unref);

	/* A root has no name to borrow. */
	files = files_for_uris (root);
	text = nemo_archive_suggest_name (files, FALSE, NEMO_ARCHIVE_FORMAT_ZIP);
	check (text == NULL);
	g_free (text);
	g_list_free_full (files, g_object_unref);

	/* Nothing selected cannot be compressed, so there is nothing to call it. */
	text = nemo_archive_suggest_name (NULL, TRUE, NEMO_ARCHIVE_FORMAT_ZIP);
	check (text == NULL);
	g_free (text);
}

/* The name each archive gets when a selection is compressed separately. */
static void
check_each_names (void)
{
	char *text;

	/* Named after the item, extension and all, the same as a single
	   selection would be offered. */
	text = nemo_archive_each_name ("holiday.jpg", NEMO_ARCHIVE_FORMAT_ZIP);
	check (g_strcmp0 (text, "holiday.jpg.zip") == 0);
	g_free (text);

	text = nemo_archive_each_name ("photos", NEMO_ARCHIVE_FORMAT_TAR_GZ);
	check (g_strcmp0 (text, "photos.tar.gz") == 0);
	g_free (text);

	/* An archive suffix is kept rather than swapped. There is no name field
	   to correct here, so swapping it would put the new archive on top of
	   the file being read. */
	text = nemo_archive_each_name ("old.zip", NEMO_ARCHIVE_FORMAT_ZIP);
	check (g_strcmp0 (text, "old.zip.zip") == 0);
	check (g_strcmp0 (text, "old.zip") != 0);
	g_free (text);

	text = nemo_archive_each_name ("notes.rar", NEMO_ARCHIVE_FORMAT_ZIP);
	check (g_strcmp0 (text, "notes.rar.zip") == 0);
	g_free (text);

	/* Nothing an archive could be named after or written to. */
	check (nemo_archive_each_name (NULL, NEMO_ARCHIVE_FORMAT_ZIP) == NULL);
	check (nemo_archive_each_name ("", NEMO_ARCHIVE_FORMAT_ZIP) == NULL);
	check (nemo_archive_each_name (".", NEMO_ARCHIVE_FORMAT_ZIP) == NULL);
	check (nemo_archive_each_name ("photos/holiday.jpg", NEMO_ARCHIVE_FORMAT_ZIP) == NULL);
}

static void
check_sizes (void)
{
	guint64 bytes = 0;

	/* The suffixes an archiver uses, and the "B" that comes with them. */
	check (nemo_archive_parse_size ("700 MB", &bytes) && bytes == 700ULL * 1024 * 1024);
	check (nemo_archive_parse_size ("700m", &bytes) && bytes == 700ULL * 1024 * 1024);
	check (nemo_archive_parse_size ("1 GB", &bytes) && bytes == 1024ULL * 1024 * 1024);
	check (nemo_archive_parse_size ("1.5g", &bytes) && bytes == 1610612736ULL);

	/* A bare number is bytes, not the last unit used. */
	check (nemo_archive_parse_size ("4096", &bytes) && bytes == 4096);

	/* Nothing usable is a refusal, not a zero - a zero would silently mean
	   "one volume" and quietly drop the split. */
	check (!nemo_archive_parse_size ("", &bytes));
	check (!nemo_archive_parse_size ("lots", &bytes));
	check (!nemo_archive_parse_size ("100 quatloos", &bytes));
	check (!nemo_archive_parse_size ("0", &bytes));
	check (!nemo_archive_parse_size ("-5m", &bytes));

	/* MB and MiB have always meant the same binary unit here, so the "i"
	   spelling the dropdown offers has to be readable too. */
	check (nemo_archive_parse_size ("2 GiB", &bytes) && bytes == 2ULL * 1024 * 1024 * 1024);
	check (nemo_archive_parse_size ("4480 MiB", &bytes) && bytes == 4480ULL * 1024 * 1024);
	check (nemo_archive_parse_size ("100mib", &bytes) && bytes == 100ULL * 1024 * 1024);

	/* A size written out comes back the same way it went in, and says which
	   unit it means. */
	{
		char *text = nemo_archive_format_size (700ULL * 1024 * 1024);

		check (g_strcmp0 (text, "700 MiB") == 0);
		g_free (text);

		text = nemo_archive_format_size (2ULL * 1024 * 1024 * 1024);
		check (g_strcmp0 (text, "2 GiB") == 0);
		g_free (text);
	}
}

static void
check_volume_names (void)
{
	char *text;

	/* 7z hangs the number off the end of the whole name. */
	text = nemo_archive_volume_name ("photos.7z", NEMO_ARCHIVE_BACKEND_7Z, 1, 3);
	check (g_strcmp0 (text, "photos.7z.001") == 0);
	g_free (text);

	text = nemo_archive_volume_name ("photos.zip", NEMO_ARCHIVE_BACKEND_7Z, 12, 3);
	check (g_strcmp0 (text, "photos.zip.012") == 0);
	g_free (text);

	/* rar puts it in front of the extension instead. */
	text = nemo_archive_volume_name ("photos.rar", NEMO_ARCHIVE_BACKEND_RAR, 1, 1);
	check (g_strcmp0 (text, "photos.part1.rar") == 0);
	g_free (text);

	text = nemo_archive_volume_name ("photos.rar", NEMO_ARCHIVE_BACKEND_RAR, 3, 2);
	check (g_strcmp0 (text, "photos.part03.rar") == 0);
	g_free (text);

	/* Only the last dot is the extension, and a name with none still works. */
	text = nemo_archive_volume_name ("backup.2026.rar", NEMO_ARCHIVE_BACKEND_RAR, 1, 1);
	check (g_strcmp0 (text, "backup.2026.part1.rar") == 0);
	g_free (text);

	text = nemo_archive_volume_name ("photos", NEMO_ARCHIVE_BACKEND_RAR, 1, 1);
	check (g_strcmp0 (text, "photos.part1") == 0);
	g_free (text);
}

/* The bug this covers: a split that fits in one volume still came out numbered,
   so an archive nothing was really split into arrived as "name.7z.001". */
static void
check_volume_collapse (const char *scratch)
{
	struct {
		NemoArchiveBackend backend;
		const char *archive;
		const char *first;
		const char *second;
	} cases[] = {
		{ NEMO_ARCHIVE_BACKEND_7Z,  "one.7z",  "one.7z.001",     NULL },
		{ NEMO_ARCHIVE_BACKEND_7Z,  "many.7z", "many.7z.001",    "many.7z.002" },
		{ NEMO_ARCHIVE_BACKEND_RAR, "one.rar", "one.part1.rar",  NULL },
		{ NEMO_ARCHIVE_BACKEND_RAR, "many.rar", "many.part1.rar", "many.part2.rar" },
	};
	guint i;

	for (i = 0; i < G_N_ELEMENTS (cases); i++) {
		GFile *dir = g_file_new_for_path (scratch);
		GFile *destination = g_file_get_child (dir, cases[i].archive);
		GFile *first = g_file_get_child (dir, cases[i].first);
		GFile *written;
		char *name;

		g_file_replace_contents (first, "x", 1, NULL, FALSE, G_FILE_CREATE_NONE,
					 NULL, NULL, NULL);

		if (cases[i].second != NULL) {
			GFile *second = g_file_get_child (dir, cases[i].second);

			g_file_replace_contents (second, "x", 1, NULL, FALSE,
						 G_FILE_CREATE_NONE, NULL, NULL, NULL);
			g_object_unref (second);
		}

		written = nemo_archive_collapse_volume (destination, cases[i].backend);
		name = g_file_get_basename (written);

		if (cases[i].second == NULL) {
			/* The only volume takes the name that was asked for. */
			check (g_strcmp0 (name, cases[i].archive) == 0);
			check (g_file_query_exists (destination, NULL));
			check (!g_file_query_exists (first, NULL));
		} else {
			/* A real split keeps its numbering, and the answer is the
			   volume unpacking has to start from. */
			check (g_strcmp0 (name, cases[i].first) == 0);
			check (!g_file_query_exists (destination, NULL));
			check (g_file_query_exists (first, NULL));
		}

		g_free (name);
		g_object_unref (written);
		g_object_unref (first);
		g_object_unref (destination);
		g_object_unref (dir);
	}
}

static void
check_backends (void)
{
	NemoArchiveOptions options;

	/* The tar family and a plain zip are libarchive's, and it is always
	   linked in - none of them can depend on an installed program. */
	nemo_archive_options_init (&options);
	check (nemo_archive_pick_backend (NEMO_ARCHIVE_FORMAT_TAR, &options) ==
	       NEMO_ARCHIVE_BACKEND_LIBARCHIVE);
	check (nemo_archive_pick_backend (NEMO_ARCHIVE_FORMAT_TAR_GZ, &options) ==
	       NEMO_ARCHIVE_BACKEND_LIBARCHIVE);
	check (nemo_archive_pick_backend (NEMO_ARCHIVE_FORMAT_ZIP, &options) ==
	       NEMO_ARCHIVE_BACKEND_LIBARCHIVE);
	check (nemo_archive_format_available (NEMO_ARCHIVE_FORMAT_TAR));

	/* An encrypted zip is still libarchive's - it writes AES itself. */
	options.password = g_strdup ("secret");
	check (nemo_archive_pick_backend (NEMO_ARCHIVE_FORMAT_ZIP, &options) ==
	       NEMO_ARCHIVE_BACKEND_LIBARCHIVE);

	/* An encrypted 7z is not: libarchive has no write-side encryption for
	   it at all, so an encrypted one has to go to 7z or come back as
	   nothing. Stated against the capability rather than against the
	   choice, because on a box that has 7z the choice would come out right
	   for the wrong reason. */
	check ((nemo_archive_backend_caps (NEMO_ARCHIVE_FORMAT_7Z,
					   NEMO_ARCHIVE_BACKEND_LIBARCHIVE) &
		NEMO_ARCHIVE_CAP_PASSWORD) == 0);
	check (nemo_archive_pick_backend (NEMO_ARCHIVE_FORMAT_7Z, &options) !=
	       NEMO_ARCHIVE_BACKEND_LIBARCHIVE);
	nemo_archive_options_clear (&options);

	/* Same for splitting, which libarchive cannot write in any format. */
	nemo_archive_options_init (&options);
	options.split_size = 700ULL * 1024 * 1024;
	check ((nemo_archive_backend_caps (NEMO_ARCHIVE_FORMAT_ZIP,
					   NEMO_ARCHIVE_BACKEND_LIBARCHIVE) &
		NEMO_ARCHIVE_CAP_SPLIT) == 0);
	check (nemo_archive_pick_backend (NEMO_ARCHIVE_FORMAT_ZIP, &options) !=
	       NEMO_ARCHIVE_BACKEND_LIBARCHIVE);
	nemo_archive_options_clear (&options);

	/* rar is never libarchive's to write, whatever the options. */
	nemo_archive_options_init (&options);
	check (nemo_archive_pick_backend (NEMO_ARCHIVE_FORMAT_RAR, &options) !=
	       NEMO_ARCHIVE_BACKEND_LIBARCHIVE);
	check (nemo_archive_backend_caps (NEMO_ARCHIVE_FORMAT_RAR,
					  NEMO_ARCHIVE_BACKEND_LIBARCHIVE) == 0);

	/* Storing links, a solid archive and a recovery record are preferences,
	   not requirements: a plain tar still gets written even with all three
	   asked for, and by libarchive, which can honor none of them. */
	options.store_links = TRUE;
	options.solid = TRUE;
	options.recovery_record = TRUE;
	check (nemo_archive_pick_backend (NEMO_ARCHIVE_FORMAT_TAR, &options) ==
	       NEMO_ARCHIVE_BACKEND_LIBARCHIVE);
	/* Same for a 7z - which writer gets it depends on what is installed,
	   but it does get written. */
	check (nemo_archive_pick_backend (NEMO_ARCHIVE_FORMAT_7Z, &options) !=
	       NEMO_ARCHIVE_BACKEND_NONE);
	nemo_archive_options_clear (&options);

	/* A link 7z keeps on Windows comes back as junk anywhere but 7-Zip
	   there, so it does not offer to keep them. */
#ifdef G_OS_WIN32
	check ((nemo_archive_backend_caps (NEMO_ARCHIVE_FORMAT_7Z, NEMO_ARCHIVE_BACKEND_7Z) &
		NEMO_ARCHIVE_CAP_STORE_LINKS) == 0);
	check ((nemo_archive_backend_caps (NEMO_ARCHIVE_FORMAT_ZIP, NEMO_ARCHIVE_BACKEND_7Z) &
		NEMO_ARCHIVE_CAP_STORE_LINKS) == 0);
#else
	check ((nemo_archive_backend_caps (NEMO_ARCHIVE_FORMAT_7Z, NEMO_ARCHIVE_BACKEND_7Z) &
		NEMO_ARCHIVE_CAP_STORE_LINKS) != 0);
#endif
}

static void
check_commands (void)
{
	NemoArchiveOptions options;
	GList *names = NULL;
	char **argv;

	names = g_list_append (names, (gpointer) "one.txt");
	names = g_list_append (names, (gpointer) "a folder");

	/* rar: every option the user asked for has to reach the command line,
	   or it is silently not honored. */
	nemo_archive_options_init (&options);
	options.format = NEMO_ARCHIVE_FORMAT_RAR;
	options.level = NEMO_ARCHIVE_LEVEL_MAX;
	options.password = g_strdup ("secret");
	options.encrypt_names = TRUE;
	options.split_size = 100ULL * 1024 * 1024;
	options.solid = TRUE;
	options.dedupe = TRUE;
	options.recovery_record = TRUE;
	options.lock = TRUE;

	argv = nemo_archive_build_command (NEMO_ARCHIVE_BACKEND_RAR, options.format, &options,
					   "rar", "/tmp/out.rar", names, NULL);
	if (argv == NULL) {
		g_printerr ("FAIL %s:%d: no rar command line built\n", __FILE__, __LINE__);
		failures++;
		return;
	}

	check (g_strcmp0 (argv[0], "rar") == 0);
	check (g_strcmp0 (argv[1], "a") == 0);
	check (!has_unexpanded (argv));
	check (has_arg (argv, "-m5"));
	check (has_prefix_arg (argv, "-mt"));
	/* Encrypted names means -hp, and -p would leave the list readable. */
	check (has_arg (argv, "-hpsecret"));
	check (!has_arg (argv, "-psecret"));
	check (has_arg (argv, "-v104857600b"));
	check (has_arg (argv, "-s"));
	check (has_arg (argv, "-oi"));
	check (has_prefix_arg (argv, "-rr"));
	check (has_arg (argv, "-k"));
	/* The archive and the names come after the end-of-switches marker, so a
	   file whose name starts with a dash is a file. */
	check (has_arg (argv, "--"));
	check (has_arg (argv, "/tmp/out.rar"));
	check (has_arg (argv, "a folder"));
	g_strfreev (argv);
	nemo_archive_options_clear (&options);

	/* The options left off have to be absent, not defaulted on by the tool:
	   rar makes solid archives by default, so -s- has to be explicit. */
	nemo_archive_options_init (&options);
	options.format = NEMO_ARCHIVE_FORMAT_RAR;
	options.solid = FALSE;
	options.recovery_record = FALSE;

	argv = nemo_archive_build_command (NEMO_ARCHIVE_BACKEND_RAR, options.format, &options,
					   "rar", "/tmp/out.rar", names, NULL);
	check (has_arg (argv, "-s-"));
	check (!has_prefix_arg (argv, "-rr"));
	check (!has_arg (argv, "-k"));
	check (!has_prefix_arg (argv, "-p"));
	check (!has_prefix_arg (argv, "-hp"));
	g_strfreev (argv);
	nemo_archive_options_clear (&options);

	/* Links: -ol keeps them. Not keeping them means following, which is what
	   rar does with no switch; -ola and -ol- would keep or drop them. A linked
	   folder that is not followed is named as left out. */
	{
		GList *skip = g_list_append (NULL, (gpointer) "a folder/linked");

		nemo_archive_options_init (&options);
		options.format = NEMO_ARCHIVE_FORMAT_RAR;
		options.store_links = TRUE;
		argv = nemo_archive_build_command (NEMO_ARCHIVE_BACKEND_RAR, options.format, &options,
						   "rar", "/tmp/out.rar", names, NULL);
		check (has_arg (argv, "-ol"));
		check (!has_arg (argv, "-ola") && !has_arg (argv, "-ol-"));
		g_strfreev (argv);

		options.store_links = FALSE;
		options.follow_link_dirs = TRUE;
		argv = nemo_archive_build_command (NEMO_ARCHIVE_BACKEND_RAR, options.format, &options,
						   "rar", "/tmp/out.rar", names, NULL);
		check (!has_prefix_arg (argv, "-ol"));
		g_strfreev (argv);

		options.follow_link_dirs = FALSE;
		argv = nemo_archive_build_command (NEMO_ARCHIVE_BACKEND_RAR, options.format, &options,
						   "rar", "/tmp/out.rar", names, skip);
		check (!has_prefix_arg (argv, "-ol"));
		check (has_arg (argv, "-xa folder/linked"));
		g_strfreev (argv);

		options.format = NEMO_ARCHIVE_FORMAT_7Z;
		argv = nemo_archive_build_command (NEMO_ARCHIVE_BACKEND_7Z, options.format, &options,
						   "7z", "/tmp/out.7z", names, skip);
		check (has_arg (argv, "-x!a folder/linked"));
		check (!has_arg (argv, "-snl"));
		g_strfreev (argv);

		/* Kept only where 7z claims it can keep them, which it does not
		   on Windows. */
		options.store_links = TRUE;
		argv = nemo_archive_build_command (NEMO_ARCHIVE_BACKEND_7Z, options.format, &options,
						   "7z", "/tmp/out.7z", names, NULL);
#ifdef G_OS_WIN32
		check (!has_arg (argv, "-snl"));
#else
		check (has_arg (argv, "-snl"));
#endif
		g_strfreev (argv);

		nemo_archive_options_clear (&options);
		g_list_free (skip);
	}

	/* 7z: the level is on its own scale, and encrypted headers are -mhe. */
	nemo_archive_options_init (&options);
	options.format = NEMO_ARCHIVE_FORMAT_7Z;
	options.level = NEMO_ARCHIVE_LEVEL_MAX;
	options.password = g_strdup ("secret");
	options.encrypt_names = TRUE;
	options.solid = TRUE;

	argv = nemo_archive_build_command (NEMO_ARCHIVE_BACKEND_7Z, options.format, &options,
					   "7z", "/tmp/out.7z", names, NULL);
	check (!has_unexpanded (argv));
	check (has_arg (argv, "-t7z"));
	check (has_arg (argv, "-mx=9"));
	check (has_prefix_arg (argv, "-mmt="));
	check (has_arg (argv, "-psecret"));
	check (has_arg (argv, "-mhe=on"));
	check (has_arg (argv, "-ms=on"));
	/* Names are names. Without -spd a left-out "a*" also leaves out "abc". */
	check (has_arg (argv, "-spd"));
	g_strfreev (argv);
	nemo_archive_options_clear (&options);

	/* Store means store all the way down: level 0 is -mx=0, not the default. */
	nemo_archive_options_init (&options);
	options.format = NEMO_ARCHIVE_FORMAT_7Z;
	options.level = NEMO_ARCHIVE_LEVEL_STORE;

	argv = nemo_archive_build_command (NEMO_ARCHIVE_BACKEND_7Z, options.format, &options,
					   "7z", "/tmp/out.7z", names, NULL);
	check (has_arg (argv, "-mx=0"));
	check (has_arg (argv, "-ms=off"));
	g_strfreev (argv);
	nemo_archive_options_clear (&options);

	/* A zip written by 7z is -tzip; the format is not read off the name. */
	nemo_archive_options_init (&options);
	options.format = NEMO_ARCHIVE_FORMAT_ZIP;
	options.split_size = 1024;

	argv = nemo_archive_build_command (NEMO_ARCHIVE_BACKEND_7Z, options.format, &options,
					   "7z", "/tmp/out.zip", names, NULL);
	check (!has_unexpanded (argv));
	check (has_arg (argv, "-tzip"));
	check (has_arg (argv, "-v1024b"));
	g_strfreev (argv);
	nemo_archive_options_clear (&options);

	/* libarchive is not a command, so there is no command line to build. */
	nemo_archive_options_init (&options);
	check (nemo_archive_build_command (NEMO_ARCHIVE_BACKEND_LIBARCHIVE, options.format,
					   &options, "x", "/tmp/out.zip", names, NULL) == NULL);
	nemo_archive_options_clear (&options);

	g_list_free (names);
}

/* What the two tools print when they pass over a link that leads nowhere,
   taken from 7-Zip 25.01 and RAR 7.20. Only that, and only for links the
   scan found, is a warning to live with; anything else fails the archive. */
static void
check_skipped_links (void)
{
	static const char seven_dangling[] =
		"\n7-Zip 25.01 (x64) : Copyright (c) 1999-2025 Igor Pavlov : 2025-08-03\n"
		"Scanning the drive:\n"
		"\nWARNING: errno=2 : No such file or directory\nsel/dang\n\n"
		"\nWARNING: errno=2 : No such file or directory\nsel/sub/dang2\n\n"
		"2 folders, 2 files, 16 bytes (1 KiB)\n\nCreating archive: x.7z\n\n"
		"Files read from disk: 2\nArchive size: 203 bytes (1 KiB)\n\n"
		"Scan WARNINGS for files and folders:\n\n"
		"sel/dang : errno=2 : No such file or directory\n"
		"sel/sub/dang2 : errno=2 : No such file or directory\n"
		"----------------\nScan WARNINGS: 2\n";
	/* A folder it could not list and a file it could not read, beside the
	   dangling link. */
	static const char seven_more[] =
		"Scanning the drive:\n"
		"\nWARNING: errno=2 : No such file or directory\nsel/dang\n\n"
		"\nWARNING: errno=13 : Permission denied\nsel/lock/\n\n"
		"Creating archive: x.7z\n\n"
		"WARNING: errno=13 : Permission denied\nsel/secret\n\n"
		"Scan WARNINGS for files and folders:\n\n"
		"sel/dang : errno=2 : No such file or directory\n"
		"sel/lock/ : errno=13 : Permission denied\n"
		"----------------\nScan WARNINGS: 2\n\n"
		"WARNINGS for files:\n\n"
		"sel/secret : errno=13 : Permission denied\n"
		"----------------\nWARNING: Cannot open 1 file\n";
	static const char rar_dangling[] =
		"\nRAR 7.20   Copyright (c) 1993-2026 Alexander Roshal   1 Feb 2026\n"
		"Cannot open sel/sub/dang2\nNo such file or directory\n"
		"Cannot open sel/dang\nNo such file or directory\n"
		"Creating archive x.rar\n\n"
		"Adding    sel/f                                                           18%  OK \n"
		"Adding    sel                                                              OK \nDone\n";
	static const char rar_more[] =
		"Cannot read contents of /tmp/x/sel/lock\nPermission denied\n"
		"Cannot open sel/dang\nNo such file or directory\n"
		"Creating archive x.rar\n\n"
		"Cannot open sel/secret\nPermission denied\n"
		"WARNING: Cannot open 1 file\nDone\n";
	GList *both = NULL;
	GList *one = NULL;

	both = g_list_append (both, (gpointer) "sel/dang");
	both = g_list_append (both, (gpointer) "sel/sub/dang2");
	one = g_list_append (one, (gpointer) "sel/dang");

	check (nemo_archive_only_skipped_links (NEMO_ARCHIVE_BACKEND_7Z, 1, seven_dangling, both));
	check (nemo_archive_only_skipped_links (NEMO_ARCHIVE_BACKEND_RAR, 6, rar_dangling, both));

	/* A warning about a link the scan did not find is not ours to excuse. */
	check (!nemo_archive_only_skipped_links (NEMO_ARCHIVE_BACKEND_7Z, 1, seven_dangling, one));
	check (!nemo_archive_only_skipped_links (NEMO_ARCHIVE_BACKEND_RAR, 6, rar_dangling, one));
	check (!nemo_archive_only_skipped_links (NEMO_ARCHIVE_BACKEND_7Z, 1, seven_dangling, NULL));

	/* A real read error beside the link still fails. */
	check (!nemo_archive_only_skipped_links (NEMO_ARCHIVE_BACKEND_7Z, 1, seven_more, one));
	check (!nemo_archive_only_skipped_links (NEMO_ARCHIVE_BACKEND_RAR, 6, rar_more, one));

	/* Only the warning status; an error status is never excused. */
	check (!nemo_archive_only_skipped_links (NEMO_ARCHIVE_BACKEND_7Z, 2, seven_dangling, both));
	check (!nemo_archive_only_skipped_links (NEMO_ARCHIVE_BACKEND_RAR, 1, rar_dangling, both));
	check (!nemo_archive_only_skipped_links (NEMO_ARCHIVE_BACKEND_RAR, 6, seven_dangling, both));

	/* Warning status with nothing named at all. */
	check (!nemo_archive_only_skipped_links (NEMO_ARCHIVE_BACKEND_7Z, 1, "Everything is Ok\n", both));
	check (!nemo_archive_only_skipped_links (NEMO_ARCHIVE_BACKEND_RAR, 6, "Done\n", both));
	/* A count with no name after it. */
	check (!nemo_archive_only_skipped_links (NEMO_ARCHIVE_BACKEND_7Z, 1,
						 "sel/dang\nWARNING: Cannot open 1 file\n", both));

	/* A name with the separator in it. */
	{
		GList *colon = g_list_append (NULL, (gpointer) "sel/a : b");

		check (nemo_archive_only_skipped_links (NEMO_ARCHIVE_BACKEND_7Z, 1,
							"WARNING: errno=2 : No such file or directory\nsel/a : b\n\n"
							"Scan WARNINGS for files and folders:\n\n"
							"sel/a : b : errno=2 : No such file or directory\n"
							"----------------\nScan WARNINGS: 1\n", colon));
		g_list_free (colon);
	}

#ifdef G_OS_WIN32
	/* The same, with the separators Windows prints. */
	check (nemo_archive_only_skipped_links (NEMO_ARCHIVE_BACKEND_7Z, 1,
						"Scan WARNINGS for files and folders:\r\n\r\n"
						"sel\\dang : The system cannot find the file specified.\r\n"
						"----------------\r\nScan WARNINGS: 1\r\n", one));
	check (nemo_archive_only_skipped_links (NEMO_ARCHIVE_BACKEND_RAR, 6,
						"Cannot open sel\\dang\r\nThe system cannot find the file specified.\r\nDone\r\n",
						one));
#endif

	g_list_free (one);
	g_list_free (both);
}

/* The share of the machine any compression is allowed. Nothing has read the
 * settings file here, so this is the shipped 50% - and the point of the check
 * is the arithmetic around it: never zero on a single-core box, never more
 * cores than the machine has, and rounded up rather than down. */
static void
check_cpu_share (void)
{
	int cores = (int) g_get_num_processors ();
	int threads = nemo_global_preferences_get_cpu_thread_count ();

	check (threads >= 1);
	check (threads <= cores);
	check (threads == MAX (1, (cores * 50 + 99) / 100));
}

int
main (int argc, char *argv[])
{
	char *scratch = test_scratch_dir ("nemo-archive-XXXXXX", NULL);

	check_extensions ();
	check_names ();
	check_each_names ();
	check_sizes ();
	check_volume_names ();
	check_volume_collapse (scratch);
	check_backends ();
	check_commands ();
	check_skipped_links ();
	check_cpu_share ();

	g_free (scratch);

	if (failures > 0) {
		g_printerr ("%d check(s) failed\n", failures);
		return EXIT_FAILURE;
	}

	g_print ("OK\n");
	return EXIT_SUCCESS;
}
