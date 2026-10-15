/* How a path in a tab gets shorter when the row runs out of room, and which tab
 * gives up room first. The root and the folder's own name have to survive every
 * step, or two tabs stop being told apart. */

#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <glib.h>

#include <libnemo-private/nemo-file-utilities.h>

static int failures = 0;

static void
check_forms (const char *path, char separator, const char *home, const char *const *expected)
{
	gchar **got = nemo_path_forms (path, separator, home);
	gchar *got_text = g_strjoinv (" | ", got);
	gchar *want_text = g_strjoinv (" | ", (gchar **) expected);

	if (g_strcmp0 (got_text, want_text) != 0) {
		g_printerr ("FAIL: %s\n  got:    %s\n  wanted: %s\n", path, got_text, want_text);
		failures++;
	}

	g_free (got_text);
	g_free (want_text);
	g_strfreev (got);
}

/* Every form keeps the root and the last name, and each is shorter than the one
   before it. */
static void
check_invariants (const char *path, char separator, const char *home,
		  const char *root, const char *name)
{
	gchar **got = nemo_path_forms (path, separator, home);
	guint i;

	for (i = 0; got[i] != NULL; i++) {
		if (!g_str_has_prefix (got[i], root) || !g_str_has_suffix (got[i], name)) {
			g_printerr ("FAIL: %s lost its root or name in %s\n", path, got[i]);
			failures++;
		}
		if (i > 0 && g_utf8_strlen (got[i], -1) >= g_utf8_strlen (got[i - 1], -1)) {
			g_printerr ("FAIL: %s is not shorter than %s\n", got[i], got[i - 1]);
			failures++;
		}
	}

	g_strfreev (got);
}

static void
check_fit (guint count, const gint *const *widths, const guint *form_counts, guint active,
	   gint min_px, gint max_px, gint avail, const guint *expected, const char *what)
{
	guint chosen[8];
	guint i;

	nemo_path_forms_fit (count, widths, form_counts, active, min_px, max_px, avail, chosen);
	for (i = 0; i < count; i++) {
		if (chosen[i] != expected[i]) {
			g_printerr ("FAIL: %s: tab %u took form %u (wanted %u)\n",
				    what, i, chosen[i], expected[i]);
			failures++;
		}
	}
}

