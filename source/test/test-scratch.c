/* Scratch directories for the tests, removed when the test exits.
 *
 * Only a directory this process made through here is removed, and only while
 * it still sits directly under the temp dir (or the base it was made in) as a
 * real directory. The walk never goes through a link, a junction or onto
 * another filesystem. Home has been lost twice on the dev box; a test helper
 * is not going to be the third. */

#include <config.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <gio/gio.h>
#include <pango/pangocairo.h>

#ifndef G_OS_WIN32
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <libnemo-private/nemo-dir-enum.h>

#include "test-scratch.h"

#define WALK_ATTRIBUTES \
	G_FILE_ATTRIBUTE_STANDARD_NAME "," \
	G_FILE_ATTRIBUTE_STANDARD_TYPE "," \
	G_FILE_ATTRIBUTE_STANDARD_IS_SYMLINK "," \
	G_FILE_ATTRIBUTE_UNIX_DEVICE "," \
	G_FILE_ATTRIBUTE_UNIX_UID "," \
	G_FILE_ATTRIBUTE_DOS_REPARSE_POINT_TAG

static GPtrArray *made = NULL;
static GPtrArray *made_in = NULL;
static gboolean registered = FALSE;
#ifndef G_OS_WIN32
static pid_t made_by = 0;
#endif

static gboolean
is_real_dir (GFileInfo *info)
{
	return nemo_dir_enum_file_type (info) == G_FILE_TYPE_DIRECTORY &&
	       !g_file_info_get_attribute_boolean (info, G_FILE_ATTRIBUTE_STANDARD_IS_SYMLINK) &&
	       g_file_info_get_attribute_uint32 (info, G_FILE_ATTRIBUTE_DOS_REPARSE_POINT_TAG) == 0;
}

static guint32
device_of (GFileInfo *info)
{
	return g_file_info_get_attribute_uint32 (info, G_FILE_ATTRIBUTE_UNIX_DEVICE);
}

static void
remove_tree (GFile *dir, guint32 device)
{
	GFileEnumerator *children;
	GFileInfo *info;

	children = nemo_enumerate_children (dir, WALK_ATTRIBUTES,
					    G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, NULL);
	if (children != NULL) {
		while ((info = g_file_enumerator_next_file (children, NULL, NULL)) != NULL) {
			GFile *child = g_file_get_child (dir, g_file_info_get_name (info));

			if (!is_real_dir (info)) {
				g_file_delete (child, NULL, NULL);
			} else if (device_of (info) == device) {
				remove_tree (child, device);
			}
			/* A directory on another device is a mount, and is left. */

			g_object_unref (child);
			g_object_unref (info);
		}
		g_object_unref (children);
	}

	g_file_delete (dir, NULL, NULL);
}

static void
remove_made (const char *path, const char *base)
{
	GFile *file, *tmp;
	GFileInfo *info, *tmp_info;
	GError *error = NULL;
	char *parent, *tmp_path;
	gboolean owned;

	file = g_file_new_for_path (path);
	info = g_file_query_info (file, WALK_ATTRIBUTES,
				  G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, &error);
	if (info == NULL) {
		/* The test cleaned up after itself. */
		if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND)) {
			g_printerr ("scratch: left %s: %s\n", path, error->message);
		}
		g_error_free (error);
		g_object_unref (file);
		return;
	}

	tmp_path = g_canonicalize_filename (base, NULL);
	tmp = g_file_new_for_path (tmp_path);
	tmp_info = g_file_query_info (tmp, WALK_ATTRIBUTES, 0, NULL, NULL);
	parent = g_path_get_dirname (path);

	owned = g_path_is_absolute (path) &&
		strcmp (parent, tmp_path) == 0 &&
		is_real_dir (info) &&
		tmp_info != NULL &&
		device_of (info) == device_of (tmp_info);
#ifndef G_OS_WIN32
	owned = owned && g_file_info_get_attribute_uint32 (info, G_FILE_ATTRIBUTE_UNIX_UID) == getuid ();
