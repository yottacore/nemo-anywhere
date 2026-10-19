/* The fence: a test may delete or move only inside its own folders.
 *
 * Its own folders are the cell the runner named in NEMO_TEST_SCRATCH (the
 * temp dir when a test is run by hand), plus every directory test_scratch_dir
 * made. On POSIX this file takes over unlink, rmdir, remove, rename and their
 * at forms for the whole process, so GLib, GIO, SQLite, libarchive and the
 * app's own code all go through it. On Windows it puts itself in front of the
 * same calls in every loaded module's import table. A refused call ends the
 * test, since an error that comes back is too easy to swallow.
 *
 * On Linux the bubblewrap jail (cicd/linux/jail.bash) stands behind this. On
 * the other boxes it's the only wall. A program the test starts, such as the
 * app itself, is not fenced unless it is a test binary too. */

#define _GNU_SOURCE

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define PSAPI_VERSION 2
#include <windows.h>
#include <psapi.h>
#else
#include <dlfcn.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <unistd.h>
#ifdef __FreeBSD__
#include <sys/types.h>
#include <sys/user.h>
#endif
#endif

#include "test-fence.h"

/* Room for every scratch dir a test makes. A few make a dozen. */
#define MAX_ROOTS 256

#ifndef _WIN32

/* GLib and the rest call these through the PLT, and the definitions below
   have to win over libc's there. A build with hidden visibility would keep
   them to this binary. */
#if defined(__GNUC__)
#define FENCE_EXPORT __attribute__ ((visibility ("default")))
#else
#define FENCE_EXPORT
#endif

typedef int (*path_fn)      (const char *);
typedef int (*unlinkat_fn)  (int, const char *, int);
typedef int (*rename_fn)    (const char *, const char *);
typedef int (*renameat_fn)  (int, const char *, int, const char *);
#ifdef __linux__
typedef int (*renameat2_fn) (int, const char *, int, const char *, unsigned int);
#endif

static path_fn      real_unlink;
static path_fn      real_rmdir;
static path_fn      real_remove;
static unlinkat_fn  real_unlinkat;
static rename_fn    real_rename;
static renameat_fn  real_renameat;
#ifdef __linux__
static renameat2_fn real_renameat2;
#endif

static char *roots[MAX_ROOTS];
static int n_roots;
static pthread_mutex_t roots_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_once_t once = PTHREAD_ONCE_INIT;

static void *
real_of (const char *name)
{
	void *fn = dlsym (RTLD_NEXT, name);

	if (fn == NULL) {
		fprintf (stderr, "fence: no %s to call\n", name);
		abort ();
	}
	return fn;
}

static void
add_root (const char *dir)
{
	char canon[PATH_MAX];
	const char *use = dir;
	int i;

	if (dir == NULL || dir[0] != '/') {
		return;
	}
	if (realpath (dir, canon) != NULL) {
		use = canon;
	}
	/* Never the whole box, whatever the environment says. */
	if (strcmp (use, "/") == 0) {
		return;
	}

	pthread_mutex_lock (&roots_lock);
	for (i = 0; i < n_roots && strcmp (roots[i], use) != 0; i++) {
	}
	if (i == n_roots) {
		if (n_roots < MAX_ROOTS && (roots[n_roots] = strdup (use)) != NULL) {
			n_roots++;
		} else {
			fprintf (stderr, "fence: no room for %s\n", dir);
		}
	}
	pthread_mutex_unlock (&roots_lock);
}

static void
init (void)
{
	const char *cell = getenv ("NEMO_TEST_SCRATCH");

	/* All of them now, so a call never has to look one up halfway. */
	real_unlink    = (path_fn)     real_of ("unlink");
	real_rmdir     = (path_fn)     real_of ("rmdir");
	real_remove    = (path_fn)     real_of ("remove");
	real_unlinkat  = (unlinkat_fn) real_of ("unlinkat");
	real_rename    = (rename_fn)   real_of ("rename");
	real_renameat  = (renameat_fn) real_of ("renameat");
#ifdef __linux__
	real_renameat2 = (renameat2_fn) real_of ("renameat2");
#endif

	if (cell != NULL && cell[0] != '\0') {
		char *copy = strdup (cell);
		char *save = NULL;
		char *part;

		for (part = copy != NULL ? strtok_r (copy, ":", &save) : NULL; part != NULL;
		     part = strtok_r (NULL, ":", &save)) {
			add_root (part);
		}
		free (copy);
	} else {
		const char *tmp = getenv ("TMPDIR");

		add_root (tmp != NULL && tmp[0] == '/' ? tmp : "/tmp");
	}
}

