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

/* A worker thread that GLib's pool starts during the rounds costs about 1.3 KB
   once, more than the smallest leak over 64 rounds. A reading taken while the
   thread count moved is taken again. A leak shows in every reading. */
#define HEAP_READINGS 3

void
test_heap_init (int argc, char **argv)
{
#ifdef __GLIBC__
	const char *old;
	char *tunables;

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

static int
thread_count (void)
{
	char *status = NULL;
	const char *line;
	int count = -1;

	if (g_file_get_contents ("/proc/self/status", &status, NULL, NULL)) {
		line = strstr (status, "\nThreads:");
		if (line != NULL) {
			count = atoi (line + strlen ("\nThreads:"));
		}
		g_free (status);
	}

	return count;
}

gint64
test_heap_growth (void (*op) (gpointer data), gpointer data,
		  guint warmup, guint rounds)
{
	gint64 before;
	gint64 growth = 0;
	int threads;
	guint reading;
	guint i;

	if (!heap_readable ()) {
		return -1;
	}

	for (i = 0; i < warmup; i++) {
		op (data);
	}

	for (reading = 0; reading < HEAP_READINGS; reading++) {
		threads = thread_count ();
		before = heap_in_use ();
		for (i = 0; i < rounds; i++) {
			op (data);
		}
		growth = heap_in_use () - before;

		if (thread_count () == threads) {
			break;
		}
		g_print ("the thread count moved, so the reading is taken again\n");
	}

	return growth;
}
