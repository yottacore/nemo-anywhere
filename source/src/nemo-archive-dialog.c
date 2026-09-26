/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-archive-dialog.c - ask what archive to make, then make it.

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

/* Name, format and folder are the whole dialog until the options expander is
 * opened. What is inside it depends on the format: an option no installed
 * program can honor for that format is shown grayed rather than hidden, so a
 * format that gains an option once 7z or rar is installed does not look like a
 * different dialog.
 */

#include <config.h>
#include "nemo-archive-dialog.h"

#include <glib/gi18n.h>

#include <eel/eel-stock-dialogs.h>
#include <libnemo-private/nemo-archive.h>
#include <libnemo-private/nemo-archive-commands.h>
#include <libnemo-private/nemo-config.h>
#include <libnemo-private/nemo-global-preferences.h>

typedef struct {
	GtkWidget *dialog;
	GtkWidget *name_entry;
	GtkWidget *name_label;
	GtkWidget *format_combo;
	GtkWidget *folder_button;
	GtkWidget *each_check;

	GtkWidget *level_scale;
	GtkWidget *level_label;
	GtkWidget *password_entry;
	GtkWidget *password_label;
	GtkWidget *encrypt_names_check;
	GtkWidget *split_check;
	GtkWidget *split_combo;
	GtkWidget *solid_check;
	GtkWidget *dedupe_check;
	GtkWidget *store_links_check;
	GtkWidget *follow_links_check;
	GtkWidget *recovery_check;
	GtkWidget *lock_check;
	GtkWidget *delete_check;

	GtkWidget *compress_button;
	GtkWidget *options_scroll;

	GList     *files;		/* GFile *, owned */
	gboolean   whole_folder;	/* the selection is all the folder shows */
	gboolean   name_edited;		/* the user typed, so stop rewriting it */

	/* Where the dialog was centered before the options expander changed it,
	   and the size it was then, so the move waits for the real new one. */
	int        center_x;
	int        center_y;
	int        width_before;
	int        height_before;
	int        width_now;
	int        height_now;
	gboolean   recenter_wanted;
	guint      recenter_id;
} ArchiveDialog;

/* Volume sizes people actually use, as the editable dropdown's starting list.
   Binary units, because that is what the sizes mean and what the parser has
   always computed. 4480 MiB is a DVD, 23 GiB a single-layer Blu-ray. */
static const char * const split_sizes[] = {
	"100 MiB", "700 MiB", "1 GiB", "2 GiB", "4480 MiB", "23 GiB", NULL
};

/* Big enough that most things fit in one volume, small enough to copy around. */
#define DEFAULT_SPLIT_SIZE_INDEX 3

static NemoArchiveFormat
current_format (ArchiveDialog *self)
{
	const char *id = gtk_combo_box_get_active_id (GTK_COMBO_BOX (self->format_combo));
	NemoArchiveFormat format = NEMO_ARCHIVE_FORMAT_ZIP;

	nemo_archive_format_from_id (id, &format);

	return format;
}

static void
set_row_sensitive (GtkWidget *widget,
		   gboolean   sensitive)
{
	if (widget != NULL) {
		gtk_widget_set_sensitive (widget, sensitive);
	}
}

static void
set_check (GtkWidget *widget, NemoConfigGroup *group, const char *key)
{
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget),
				      nemo_config_get_boolean (group, key));
}

static void
save_check (GtkWidget *widget, NemoConfigGroup *group, const char *key)
{
	nemo_config_set_boolean (group, key,
				 gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget)));
}

/* One archive per item, each named after its item. */
static gboolean
compressing_each (ArchiveDialog *self)
{
	return self->each_check != NULL &&
	       gtk_widget_get_sensitive (self->each_check) &&
	       gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (self->each_check));
}

/* Part of a folder gets no suggested name, so the field starts empty and there
   is nothing to compress into until it is filled in. Separate archives take
   their names from the items, so an empty field stops nothing. */
static void
update_name_validity (ArchiveDialog *self)
{
	gboolean each = compressing_each (self);

	gtk_widget_set_sensitive (self->name_entry, !each);
	set_row_sensitive (self->name_label, !each);

	if (self->compress_button != NULL) {
		gtk_widget_set_sensitive (self->compress_button,
					  each ||
					  gtk_entry_get_text_length (GTK_ENTRY (self->name_entry)) > 0);
	}
}

/* Everything that depends on the chosen format: which options that format can
   honor on this box, and the extension on the suggested name. */
