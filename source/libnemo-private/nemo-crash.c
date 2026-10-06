/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-crash.c - write a report when the program dies unexpectedly.

   Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu).

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; version 2 of the
   License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, see <http://www.gnu.org/licenses/>.
*/

/* For the register names in a signal context. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <config.h>

#include "nemo-crash.h"
#include "nemo-file-utilities.h"

#include <nemo-build-number.h>

#include <glib/gstdio.h>
#include <string.h>

#ifdef G_OS_WIN32
#include <windows.h>
#include <signal.h>
#include <stdio.h>
#include <wchar.h>

#ifndef STATUS_FATAL_APP_EXIT
#define STATUS_FATAL_APP_EXIT 0x40000015L
#endif
#else
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#if HAVE_BACKTRACE
#include <execinfo.h>
#if defined (__linux__) && defined (__x86_64__)
#include <ucontext.h>
#define CRASH_STEP_BAD_JUMP 1
typedef greg_t crash_reg_t;
#define CRASH_PC(uc) ((uc)->uc_mcontext.gregs[REG_RIP])
#define CRASH_SP(uc) ((uc)->uc_mcontext.gregs[REG_RSP])
#elif defined (__FreeBSD__) && defined (__x86_64__)
#include <ucontext.h>
#define CRASH_STEP_BAD_JUMP 1
typedef __register_t crash_reg_t;
#define CRASH_PC(uc) ((uc)->uc_mcontext.mc_rip)
#define CRASH_SP(uc) ((uc)->uc_mcontext.mc_rsp)
#endif
#endif
#endif

#define CRASH_PATH_MAX 1024
#define CRASH_MAX_FRAMES 64

/* Old reports are the user's to read, not ours to hoard. */
#define CRASH_KEEP_REPORTS 20

static char report_parent[CRASH_PATH_MAX];
static char report_dir[CRASH_PATH_MAX];
static char report_path[CRASH_PATH_MAX];
static char started_stamp[32];
static gboolean installed = FALSE;

/* The name the report really went under, which differs from report_path when
   another run already has that one. */
static char written_path[CRASH_PATH_MAX];

#ifdef G_OS_WIN32
static wchar_t report_parent_w[CRASH_PATH_MAX];
static wchar_t report_dir_w[CRASH_PATH_MAX];
static wchar_t report_path_w[CRASH_PATH_MAX];
static wchar_t written_path_w[CRASH_PATH_MAX];
#endif

/* An empty value reads as unset, the way the rest of the tree treats one. */
static gboolean
is_empty_env (const char *name)
{
	const char *value = g_getenv (name);

	return value == NULL || *value == '\0';
}

static gboolean
build_paths (void)
{
	g_autofree char *parent = NULL;
	g_autofree char *dir = NULL;
	g_autofree char *name = NULL;
	g_autofree char *path = NULL;
	GDateTime *now = NULL;
	g_autofree char *stamp = NULL;
	guint pid;

#ifdef G_OS_WIN32
	pid = (guint) GetCurrentProcessId ();
#else
	pid = (guint) getpid ();
#endif

	/* Deliberately not nemo_get_user_directory: that creates the config dir
	   and migrates an older one, and a run that only prints its version has
	   no business doing either. The directory is made when there is finally
	   something to put in it. */
	parent = g_build_filename (nemo_get_user_config_root (), NEMO_APP_SLUG, NULL);
	dir = g_build_filename (parent, "crash", NULL);

	now = g_date_time_new_now_local ();
	stamp = g_date_time_format (now, "%Y%m%d-%H%M%S");
	g_date_time_unref (now);

	/* The name carries when the run STARTED, because a signal handler cannot
	   safely work out what time it is. The file's own timestamp is the crash. */
	name = g_strdup_printf ("crash-%s-%u.txt", stamp, pid);
	path = g_build_filename (dir, name, NULL);

	/* Room for the "-2" a clashing name gets. */
	if (strlen (path) + 2 >= CRASH_PATH_MAX)
		return FALSE;

	g_strlcpy (started_stamp, stamp, sizeof started_stamp);
	g_strlcpy (report_parent, parent, sizeof report_parent);
	g_strlcpy (report_dir, dir, sizeof report_dir);
	g_strlcpy (report_path, path, sizeof report_path);
	g_strlcpy (written_path, path, sizeof written_path);

#ifdef G_OS_WIN32
	{
		g_autofree gunichar2 *wparent = g_utf8_to_utf16 (parent, -1, NULL, NULL, NULL);
		g_autofree gunichar2 *wdir = g_utf8_to_utf16 (dir, -1, NULL, NULL, NULL);
		g_autofree gunichar2 *wpath = g_utf8_to_utf16 (path, -1, NULL, NULL, NULL);

		if (wparent == NULL || wdir == NULL || wpath == NULL)
			return FALSE;

		if (wcslen ((const wchar_t *) wpath) + 2 >= CRASH_PATH_MAX)
			return FALSE;

		wcscpy (report_parent_w, (const wchar_t *) wparent);
		wcscpy (report_dir_w, (const wchar_t *) wdir);
		wcscpy (report_path_w, (const wchar_t *) wpath);
		wcscpy (written_path_w, (const wchar_t *) wpath);
	}
#endif

	return TRUE;
}

