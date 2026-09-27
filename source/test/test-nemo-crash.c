/* Exercises the crash reporter: a child process is made to die both ways it
 * can, and the report it leaves behind is read back. */

#include <config.h>

#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>
#ifdef G_OS_WIN32
#include <windows.h>
#else
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <libnemo-private/nemo-crash.h>

#include "test-scratch.h"
#include "test-check.h"

static volatile int *null_pointer = NULL;
static void (*volatile null_function) (void) = NULL;

static int recurse (int depth);
static int (*volatile recurse_again) (int) = recurse;

/* Not a tail call, and through a pointer, so nothing can turn it into a loop. */
static int
recurse (int depth)
{
	volatile char pad[512];

	pad[0] = (char) depth;
	return recurse_again (depth + 1) + pad[0];
}

#if defined (G_OS_WIN32) && defined (__x86_64__)

/* Functions made up at run time, so their unwind data can be exactly as broken
   as a check needs. Each one calls abort, whose handler then has to walk back
   out through it. Both bodies end in mov rax, imm64 / call rax / int3, and the
   imm64 is filled in with abort. */

/* Plants a return address back into itself and a stack pointer that does not
   move, where its unwind data says a machine frame is. */
static const unsigned char loop_body[] = {
	0x48, 0x83, 0xec, 0x48,				/* sub rsp, 0x48 */
	0x48, 0x8d, 0x05, 0x00, 0x00, 0x00, 0x00,	/* lea rax, [rip] */
	0x48, 0x89, 0x44, 0x24, 0x20,			/* mov [rsp+0x20], rax */
	0x48, 0x89, 0x64, 0x24, 0x38,			/* mov [rsp+0x38], rsp */
	0x48, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0,		/* mov rax, abort */
	0xff, 0xd0,					/* call rax */
	0xcc,						/* int3 */
};

/* Version 1, two codes: alloc 32, then a machine frame. */
static const unsigned char loop_unwind[] = {
	0x01, 0x00, 0x02, 0x00, 0x00, 0x32, 0x00, 0x0a,
};

/* Uses rbp as its frame register, and points it at nothing. */
static const unsigned char shredded_body[] = {
	0x48, 0x83, 0xec, 0x28,				/* sub rsp, 0x28 */
	0x48, 0xc7, 0xc5, 0x10, 0x00, 0x00, 0x00,	/* mov rbp, 0x10 */
	0x48, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0,		/* mov rax, abort */
	0xff, 0xd0,					/* call rax */
	0xcc,						/* int3 */
};

/* Version 1, frame register rbp at offset 0, one code: set the frame. */
static const unsigned char shredded_unwind[] = {
	0x01, 0x00, 0x01, 0x05, 0x00, 0x03, 0x00, 0x00,
};

