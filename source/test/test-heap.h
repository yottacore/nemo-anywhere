/* A leak shows as heap growth when the operation that leaks is repeated.
 * glibc only: elsewhere the heap cannot be read. */

#ifndef TEST_HEAP_H
#define TEST_HEAP_H

#include <glib.h>

G_BEGIN_DECLS

/* glibc's smallest block. A leak of anything at all, once a round, grows the
   heap by at least this much a round. */
#define TEST_HEAP_SMALLEST_BLOCK 32

/* Runs this program again with glibc's per-thread caches off and one arena
   for every thread, so the heap's in-use count is exact. Call it first thing
   in main(). Returns only where there is nothing to do or the relaunch failed. */
void   test_heap_init   (int argc, char **argv);

typedef enum {
	TEST_HEAP_READ,
	/* Such as under a sanitizer, which keeps a heap of its own. */
	TEST_HEAP_UNREADABLE,
	/* A thread started or ended during every reading. */
	TEST_HEAP_UNSETTLED,
} TestHeapResult;

/* Runs op warmup times, then rounds times more, and sets growth to how many
   bytes the heap grew over the rounds, which can be below zero. A reading
   where any thread started or ended is taken again, and so is one that grew
   by limit or more, to see it twice. From the first call on, GLib's pools
   keep every thread they start. */
TestHeapResult test_heap_growth (void (*op) (gpointer data), gpointer data,
				 guint warmup, guint rounds, gint64 limit,
				 gint64 *growth);

G_END_DECLS

#endif /* TEST_HEAP_H */
