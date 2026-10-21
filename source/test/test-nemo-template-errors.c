/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-template-errors.c - a template rename or add that fails says so.

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

/* The templates part of Settings renamed and added templates with no callback,
 * so a rename or copy that failed on disk said nothing (2026101416533954).
 * Each is driven here the way its button's dialog ends, and the error has to
 * come up in a message dialog. One that works has to say nothing. */

#include <config.h>

#include <string.h>

#include <glib/gstdio.h>
#include <gtk/gtk.h>

#include <libnemo-private/nemo-global-preferences.h>

#include "nemo-application.h"
#include "nemo-template-config-widget.h"

#include "test-scratch.h"
#include "test-check.h"

/* The widget's folder button opens a window through the app, which isn't
   here.
   Returns: (transfer none): always NULL */
NemoApplication *
nemo_application_get_singleton (void)
{
	return NULL;
}

void
nemo_application_open_location (G_GNUC_UNUSED NemoApplication *application,
				G_GNUC_UNUSED GFile           *location,
				G_GNUC_UNUSED GFile           *selection,
				G_GNUC_UNUSED const char      *startup_id)
{
}

/* The text of the first message dialog up within limit_ms, closed as it's
   read, or NULL for none.
   Returns: (transfer full) */
static char *
wait_for_message (int limit_ms)
{
	gint64 end = g_get_monotonic_time () + (gint64) (limit_ms * 1000 * test_slowness ());

	while (g_get_monotonic_time () < end) {
		GList *windows = gtk_window_list_toplevels ();
		char *said = NULL;

		for (GList *l = windows; l != NULL && said == NULL; l = l->next) {
			if (GTK_IS_MESSAGE_DIALOG (l->data)) {
				g_autofree char *text = NULL;
				g_autofree char *details = NULL;

				g_object_get (l->data, "text", &text, "secondary-text", &details, NULL);
				said = g_strdup_printf ("%s\n%s", text, details != NULL ? details : "");
				gtk_widget_destroy (l->data);
			}
		}
		g_list_free (windows);
		if (said != NULL) {
			return said;
		}
		g_main_context_iteration (NULL, FALSE);
		g_usleep (5000);
	}

	return NULL;
}

static void
write_text (const char *path, const char *text)
{
	check (g_file_set_contents (path, text, -1, NULL));
}

int
main (int argc, char **argv)
{
	g_autofree char *tmp = NULL;
	g_autofree char *dir = NULL;
	g_autofree char *taken_from = NULL;
	g_autofree char *taken_to = NULL;
	g_autofree char *free_from = NULL;
	g_autofree char *free_to = NULL;
	g_autofree char *missing = NULL;
	char *said;
	GtkWidget *window, *widget;

	tmp = test_scratch_config_home ("nemo-template-errors-XXXXXX");
	if (tmp == NULL) {
		g_printerr ("could not make a scratch dir\n");
		return 77;
	}
	if (!gtk_init_check (&argc, &argv)) {
		g_print ("SKIP: no display\n");
		return 77;
	}
	nemo_global_preferences_init ();

	window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	widget = nemo_template_config_widget_new ();
	gtk_container_add (GTK_CONTAINER (window), widget);
	gtk_widget_show_all (window);

	dir = g_build_filename (tmp, "picked", NULL);
	check (g_mkdir (dir, 0700) == 0);
	taken_from = g_build_filename (dir, "letter.odt", NULL);
	taken_to = g_build_filename (dir, "memo.odt", NULL);
	free_from = g_build_filename (dir, "sheet.ods", NULL);
	free_to = g_build_filename (dir, "budget.ods", NULL);
	write_text (taken_from, "letter");
	write_text (taken_to, "memo");
	write_text (free_from, "sheet");

	/* Onto a name that's taken, which fails the same as root or not. */
	nemo_template_config_widget_rename (NEMO_TEMPLATE_CONFIG_WIDGET (widget), taken_from, "memo.odt");
	said = wait_for_message (5000);
	check (said != NULL && strstr (said, "The item couldn't be renamed.") != NULL &&
	       strstr (said, "\"memo.odt\" is already used") != NULL);
	if (said == NULL) {
		g_printerr ("a failed rename said nothing\n");
	}
	g_free (said);
	check (g_file_test (taken_from, G_FILE_TEST_EXISTS));

	nemo_template_config_widget_rename (NEMO_TEMPLATE_CONFIG_WIDGET (widget), free_from, "budget.ods");
	said = wait_for_message (1000);
	check (said == NULL);
	g_free (said);
	check (g_file_test (free_to, G_FILE_TEST_EXISTS));

#ifndef G_OS_WIN32
	/* On Windows the templates folder is the profile's real one. */
	missing = g_build_filename (dir, "not there.odt", NULL);
	nemo_template_config_widget_import (NEMO_TEMPLATE_CONFIG_WIDGET (widget), missing);
	said = wait_for_message (5000);
	check (said != NULL && strstr (said, "The template couldn't be added.") != NULL);
	if (said == NULL) {
		g_printerr ("a failed add said nothing\n");
	}
	g_free (said);

	{
		g_autofree char *added = g_build_filename (g_get_home_dir (), "Templates", "memo.odt", NULL);

		nemo_template_config_widget_import (NEMO_TEMPLATE_CONFIG_WIDGET (widget), taken_to);
		said = wait_for_message (1000);
		check (said == NULL);
		g_free (said);
		check (g_file_test (added, G_FILE_TEST_EXISTS));
	}
#endif

	gtk_widget_destroy (window);

	if (failures > 0) {
		g_printerr ("%d check(s) failed\n", failures);
		return 1;
	}

	g_print ("OK\n");
	return 0;
}