#endif

	if (owned) {
		remove_tree (file, device_of (info));
		if (g_file_query_exists (file, NULL)) {
			g_printerr ("scratch: could not remove all of %s\n", path);
		}
	} else {
		g_printerr ("scratch: left %s, it is no longer the directory made there\n", path);
	}

	g_free (parent);
	g_clear_object (&tmp_info);
	g_object_unref (tmp);
	g_free (tmp_path);
	g_object_unref (info);
	g_object_unref (file);
}

/* Newer GTK 3 starts pango's font map in gtk_init, and pango runs FcInit in a
   thread. With no usable system font cache that thread writes one under
   XDG_CACHE_HOME, which is usually a scratch dir, and fontconfig makes the
   dir again if it is gone. So it has to be done before anything is removed.
   A test that never made a font map is not given one here. */
static void
wait_for_font_setup (void)
{
	PangoFontFamily **families = NULL;
	int count = 0;

	if (g_type_from_name ("PangoFcFontMap") == 0) {
		return;
	}
	pango_font_map_list_families (pango_cairo_font_map_get_default (), &families, &count);
	g_free (families);
}

/* TRUE while canon sits inside one of the directories this process made. */
static gboolean
inside_a_made_dir (const char *canon)
{
	guint i;

	for (i = 0; made != NULL && i < made->len; i++) {
		const char *root = g_ptr_array_index (made, i);
		gsize len = strlen (root);

		if (strncmp (canon, root, len) == 0 && G_IS_DIR_SEPARATOR (canon[len])) {
			return TRUE;
		}
	}
	return FALSE;
}

gboolean
test_scratch_remove_tree (const char *path)
{
	GFile *file;
	GFileInfo *info;
	char *canon;
	gboolean ok = FALSE;

	canon = g_canonicalize_filename (path, NULL);
	if (!inside_a_made_dir (canon)) {
		g_printerr ("scratch: refused to remove %s, it is not in a scratch directory\n", path);
		g_free (canon);
		return FALSE;
	}

	wait_for_font_setup ();
	file = g_file_new_for_path (canon);
	info = g_file_query_info (file, WALK_ATTRIBUTES,
				  G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, NULL);
	if (info != NULL && is_real_dir (info)) {
		remove_tree (file, device_of (info));
		ok = !g_file_query_exists (file, NULL);
	}

	g_clear_object (&info);
	g_object_unref (file);
	g_free (canon);
	return ok;
}

void
test_scratch_cleanup (void)
{
	guint i;

	if (made == NULL) {
		return;
	}

#ifndef G_OS_WIN32
	/* A forked child inherits the exit handler, and must not take the parent's
	   directories out from under it. */
	if (getpid () != made_by) {
		return;
	}
#endif

	wait_for_font_setup ();
	for (i = 0; i < made->len; i++) {
		remove_made (g_ptr_array_index (made, i), g_ptr_array_index (made_in, i));
	}

	g_ptr_array_free (made, TRUE);
	g_ptr_array_free (made_in, TRUE);
	made = NULL;
	made_in = NULL;
}

static char *
make_in (const char *base, const char *tmpl, GError **error)
{
	char *path;

	/* g_mkdtemp_full fills in the template where it stands. */
	path = g_build_filename (base, tmpl, NULL);
	if (g_mkdtemp_full (path, 0700) == NULL) {
		int saved = errno;

		g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (saved),
			     "could not make a directory in %s: %s", base, g_strerror (saved));
		g_free (path);
		return NULL;
	}

	if (made == NULL) {
		made = g_ptr_array_new_with_free_func (g_free);
		made_in = g_ptr_array_new_with_free_func (g_free);
	}
	g_ptr_array_add (made, g_canonicalize_filename (path, NULL));
	g_ptr_array_add (made_in, g_canonicalize_filename (base, NULL));

	if (!registered) {
#ifndef G_OS_WIN32
		made_by = getpid ();
#endif
		atexit (test_scratch_cleanup);
		registered = TRUE;
	}

	return path;
}

/* Returns: (transfer full): free with g_free */
char *
test_scratch_dir (const char *tmpl, GError **error)
{
	return make_in (g_get_tmp_dir (), tmpl, error);
}

