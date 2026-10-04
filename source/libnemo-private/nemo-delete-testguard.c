/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-delete-testguard.c - stop on every trash and delete, for testing.

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

#include <config.h>

#include "nemo-delete-testguard.h"

#include <glib/gi18n.h>

#include "nemo-delete-guard.h"
#include "nemo-global-preferences.h"
#include "nemo-job-queue.h"

#if HAVE_BACKTRACE
#include <execinfo.h>
#define TESTGUARD_MAX_FRAMES 24
#endif

/* Long enough to read, short enough to fit a dialog. */
#define MAX_PATHS_SHOWN 40

/* Long enough for a person to read the thing, short enough that a run with
   nobody watching ends rather than wedges. */
#define ANSWER_TIMEOUT_USEC (300 * G_USEC_PER_SEC)

/* Said on every ask, so nobody takes it for a real problem with the files. */
#define WHY_ASKED "Every pre-release asks this, as a rule. This release candidate does " \
		  "not need it, and it can be turned off in Preferences, Behavior, Trash."

/* Set while a job works through what it already asked about, so the two
   removal functions below it stay quiet. A job and its removals run on one
   thread, which is what makes this enough. */
static GPrivate covered = G_PRIVATE_INIT (NULL);

/* The define is the loudest of the three ways in: on, nothing quiets it. */
static const gboolean forced_on = (NEMO_TESTGUARD_ALL_DELETES != 0);

/* 1 for on, 0 for off, -1 for "said nothing". */
static int
parse_choice (const char *text)
{
	static const char *const on[]  = { "1", "true", "yes", "on", NULL };
	static const char *const off[] = { "0", "false", "no", "off", NULL };
	int i;

	for (i = 0; on[i] != NULL; i++) {
		if (g_ascii_strcasecmp (text, on[i]) == 0) {
			return 1;
		}
	}

	for (i = 0; off[i] != NULL; i++) {
		if (g_ascii_strcasecmp (text, off[i]) == 0) {
			return 0;
		}
	}

	return -1;
}

/* Read once. The environment does not change under a running process, and a
   delete is no place to be parsing strings. */
static int
env_choice (void)
{
	static gsize read_once = 0;
	static int choice = -1;

	if (g_once_init_enter (&read_once)) {
		const char *text = g_getenv (NEMO_TESTGUARD_ENV_VAR);

		if (text != NULL && *text != '\0') {
			choice = parse_choice (text);
			if (choice < 0) {
				g_warning ("%s is set to \"%s\", which means neither on nor off - ignoring it",
					   NEMO_TESTGUARD_ENV_VAR, text);
			}
		}

		g_once_init_leave (&read_once, 1);
	}

	return choice;
}

gboolean
nemo_delete_testguard_armed (void)
{
	int choice;

	if (forced_on) {
		return TRUE;
	}

	choice = env_choice ();
	if (choice >= 0) {
		return choice == 1;
	}

	/* A delete can be reached from local_command_line, before the settings
	   file has been read. Nothing is known yet, so the default answers. */
	if (!nemo_config_is_ready ()) {
		return nemo_config_get_default_boolean (NEMO_DEBUG_GROUP,
							NEMO_PREFERENCES_TESTGUARD_ALL_DELETES);
	}

	return nemo_config_get_boolean (nemo_config_get_group (NEMO_DEBUG_GROUP),
					NEMO_PREFERENCES_TESTGUARD_ALL_DELETES);
}

/* Heap held, since the waiting side can give up on the timeout below while the
   dialog is still up. Whoever lets go last frees it. */
typedef struct {
	const char *op;
	char *primary;
	char *detail;
	gboolean go_ahead;

	GMutex lock;
	GCond done;
	gboolean answered;
	guint refs;
} AskData;

static void
ask_data_unref (AskData *data)
{
	gboolean last;

	g_mutex_lock (&data->lock);
	data->refs--;
	last = (data->refs == 0);
	g_mutex_unlock (&data->lock);

	if (!last) {
		return;
	}

	g_mutex_clear (&data->lock);
	g_cond_clear (&data->done);
	g_free (data->primary);
	g_free (data->detail);
	g_free (data);
}

void
nemo_delete_testguard_begin (void)
{
	gsize depth = GPOINTER_TO_SIZE (g_private_get (&covered));

	g_private_set (&covered, GSIZE_TO_POINTER (depth + 1));
}

void
nemo_delete_testguard_end (void)
{
	gsize depth = GPOINTER_TO_SIZE (g_private_get (&covered));

	if (depth > 0) {
		g_private_set (&covered, GSIZE_TO_POINTER (depth - 1));
	}
}

static gboolean
already_asked (void)
{
	return GPOINTER_TO_SIZE (g_private_get (&covered)) > 0;
}