typedef struct {
	char *name;
	gint64 written;
} Report;

static void
report_free (gpointer data)
{
	Report *report = data;

	g_free (report->name);
	g_free (report);
}

/* By when it was written, not by the name: the name carries when the run
   started, and a window open for a week can crash after one opened an hour ago. */
static int
compare_written (gconstpointer a, gconstpointer b)
{
	const Report *ra = *(const Report * const *) a;
	const Report *rb = *(const Report * const *) b;

	if (ra->written != rb->written)
		return ra->written < rb->written ? -1 : 1;

	/* Second resolution, so ties are ordinary. Without a tie-break the newest
	   moves between launches and the announcement repeats. */
	return g_strcmp0 (ra->name, rb->name);
}

/* Two jobs at startup: say that an earlier run left a report, since that run
   had no chance to, and drop the oldest so the folder cannot grow forever. */
static void
sweep_old_reports (void)
{
	g_autoptr (GPtrArray) reports = g_ptr_array_new_with_free_func (report_free);
	g_autoptr (GDir) dir = NULL;
	g_autofree char *marker = NULL;
	g_autofree char *seen = NULL;
	const char *entry;
	const char *newest;
	guint i;

	dir = g_dir_open (report_dir, 0, NULL);
	if (dir == NULL)
		return;

	while ((entry = g_dir_read_name (dir)) != NULL) {
		g_autofree char *path = NULL;
		GStatBuf info;
		Report *report;

		if (!g_str_has_prefix (entry, "crash-") || !g_str_has_suffix (entry, ".txt"))
			continue;

		path = g_build_filename (report_dir, entry, NULL);
		if (g_stat (path, &info) != 0)
			continue;

		report = g_new0 (Report, 1);
		report->name = g_strdup (entry);
		report->written = (gint64) info.st_mtime;
		g_ptr_array_add (reports, report);
	}

	if (reports->len == 0)
		return;

	g_ptr_array_sort (reports, compare_written);

	/* Said once per report rather than once per launch, which matters when
	   every window is its own process. */
	newest = ((const Report *) g_ptr_array_index (reports, reports->len - 1))->name;
	marker = g_build_filename (report_dir, "last-seen", NULL);

	if (!g_file_get_contents (marker, &seen, NULL, NULL))
		seen = NULL;

	if (g_strcmp0 (seen, newest) != 0) {
		g_message ("An earlier run stopped unexpectedly. Its report is in %s",
			   report_dir);
		g_file_set_contents (marker, newest, -1, NULL);
	}

	for (i = 0; reports->len - i > CRASH_KEEP_REPORTS; i++) {
		const Report *report = g_ptr_array_index (reports, i);
		g_autofree char *old = g_build_filename (report_dir, report->name, NULL);

		g_unlink (old);
	}
}

/* Past here the program is already broken. Nothing below allocates, and on
   POSIX nothing below is outside what a signal handler may call. */

/* Two runs that start in the same second can be handed the same process id.
   The second one's report goes under "-2" and so on, rather than being lost. */
static void
number_written_path (gsize stem, char n)
{
	written_path[stem] = '-';
	written_path[stem + 1] = n;
	memcpy (written_path + stem + 2, ".txt", sizeof ".txt");
}

#ifndef G_OS_WIN32

static int
open_report (void)
{
	gsize stem = strlen (report_path) - strlen (".txt");
	char n;

	memcpy (written_path, report_path, strlen (report_path) + 1);

	for (n = '2'; ; n++) {
		int fd = open (written_path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);

		if (fd >= 0 || errno != EEXIST || n > '9')
			return fd;

		number_written_path (stem, n);
	}
}

static void
write_all (int fd, const char *s, size_t len)
{
	if (fd < 0)
		return;

	while (len > 0) {
		ssize_t n = write (fd, s, len);

		if (n < 0) {
			if (errno == EINTR)
				continue;
			return;
		}
		if (n == 0)
			return;

		s += n;
		len -= (size_t) n;
	}
}

