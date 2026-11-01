/* Checksums kept on the file itself. What is worth checking is not whether the
 * file system works but that a checksum is refused the moment the file stops
 * being what it was, whether that shows up as a different size, a different
 * time, or a write that only got half way.
 *
 * Plenty of file systems have nowhere to put these. The test finds out by
 * trying, and skips rather than failing.
 *
 * It passes under wine and that proves nothing. A colon is an ordinary
 * character in a Linux filename, so wine turns `f.txt:stream` into a file
 * called `f.txt:stream` sitting next to `f.txt`, and every read comes back.
 * Only a real NTFS volume says whether the Windows half works. */

#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <glib/gstdio.h>

#include <libnemo-private/nemo-file-digest.h>
#include <libnemo-private/nemo-file-xattr.h>

#include "test-scratch.h"
#include "test-check.h"

static GFile *
write_file (const char *scratch, const char *name, const char *text)
{
	g_autofree char *path = g_build_filename (scratch, name, NULL);

	if (!g_file_set_contents (path, text, -1, NULL))
		return NULL;

	return g_file_new_for_path (path);
}

static gboolean
supported_here (const char *scratch)
{
	g_autoptr (GFile) file = write_file (scratch, "probe", "x");
	g_autofree char *back = NULL;

	if (file == NULL)
		return FALSE;

	if (!nemo_file_xattr_set (file, "blake3.probe", "1"))
		return FALSE;

	back = nemo_file_xattr_get (file, "blake3.probe");

	return back != NULL && g_str_equal (back, "1");
}

/* A value written comes back, a name nobody wrote does not, and a second write
 * replaces rather than appends. */
static void
check_round_trip (const char *scratch)
{
	g_autoptr (GFile) file = write_file (scratch, "trip", "contents");
	g_autofree char *back = NULL;
	g_autofree char *shorter = NULL;

	check (file != NULL);
	if (file == NULL)
		return;

	check (nemo_file_xattr_get (file, "blake3.b64u") == NULL);

	check (nemo_file_xattr_set (file, "blake3.b64u", "a-long-first-value"));
	back = nemo_file_xattr_get (file, "blake3.b64u");
	check (back != NULL && g_str_equal (back, "a-long-first-value"));

	check (nemo_file_xattr_set (file, "blake3.b64u", "short"));
	shorter = nemo_file_xattr_get (file, "blake3.b64u");
	check (shorter != NULL && g_str_equal (shorter, "short"));
}

/* The checksum on a file is only good while the file still is what it was. */
static void
check_stale_attribute_is_refused (const char *scratch)
{
	g_autoptr (GFile) file = write_file (scratch, "stale", "hello");
	guint8 digest[NEMO_CACHE_DIGEST_LEN];
	guint8 back[NEMO_CACHE_DIGEST_LEN];
	const gint64 bytes = 5;
	const gint64 mtime = 1700000000 * (gint64) G_USEC_PER_SEC;

	check (file != NULL);
	if (file == NULL)
		return;

	nemo_file_digest_bytes ("hello", 5, digest);
	check (nemo_file_digest_write_attr (file, bytes, mtime, digest));

	check (nemo_file_digest_read_attr (file, bytes, mtime, back));
	check (memcmp (back, digest, sizeof (digest)) == 0);

	/* Edited, so the timestamp moved. */
	check (!nemo_file_digest_read_attr (file, bytes, mtime + 1, back));

	/* Edited without the timestamp moving, which some editors manage, but
	 * the size gives it away. */
	check (!nemo_file_digest_read_attr (file, bytes + 1, mtime, back));
}

/* Writing the checksum must not touch the time it was taken at. NTFS counts a
 * write to any stream as a change to the whole file, so a careless write there
 * makes every checksum stale as soon as it is written, and the file reads as edited
 * to everything else too. */
static void
check_write_keeps_file_time (const char *scratch)
{
	g_autoptr (GFile) file = write_file (scratch, "kept", "hello");
	g_autofree char *path = NULL;
	guint8 digest[NEMO_CACHE_DIGEST_LEN];
	GStatBuf before, after;

	check (file != NULL);
	if (file == NULL)
		return;

	path = g_file_get_path (file);
	check (test_set_mtime (path, 1600000000));
	check (g_stat (path, &before) == 0);

	nemo_file_digest_bytes ("hello", 5, digest);
	check (nemo_file_digest_write_attr (file, 5, (gint64) before.st_mtime * G_USEC_PER_SEC, digest));

	check (g_stat (path, &after) == 0);
	check (after.st_mtime == before.st_mtime);
	check (after.st_mtime == 1600000000);
}

