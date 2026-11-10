/* The file checksum. The point of checking it against blake3's own published
 * vectors rather than against itself is that the result has to be the same
 * number anyone else's blake3 would give: a checksum written into an extended
 * attribute outlives this program, and a future duplicate finder will compare
 * numbers taken years apart on different machines.
 *
 * The lengths are chosen to run every path the vendored code has. Up to 1024
 * bytes is one chunk and the plain C routine; past that the wide routine runs,
 * and which wide routine is decided at run time by what the processor turns out
 * to have. A length of 102400 is several levels of the tree deep. */

#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <glib/gstdio.h>

#include <libnemo-private/nemo-file-digest.h>

#include "test-scratch.h"
#include <eel/eel-analyzer.h>
#include "test-check.h"

/* blake3 fills its test inputs with 0, 1, 2 ... 250 over and over. */
static guint8 *
vector_input (gsize len)
{
	guint8 *buf = g_malloc (len > 0 ? len : 1);
	gsize   i;

	for (i = 0; i < len; i++)
		buf[i] = (guint8) (i % 251);

	return buf;
}

static char *
as_hex (const guint8 *digest)
{
	GString *s = g_string_new (NULL);
	int      i;

	for (i = 0; i < NEMO_CACHE_DIGEST_LEN; i++)
		g_string_append_printf (s, "%02x", digest[i]);

	return g_string_free (s, FALSE);
}

static const struct {
	gsize       len;
	const char *hash;
} vectors[] = {
	{ 0,      "af1349b9f5f9a1a6a0404dea36dcc9499bcb25c9adc112b7cc9a93cae41f3262" },
	{ 1,      "2d3adedff11b61f14c886e35afa036736dcd87a74d27b5c1510225d0f592e213" },
	{ 1024,   "42214739f095a406f3fc83deb889744ac00df831c10daa55189b5d121c855af7" },
	{ 3073,   "7124b49501012f81cc7f11ca069ec9226cecb8a2c850cfe644e327d22d3e1cd3" },
	{ 102400, "bc3e3d41a1146b069abffad3c0d44860cf664390afce4d9661f7902e7943e085" },
};

static void
check_known_answers (void)
{
	guint i;

	for (i = 0; i < G_N_ELEMENTS (vectors); i++) {
		EEL_ANALYZER_SKIP ("malloc-leak") // g_autofree frees it, and clang doesn't run cleanup functions
		g_autofree guint8 *in = vector_input (vectors[i].len);
		EEL_ANALYZER_END
		g_autofree char   *got = NULL;
		guint8 digest[NEMO_CACHE_DIGEST_LEN];

		nemo_file_digest_bytes (in, vectors[i].len, digest);
		got = as_hex (digest);

		check (g_str_equal (got, vectors[i].hash));
		if (!g_str_equal (got, vectors[i].hash))
			g_printerr ("  %" G_GSIZE_FORMAT " bytes: %s\n", vectors[i].len, got);
	}
}

/* Reading the file has to give the same checksum as hashing the buffer, whatever
 * the read gets broken into. */
static void
check_file_matches_buffer (const char *scratch)
{
	guint i;

	for (i = 0; i < G_N_ELEMENTS (vectors); i++) {
		g_autofree guint8 *in = vector_input (vectors[i].len);
		g_autofree char   *path = g_build_filename (scratch, "vector", NULL);
		g_autoptr (GFile)  file = NULL;
		g_autoptr (GError) error = NULL;
		g_autofree char   *got = NULL;
		guint8 digest[NEMO_CACHE_DIGEST_LEN];

		check (g_file_set_contents (path, (const char *) in, (gssize) vectors[i].len, NULL));

		file = g_file_new_for_path (path);
		check (nemo_file_digest_file (file, digest, NULL, &error));
		if (error != NULL) {
			g_printerr ("  %s\n", error->message);
			continue;
		}

		got = as_hex (digest);
		check (g_str_equal (got, vectors[i].hash));
	}
}

