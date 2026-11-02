/* -*- Mode: ObjC; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-drop-quartz.m - files dropped in from another window on macOS.

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

/* A drag from one window to another, each its own process, dropped nothing.
 * The drag puts the icon list on the pasteboard under the UTI the toolkit
 * makes from its MIME name, and the drop read it back under the bare name.
 *
 * The pointer is left alone. The window's NSDraggingDestination calls are
 * made directly, as AppKit would, with a pasteboard of the test's own filled
 * the way the toolkit fills one for a drag out of a folder view. */

#include <config.h>

#include <string.h>

#import <AppKit/AppKit.h>

#include <gtk/gtk.h>

#include <libnemo-private/nemo-dnd.h>

#include "test-check.h"

/* Exported by GDK, but its headers keep them to the toolkit. */
extern NSWindow *gdk_quartz_window_get_nswindow (GdkWindow *window);
extern NSString *gdk_quartz_target_to_pasteboard_type_libgtk_only (const gchar *target);

/* What a folder view offers when a drag starts, and what one takes. */
static const GtkTargetEntry drag_types[] = {
	{ (char *) NEMO_ICON_DND_GNOME_ICON_LIST_TYPE, 0, NEMO_ICON_DND_GNOME_ICON_LIST },
	{ (char *) NEMO_ICON_DND_URI_LIST_TYPE, 0, NEMO_ICON_DND_URI_LIST },
};

static const GtkTargetEntry drop_types[] = {
	{ (char *) NEMO_ICON_DND_GNOME_ICON_LIST_TYPE, 0, NEMO_ICON_DND_GNOME_ICON_LIST },
	{ (char *) NEMO_ICON_DND_XDNDDIRECTSAVE_TYPE, 0, NEMO_ICON_DND_XDNDDIRECTSAVE },
	{ (char *) NEMO_ICON_DND_NETSCAPE_URL_TYPE, 0, NEMO_ICON_DND_NETSCAPE_URL },
	{ (char *) NEMO_ICON_DND_URI_LIST_TYPE, 0, NEMO_ICON_DND_URI_LIST },
	{ (char *) NEMO_ICON_DND_RAW_TYPE, 0, NEMO_ICON_DND_RAW },
};

static const char *dragged[] = {
	"file:///Users/someone/Documents/first%20one.txt",
	"file:///Users/someone/Documents/second.png",
};

/* The drag as AppKit hands it to a window, from another process. */
@interface FakeDrag : NSObject {
@public
	NSPasteboard *pasteboard;
	NSWindow *window;
	NSPoint location;
}
@end

@implementation FakeDrag
- (NSPasteboard *) draggingPasteboard { return pasteboard; }
- (NSPoint) draggingLocation { return location; }
- (NSWindow *) draggingDestinationWindow { return window; }
- (id) draggingSource { return nil; }
- (NSInteger) draggingSequenceNumber { return 1; }
- (NSDragOperation) draggingSourceOperationMask
{
	return NSDragOperationCopy | NSDragOperationMove | NSDragOperationLink;
}
@end

static void
each_dragged (NemoDragEachSelectedItemDataGet iteratee, G_GNUC_UNUSED gpointer iterator_context, gpointer data)
{
	for (guint i = 0; i < G_N_ELEMENTS (dragged); i++) {
		iteratee (dragged[i], NULL, 4 + 40 * (int) i, 4, 32, 32, data);
	}
}

/* Filled as the toolkit's drag source fills it: each type declared under the
 * name it maps the target to, a custom one's bytes as they are, and the uri
 * list as one URL. */
static NSPasteboard *
folder_view_drag (void)
{
	NSPasteboard *pb = [NSPasteboard pasteboardWithUniqueName];
	NSMutableArray *types = [NSMutableArray array];

	for (guint i = 0; i < G_N_ELEMENTS (drag_types); i++) {
		[types addObject:gdk_quartz_target_to_pasteboard_type_libgtk_only (drag_types[i].target)];
	}
	[pb declareTypes:types owner:nil];

	char *icons = nemo_drag_selection_payload (NEMO_ICON_DND_GNOME_ICON_LIST, NULL, each_dragged);
	[pb setData:[NSData dataWithBytes:icons length:strlen (icons)]
	    forType:gdk_quartz_target_to_pasteboard_type_libgtk_only (NEMO_ICON_DND_GNOME_ICON_LIST_TYPE)];
	g_free (icons);

	[[NSURL URLWithString:[NSString stringWithUTF8String:dragged[0]]] writeToPasteboard:pb];

	return pb;
}

/* The widget's side, as the folder views do it. */

static int probe_x, probe_y;
static GdkAtom motion_target;
static gint motion_length = -2;
static gboolean dropping;
static gboolean drop_seen;
static GPtrArray *dropped;

static gboolean
on_motion (GtkWidget *widget, GdkDragContext *context, G_GNUC_UNUSED int x, G_GNUC_UNUSED int y, guint time)
{
	motion_target = gtk_drag_dest_find_target (widget, context, NULL);
	if (motion_target == GDK_NONE) {
		gdk_drag_status (context, 0, time);
		return TRUE;
	}

	gtk_drag_get_data (widget, context, motion_target, time);
	return TRUE;
}

static gboolean
on_drop (GtkWidget *widget, GdkDragContext *context, G_GNUC_UNUSED int x, G_GNUC_UNUSED int y, guint time)
{
	GdkAtom target = gtk_drag_dest_find_target (widget, context, NULL);

	if (target == GDK_NONE) {
		gtk_drag_finish (context, FALSE, FALSE, time);
		return TRUE;
	}

	dropping = TRUE;
	gtk_drag_get_data (widget, context, target, time);
	return TRUE;
}