static void
write_str (int fd, const char *s)
{
	write_all (fd, s, strlen (s));
}

static void
write_num (int fd, gsize value, int base)
{
	static const char digits[] = "0123456789abcdef";
	char buf[24];
	int i = (int) sizeof buf;

	if (value == 0) {
		write_str (fd, "0");
		return;
	}

	while (value > 0 && i > 0) {
		buf[--i] = digits[value % (gsize) base];
		value /= (gsize) base;
	}

	write_all (fd, buf + i, sizeof buf - (size_t) i);
}

static const char *
signal_name (int sig)
{
	switch (sig) {
	case SIGSEGV: return "SIGSEGV";
	case SIGBUS:  return "SIGBUS";
	case SIGILL:  return "SIGILL";
	case SIGFPE:  return "SIGFPE";
	case SIGABRT: return "SIGABRT";
	default:      return "signal";
	}
}

/* A fault the kernel raised, as opposed to the same signal sent by something.
   Only the first has an address in si_addr; a sent one carries whoever sent
   it there, which reads as a plausible code address and is not one. */
static gboolean
is_real_fault (int sig, const siginfo_t *info)
{
	if (info == NULL || info->si_code <= 0)
		return FALSE;

	/* Linux numbers every sent code at or below zero. The BSDs do not. */
	if (info->si_code == SI_USER || info->si_code == SI_QUEUE)
		return FALSE;
#ifdef SI_LWP
	if (info->si_code == SI_LWP)
		return FALSE;
#endif

	return sig == SIGSEGV || sig == SIGBUS || sig == SIGILL || sig == SIGFPE;
}

static void
write_header (int fd, int sig, const siginfo_t *info)
{
	write_str (fd, "nemo-anywhere " NEMO_VERSION_STRING "\n");
	write_str (fd, "started ");
	write_str (fd, started_stamp);
	write_str (fd, "\ndied on ");
	write_str (fd, signal_name (sig));
	write_str (fd, " (");
	write_num (fd, (gsize) sig, 10);
	write_str (fd, ")");

	if (is_real_fault (sig, info)) {
		write_str (fd, " at 0x");
		write_num (fd, (gsize) info->si_addr, 16);
	}

	write_str (fd, "\npid ");
	write_num (fd, (gsize) getpid (), 10);
	write_str (fd, "\n\nstack (addr2line -e <module> <the bare +0x in parentheses>):\n");
}

#ifdef CRASH_STEP_BAD_JUMP

/* A call through a null or freed pointer faults on arrival, at an address with
   no unwind data, so the unwinder stops there after two frames. The caller's
   return address is still on top of the stack, and the unwinder reads the
   interrupted registers back out of this very context. Pointing them at the
   caller for the length of the walk recovers the rest. */
static gboolean
step_past_bad_jump (int sig, const siginfo_t *info, ucontext_t *uc, crash_reg_t saved[2])
{
	if (sig != SIGSEGV || !is_real_fault (sig, info))
		return FALSE;

	if ((crash_reg_t) (gsize) info->si_addr != CRASH_PC (uc))
		return FALSE;

	saved[0] = CRASH_PC (uc);
	saved[1] = CRASH_SP (uc);

	/* One byte back, inside the call. A signal frame's address is looked up
	   as it stands, not as a return address, and a call that is the last
	   thing in its function returns into the next one. */
	CRASH_PC (uc) = *(crash_reg_t *) (gsize) CRASH_SP (uc) - 1;
	CRASH_SP (uc) += 8;

	return TRUE;
}

#endif

static volatile gint handling = 0;

