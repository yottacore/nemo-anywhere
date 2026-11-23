/* How far a keyboard zoom takes the window. Pure arithmetic, so it's all
 * checkable without a screen: the contents area scales by the zoom, the rest
 * of the window doesn't, a screen edge stops it, and zooming back comes back
 * to where it started. */

#include <config.h>

#include <math.h>
#include <stdlib.h>
#include <glib.h>

#include <src/nemo-zoom-resize.h>
#include <libnemo-private/nemo-icon-info.h>
#include "test-check.h"

#define NEAR(a, b) (fabs ((a) - (b)) < 1e-9)

/* The window used throughout: 1000 by 800, of which the contents take 700 by
   600. The other 300 by 200 is side panes, toolbar and status bar. */
#define W 1000
#define H 800
#define CW 700
#define CH 600

static void
check_ratios (void)
{
	double x, y;

	nemo_zoom_resize_ratios (NEMO_ZOOM_RESIZE_ICON, 64, 96, &x, &y);
	check (NEAR (x, 1.5) && NEAR (y, 1.5));

	nemo_zoom_resize_ratios (NEMO_ZOOM_RESIZE_ICON, 96, 64, &x, &y);
	check (NEAR (x, 64.0 / 96) && NEAR (y, 64.0 / 96));

	/* The names beside a compact view's icons don't grow. */
	nemo_zoom_resize_ratios (NEMO_ZOOM_RESIZE_COMPACT, 32, 48, &x, &y);
	check (NEAR (x, 1.0) && NEAR (y, 1.5));

	/* A list follows its text, a fifth bigger a step. */
	nemo_zoom_resize_ratios (NEMO_ZOOM_RESIZE_LIST,
				 NEMO_ICON_SIZE_SMALL, NEMO_ICON_SIZE_STANDARD, &x, &y);
	check (NEAR (x, 1.2) && NEAR (y, 1.2));
	nemo_zoom_resize_ratios (NEMO_ZOOM_RESIZE_LIST,
				 NEMO_ICON_SIZE_STANDARD, NEMO_ICON_SIZE_SMALL, &x, &y);
	check (NEAR (x, 1 / 1.2) && NEAR (y, 1 / 1.2));

	/* Nothing to scale by. */
	nemo_zoom_resize_ratios (NEMO_ZOOM_RESIZE_ICON, 64, 64, &x, &y);
	check (NEAR (x, 1.0) && NEAR (y, 1.0));
	nemo_zoom_resize_ratios (NEMO_ZOOM_RESIZE_ICON, 0, 64, &x, &y);
	check (NEAR (x, 1.0) && NEAR (y, 1.0));
}

/* Only the contents area scales. */
static void
check_contents_scale (void)
{
	NemoZoomResize state = { 0 };
	int w, h;

	check (nemo_zoom_resize_next (&state, W, H, CW, CH, 1.5, 1.5, 0, 0, &w, &h));
	check (w == W + CW / 2);
	check (h == H + CH / 2);
}

/* In and back out, as the window manager gives what was asked. */
static void
check_round_trip (void)
{
	NemoZoomResize state = { 0 };
	int w, h, w2, h2;

	nemo_zoom_resize_next (&state, W, H, CW, CH, 1.5, 1.5, 0, 0, &w, &h);
	check (nemo_zoom_resize_next (&state, w, h, CW + (w - W), CH + (h - H),
				      1 / 1.5, 1 / 1.5, 0, 0, &w2, &h2));
	check (w2 == W && h2 == H);
}

/* A screen edge stops it. Zooming out again shrinks it only as far as the
   size it would have had, so the same number of steps back comes home. */
static void
check_screen_edge (void)
{
	NemoZoomResize state = { 0 };
	int w, h, w2, h2;

	check (nemo_zoom_resize_next (&state, W, H, CW, CH, 1.5, 1.5, 1200, 1000, &w, &h));
	check (w == 1200 && h == 1000);

	/* Another step in has nowhere to go. */
	check (!nemo_zoom_resize_next (&state, w, h, CW + (w - W), CH + (h - H),
				       1.5, 1.5, 1200, 1000, &w2, &h2));
	check (w2 == 1200 && h2 == 1000);

	/* 2 steps out. The first is still past the edge. */
	nemo_zoom_resize_next (&state, w, h, CW + (w - W), CH + (h - H),
			       1 / 1.5, 1 / 1.5, 1200, 1000, &w2, &h2);
	check (w2 == 1200 && h2 == 1000);
	nemo_zoom_resize_next (&state, w2, h2, CW + (w2 - W), CH + (h2 - H),
			       1 / 1.5, 1 / 1.5, 1200, 1000, &w, &h);
	check (w == W && h == H);
}

/* A size nobody asked for, such as a drag by hand, starts over from there. */
static void
check_hand_resize (void)
{
	NemoZoomResize state = { 0 };
	int w, h;

	nemo_zoom_resize_next (&state, W, H, CW, CH, 1.5, 1.5, 1200, 1000, &w, &h);
	check (w == 1200 && h == 1000);

	/* Dragged down to 900 by 700, contents 600 by 500. */
	nemo_zoom_resize_next (&state, 900, 700, 600, 500, 1 / 1.5, 1 / 1.5, 1200, 1000, &w, &h);
	check (w == 300 + 400 && h == 200 + 333);
}

/* 2 zooms before the window manager has caught up with the first. The second
   goes on from the size asked for, not the size still on screen. */
static void
check_quick_zooms (void)
{
	NemoZoomResize state = { 0 };
	int w, h;

	nemo_zoom_resize_next (&state, W, H, CW, CH, 1.5, 1.5, 0, 0, &w, &h);
	check (w == 1350 && h == 1100);
	check (nemo_zoom_resize_next (&state, W, H, CW, CH, 2.0, 2.0, 0, 0, &w, &h));
	check (w == 300 + 2100 && h == 200 + 1800);
}

/* A zoom in never shrinks the window, even one already bigger than the screen,
   and a zoom out never grows it. */
static void
check_direction (void)
{
	NemoZoomResize state = { 0 };
	int w, h;

	check (!nemo_zoom_resize_next (&state, 1500, 1200, 1200, 1000, 1.5, 1.5, 1200, 1000, &w, &h));
	check (w == 1500 && h == 1200);

	/* Only the rows grow, so only the height moves. */
	state = (NemoZoomResize) { 0 };
	check (nemo_zoom_resize_next (&state, W, H, CW, CH, 1.0, 1.5, 0, 0, &w, &h));
	check (w == W && h == H + CH / 2);
}

/* Nothing measured yet, or a measurement that can't be right. */
static void
check_bad_input (void)
{
	NemoZoomResize state = { 0 };
	int w, h;

	check (!nemo_zoom_resize_next (&state, W, H, 0, CH, 1.5, 1.5, 0, 0, &w, &h));
	check (w == W && h == H);
	check (!nemo_zoom_resize_next (&state, W, H, W + 1, CH, 1.5, 1.5, 0, 0, &w, &h));
	check (w == W && h == H);
}

int
main (void)
{
	check_ratios ();
	check_contents_scale ();
	check_round_trip ();
	check_screen_edge ();
	check_hand_resize ();
	check_quick_zooms ();
	check_direction ();
	check_bad_input ();

	if (failures > 0) {
		g_printerr ("%d check(s) failed\n", failures);
		return EXIT_FAILURE;
	}

	g_print ("OK\n");
	return EXIT_SUCCESS;
}
