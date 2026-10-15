/* The divider has to actually move when the paned changes width, not just be
 * asked to. The first version set the position from a size-allocate handler,
 * after GtkPaned had placed its children, and the tree pane never moved. The
 * arithmetic test passed the whole time, so this one reads the width the first
 * child was really given. */

#include <config.h>

#include <stdlib.h>
#include <gtk/gtk.h>

#include <src/nemo-proportional-paned.h>
#include "test-check.h"

#define near(got, want) ((got) >= (want) - 1 && (got) <= (want) + 1)

typedef struct {
	GtkWidget *paned;
	GtkWidget *left;
	GtkWidget *right;
} Rig;

static void
rig_up (Rig *rig, int right_min)
{
	/* No window around it. A window allocates the paned at its own size as
	   soon as it is shown, and that would be where the share starts from. */
	rig->paned = g_object_ref_sink (nemo_proportional_paned_new ());
	rig->left = gtk_label_new ("");
	rig->right = gtk_label_new ("");

	gtk_widget_set_size_request (rig->left, 10, -1);
	gtk_widget_set_size_request (rig->right, right_min, -1);

	gtk_paned_pack1 (GTK_PANED (rig->paned), rig->left, FALSE, FALSE);
	gtk_paned_pack2 (GTK_PANED (rig->paned), rig->right, TRUE, FALSE);
	gtk_widget_show_all (rig->paned);
}

static void
rig_down (Rig *rig)
{
	gtk_widget_destroy (rig->paned);
	/* cppcheck-suppress doubleFree ; the ref_sink in rig_up is ours to drop */
	g_object_unref (rig->paned);
}

/* One pass the way a window resize makes it, plus the pass the paned queues
   for itself, and the width the left side ended up with. */
static int
allocate (Rig *rig, int width)
{
	GtkAllocation allocation = { 0, 0, width, 200 };
	int min, natural;
	int pass;

	for (pass = 0; pass < 2; pass++) {
		gtk_widget_get_preferred_width (rig->paned, &min, &natural);
		gtk_widget_get_preferred_height_for_width (rig->paned, width, &min, &natural);
		gtk_widget_size_allocate (rig->paned, &allocation);
	}

	return gtk_widget_get_allocated_width (rig->left);
}

static void
check_resize_keeps_share (void)
{
	Rig rig;

	rig_up (&rig, 10);
	gtk_paned_set_position (GTK_PANED (rig.paned), 300);

	check (allocate (&rig, 900) == 300);
	check (near (allocate (&rig, 1800), 600));
	check (near (allocate (&rig, 450), 150));
	check (near (allocate (&rig, 900), 300));

	rig_down (&rig);
}

/* GtkPaned's own scaling truncates from the last position on each step, so a
   one-pixel-at-a-time drag never moves the divider. */
static void
check_slow_drag_adds_up (void)
{
	Rig rig;
	int width;

	rig_up (&rig, 10);
	gtk_paned_set_position (GTK_PANED (rig.paned), 400);
	allocate (&rig, 1000);

	for (width = 1001; width <= 1100; width++) {
		allocate (&rig, width);
	}

	check (near (gtk_widget_get_allocated_width (rig.left), 440));

	rig_down (&rig);
}

/* A move at the same width is the user's, and later resizes scale from it. */
static void
check_drag_is_remembered (void)
{
	Rig rig;

	rig_up (&rig, 10);
	gtk_paned_set_position (GTK_PANED (rig.paned), 200);
	allocate (&rig, 1000);

	gtk_paned_set_position (GTK_PANED (rig.paned), 500);
	check (allocate (&rig, 1000) == 500);
	check (near (allocate (&rig, 2000), 1000));

	rig_down (&rig);
}

/* Held back by the right side's minimum on a narrow window, the old share
   comes back once there is room again. */
static void
check_clamp_is_not_kept (void)
{
	Rig rig;

	rig_up (&rig, 300);
	gtk_paned_set_position (GTK_PANED (rig.paned), 500);
	allocate (&rig, 1000);

	check (allocate (&rig, 500) < 250);
	check (near (allocate (&rig, 1000), 500));

	rig_down (&rig);
}

static void
center_once (GtkWidget *paned, GParamSpec *pspec, gpointer user_data)
{
	(void) pspec;
	(void) user_data;

	g_signal_handlers_disconnect_by_func (paned, center_once, NULL);
	gtk_paned_set_position (GTK_PANED (paned), gtk_widget_get_allocated_width (paned) / 2);
}

/* The split view centers its divider from notify::position, which fires in the
   middle of the first allocation. That is where the share starts from. */
static void
check_position_moved_during_allocation (void)
{
	Rig rig;

	rig_up (&rig, 10);
	g_signal_connect_after (rig.paned, "notify::position", G_CALLBACK (center_once), NULL);

	allocate (&rig, 1000);
	check (near (allocate (&rig, 1000), 500));
	check (near (allocate (&rig, 1600), 800));

	rig_down (&rig);
}

static void
count_placed (GtkWidget *paned, int position, gpointer user_data)
{
	int *placed = user_data;

	(void) paned;
	placed[0]++;
	placed[1] = position;
}

/* The window saves the tree width from "placed". It used to save every
   allocation, so a narrow window overwrote the width that had been dragged. */
static void
check_only_a_drag_is_placed (void)
{
	Rig rig;
	int placed[2] = { 0, 0 };

	rig_up (&rig, 10);
	g_signal_connect (rig.paned, "placed", G_CALLBACK (count_placed), placed);
	gtk_paned_set_position (GTK_PANED (rig.paned), 300);

	allocate (&rig, 900);
	check (placed[0] == 0);

	allocate (&rig, 450);
	allocate (&rig, 1800);
	check (placed[0] == 0);

	gtk_paned_set_position (GTK_PANED (rig.paned), 700);
	allocate (&rig, 1800);
	check (placed[0] == 1);
	check (placed[1] == 700);

	allocate (&rig, 900);
	check (placed[0] == 1);

	rig_down (&rig);
}

int
main (int argc, char *argv[])
{
	if (!gtk_init_check (&argc, &argv)) {
		g_print ("no display; skipping\n");
		return 77;
	}

	check_resize_keeps_share ();
	check_slow_drag_adds_up ();
	check_drag_is_remembered ();
	check_clamp_is_not_kept ();
	check_position_moved_during_allocation ();
	check_only_a_drag_is_placed ();

	if (failures > 0) {
		g_printerr ("%d check(s) failed\n", failures);
		return 1;
	}

	return 0;
}