static void
crash_signal_handler (int sig, siginfo_t *info, void *context)
{
	void *frames[CRASH_MAX_FRAMES];
	int n_frames = 0;
	sigset_t unblock;
	int fd;
#ifdef CRASH_STEP_BAD_JUMP
	crash_reg_t saved[2];
	gboolean stepped;
#endif

	(void) context;
	(void) frames;

	/* One report per process. A second thread faulting waits rather than
	   truncating the first one's report, but not forever: the thread writing
	   it can be stuck behind a lock the crash left held. */
	if (!g_atomic_int_compare_and_exchange (&handling, 0, 1)) {
		struct timespec wait = { 5, 0 };

		nanosleep (&wait, NULL);
		_exit (128 + sig);
	}

	/* mkdir is a bare syscall wrapper, so it is safe here. It and the open
	   are both allowed to fail: stderr still gets the report. */
	mkdir (report_parent, DEFAULT_NEMO_DIRECTORY_MODE);
	mkdir (report_dir, 0700);
	fd = open_report ();

	/* Everything known for certain goes out before the stack is collected.
	   Walking it is the part that can fault again, and a report that says
	   only what killed the program still beats no report. */
	write_header (fd, sig, info);

	if (fd >= 0) {
		write_str (STDERR_FILENO, "\nnemo-anywhere crashed. Report written to ");
		write_str (STDERR_FILENO, written_path);
		write_str (STDERR_FILENO, "\n");
	}

	write_header (STDERR_FILENO, sig, info);

#if HAVE_BACKTRACE
#ifdef CRASH_STEP_BAD_JUMP
	stepped = step_past_bad_jump (sig, info, context, saved);
#endif

	n_frames = backtrace (frames, CRASH_MAX_FRAMES);

#ifdef CRASH_STEP_BAD_JUMP
	/* Put back before anything can return into it. */
	if (stepped) {
		CRASH_PC ((ucontext_t *) context) = saved[0];
		CRASH_SP ((ucontext_t *) context) = saved[1];
	}
#endif

	if (n_frames > 0) {
		backtrace_symbols_fd (frames, n_frames, fd);
		backtrace_symbols_fd (frames, n_frames, STDERR_FILENO);
	} else
#endif
	{
		write_str (fd, "  not available in this build\n");
		write_str (STDERR_FILENO, "  not available in this build\n");
	}

	if (fd >= 0)
		close (fd);

	signal (sig, SIG_DFL);

	/* A real fault happens again the moment this returns, now with no
	   handler, so a core file and a debugger stop on the fault itself rather
	   than in here. */
	if (is_real_fault (sig, info))
		return;

	/* A sent signal has nothing to repeat it, so it is raised again. It is
	   blocked on the way in here, so it has to be unblocked or the raise only
	   marks it pending and the _exit wins. */
	sigemptyset (&unblock);
	sigaddset (&unblock, sig);
	pthread_sigmask (SIG_UNBLOCK, &unblock, NULL);
	raise (sig);

	_exit (128 + sig);
}

static void
install_posix (void)
{
	static const int signals[] = { SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT };
	/* Only the thread that registers it gets an alternate stack, so a stack
	   overflow is caught on the main thread and not on a worker. */
	static char alt_stack[64 * 1024];
	stack_t ss;
	gsize i;

#if HAVE_BACKTRACE
	{
		/* The first backtrace loads the unwinder, which is not something to
		   do from inside a handler. */
		void *warmup[4];

		backtrace (warmup, G_N_ELEMENTS (warmup));
	}
#endif

	ss.ss_sp = alt_stack;
	ss.ss_size = sizeof alt_stack;
	ss.ss_flags = 0;
	sigaltstack (&ss, NULL);

	for (i = 0; i < G_N_ELEMENTS (signals); i++) {
		struct sigaction sa;

		memset (&sa, 0, sizeof sa);
		sa.sa_sigaction = crash_signal_handler;
		sa.sa_flags = SA_SIGINFO | SA_ONSTACK;

		/* Everything else fatal stays blocked while the report is written,
		   or a second one arriving cuts it in half. */
		sigfillset (&sa.sa_mask);

		sigaction (signals[i], &sa, NULL);
	}
}

#else /* G_OS_WIN32 */

static HANDLE
create_report (void)
{
	gsize stem = strlen (report_path) - strlen (".txt");
	gsize stem_w = wcslen (report_path_w) - wcslen (L".txt");
	char n;

	memcpy (written_path, report_path, strlen (report_path) + 1);
	wcscpy (written_path_w, report_path_w);

	for (n = '2'; ; n++) {
		HANDLE file = CreateFileW (written_path_w, GENERIC_WRITE, FILE_SHARE_READ,
					   NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);

		if (file != INVALID_HANDLE_VALUE || GetLastError () != ERROR_FILE_EXISTS || n > '9')
			return file;

		number_written_path (stem, n);
		written_path_w[stem_w] = L'-';
		written_path_w[stem_w + 1] = (wchar_t) n;
		wcscpy (written_path_w + stem_w + 2, L".txt");
	}
}

/* The report is built once and written twice, so the unwind is not paid for
   again on the way to stderr. */
static char report_text[16 * 1024];
static gsize report_len = 0;