static void
update_for_format (ArchiveDialog *self)
{
	NemoArchiveFormat format = current_format (self);
	NemoArchiveCaps caps = nemo_archive_format_caps (format);
	NemoArchiveOptions checkable;
	gboolean has_password;
	gboolean hidden_names;
	gboolean splitting;
	char *name;

	set_row_sensitive (self->level_scale, (caps & NEMO_ARCHIVE_CAP_LEVEL) != 0);
	set_row_sensitive (self->level_label, (caps & NEMO_ARCHIVE_CAP_LEVEL) != 0);
	set_row_sensitive (self->password_entry, (caps & NEMO_ARCHIVE_CAP_PASSWORD) != 0);
	set_row_sensitive (self->password_label, (caps & NEMO_ARCHIVE_CAP_PASSWORD) != 0);
	set_row_sensitive (self->split_check, (caps & NEMO_ARCHIVE_CAP_SPLIT) != 0);
	set_row_sensitive (self->solid_check, (caps & NEMO_ARCHIVE_CAP_SOLID) != 0);
	set_row_sensitive (self->dedupe_check, (caps & NEMO_ARCHIVE_CAP_DEDUPE) != 0);
	set_row_sensitive (self->store_links_check, (caps & NEMO_ARCHIVE_CAP_STORE_LINKS) != 0);
	set_row_sensitive (self->recovery_check, (caps & NEMO_ARCHIVE_CAP_RECOVERY) != 0);
	set_row_sensitive (self->lock_check, (caps & NEMO_ARCHIVE_CAP_LOCK) != 0);

	/* Encrypting the names is only meaningful once there is a password, and
	   the volume size only once splitting is asked for. */
	has_password = gtk_entry_get_text_length (GTK_ENTRY (self->password_entry)) > 0 &&
		       (caps & NEMO_ARCHIVE_CAP_PASSWORD) != 0;
	set_row_sensitive (self->encrypt_names_check,
			   has_password && (caps & NEMO_ARCHIVE_CAP_ENCRYPT_NAMES) != 0);

	splitting = (caps & NEMO_ARCHIVE_CAP_SPLIT) != 0 &&
		    gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (self->split_check));
	set_row_sensitive (self->split_combo, splitting);

	/* The box is only offered for an archive that can be checked afterwards,
	   and nemo_archive_can_verify is the one place that decides. */
	hidden_names = has_password &&
		       (caps & NEMO_ARCHIVE_CAP_ENCRYPT_NAMES) != 0 &&
		       gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (self->encrypt_names_check));
	nemo_archive_options_init (&checkable);
	checkable.split_size = splitting ? 1 : 0;
	checkable.encrypt_names = hidden_names;
	checkable.password = hidden_names ? g_strdup ("x") : NULL;
	set_row_sensitive (self->delete_check, nemo_archive_can_verify (&checkable));
	nemo_archive_options_clear (&checkable);

	/* Following linked folders is ours, not the format's - except that a
	   backend storing the links is not descending into them either. */
	set_row_sensitive (self->follow_links_check,
			   !gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (self->store_links_check)) ||
			   (caps & NEMO_ARCHIVE_CAP_STORE_LINKS) == 0);

	if (!self->name_edited) {
		name = nemo_archive_suggest_name (self->files, self->whole_folder, format);
	} else {
		const char *typed = gtk_entry_get_text (GTK_ENTRY (self->name_entry));

		/* An emptied field stays empty rather than becoming a bare
		   extension the user would have to delete again. */
		name = typed[0] != '\0' ? nemo_archive_apply_extension (typed, format) : NULL;
	}

	/* Setting the text counts as a change, so the guard has to be up first. */
	g_object_set_data (G_OBJECT (self->name_entry), "nemo-archive-programmatic",
			   GINT_TO_POINTER (1));
	gtk_entry_set_text (GTK_ENTRY (self->name_entry), name != NULL ? name : "");
	g_object_set_data (G_OBJECT (self->name_entry), "nemo-archive-programmatic", NULL);

	g_free (name);

	update_name_validity (self);
}

static void
format_changed (GtkComboBox *combo,
		gpointer     user_data)
{
	(void) combo;

	update_for_format (user_data);
}

static void
option_toggled (GtkToggleButton *button,
		gpointer         user_data)
{
	(void) button;

	update_for_format (user_data);
}

static void
password_changed (GtkEditable *editable,
		  gpointer     user_data)
{
	(void) editable;

	update_for_format (user_data);
}

static void
each_toggled (GtkToggleButton *button,
	      gpointer         user_data)
{
	(void) button;

	update_name_validity (user_data);
}

static void
name_changed (GtkEditable *editable,
	      gpointer     user_data)
{
	ArchiveDialog *self = user_data;

	if (g_object_get_data (G_OBJECT (editable), "nemo-archive-programmatic") == NULL) {
		self->name_edited = TRUE;
	}

	update_name_validity (self);
}