/* Runs before main, so a test is fenced from its first line. */
__attribute__ ((constructor)) static void
fence_start (void)
{
	pthread_once (&once, init);
}

static int
dir_of_fd (int dirfd, char out[PATH_MAX])
{
	if (dirfd == AT_FDCWD) {
		return getcwd (out, PATH_MAX) != NULL;
	}
#if defined(__linux__)
	{
		char link[64];
		ssize_t n;

		snprintf (link, sizeof link, "/proc/self/fd/%d", dirfd);
		n = readlink (link, out, PATH_MAX - 1);
		if (n <= 0) {
			return 0;
		}
		out[n] = '\0';
		return out[0] == '/';
	}
#elif defined(__FreeBSD__) && defined(F_KINFO)
	{
		struct kinfo_file info;

		memset (&info, 0, sizeof info);
		info.kf_structsize = KINFO_FILE_SIZE;
		if (fcntl (dirfd, F_KINFO, &info) != 0 || info.kf_path[0] != '/') {
			return 0;
		}
		snprintf (out, PATH_MAX, "%s", info.kf_path);
		return 1;
	}
#else
	return 0;
#endif
}

/* Where a delete of path lands: the folder it sits in with every link
   resolved, plus its last name as it stands, since a delete takes the link and
   not what it points at. 1 when found. 0 when the folder isn't there to
   resolve, so the real call fails on its own. -1 when the folder of an open
   descriptor can't be named, which is refused. */
static int
resolve (int dirfd, const char *path, char out[PATH_MAX])
{
	char full[PATH_MAX], dir[PATH_MAX];
	char *slash, *name;
	size_t len;
	int n;

	if (path[0] == '/') {
		n = snprintf (full, sizeof full, "%s", path);
	} else {
		if (!dir_of_fd (dirfd, dir)) {
			return dirfd == AT_FDCWD ? 0 : -1;
		}
		n = snprintf (full, sizeof full, "%s/%s", dir, path);
	}
	if (n < 0 || (size_t) n >= sizeof full) {
		return 0;
	}

	len = strlen (full);
	while (len > 1 && full[len - 1] == '/') {
		full[--len] = '\0';
	}
	slash = strrchr (full, '/');
	name = slash + 1;
	if (strcmp (name, ".") == 0 || strcmp (name, "..") == 0 || name[0] == '\0') {
		return realpath (full, out) != NULL;
	}

	if (slash == full) {
		snprintf (dir, sizeof dir, "/");
	} else {
		*slash = '\0';
		if (realpath (full, dir) == NULL) {
			return 0;
		}
	}
	n = snprintf (out, PATH_MAX, "%s/%s", strcmp (dir, "/") == 0 ? "" : dir, name);
	return n >= 0 && n < PATH_MAX;
}

static int
inside (const char *canon)
{
	int i, found = 0;

	pthread_mutex_lock (&roots_lock);
	for (i = 0; i < n_roots && !found; i++) {
		size_t len = strlen (roots[i]);

		found = strncmp (canon, roots[i], len) == 0 && (canon[len] == '/' || canon[len] == '\0');
	}
	pthread_mutex_unlock (&roots_lock);
	return found;
}

static void
refuse (const char *what, const char *path, const char *canon)
{
	int i;

	fprintf (stderr, "fence: refused to %s %s", what, path);
	if (canon != NULL && strcmp (canon, path) != 0) {
		fprintf (stderr, " (%s)", canon);
	}
	fprintf (stderr, ", which is outside this test's own folders:\n");
	for (i = 0; i < n_roots; i++) {
		fprintf (stderr, "fence:     %s\n", roots[i]);
	}
	abort ();
}

static void
fence (const char *what, int dirfd, const char *path)
{
	char canon[PATH_MAX];
	int found;

	pthread_once (&once, init);
	if (path == NULL) {
		return;
	}
	found = resolve (dirfd, path, canon);
	if (found == 0 || (found > 0 && inside (canon))) {
		return;
	}
	refuse (what, path, found > 0 ? canon : NULL);
}

void
test_fence_allow (const char *dir)
{
	pthread_once (&once, init);
	add_root (dir);
}