static void
on_received (G_GNUC_UNUSED GtkWidget *widget, GdkDragContext *context, G_GNUC_UNUSED int x, G_GNUC_UNUSED int y,
	     GtkSelectionData *data, guint info, guint time)
{
	if (!dropping) {
		motion_length = gtk_selection_data_get_length (data);
		gdk_drag_status (context, motion_length > 0 ? GDK_ACTION_COPY : 0, time);
		return;
	}

	dropping = FALSE;
	drop_seen = TRUE;

	if (info == NEMO_ICON_DND_GNOME_ICON_LIST && gtk_selection_data_get_length (data) > 0) {
		GList *items = nemo_drag_build_selection_list (data);

		for (GList *l = items; l != NULL; l = l->next) {
			g_ptr_array_add (dropped, g_strdup (((NemoDragSelectionItem *) l->data)->uri));
		}
		nemo_drag_destroy_selection_list (items);
	} else if (info == NEMO_ICON_DND_URI_LIST) {
		char **uris = gtk_selection_data_get_uris (data);

		for (char **u = uris; u != NULL && *u != NULL; u++) {
			g_ptr_array_add (dropped, g_strdup (*u));
		}
		g_strfreev (uris);
	}

	gtk_drag_finish (context, dropped->len > 0, FALSE, time);
}

static void
note_motion (GdkEvent *event, G_GNUC_UNUSED gpointer data)
{
	if (event->type == GDK_DRAG_MOTION) {
		probe_x = event->dnd.x_root;
		probe_y = event->dnd.y_root;
	}
	gtk_main_do_event (event);
}

static void
spin (int rounds)
{
	for (int i = 0; i < rounds; i++) {
		while (g_main_context_iteration (NULL, FALSE)) {
		}
		g_usleep (2000);
	}
}

int
main (int argc, char **argv)
{
	@autoreleasepool {
		gtk_init (&argc, &argv);
		dropped = g_ptr_array_new_with_free_func (g_free);
		gdk_event_handler_set (note_motion, NULL, NULL);

		GtkWidget *window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
		GtkWidget *view = gtk_event_box_new ();

		gtk_window_set_default_size (GTK_WINDOW (window), 240, 160);
		gtk_container_add (GTK_CONTAINER (window), view);
		gtk_drag_dest_set (view, 0, drop_types, G_N_ELEMENTS (drop_types),
				   GDK_ACTION_COPY | GDK_ACTION_MOVE | GDK_ACTION_LINK);
		g_signal_connect (view, "drag-motion", G_CALLBACK (on_motion), NULL);
		g_signal_connect (view, "drag-drop", G_CALLBACK (on_drop), NULL);
		g_signal_connect (view, "drag-data-received", G_CALLBACK (on_received), NULL);

		gtk_widget_show_all (window);
		for (int i = 0; i < 2500 && (!gtk_widget_get_mapped (view) || gtk_widget_get_allocated_width (view) < 100); i++) {
			spin (1);
		}
		check (gtk_widget_get_mapped (view));
		check (gtk_widget_get_allocated_width (view) >= 100);

		FakeDrag *drag = [[FakeDrag alloc] init];
		NSWindow *nswindow = gdk_quartz_window_get_nswindow (gtk_widget_get_window (window));
		NSRect content = [[nswindow contentView] frame];

		drag->pasteboard = folder_view_drag ();
		drag->window = nswindow;
		drag->location = NSMakePoint (NSMidX (content), NSMidY (content));
		id<NSDraggingInfo> sender = (id) drag;

		[(id) nswindow draggingEntered:sender];

		/* With no display awake GDK's screen origin is off, and a point in
		 * the window can end up outside it. One motion shows by how much. */
		int wx, wy, cx, cy;

		[(id) nswindow draggingUpdated:sender];
		gdk_window_get_position (gtk_widget_get_window (window), &wx, &wy);
		gtk_widget_translate_coordinates (view, window, gtk_widget_get_allocated_width (view) / 2,
						  gtk_widget_get_allocated_height (view) / 2, &cx, &cy);
		drag->location.x += wx + cx - probe_x;
		drag->location.y -= wy + cy - probe_y;
		motion_target = GDK_NONE;
		motion_length = -2;

		[(id) nswindow draggingUpdated:sender];
		spin (5);

		check (motion_target == gdk_atom_intern_static_string (NEMO_ICON_DND_GNOME_ICON_LIST_TYPE));
		check (motion_length > 0);

		[(id) nswindow performDragOperation:sender];
		[(id) nswindow draggingEnded:sender];
		spin (5);

		check (drop_seen);
		check (dropped->len == G_N_ELEMENTS (dragged));
		for (guint i = 0; i < dropped->len && i < G_N_ELEMENTS (dragged); i++) {
			check (g_strcmp0 (g_ptr_array_index (dropped, i), dragged[i]) == 0);
		}
		if (dropped->len != G_N_ELEMENTS (dragged)) {
			g_printerr ("dropped %u of %u files, motion read %d bytes\n",
				    dropped->len, (guint) G_N_ELEMENTS (dragged), motion_length);
		}

		[drag->pasteboard releaseGlobally];
		[drag release];
		gtk_widget_destroy (window);
		spin (5);
		g_ptr_array_unref (dropped);
	}

	return failures == 0 ? 0 : 1;
}