/* One labelled row in the options grid. */
static GtkWidget *
add_row (GtkWidget  *grid,
	 int         row,
	 const char *label_text,
	 GtkWidget  *widget)
{
	GtkWidget *label;

	label = gtk_label_new_with_mnemonic (label_text);
	gtk_label_set_xalign (GTK_LABEL (label), 0.0);
	gtk_label_set_mnemonic_widget (GTK_LABEL (label), widget);
	gtk_grid_attach (GTK_GRID (grid), label, 0, row, 1, 1);

	gtk_widget_set_hexpand (widget, TRUE);
	gtk_grid_attach (GTK_GRID (grid), widget, 1, row, 1, 1);

	return label;
}

static GtkWidget *
add_check (GtkWidget  *grid,
	   int         row,
	   const char *label_text,
	   gboolean    active)
{
	GtkWidget *check;

	check = gtk_check_button_new_with_mnemonic (label_text);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check), active);
	gtk_grid_attach (GTK_GRID (grid), check, 0, row, 2, 1);

	return check;
}

/* How much bigger the title bar and border make the window than its contents.
   Read as a difference, so it is the same answer whichever size is current. */
static void
decoration_size (GtkWidget *widget, int *width, int *height)
{
	GdkWindow *gdk_window = gtk_widget_get_window (widget);
	GdkRectangle frame;

	*width = 0;
	*height = 0;

	if (gdk_window == NULL) {
		return;
	}

	gdk_window_get_frame_extents (gdk_window, &frame);
	*width = MAX (0, frame.width - gdk_window_get_width (gdk_window));
	*height = MAX (0, frame.height - gdk_window_get_height (gdk_window));
}

/* Out of the size-allocate that asked for it, since moving a toplevel from
   inside one is a request the window manager may simply lose. */
static gboolean
recenter_dialog (gpointer data)
{
	ArchiveDialog *self = data;
	GdkWindow *gdk_window = gtk_widget_get_window (self->dialog);
	int deco_width, deco_height;
	int width, height;
	int x, y;

	self->recenter_id = 0;

	/* Everything here is the outer rectangle, which is what the window
	   manager places and what has to fit on the screen. */
	decoration_size (self->dialog, &deco_width, &deco_height);
	width = self->width_now + deco_width;
	height = self->height_now + deco_height;

	x = self->center_x - width / 2;
	y = self->center_y - height / 2;

	/* The work area, so a taller dialog does not end up under a panel or
	   with its buttons off the bottom of the screen. */
	if (gdk_window != NULL) {
		GdkDisplay *display = gtk_widget_get_display (self->dialog);
		GdkMonitor *monitor = gdk_display_get_monitor_at_window (display, gdk_window);

		if (monitor != NULL) {
			GdkRectangle area;

			gdk_monitor_get_workarea (monitor, &area);

			x = MIN (x, area.x + area.width - width);
			y = MIN (y, area.y + area.height - height);
			x = MAX (x, area.x);
			y = MAX (y, area.y);
		}
	}

	gtk_window_move (GTK_WINDOW (self->dialog), x, y);

	return G_SOURCE_REMOVE;
}

/* The size the expander asked for arrives here, one or more allocations after
   it was toggled. Waiting for it rather than reading the window back is the
   point: gtk_window_get_size still answers with the old one at that stage. */
static void
dialog_size_allocated (GtkWidget     *widget,
		       GdkRectangle  *allocation,
		       ArchiveDialog *self)
{
	if (!self->recenter_wanted ||
	    (allocation->width == self->width_before &&
	     allocation->height == self->height_before)) {
		return;
	}

	self->recenter_wanted = FALSE;
	self->width_now = allocation->width;
	self->height_now = allocation->height;

	if (self->recenter_id == 0) {
		self->recenter_id = g_idle_add (recenter_dialog, self);
	}
}

/* The window grows and shrinks from its top left, so opening the options walks
   the dialog down the screen and can push its buttons off the bottom. */
static void
expander_toggled (GObject       *expander,
		  GParamSpec    *pspec,
		  ArchiveDialog *self)
{
	int deco_width, deco_height;
	int x, y;

	gtk_window_get_position (GTK_WINDOW (self->dialog), &x, &y);
	gtk_window_get_size (GTK_WINDOW (self->dialog), &self->width_before,
			     &self->height_before);
	decoration_size (self->dialog, &deco_width, &deco_height);

	self->center_x = x + (self->width_before + deco_width) / 2;
	self->center_y = y + (self->height_before + deco_height) / 2;
	self->recenter_wanted = TRUE;

	/* On a short screen the options scroll rather than grow the dialog past
	   the bottom. The closed dialog is what the rest of it needs. */
	if (gtk_expander_get_expanded (GTK_EXPANDER (expander))) {
		GdkWindow *gdk_window = gtk_widget_get_window (self->dialog);
		GdkMonitor *monitor = NULL;
		GdkRectangle area;

		if (gdk_window != NULL) {
			monitor = gdk_display_get_monitor_at_window (gtk_widget_get_display (self->dialog),
								     gdk_window);
		}

		if (monitor != NULL) {
			GtkScrolledWindow *scroll = GTK_SCROLLED_WINDOW (self->options_scroll);
			int room, natural;

			gdk_monitor_get_workarea (monitor, &area);
			room = nemo_archive_options_room (area.height, self->height_before + deco_height);
			gtk_widget_get_preferred_height (gtk_bin_get_child (GTK_BIN (scroll)), NULL, &natural);

			/* A shown window only grows to its minimum, so the minimum is
			   what sets the height here. */
			gtk_scrolled_window_set_max_content_height (scroll, room);
			gtk_scrolled_window_set_min_content_height (scroll, MIN (natural, room));
		}
	}
}