int
test_fence_allows (const char *path)
{
	char canon[PATH_MAX];

	pthread_once (&once, init);
	return resolve (AT_FDCWD, path, canon) > 0 && inside (canon);
}

FENCE_EXPORT int
unlink (const char *path)
{
	fence ("delete", AT_FDCWD, path);
	return real_unlink (path);
}

FENCE_EXPORT int
rmdir (const char *path)
{
	fence ("delete", AT_FDCWD, path);
	return real_rmdir (path);
}

FENCE_EXPORT int
remove (const char *path)
{
	fence ("delete", AT_FDCWD, path);
	return real_remove (path);
}

FENCE_EXPORT int
unlinkat (int dirfd, const char *path, int flags)
{
	fence ("delete", dirfd, path);
	return real_unlinkat (dirfd, path, flags);
}

FENCE_EXPORT int
rename (const char *from, const char *to)
{
	fence ("move", AT_FDCWD, from);
	fence ("move onto", AT_FDCWD, to);
	return real_rename (from, to);
}

FENCE_EXPORT int
renameat (int from_dir, const char *from, int to_dir, const char *to)
{
	fence ("move", from_dir, from);
	fence ("move onto", to_dir, to);
	return real_renameat (from_dir, from, to_dir, to);
}

#ifdef __linux__
FENCE_EXPORT int
renameat2 (int from_dir, const char *from, int to_dir, const char *to, unsigned int flags)
{
	fence ("move", from_dir, from);
	fence ("move onto", to_dir, to);
	return real_renameat2 (from_dir, from, to_dir, to, flags);
}
#endif

#else /* _WIN32 */

/* Paths are compared in their final form, junctions and 8.3 names resolved,
   with no \\?\ in front. A long path past this is let through to fail on its
   own, as it would without the fence. */
#define FENCE_PATH 4096

static wchar_t *roots[MAX_ROOTS];
static int n_roots;
static SRWLOCK roots_lock = SRWLOCK_INIT;
static INIT_ONCE once = INIT_ONCE_STATIC_INIT;
static SRWLOCK patch_lock = SRWLOCK_INIT;

