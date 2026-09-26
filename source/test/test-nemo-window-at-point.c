/* A tab dropped outside its own window goes to the window of ours under the
 * pointer, if that one is on top there. Popups, the drag icon among them, are
 * looked through; anything else on top means no window of ours. Checked with
 * the window manager's stacking list and without one. X11 only.
 */

#include <config.h>

#include <stdlib.h>
#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>

#include "nemo-window-at-point.h"
#include "test-check.h"
#include "test-scratch.h"

static GtkWidget *
window_at (GtkWindowType type, int x, int y, int width, int height)
{
	GtkWidget *window = gtk_window_new (type);

	gtk_window_set_decorated (GTK_WINDOW (window), FALSE);
	gtk_window_set_default_size (GTK_WINDOW (window), width, height);
	gtk_window_move (GTK_WINDOW (window), x, y);
	gtk_widget_show (window);

	return window;
}

static void
settle (Display *xdisplay)
{
	int i;

	for (i = 0; i < 50; i++) {
		XSync (xdisplay, False);
		while (gtk_events_pending ()) {
			gtk_main_iteration ();
		}
		g_usleep (10 * 1000);
	}
}

static int
at (Display *xdisplay, int x, int y, const guint64 *candidates, int n)
{
	XWarpPointer (xdisplay, None, DefaultRootWindow (xdisplay), 0, 0, 0, 0, x, y);
	XSync (xdisplay, False);

	return nemo_window_at_pointer (gdk_display_get_default (), candidates, n);
}

static guint64
xid (GtkWidget *window)
{
	return (guint64) gdk_x11_window_get_xid (gtk_widget_get_window (window));
}

static void
expect (Display *xdisplay, int x, int y, const guint64 *candidates, int n, int want, const char *what)
{
	int got = at (xdisplay, x, y, candidates, n);

	if (got != want) {
		g_printerr ("FAIL %s: at %d,%d got %d, wanted %d\n", what, x, y, got, want);
		failures++;
	}
}

int
main (int argc, char **argv)
{
	GtkWidget *low, *high, *hidden, *foreign, *popup;
	Display *xdisplay;
	Atom stacking;
	guint64 candidates[3];
	Window order[3];

	/* Another test's window on top of these would be a right answer to the
	   wrong question. */
	test_own_display (argc, argv, "640x480x24");

	if (!gtk_init_check (&argc, &argv) || !GDK_IS_X11_DISPLAY (gdk_display_get_default ())) {
		g_print ("SKIP: no X11 display\n");
		return 77;
	}
	xdisplay = GDK_DISPLAY_XDISPLAY (gdk_display_get_default ());

	/* A window manager would get in the way of placing these, and the other
	   half of this test pretends to be one. */
	if (XInternAtom (xdisplay, "_NET_SUPPORTING_WM_CHECK", True) != None) {
		Atom type;
		int format;
		unsigned long n, after;
		unsigned char *data = NULL;

		XGetWindowProperty (xdisplay, DefaultRootWindow (xdisplay),
		                    XInternAtom (xdisplay, "_NET_SUPPORTING_WM_CHECK", True),
		                    0, 1, False, XA_WINDOW, &type, &format, &n, &after, &data);
		if (data != NULL) {
			XFree (data);
			g_print ("SKIP: a window manager is running\n");
			return 77;
		}
	}

	/* Mapped in this order, so each is above the one before. */
	low = window_at (GTK_WINDOW_TOPLEVEL, 0, 0, 200, 200);
	high = window_at (GTK_WINDOW_TOPLEVEL, 100, 100, 200, 200);
	hidden = window_at (GTK_WINDOW_TOPLEVEL, 400, 0, 100, 100);
	foreign = window_at (GTK_WINDOW_TOPLEVEL, 20, 20, 50, 50);
	popup = window_at (GTK_WINDOW_POPUP, 140, 140, 120, 120);
	settle (xdisplay);
	gtk_widget_hide (hidden);
	settle (xdisplay);

	candidates[0] = xid (low);
	candidates[1] = xid (high);
	candidates[2] = xid (hidden);

	check (nemo_window_native_handle (GTK_WINDOW (low)) == candidates[0]);
	check (candidates[0] != 0 && candidates[1] != 0 && candidates[0] != candidates[1]);

	expect (xdisplay, 50, 150, candidates, 3, 0, "only the low window");
	expect (xdisplay, 150, 120, candidates, 3, 1, "both, the high one on top");
	expect (xdisplay, 150, 150, candidates, 3, 1, "under a popup");
	expect (xdisplay, 250, 250, candidates, 3, 1, "only the high window, under a popup");
	expect (xdisplay, 30, 30, candidates, 3, -1, "another window on top");
	expect (xdisplay, 450, 50, candidates, 3, -1, "a hidden window");
	expect (xdisplay, 600, 450, candidates, 3, -1, "nothing there");
	expect (xdisplay, 50, 150, candidates, 0, -1, "no candidates");

	gdk_window_raise (gtk_widget_get_window (low));
	settle (xdisplay);
	expect (xdisplay, 150, 120, candidates, 3, 0, "the low window raised");

	/* A window manager's list, bottom first, is believed over the server's
	   own order, which it cannot see past frames and overlays in. */
	stacking = XInternAtom (xdisplay, "_NET_CLIENT_LIST_STACKING", False);
	order[0] = (Window) candidates[0];
	order[1] = (Window) candidates[1];
	order[2] = (Window) xid (foreign);
	XChangeProperty (xdisplay, DefaultRootWindow (xdisplay), stacking, XA_WINDOW, 32,
	                 PropModeReplace, (unsigned char *) order, 3);
	XSync (xdisplay, False);

	expect (xdisplay, 150, 120, candidates, 3, 1, "listed above");
	expect (xdisplay, 50, 150, candidates, 3, 0, "listed, alone there");
	expect (xdisplay, 30, 30, candidates, 3, -1, "listed, another on top");
	expect (xdisplay, 600, 450, candidates, 3, -1, "listed, nothing there");

	XDeleteProperty (xdisplay, DefaultRootWindow (xdisplay), stacking);
	XSync (xdisplay, False);

	gtk_widget_destroy (popup);
	gtk_widget_destroy (foreign);
	gtk_widget_destroy (hidden);
	gtk_widget_destroy (high);
	gtk_widget_destroy (low);

	if (failures == 0) {
		g_print ("nemo-window-at-point: all checks passed\n");
	}

	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
