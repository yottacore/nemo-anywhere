/* nemo-small-copy.c
 *
 * Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2, as published by the
 * Free Software Foundation.
 */

#include <config.h>
#include "nemo-small-copy.h"

#include "nemo-global-preferences.h"

/* GLib tries a clone first on Linux, and copy_file_range clones as well on
   Btrfs and ZFS. On a small file that saves a block or two and leaves the
   file system tracking a shared extent for as long as both copies live. */
goffset
nemo_small_copy_limit (void)
{
#ifdef __linux__
	int kib = 64;

	if (nemo_config_is_ready ()) {
		kib = nemo_config_get_int (nemo_config_get_group (NEMO_PERFORMANCE_GROUP),
					   NEMO_PREFERENCES_CLONE_MIN_KIB);
	}

	/* the file is held in memory while it is copied */
	return (goffset) CLAMP (kib, 0, 1024) * 1024;
#else
	return 0;
#endif
}

/* The whole file is read before the target is made, so a source that can't be
   read, or has grown past the limit since it was sized, leaves nothing behind. */
static guint8 *
read_small (GFile *src, goffset limit, GCancellable *cancellable, gsize *len)
{
	GFileInputStream *in;
	guint8 *buf;
	gboolean ok;

	in = g_file_read (src, cancellable, NULL);
	if (in == NULL) {
		return NULL;
	}

	buf = g_malloc (limit);
	ok = g_input_stream_read_all (G_INPUT_STREAM (in), buf, limit, len,
				      cancellable, NULL);
	g_input_stream_close (G_INPUT_STREAM (in), NULL, NULL);
	g_object_unref (in);

	if (!ok || *len >= (gsize) limit) {
		g_free (buf);
		return NULL;
	}

	return buf;
}

NemoSmallCopyResult
nemo_small_copy (GFile          *src,
                 GFile          *dest,
                 GFileCopyFlags  flags,
                 goffset         limit,
                 GCancellable   *cancellable,
                 goffset        *copied,
                 GError        **error)
{
	GFileQueryInfoFlags query_flags = G_FILE_QUERY_INFO_NONE;
	GFileCreateFlags create_flags = G_FILE_CREATE_PRIVATE;
	GFileOutputStream *out;
	GFileInfo *info;
	gboolean small;
	guint8 *buf;
	gsize len = 0;
	gboolean ok;

	if (limit <= 0 || (flags & G_FILE_COPY_OVERWRITE) ||
	    !g_file_is_native (src) || !g_file_is_native (dest)) {
		return NEMO_SMALL_COPY_NOT_TRIED;
	}

	/* A link copied as a link is the ordinary copy's job. */
	if (flags & G_FILE_COPY_NOFOLLOW_SYMLINKS) {
		query_flags = G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS;
	}
	info = g_file_query_info (src,
				  G_FILE_ATTRIBUTE_STANDARD_TYPE ","
				  G_FILE_ATTRIBUTE_STANDARD_SIZE,
				  query_flags, cancellable, NULL);
	if (info == NULL) {
		return NEMO_SMALL_COPY_NOT_TRIED;
	}
	small = g_file_info_get_file_type (info) == G_FILE_TYPE_REGULAR &&
		g_file_info_get_size (info) < limit;
	g_object_unref (info);
	if (!small) {
		return NEMO_SMALL_COPY_NOT_TRIED;
	}

	buf = read_small (src, limit, cancellable, &len);
	if (buf == NULL) {
		return NEMO_SMALL_COPY_NOT_TRIED;
	}

	/* Made private, then given the source's mode when the caller copies the
	   attributes, so it is never readable by more people than the source. With
	   default permissions asked for, that step leaves the mode alone. */
	if (flags & G_FILE_COPY_TARGET_DEFAULT_PERMS) {
		create_flags = G_FILE_CREATE_NONE;
	}
	out = g_file_create (dest, create_flags, cancellable, NULL);
	if (out == NULL) {
		g_free (buf);
		return NEMO_SMALL_COPY_NOT_TRIED;
	}

	ok = g_output_stream_write_all (G_OUTPUT_STREAM (out), buf, len, NULL,
					cancellable, error);
	if (ok) {
		ok = g_output_stream_close (G_OUTPUT_STREAM (out), cancellable, error);
	} else {
		g_output_stream_close (G_OUTPUT_STREAM (out), NULL, NULL);
	}
	g_object_unref (out);
	g_free (buf);

	if (!ok) {
		return NEMO_SMALL_COPY_FAILED;
	}

	*copied = (goffset) len;
	return NEMO_SMALL_COPY_DONE;
}