/* Each of the three attributes is written on its own, so a process that dies
 * between two of them leaves a state a later reader has to throw away rather
 * than believe. Which order they go in is what decides whether that is always
 * possible, and no test can hold that part - the unsafe state is one a read
 * cannot tell from a good one. `fCheckDigestAttrOrder` in lint-c.bash holds it
 * instead. What is checked here is the half a read can see. */
static void
check_torn_write_is_refused (const char *scratch)
{
	g_autoptr (GFile) file = write_file (scratch, "torn", "one");
	guint8 first[NEMO_CACHE_DIGEST_LEN];
	guint8 second[NEMO_CACHE_DIGEST_LEN];
	guint8 back[NEMO_CACHE_DIGEST_LEN];
	char   text[NEMO_FILE_DIGEST_TEXT_LEN];
	const gint64 mtime_one = 1700000000 * (gint64) G_USEC_PER_SEC;
	const gint64 mtime_two = 1700000009 * (gint64) G_USEC_PER_SEC;

	check (file != NULL);
	if (file == NULL)
		return;

	nemo_file_digest_bytes ("one", 3, first);
	nemo_file_digest_bytes ("two!", 4, second);

	check (nemo_file_digest_write_attr (file, 3, mtime_one, first));

	/* Now the file is edited and the write of the new checksum stops after
	 * the first of its three attributes. */
	nemo_file_digest_to_text (second, text);
	check (nemo_file_xattr_set (file, "blake3.b64u", text));

	/* A reader always asks about the file as it is now, and the file is now
	 * four bytes stamped later. The new checksum is sitting there next to a
	 * size and a time that predate it, so nothing comes back. */
	check (!nemo_file_digest_read_attr (file, 4, mtime_two, back));

	/* Finishing the write makes it good again. */
	check (nemo_file_digest_write_attr (file, 4, mtime_two, second));
	check (nemo_file_digest_read_attr (file, 4, mtime_two, back));
	check (memcmp (back, second, sizeof (second)) == 0);
}

/* A checksum half written by something else, or an attribute that is not one at
 * all, is not a checksum. */
static void
check_junk_is_refused (const char *scratch)
{
	g_autoptr (GFile) file = write_file (scratch, "junk", "xx");
	guint8 back[NEMO_CACHE_DIGEST_LEN];
	const gint64 mtime = 1700000000 * (gint64) G_USEC_PER_SEC;

	check (file != NULL);
	if (file == NULL)
		return;

	/* The checksum alone, with nothing to say what it describes. */
	check (nemo_file_xattr_set (file, "blake3.b64u", "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"));
	check (!nemo_file_digest_read_attr (file, 2, mtime, back));

	/* All three, but the checksum is somebody else's idea of a value. */
	check (nemo_file_xattr_set (file, "blake3.b64u", "not a checksum"));
	check (nemo_file_xattr_set (file, "blake3.bytes", "2"));
	check (nemo_file_xattr_set (file, "blake3.mtime", "1700000000000000"));
	check (!nemo_file_digest_read_attr (file, 2, mtime, back));
}

int
main (int argc, char *argv[])
{
	g_autofree char *scratch = NULL;

	(void) argc;
	(void) argv;

	scratch = test_scratch_dir ("nemo-xattr-XXXXXX", NULL);
	if (scratch == NULL) {
		g_printerr ("could not make a scratch dir\n");
		return 77;
	}

	if (!supported_here (scratch)) {
		g_print ("no extended attributes on %s, skipping\n", scratch);
		return 77;
	}

	check_round_trip (scratch);
	check_stale_attribute_is_refused (scratch);
	check_write_keeps_file_time (scratch);
	check_torn_write_is_refused (scratch);
	check_junk_is_refused (scratch);

	if (failures > 0) {
		g_printerr ("%d check(s) failed\n", failures);
		return 1;
	}

	g_print ("OK\n");
	return 0;
}
