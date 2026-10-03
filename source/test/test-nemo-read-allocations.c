/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-read-allocations.c - what the reads made per icon and per row cost.

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

/* Heap allocations counted, rather than time, so a busy box cannot move it.
 *
 * Settings are read per icon hover, so a read of a flag or a number makes no
 * allocation: the "group.key" path is built once per key, not per read. Every
 * flag and number in the key table is read a thousand times, and the bar is
 * one allocation per ten reads. None is made today; building the path per
 * read made one or two.
 *
 * A string or enum read makes one allocation of its own, the string handed
 * back or the nick looked up, so each kind is read a thousand times over the
 * table on its own, with a bar of one and a half per read.
 *
 * A string list read hands back an array and a copy of each item, so its bar
 * goes by the length of each list: that many allocations plus one, and half
 * an allocation more per read. It is read once with the defaults and once with
 * every list stored in the file, since those take different paths.
 *
 * The Ext column asks every row for its extension, which is one string handed
 * back. The file name is looked at where it sits rather than copied first. The
 * bar is one and a half allocations per read: one today, two with the copy.
 *
 * The count comes from this program's own malloc, which only glibc lets it
 * put in front of the real one, and which a sanitizer build already owns. */

#include <config.h>

#include <stdlib.h>

#include <gtk/gtk.h>

#include <libnemo-private/nemo-config.h>
#include <libnemo-private/nemo-config-keys.h>
#include <libnemo-private/nemo-file.h>

#include "test-scratch.h"
#include "test-check.h"

#if defined (__has_feature)
#if __has_feature (address_sanitizer)
#define READS_SANITIZED 1
#endif
#endif
#if defined (__SANITIZE_ADDRESS__)
#define READS_SANITIZED 1
#endif

#if defined (__GLIBC__) && !defined (READS_SANITIZED)
#define READS_COUNTED 1

extern void *__libc_malloc (size_t size);
extern void *__libc_calloc (size_t count, size_t size);
extern void *__libc_realloc (void *block, size_t size);

/* Per thread, so the file monitor's own work is not counted. */
static __thread gboolean counting;
static __thread guint64  allocations;

void *
malloc (size_t size)
{
	if (counting)
		allocations++;
	return __libc_malloc (size);
}

void *
calloc (size_t count, size_t size)
{
	if (counting)
		allocations++;
	return __libc_calloc (count, size);
}

void *
realloc (void *block, size_t size)
{
	if (counting)
		allocations++;
	return __libc_realloc (block, size);
}
#endif

#ifdef READS_COUNTED
#define ROUNDS 1000

/* Returns how many reads it made; the values go into sink so none is skipped. */
static guint
read_every_scalar (gint64 *sink)
{
	const NemoConfigKey *k;
	guint reads = 0;

	for (k = nemo_config_keys; k->key != NULL; k++) {
		NemoConfigGroup *group = nemo_config_get_group (k->group);

		switch (k->type) {
		case NEMO_CONFIG_BOOL:
			*sink += nemo_config_get_boolean (group, k->key);
			break;
		case NEMO_CONFIG_INT:
			*sink += nemo_config_get_int64 (group, k->key);
			break;
		case NEMO_CONFIG_FLOAT:
			*sink += (gint64) nemo_config_get_double (group, k->key);
			break;
		default:
			continue;
		}
		reads++;
	}

	return reads;
}

static guint
read_every_text (NemoConfigType type, gint64 *sink)
{
	const NemoConfigKey *k;
	guint reads = 0;

	for (k = nemo_config_keys; k->key != NULL; k++) {
		NemoConfigGroup *group;

		if (k->type != type)
			continue;
		group = nemo_config_get_group (k->group);
		if (type == NEMO_CONFIG_STRING)
			g_free (nemo_config_get_string (group, k->key));
		else
			*sink += nemo_config_get_enum (group, k->key);
		reads++;
	}

	return reads;
}

/* Kept apart per kind, since together the one with more keys hides the other. */
static void
check_text_reads (NemoConfigType type, const char *label, gint64 *sink)
{
	guint64 reads = 0;
	int     round;

	check (read_every_text (type, sink) > 0);
	allocations = 0;
	counting = TRUE;
	for (round = 0; round < ROUNDS; round++)
		reads += read_every_text (type, sink);
	counting = FALSE;

	g_print ("%s: %" G_GUINT64_FORMAT " reads, %" G_GUINT64_FORMAT " allocations\n",
		 label, reads, allocations);
	check (allocations * 2 < reads * 3);
}