static char *
describe_stack (void)
{
#if HAVE_BACKTRACE
	void *frames[TESTGUARD_MAX_FRAMES];
	char **names;
	GString *out;
	int n_frames;
	int i;

	n_frames = backtrace (frames, TESTGUARD_MAX_FRAMES);
	if (n_frames <= 0) {
		return g_strdup ("  (no frames)");
	}

	names = backtrace_symbols (frames, n_frames);
	if (names == NULL) {
		return g_strdup ("  (no symbols)");
	}

	out = g_string_new (NULL);
	/* Frame 0 is this function, 1 is the text builder below it. */
	for (i = 2; i < n_frames; i++) {
		g_string_append_printf (out, "  %s\n", names[i]);
	}

	free (names);

	return g_string_free (out, FALSE);
#else
	return g_strdup ("  (not available in this build)");
#endif
}

static char *
describe_files (GList *files)
{
	GString *out;
	GList *l;
	guint shown = 0;

	out = g_string_new (NULL);

	for (l = files; l != NULL; l = l->next) {
		char *name;

		if (shown == MAX_PATHS_SHOWN) {
			g_string_append_printf (out, "  ... and %u more\n",
						g_list_length (l));
			break;
		}

		name = g_file_get_parse_name (l->data);
		g_string_append_printf (out, "  %s\n", name != NULL ? name : "?");
		g_free (name);
		shown++;
	}

	return g_string_free (out, FALSE);
}

static char *
count_line (const char *op, guint count)
{
	return g_strdup_printf ("%s: %u item%s", op, count, count == 1 ? "" : "s");
}

/* Everything the dialog shows below the headline: where it came from in the C
   source, what it would take, and how it got there. */
static char *
build_detail (GList      *files,
	      const char *func,
	      const char *source_file,
	      int         line)
{
	char *paths;
	char *stack;
	char *detail;

	paths = describe_files (files);
	stack = describe_stack ();

	detail = g_strdup_printf ("Asked from %s at %s:%d\n\nItems:\n%s\nCall stack:\n%s",
				  func, source_file, line, paths, stack);

	g_free (paths);
	g_free (stack);

	return detail;
}

void
nemo_delete_testguard_dialog_caps (int  area_width,
				   int  area_height,
				   int *max_width,
				   int *max_height)
{
	if (area_width >= area_height) {
		*max_width = area_width / 4;
		*max_height = area_height / 2;
	} else {
		*max_width = area_width / 2;
		*max_height = area_height / 4;
	}
}

/* The dialog has no parent, and GTK centers one of those on the monitor the
   pointer is on, so that is the one to measure. */
static GdkMonitor *
dialog_monitor (GdkDisplay *display)
{
	GdkSeat *seat = gdk_display_get_default_seat (display);
	GdkDevice *pointer = seat != NULL ? gdk_seat_get_pointer (seat) : NULL;
	GdkMonitor *monitor = NULL;

	if (pointer != NULL) {
		int x, y;

		gdk_device_get_position (pointer, NULL, &x, &y);
		monitor = gdk_display_get_monitor_at_point (display, x, y);
	}
	if (monitor == NULL) {
		monitor = gdk_display_get_primary_monitor (display);
	}
	if (monitor == NULL) {
		monitor = gdk_display_get_monitor (display, 0);
	}

	return monitor;
}

/* Forty paths and a call stack can be taller than the screen, so the detail
   scrolls and the buttons keep their own row below it. The window is only as
   big as its text wants, up to the caps above, and it takes width before
   height so a long path stays on one line where it can. */
