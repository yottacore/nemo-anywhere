/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-eel-stock-dialogs.c - message text that can be copied out.

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

/* The message in an error or question dialog can be selected, so it can be
 * copied. A selectable label also takes focus, and the first one took it with
 * the whole message highlighted and the default button unfocused, so every
 * label in the message area has to be selectable and unable to take focus.
 * Needs a display and skips without one. */

#include <config.h>

#include <stdlib.h>
#include <gtk/gtk.h>

#include <eel/eel-stock-dialogs.h>

#include "test-check.h"

static void
check_message_labels (GtkDialog *dialog, const char *what)
{
	GtkWidget *area = gtk_message_dialog_get_message_area (GTK_MESSAGE_DIALOG (dialog));
	GList *children = gtk_container_get_children (GTK_CONTAINER (area));
	GList *l;
	int labels = 0;

	for (l = children; l != NULL; l = l->next) {
		if (!GTK_IS_LABEL (l->data)) {
			continue;
		}
		labels++;
		if (!gtk_label_get_selectable (GTK_LABEL (l->data))) {
			g_printerr ("FAIL: %s: label %d is not selectable\n", what, labels);
			failures++;
		}
		if (gtk_widget_get_can_focus (GTK_WIDGET (l->data))) {
			g_printerr ("FAIL: %s: label %d can take focus\n", what, labels);
			failures++;
		}
	}
	g_list_free (children);

	/* The headline and the text under it. */
	if (labels < 2) {
		g_printerr ("FAIL: %s: %d label(s) in the message area\n", what, labels);
		failures++;
	}
}

int
main (int argc, char *argv[])
{
	GtkDialog *dialog;

	if (!gtk_init_check (&argc, &argv)) {
		g_print ("SKIP: no display\n");
		return 77;
	}

	dialog = eel_show_error_dialog ("Could not copy", "The disk is full.", NULL);
	check_message_labels (dialog, "error");
	gtk_widget_destroy (GTK_WIDGET (dialog));

	dialog = eel_show_error_dialog_with_details ("Could not copy", "The disk is full.",
						     "No space left on device", NULL);
	check_message_labels (dialog, "error with details");
	gtk_widget_destroy (GTK_WIDGET (dialog));

	dialog = eel_create_question_dialog ("Replace it?", "A file of that name is there.",
					     "Skip", GTK_RESPONSE_CANCEL,
					     "Replace", GTK_RESPONSE_YES, NULL);
	check_message_labels (dialog, "question");
	gtk_widget_destroy (GTK_WIDGET (dialog));

	if (failures == 0) {
		g_print ("eel-stock-dialogs: all checks passed\n");
	}

	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
