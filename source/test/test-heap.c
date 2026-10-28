/* Heap growth over repeated operations, for the leak tests. */

#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <glib-object.h>

#ifdef __GLIBC__
#include <malloc.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

#ifdef G_OS_WIN32
#include <windows.h>
#include <tlhelp32.h>
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

/* A leak grows the heap in every reading. What a thread takes the first time
   it does some piece of work is taken once, and can come a reading or more
   after the thread started, so growth has to show twice.
   On Windows, GIO's start-up scan of the installed apps leaves Windows' own
   file explorer code in the process, and from then on COM calls come and go
   in it from outside, a 4 KB buffer at a time. They can grow two readings in
   a row and then give it all back, which a leak never does, so there growth
   has to show 4 times (2026101913541101). */
#ifdef G_OS_WIN32
#define HEAP_SHOWINGS 4
#else
#define HEAP_SHOWINGS 2
#endif

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

#ifdef G_OS_WIN32
/* GLib and the other mingw DLLs take from msvcrt's heap, which is not the
   process heap. Rust code such as librsvg and Windows' own DLLs take from the
   process heap. So every heap is read, and a block's overhead counts, as
   glibc's chunk sizes do. */
static gint64
heap_in_use (void)
{
	static HANDLE heaps[256];
	DWORD count = GetProcessHeaps (G_N_ELEMENTS (heaps), heaps);
	gint64 in_use = 0;
	DWORD i;

	if (count == 0) {
		return -1;
	}
	if (count > G_N_ELEMENTS (heaps)) {
		g_error ("%lu heaps, more than can be read", (unsigned long) count);
	}
	for (i = 0; i < count; i++) {
		PROCESS_HEAP_ENTRY entry = { 0 };

		/* Fails only where the heap was destroyed since it was listed. */
		if (!HeapLock (heaps[i])) {
			continue;
		}
		while (HeapWalk (heaps[i], &entry)) {
			if ((entry.wFlags & PROCESS_HEAP_ENTRY_BUSY) != 0) {
				in_use += (gint64) entry.cbData + entry.cbOverhead;
			}
		}
		HeapUnlock (heaps[i]);
	}

	return in_use;
}
#else
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
#endif

gint64
test_heap_in_use (void)
{
	return heap_in_use ();
}

#define PROBE_BIG (64 * 1024)
#define PROBE_SMALL 24

/* A sanitizer answers mallinfo2 with numbers that do not move. Wine's heap
   walk can see a big block and miss a small one. */
static gboolean
heap_readable (void)
{
	gpointer small[64];
	gint64 before, between, after;
	char *big;
	guint i;

	before = heap_in_use ();
	if (before < 0) {
		return FALSE;
	}
	big = g_malloc (PROBE_BIG);
	memset (big, 1, PROBE_BIG);
	between = heap_in_use ();
	for (i = 0; i < G_N_ELEMENTS (small); i++) {
		small[i] = g_malloc0 (PROBE_SMALL);
	}
	after = heap_in_use ();
	for (i = 0; i < G_N_ELEMENTS (small); i++) {
		g_free (small[i]);
	}
	g_free (big);

	return between - before >= PROBE_BIG &&
	       after - between >= (gint64) (G_N_ELEMENTS (small) * PROBE_SMALL);
}

/* Windows gives a thread id out again soon after, so there a thread is told
   apart by when it started too. */
typedef struct {
	gint64 id;
	gint64 started;
} ThreadMark;

static gint
compare_ids (gconstpointer a, gconstpointer b)
{
	const ThreadMark *left = a;
	const ThreadMark *right = b;

	if (left->id != right->id) {
		return (left->id > right->id) - (left->id < right->id);
	}
	return (left->started > right->started) - (left->started < right->started);
}

#ifdef G_OS_WIN32
/* FALSE where it has ended, or cannot be looked at. */
static gboolean
thread_running (DWORD id, gint64 *started)
{
	HANDLE thread = OpenThread (THREAD_QUERY_LIMITED_INFORMATION, FALSE, id);
	FILETIME created, exited, kernel, user;
	DWORD code = 0;
	gboolean running;

	if (thread == NULL) {
		return FALSE;
	}
	running = GetExitCodeThread (thread, &code) && code == STILL_ACTIVE &&
		  GetThreadTimes (thread, &created, &exited, &kernel, &user);
	CloseHandle (thread);
	if (running) {
		*started = ((gint64) created.dwHighDateTime << 32) | created.dwLowDateTime;
	}

	return running;
}