static int
final_path (const wchar_t *path, wchar_t *out, DWORD size)
{
	HANDLE handle;
	DWORD n;

	handle = CreateFileW (path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
			      OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
	if (handle == INVALID_HANDLE_VALUE) {
		return 0;
	}
	n = GetFinalPathNameByHandleW (handle, out, size, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
	CloseHandle (handle);
	if (n == 0 || n >= size) {
		return 0;
	}

	if (wcsncmp (out, L"\\\\?\\UNC\\", 8) == 0) {
		memmove (out + 2, out + 8, (wcslen (out + 8) + 1) * sizeof (wchar_t));
	} else if (wcsncmp (out, L"\\\\?\\", 4) == 0) {
		memmove (out, out + 4, (wcslen (out + 4) + 1) * sizeof (wchar_t));
	}
	return 1;
}

static void
add_root_w (const wchar_t *dir)
{
	wchar_t full[FENCE_PATH], canon[FENCE_PATH];
	const wchar_t *use;
	DWORD n;
	size_t len;
	int i;

	n = GetFullPathNameW (dir, FENCE_PATH, full, NULL);
	if (n == 0 || n >= FENCE_PATH) {
		return;
	}
	use = final_path (full, canon, FENCE_PATH) ? canon : full;
	len = wcslen (use);
	while (len > 0 && (use[len - 1] == L'\\' || use[len - 1] == L'/')) {
		len--;
	}
	/* A drive root is not a scratch folder. */
	if (len <= 2) {
		return;
	}

	AcquireSRWLockExclusive (&roots_lock);
	for (i = 0; i < n_roots && !(wcslen (roots[i]) == len && _wcsnicmp (roots[i], use, len) == 0); i++) {
	}
	if (i == n_roots) {
		if (n_roots < MAX_ROOTS && (roots[n_roots] = calloc (len + 1, sizeof (wchar_t))) != NULL) {
			memcpy (roots[n_roots], use, len * sizeof (wchar_t));
			n_roots++;
		} else {
			fprintf (stderr, "fence: no room for another folder\n");
		}
	}
	ReleaseSRWLockExclusive (&roots_lock);
}

static int
widen (UINT code_page, const char *text, wchar_t *out, int size)
{
	return text != NULL && MultiByteToWideChar (code_page, 0, text, -1, out, size) > 0;
}

static int
resolve_w (const wchar_t *path, wchar_t out[FENCE_PATH])
{
	wchar_t full[FENCE_PATH], dir[FENCE_PATH];
	wchar_t *cut, *name;
	DWORD n;
	size_t len;
	int written;

	n = GetFullPathNameW (path, FENCE_PATH, full, NULL);
	if (n == 0 || n >= FENCE_PATH) {
		return 0;
	}
	len = wcslen (full);
	while (len > 3 && (full[len - 1] == L'\\' || full[len - 1] == L'/')) {
		full[--len] = L'\0';
	}
	cut = wcsrchr (full, L'\\');
	if (cut == NULL) {
		return final_path (full, out, FENCE_PATH);
	}
	name = cut + 1;
	*cut = L'\0';
	if (!final_path (full, dir, FENCE_PATH)) {
		return 0;
	}
	len = wcslen (dir);
	written = _snwprintf (out, FENCE_PATH, L"%ls%ls%ls", dir,
			      len > 0 && dir[len - 1] == L'\\' ? L"" : L"\\", name);
	return written > 0 && written < FENCE_PATH;
}

static int
inside_w (const wchar_t *canon)
{
	int i, found = 0;

	AcquireSRWLockShared (&roots_lock);
	for (i = 0; i < n_roots && !found; i++) {
		size_t len = wcslen (roots[i]);

		found = _wcsnicmp (canon, roots[i], len) == 0 && (canon[len] == L'\\' || canon[len] == L'\0');
	}
	ReleaseSRWLockShared (&roots_lock);
	return found;
}

static void
refuse_w (const char *what, const wchar_t *path, const wchar_t *canon)
{
	int i;

	fprintf (stderr, "fence: refused to %s %ls", what, path);
	if (canon != NULL && wcscmp (canon, path) != 0) {
		fprintf (stderr, " (%ls)", canon);
	}
	fprintf (stderr, ", which is outside this test's own folders:\n");
	for (i = 0; i < n_roots; i++) {
		fprintf (stderr, "fence:     %ls\n", roots[i]);
	}
	fflush (stderr);
	/* abort() can stop on a dialog; this can't. 3 is what abort() exits with. */
	TerminateProcess (GetCurrentProcess (), 3);
}

static void fence_init (void);

static void
fence_w (const char *what, const wchar_t *path)
{
	wchar_t canon[FENCE_PATH];

	fence_init ();
	if (path == NULL || resolve_w (path, canon) == 0 || inside_w (canon)) {
		return;
	}
	refuse_w (what, path, canon);
}

static void
fence_a (const char *what, const char *path)
{
	wchar_t wide[FENCE_PATH];

	if (widen (CP_ACP, path, wide, FENCE_PATH)) {
		fence_w (what, wide);
	}
}

/* Each hooked call, with the address it had in kernelbase and in kernel32. An
   import table may hold either, depending on whether the module asked for
   kernel32 by name or for an API set. The real call is kernelbase's where it
   has one: kernel32's is often a stub that goes through kernel32's own import
   table, and a hook called from there would call itself. */
typedef struct {
	const char *name;
	void       *hook;
	void       *real;
	void       *also;
} Hook;

enum {
	H_DELETE_W, H_DELETE_A, H_RMDIR_W, H_RMDIR_A, H_MOVE_W, H_MOVE_A, H_MOVE_EX_W, H_MOVE_EX_A,
	H_MOVE_PROGRESS_W, H_MOVE_PROGRESS_A, H_REPLACE_W, H_LOAD_W, H_LOAD_A, H_LOAD_EX_W, H_LOAD_EX_A,
	N_HOOKS
};

static BOOL WINAPI hook_delete_w        (LPCWSTR path);
static BOOL WINAPI hook_delete_a        (LPCSTR path);
static BOOL WINAPI hook_rmdir_w         (LPCWSTR path);
static BOOL WINAPI hook_rmdir_a         (LPCSTR path);
static BOOL WINAPI hook_move_w          (LPCWSTR from, LPCWSTR to);
static BOOL WINAPI hook_move_a          (LPCSTR from, LPCSTR to);
static BOOL WINAPI hook_move_ex_w       (LPCWSTR from, LPCWSTR to, DWORD flags);
static BOOL WINAPI hook_move_ex_a       (LPCSTR from, LPCSTR to, DWORD flags);
static BOOL WINAPI hook_move_progress_w (LPCWSTR from, LPCWSTR to, LPPROGRESS_ROUTINE progress, LPVOID data, DWORD flags);
static BOOL WINAPI hook_move_progress_a (LPCSTR from, LPCSTR to, LPPROGRESS_ROUTINE progress, LPVOID data, DWORD flags);
static BOOL WINAPI hook_replace_w       (LPCWSTR replaced, LPCWSTR replacement, LPCWSTR backup, DWORD flags,
					 LPVOID exclude, LPVOID reserved);
static HMODULE WINAPI hook_load_w       (LPCWSTR name);
static HMODULE WINAPI hook_load_a       (LPCSTR name);
static HMODULE WINAPI hook_load_ex_w    (LPCWSTR name, HANDLE file, DWORD flags);
static HMODULE WINAPI hook_load_ex_a    (LPCSTR name, HANDLE file, DWORD flags);

static Hook hooks[N_HOOKS] = {
	[H_DELETE_W]        = { "DeleteFileW",           (void *) hook_delete_w,        NULL, NULL },
	[H_DELETE_A]        = { "DeleteFileA",           (void *) hook_delete_a,        NULL, NULL },
	[H_RMDIR_W]         = { "RemoveDirectoryW",      (void *) hook_rmdir_w,         NULL, NULL },
	[H_RMDIR_A]         = { "RemoveDirectoryA",      (void *) hook_rmdir_a,         NULL, NULL },
	[H_MOVE_W]          = { "MoveFileW",             (void *) hook_move_w,          NULL, NULL },
	[H_MOVE_A]          = { "MoveFileA",             (void *) hook_move_a,          NULL, NULL },
	[H_MOVE_EX_W]       = { "MoveFileExW",           (void *) hook_move_ex_w,       NULL, NULL },
	[H_MOVE_EX_A]       = { "MoveFileExA",           (void *) hook_move_ex_a,       NULL, NULL },
	[H_MOVE_PROGRESS_W] = { "MoveFileWithProgressW", (void *) hook_move_progress_w, NULL, NULL },
	[H_MOVE_PROGRESS_A] = { "MoveFileWithProgressA", (void *) hook_move_progress_a, NULL, NULL },
	[H_REPLACE_W]       = { "ReplaceFileW",          (void *) hook_replace_w,       NULL, NULL },
	[H_LOAD_W]          = { "LoadLibraryW",          (void *) hook_load_w,          NULL, NULL },
	[H_LOAD_A]          = { "LoadLibraryA",          (void *) hook_load_a,          NULL, NULL },
	[H_LOAD_EX_W]       = { "LoadLibraryExW",        (void *) hook_load_ex_w,       NULL, NULL },
	[H_LOAD_EX_A]       = { "LoadLibraryExA",        (void *) hook_load_ex_a,       NULL, NULL },
};

static void patch_all (void);

#define REAL(index, type) ((type) hooks[index].real)

typedef BOOL (WINAPI *DeleteW)  (LPCWSTR);
typedef BOOL (WINAPI *DeleteA)  (LPCSTR);
typedef BOOL (WINAPI *MoveW)    (LPCWSTR, LPCWSTR);
typedef BOOL (WINAPI *MoveA)    (LPCSTR, LPCSTR);
typedef BOOL (WINAPI *MoveExW)  (LPCWSTR, LPCWSTR, DWORD);
typedef BOOL (WINAPI *MoveExA)  (LPCSTR, LPCSTR, DWORD);
typedef BOOL (WINAPI *MoveProgW) (LPCWSTR, LPCWSTR, LPPROGRESS_ROUTINE, LPVOID, DWORD);
typedef BOOL (WINAPI *MoveProgA) (LPCSTR, LPCSTR, LPPROGRESS_ROUTINE, LPVOID, DWORD);
typedef BOOL (WINAPI *ReplaceW) (LPCWSTR, LPCWSTR, LPCWSTR, DWORD, LPVOID, LPVOID);
typedef HMODULE (WINAPI *LoadW)   (LPCWSTR);
typedef HMODULE (WINAPI *LoadA)   (LPCSTR);
typedef HMODULE (WINAPI *LoadExW) (LPCWSTR, HANDLE, DWORD);
typedef HMODULE (WINAPI *LoadExA) (LPCSTR, HANDLE, DWORD);

static BOOL WINAPI
hook_delete_w (LPCWSTR path)
{
	fence_w ("delete", path);
	return REAL (H_DELETE_W, DeleteW) (path);
}

static BOOL WINAPI
hook_delete_a (LPCSTR path)
{
	fence_a ("delete", path);
	return REAL (H_DELETE_A, DeleteA) (path);
}

static BOOL WINAPI
hook_rmdir_w (LPCWSTR path)
{
	fence_w ("delete", path);
	return REAL (H_RMDIR_W, DeleteW) (path);
}

static BOOL WINAPI
hook_rmdir_a (LPCSTR path)
{
	fence_a ("delete", path);
	return REAL (H_RMDIR_A, DeleteA) (path);
}

static BOOL WINAPI
hook_move_w (LPCWSTR from, LPCWSTR to)
{
	fence_w ("move", from);
	fence_w ("move onto", to);
	return REAL (H_MOVE_W, MoveW) (from, to);
}

static BOOL WINAPI
hook_move_a (LPCSTR from, LPCSTR to)
{
	fence_a ("move", from);
	fence_a ("move onto", to);
	return REAL (H_MOVE_A, MoveA) (from, to);
}

/* No destination with MOVEFILE_DELAY_UNTIL_REBOOT is a delete at boot. */
static BOOL WINAPI
hook_move_ex_w (LPCWSTR from, LPCWSTR to, DWORD flags)
{
	fence_w ("move", from);
	fence_w ("move onto", to);
	return REAL (H_MOVE_EX_W, MoveExW) (from, to, flags);
}

static BOOL WINAPI
hook_move_ex_a (LPCSTR from, LPCSTR to, DWORD flags)
{
	fence_a ("move", from);
	fence_a ("move onto", to);
	return REAL (H_MOVE_EX_A, MoveExA) (from, to, flags);
}

static BOOL WINAPI
hook_move_progress_w (LPCWSTR from, LPCWSTR to, LPPROGRESS_ROUTINE progress, LPVOID data, DWORD flags)
{
	fence_w ("move", from);
	fence_w ("move onto", to);
	return REAL (H_MOVE_PROGRESS_W, MoveProgW) (from, to, progress, data, flags);
}

static BOOL WINAPI
hook_move_progress_a (LPCSTR from, LPCSTR to, LPPROGRESS_ROUTINE progress, LPVOID data, DWORD flags)
{
	fence_a ("move", from);
	fence_a ("move onto", to);
	return REAL (H_MOVE_PROGRESS_A, MoveProgA) (from, to, progress, data, flags);
}

static BOOL WINAPI
hook_replace_w (LPCWSTR replaced, LPCWSTR replacement, LPCWSTR backup, DWORD flags, LPVOID exclude, LPVOID reserved)
{
	fence_w ("replace", replaced);
	fence_w ("move", replacement);
	fence_w ("move onto", backup);
	return REAL (H_REPLACE_W, ReplaceW) (replaced, replacement, backup, flags, exclude, reserved);
}

/* A module loaded later has an import table of its own to patch. */
static HMODULE WINAPI
hook_load_w (LPCWSTR name)
{
	HMODULE module = REAL (H_LOAD_W, LoadW) (name);

	if (module != NULL) {
		patch_all ();
	}
	return module;
}

static HMODULE WINAPI
hook_load_a (LPCSTR name)
{
	HMODULE module = REAL (H_LOAD_A, LoadA) (name);

	if (module != NULL) {
		patch_all ();
	}
	return module;
}

static HMODULE WINAPI
hook_load_ex_w (LPCWSTR name, HANDLE file, DWORD flags)
{
	HMODULE module = REAL (H_LOAD_EX_W, LoadExW) (name, file, flags);

	if (module != NULL) {
		patch_all ();
	}
	return module;
}

static HMODULE WINAPI
hook_load_ex_a (LPCSTR name, HANDLE file, DWORD flags)
{
	HMODULE module = REAL (H_LOAD_EX_A, LoadExA) (name, file, flags);

	if (module != NULL) {
		patch_all ();
	}
	return module;
}


static void
patch_module (HMODULE module)
{
	BYTE *base = (BYTE *) module;
	IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *) base;
	IMAGE_NT_HEADERS *nt;
	IMAGE_DATA_DIRECTORY imports;
	IMAGE_IMPORT_DESCRIPTOR *import;

	if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
		return;
	}
	nt = (IMAGE_NT_HEADERS *) (base + dos->e_lfanew);
	if (nt->Signature != IMAGE_NT_SIGNATURE) {
		return;
	}
	imports = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
	if (imports.VirtualAddress == 0) {
		return;
	}

	for (import = (IMAGE_IMPORT_DESCRIPTOR *) (base + imports.VirtualAddress); import->Name != 0; import++) {
		IMAGE_THUNK_DATA *thunk;

		for (thunk = (IMAGE_THUNK_DATA *) (base + import->FirstThunk); thunk->u1.Function != 0; thunk++) {
			void **slot = (void **) &thunk->u1.Function;
			size_t i;

			for (i = 0; i < N_HOOKS; i++) {
				DWORD old;

				if (hooks[i].real == NULL || (*slot != hooks[i].real && *slot != hooks[i].also)) {
					continue;
				}
				if (VirtualProtect (slot, sizeof *slot, PAGE_READWRITE, &old)) {
					*slot = hooks[i].hook;
					VirtualProtect (slot, sizeof *slot, old, &old);
				}
				break;
			}
		}
	}
}

