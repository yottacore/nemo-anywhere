/* The row under the pointer gets a tint that is easy to miss on purpose, but
 * never gray, never as strong as a shaded row, and nowhere near a selected
 * one. Checked against the bundled themes' own colors, light and dark, then
 * once through a real tree view to see the tint is what gets painted and the
 * setting beats it.
 *
 * The color math is repeated here rather than borrowed, so a slip in it shows
 * as a disagreement instead of agreeing with itself. */

#include <config.h>

#include <math.h>
#include <gtk/gtk.h>

#include <libnemo-private/nemo-global-preferences.h>
#include <libnemo-private/nemo-row-hover.h>

#include "test-scratch.h"
#include "test-check.h"

typedef struct {
	const char *name;
	const char *base;
	const char *selected;
	const char *text;
} Theme;

static const Theme themes[] = {
	{ "Fluent",          "#FFFFFF", "#1A73E8", "rgba(0,0,0,0.87)" },
	{ "Fluent-dark",     "#2B2B2B", "#3281EA", "#FFFFFF" },
	{ "Windows-10",      "#FFFFFF", "#CCE8FF", "#000000" },
	{ "Windows-10-dark", "#202020", "#4D4D4D", "#FFFFFF" },
	{ "Windows-XP",      "#FFFFFF", "#1466C9", "#000000" },
	{ "Windows-XP-dark", "#FFFFFF", "#5B83BD", "#000000" },
	{ "macOS",           "#FFFFFF", "#2E7CF7", "#000000" },
	{ "macOS-dark",      "#242226", "#2E7CF7", "#FFFFFF" },
	{ "Adwaita",         "#FFFFFF", "#3584E4", "#000000" },
	{ "Adwaita-dark",    "#2D2D2D", "#15539E", "#FFFFFF" },
};

static double
linear (double c)
{
	return c <= 0.04045 ? c / 12.92 : pow ((c + 0.055) / 1.055, 2.4);
}

static void
oklab (const GdkRGBA *c, double out[3])
{
	double r = linear (c->red), g = linear (c->green), b = linear (c->blue);
	double l = cbrt (0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b);
	double m = cbrt (0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b);
	double s = cbrt (0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b);

	out[0] = 0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s;
	out[1] = 1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s;
	out[2] = 0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s;
}

static double
distance (const GdkRGBA *x, const GdkRGBA *y)
{
	double p[3], q[3];

	oklab (x, p);
	oklab (y, q);
	return sqrt ((p[0] - q[0]) * (p[0] - q[0]) + (p[1] - q[1]) * (p[1] - q[1]) +
		     (p[2] - q[2]) * (p[2] - q[2]));
}

static GdkRGBA
over (const GdkRGBA *base, const GdkRGBA *top)
{
	GdkRGBA mixed;

	mixed.red = base->red + (top->red - base->red) * top->alpha;
	mixed.green = base->green + (top->green - base->green) * top->alpha;
	mixed.blue = base->blue + (top->blue - base->blue) * top->alpha;
	mixed.alpha = 1.0;
	return mixed;
}

static void
test_bundled_themes (void)
{
	guint i;

	for (i = 0; i < G_N_ELEMENTS (themes); i++) {
		GdkRGBA base, selected, text, hover, painted, shaded;
		double hover_step, hue_step, lab[3], base_lab[3];

		gdk_rgba_parse (&base, themes[i].base);
		gdk_rgba_parse (&selected, themes[i].selected);
		gdk_rgba_parse (&text, themes[i].text);

		nemo_row_hover_pick (&base, &selected, &hover);
		painted = over (&base, &hover);

		/* What the list view shades a row with when the theme names no color. */
		text.alpha *= 0.06;
		shaded = over (&base, &text);

		hover_step = distance (&base, &painted);
		oklab (&painted, lab);
		oklab (&base, base_lab);
		hue_step = hypot (lab[1] - base_lab[1], lab[2] - base_lab[2]);

		g_print ("%-16s hover %.3f (hue %.3f)  shaded %.3f  selected %.3f\n",
			 themes[i].name, hover_step, hue_step,
			 distance (&base, &shaded), distance (&base, &selected));

		/* Seen, but only just. */
		check (hover_step >= 0.02);
		check (hover_step < distance (&base, &shaded));
		check (hover_step <= 0.4 * distance (&base, &selected) + 0.001);

		/* A color, not a gray: a good part of the step is hue. Near white
		 * a bit under half is all there is room for. */
		check (hue_step >= 0.35 * hover_step);
		if (base_lab[0] < 0.5) {
			check (hue_step >= 0.9 * hover_step);
		}
	}
}