/* Returns how many reads it made, and adds to owed what the lists handed back
 * cost by themselves: the array, and one copy per item. */
static guint
read_every_list (guint64 *owed)
{
	const NemoConfigKey *k;
	guint reads = 0;

	for (k = nemo_config_keys; k->key != NULL; k++) {
		char **list;

		if (k->type != NEMO_CONFIG_STRING_LIST)
			continue;
		list = nemo_config_get_strv (nemo_config_get_group (k->group), k->key);
		*owed += g_strv_length (list) + 1;
		g_strfreev (list);
		reads++;
	}

	return reads;
}

static void
check_list_reads (const char *label)
{
	guint64 owed = 0, reads = 0;
	int     round;

	check (read_every_list (&owed) > 0);
	owed = 0;
	allocations = 0;
	counting = TRUE;
	for (round = 0; round < ROUNDS; round++)
		reads += read_every_list (&owed);
	counting = FALSE;

	g_print ("%s: %" G_GUINT64_FORMAT " reads, %" G_GUINT64_FORMAT " allocations, %"
		 G_GUINT64_FORMAT " for the lists themselves\n", label, reads, allocations, owed);
	check (allocations * 2 < owed * 2 + reads);
}

/* Not the default for any list, so each one is stored and read from the file. */
static void
store_every_list (void)
{
	static const char *const stored[] = { "one", "two", "three", NULL };
	const NemoConfigKey *k;

	for (k = nemo_config_keys; k->key != NULL; k++) {
		char **list;

		if (k->type != NEMO_CONFIG_STRING_LIST)
			continue;
		nemo_config_set_strv (nemo_config_get_group (k->group), k->key, stored);
		list = nemo_config_get_strv (nemo_config_get_group (k->group), k->key);
		check (g_strv_length (list) == 3);
		g_strfreev (list);
	}
}

static void
check_extension_reads (const char *tmp)
{
	char     *path = g_build_filename (tmp, "report.txt", NULL);
	char     *uri = g_filename_to_uri (path, NULL, NULL);
	NemoFile *file = nemo_file_get_by_uri (uri);
	GQuark    extension = g_quark_from_static_string ("extension");
	char     *got;
	guint64   reads;

	got = nemo_file_get_string_attribute_q (file, extension);
	check (g_strcmp0 (got, "txt") == 0);
	g_free (got);

	allocations = 0;
	counting = TRUE;
	for (reads = 0; reads < ROUNDS * 100; reads++)
		g_free (nemo_file_get_string_attribute_q (file, extension));
	counting = FALSE;

	g_print ("extension: %" G_GUINT64_FORMAT " reads, %" G_GUINT64_FORMAT " allocations\n",
		 reads, allocations);
	check (allocations * 2 < reads * 3);

	nemo_file_unref (file);
	g_free (uri);
	g_free (path);
}
#endif

int
main (int argc, char *argv[])
{
#ifndef READS_COUNTED
	g_print ("SKIP: allocations can only be counted on a glibc build with no sanitizer\n");
	return 77;
#else
	char   *tmp;
	gint64  sink = 0;
	guint64 reads = 0;
	int     round;

	tmp = test_scratch_config_home ("nemo-read-allocations-XXXXXX");
	/* A file object looks up the icon theme, which wants a screen. */
	gtk_init_check (&argc, &argv);
	nemo_config_init ();

	/* The first pass builds the key index and opens every group, once. */
	check (read_every_scalar (&sink) > 0);

	counting = TRUE;
	for (round = 0; round < ROUNDS; round++)
		reads += read_every_scalar (&sink);
	counting = FALSE;

	g_print ("settings: %" G_GUINT64_FORMAT " reads, %" G_GUINT64_FORMAT " allocations\n",
		 reads, allocations);
	check (allocations * 10 < reads);

	check_text_reads (NEMO_CONFIG_STRING, "strings", &sink);
	check_text_reads (NEMO_CONFIG_ENUM, "enums", &sink);
	check_list_reads ("default lists");
	store_every_list ();
	check_list_reads ("stored lists");

	check_extension_reads (tmp);

	nemo_config_shutdown ();
	g_free (tmp);

	if (failures == 0)
		g_print ("nemo-read-allocations: all checks passed\n");

	return failures == 0 ? 0 : 1;
#endif
}
