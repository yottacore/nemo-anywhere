/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-file-digest.c - a checksum of what is in a file.

   Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu).

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; version 2 of the
   License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public
   License along with this program; if not, write to the
   Free Software Foundation, Inc., 51 Franklin Street, Suite 500,
   Boston, MA 02110-1335, USA.
*/

#include <config.h>

#include "nemo-file-digest.h"

#include "nemo-file-xattr.h"

#include <blake3.h>
#include <string.h>

/* Big enough that blake3 gets whole 16-chunk batches to work on, small enough
 * to be nothing on a folder full of pictures. */
#define READ_CHUNK (64 * 1024)

void
nemo_file_digest_bytes (gconstpointer data, gsize len, guint8 *digest)
{
	blake3_hasher hasher;

	g_return_if_fail (data != NULL || len == 0);
	g_return_if_fail (digest != NULL);

	blake3_hasher_init (&hasher);
	blake3_hasher_update (&hasher, data, len);
	blake3_hasher_finalize (&hasher, digest, NEMO_CACHE_DIGEST_LEN);
}

gboolean
nemo_file_digest_file (GFile        *file,
		       guint8       *digest,
		       GCancellable *cancellable,
		       GError      **error)
{
	g_autoptr (GFileInputStream) in = NULL;
	g_autofree guint8 *buffer = NULL;
	blake3_hasher hasher;
	gboolean      ok = TRUE;

	g_return_val_if_fail (G_IS_FILE (file), FALSE);
	g_return_val_if_fail (digest != NULL, FALSE);

	in = g_file_read (file, cancellable, error);
	if (in == NULL)
		return FALSE;

	blake3_hasher_init (&hasher);
	buffer = g_malloc (READ_CHUNK);

	for (;;) {
		gssize n = g_input_stream_read (G_INPUT_STREAM (in), buffer, READ_CHUNK,
						cancellable, error);

		if (n < 0) {
			ok = FALSE;
			break;
		}
		if (n == 0)
			break;

		blake3_hasher_update (&hasher, buffer, (size_t) n);
	}

	g_input_stream_close (G_INPUT_STREAM (in), NULL, NULL);

	if (!ok)
		return FALSE;

	blake3_hasher_finalize (&hasher, digest, NEMO_CACHE_DIGEST_LEN);

	return TRUE;
}

void
nemo_file_digest_to_text (const guint8 *digest, char *text)
{
	g_autofree char *b64 = NULL;
	char *p;

	g_return_if_fail (digest != NULL);
	g_return_if_fail (text != NULL);

	b64 = g_base64_encode (digest, NEMO_CACHE_DIGEST_LEN);

	/* base64url, which is plain base64 with 2 characters swapped. Here 32 bytes
	 * come out as 44 with one '=' on the end, and the padding goes. */
	for (p = b64; *p != '\0'; p++) {
		if (*p == '+')
			*p = '-';
		else if (*p == '/')
			*p = '_';
		else if (*p == '=')
			*p = '\0';
	}

	g_strlcpy (text, b64, NEMO_FILE_DIGEST_TEXT_LEN);
}

gboolean
nemo_file_digest_from_text (const char *text, guint8 *digest)
{
	char   padded[NEMO_FILE_DIGEST_TEXT_LEN + 1];
	g_autofree guchar *raw = NULL;
	gsize  raw_len = 0;
	gsize  i, len;

	g_return_val_if_fail (digest != NULL, FALSE);

	if (text == NULL)
		return FALSE;

	len = strlen (text);
	if (len != NEMO_FILE_DIGEST_TEXT_LEN - 1)
		return FALSE;

	for (i = 0; i < len; i++) {
		char c = text[i];

		if (c == '-')
			c = '+';
		else if (c == '_')
			c = '/';
		else if (!g_ascii_isalnum (c))
			return FALSE;	/* anything else isn't a checksum */

		padded[i] = c;
	}
	padded[len] = '=';
	padded[len + 1] = '\0';

	raw = g_base64_decode (padded, &raw_len);
	if (raw == NULL || raw_len != NEMO_CACHE_DIGEST_LEN)
		return FALSE;

	memcpy (digest, raw, NEMO_CACHE_DIGEST_LEN);

	return TRUE;
}

/* The 3 names picked in the backlog. `user.` is prepended on Linux and
 * the BSDs; on Windows these are stream names as they stand. */
#define ATTR_DIGEST "blake3.b64u"
#define ATTR_BYTES  "blake3.bytes"
#define ATTR_MTIME  "blake3.mtime"

gboolean
nemo_file_digest_read_attr (GFile *file, gint64 bytes, gint64 mtime, guint8 *digest)
{
	g_autofree char *text = NULL;
	g_autofree char *stored_bytes = NULL;
	g_autofree char *stored_mtime = NULL;

	g_return_val_if_fail (G_IS_FILE (file), FALSE);
	g_return_val_if_fail (digest != NULL, FALSE);

	text = nemo_file_xattr_get (file, ATTR_DIGEST);
	if (text == NULL)
		return FALSE;

	stored_bytes = nemo_file_xattr_get (file, ATTR_BYTES);
	stored_mtime = nemo_file_xattr_get (file, ATTR_MTIME);

	if (stored_bytes == NULL || stored_mtime == NULL)
		return FALSE;

	if (g_ascii_strtoll (stored_bytes, NULL, 10) != bytes
	    || g_ascii_strtoll (stored_mtime, NULL, 10) != mtime)
		return FALSE;

	return nemo_file_digest_from_text (text, digest);
}

gboolean
nemo_file_digest_write_attr (GFile *file, gint64 bytes, gint64 mtime, const guint8 *digest)
{
	g_autofree char *stored_bytes = NULL;
	g_autofree char *stored_mtime = NULL;
	char text[NEMO_FILE_DIGEST_TEXT_LEN];

	g_return_val_if_fail (G_IS_FILE (file), FALSE);
	g_return_val_if_fail (digest != NULL, FALSE);

	nemo_file_digest_to_text (digest, text);
	stored_bytes = g_strdup_printf ("%" G_GINT64_FORMAT, bytes);
	stored_mtime = g_strdup_printf ("%" G_GINT64_FORMAT, mtime);

	/* The time goes last, and that ordering is the whole safety of this.
	 * Reading requires the time to match, so a write that stops part way
	 * leaves the old time standing next to the new checksum and the next
	 * reader throws the lot away. Writing the time first would leave a new
	 * time vouching for a checksum of the old contents. */
	if (!nemo_file_xattr_set (file, ATTR_DIGEST, text))
		return FALSE;
	if (!nemo_file_xattr_set (file, ATTR_BYTES, stored_bytes))
		return FALSE;

	return nemo_file_xattr_set (file, ATTR_MTIME, stored_mtime);
}