int
main (void)
{
	{
		const char *want[] = {
			"C:\\Users\\somebody\\data\\prs\\dev",
			"C:\\Users\\somebody\\...\\dev",
			"C:\\Users\\...\\dev",
			"C:\\...\\dev",
			NULL };
		check_forms ("C:\\Users\\somebody\\data\\prs\\dev", '\\', NULL, want);
	}

	/* A hidden folder keeps the letter after its dot, and a path under home
	   reads as ~ once it is being shortened. */
	{
		const char *want[] = {
			"/home/somebody/.config/nemo-anywhere/x",
			"~/.config/nemo-anywhere/x",
			"~/.config/.../x",
			"~/.../x",
			NULL };
		check_forms ("/home/somebody/.config/nemo-anywhere/x", '/', "/home/somebody", want);
	}

	/* A folder that only shares home's prefix is not under it. */
	{
		const char *want[] = { "/home/somebodyelse/a", "/home/.../a", "/.../a", NULL };
		check_forms ("/home/somebodyelse/a", '/', "/home/somebody", want);
	}

	{
		const char *want[] = { "/home/somebody", "~", NULL };
		check_forms ("/home/somebody", '/', "/home/somebody/", want);
	}

	/* Two folders cannot be beaten by an ellipsis. */
	{
		const char *want[] = { "C:\\a\\project", NULL };
		check_forms ("C:\\a\\project", '\\', NULL, want);
	}

	{
		const char *want[] = { "C:\\", NULL };
		check_forms ("C:\\", '\\', NULL, want);
	}

	/* A share's root is the server and the share together. */
	{
		const char *want[] = {
			"\\\\box\\share\\team\\docs\\old",
			"\\\\box\\share\\team\\...\\old",
			"\\\\box\\share\\...\\old",
			NULL };
		check_forms ("\\\\box\\share\\team\\docs\\old", '\\', NULL, want);
	}

	/* Initials only turn up where the folder names are shorter than the ellipsis
	   that would replace them. */
	{
		const char *want[] = { "/home/somebody", "/.../somebody", "/h/somebody", NULL };
		check_forms ("/home/somebody", '/', NULL, want);
	}

	/* Windows spelled with forward slashes, which the separator preference allows. */
	check_invariants ("C:/Users/somebody/data/prs/dev/github.com/someone/nemo-anywhere/github",
			  '/', NULL, "C:/", "/github");
	check_invariants ("/mnt/zfs/zf10/0-0/users/somebody/data/prs/dev/github.com/someone/nemo-anywhere",
			  '/', NULL, "/", "/nemo-anywhere");
	check_invariants ("/\xc3\xa9t\xc3\xa9/\xc3\xa0/b/c/d/\xc3\xa9t\xc3\xa9", '/', NULL, "/", "/\xc3\xa9t\xc3\xa9");

	{
		const char *want[] = { NULL };
		check_forms (NULL, '/', NULL, want);
	}

	/* Room for everything: each tab keeps its full path. */
	{
		const gint a[] = { 300, 120, 60 }, b[] = { 200, 100, 50 };
		const gint *const widths[] = { a, b };
		const guint counts[] = { 3, 3 };
		const guint want[] = { 0, 0 };
		check_fit (2, widths, counts, 0, 50, 400, 1000, want, "room for all");
	}

	/* A tab behind the front one is capped whatever room the row has. */
	{
		const gint a[] = { 300, 120, 60 }, b[] = { 500, 100, 50 };
		const gint *const widths[] = { a, b };
		const guint counts[] = { 3, 3 };
		const guint want[] = { 0, 1 };
		check_fit (2, widths, counts, 0, 50, 400, 1000, want, "behind and over the cap");
	}

	/* The tab in front is not capped: its whole path stands while the row has
	   room for it. */
	{
		const gint a[] = { 500, 120, 60 }, b[] = { 200, 100, 50 };
		const gint *const widths[] = { a, b };
		const guint counts[] = { 3, 3 };
		const guint want[] = { 0, 0 };
		check_fit (2, widths, counts, 0, 50, 400, 1000, want, "in front and over the cap");
	}

	/* Crowded: the two behind shorten together, and stop as soon as the one in
	   front can spell its path out. */
	{
		const gint a[] = { 600, 200, 80 }, b[] = { 300, 120, 60 }, c[] = { 350, 130, 70 };
		const gint *const widths[] = { a, b, c };
		const guint counts[] = { 3, 3, 3 };
		const guint want[] = { 0, 1, 1 };
		const guint tighter[] = { 0, 2, 2 };
		check_fit (3, widths, counts, 0, 50, 400, 900, want, "crowded");
		check_fit (3, widths, counts, 0, 50, 400, 800, tighter, "more crowded");
	}

	/* The tabs behind take the same step, so one of them being over the cap
	   shortens the other as well. */
	{
		const gint a[] = { 600, 200, 80 }, c[] = { 300, 130, 70 }, b[] = { 500, 120, 60 };
		const gint *const widths[] = { a, c, b };
		const guint counts[] = { 3, 3, 3 };
		const guint want[] = { 0, 1, 1 };
		check_fit (3, widths, counts, 0, 50, 400, 5000, want, "behind in step");
	}

	/* Tighter still: the tab in front gives way once the others have nothing
	   left to give. */
	{
		const gint a[] = { 600, 200, 80 }, b[] = { 300, 120, 60 };
		const gint *const widths[] = { a, b };
		const guint counts[] = { 3, 3 };
		const guint want[] = { 1, 2 };
		check_fit (2, widths, counts, 0, 50, 400, 300, want, "front gives way last");
	}

	/* The front tab is wherever the user left it, not always the first. */
	{
		const gint a[] = { 300, 120, 60 }, b[] = { 600, 200, 80 };
		const gint *const widths[] = { a, b };
		const guint counts[] = { 3, 3 };
		const guint want[] = { 2, 0 };
		check_fit (2, widths, counts, 1, 50, 400, 700, want, "front is the second tab");
	}

	/* One tab: no cap, and nothing to shorten but itself. */
	{
		const gint a[] = { 300, 120, 60 };
		const gint *const widths[] = { a };
		const guint counts[] = { 3 };
		const guint want[] = { 2 };
		check_fit (1, widths, counts, 0, 50, 400, 100, want, "one tab");
	}

	/* Nothing short enough: every tab ends at its last form and the row scrolls. */
	{
		const gint a[] = { 300, 120, 60 }, b[] = { 40 };
		const gint *const widths[] = { a, b };
		const guint counts[] = { 3, 1 };
		const guint want[] = { 2, 0 };
		check_fit (2, widths, counts, 0, 50, 400, 10, want, "no room at all");
	}

	/* A tab already at the narrowest a tab gets would save nothing by being
	   shortened, so it keeps its whole path. */
	{
		const gint a[] = { 300, 120, 60 }, b[] = { 45, 30 };
		const gint *const widths[] = { a, b };
		const guint counts[] = { 3, 2 };
		const guint want[] = { 2, 0 };
		check_fit (2, widths, counts, 0, 50, 400, 10, want, "already narrow");
	}

	/* What the window title asks: the longest that fits, or the shortest when
	   none of them does. */
	{
		const gint w[] = { 300, 120, 60 };

		if (nemo_path_form_for_width (w, 3, 1000) != 0 ||
		    nemo_path_form_for_width (w, 3, 200) != 1 ||
		    nemo_path_form_for_width (w, 3, 60) != 2 ||
		    nemo_path_form_for_width (w, 3, 10) != 2) {
			g_printerr ("FAIL: a title took the wrong form for its width\n");
			failures++;
		}
	}

	if (failures > 0) {
		g_printerr ("%d failure(s)\n", failures);
		return EXIT_FAILURE;
	}

	g_print ("path-forms: all checks passed\n");
	return EXIT_SUCCESS;
}