static gboolean
show_dialog (gpointer _data)
{
	AskData *data = _data;
	GtkWidget *dialog;
	GtkWidget *content;
	GtkWidget *header;
	GtkWidget *icon;
	GtkWidget *words;
	GtkWidget *headline;
	GtkWidget *why;
	GtkWidget *scroll;
	GtkWidget *detail;
	GdkMonitor *monitor;
	char *markup;
	int response;

	dialog = gtk_dialog_new ();
	gtk_window_set_title (GTK_WINDOW (dialog), "Delete/overwrite test guard");
	gtk_window_set_position (GTK_WINDOW (dialog), GTK_WIN_POS_CENTER);
	gtk_window_set_urgency_hint (GTK_WINDOW (dialog), TRUE);
	gtk_window_set_keep_above (GTK_WINDOW (dialog), TRUE);

	gtk_dialog_add_button (GTK_DIALOG (dialog), GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);
	gtk_dialog_add_button (GTK_DIALOG (dialog), GTK_STOCK_OK, GTK_RESPONSE_OK);
	gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_CANCEL);

	content = gtk_dialog_get_content_area (GTK_DIALOG (dialog));
	gtk_container_set_border_width (GTK_CONTAINER (content), 12);
	gtk_box_set_spacing (GTK_BOX (content), 12);

	header = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
	icon = gtk_image_new_from_icon_name ("dialog-warning", GTK_ICON_SIZE_DIALOG);
	gtk_widget_set_valign (icon, GTK_ALIGN_START);
	gtk_box_pack_start (GTK_BOX (header), icon, FALSE, FALSE, 0);

	/* Wraps, but never so narrow that it is hard to read. */
	markup = g_markup_printf_escaped ("<b>%s</b>", data->primary);
	headline = gtk_label_new (NULL);
	gtk_label_set_markup (GTK_LABEL (headline), markup);
	g_free (markup);
	gtk_label_set_line_wrap (GTK_LABEL (headline), TRUE);
	gtk_label_set_width_chars (GTK_LABEL (headline), 30);
	gtk_label_set_max_width_chars (GTK_LABEL (headline), 60);
	gtk_label_set_xalign (GTK_LABEL (headline), 0.0);

	why = gtk_label_new (WHY_ASKED);
	gtk_label_set_line_wrap (GTK_LABEL (why), TRUE);
	gtk_label_set_width_chars (GTK_LABEL (why), 30);
	gtk_label_set_max_width_chars (GTK_LABEL (why), 60);
	gtk_label_set_xalign (GTK_LABEL (why), 0.0);

	words = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
	gtk_widget_set_valign (words, GTK_ALIGN_CENTER);
	gtk_box_pack_start (GTK_BOX (words), headline, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (words), why, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (header), words, TRUE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (content), header, FALSE, FALSE, 0);

	detail = gtk_label_new (data->detail);
	gtk_label_set_selectable (GTK_LABEL (detail), TRUE);
	gtk_label_set_xalign (GTK_LABEL (detail), 0.0);
	gtk_label_set_yalign (GTK_LABEL (detail), 0.0);
	g_object_set (detail, "margin", 6, NULL);

	scroll = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroll),
					GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (scroll), GTK_SHADOW_IN);
	gtk_scrolled_window_set_propagate_natural_width (GTK_SCROLLED_WINDOW (scroll), TRUE);
	gtk_scrolled_window_set_propagate_natural_height (GTK_SCROLLED_WINDOW (scroll), TRUE);
	gtk_container_add (GTK_CONTAINER (scroll), detail);
	gtk_box_pack_start (GTK_BOX (content), scroll, TRUE, TRUE, 0);

	monitor = dialog_monitor (gtk_widget_get_display (dialog));
	if (monitor != NULL) {
		GdkRectangle area;
		GdkGeometry geometry = { 0 };

		gdk_monitor_get_workarea (monitor, &area);
		nemo_delete_testguard_dialog_caps (area.width, area.height,
						   &geometry.max_width, &geometry.max_height);
		gtk_window_set_geometry_hints (GTK_WINDOW (dialog), NULL, &geometry,
					       GDK_HINT_MAX_SIZE);
	}

	gtk_widget_show_all (content);

	/* A selectable label takes the focus and selects all of itself, and
	   a stray Enter should mean Cancel. */
	gtk_widget_grab_focus (gtk_dialog_get_widget_for_response (GTK_DIALOG (dialog),
								   GTK_RESPONSE_CANCEL));

	response = gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (dialog);

	g_mutex_lock (&data->lock);
	data->go_ahead = (response == GTK_RESPONSE_OK);
	data->answered = TRUE;
	g_cond_signal (&data->done);
	g_mutex_unlock (&data->lock);

	ask_data_unref (data);

	return G_SOURCE_REMOVE;
}

/* A job runs off the main thread, so the dialog goes over there and this side
   waits for the answer. */
static gboolean
ask (const char *op, char *primary, char *detail)
{
	AskData *data;
	gboolean go_ahead;

	/* A test binary has no display and often no loop to hand a dialog to, and
	   waiting on an answer that cannot come would just hang it. */
	if (gdk_display_get_default () == NULL) {
		nemo_delete_guard_log ("test guard: %s went ahead, nothing to ask on", op);
		g_free (primary);
		g_free (detail);
		return TRUE;
	}

	data = g_new0 (AskData, 1);
	data->op = op;
	data->primary = primary;
	data->detail = detail;
	data->refs = 1;
	g_mutex_init (&data->lock);
	g_cond_init (&data->done);

	/* Owning the default context means this is the thread running the GTK
	   loop, so the dialog can just go up here. */
	if (g_main_context_is_owner (g_main_context_default ())) {
		data->refs++;
		show_dialog (data);

		g_mutex_lock (&data->lock);
		go_ahead = data->go_ahead;
		g_mutex_unlock (&data->lock);
	} else {
		gint64 deadline = g_get_monotonic_time () + ANSWER_TIMEOUT_USEC;
		NemoProgressInfo *progress = nemo_job_queue_get_current_info ();
		gboolean paused_here = FALSE;

		/* Paused, the job's progress window stays down while this waits,
		   as it does for the job's own questions. A job the user had
		   already paused stays paused after the answer. */
		if (progress != NULL) {
			paused_here = nemo_progress_info_pause (progress);
		}

		data->refs++;
		g_main_context_invoke (NULL, show_dialog, data);

		g_mutex_lock (&data->lock);
		while (!data->answered) {
			if (!g_cond_wait_until (&data->done, &data->lock, deadline)) {
				/* Nobody is iterating the loop the dialog went to, or
				   nobody is there to answer. Cancel is the default, so
				   that is what running out of time means. */
				nemo_delete_guard_log ("test guard: %s called off, no answer in %d seconds",
						       op, (int) (ANSWER_TIMEOUT_USEC / G_USEC_PER_SEC));
				break;
			}
		}
		go_ahead = data->answered && data->go_ahead;
		g_mutex_unlock (&data->lock);

		if (paused_here) {
			nemo_progress_info_resume (progress);
		}
	}

	ask_data_unref (data);

	return go_ahead;
}

