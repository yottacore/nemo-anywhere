/* The x-special/gnome-icon-list parser must not read past the buffer on the
 * no-geometry branch. We place the payload at the very end of an mmap'd page
 * whose next page is unmapped, so any over-read faults immediately - the
 * check is "did we survive", and the parsed count/URIs are verified too. */

#include <config.h>

#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <gtk/gtk.h>
#include <libnemo-private/nemo-dnd.h>
#include "test-check.h"

/* Copy `len` bytes so they end exactly at a page boundary backed by a guard
 * page, so a one-byte over-read segfaults instead of passing silently. */
static guchar *guard_base;
static long    page;

static const guchar *
at_page_end (const char *bytes, int len)
{
	guchar *dst = guard_base + page - len;
	memcpy (dst, bytes, len);
	return dst;
}

int
main (void)
{
	GList *list;

	page = sysconf (_SC_PAGESIZE);

	/* two pages: [readable][guard]. */
	guard_base = mmap (NULL, page * 2, PROT_READ | PROT_WRITE,
			   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	g_assert (guard_base != MAP_FAILED);
	mprotect (guard_base + page, page, PROT_NONE);

	/* Two name-only (no-geometry) entries. The buggy branch skipped the
	 * `size -=` every other path runs, leaving size inflated, so a later
	 * memchr scanned from the buffer's end into the guard page. */
	{
		const char p[] = "file:///a\r\nfile:///b\r\n";
		int len = (int) (sizeof (p) - 1);
		list = nemo_drag_build_selection_list_from_raw (at_page_end (p, len), len);
		check (g_list_length (list) == 2);
		if (list) {
			NemoDragSelectionItem *it = list->data;
			check (g_strcmp0 (it->uri, "file:///a") == 0);
		}
		nemo_drag_destroy_selection_list (list);
	}

	/* A single name-only entry with a trailing newline, also at page end. */
	{
		const char p[] = "file:///solo\r\n";
		int len = (int) (sizeof (p) - 1);
		list = nemo_drag_build_selection_list_from_raw (at_page_end (p, len), len);
		check (g_list_length (list) == 1);
		nemo_drag_destroy_selection_list (list);
	}

	munmap (guard_base, page * 2);

	if (failures == 0)
		g_print ("nemo-dnd: all checks passed\n");
	return failures == 0 ? 0 : 1;
}