/* Returns: (transfer full): free with g_free */
char *
test_scratch_dir_in (const char *base, const char *tmpl, GError **error)
{
	return make_in (base, tmpl, error);
}

void
test_scratch_point_config_at (const char *dir)
{
	g_setenv ("HOME", dir, TRUE);
	g_setenv ("APPDATA", dir, TRUE);	/* the config root on Windows */
	g_setenv ("XDG_CONFIG_HOME", dir, TRUE);

	/* The cache root is picked separately from the config one, so a test that
	 * only pointed the config vars would still write a thumbnail database into
	 * the real cache dir. */
	g_setenv ("LOCALAPPDATA", dir, TRUE);
	g_setenv ("XDG_CACHE_HOME", dir, TRUE);
}

/* Returns: (transfer full): free with g_free */
char *
test_scratch_config_home (const char *tmpl)
{
	char *dir = make_in (g_get_tmp_dir (), tmpl, NULL);

	g_return_val_if_fail (dir != NULL, NULL);

	test_scratch_point_config_at (dir);

	return dir;
}

/* Small seeks and reads through GIO are what the timed tests mostly wait on,
   and they are what an emulated box is slowest at: 50 to 100 times slower on
   an emulated arm64 box, where plain arithmetic is only 3 times slower. So
   they are the yardstick. The dev box takes about 2 ms; the reference sits a
   bit above that so a busy dev box keeps the limits as written. */
#define SLOWNESS_PAIRS             4000
#define SLOWNESS_REFERENCE_SECONDS 0.003

static int
compare_doubles (const void *a, const void *b)
{
	double x = *(const double *) a;
	double y = *(const double *) b;

	return (x > y) - (x < y);
}

double
test_slowness (void)
{
	static double slowness = 0;
	g_autofree char *dir = NULL;
	g_autofree char *path = NULL;
	g_autofree guint8 *bytes = NULL;
	g_autoptr (GFile) file = NULL;
	g_autoptr (GFileInputStream) in = NULL;
	double runs[3];
	guint32 next = 1;
	guint i, j;

	if (slowness > 0) {
		return slowness;
	}
	slowness = 1;

	dir = test_scratch_dir ("nemo-slowness-XXXXXX", NULL);
	if (dir == NULL) {
		return slowness;
	}
	path = g_build_filename (dir, "yardstick", NULL);
	bytes = g_malloc0 (65536);
	file = g_file_new_for_path (path);
	if (!g_file_set_contents (path, (const char *) bytes, 65536, NULL) ||
	    (in = g_file_read (file, NULL, NULL)) == NULL) {
		return slowness;
	}

	/* The median, so one hiccup on a fast box does not loosen every limit. */
	for (i = 0; i < G_N_ELEMENTS (runs); i++) {
		gint64 start = g_get_monotonic_time ();

		for (j = 0; j < SLOWNESS_PAIRS; j++) {
			next = next * 1103515245 + 12345;
			g_seekable_seek (G_SEEKABLE (in), (next >> 8) % (65536 - 64), G_SEEK_SET, NULL, NULL);
			g_input_stream_read (G_INPUT_STREAM (in), bytes, 64, NULL, NULL);
		}
		runs[i] = (double) (g_get_monotonic_time () - start) / G_USEC_PER_SEC;
	}
	qsort (runs, G_N_ELEMENTS (runs), sizeof runs[0], compare_doubles);

	slowness = MAX (1.0, runs[1] / SLOWNESS_REFERENCE_SECONDS);
	if (slowness >= 2) {
		g_printerr ("  time limits scaled %.0f times for a slow box\n", slowness);
	}

	return slowness;
}

#ifndef G_OS_WIN32
static volatile pid_t kept_child = 0;

/* A test timed out is stopped through its keeper, so the test has to hear
   of it too, or it and the server are left running. */
static void
pass_on (int sig)
{
	if (kept_child > 0) {
		kill (kept_child, sig);
	}
}

/* No xvfb-run, which is Debian's and not on the BSDs. Xvfb picks a free number
   itself through -displayfd, and this process stays behind as its keeper while
   a forked copy runs the test. Returns in that copy, or here on any failure. */