gboolean
nemo_delete_testguard_ask_at (const char *op,
			      GList      *files,
			      const char *func,
			      const char *source_file,
			      int         line)
{
	gboolean go_ahead;

	if (files == NULL || !nemo_delete_testguard_armed ()) {
		return TRUE;
	}

	go_ahead = ask (op, count_line (op, g_list_length (files)),
			build_detail (files, func, source_file, line));

	nemo_delete_guard_log ("test guard: %s from %s at %s:%d, %s",
			       op, func, source_file, line,
			       go_ahead ? "went ahead" : "called off");

	return go_ahead;
}

gboolean
nemo_delete_testguard_ask_one_at (const char *op,
				  GFile      *file,
				  const char *func,
				  const char *source_file,
				  int         line)
{
	GList one = { file, NULL, NULL };

	if (already_asked ()) {
		return TRUE;
	}

	return nemo_delete_testguard_ask_at (op, &one, func, source_file, line);
}

static char *
parse_name (GFile *file)
{
	char *name = file != NULL ? g_file_get_parse_name (file) : NULL;

	return name != NULL ? name : g_strdup ("?");
}

char *
nemo_delete_testguard_describe_move (GList *files,
				     GFile *destination)
{
	char *into;
	char *paths;
	char *text;

	into = parse_name (destination);
	paths = describe_files (files);

	text = g_strdup_printf ("Into:\n  %s\n\nLeaving these locations:\n%s",
				into, paths);

	g_free (into);
	g_free (paths);

	return text;
}

char *
nemo_delete_testguard_describe_overwrite (GFile *source,
					  GFile *target)
{
	char *lost;
	char *kept;
	char *text;

	lost = parse_name (target);
	kept = parse_name (source);

	text = g_strdup_printf ("Overwritten, contents lost:\n  %s\n\nReplaced by:\n  %s\n",
				lost, kept);

	g_free (lost);
	g_free (kept);

	return text;
}

/* The two below share the delete path's dialog, but build their own middle
   section: one list of paths cannot say which side is being lost. */
static gboolean
ask_described (const char *op,
	       char       *primary,
	       char       *described,
	       const char *func,
	       const char *source_file,
	       int         line)
{
	char *stack;
	char *detail;
	gboolean go_ahead;

	stack = describe_stack ();
	detail = g_strdup_printf ("Asked from %s at %s:%d\n\n%s\nCall stack:\n%s",
				  func, source_file, line, described, stack);
	g_free (described);
	g_free (stack);

	go_ahead = ask (op, primary, detail);

	nemo_delete_guard_log ("test guard: %s from %s at %s:%d, %s",
			       op, func, source_file, line,
			       go_ahead ? "went ahead" : "called off");

	return go_ahead;
}

gboolean
nemo_delete_testguard_ask_move_at (const char *op,
				   GList      *files,
				   GFile      *destination,
				   const char *func,
				   const char *source_file,
				   int         line)
{
	if (files == NULL || !nemo_delete_testguard_armed ()) {
		return TRUE;
	}

	return ask_described (op, count_line (op, g_list_length (files)),
			      nemo_delete_testguard_describe_move (files, destination),
			      func, source_file, line);
}

gboolean
nemo_delete_testguard_ask_overwrite_at (const char *op,
					GFile      *source,
					GFile      *target,
					const char *func,
					const char *source_file,
					int         line)
{
	/* No already_asked() check on purpose. A job asks about the files it was
	   given; a target that was already sitting there is nobody's source. */
	if (target == NULL || !nemo_delete_testguard_armed ()) {
		return TRUE;
	}

	return ask_described (op, count_line (op, 1),
			      nemo_delete_testguard_describe_overwrite (source, target),
			      func, source_file, line);
}
