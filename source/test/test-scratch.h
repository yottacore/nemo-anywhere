#ifndef TEST_SCRATCH_H
#define TEST_SCRATCH_H

#include <glib.h>

G_BEGIN_DECLS

/* Same contract as g_dir_make_tmp. The directory is removed when the test
   exits, however it exits short of a crash or a kill. A current directory
   inside one is left, and a file cache inside one is closed, first. */
char *test_scratch_dir     (const char  *tmpl,
			    GError     **error);

/* The same, made in base instead of the temp dir, for a test that needs a
   second file system. Removed only while it still sits directly in base. */
char *test_scratch_dir_in  (const char  *base,
			    const char  *tmpl,
			    GError     **error);

/* Removes a tree early, for a test that has to reuse the path. Same walk as
   the exit cleanup: never through a link, never onto another file system. The
   path has to sit inside a directory this process made through here, or
   nothing is removed and it returns FALSE. */
gboolean test_scratch_remove_tree (const char *path);

/* Points HOME, APPDATA, LOCALAPPDATA, and the XDG config, cache, data, and
   state vars at dir, so a test that reads a preference or writes a cache, the
   trash, or the recent list can't touch the real one. */
void  test_scratch_point_config_at (const char *dir);

/* Makes a scratch directory and points the config root at it. Call it before
   anything that would cache the real config root - GLib caches its XDG path
   on first use, and so do we. */
char *test_scratch_config_home    (const char *tmpl);

/* Point a started program's temp dir at a tmp folder made in root, which has
   to be a scratch directory. A program the test kills can't clean up, and a
   thumbnail helper's output file is then left behind, which fails the suite.
   Inside root, the exit cleanup takes it. The first is for a program given its
   own environment, the second for one that inherits this process's. This
   process's own scratch dirs stay where they were either way. */
char **test_scratch_tmp_environ  (char **envp, const char *root);
void   test_scratch_point_tmp_at (const char *root);

/* The suite shares one display, so a test whose result depends on what else
   is on screen or what has the pointer grabbed runs itself again under an X server
   of its own. screen is xvfb-run's -screen value, or NULL for its default.
   Returns only when there is nothing to do or the relaunch failed. With no
   xvfb-run it starts Xvfb itself and returns in a forked copy that has it. */
void  test_own_display (int argc, char **argv, const char *screen);

/* How many times slower this box is than the one the suite's time limits
   were set on, measured once per process and never below 1. A limit
   multiplied by it still fails a real slowdown on a normal box, and stops
   failing on an emulated one that is slow at everything. Measuring makes a
   scratch directory, so a copy of the test that may be killed takes its
   parent's reading from NEMO_TEST_SLOWNESS instead, or the directory stays. */
double test_slowness (void);

/* A bar set to tell the old 200 ms view timer from a fixed build can't be
   scaled, and a sanitizer build on a loaded box goes past it with the fix in
   (2026102411433800). Such tests still run there for the memory checks and
   leave their time bars to the plain suite. */
#if defined (__SANITIZE_ADDRESS__)
#define TEST_TIME_BARS_HOLD FALSE
#else
#define TEST_TIME_BARS_HOLD TRUE
#endif

/* Whether the gdk-pixbuf in use has a loader named name ("bmp", "xpm"),
   and whether that loader can save. */
gboolean test_pixbuf_reads  (const char *name);
gboolean test_pixbuf_writes (const char *name);

/* Sets a file or folder's modified time, in seconds since 1970. Use it
   rather than g_utime, which on Windows sets the time an hour off when
   daylight saving differs between then and now. */
gboolean test_set_mtime (const char *path, gint64 secs);

/* Removes every directory made so far. Runs on its own at exit. */
void  test_scratch_cleanup (void);

G_END_DECLS

#endif /* TEST_SCRATCH_H */
