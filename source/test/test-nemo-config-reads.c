/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-config-reads.c - what reading a setting costs.

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

/* Settings are read per icon hover, so a read of a flag or a number makes no
 * heap allocation: the "group.key" path is built once per key, not per read.
 * Counted rather than timed, so a busy box cannot move it. Every flag and
 * number in the key table is read a thousand times, and the bar is one
 * allocation per ten reads. None is made today; building the path per read
 * made two.
 *
 * The count comes from this program's own malloc, which only glibc lets it
 * put in front of the real one, and which a sanitizer build already owns. */

#include <config.h>

#include <stdlib.h>

#include <glib.h>

#include <libnemo-private/nemo-config.h>
#include <libnemo-private/nemo-config-keys.h>

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

/* Per thread, so the file monitor's own work does not land in the count. */
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

	tmp = test_scratch_config_home ("nemo-config-reads-XXXXXX");
	nemo_config_init ();

	/* The first pass builds the key index and opens every group, once. */
	check (read_every_scalar (&sink) > 0);

	counting = TRUE;
	for (round = 0; round < ROUNDS; round++)
		reads += read_every_scalar (&sink);
	counting = FALSE;

	g_print ("%" G_GUINT64_FORMAT " reads, %" G_GUINT64_FORMAT " allocations\n",
		 reads, allocations);
	check (allocations * 10 < reads);

	nemo_config_shutdown ();
	g_free (tmp);

	if (failures == 0)
		g_print ("nemo-config-reads: all checks passed\n");

	return failures == 0 ? 0 : 1;
#endif
}