static void
build_options (ArchiveDialog *self,
	       GtkWidget     *box)
{
	GtkWidget *expander;
	GtkWidget *grid;
	int row = 0;
	int i;

	expander = gtk_expander_new_with_mnemonic (_("_Options"));
	gtk_box_pack_start (GTK_BOX (box), expander, FALSE, FALSE, 0);
	g_signal_connect (expander, "notify::expanded", G_CALLBACK (expander_toggled), self);
	g_signal_connect (self->dialog, "size-allocate", G_CALLBACK (dialog_size_allocated), self);

	grid = gtk_grid_new ();
	gtk_grid_set_row_spacing (GTK_GRID (grid), 6);
	gtk_grid_set_column_spacing (GTK_GRID (grid), 12);
	gtk_widget_set_margin_top (grid, 6);
	gtk_widget_set_margin_start (grid, 12);

	self->options_scroll = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (self->options_scroll),
					GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_propagate_natural_height (GTK_SCROLLED_WINDOW (self->options_scroll), TRUE);
	gtk_scrolled_window_set_propagate_natural_width (GTK_SCROLLED_WINDOW (self->options_scroll), TRUE);
	gtk_container_add (GTK_CONTAINER (self->options_scroll), grid);
	gtk_container_add (GTK_CONTAINER (expander), self->options_scroll);

	self->level_scale = gtk_scale_new_with_range (GTK_ORIENTATION_HORIZONTAL,
						      NEMO_ARCHIVE_LEVEL_STORE,
						      NEMO_ARCHIVE_LEVEL_MAX, 1);
	gtk_scale_set_draw_value (GTK_SCALE (self->level_scale), FALSE);
	gtk_range_set_value (GTK_RANGE (self->level_scale), NEMO_ARCHIVE_LEVEL_DEFAULT);
	gtk_scale_add_mark (GTK_SCALE (self->level_scale), NEMO_ARCHIVE_LEVEL_STORE,
			    GTK_POS_BOTTOM, _("Store"));
	gtk_scale_add_mark (GTK_SCALE (self->level_scale), NEMO_ARCHIVE_LEVEL_DEFAULT,
			    GTK_POS_BOTTOM, _("Normal"));
	gtk_scale_add_mark (GTK_SCALE (self->level_scale), NEMO_ARCHIVE_LEVEL_MAX,
			    GTK_POS_BOTTOM, _("Smallest"));
	self->level_label = add_row (grid, row++, _("Co_mpression"), self->level_scale);

	self->password_entry = gtk_entry_new ();
	gtk_entry_set_visibility (GTK_ENTRY (self->password_entry), FALSE);
	gtk_entry_set_input_purpose (GTK_ENTRY (self->password_entry), GTK_INPUT_PURPOSE_PASSWORD);
	gtk_entry_set_placeholder_text (GTK_ENTRY (self->password_entry), _("None"));
	self->password_label = add_row (grid, row++, _("Pass_word"), self->password_entry);
	g_signal_connect (self->password_entry, "changed", G_CALLBACK (password_changed), self);

	self->encrypt_names_check = add_check (grid, row++, _("Encrypt the _file names too"), FALSE);
	g_signal_connect (self->encrypt_names_check, "toggled", G_CALLBACK (option_toggled), self);

	self->split_check = add_check (grid, row++, _("Split into _volumes"), FALSE);
	g_signal_connect (self->split_check, "toggled", G_CALLBACK (option_toggled), self);

	self->split_combo = gtk_combo_box_text_new_with_entry ();
	for (i = 0; split_sizes[i] != NULL; i++) {
		gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (self->split_combo), split_sizes[i]);
	}
	gtk_combo_box_set_active (GTK_COMBO_BOX (self->split_combo), DEFAULT_SPLIT_SIZE_INDEX);
	add_row (grid, row++, _("Volume si_ze"), self->split_combo);

	self->solid_check = add_check (grid, row++, _("_Solid archive"), FALSE);
	self->dedupe_check = add_check (grid, row++, _("Store _duplicate files once"), FALSE);

	self->store_links_check = add_check (grid, row++,
					     _("Store s_ymlinks and junctions as links"), TRUE);
	g_signal_connect (self->store_links_check, "toggled", G_CALLBACK (option_toggled), self);

	self->follow_links_check = add_check (grid, row++,
					      _("Follow _linked folders into the archive"), FALSE);

	self->recovery_check = add_check (grid, row++, _("Add a _recovery record"), TRUE);
	self->lock_check = add_check (grid, row++, _("Loc_k the archive against changes"), FALSE);

	self->delete_check = add_check (grid, row++,
					_("De_lete the originals once the archive checks out"), FALSE);
	gtk_widget_set_tooltip_text (self->delete_check,
				     _("The archive is read back first. Unless every file is in "
				       "there at the same size, nothing is deleted."));
}