/* NULL where the threads cannot be listed. */
static GArray *
thread_ids (void)
{
	HANDLE snapshot = CreateToolhelp32Snapshot (TH32CS_SNAPTHREAD, 0);
	DWORD self = GetCurrentProcessId ();
	THREADENTRY32 entry = { 0 };
	GArray *ids;

	if (snapshot == INVALID_HANDLE_VALUE) {
		return NULL;
	}
	ids = g_array_new (FALSE, FALSE, sizeof (ThreadMark));
	entry.dwSize = sizeof (entry);
	if (Thread32First (snapshot, &entry)) {
		do {
			ThreadMark mark = { entry.th32ThreadID, 0 };

			if (entry.th32OwnerProcessID == self &&
			    thread_running (entry.th32ThreadID, &mark.started)) {
				g_array_append_val (ids, mark);
			}
		} while (Thread32Next (snapshot, &entry));
	}
	CloseHandle (snapshot);

	/* This thread at least. */
	if (ids->len == 0) {
		g_array_unref (ids);
		return NULL;
	}
	g_array_sort (ids, compare_ids);

	return ids;
}
#else

/* The kernel's PF_EXITING. A thread that has been joined can stay listed for
   a moment after, with this set. */
#define THREAD_EXITING 0x4

/* The thread's stat line, read past the name, which is in brackets and can
   hold anything. Field 3 is its state, and field 9 its flags. NULL where the
   thread is gone. */
static char *
thread_stat (const char *id, const char **after_name)
{
	char *path = g_build_filename ("/proc/self/task", id, "stat", NULL);
	char *stat = NULL;

	*after_name = NULL;
	if (g_file_get_contents (path, &stat, NULL, NULL)) {
		*after_name = strrchr (stat, ')');
	}
	g_free (path);
	if (*after_name == NULL) {
		g_free (stat);
		return NULL;
	}

	return stat;
}