static void
emit_str (const char *s)
{
	static const char cut[] = "  (report truncated)\n";
	static gboolean full = FALSE;
	gsize len = strlen (s);

	if (full)
		return;

	if (report_len + len + sizeof cut >= sizeof report_text) {
		full = TRUE;
		memcpy (report_text + report_len, cut, sizeof cut - 1);
		report_len += sizeof cut - 1;
		return;
	}

	memcpy (report_text + report_len, s, len);
	report_len += len;
}

static void
emit_num (guint64 value, int base, int pad)
{
	static const char digits[] = "0123456789abcdef";
	char buf[24];
	int i = (int) sizeof buf;

	buf[--i] = '\0';

	do {
		buf[--i] = digits[value % (guint64) base];
		value /= (guint64) base;
		pad--;
	} while ((value > 0 || pad > 0) && i > 0);

	emit_str (buf + i);
}

static void
write_handle (HANDLE h, const char *s, gsize len)
{
	DWORD written = 0;

	if (h == NULL || h == INVALID_HANDLE_VALUE)
		return;

	WriteFile (h, s, (DWORD) len, &written, NULL);
}

static void
flush_report (HANDLE file)
{
	write_handle (file, report_text, report_len);
	write_handle (GetStdHandle (STD_ERROR_HANDLE), report_text, report_len);
	report_len = 0;
}

static const char *
exception_name (DWORD code)
{
	switch (code) {
	case EXCEPTION_ACCESS_VIOLATION:      return "access violation";
	case EXCEPTION_STACK_OVERFLOW:        return "stack overflow";
	case EXCEPTION_ILLEGAL_INSTRUCTION:   return "illegal instruction";
	case EXCEPTION_INT_DIVIDE_BY_ZERO:    return "integer divide by zero";
	case EXCEPTION_FLT_DIVIDE_BY_ZERO:    return "float divide by zero";
	case EXCEPTION_IN_PAGE_ERROR:         return "page error";
	case EXCEPTION_PRIV_INSTRUCTION:      return "privileged instruction";
	case STATUS_FATAL_APP_EXIT:           return "aborted";
	default:                              return "exception";
	}
}

/* A crashed process can hand over anything, and a read that faults in here
   loses the whole report, so every address off the stack is checked first. */
static gboolean
readable (const void *p, gsize len)
{
	MEMORY_BASIC_INFORMATION mbi;
	const char *start = p;

	if (p == NULL)
		return FALSE;

	if (VirtualQuery (p, &mbi, sizeof mbi) != sizeof mbi)
		return FALSE;

	if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) != 0)
		return FALSE;

	return (const char *) mbi.BaseAddress + mbi.RegionSize >= start + len;
}

/* Where the module wanted to be loaded. Frames are reported at that address
   rather than the one they ran at, so the number in the report is the one
   addr2line takes. A mingw build carries no PDB, so this is all a frame can
   be made to mean on another machine. */
static DWORD64
preferred_base (DWORD64 loaded_base)
{
	const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *) (UINT_PTR) loaded_base;
	const IMAGE_NT_HEADERS *nt;

	if (!readable (dos, sizeof *dos) || dos->e_magic != IMAGE_DOS_SIGNATURE)
		return loaded_base;

	if (dos->e_lfanew <= 0 || dos->e_lfanew > 0x10000)
		return loaded_base;

	nt = (const IMAGE_NT_HEADERS *) ((const char *) dos + dos->e_lfanew);

	if (!readable (nt, sizeof *nt) || nt->Signature != IMAGE_NT_SIGNATURE)
		return loaded_base;

	return nt->OptionalHeader.ImageBase;
}

#if defined (__x86_64__) || defined (_M_X64)

enum {
	UWOP_PUSH_NONVOL = 0,
	UWOP_ALLOC_LARGE = 1,
	UWOP_ALLOC_SMALL = 2,
	UWOP_SET_FPREG = 3,
	UWOP_SAVE_NONVOL = 4,
	UWOP_SAVE_NONVOL_FAR = 5,
	UWOP_SAVE_XMM128 = 8,
	UWOP_SAVE_XMM128_FAR = 9,
	UWOP_PUSH_MACHFRAME = 10,
};

static gboolean
stack_readable (DWORD64 address, gsize len)
{
	return readable ((const void *) (UINT_PTR) address, len);
}