static void
run_generated (const unsigned char *body, gsize body_len,
	       const unsigned char *unwind, gsize unwind_len)
{
	static RUNTIME_FUNCTION table;
	DWORD64 abort_address = (DWORD64) (UINT_PTR) abort;
	unsigned char *base;
	void (*entry) (void);

	base = VirtualAlloc (NULL, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
	if (base == NULL) {
		return;
	}

	memcpy (base, body, body_len);
	memcpy (base + body_len - 11, &abort_address, sizeof abort_address);
	memcpy (base + 64, unwind, unwind_len);

	table.BeginAddress = 0;
	table.EndAddress = (DWORD) body_len;
	table.UnwindData = 64;
	RtlAddFunctionTable (&table, 1, (DWORD64) (UINT_PTR) base);

	entry = (void (*) (void)) base;
	entry ();
}

static gboolean
under_wine (void)
{
	HMODULE ntdll = GetModuleHandleA ("ntdll.dll");

	return ntdll != NULL && GetProcAddress (ntdll, "wine_get_version") != NULL;
}

#endif

/* The child half. Prints where the report will go, then dies on purpose. */
static int
run_child (const char *how)
{
	const char *path;

#ifndef G_OS_WIN32
	/* Deliberate crashes would otherwise drop cores in the build directory. */
	struct rlimit no_core = { 0, 0 };

	setrlimit (RLIMIT_CORE, &no_core);
#endif

	nemo_crash_handler_install ();

	path = nemo_crash_report_path ();
	g_print ("%s\n", path != NULL ? path : "none");
	fflush (stdout);

	/* A start that goes on to run normally: only the startup sweep. */
	if (strcmp (how, "sweep") == 0) {
		return 0;
	}

	if (strcmp (how, "abort") == 0) {
		abort ();
	}

	/* A call through a pointer that is no longer there: the shape a freed
	   object takes, and the one an unwinder is most likely to give up on. */
	if (strcmp (how, "nullcall") == 0) {
		null_function ();
	}

	if (strcmp (how, "overflow") == 0) {
		return recurse (0);
	}

	/* Another run already wrote a report under this one's name. */
	if (strcmp (how, "collide") == 0 && path != NULL) {
		g_autofree char *dir = g_path_get_dirname (path);

		g_mkdir_with_parents (dir, 0700);
		if (!g_file_set_contents (path, "placeholder", -1, NULL)) {
			return 2;
		}
	}

#ifndef G_OS_WIN32
	/* The same signal a fault raises, sent instead. There is no fault to come
	   back to, so a handler that simply returns lets this exit cleanly. */
	if (strcmp (how, "sent") == 0) {
		kill (getpid (), SIGSEGV);
		return 0;
	}
#endif

#if defined (G_OS_WIN32) && defined (__x86_64__)
	if (strcmp (how, "loop") == 0) {
		run_generated (loop_body, sizeof loop_body, loop_unwind, sizeof loop_unwind);
		return 0;
	}

	if (strcmp (how, "shredded") == 0) {
		run_generated (shredded_body, sizeof shredded_body,
			       shredded_unwind, sizeof shredded_unwind);
		return 0;
	}
#endif

	*null_pointer = 1;

	return 0;
}

/* The point of the handler is that it hands the signal back, so the process
   still dies the way it would have without one. Reporting and then exiting
   normally would pass a plain "did not exit zero" check. */
static void
check_died_of (int status, int expect_signal, unsigned long expect_code)
{
#ifdef G_OS_WIN32
	(void) expect_signal;
	check ((unsigned long) status == expect_code);
#else
	(void) expect_code;
	check (WIFSIGNALED (status));
	if (WIFSIGNALED (status)) {
		check (WTERMSIG (status) == expect_signal);
	}
#endif
}

typedef struct {
	char *report_path;
	char *stderr_text;
	int status;
	gboolean spawned;
} ChildResult;

static void
child_result_clear (ChildResult *result)
{
	g_free (result->report_path);
	g_free (result->stderr_text);
}

static ChildResult
run_crashing_child (const char *self, const char *config_root, const char *how,
		    gboolean handler_on)
{
	ChildResult result = { NULL, NULL, 0, FALSE };
	g_autoptr (GError) error = NULL;
	g_auto (GStrv) envp = NULL;
	g_autofree char *output = NULL;
	char *argv[3];

	envp = g_get_environ ();
	envp = g_environ_setenv (envp, "XDG_CONFIG_HOME", config_root, TRUE);
	envp = g_environ_setenv (envp, "APPDATA", config_root, TRUE);
	envp = g_environ_setenv (envp, "HOME", config_root, TRUE);
	envp = g_environ_setenv (envp, "NEMO_NO_CRASH_DIALOG", "1", TRUE);

	if (handler_on) {
		envp = g_environ_unsetenv (envp, "NEMO_NO_CRASH_HANDLER");
	} else {
		envp = g_environ_setenv (envp, "NEMO_NO_CRASH_HANDLER", "1", TRUE);
	}

	argv[0] = (char *) self;
	argv[1] = (char *) how;
	argv[2] = NULL;

	result.spawned = g_spawn_sync (NULL, argv, envp, G_SPAWN_DEFAULT, NULL, NULL,
				       &output, &result.stderr_text, &result.status,
				       &error);

	if (!result.spawned) {
		g_printerr ("could not start the child: %s\n", error->message);
		return result;
	}

	/* A Windows child's stdout is text mode, so the line ends in \r\n. */
	result.report_path = g_strdup (output != NULL ? output : "");
	result.report_path[strcspn (result.report_path, "\r\n")] = '\0';

	return result;
}

/* Lines under the stack heading that carry an address. Notes about where the
   walk stopped are not frames. */
static int
count_frames (const char *text)
{
	g_auto (GStrv) lines = g_strsplit (text, "\n", -1);
	gboolean in_stack = FALSE;
	int n = 0;
	int i;

	for (i = 0; lines[i] != NULL; i++) {
		if (g_str_has_prefix (lines[i], "stack")) {
			in_stack = TRUE;
		} else if (in_stack && strstr (lines[i], "0x") != NULL &&
			   strstr (lines[i], "  (") == NULL) {
			n++;
		}
	}

	return n;
}

/* Hands back the report text, or NULL when there is none. */
static char *
check_report (const char *path, const char *expect_cause)
{
	char *text = NULL;

	if (!g_file_get_contents (path, &text, NULL, NULL)) {
		g_printerr ("FAIL no report at %s\n", path);
		failures++;
		return NULL;
	}

	check (strstr (text, "nemo-anywhere v") != NULL);
	check (strstr (text, "died on ") != NULL);
	check (strstr (text, expect_cause) != NULL);
	check (strstr (text, "\npid ") != NULL);

	/* Which frames show up depends on the build, so only their presence is
	   checked. A build with no unwinder says so instead, and that is the
	   whole contract there. */
	check (count_frames (text) > 0 || strstr (text, "not available in this build") != NULL);

	return text;
}

static guint
count_reports (const char *dir)
{
	g_autoptr (GDir) handle = g_dir_open (dir, 0, NULL);
	const char *entry;
	guint n = 0;

	if (handle == NULL) {
		return 0;
	}

	while ((entry = g_dir_read_name (handle)) != NULL) {
		if (g_str_has_prefix (entry, "crash-")) {
			n++;
		}
	}

	return n;
}

/* The path the child said it wrote to, off its stderr. */
static char *
written_to (const char *stderr_text)
{
	static const char lead[] = "Report written to ";
	const char *start = stderr_text != NULL ? strstr (stderr_text, lead) : NULL;
	const char *end;

	if (start == NULL) {
		return NULL;
	}

	start += sizeof lead - 1;
	end = strpbrk (start, "\r\n");

	return end != NULL ? g_strndup (start, (gsize) (end - start)) : g_strdup (start);
}

#define SWEEP_SEEDED 23
#define SWEEP_KEPT 20

static char *
seeded_name (int i)
{
	/* Named in the opposite order to their times, since the name carries
	   when a run started and the file time is when it crashed. */
	return g_strdup_printf ("crash-20260101-000000-%d.txt", SWEEP_SEEDED - i);
}

/* A folder with more reports than are kept: at startup the oldest go, the
   newest is announced once, and never again for the same report. */
static void
check_sweep (const char *self)
{
	g_autofree char *root = NULL;
	g_autofree char *dir = NULL;
	g_autofree char *seen = NULL;
	g_autoptr (GError) error = NULL;
	ChildResult result;
	gint64 now = g_get_real_time () / G_USEC_PER_SEC;
	int i;

	root = test_scratch_dir ("nemo-crash-sweep-XXXXXX", &error);
	if (root == NULL) {
		g_printerr ("FAIL could not make a config root: %s\n", error->message);
		failures++;
		return;
	}

	/* The first start only says where reports go; there are none yet. */
	result = run_crashing_child (self, root, "sweep", TRUE);
	check (result.spawned && result.status == 0);
	if (result.spawned && result.report_path[0] != '\0' &&
	    strcmp (result.report_path, "none") != 0) {
		dir = g_path_get_dirname (result.report_path);
	}
	check (result.stderr_text == NULL || strstr (result.stderr_text, "stopped unexpectedly") == NULL);
	child_result_clear (&result);
	check (dir != NULL);
	if (dir == NULL) {
		return;
	}

	g_mkdir_with_parents (dir, 0700);
	for (i = 0; i < SWEEP_SEEDED; i++) {
		g_autofree char *name = seeded_name (i);
		g_autofree char *path = g_build_filename (dir, name, NULL);
		g_autoptr (GFile) file = g_file_new_for_path (path);

		check (g_file_set_contents (path, "report", -1, NULL));
		/* An hour apart, the first seeded the oldest. */
		check (g_file_set_attribute_uint64 (file, G_FILE_ATTRIBUTE_TIME_MODIFIED,
						    (guint64) (now - (SWEEP_SEEDED - i) * 3600),
						    G_FILE_QUERY_INFO_NONE, NULL, NULL));
	}

	result = run_crashing_child (self, root, "sweep", TRUE);
	check (result.spawned && result.status == 0);
	check (result.stderr_text != NULL && strstr (result.stderr_text, "stopped unexpectedly") != NULL);
	child_result_clear (&result);

	check (count_reports (dir) == SWEEP_KEPT);
	for (i = 0; i < SWEEP_SEEDED; i++) {
		g_autofree char *name = seeded_name (i);
		g_autofree char *path = g_build_filename (dir, name, NULL);
		gboolean kept = g_file_test (path, G_FILE_TEST_EXISTS);

		if (kept != (i >= SWEEP_SEEDED - SWEEP_KEPT)) {
			g_printerr ("FAIL %s was %s\n", name, kept ? "kept" : "dropped");
			failures++;
		}
	}

	/* The marker names the newest, which is the last seeded. */
	{
		g_autofree char *marker = g_build_filename (dir, "last-seen", NULL);
		g_autofree char *newest = seeded_name (SWEEP_SEEDED - 1);

		check (g_file_get_contents (marker, &seen, NULL, NULL));
		check (g_strcmp0 (seen, newest) == 0);
	}

	/* Nothing new since, so the next start says nothing. */
	result = run_crashing_child (self, root, "sweep", TRUE);
	check (result.spawned && result.status == 0);
	check (result.stderr_text == NULL || strstr (result.stderr_text, "stopped unexpectedly") == NULL);
	child_result_clear (&result);
	check (count_reports (dir) == SWEEP_KEPT);
}

#ifdef G_OS_WIN32
#define FAULT_CAUSE "access violation"
#define ABORT_CAUSE "aborted"
#else
#define FAULT_CAUSE "SIGSEGV"
#define ABORT_CAUSE "SIGABRT"
#endif

int
main (int argc, char *argv[])
{
	g_autofree char *config_root = NULL;
	g_autofree char *crash_dir = NULL;
	g_autoptr (GError) error = NULL;
	ChildResult result;
	guint before = 0;

	if (argc == 2) {
		return run_child (argv[1]);
	}

	config_root = test_scratch_dir ("nemo-crash-XXXXXX", &error);
	if (config_root == NULL) {
		g_printerr ("FAIL could not make a config root: %s\n", error->message);
		return 1;
	}

	/* A real fault: the unhandled-exception filter on Windows, SIGSEGV
	   everywhere else. */
	result = run_crashing_child (argv[0], config_root, "fault", TRUE);
	if (result.spawned) {
		check_died_of (result.status, SIGSEGV, 0xc0000005UL);
		check (result.report_path[0] != '\0');
		check (strcmp (result.report_path, "none") != 0);

		if (result.report_path[0] != '\0') {
			g_free (check_report (result.report_path, FAULT_CAUSE));
		}

		/* The same report goes to stderr, which is where a launcher log
		   picks it up. */
		check (strstr (result.stderr_text, "nemo-anywhere v") != NULL);
		check (strstr (result.stderr_text, result.report_path) != NULL);
	}

	/* Learned from the child rather than rebuilt here, so the test cannot
	   disagree with the code about where reports go. */
	if (result.report_path != NULL && result.report_path[0] != '\0') {
		crash_dir = g_path_get_dirname (result.report_path);
	}

	child_result_clear (&result);

	/* An assertion failure, which never reaches a fault handler on Windows. */
	result = run_crashing_child (argv[0], config_root, "abort", TRUE);
	if (result.spawned) {
		check_died_of (result.status, SIGABRT, 0x40000015UL);
		check (result.report_path[0] != '\0');
		check (strcmp (result.report_path, "none") != 0);

		if (result.report_path[0] != '\0') {
			g_free (check_report (result.report_path, ABORT_CAUSE));
		}
	}
	child_result_clear (&result);

	/* A jump to nowhere. The stack has to be recovered from the return
	   address, since there is no function at the address that faulted. The
	   frames that matter are the caller's and everything above it, not just
	   the reporter's own two. */
	result = run_crashing_child (argv[0], config_root, "nullcall", TRUE);
	if (result.spawned) {
		check_died_of (result.status, SIGSEGV, 0xc0000005UL);
		check (result.report_path[0] != '\0');

		if (result.report_path[0] != '\0') {
			g_autofree char *text = check_report (result.report_path, FAULT_CAUSE);

			check (text == NULL || count_frames (text) >= 4 ||
			       strstr (text, "not available in this build") != NULL);
		}
	}
	child_result_clear (&result);

	/* Two runs that got the same name. The earlier report stays as it was and
	   the new one goes beside it, under the name the child says it used. */
	result = run_crashing_child (argv[0], config_root, "collide", TRUE);
	if (result.spawned) {
		g_autofree char *earlier = NULL;
		g_autofree char *actual = written_to (result.stderr_text);

		check_died_of (result.status, SIGSEGV, 0xc0000005UL);
		check (g_file_get_contents (result.report_path, &earlier, NULL, NULL) &&
		       strcmp (earlier, "placeholder") == 0);
		check (actual != NULL && strcmp (actual, result.report_path) != 0);

		if (actual != NULL) {
			g_free (check_report (actual, FAULT_CAUSE));
		}
	}
	child_result_clear (&result);

#ifndef G_OS_WIN32
	/* A fault the kernel raised is left to happen again, which only works
	   because there is a fault to repeat. A sent one has to be raised again,
	   or the child carries on and exits cleanly. */
	result = run_crashing_child (argv[0], config_root, "sent", TRUE);
	if (result.spawned) {
		check_died_of (result.status, SIGSEGV, 0);

		if (result.report_path[0] != '\0') {
			g_autofree char *text = check_report (result.report_path, "SIGSEGV");

			/* Nothing faulted, so there is no address to give. */
			check (text == NULL || strstr (text, " at 0x") == NULL);
		}
	}
	child_result_clear (&result);
#endif

	/* Running out of stack, which leaves the handler very little to run on.
	   The emulator does not reserve the stack the handler needs, so there it
	   proves nothing either way. */
#if defined (G_OS_WIN32) && defined (__x86_64__)
	if (!under_wine ())
#endif
	{
		result = run_crashing_child (argv[0], config_root, "overflow", TRUE);
		if (result.spawned) {
			check_died_of (result.status, SIGSEGV, 0xc00000fdUL);
			check (result.report_path[0] != '\0');

			if (result.report_path[0] != '\0') {
#ifdef G_OS_WIN32
				g_free (check_report (result.report_path, "stack overflow"));
#else
				g_free (check_report (result.report_path, "SIGSEGV"));
#endif
			}
		}
		child_result_clear (&result);
	}

#if defined (G_OS_WIN32) && defined (__x86_64__)
	/* Unwind data that leads a frame back to itself. The walk has to notice
	   and stop, not fill the report with one frame over and over. */
	result = run_crashing_child (argv[0], config_root, "loop", TRUE);
	if (result.spawned) {
		check_died_of (result.status, SIGABRT, 0x40000015UL);

		if (result.report_path[0] != '\0') {
			g_autofree char *text = check_report (result.report_path, ABORT_CAUSE);

			check (text == NULL || strstr (text, "unwinds back into itself") != NULL);
			check (text == NULL || count_frames (text) < 32);
		}
	}
	child_result_clear (&result);

	/* A frame whose saved registers point at nothing. The frames up to it
	   still have to make it into the report. */
	result = run_crashing_child (argv[0], config_root, "shredded", TRUE);
	if (result.spawned) {
		check_died_of (result.status, SIGABRT, 0x40000015UL);

		if (result.report_path[0] != '\0') {
			g_autofree char *text = check_report (result.report_path, ABORT_CAUSE);

			check (text == NULL || strstr (text, "cannot be read past here") != NULL);
		}
	}
	child_result_clear (&result);
#endif

	/* Switched off, a real crash leaves nothing behind. */
	before = crash_dir != NULL ? count_reports (crash_dir) : 0;
	check (before > 0);

	result = run_crashing_child (argv[0], config_root, "fault", FALSE);
	if (result.spawned) {
		check (result.status != 0);
		check (strcmp (result.report_path, "none") == 0);
		check (crash_dir != NULL && count_reports (crash_dir) == before);
	}
	child_result_clear (&result);

	check_sweep (argv[0]);

	if (failures == 0) {
		g_print ("crash reporter: all checks passed\n");
	}

	return failures == 0 ? 0 : 1;
}