static gboolean
thread_exiting (const char *id)
{
	const char *field;
	char *stat = thread_stat (id, &field);
	gboolean exiting = TRUE;
	guint i;

	if (stat == NULL) {
		return TRUE;
	}
	for (i = 0; field != NULL && i < 7; i++) {
		field = strchr (field + 1, ' ');
	}
	if (field != NULL) {
		exiting = (g_ascii_strtoull (field + 1, NULL, 10) & THREAD_EXITING) != 0;
	}
	g_free (stat);

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
	ids = g_array_new (FALSE, FALSE, sizeof (ThreadMark));
	while ((name = g_dir_read_name (dir)) != NULL) {
		ThreadMark mark = { g_ascii_strtoll (name, NULL, 10), 0 };

		if (!thread_exiting (name)) {
			g_array_append_val (ids, mark);
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
#endif

static gboolean
same_threads (GArray *before, GArray *after)
{
	return before != NULL && after != NULL && before->len == after->len &&
	       memcmp (before->data, after->data, before->len * sizeof (ThreadMark)) == 0;
}

#ifdef G_OS_WIN32
/* For a second or so after start, Windows and GTK take and give back memory
   in threads that were already there, so no thread list shows it. And GTK
   keeps what it used to ask the clipboard for until a 1 second timer goes off,
   since the answer comes from outside the process there. So the heap has to
   sit still for longer than that, with the main loop running. */
#define QUIET_POLLS 25
#define QUIET_POLL_MS 50
#define QUIET_GIVE_UP_MS 20000

static void
wait_for_quiet_heap (void)
{
	gint64 give_up_at = g_get_monotonic_time () + QUIET_GIVE_UP_MS * 1000;
	gint64 last = heap_in_use ();
	guint still = 0;

	while (still < QUIET_POLLS) {
		gint64 now;

		while (g_main_context_iteration (NULL, FALSE)) {
		}
		g_usleep (QUIET_POLL_MS * 1000);
		now = heap_in_use ();
		still = now == last ? still + 1 : 0;
		last = now;
		if (g_get_monotonic_time () > give_up_at) {
			g_print ("the heap was still moving after %d seconds\n",
				 QUIET_GIVE_UP_MS / 1000);
			return;
		}
	}
}
#elif defined(__GLIBC__)
/* A job's thread hands its task back to the main loop a moment after the
   job's own done callback has run there, and the task, about 800 bytes, is
   freed only then. On a busy box that can come after the round's main loop
   has gone quiet, so a reading caught the task on one side or the other
   (2026102518200000). So a reading waits until no other thread is running
   and the main loop has nothing left to do. */
#define STILL_POLL_MS 2
#define STILL_GIVE_UP_MS 5000

static gboolean
other_thread_running (void)
{
	g_autofree char *self = g_strdup_printf ("%ld", (long) syscall (SYS_gettid));
	GDir *dir = g_dir_open ("/proc/self/task", 0, NULL);
	const char *name;
	gboolean running = FALSE;

	if (dir == NULL) {
		return FALSE;
	}
	while (!running && (name = g_dir_read_name (dir)) != NULL) {
		const char *field;
		char *stat;

		if (strcmp (name, self) == 0) {
			continue;
		}
		stat = thread_stat (name, &field);
		running = stat != NULL && field[1] == ' ' && field[2] == 'R';
		g_free (stat);
	}
	g_dir_close (dir);

	return running;
}

static void
wait_for_quiet_heap (void)
{
	gint64 give_up_at = g_get_monotonic_time () + STILL_GIVE_UP_MS * 1000;

	for (;;) {
		while (g_main_context_iteration (NULL, FALSE)) {
		}
		if (!other_thread_running () && !g_main_context_pending (NULL)) {
			return;
		}
		if (g_get_monotonic_time () > give_up_at) {
			g_print ("a thread was still running after %d seconds\n",
				 STILL_GIVE_UP_MS / 1000);
			return;
		}
		g_usleep (STILL_POLL_MS * 1000);
	}
}
#else
static void
wait_for_quiet_heap (void)
{
}
#endif

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
	GArray *threads_before, *threads_after;
	gint64 before;
	gboolean settled;
	guint i;

	/* Listed outside the heap readings, so the lists cost nothing. */
	wait_for_quiet_heap ();
	threads_before = thread_ids ();
	before = heap_in_use ();
	for (i = 0; i < rounds; i++) {
		op (data);
	}
	wait_for_quiet_heap ();
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

/* GObject keeps every signal handler in one hash table, and a table that
   has filled with removed entries is rebuilt to fit how many are connected at
   that moment. A job connects a few dozen and lets them go, and with only
   the few dozen a bare test has, a rebuild left the table at 64 or 128
   slots, 768 bytes apart, by when it came (2026102518200000). With this many
   more held for the readings, every rebuild comes out the same size. 384 put
   the drag case, which keeps a few objects a round, near the next size up. */
#define HELD_HANDLERS 1024

static void
held_handler (GObject *object, GParamSpec *pspec, gpointer data)
{
	(void) object;
	(void) pspec;
	(void) data;
}

static GPtrArray *
hold_handlers (void)
{
	GPtrArray *held = g_ptr_array_new_full (HELD_HANDLERS, g_object_unref);
	guint i;

	/* One object each, since the table of objects with handlers is rebuilt
	   the same way. */
	for (i = 0; i < HELD_HANDLERS; i++) {
		GObject *object = g_object_new (G_TYPE_OBJECT, NULL);

		g_signal_connect (object, "notify", G_CALLBACK (held_handler), NULL);
		g_ptr_array_add (held, object);
	}

	return held;
}

TestHeapResult
test_heap_growth (void (*op) (gpointer data), gpointer data,
		  guint warmup, guint rounds, gint64 limit, gint64 *growth)
{
	g_autoptr (GPtrArray) held = hold_handlers ();
	gint64 reading_growth;
	guint settled = 0;
	guint reading;
	guint i;

	*growth = 0;
	wait_for_quiet_heap ();
	if (!heap_readable ()) {
		return TEST_HEAP_UNREADABLE;
	}

	keep_pool_threads ();
	for (i = 0; i < warmup; i++) {
		op (data);
	}

	for (reading = 0; reading < HEAP_READINGS && settled < HEAP_SHOWINGS; reading++) {
		if (!settled_reading (op, data, rounds, &reading_growth)) {
			g_print ("a thread started or ended, so the reading is taken again\n");
			continue;
		}
		*growth = settled == 0 ? reading_growth : MIN (*growth, reading_growth);
		settled++;
		if (*growth < limit) {
			break;
		}
		if (settled < HEAP_SHOWINGS) {
			g_print ("the heap grew %" G_GINT64_FORMAT " bytes, so it is read again\n",
				 reading_growth);
		}
	}

	return settled > 0 ? TEST_HEAP_READ : TEST_HEAP_UNSETTLED;
}