static DWORD64
context_register (const CONTEXT *context, guint reg)
{
	static const gsize offsets[16] = {
		G_STRUCT_OFFSET (CONTEXT, Rax), G_STRUCT_OFFSET (CONTEXT, Rcx),
		G_STRUCT_OFFSET (CONTEXT, Rdx), G_STRUCT_OFFSET (CONTEXT, Rbx),
		G_STRUCT_OFFSET (CONTEXT, Rsp), G_STRUCT_OFFSET (CONTEXT, Rbp),
		G_STRUCT_OFFSET (CONTEXT, Rsi), G_STRUCT_OFFSET (CONTEXT, Rdi),
		G_STRUCT_OFFSET (CONTEXT, R8),  G_STRUCT_OFFSET (CONTEXT, R9),
		G_STRUCT_OFFSET (CONTEXT, R10), G_STRUCT_OFFSET (CONTEXT, R11),
		G_STRUCT_OFFSET (CONTEXT, R12), G_STRUCT_OFFSET (CONTEXT, R13),
		G_STRUCT_OFFSET (CONTEXT, R14), G_STRUCT_OFFSET (CONTEXT, R15),
	};

	return G_STRUCT_MEMBER (DWORD64, context, offsets[reg & 15]);
}

static guint
unwind_code_slots (guint op, guint op_info)
{
	switch (op) {
	case UWOP_ALLOC_LARGE:
		return op_info == 0 ? 2 : 3;
	case UWOP_SAVE_NONVOL:
	case 6:
	case UWOP_SAVE_XMM128:
		return 2;
	case UWOP_SAVE_NONVOL_FAR:
	case 7:
	case UWOP_SAVE_XMM128_FAR:
		return 3;
	default:
		return 1;
	}
}

/* RtlVirtualUnwind reads saved registers and the return address off the
   frame with no checks of its own, and one wild read loses the rest of the
   report. So the unwind codes are followed here first, reading nothing, and
   each address the unwinder is about to read is checked. */
static gboolean
frame_readable (const CONTEXT *context, DWORD64 image_base, DWORD64 pc,
		PRUNTIME_FUNCTION function)
{
	DWORD64 rsp = context->Rsp;
	DWORD64 offset = pc - (image_base + function->BeginAddress);
	int chain;

	for (chain = 0; chain < 32; chain++) {
		const BYTE *info = (const BYTE *) (UINT_PTR) (image_base + function->UnwindData);
		const BYTE *code;
		gboolean chained;
		guint n_codes;
		guint slots;
		guint i;

		if (!readable (info, 4))
			return FALSE;

		chained = ((info[0] >> 3) & UNW_FLAG_CHAININFO) != 0;
		n_codes = info[2];
		slots = (n_codes + 1) & ~1u;
		code = info + 4;

		if (!readable (info, 4 + 2 * (gsize) slots + (chained ? sizeof *function : 0)))
			return FALSE;

		for (i = 0; i < n_codes; ) {
			guint op = code[2 * i + 1] & 0x0f;
			guint op_info = code[2 * i + 1] >> 4;
			guint word = i + 1 < n_codes ? code[2 * i + 2] | (code[2 * i + 3] << 8) : 0;
			guint32 large = i + 2 < n_codes
				? word | ((guint32) (code[2 * i + 4] | (code[2 * i + 5] << 8)) << 16)
				: 0;
			guint step = unwind_code_slots (op, op_info);

			/* Still in the prolog, only what it has done so far is undone. */
			if (offset < info[1] && code[2 * i] > offset) {
				i += step;
				continue;
			}

			switch (op) {
			case UWOP_PUSH_NONVOL:
				if (!stack_readable (rsp, 8))
					return FALSE;
				rsp += 8;
				break;
			case UWOP_ALLOC_LARGE:
				rsp += op_info == 0 ? 8 * (DWORD64) word : large;
				break;
			case UWOP_ALLOC_SMALL:
				rsp += 8 * (DWORD64) op_info + 8;
				break;
			case UWOP_SET_FPREG:
				rsp = context_register (context, info[3] & 0x0f) - 16 * (DWORD64) (info[3] >> 4);
				break;
			case UWOP_SAVE_NONVOL:
				if (!stack_readable (rsp + 8 * (DWORD64) word, 8))
					return FALSE;
				break;
			case UWOP_SAVE_NONVOL_FAR:
				if (!stack_readable (rsp + large,8))
					return FALSE;
				break;
			case UWOP_SAVE_XMM128:
				if (!stack_readable (rsp + 16 * (DWORD64) word, 16))
					return FALSE;
				break;
			case UWOP_SAVE_XMM128_FAR:
				if (!stack_readable (rsp + large,16))
					return FALSE;
				break;
			case UWOP_PUSH_MACHFRAME:
				/* The whole interrupted frame, return address included. */
				return stack_readable (rsp + (op_info != 0 ? 8 : 0), 40);
			default:
				break;
			}

			i += step;
		}

		if (!chained)
			break;

		function = (PRUNTIME_FUNCTION) (code + 2 * slots);
		offset = G_MAXUINT64;
	}

	return stack_readable (rsp, 8);
}

