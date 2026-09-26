/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-row-hover.c - the tint on a list row under the pointer.

   Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu).

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; version 2 of the
   License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public
   License along with this program; if not, write to the
   Free Software Foundation, Inc., 51 Franklin Street, Suite 500,
   Boston, MA 02110-1335, USA.
*/

/* Hover used to be a faint gray, which read the same as a shaded row and, on
 * themes that paint their own hover strongly, like half a selection. Now it
 * takes its hue from the theme's selection, the way Explorer does, and is made
 * just strong enough to see: fainter than a shaded row and well short of a
 * selected one, whatever the theme and whether it is light or dark.
 *
 * Strength is measured in OKLab, where equal steps look about equally large on
 * light and dark rows alike. As much of the step as will fit goes to hue, and
 * only the rest to lightness, since lightness alone is what a shaded row is.
 * On a dark row all of it fits. Near white very little color exists, so on a
 * white row a bit under half is hue and the tint is also a little darker. */

#include <config.h>

#include "nemo-row-hover.h"
#include "nemo-global-preferences.h"

#include <math.h>
#include <string.h>

/* A step of 0.02 is about the least most people can see. A shaded row sits
 * 0.04 to 0.055 off base on the bundled themes. */
#define HOVER_STEP      0.03
/* Never more than this share of the selection's step, for themes whose
 * selection is itself pale, such as Explorer's light blue. */
#define SELECTION_SHARE 0.4
/* Below this the selection is a gray, and a gray hover is what this replaces. */
#define GRAY_CHROMA     0.04

typedef struct {
	double l, a, b;
} Lab;

static double
to_linear (double c)
{
	return c <= 0.04045 ? c / 12.92 : pow ((c + 0.055) / 1.055, 2.4);
}

static double
from_linear (double c)
{
	return c <= 0.0031308 ? c * 12.92 : 1.055 * pow (c, 1 / 2.4) - 0.055;
}

static Lab
to_oklab (const GdkRGBA *color)
{
	double r = to_linear (color->red), g = to_linear (color->green), b = to_linear (color->blue);
	double l, m, s;
	Lab lab;

	l = cbrt (0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b);
	m = cbrt (0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b);
	s = cbrt (0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b);

	lab.l = 0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s;
	lab.a = 1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s;
	lab.b = 0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s;
	return lab;
}

/* FALSE when lab is outside what a screen can show. */
static gboolean
from_oklab (Lab lab, GdkRGBA *color)
{
	double l = pow (lab.l + 0.3963377774 * lab.a + 0.2158037573 * lab.b, 3);
	double m = pow (lab.l - 0.1055613458 * lab.a - 0.0638541728 * lab.b, 3);
	double s = pow (lab.l - 0.0894841775 * lab.a - 1.2914855480 * lab.b, 3);
	double r = 4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s;
	double g = -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s;
	double b = -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s;
	const double slack = 1e-6;

	color->red = from_linear (CLAMP (r, 0.0, 1.0));
	color->green = from_linear (CLAMP (g, 0.0, 1.0));
	color->blue = from_linear (CLAMP (b, 0.0, 1.0));
	color->alpha = 1.0;

	return r > -slack && g > -slack && b > -slack &&
	       r < 1 + slack && g < 1 + slack && b < 1 + slack;
}

static double
lab_distance (Lab x, Lab y)
{
	return sqrt ((x.l - y.l) * (x.l - y.l) + (x.a - y.a) * (x.a - y.a) +
		     (x.b - y.b) * (x.b - y.b));
}

/* The least opaque color that paints target when laid over base. The less
 * opaque it is, the more it acts as a plain shift, so it still looks right on
 * rows a shade off base, like a sidebar's. */
static void
thinnest_over (const GdkRGBA *base, const GdkRGBA *target, GdkRGBA *out)
{
	const double from[3] = { base->red, base->green, base->blue };
	const double to[3] = { target->red, target->green, target->blue };
	double channel[3], alpha = 0.0;
	int i;

	for (i = 0; i < 3; i++) {
		double move = to[i] - from[i];

		if (move > 0) {
			alpha = MAX (alpha, move / MAX (1.0 - from[i], 1e-9));
		} else if (move < 0) {
			alpha = MAX (alpha, -move / MAX (from[i], 1e-9));
		}
	}
	alpha = MIN (alpha, 1.0);

	for (i = 0; i < 3; i++) {
		channel[i] = alpha > 0 ? CLAMP (from[i] + (to[i] - from[i]) / alpha, 0.0, 1.0) : from[i];
	}

	out->red = channel[0];
	out->green = channel[1];
	out->blue = channel[2];
	out->alpha = alpha;
}

void
nemo_row_hover_pick (const GdkRGBA *base, const GdkRGBA *selected, GdkRGBA *hover)
{
	/* Windows' own default accent, for themes that select in gray. */
	static const GdkRGBA fallback = { 0.0, 0x78 / 255.0, 0xd7 / 255.0, 1.0 };
	GdkRGBA solid, target;
	Lab base_lab, hue, lab;
	double step, chroma, toward;
	int share;

	solid.red = base->red + (selected->red - base->red) * selected->alpha;
	solid.green = base->green + (selected->green - base->green) * selected->alpha;
	solid.blue = base->blue + (selected->blue - base->blue) * selected->alpha;
	solid.alpha = 1.0;

	base_lab = to_oklab (base);
	hue = to_oklab (&solid);
	step = MIN (HOVER_STEP, SELECTION_SHARE * lab_distance (base_lab, hue));

	if (hypot (hue.a, hue.b) < GRAY_CHROMA) {
		hue = to_oklab (&fallback);
	}
	chroma = hypot (hue.a, hue.b);

	/* Darker on a light row, lighter on a dark one. */
	toward = base_lab.l > 0.5 ? -1.0 : 1.0;

	/* Hue first, giving up a percent at a time to lightness until it fits. */
	for (share = 100; share >= 0; share--) {
		double part = share / 100.0;

		lab.l = base_lab.l + toward * step * sqrt (1 - part * part);
		lab.a = base_lab.a + step * part * hue.a / chroma;
		lab.b = base_lab.b + step * part * hue.b / chroma;

		if (from_oklab (lab, &target)) {
			break;
		}
	}

	thinnest_over (base, &target, hover);
}