static void
own_display_by_hand (const char *screen)
{
	char *xvfb = g_find_program_in_path ("Xvfb");
	char number[32];
	size_t got = 0;
	ssize_t n;
	int fds[2], status;
	pid_t server, child, waited;

	if (xvfb == NULL || pipe (fds) != 0) {
		g_free (xvfb);
		return;
	}

	server = fork ();
	if (server == 0) {
		char fd_text[16];
		int null_fd = open ("/dev/null", O_WRONLY);

		close (fds[0]);
		if (null_fd >= 0) {
			dup2 (null_fd, STDOUT_FILENO);
			dup2 (null_fd, STDERR_FILENO);
		}
		snprintf (fd_text, sizeof fd_text, "%d", fds[1]);
		execl (xvfb, "Xvfb", "-displayfd", fd_text, "-nolisten", "tcp",
		       "-screen", "0", screen != NULL ? screen : "1280x1024x24", (char *) NULL);
		_exit (127);
	}
	g_free (xvfb);
	close (fds[1]);
	if (server < 0) {
		close (fds[0]);
		return;
	}

	/* The number and a newline, written once the server is listening. */
	while (got < sizeof number - 1 &&
	       ((n = read (fds[0], number + got, sizeof number - 1 - got)) > 0 ||
	        (n < 0 && errno == EINTR))) {
		if (n > 0) {
			got += (size_t) n;
			if (memchr (number, '\n', got) != NULL) {
				break;
			}
		}
	}
	close (fds[0]);
	number[got] = '\0';
	if (got == 0 || number[0] < '0' || number[0] > '9') {
		kill (server, SIGTERM);
		waitpid (server, NULL, 0);
		return;
	}
	number[strcspn (number, "\n")] = '\0';

	child = fork ();
	if (child < 0) {
		kill (server, SIGTERM);
		waitpid (server, NULL, 0);
		return;
	}
	if (child == 0) {
		char *display = g_strconcat (":", number, NULL);

		g_setenv ("DISPLAY", display, TRUE);
		g_free (display);
		return;
	}

	kept_child = child;
	signal (SIGTERM, pass_on);
	signal (SIGINT, pass_on);
	signal (SIGHUP, pass_on);
	while ((waited = waitpid (child, &status, 0)) < 0 && errno == EINTR) {
	}
	kill (server, SIGTERM);
	waitpid (server, NULL, 0);
	if (waited < 0) {
		_exit (1);
	}

	/* Die the way the test did, so a crash still reads as one. _exit, since
	   the scratch dirs and the rest are the child's to clean up. */
	if (WIFSIGNALED (status)) {
		signal (WTERMSIG (status), SIG_DFL);
		raise (WTERMSIG (status));
	}
	_exit (WIFEXITED (status) ? WEXITSTATUS (status) : 1);
}
#endif

void
test_own_display (int argc, char **argv, const char *screen)
{
#ifndef G_OS_WIN32
	char *xvfb_run;
	char **relaunch;
	int i, n = 0;

	if (g_getenv ("NEMO_TEST_OWN_DISPLAY") != NULL) {
		return;
	}
	xvfb_run = g_find_program_in_path ("xvfb-run");
	if (xvfb_run == NULL) {
		g_setenv ("NEMO_TEST_OWN_DISPLAY", "1", TRUE);
		own_display_by_hand (screen);
		return;
	}
	g_setenv ("NEMO_TEST_OWN_DISPLAY", "1", TRUE);

	relaunch = g_new0 (char *, argc + 5);
	relaunch[n++] = xvfb_run;
	relaunch[n++] = (char *) "-a";
	if (screen != NULL) {
		relaunch[n++] = (char *) "-s";
		relaunch[n++] = g_strdup_printf ("-screen 0 %s", screen);
	}
	for (i = 0; i < argc; i++) {
		relaunch[n++] = argv[i];
	}

	execv (xvfb_run, relaunch);

	/* Only here if the exec failed; the shared display will have to do. */
	if (screen != NULL) {
		g_free (relaunch[3]);
	}
	g_free (relaunch);
	g_free (xvfb_run);
#else
	(void) argc;
	(void) argv;
	(void) screen;
#endif
}