/* The OS unwinder, rather than dbghelp: StackWalk64 needs SymInitialize, which
   enumerates every loaded module under the loader lock. A crash under that lock
   is exactly the case this has to survive. */
static void
emit_stack (const CONTEXT *context, HANDLE file)
{
	static CONTEXT walk;
	static UNWIND_HISTORY_TABLE history;
	int depth;

	walk = *context;
	memset (&history, 0, sizeof history);

	emit_str ("stack (addr2line -e <module> <address>):\n");

	for (depth = 0; depth < CRASH_MAX_FRAMES; depth++) {
		PRUNTIME_FUNCTION function;
		DWORD64 image_base = 0;
		DWORD64 pc = walk.Rip;
		DWORD64 probe;
		char module[MAX_PATH];

		/* Past the first frame the address is a RETURN address, and for a
		   call that never comes back it belongs to the next function along.
		   One byte back is inside the call itself. */
		probe = depth == 0 ? pc : pc - 1;

		function = RtlLookupFunctionEntry (probe, &image_base, &history);

		emit_str ("  0x");
		emit_num (image_base != 0
			  ? preferred_base (image_base) + (probe - image_base)
			  : probe, 16, 16);

		if (image_base != 0 &&
		    GetModuleFileNameA ((HMODULE) (UINT_PTR) image_base, module,
					sizeof module) > 0) {
			const char *leaf = strrchr (module, '\\');

			emit_str ("  ");
			emit_str (leaf != NULL ? leaf + 1 : module);
			emit_str ("+0x");
			emit_num (probe - image_base, 16, 0);
		}

		emit_str ("\n");

		/* A frame at a time, so a walk that faults anyway keeps what it had. */
		flush_report (file);

		if (function == NULL) {
			/* A leaf, or a jump to an address with no code behind it at
			   all - the commonest shape of a call through a freed object.
			   Either way the return address is on top of the stack. */
			if (!stack_readable (walk.Rsp, 8)) {
				emit_str ("  (the stack cannot be read past here)\n");
				break;
			}

			walk.Rip = *(DWORD64 *) (UINT_PTR) walk.Rsp;
			walk.Rsp += 8;
		} else {
			PVOID handler_data;
			DWORD64 establisher;
			DWORD64 before = walk.Rsp;

			if (!frame_readable (&walk, image_base, probe, function)) {
				emit_str ("  (the stack cannot be read past here)\n");
				break;
			}

			/* The same address the lookup used, or the unwinder decides
			   whether it is in an epilogue by reading the wrong function. */
			RtlVirtualUnwind (UNW_FLAG_NHANDLER, image_base, probe, function,
					  &walk, &handler_data, &establisher, NULL);

			/* Every frame pops at least its return address. One that does
			   not has unwind data leading back to itself, and would repeat
			   to the end of the report. */
			if (walk.Rsp <= before) {
				emit_str ("  (the stack unwinds back into itself here)\n");
				break;
			}
		}

		if (walk.Rip == 0)
			break;
	}
}

#else

static void
emit_stack (const CONTEXT *context, HANDLE file)
{
	(void) context;
	(void) file;

	emit_str ("stack:\n  not available in this build\n");
}

#endif

static volatile gint handling = 0;
static volatile DWORD reporting_thread = 0;
static gboolean quiet = FALSE;