/* A widget's own background color, as the theme paints it in one state.
 * FALSE when the theme leaves it see-through or draws an image instead. */
static gboolean
state_background (GtkStyleContext *context, GtkStateFlags state, GdkRGBA *color)
{
	GdkRGBA *got = NULL;

	gtk_style_context_save (context);
	gtk_style_context_set_state (context, state);
	gtk_style_context_get (context, state, GTK_STYLE_PROPERTY_BACKGROUND_COLOR, &got, NULL);
	gtk_style_context_restore (context);

	if (got == NULL) {
		return FALSE;
	}

	*color = *got;
	gdk_rgba_free (got);
	return color->alpha >= 0.5;
}

static gboolean
lookup_either (GtkStyleContext *context, const char *name, GdkRGBA *color)
{
	char *plain;
	gboolean found;

	if (gtk_style_context_lookup_color (context, name, color)) {
		return TRUE;
	}

	/* Some themes name their colors without the theme_ prefix. */
	plain = g_str_has_prefix (name, "theme_") ? g_strdup (name + strlen ("theme_")) : NULL;
	found = plain != NULL && gtk_style_context_lookup_color (context, plain, color);
	g_free (plain);
	return found;
}

/* The setting wins, then a nemo_row_hover color from the theme or the user's
 * gtk.css, then one worked out from the theme's rows. */
static void
row_hover_update (GtkWidget *tree_view)
{
	GtkStyleContext *context;
	GtkCssProvider *provider;
	GdkRGBA base, selected, hover;
	char *setting, *color_text, *css;

	context = gtk_widget_get_style_context (tree_view);
	setting = nemo_config_get_string (nemo_list_view_preferences,
					  NEMO_PREFERENCES_LIST_VIEW_ROW_HOVER_COLOR);

	if ((setting == NULL || !gdk_rgba_parse (&hover, setting)) &&
	    !gtk_style_context_lookup_color (context, "nemo_row_hover", &hover)) {
		if (!state_background (context, GTK_STATE_FLAG_NORMAL, &base) &&
		    !lookup_either (context, "theme_base_color", &base)) {
			gdk_rgba_parse (&base, "white");
		}
		base.alpha = 1.0;

		if (!state_background (context, GTK_STATE_FLAG_SELECTED, &selected) &&
		    !lookup_either (context, "theme_selected_bg_color", &selected)) {
			selected = base;
		}

		nemo_row_hover_pick (&base, &selected, &hover);
	}
	g_free (setting);

	color_text = gdk_rgba_to_string (&hover);
	css = g_strdup_printf ("treeview.view:hover:not(:selected) {"
			       " background-color: %s; background-image: none; }",
			       color_text);
	g_free (color_text);

	/* Loading the sheet restyles the widget, which calls this again. */
	if (g_strcmp0 (css, g_object_get_data (G_OBJECT (tree_view), "nemo-row-hover-css")) == 0) {
		g_free (css);
		return;
	}

	provider = g_object_get_data (G_OBJECT (tree_view), "nemo-row-hover");
	g_object_set_data_full (G_OBJECT (tree_view), "nemo-row-hover-css", css, g_free);
	gtk_css_provider_load_from_data (provider, css, -1, NULL);
}

void
nemo_row_hover_attach (GtkWidget *tree_view)
{
	GtkCssProvider *provider;

	provider = gtk_css_provider_new ();
	gtk_style_context_add_provider (gtk_widget_get_style_context (tree_view),
					GTK_STYLE_PROVIDER (provider),
					GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	g_object_set_data_full (G_OBJECT (tree_view), "nemo-row-hover", provider, g_object_unref);

	g_signal_connect (tree_view, "style-updated", G_CALLBACK (row_hover_update), NULL);
	g_signal_connect_object (nemo_list_view_preferences,
				 "changed::" NEMO_PREFERENCES_LIST_VIEW_ROW_HOVER_COLOR,
				 G_CALLBACK (row_hover_update), tree_view, G_CONNECT_SWAPPED);

	row_hover_update (tree_view);
}

/* The setting wins, then a nemo_row_shading color from the theme or the
 * user's gtk.css, then a faint wash of the text color, which reads on light
 * and dark themes alike. */
void
nemo_row_shading_pick (GtkWidget *tree_view,
		       GdkRGBA   *shading)
{
	GtkStyleContext *context;
	char *color_text;

	context = gtk_widget_get_style_context (tree_view);
	color_text = nemo_config_get_string (nemo_list_view_preferences,
					     NEMO_PREFERENCES_LIST_VIEW_ROW_SHADING_COLOR);

	if (color_text == NULL || !gdk_rgba_parse (shading, color_text)) {
		if (!gtk_style_context_lookup_color (context, "nemo_row_shading", shading)) {
			gtk_style_context_get_color (context, gtk_style_context_get_state (context),
						     shading);
			shading->alpha = 0.06;
		}
	}

	g_free (color_text);
}
