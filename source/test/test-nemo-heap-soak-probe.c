/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-heap-soak-probe.c - opens and closes tabs, folders and thumbnails
   over and over from inside the program, and reads the heap as it goes.

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

/* Preloaded into the real program by heap-soak.bash, beside heaptrack. The
 * window starts on a folder of images. Each round opens a tab, goes up to the
 * parent and back, shows the list view and the icon view again, so the
 * thumbnails are asked for again, reloads, and closes the tab.
 *
 * After round $NEMO_SOAK_ROUNDS and after twice that many, it waits for the
 * program to go quiet and writes "heap <round> <bytes>" to $NEMO_SOAK_OUT.
 * Then it closes the window and writes "done". $NEMO_SOAK_PLANT bytes are
 * kept each round, for the check that the soak sees a heap that grows. */

#include <stdio.h>
#include <malloc.h>
#include <unistd.h>

#include <gtk/gtk.h>

/* Each key gets this long to take effect. */
#define STEP_MS 300
/* How long the program has to sit still before a reading. */
#define QUIET_MS 2000
/* A reading is the least of these, so work still finishing, such as a
   thumbnail thread's, doesn't count as growth. */
#define SAMPLES 6
#define SAMPLE_MS 500
#define START_TIMEOUT_MS 60000

static FILE      *out;
static GtkWidget *window;
static int        rounds;
static int        round_no;
static int        step;
static gsize      plant;
static GSList    *planted;
static guint32    stamp;

static void
send_key_event (GdkEventType type, guint keyval, guint modifiers)
{
	GdkDisplay *display = gtk_widget_get_display (window);
	GdkEvent *event = gdk_event_new (type);
	GdkKeymapKey *keys = NULL;
	gint n_keys = 0;

	stamp += 20;
	event->key.window = g_object_ref (gtk_widget_get_window (window));
	event->key.send_event = TRUE;
	event->key.time = stamp;
	event->key.keyval = keyval;
	event->key.state = modifiers;
	if (gdk_keymap_get_entries_for_keyval (gdk_keymap_get_for_display (display),
					       gdk_keyval_to_lower (keyval), &keys, &n_keys) && n_keys > 0) {
		event->key.hardware_keycode = keys[0].keycode;
		event->key.group = keys[0].group;
	}
	g_free (keys);
	gdk_event_set_device (event, gdk_seat_get_keyboard (gdk_display_get_default_seat (display)));

	gtk_main_do_event (event);
	gdk_event_free (event);
}

static void
key (guint keyval, guint modifiers)
{
	send_key_event (GDK_KEY_PRESS, keyval, modifiers);
	send_key_event (GDK_KEY_RELEASE, keyval, modifiers);
}

static const struct {
	guint keyval;
	guint modifiers;
} round_keys[] = {
	{ GDK_KEY_t,    GDK_CONTROL_MASK },	/* a new tab on the same folder */
	{ GDK_KEY_Up,   GDK_MOD1_MASK },	/* its parent */
	{ GDK_KEY_Left, GDK_MOD1_MASK },	/* and back */
	{ GDK_KEY_2,    GDK_CONTROL_MASK },	/* list view */
	{ GDK_KEY_1,    GDK_CONTROL_MASK },	/* icon view, thumbnails again */
	{ GDK_KEY_r,    GDK_CONTROL_MASK },	/* reload */
	{ GDK_KEY_w,    GDK_CONTROL_MASK },	/* close the tab */
};

static gint64
heap_in_use (void)
{
	struct mallinfo2 info = mallinfo2 ();

	return (gint64) (info.uordblks + info.hblkhd);
}

static gboolean finish (gpointer data);

static gboolean
read_heap (G_GNUC_UNUSED gpointer data)
{
	static int    sampled;
	static gint64 least;
	gint64        now = heap_in_use ();

	if (sampled == 0 || now < least) {
		least = now;
	}
	if (++sampled < SAMPLES) {
		return G_SOURCE_CONTINUE;
	}
	sampled = 0;
	fprintf (out, "heap %d %" G_GINT64_FORMAT "\n", round_no, least);
	fflush (out);
	if (round_no >= 2 * rounds) {
		g_timeout_add (STEP_MS, finish, NULL);
		return G_SOURCE_REMOVE;
	}
	step = 0;
	return G_SOURCE_REMOVE;
}

static gboolean
finish (G_GNUC_UNUSED gpointer data)
{
	fprintf (out, "done\n");
	fflush (out);
	/* As a person closes it, so heaptrack sees the program's own exit. */
	gtk_window_close (GTK_WINDOW (window));
	return G_SOURCE_REMOVE;
}

static gboolean
start_reading (G_GNUC_UNUSED gpointer data)
{
	g_timeout_add (SAMPLE_MS, read_heap, NULL);
	return G_SOURCE_REMOVE;
}

static gboolean
tick (G_GNUC_UNUSED gpointer data)
{
	if (step < 0) {
		return G_SOURCE_CONTINUE;
	}
	key (round_keys[step].keyval, round_keys[step].modifiers);
	if (++step < (int) G_N_ELEMENTS (round_keys)) {
		return G_SOURCE_CONTINUE;
	}

	round_no++;
	if (plant > 0) {
		planted = g_slist_prepend (planted, g_malloc0 (plant));
	}
	if (round_no == rounds || round_no == 2 * rounds) {
		step = -1;
		g_timeout_add (QUIET_MS, start_reading, NULL);
		if (round_no == 2 * rounds) {
			return G_SOURCE_REMOVE;
		}
		return G_SOURCE_CONTINUE;
	}
	step = 0;
	return G_SOURCE_CONTINUE;
}

static gboolean
wait_for_window (G_GNUC_UNUSED gpointer data)
{
	static int waited;
	GList *toplevels, *l;

	toplevels = gtk_window_list_toplevels ();
	for (l = toplevels; l != NULL; l = l->next) {
		if (g_type_from_name ("NemoWindow") != 0 &&
		    g_type_is_a (G_OBJECT_TYPE (l->data), g_type_from_name ("NemoWindow")) &&
		    gtk_widget_get_mapped (l->data)) {
			window = l->data;
		}
	}
	g_list_free (toplevels);

	if (window == NULL) {
		waited += 100;
		if (waited > START_TIMEOUT_MS) {
			fprintf (out, "FAIL no window\n");
			fflush (out);
			return G_SOURCE_REMOVE;
		}
		return G_SOURCE_CONTINUE;
	}
	fprintf (out, "start %d\n", (int) getpid ());
	fflush (out);
	g_timeout_add (STEP_MS, tick, NULL);
	return G_SOURCE_REMOVE;
}

__attribute__((constructor)) static void
probe_start (void)
{
	const char *path = g_getenv ("NEMO_SOAK_OUT");
	const char *count = g_getenv ("NEMO_SOAK_ROUNDS");
	const char *bytes = g_getenv ("NEMO_SOAK_PLANT");

	if (path == NULL || count == NULL) {
		return;
	}
	rounds = (int) g_ascii_strtoll (count, NULL, 10);
	plant = bytes != NULL ? (gsize) g_ascii_strtoull (bytes, NULL, 10) : 0;
	out = fopen (path, "a");
	if (out == NULL || rounds < 1) {
		return;
	}
	g_timeout_add (100, wait_for_window, NULL);
}
