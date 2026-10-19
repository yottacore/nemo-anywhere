/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-sanitizer-leaks.c - the sanitizer lane's leak suppressions hide what
   they name and nothing else.

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

/* Only means something under AddressSanitizer, so elsewhere it skips.
 *
 * The lane turns animations off, since the GTK leak they bring can only be
 * matched by its function on a whole stack, and whole stacks everywhere are
 * too slow for the timed tests. Here they are on, with whole stacks: GTK's
 * theme and each vendored one have their transitions cut short over and over,
 * which is what makes GTK leak, and the leak check has to come back clean.
 *
 * Then 2 planted leaks it has to find. A pattern dropped on fontconfig's
 * path, beside the pattern data that is suppressed there, and strings
 * dropped by an animated widget's frame callback while GTK's own leak is
 * suppressed. Each is freed again afterward, through a pointer kept where
 * the check can't see it. */

#include <config.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <fontconfig/fontconfig.h>
#include <gtk/gtk.h>

#ifdef __SANITIZE_ADDRESS__
#include <sanitizer/lsan_interface.h>

#include <libnemo-private/nemo-appearance.h>
#include <libnemo-private/nemo-config.h>
#include <libnemo-private/nemo-dir-enum.h>
#include <libnemo-private/nemo-global-preferences.h>

#include "test-scratch.h"
#include "test-check.h"

#define PLANTS 16
/* A pointer stored like this is no pointer to the leak check. */
#define HIDE ((uintptr_t) 0x5a5a5a5a5a5a5a5aULL)

static uintptr_t planted[PLANTS];
static guint     n_planted;

/* Hover on and off, faster than any theme's transition runs, so each one
   starts from a value still on its way somewhere else. */
static void
flicker (GList *widgets, guint times)
{
	guint i;

	for (i = 0; i < times; i++) {
		gint64 until = g_get_monotonic_time () + 40 * 1000;
		GList *l;

		for (l = widgets; l != NULL; l = l->next) {
			if (i % 2 == 0) {
				gtk_widget_set_state_flags (l->data, GTK_STATE_FLAG_PRELIGHT, FALSE);
			} else {
				gtk_widget_unset_state_flags (l->data, GTK_STATE_FLAG_PRELIGHT);
			}
		}
		while (g_get_monotonic_time () < until) {
			if (!g_main_context_iteration (NULL, FALSE)) {
				g_usleep (2000);
			}
		}
	}
}

static GList *
make_widgets (GtkWidget *window)
{
	GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
	GtkWidget *items[] = {
		gtk_button_new_with_label ("Button"),
		gtk_toggle_button_new_with_label ("Toggle"),
		gtk_check_button_new_with_label ("Check"),
		gtk_entry_new (),
		gtk_switch_new (),
		gtk_scale_new_with_range (GTK_ORIENTATION_HORIZONTAL, 0, 10, 1),
		gtk_spin_button_new_with_range (0, 10, 1),
	};
	GList *widgets = NULL;
	guint i;

	for (i = 0; i < G_N_ELEMENTS (items); i++) {
		gtk_container_add (GTK_CONTAINER (box), items[i]);
		widgets = g_list_append (widgets, items[i]);
	}
	gtk_container_add (GTK_CONTAINER (window), box);
	gtk_widget_show_all (window);

	return widgets;
}

/* A position going from percent to pixels is a calc() part way, and turned
   back on its way it takes the path GTK leaks on, under any theme. */
static void
add_mixed_unit_transition (GtkWidget *widget)
{
	GtkCssProvider *provider = gtk_css_provider_new ();

	gtk_css_provider_load_from_data (provider,
		".leak-probe { background-image: linear-gradient(red, blue);"
		" background-repeat: no-repeat; background-size: 8px 8px;"
		" background-position: 0% 0%; transition: background-position 400ms linear; }"
		" .leak-probe:hover { background-position: 20px 0%; }",
		-1, NULL);
	gtk_style_context_add_provider_for_screen (gtk_widget_get_screen (widget),
						   GTK_STYLE_PROVIDER (provider),
						   GTK_STYLE_PROVIDER_PRIORITY_USER + 1);
	gtk_style_context_add_class (gtk_widget_get_style_context (widget), "leak-probe");
	g_object_unref (provider);
}

/* A Linux build carries no bundled set, so the vendored themes go in as
   drop-ins, the loader a user's own theme takes. Returns the folder. */
