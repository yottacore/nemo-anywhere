#ifndef TEST_SCRATCH_H
#define TEST_SCRATCH_H

#include <glib.h>

G_BEGIN_DECLS

/* Same contract as g_dir_make_tmp. The directory is removed when the test
   exits, however it exits short of a crash. */
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
   nothing is removed and it answers FALSE. */
gboolean test_scratch_remove_tree (const char *path);

/* Points HOME, APPDATA, LOCALAPPDATA and the two XDG vars at dir, so a test
   that reads a preference or writes a cache cannot reach the real one. */
void  test_scratch_point_config_at (const char *dir);

/* Makes a scratch directory and points the config root at it. Call it before
   anything that would cache the real config root - GLib caches its XDG answer
   on first use, and so do we. */
char *test_scratch_config_home    (const char *tmpl);

/* The suite shares one display, so a test whose answer depends on what else
   is on screen or who holds the pointer runs itself again under an X server
   of its own. screen is xvfb-run's -screen value, or NULL for its default.
   Returns only when there is nothing to do or the relaunch failed. With no
   xvfb-run it starts Xvfb itself and returns in a forked copy that has it. */
void  test_own_display (int argc, char **argv, const char *screen);

/* How many times slower this box is than the one the suite's time limits
   were set on, measured once per process and never below 1. A limit
   multiplied by it still fails a real slowdown on a normal box, and stops
   failing on an emulated one that is slow at everything. */
double test_slowness (void);

/* Removes every directory made so far. Runs on its own at exit. */
void  test_scratch_cleanup (void);

G_END_DECLS

#endif /* TEST_SCRATCH_H */
