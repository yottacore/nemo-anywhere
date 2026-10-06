/* Heap growth over repeated operations, for the leak tests. */

#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <glib.h>

#ifdef __GLIBC__
#include <malloc.h>
#include <unistd.h>
#endif

#include "test-heap.h"

/* A freed block that sits in a thread's cache still counts as in use, and a
   thread with its own arena is not counted at all. With both off, in use
   means in use. */
#define HEAP_TUNABLES "glibc.malloc.tcache_count=0:glibc.malloc.arena_max=1"

/* GLib before 2.76 keeps freed slices in a cache of its own, the same way.
   On 2.72 that read as a move job leaking 32 bytes a round. Later GLib
   ignores it. */
#define HEAP_SLICE "always-malloc"

/* A reading where any thread started or ended is thrown away: the thread's
   own memory, taken or given back, can hide a small leak or look like one. */
#define HEAP_READINGS 8

void
test_heap_init (int argc, char **argv)
{
#ifdef __GLIBC__
	const char *old;
	char *tunables;
	char *slice;

	(void) argc;

	if (g_getenv ("NEMO_TEST_HEAP") != NULL) {
		return;
	}
	g_setenv ("NEMO_TEST_HEAP", "1", TRUE);

	old = g_getenv ("GLIBC_TUNABLES");
	tunables = old != NULL && *old != '\0'
		   ? g_strconcat (old, ":", HEAP_TUNABLES, NULL)
		   : g_strdup (HEAP_TUNABLES);
	g_setenv ("GLIBC_TUNABLES", tunables, TRUE);
	g_free (tunables);

	old = g_getenv ("G_SLICE");
	slice = old != NULL && *old != '\0'
		? g_strconcat (old, ",", HEAP_SLICE, NULL)
		: g_strdup (HEAP_SLICE);
	g_setenv ("G_SLICE", slice, TRUE);
	g_free (slice);

	execv ("/proc/self/exe", argv);
#else
	(void) argc;
	(void) argv;
#endif
}

static gint64
heap_in_use (void)
{
#ifdef __GLIBC__
	struct mallinfo2 info = mallinfo2 ();

	return (gint64) (info.uordblks + info.hblkhd);
#else
	return -1;
#endif
}

/* A sanitizer answers mallinfo2 with numbers that do not move. */
static gboolean
heap_readable (void)
{
	const gsize probe_size = 64 * 1024;
	gint64 before, after;
	char *probe;

	before = heap_in_use ();
	if (before < 0) {
		return FALSE;
	}
	probe = malloc (probe_size);
	if (probe == NULL) {
		return FALSE;
	}
	memset (probe, 1, probe_size);
	after = heap_in_use ();
	free (probe);

	return after - before >= (gint64) probe_size;
}

static gint
compare_ids (gconstpointer a, gconstpointer b)
{
	gint64 left = *(const gint64 *) a;
	gint64 right = *(const gint64 *) b;

	return (left > right) - (left < right);
}

/* The kernel's PF_EXITING. A thread that has been joined can stay listed for
   a moment after, with this set. */
#define THREAD_EXITING 0x4

/* Field 9 of the thread's stat line, its flags. The name before it is in
   brackets and can hold anything, so the count starts after the last one. */
static gboolean
thread_exiting (const char *id)
{
	char *path = g_build_filename ("/proc/self/task", id, "stat", NULL);
	char *stat = NULL;
	gboolean exiting = TRUE;

	if (g_file_get_contents (path, &stat, NULL, NULL)) {
		const char *field = strrchr (stat, ')');
		guint i;

		for (i = 0; field != NULL && i < 7; i++) {
			field = strchr (field + 1, ' ');
		}
		if (field != NULL) {
			exiting = (g_ascii_strtoull (field + 1, NULL, 10) & THREAD_EXITING) != 0;
		}
		g_free (stat);
	}
	g_free (path);

	return exiting;
}

/* NULL where the threads cannot be listed. A thread id is not given out again
   for a long while, so the same list twice means no thread started or ended
   in between, apart from one that did both and so came out even. */
static GArray *
thread_ids (void)
{
	GDir *dir = g_dir_open ("/proc/self/task", 0, NULL);
	GArray *ids;
	const char *name;

	if (dir == NULL) {
		return NULL;
	}
	ids = g_array_new (FALSE, FALSE, sizeof (gint64));
	while ((name = g_dir_read_name (dir)) != NULL) {
		gint64 id = g_ascii_strtoll (name, NULL, 10);

		if (!thread_exiting (name)) {
			g_array_append_val (ids, id);
		}
	}
	g_dir_close (dir);

	/* This thread at least, or the flags were misread. */
	if (ids->len == 0) {
		g_array_unref (ids);
		return NULL;
	}
	g_array_sort (ids, compare_ids);

	return ids;
}

static gboolean
same_threads (GArray *before, GArray *after)
{
	return before != NULL && after != NULL && before->len == after->len &&
	       memcmp (before->data, after->data, before->len * sizeof (gint64)) == 0;
}

/* GLib's pools stop a thread that has sat idle a while, half a second in some
   cases, so under load nearly every reading would see one go. Kept instead,
   a thread only starts, and once the pools have grown to what the operation
   needs, readings stop being thrown away. */
static void
keep_pool_threads (void)
{
	g_thread_pool_set_max_unused_threads (-1);
	g_thread_pool_set_max_idle_time (0);
}

/* One reading of the rounds. FALSE where a thread started or ended in it. */
static gboolean
settled_reading (void (*op) (gpointer data), gpointer data, guint rounds,
		 gint64 *growth)
{
	/* Listed outside the heap readings, so the lists cost nothing. */
	GArray *threads_before = thread_ids ();
	GArray *threads_after;
	gint64 before = heap_in_use ();
	gboolean settled;
	guint i;

	for (i = 0; i < rounds; i++) {
		op (data);
	}
	*growth = heap_in_use () - before;
	threads_after = thread_ids ();

	settled = same_threads (threads_before, threads_after);
	if (threads_before != NULL) {
		g_array_unref (threads_before);
	}
	if (threads_after != NULL) {
		g_array_unref (threads_after);
	}

	return settled;
}

TestHeapResult
test_heap_growth (void (*op) (gpointer data), gpointer data,
		  guint warmup, guint rounds, gint64 limit, gint64 *growth)
{
	gint64 reading_growth;
	guint settled = 0;
	guint reading;
	guint i;

	*growth = 0;
	if (!heap_readable ()) {
		return TEST_HEAP_UNREADABLE;
	}

	keep_pool_threads ();
	for (i = 0; i < warmup; i++) {
		op (data);
	}

	/* A leak grows the heap in every reading. What a thread takes the first
	   time it does some piece of work is taken once, and can come a reading
	   or more after the thread started, so growth has to show twice. */
	for (reading = 0; reading < HEAP_READINGS && settled < 2; reading++) {
		if (!settled_reading (op, data, rounds, &reading_growth)) {
			g_print ("a thread started or ended, so the reading is taken again\n");
			continue;
		}
		*growth = settled == 0 ? reading_growth : MIN (*growth, reading_growth);
		settled++;
		if (*growth < limit) {
			break;
		}
		if (settled < 2) {
			g_print ("the heap grew %" G_GINT64_FORMAT " bytes, so it is read again\n",
				 reading_growth);
		}
	}

	return settled > 0 ? TEST_HEAP_READ : TEST_HEAP_UNSETTLED;
}