/* What the dialog starts from next time. The password is never written
   anywhere, and the delete box has to be ticked afresh each time - neither is
   a setting so much as a decision about one archive. */
static void
restore_remembered (ArchiveDialog *self)
{
	NemoConfigGroup *group = nemo_config_get_group (NEMO_ARCHIVE_COMMANDS_GROUP);
	char *split_size;

	if (group == NULL) {
		return;
	}

	gtk_range_set_value (GTK_RANGE (self->level_scale),
			     nemo_config_get_int (group, NEMO_ARCHIVE_STATE_KEY_LEVEL));

	set_check (self->encrypt_names_check, group, NEMO_ARCHIVE_STATE_KEY_ENCRYPT_NAMES);
	set_check (self->split_check, group, NEMO_ARCHIVE_STATE_KEY_SPLIT);
	set_check (self->solid_check, group, NEMO_ARCHIVE_STATE_KEY_SOLID);
	set_check (self->dedupe_check, group, NEMO_ARCHIVE_STATE_KEY_DEDUPE);
	set_check (self->store_links_check, group, NEMO_ARCHIVE_STATE_KEY_STORE_LINKS);
	set_check (self->follow_links_check, group, NEMO_ARCHIVE_STATE_KEY_FOLLOW_LINKS);
	set_check (self->recovery_check, group, NEMO_ARCHIVE_STATE_KEY_RECOVERY);
	set_check (self->lock_check, group, NEMO_ARCHIVE_STATE_KEY_LOCK);

	/* Ticking a box nothing can act on would just be confusing, so the one
	   that depends on the selection is only put back where it applies. */
	if (gtk_widget_get_sensitive (self->each_check)) {
		set_check (self->each_check, group, NEMO_ARCHIVE_STATE_KEY_EACH);
	}

	split_size = nemo_config_get_string (group, NEMO_ARCHIVE_STATE_KEY_SPLIT_SIZE);
	if (split_size != NULL && split_size[0] != '\0') {
		GtkWidget *entry = gtk_bin_get_child (GTK_BIN (self->split_combo));

		gtk_entry_set_text (GTK_ENTRY (entry), split_size);
	}
	g_free (split_size);
}

static void
remember_settings (ArchiveDialog *self)
{
	NemoConfigGroup *group = nemo_config_get_group (NEMO_ARCHIVE_COMMANDS_GROUP);
	GtkWidget *entry;
	const char *id;

	if (group == NULL) {
		return;
	}

	id = gtk_combo_box_get_active_id (GTK_COMBO_BOX (self->format_combo));
	if (id != NULL) {
		nemo_config_set_string (group, NEMO_ARCHIVE_STATE_KEY_FORMAT, id);
	}

	nemo_config_set_int (group, NEMO_ARCHIVE_STATE_KEY_LEVEL,
			     (int) gtk_range_get_value (GTK_RANGE (self->level_scale)));

	save_check (self->encrypt_names_check, group, NEMO_ARCHIVE_STATE_KEY_ENCRYPT_NAMES);
	save_check (self->split_check, group, NEMO_ARCHIVE_STATE_KEY_SPLIT);
	save_check (self->solid_check, group, NEMO_ARCHIVE_STATE_KEY_SOLID);
	save_check (self->dedupe_check, group, NEMO_ARCHIVE_STATE_KEY_DEDUPE);
	save_check (self->store_links_check, group, NEMO_ARCHIVE_STATE_KEY_STORE_LINKS);
	save_check (self->follow_links_check, group, NEMO_ARCHIVE_STATE_KEY_FOLLOW_LINKS);
	save_check (self->recovery_check, group, NEMO_ARCHIVE_STATE_KEY_RECOVERY);
	save_check (self->lock_check, group, NEMO_ARCHIVE_STATE_KEY_LOCK);
	save_check (self->each_check, group, NEMO_ARCHIVE_STATE_KEY_EACH);

	entry = gtk_bin_get_child (GTK_BIN (self->split_combo));
	nemo_config_set_string (group, NEMO_ARCHIVE_STATE_KEY_SPLIT_SIZE,
				gtk_entry_get_text (GTK_ENTRY (entry)));
}