static char *
offer_vendored_themes (const char *tmp)
{
	GFile *vendored = g_file_new_for_path (VENDOR_THEMES_DIR);
	char *dropins = g_build_filename (tmp, "nemo-anywhere", "themes", NULL);
	GFileEnumerator *children;
	GFileInfo *info;

	check (g_mkdir_with_parents (dropins, 0700) == 0);
	children = nemo_enumerate_children (vendored, G_FILE_ATTRIBUTE_STANDARD_NAME,
					    G_FILE_QUERY_INFO_NONE, NULL, NULL);
	check (children != NULL);
	while (children != NULL &&
	       (info = g_file_enumerator_next_file (children, NULL, NULL)) != NULL) {
		char *target = g_build_filename (VENDOR_THEMES_DIR, g_file_info_get_name (info), NULL);
		GFile *link = g_file_new_build_filename (dropins, g_file_info_get_name (info), NULL);

		check (g_file_make_symbolic_link (link, target, NULL, NULL));
		g_object_unref (link);
		g_free (target);
		g_object_unref (info);
	}
	g_clear_object (&children);
	g_object_unref (vendored);

	return dropins;
}

static gpointer
plant_patterns (gpointer data)
{
	guint i;

	(void) data;
	for (i = 0; i < PLANTS; i++) {
		FcPattern *pattern = FcPatternCreate ();

		FcPatternAddString (pattern, FC_FAMILY, (const FcChar8 *) "Planted");
		planted[n_planted++] = (uintptr_t) pattern ^ HIDE;
	}

	return NULL;
}

static gboolean
leaky_frame (GtkWidget *widget, GdkFrameClock *clock, gpointer data)
{
	(void) widget;
	(void) clock;
	(void) data;
	if (n_planted < PLANTS) {
		planted[n_planted++] = (uintptr_t) g_strdup ("planted") ^ HIDE;
	}

	return G_SOURCE_CONTINUE;
}

/* What was planted, freed and forgotten, and the check clean again. */
static void
unplant (GDestroyNotify free_func)
{
	while (n_planted > 0) {
		n_planted--;
		free_func ((gpointer) (planted[n_planted] ^ HIDE));
	}
	check (__lsan_do_recoverable_leak_check () == 0);
}

int
main (int argc, char *argv[])
{
	const char *options = g_getenv ("ASAN_OPTIONS");
	GtkWidget *window;
	GList *widgets;
	GList *themes, *l;
	GThread *thread;
	guint tick;
	guint tried = 0;
	char *tmp;
	char *dropins;

	/* GTK's frames are only on a whole stack. */
	if (options == NULL || strstr (options, "fast_unwind_on_malloc=0") == NULL) {
		g_printerr ("FAIL: ASAN_OPTIONS lacks fast_unwind_on_malloc=0\n");
		return 1;
	}

	tmp = test_scratch_config_home ("nemo-sanitizer-leaks-XXXXXX");
	if (!gtk_init_check (&argc, &argv)) {
		g_print ("SKIP: no display\n");
		g_free (tmp);
		return 77;
	}
	g_object_set (gtk_settings_get_default (), "gtk-enable-animations", TRUE, NULL);
	dropins = offer_vendored_themes (tmp);
	nemo_global_preferences_init ();
	nemo_appearance_init ();

	window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	widgets = make_widgets (window);
	add_mixed_unit_transition (widgets->data);
	while (!gtk_widget_get_mapped (window) && g_main_context_iteration (NULL, TRUE)) {
	}

	/* GTK's own theme first, then each one that comes with the app. */
	flicker (widgets, 8);
	themes = nemo_appearance_list_themes (NEMO_THEME_KIND_WIDGET, NEMO_THEME_FITS_BOTH);
	for (l = themes; l != NULL; l = l->next) {
		NemoThemeInfo *info = l->data;

		if (!info->bundled && (info->dir == NULL || !g_str_has_prefix (info->dir, dropins))) {
			continue;
		}
		nemo_config_set_string (nemo_appearance_preferences,
					NEMO_PREFERENCES_APPEARANCE_GTK_THEME, info->name);
		flicker (widgets, 8);
		tried++;
	}
	g_list_free_full (themes, (GDestroyNotify) nemo_theme_info_free);
	g_print ("transitions run under GTK's theme and %u of the app's\n", tried);
	check (tried > 0);
	check (__lsan_do_recoverable_leak_check () == 0);

	/* Off the main thread, so no copy of a planted pointer is left on a
	   stack the check reads. */
	thread = g_thread_new ("plant", plant_patterns, NULL);
	g_thread_join (thread);
	check (__lsan_do_recoverable_leak_check () != 0);
	unplant ((GDestroyNotify) FcPatternDestroy);

	tick = gtk_widget_add_tick_callback (g_list_first (widgets)->data, leaky_frame, NULL, NULL);
	flicker (widgets, 8);
	gtk_widget_remove_tick_callback (g_list_first (widgets)->data, tick);
	check (n_planted > 0);
	check (__lsan_do_recoverable_leak_check () != 0);
	unplant (g_free);

	g_list_free (widgets);
	gtk_widget_destroy (window);
	g_free (dropins);
	g_free (tmp);

	if (failures > 0) {
		g_printerr ("%d failure(s)\n", failures);
		return EXIT_FAILURE;
	}

	g_print ("OK\n");
	return EXIT_SUCCESS;
}

#else

int
main (void)
{
	g_print ("SKIP: needs a build with AddressSanitizer\n");
	return 77;
}

#endif
