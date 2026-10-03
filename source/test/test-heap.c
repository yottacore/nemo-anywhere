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

gint64
test_heap_growth (void (*op) (gpointer data), gpointer data,
		  guint warmup, guint rounds)
{
	gint64 before;
	guint i;

	if (!heap_readable ()) {
		return -1;
	}

	for (i = 0; i < warmup; i++) {
		op (data);
	}

	before = heap_in_use ();
	for (i = 0; i < rounds; i++) {
		op (data);
	}

	return heap_in_use () - before;
}