static void
collect_options (ArchiveDialog      *self,
		 NemoArchiveOptions *options)
{
	NemoArchiveCaps caps;
	const char *password;

	nemo_archive_options_init (options);

	options->format = current_format (self);
	caps = nemo_archive_format_caps (options->format);

	options->level = (int) gtk_range_get_value (GTK_RANGE (self->level_scale));

	password = gtk_entry_get_text (GTK_ENTRY (self->password_entry));
	if ((caps & NEMO_ARCHIVE_CAP_PASSWORD) != 0 && password != NULL && password[0] != '\0') {
		options->password = g_strdup (password);
		options->encrypt_names = (caps & NEMO_ARCHIVE_CAP_ENCRYPT_NAMES) != 0 &&
			gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (self->encrypt_names_check));
	}

	if ((caps & NEMO_ARCHIVE_CAP_SPLIT) != 0 &&
	    gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (self->split_check))) {
		GtkWidget *entry = gtk_bin_get_child (GTK_BIN (self->split_combo));
		guint64 bytes = 0;

		if (nemo_archive_parse_size (gtk_entry_get_text (GTK_ENTRY (entry)), &bytes)) {
			options->split_size = bytes;
		}
	}

	options->solid = (caps & NEMO_ARCHIVE_CAP_SOLID) != 0 &&
		gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (self->solid_check));
	options->dedupe = (caps & NEMO_ARCHIVE_CAP_DEDUPE) != 0 &&
		gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (self->dedupe_check));
	options->store_links = (caps & NEMO_ARCHIVE_CAP_STORE_LINKS) != 0 &&
		gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (self->store_links_check));
	options->follow_link_dirs =
		gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (self->follow_links_check));
	options->recovery_record = (caps & NEMO_ARCHIVE_CAP_RECOVERY) != 0 &&
		gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (self->recovery_check));
	options->lock = (caps & NEMO_ARCHIVE_CAP_LOCK) != 0 &&
		gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (self->lock_check));

	options->delete_sources = gtk_widget_get_sensitive (self->delete_check) &&
		gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (self->delete_check));
}

/* Typed a second time before the originals are deleted. See
   nemo_archive_should_confirm_password for why only that combination asks. */
static gboolean
confirm_password (ArchiveDialog *self,
		  const char    *password)
{
	GtkWidget *dialog;
	GtkWidget *box;
	GtkWidget *label;
	GtkWidget *mismatch;
	GtkWidget *entry;
	gboolean   matched = FALSE;

	dialog = gtk_dialog_new_with_buttons (_("Confirm the password"),
					      GTK_WINDOW (self->dialog),
					      GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
					      GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
					      _("C_ontinue"), GTK_RESPONSE_OK,
					      NULL);
	gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);

	box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
	gtk_widget_set_margin_start (box, 12);
	gtk_widget_set_margin_end (box, 12);
	gtk_widget_set_margin_top (box, 12);
	gtk_widget_set_margin_bottom (box, 12);
	gtk_box_pack_start (GTK_BOX (gtk_dialog_get_content_area (GTK_DIALOG (dialog))),
			    box, TRUE, TRUE, 0);

	label = gtk_label_new (_("The originals go to the trash once the archive checks out, and a "
				 "password with a typo in it still writes an archive that checks "
				 "out. Type it again."));
	gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
	gtk_label_set_max_width_chars (GTK_LABEL (label), 48);
	gtk_label_set_xalign (GTK_LABEL (label), 0.0);
	gtk_box_pack_start (GTK_BOX (box), label, FALSE, FALSE, 0);

	entry = gtk_entry_new ();
	gtk_entry_set_visibility (GTK_ENTRY (entry), FALSE);
	gtk_entry_set_input_purpose (GTK_ENTRY (entry), GTK_INPUT_PURPOSE_PASSWORD);
	gtk_entry_set_activates_default (GTK_ENTRY (entry), TRUE);
	gtk_box_pack_start (GTK_BOX (box), entry, FALSE, FALSE, 0);

	mismatch = gtk_label_new (_("That is not the same password."));
	gtk_label_set_xalign (GTK_LABEL (mismatch), 0.0);
	gtk_box_pack_start (GTK_BOX (box), mismatch, FALSE, FALSE, 0);

	gtk_widget_show_all (dialog);
	gtk_widget_hide (mismatch);
	gtk_widget_grab_focus (entry);

	while (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_OK) {
		if (g_strcmp0 (gtk_entry_get_text (GTK_ENTRY (entry)), password) == 0) {
			matched = TRUE;
			break;
		}

		gtk_entry_set_text (GTK_ENTRY (entry), "");
		gtk_widget_show (mismatch);
		gtk_widget_grab_focus (entry);
	}

	gtk_widget_destroy (dialog);

	return matched;
}