/* A theme that selects in gray still gets a colored hover, and it is blue. */
static void
test_gray_selection (void)
{
	GdkRGBA base, selected, hover, painted;
	double lab[3];

	gdk_rgba_parse (&base, "#202020");
	gdk_rgba_parse (&selected, "#4D4D4D");
	nemo_row_hover_pick (&base, &selected, &hover);
	painted = over (&base, &hover);
	oklab (&painted, lab);

	check (hover.blue > hover.red);
	check (lab[2] < -0.005);
}

/* A see-through selection counts as what it looks like on the row. */
static void
test_translucent_selection (void)
{
	GdkRGBA base, selected, solid, from_alpha, from_solid;

	gdk_rgba_parse (&base, "#FFFFFF");
	gdk_rgba_parse (&selected, "rgba(26,115,232,0.5)");
	solid = over (&base, &selected);

	nemo_row_hover_pick (&base, &selected, &from_alpha);
	nemo_row_hover_pick (&base, &solid, &from_solid);

	check (fabs (from_alpha.alpha - from_solid.alpha) < 0.001);
}

static gboolean
hover_background (GtkWidget *tree_view, GdkRGBA *color)
{
	GtkStyleContext *context = gtk_widget_get_style_context (tree_view);
	GdkRGBA *got = NULL;

	gtk_style_context_save (context);
	gtk_style_context_set_state (context, GTK_STATE_FLAG_PRELIGHT);
	gtk_style_context_get (context, GTK_STATE_FLAG_PRELIGHT,
			       GTK_STYLE_PROPERTY_BACKGROUND_COLOR, &got, NULL);
	gtk_style_context_restore (context);

	if (got == NULL) {
		return FALSE;
	}
	*color = *got;
	gdk_rgba_free (got);
	return TRUE;
}

static gboolean
same_color (const GdkRGBA *a, const GdkRGBA *b)
{
	return fabs (a->red - b->red) < 0.005 && fabs (a->green - b->green) < 0.005 &&
	       fabs (a->blue - b->blue) < 0.005 && fabs (a->alpha - b->alpha) < 0.005;
}

static void
test_tree_view (void)
{
	GtkCssProvider *theme;
	GtkWidget *window, *tree_view;
	GdkRGBA base, selected, want, got;

	/* A theme that paints its own strong hover, which has to lose. */
	theme = gtk_css_provider_new ();
	gtk_css_provider_load_from_data (theme,
		"treeview.view { background-color: #FFFFFF; }"
		"treeview.view:selected { background-color: #1A73E8; }"
		"treeview.view:hover { background-color: #FF00FF; }", -1, NULL);
	gtk_style_context_add_provider_for_screen (gdk_screen_get_default (),
						   GTK_STYLE_PROVIDER (theme),
						   GTK_STYLE_PROVIDER_PRIORITY_SETTINGS);

	window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	tree_view = gtk_tree_view_new ();
	gtk_container_add (GTK_CONTAINER (window), tree_view);
	nemo_row_hover_attach (tree_view);

	gdk_rgba_parse (&base, "#FFFFFF");
	gdk_rgba_parse (&selected, "#1A73E8");
	nemo_row_hover_pick (&base, &selected, &want);
	check (hover_background (tree_view, &got) && same_color (&got, &want));

	nemo_config_set_string (nemo_list_view_preferences,
				NEMO_PREFERENCES_LIST_VIEW_ROW_HOVER_COLOR, "rgba(255,0,0,0.25)");
	gdk_rgba_parse (&want, "rgba(255,0,0,0.25)");
	check (hover_background (tree_view, &got) && same_color (&got, &want));

	/* Not a color, so it is as if unset. */
	nemo_config_set_string (nemo_list_view_preferences,
				NEMO_PREFERENCES_LIST_VIEW_ROW_HOVER_COLOR, "not a color");
	nemo_row_hover_pick (&base, &selected, &want);
	check (hover_background (tree_view, &got) && same_color (&got, &want));

	gtk_widget_destroy (window);
	gtk_style_context_remove_provider_for_screen (gdk_screen_get_default (),
						      GTK_STYLE_PROVIDER (theme));
	g_object_unref (theme);
}