static void
report_and_die (EXCEPTION_POINTERS *info, gboolean has_address)
{
	const EXCEPTION_RECORD *record = info->ExceptionRecord;
	HANDLE file;

	if (!g_atomic_int_compare_and_exchange (&handling, 0, 1)) {
		/* Either this thread faulted again inside the filter, in which case
		   there is nothing left to try, or another thread is writing the
		   report and is worth a short wait but not an unbounded one. */
		if (reporting_thread != GetCurrentThreadId ())
			Sleep (5000);

		TerminateProcess (GetCurrentProcess (), record->ExceptionCode);
	}

	reporting_thread = GetCurrentThreadId ();

	CreateDirectoryW (report_parent_w, NULL);
	CreateDirectoryW (report_dir_w, NULL);

	file = create_report ();

	/* Nothing reads stderr in a windowed build, but a console one and every
	   test do. */
	if (file != INVALID_HANDLE_VALUE) {
		static const char wrote[] = "\nnemo-anywhere crashed. Report written to ";
		HANDLE err = GetStdHandle (STD_ERROR_HANDLE);

		write_handle (err, wrote, sizeof wrote - 1);
		write_handle (err, written_path, strlen (written_path));
		write_handle (err, "\n", 1);
	}

	emit_str ("nemo-anywhere " NEMO_VERSION_STRING "\n");
	emit_str ("started ");
	emit_str (started_stamp);
	emit_str ("\ndied on ");
	emit_str (exception_name (record->ExceptionCode));
	emit_str (" (0x");
	emit_num (record->ExceptionCode, 16, 8);
	emit_str (")");

	if (has_address) {
		emit_str (" at 0x");
		emit_num ((guint64) (UINT_PTR) record->ExceptionAddress, 16, 0);
	}

	emit_str ("\npid ");
	emit_num (GetCurrentProcessId (), 10, 0);
	emit_str ("\n\n");

	/* Everything known for certain goes out before the stack is walked.
	   Walking it is the part that can fault again, and a report that says
	   only what killed the program still beats no report. */
	flush_report (file);

	emit_stack (info->ContextRecord, file);
	flush_report (file);

	if (file != INVALID_HANDLE_VALUE)
		CloseHandle (file);

	/* Last, and only once the report is safely on disk: this runs a modal
	   loop, which dispatches messages back into the code that just died, so
	   it is allowed to fail. Not translated, because loading a catalog in a
	   broken process is one more thing that can go wrong. */
	if (!quiet) {
		static wchar_t message[CRASH_PATH_MAX + 128];

		if (file != INVALID_HANDLE_VALUE) {
			_snwprintf (message, G_N_ELEMENTS (message) - 1,
				    L"Nemo Anywhere stopped unexpectedly.\n\n"
				    L"A report was written to:\n%ls",
				    written_path_w);
		} else {
			_snwprintf (message, G_N_ELEMENTS (message) - 1,
				    L"Nemo Anywhere stopped unexpectedly.\n\n"
				    L"No report could be written to:\n%ls",
				    written_path_w);
		}
		message[G_N_ELEMENTS (message) - 1] = L'\0';

		MessageBoxW (NULL, message, L"Nemo Anywhere",
			     MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
	}

	/* Exit with the cause, so a launcher or a smoke script still sees which
	   one it was. The cost is that Windows Error Reporting never buckets it,
	   which is a trade for the report and the dialog above. */
	TerminateProcess (GetCurrentProcess (), record->ExceptionCode);
}

static LONG WINAPI
crash_exception_filter (EXCEPTION_POINTERS *info)
{
	report_and_die (info, TRUE);

	return EXCEPTION_EXECUTE_HANDLER;
}

/* abort() never reaches the unhandled-exception filter, and g_error and every
   failed assertion go out that way. */
static void
crash_abort_handler (int sig)
{
	static EXCEPTION_RECORD record;
	static EXCEPTION_POINTERS info;
	static CONTEXT context;

	(void) sig;

	RtlCaptureContext (&context);

	memset (&record, 0, sizeof record);
	record.ExceptionCode = STATUS_FATAL_APP_EXIT;

	info.ExceptionRecord = &record;
	info.ContextRecord = &context;

	/* An abort has no faulting address to report. */
	report_and_die (&info, FALSE);
}

static void
install_win32 (void)
{
	ULONG guarantee = 64 * 1024;

	quiet = !is_empty_env ("NEMO_NO_CRASH_DIALOG");

	/* Keeps enough stack in reserve for the filter to run after a stack
	   overflow, which is the one crash that otherwise reports nothing. Per
	   thread, and only this one, the same way the alternate stack is on POSIX. */
	SetThreadStackGuarantee (&guarantee);

	/* Whatever was there before is replaced on purpose: a packer's own filter
	   would take the crash and leave no report of ours. */
	SetUnhandledExceptionFilter (crash_exception_filter);
	signal (SIGABRT, crash_abort_handler);
}

#endif /* G_OS_WIN32 */

void
nemo_crash_handler_install (void)
{
	if (installed)
		return;

	if (!is_empty_env ("NEMO_NO_CRASH_HANDLER"))
		return;

	if (!build_paths ())
		return;

	installed = TRUE;

	sweep_old_reports ();

#ifdef G_OS_WIN32
	install_win32 ();
#else
	install_posix ();
#endif
}

/* Returns: (transfer none): kept for the life of the process */
const char *
nemo_crash_report_path (void)
{
	return installed ? report_path : NULL;
}