/* Asked once for the lot: with a selection compressed separately there can be
   any number of them, and answering one at a time before anything has started
   is worse than seeing the count. */
static gboolean
confirm_overwrite (ArchiveDialog *self,
		   GList         *destinations)
{
	GList *existing = NULL;
	GList *l;
	guint count;
	char *primary;
	int response;

	for (l = destinations; l != NULL; l = l->next) {
		if (g_file_query_exists (G_FILE (l->data), NULL)) {
			existing = g_list_prepend (existing, l->data);
		}
	}

	if (existing == NULL) {
		return TRUE;
	}

	count = g_list_length (existing);

	if (count == 1) {
		char *name = g_file_get_basename (G_FILE (existing->data));

		primary = g_strdup_printf (_("A file named \"%s\" already exists. Replace it?"), name);
		g_free (name);
	} else {
		primary = g_strdup_printf (_("%d of the archives already exist. Replace them?"), count);
	}

	response = eel_run_simple_dialog (self->dialog, TRUE, GTK_MESSAGE_QUESTION,
					  primary,
					  ngettext ("Replacing it overwrites its contents.",
						    "Replacing them overwrites their contents.",
						    count),
					  GTK_STOCK_CANCEL, _("_Replace"), NULL);

	g_free (primary);
	g_list_free (existing);

	return response == 1;
}

/* The archives separate mode would write. Worked out here as well as in the
   job so that what is already there can be asked about before it starts. */
static GList *
each_destinations (ArchiveDialog     *self,
		   GFile             *folder,
		   NemoArchiveFormat  format)
{
	GList *destinations = NULL;
	GList *l;

	for (l = self->files; l != NULL; l = l->next) {
		GFile *destination;
		char *basename;
		char *name;

		basename = g_file_get_basename (G_FILE (l->data));
		name = nemo_archive_each_name (basename, format);
		g_free (basename);

		if (name == NULL) {
			continue;
		}

		destination = g_file_get_child_for_display_name (folder, name, NULL);
		if (destination == NULL) {
			destination = g_file_get_child (folder, name);
		}
		g_free (name);

		destinations = g_list_prepend (destinations, destination);
	}

	return g_list_reverse (destinations);
}

static void
dialog_free (ArchiveDialog *self)
{
	if (self->recenter_id != 0) {
		g_source_remove (self->recenter_id);
	}

	g_list_free_full (self->files, g_object_unref);
	g_free (self);
}