/* Row shading takes the setting first, then the theme's nemo_row_shading,
   then a 6% wash of the text color. */
static void
test_shading (void)
{
	GtkCssProvider *theme;
	GtkWidget *window, *tree_view;
	GdkRGBA want, got;

	theme = gtk_css_provider_new ();
	gtk_css_provider_load_from_data (theme, "treeview.view { color: #204060; }", -1, NULL);
	gtk_style_context_add_provider_for_screen (gdk_screen_get_default (),
						   GTK_STYLE_PROVIDER (theme),
						   GTK_STYLE_PROVIDER_PRIORITY_SETTINGS);

	window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	tree_view = gtk_tree_view_new ();
	gtk_container_add (GTK_CONTAINER (window), tree_view);

	nemo_row_shading_pick (tree_view, &got);
	gdk_rgba_parse (&want, "rgba(32,64,96,0.06)");
	check (same_color (&got, &want));

	gtk_css_provider_load_from_data (theme,
		"@define-color nemo_row_shading rgba(0,128,0,0.1);"
		"treeview.view { color: #204060; }", -1, NULL);
	nemo_row_shading_pick (tree_view, &got);
	gdk_rgba_parse (&want, "rgba(0,128,0,0.1)");
	check (same_color (&got, &want));

	nemo_config_set_string (nemo_list_view_preferences,
				NEMO_PREFERENCES_LIST_VIEW_ROW_SHADING_COLOR, "rgba(255,0,0,0.2)");
	nemo_row_shading_pick (tree_view, &got);
	gdk_rgba_parse (&want, "rgba(255,0,0,0.2)");
	check (same_color (&got, &want));

	/* Not a color, so it is as if unset. */
	nemo_config_set_string (nemo_list_view_preferences,
				NEMO_PREFERENCES_LIST_VIEW_ROW_SHADING_COLOR, "not a color");
	nemo_row_shading_pick (tree_view, &got);
	gdk_rgba_parse (&want, "rgba(0,128,0,0.1)");
	check (same_color (&got, &want));

	nemo_config_reset (nemo_list_view_preferences, NEMO_PREFERENCES_LIST_VIEW_ROW_SHADING_COLOR);
	gtk_widget_destroy (window);
	gtk_style_context_remove_provider_for_screen (gdk_screen_get_default (),
						      GTK_STYLE_PROVIDER (theme));
	g_object_unref (theme);
}

int
main (int argc, char *argv[])
{
	char *tmp;

	tmp = test_scratch_config_home ("nemo-row-hover-test-XXXXXX");

	test_bundled_themes ();
	test_gray_selection ();
	test_translucent_selection ();

	if (!gtk_init_check (&argc, &argv)) {
		g_print ("SKIP: tree view half, no display\n");
		g_free (tmp);
		return failures ? 1 : 77;
	}

	nemo_global_preferences_init ();
	test_tree_view ();
	test_shading ();

	g_free (tmp);

	if (failures > 0) {
		g_printerr ("%d check(s) failed\n", failures);
		return 1;
	}
	g_print ("PASS\n");
	return 0;
}