/* A file that isn't there is an error, not a checksum of nothing. An empty
 * file has a valid checksum, so a failure that returned one would look like
 * a real result. */
static void
check_missing_file_fails (const char *scratch)
{
	g_autofree char   *path = g_build_filename (scratch, "not-here", NULL);
	g_autoptr (GFile)  file = g_file_new_for_path (path);
	g_autoptr (GError) error = NULL;
	guint8 digest[NEMO_CACHE_DIGEST_LEN];
	guint8 empty[NEMO_CACHE_DIGEST_LEN];

	nemo_file_digest_bytes (NULL, 0, empty);

	memset (digest, 0, sizeof (digest));
	check (!nemo_file_digest_file (file, digest, NULL, &error));
	check (error != NULL);
	check (memcmp (digest, empty, sizeof (digest)) != 0);
}

/* A canceled read must not return one either. The thumbnail queue cancels on every
 * folder change, so this is the ordinary case rather than an edge. */
static void
check_cancelled_read_fails (const char *scratch)
{
	g_autofree guint8 *in = vector_input (102400);
	g_autofree char   *path = g_build_filename (scratch, "cancelled", NULL);
	g_autoptr (GFile)  file = NULL;
	g_autoptr (GCancellable) cancellable = g_cancellable_new ();
	g_autoptr (GError) error = NULL;
	guint8 digest[NEMO_CACHE_DIGEST_LEN];

	check (g_file_set_contents (path, (const char *) in, 102400, NULL));
	file = g_file_new_for_path (path);

	g_cancellable_cancel (cancellable);

	check (!nemo_file_digest_file (file, digest, cancellable, &error));
	check (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED));
}

/* The text form is what goes into an extended attribute, so it has to survive
 * a round trip and refuse anything that isn't one checksum. */
static void
check_text_form (void)
{
	guint8 digest[NEMO_CACHE_DIGEST_LEN];
	guint8 back[NEMO_CACHE_DIGEST_LEN];
	char   text[NEMO_FILE_DIGEST_TEXT_LEN];
	char   spoiled[NEMO_FILE_DIGEST_TEXT_LEN];

	nemo_file_digest_bytes ("hello", 5, digest);
	nemo_file_digest_to_text (digest, text);

	check (strlen (text) == NEMO_FILE_DIGEST_TEXT_LEN - 1);

	/* base64url, so neither of the 2 characters plain base64 uses may show
	 * up, and nor may the padding. */
	check (strchr (text, '+') == NULL);
	check (strchr (text, '/') == NULL);
	check (strchr (text, '=') == NULL);

	check (nemo_file_digest_from_text (text, back));
	check (memcmp (back, digest, sizeof (digest)) == 0);

	/* Every checksum is the same length, so a short or long one is somebody
	 * else's attribute rather than a truncated checksum to be salvaged. */
	check (!nemo_file_digest_from_text ("", back));
	check (!nemo_file_digest_from_text ("AAAA", back));
	check (!nemo_file_digest_from_text (NULL, back));

	g_strlcpy (spoiled, text, sizeof (spoiled));
	spoiled[3] = '!';
	check (!nemo_file_digest_from_text (spoiled, back));
}

int
main (int argc, char *argv[])
{
	g_autofree char *scratch = NULL;

	(void) argc;
	(void) argv;

	scratch = test_scratch_dir ("nemo-digest-XXXXXX", NULL);
	if (scratch == NULL) {
		g_printerr ("could not make a scratch dir\n");
		return 77;
	}

	check_known_answers ();
	check_file_matches_buffer (scratch);
	check_missing_file_fails (scratch);
	check_cancelled_read_fails (scratch);
	check_text_form ();

	if (failures > 0) {
		g_printerr ("%d check(s) failed\n", failures);
		return 1;
	}

	g_print ("OK\n");
	return 0;
}