/* The modules the real calls live in are left as they are, for the reason
   above Hook. */
static void
patch_all (void)
{
	HMODULE modules[1024];
	HMODULE own[3];
	DWORD needed = 0, i;

	own[0] = GetModuleHandleW (L"kernel32.dll");
	own[1] = GetModuleHandleW (L"kernelbase.dll");
	own[2] = GetModuleHandleW (L"ntdll.dll");

	AcquireSRWLockExclusive (&patch_lock);
	if (K32EnumProcessModules (GetCurrentProcess (), modules, sizeof modules, &needed)) {
		for (i = 0; i < needed / sizeof modules[0] && i < 1024; i++) {
			if (modules[i] != own[0] && modules[i] != own[1] && modules[i] != own[2]) {
				patch_module (modules[i]);
			}
		}
	}
	ReleaseSRWLockExclusive (&patch_lock);
}

static BOOL CALLBACK
init_once (PINIT_ONCE unused_once, PVOID unused_param, PVOID *unused_context)
{
	HMODULE kernel32 = GetModuleHandleW (L"kernel32.dll");
	HMODULE kernelbase = GetModuleHandleW (L"kernelbase.dll");
	wchar_t cell[FENCE_PATH];
	DWORD n;
	size_t i;

	(void) unused_once;
	(void) unused_param;
	(void) unused_context;

	for (i = 0; i < N_HOOKS; i++) {
		hooks[i].real = kernelbase != NULL ? (void *) GetProcAddress (kernelbase, hooks[i].name) : NULL;
		hooks[i].also = kernel32 != NULL ? (void *) GetProcAddress (kernel32, hooks[i].name) : NULL;
		if (hooks[i].real == NULL) {
			hooks[i].real = hooks[i].also;
		}
	}

	n = GetEnvironmentVariableW (L"NEMO_TEST_SCRATCH", cell, FENCE_PATH);
	if (n > 0 && n < FENCE_PATH) {
		wchar_t *part = cell;
		wchar_t *end;

		do {
			end = wcschr (part, L';');
			if (end != NULL) {
				*end = L'\0';
			}
			if (part[0] != L'\0') {
				add_root_w (part);
			}
			part = end + 1;
		} while (end != NULL);
	} else {
		n = GetTempPathW (FENCE_PATH, cell);
		if (n > 0 && n < FENCE_PATH) {
			add_root_w (cell);
		}
	}

	patch_all ();
	return TRUE;
}

static void
fence_init (void)
{
	InitOnceExecuteOnce (&once, init_once, NULL, NULL);
}

__attribute__ ((constructor)) static void
fence_start (void)
{
	fence_init ();
}

void
test_fence_allow (const char *dir)
{
	wchar_t wide[FENCE_PATH];

	fence_init ();
	if (widen (CP_UTF8, dir, wide, FENCE_PATH)) {
		add_root_w (wide);
	}
}

int
test_fence_allows (const char *path)
{
	wchar_t wide[FENCE_PATH], canon[FENCE_PATH];

	fence_init ();
	return widen (CP_UTF8, path, wide, FENCE_PATH) && resolve_w (wide, canon) && inside_w (canon);
}

#endif /* _WIN32 */
