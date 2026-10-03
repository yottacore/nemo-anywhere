/* A leak shows as heap growth when the operation that leaks is repeated.
 * glibc only: elsewhere the heap cannot be read and the growth is -1. */

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

/* Runs op warmup times, then rounds times more, and gives how many bytes the
   heap grew over the rounds. -1 where the heap cannot be read, such as under
   a sanitizer, which keeps a heap of its own. */
gint64 test_heap_growth (void (*op) (gpointer data), gpointer data,
			 guint warmup, guint rounds);

G_END_DECLS

#endif /* TEST_HEAP_H */