void
nemo_archive_dialog_show (GtkWindow *parent_window,
			  GList     *files,
			  GFile     *default_dir,
			  gboolean   whole_folder)
{
	ArchiveDialog *self;
	GtkWidget *content;
	GtkWidget *box;
	GtkWidget *grid;
	GtkWidget *compress_button;
	GList *l;
	int i;
	int row = 0;
	gboolean any_format = FALSE;

	g_return_if_fail (files != NULL);

	self = g_new0 (ArchiveDialog, 1);

	for (l = files; l != NULL; l = l->next) {
		self->files = g_list_prepend (self->files, g_object_ref (G_FILE (l->data)));
	}
	self->files = g_list_reverse (self->files);
	self->whole_folder = whole_folder;

	self->dialog = gtk_dialog_new_with_buttons (_("Compress"), parent_window,
						    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
						    GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
						    _("C_ompress"), GTK_RESPONSE_OK,
						    NULL);
	gtk_dialog_set_default_response (GTK_DIALOG (self->dialog), GTK_RESPONSE_OK);
	compress_button = gtk_dialog_get_widget_for_response (GTK_DIALOG (self->dialog), GTK_RESPONSE_OK);
	gtk_widget_set_can_default (compress_button, TRUE);

	content = gtk_dialog_get_content_area (GTK_DIALOG (self->dialog));
	box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
	gtk_widget_set_margin_start (box, 12);
	gtk_widget_set_margin_end (box, 12);
	gtk_widget_set_margin_top (box, 12);
	gtk_widget_set_margin_bottom (box, 12);
	gtk_box_pack_start (GTK_BOX (content), box, TRUE, TRUE, 0);

	grid = gtk_grid_new ();
	gtk_grid_set_row_spacing (GTK_GRID (grid), 6);
	gtk_grid_set_column_spacing (GTK_GRID (grid), 12);
	gtk_box_pack_start (GTK_BOX (box), grid, FALSE, FALSE, 0);

	self->name_entry = gtk_entry_new ();
	gtk_entry_set_activates_default (GTK_ENTRY (self->name_entry), TRUE);
	gtk_entry_set_width_chars (GTK_ENTRY (self->name_entry), 36);
	self->name_label = add_row (grid, row++, _("_Name"), self->name_entry);

	self->format_combo = gtk_combo_box_text_new ();
	for (i = 0; i < NEMO_ARCHIVE_N_FORMATS; i++) {
		/* A format nothing here can write is left out entirely - it
		   would only be a dead entry. */
		if (!nemo_archive_format_available ((NemoArchiveFormat) i)) {
			continue;
		}
		gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (self->format_combo),
					   nemo_archive_format_id ((NemoArchiveFormat) i),
					   nemo_archive_format_name ((NemoArchiveFormat) i));
		any_format = TRUE;
	}
	add_row (grid, row++, _("_Format"), self->format_combo);

	self->folder_button = gtk_file_chooser_button_new (_("Where to put it"),
							   GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER);
	if (default_dir != NULL) {
		gtk_file_chooser_set_current_folder_file (GTK_FILE_CHOOSER (self->folder_button),
							  default_dir, NULL);
	}
	add_row (grid, row++, _("_Where"), self->folder_button);

	/* Grayed rather than hidden with one item selected: it is the same
	   dialog either way, and there is nothing to explain about why it is
	   not offered. */
	self->each_check = add_check (grid, row++, _("Compress each item se_parately"), FALSE);
	gtk_widget_set_sensitive (self->each_check, files->next != NULL);
	gtk_widget_set_tooltip_text (self->each_check,
				     _("Each item becomes its own archive, named after it."));
	g_signal_connect (self->each_check, "toggled", G_CALLBACK (each_toggled), self);

	build_options (self, box);

	g_signal_connect (self->format_combo, "changed", G_CALLBACK (format_changed), self);
	g_signal_connect (self->name_entry, "changed", G_CALLBACK (name_changed), self);

	if (!any_format) {
		/* Cannot happen with libarchive linked in, but a combo with no
		   rows would silently make the button do nothing. */
		gtk_widget_set_sensitive (compress_button, FALSE);
	} else {
		NemoConfigGroup *group = nemo_config_get_group (NEMO_ARCHIVE_COMMANDS_GROUP);
		char *format_id = NULL;

		self->compress_button = compress_button;

		/* Everything but the format first, then the format, because
		   setting it is what works out which of them apply. A format
		   that is no longer available falls back to the first one. */
		restore_remembered (self);

		if (group != NULL) {
			format_id = nemo_config_get_string (group, NEMO_ARCHIVE_STATE_KEY_FORMAT);
		}
		if (format_id == NULL ||
		    !gtk_combo_box_set_active_id (GTK_COMBO_BOX (self->format_combo), format_id)) {
			gtk_combo_box_set_active (GTK_COMBO_BOX (self->format_combo), 0);
		}
		g_free (format_id);
	}

	gtk_widget_show_all (self->dialog);
	gtk_widget_grab_focus (self->name_entry);

	/* Looping rather than a one-shot run: an empty name or a refused
	   overwrite should put the dialog back, not throw the settings away. */
	while (TRUE) {
		NemoArchiveOptions options;
		GFile *folder;
		GList *destinations;
		gboolean each;
		const char *name;

		if (gtk_dialog_run (GTK_DIALOG (self->dialog)) != GTK_RESPONSE_OK) {
			break;
		}

		each = compressing_each (self);

		name = gtk_entry_get_text (GTK_ENTRY (self->name_entry));
		if (!each && (name == NULL || name[0] == '\0')) {
			gtk_widget_grab_focus (self->name_entry);
			continue;
		}

		folder = gtk_file_chooser_get_file (GTK_FILE_CHOOSER (self->folder_button));
		if (folder == NULL) {
			continue;
		}

		if (each) {
			destinations = each_destinations (self, folder, current_format (self));
		} else {
			GFile *destination = g_file_get_child_for_display_name (folder, name, NULL);

			if (destination == NULL) {
				destination = g_file_get_child (folder, name);
			}
			destinations = g_list_append (NULL, destination);
		}

		if (destinations == NULL || !confirm_overwrite (self, destinations)) {
			g_list_free_full (destinations, g_object_unref);
			g_object_unref (folder);
			continue;
		}

		collect_options (self, &options);

		if (nemo_archive_should_confirm_password (&options) &&
		    !confirm_password (self, options.password)) {
			nemo_archive_options_clear (&options);
			g_list_free_full (destinations, g_object_unref);
			g_object_unref (folder);
			continue;
		}

		remember_settings (self);

		gtk_widget_hide (self->dialog);

		if (each) {
			nemo_archive_create_each (self->files, folder, &options,
						  parent_window, NULL, NULL);
		} else {
			nemo_archive_create (self->files, G_FILE (destinations->data), &options,
					     parent_window, NULL, NULL);
		}

		nemo_archive_options_clear (&options);
		g_list_free_full (destinations, g_object_unref);
		g_object_unref (folder);
		break;
	}

	gtk_widget_destroy (self->dialog);
	dialog_free (self);
}
