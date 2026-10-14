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
#include <libnemo-private/nemo-archive-host.h>
#include <libnemo-private/nemo-config.h>
#include <libnemo-private/nemo-file-utilities.h>
#include <libnemo-private/nemo-global-preferences.h>
#include <libnemo-private/nemo-share.h>

#include "arc-scan.h"

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
	/* Indexed by ArcLinkChoice. Junctions is Windows only and Follow nested
	   filesystems Linux only, so one of them is NULL. The 4 Follow controls
	   show their size changes. */
	GtkWidget *symlinks[ARC_LINK_STORE_SYMLINK + 1];
	GtkWidget *junctions[ARC_LINK_STORE_JUNCTION + 1];
	GtkWidget *nested_check;
	GtkWidget *other_check;
	GtkWidget *recovery_check;
	GtkWidget *lock_check;
	GtkWidget *delete_check;

	GtkWidget *compress_button;
	GtkWidget *expander;
	GtkWidget *reset_button;
	GtkWidget *options_scroll;

	/* The choices under Options as chosen. What the controls show is what
	   the format can do with them, so a box it can't do shows off, a store
	   it can't do shows as Ignore, and either comes back with a format that
	   can. The password and the delete box are never remembered. */
	NemoArchiveChoices chosen;
	char      *password;
	gboolean   delete_sources;
	gboolean   showing;		/* set while the controls are made to match */

	/* Counts what the options as shown would put in, which is what the job
	   would do with this format. Freed on Cancel, on Compress, and with the
	   window, so no report comes after. */
	ArcScan   *scan;
	guint      follow;		/* ArcFollow bits as shown */
	guint      scan_follow;		/* the same, as last handed to the scan */
	GtkWidget *total_label;
	gboolean   destroyed;

	GList     *files;		/* GFile *, owned */
	gboolean   whole_folder;	/* the selection is all the folder shows */
	gboolean   links_or_folders;	/* something in it a link choice applies to */
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

static GtkWidget *
split_entry (ArchiveDialog *self)
{
	return gtk_bin_get_child (GTK_BIN (self->split_combo));
}

/* Splitting is a requirement, so a size that can't be read stops Compress
   rather than writing one file. */
static gboolean
size_unreadable (ArchiveDialog *self)
{
	return gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (self->split_check)) &&
	       !nemo_archive_parse_size (gtk_entry_get_text (GTK_ENTRY (split_entry (self))), NULL);
}

/* Part of a folder gets no suggested name, so the field starts empty and there
   is nothing to compress into until it is filled in. Separate archives take
   their names from the items, so an empty field stops nothing. */
static void
update_validity (ArchiveDialog *self)
{
	gboolean each = compressing_each (self);
	gboolean bad_size = size_unreadable (self);
	GtkStyleContext *style = gtk_widget_get_style_context (split_entry (self));

	gtk_widget_set_sensitive (self->name_entry, !each);
	set_row_sensitive (self->name_label, !each);

	if (bad_size) {
		gtk_style_context_add_class (style, GTK_STYLE_CLASS_ERROR);
		gtk_widget_set_tooltip_text (split_entry (self),
					     _("Not a size that can be read. Try one like 700 MiB or 4.5 GiB."));
	} else {
		gtk_style_context_remove_class (style, GTK_STYLE_CLASS_ERROR);
		gtk_widget_set_tooltip_text (split_entry (self), NULL);
	}

	if (self->compress_button != NULL) {
		gtk_widget_set_sensitive (self->compress_button,
					  !bad_size &&
					  (each || gtk_entry_get_text_length (GTK_ENTRY (self->name_entry)) > 0));
	}
}

static void
show_check (GtkWidget *check,
	    gboolean   on,
	    gboolean   can)
{
	if (check != NULL) {
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check), on && can);
		gtk_widget_set_sensitive (check, can);
	}
}

static void
set_store_tip (GtkWidget                *button,
	       gboolean                  can_store,
	       NemoArchiveFormat         format,
	       const NemoArchiveOptions *wanted)
{
	const char *tip;

	if (!can_store) {
		tip = _("Nothing installed can keep links in this archive with these options.");
	} else if (nemo_archive_links_cost_threads (format, wanted)) {
		tip = _("Puts each link in as a link, not what it leads to. For a 7z here that "
			"takes the built-in writer, which compresses on one thread.");
	} else {
		tip = _("Puts each link in as a link, not what it leads to.");
	}

	gtk_widget_set_tooltip_text (button, tip);
}

/* The buttons show the choices as this format and these options can do them.
   Called with self->showing set. */
static void
show_link_choices (ArchiveDialog            *self,
		   NemoArchiveFormat         format,
		   const NemoArchiveOptions *wanted)
{
	guint stores = nemo_archive_options_link_stores (format, wanted);
	gboolean can_store;

	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (
		self->symlinks[arc_link_options_symlinks (&self->chosen.links, stores)]), TRUE);
	can_store = (stores & ARC_STORES_SYMLINKS) != 0;
	gtk_widget_set_sensitive (self->symlinks[ARC_LINK_STORE_SYMLINK], can_store);
	set_store_tip (self->symlinks[ARC_LINK_STORE_SYMLINK], can_store, format, wanted);

	if (self->junctions[ARC_LINK_IGNORE] != NULL) {
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (
			self->junctions[arc_link_options_junctions (&self->chosen.links, stores)]), TRUE);
		can_store = (stores & ARC_STORES_JUNCTIONS) != 0;
		gtk_widget_set_sensitive (self->junctions[ARC_LINK_STORE_JUNCTION], can_store);
		gtk_widget_set_tooltip_text (self->junctions[ARC_LINK_STORE_JUNCTION], can_store
			? _("Puts each junction in as a junction, not what it leads to.")
			: _("Nothing installed can keep links in this archive with these options."));
		can_store = (stores & ARC_STORES_JUNCTIONS_AS_SYMLINKS) != 0;
		gtk_widget_set_sensitive (self->junctions[ARC_LINK_STORE_SYMLINK], can_store);
		set_store_tip (self->junctions[ARC_LINK_STORE_SYMLINK], can_store, format, wanted);
	}

	/* Only a folder or a link has anything mounted below it. */
	show_check (self->nested_check, self->chosen.links.follow_nested, self->links_or_folders);
	show_check (self->other_check, self->chosen.links.follow_other, self->links_or_folders);

	/* Plain files have nothing to follow, and the groups are hidden. */
	self->follow = self->links_or_folders ? arc_link_options_follow (&self->chosen.links, stores) : 0;
}

static gboolean
has_password (ArchiveDialog *self, NemoArchiveCaps caps)
{
	return (caps & NEMO_ARCHIVE_CAP_PASSWORD) != 0 &&
	       self->password != NULL && self->password[0] != '\0';
}

/* Reset has nothing to do once everything is at its default. */
static gboolean
all_at_default (ArchiveDialog *self)
{
	return nemo_archive_choices_at_default (&self->chosen) &&
	       (self->password == NULL || self->password[0] == '\0') &&
	       !self->delete_sources;
}

/* Everything that depends on the chosen format: which options that format can
   honor on this box, and the extension on the suggested name. */
static void
update_for_format (ArchiveDialog *self)
{
	NemoArchiveFormat format = current_format (self);
	NemoArchiveCaps caps = nemo_archive_format_caps (format);
	NemoArchiveChoices shown = { 0 };
	NemoArchiveOptions checkable;
	const char *password_shown;
	gboolean can_delete;
	char *name;

	nemo_archive_choices_shown (&self->chosen, caps, has_password (self, caps), &shown);
	self->showing = TRUE;

	set_row_sensitive (self->level_scale, (caps & NEMO_ARCHIVE_CAP_LEVEL) != 0);
	set_row_sensitive (self->level_label, (caps & NEMO_ARCHIVE_CAP_LEVEL) != 0);

	/* A password the format can't take shows as none, like a box. */
	set_row_sensitive (self->password_entry, (caps & NEMO_ARCHIVE_CAP_PASSWORD) != 0);
	set_row_sensitive (self->password_label, (caps & NEMO_ARCHIVE_CAP_PASSWORD) != 0);
	password_shown = (caps & NEMO_ARCHIVE_CAP_PASSWORD) != 0 && self->password != NULL
		? self->password : "";
	if (g_strcmp0 (gtk_entry_get_text (GTK_ENTRY (self->password_entry)), password_shown) != 0) {
		gtk_entry_set_text (GTK_ENTRY (self->password_entry), password_shown);
	}

	/* Encrypting the names is only meaningful once there is a password, and
	   the volume size only once splitting is asked for. */
	show_check (self->encrypt_names_check, shown.encrypt_names,
		    has_password (self, caps) && (caps & NEMO_ARCHIVE_CAP_ENCRYPT_NAMES) != 0);
	show_check (self->split_check, shown.split, (caps & NEMO_ARCHIVE_CAP_SPLIT) != 0);
	set_row_sensitive (self->split_combo, shown.split);
	show_check (self->solid_check, shown.solid, (caps & NEMO_ARCHIVE_CAP_SOLID) != 0);
	show_check (self->dedupe_check, shown.dedupe, (caps & NEMO_ARCHIVE_CAP_DEDUPE) != 0);
	show_check (self->recovery_check, shown.recovery, (caps & NEMO_ARCHIVE_CAP_RECOVERY) != 0);
	show_check (self->lock_check, shown.lock, (caps & NEMO_ARCHIVE_CAP_LOCK) != 0);

	/* The box is only offered for an archive that can be checked afterwards,
	   and nemo_archive_can_verify is the one place that decides. Plain files
	   have no links to keep, whatever the choice. */
	nemo_archive_options_init (&checkable);
	checkable.format = format;
	checkable.split_size = shown.split ? 1 : 0;
	checkable.encrypt_names = shown.encrypt_names;
	checkable.password = has_password (self, caps) ? g_strdup ("x") : NULL;
	if (self->links_or_folders) {
		checkable.links = self->chosen.links;
	}
	can_delete = nemo_archive_can_verify (&checkable);
	show_check (self->delete_check, self->delete_sources, can_delete);

	/* The store choices go by the writers that can also do the password and
	   volumes asked for. */
	show_link_choices (self, format, &checkable);
	nemo_archive_options_clear (&checkable);
	nemo_archive_choices_clear (&shown);

	if (self->reset_button != NULL) {
		gtk_widget_set_sensitive (self->reset_button, !all_at_default (self));
	}

	self->showing = FALSE;

	/* Every change comes through here, a format change included, so the
	   scan goes by what shows. */
	if (self->scan != NULL && self->follow != self->scan_follow) {
		self->scan_follow = self->follow;
		arc_scan_set_follow (self->scan, self->follow);
	}

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

	update_validity (self);
}

static void
format_changed (GtkComboBox *combo,
		gpointer     user_data)
{
	(void) combo;

	update_for_format (user_data);
}

/* Only a click is a choice. Making the controls match a format is not. Each
   box has the choice it stands for. */
static void
choice_toggled (GtkToggleButton *button,
		ArchiveDialog   *self)
{
	gboolean *choice = g_object_get_data (G_OBJECT (button), "nemo-choice");

	if (self->showing) {
		return;
	}

	*choice = gtk_toggle_button_get_active (button);
	update_for_format (self);
}

static void
link_choice_toggled (GtkToggleButton *button,
		     ArchiveDialog   *self)
{
	ArcLinkChoice choice;

	if (self->showing || !gtk_toggle_button_get_active (button)) {
		return;
	}

	choice = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (button), "nemo-link-choice"));
	if (GTK_WIDGET (button) == self->symlinks[choice]) {
		arc_link_options_set_symlinks (&self->chosen.links, choice);
	} else {
		arc_link_options_set_junctions (&self->chosen.links, choice);
	}

	update_for_format (self);
}

static void
level_changed (GtkRange      *range,
	       ArchiveDialog *self)
{
	if (!self->showing) {
		self->chosen.level = (int) gtk_range_get_value (range);
		update_for_format (self);
	}
}

static void
split_size_changed (GtkComboBox   *combo,
		    ArchiveDialog *self)
{
	if (!self->showing) {
		g_free (self->chosen.split_size);
		self->chosen.split_size = g_strdup (gtk_entry_get_text (GTK_ENTRY (
			gtk_bin_get_child (GTK_BIN (combo)))));
		update_for_format (self);
	}
}

/* The group's access key goes to the button that's on, and moves nothing.
   Arrows change the choice from there, as in any radio group. */
static gboolean
link_group_mnemonic (GtkWidget *button,
		     gboolean   group_cycling,
		     gpointer   user_data)
{
	GSList *l;

	(void) group_cycling;
	(void) user_data;

	for (l = gtk_radio_button_get_group (GTK_RADIO_BUTTON (button)); l != NULL; l = l->next) {
		if (gtk_toggle_button_get_active (l->data)) {
			button = l->data;
		}
	}
	gtk_widget_grab_focus (button);

	return TRUE;
}

static void
password_changed (GtkEditable   *editable,
		  ArchiveDialog *self)
{
	if (!self->showing) {
		g_free (self->password);
		self->password = g_strdup (gtk_entry_get_text (GTK_ENTRY (editable)));
		update_for_format (self);
	}
}

static void
each_toggled (GtkToggleButton *button,
	      gpointer         user_data)
{
	(void) button;

	update_validity (user_data);
}

static void
name_changed (GtkEditable *editable,
	      gpointer     user_data)
{
	ArchiveDialog *self = user_data;

	if (g_object_get_data (G_OBJECT (editable), "nemo-archive-programmatic") == NULL) {
		self->name_edited = TRUE;
	}

	update_validity (self);
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

/* A box that stands for one of the choices. */
static GtkWidget *
add_choice (ArchiveDialog *self,
	    GtkWidget     *grid,
	    int            row,
	    const char    *label_text,
	    gboolean      *choice)
{
	GtkWidget *check = add_check (grid, row, label_text, *choice);

	g_object_set_data (G_OBJECT (check), "nemo-choice", choice);
	g_signal_connect (check, "toggled", G_CALLBACK (choice_toggled), self);

	return check;
}

/* The size change goes after the control's own text. */
static void
keep_follow_text (GtkWidget *button)
{
	g_object_set_data_full (G_OBJECT (button), "nemo-follow-text",
				g_strdup (gtk_button_get_label (GTK_BUTTON (button))), g_free);
}

typedef struct {
	ArcLinkChoice choice;
	const char   *label;
	const char   *name;
} LinkButton;

/* A labelled radio group, shown only when the selection has a link or a
   folder for it to apply to. */
static void
add_link_group (ArchiveDialog    *self,
		GtkWidget        *grid,
		int               row,
		const char       *name,
		const char       *label_text,
		const LinkButton *buttons,
		guint             count,
		GtkWidget       **slots)
{
	GtkWidget *box;
	GtkWidget *label;
	GSList *group = NULL;
	guint i;

	box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 2);

	for (i = 0; i < count; i++) {
		GtkWidget *button = gtk_radio_button_new_with_label (group, _(buttons[i].label));

		group = gtk_radio_button_get_group (GTK_RADIO_BUTTON (button));
		gtk_widget_set_name (button, buttons[i].name);
		g_object_set_data (G_OBJECT (button), "nemo-link-choice",
				   GINT_TO_POINTER (buttons[i].choice));
		if (buttons[i].choice == ARC_LINK_FOLLOW) {
			gtk_widget_set_tooltip_text (button,
				_("Puts in what each one leads to. A shortcut (.lnk) is a "
				  "file, so it always goes in as itself."));
			keep_follow_text (button);
		}
		g_signal_connect (button, "toggled", G_CALLBACK (link_choice_toggled), self);
		g_signal_connect (button, "mnemonic-activate", G_CALLBACK (link_group_mnemonic), NULL);
		gtk_box_pack_start (GTK_BOX (box), button, FALSE, FALSE, 0);
		slots[buttons[i].choice] = button;
	}

	label = add_row (grid, row, label_text, box);
	gtk_widget_set_name (label, name);
	gtk_label_set_mnemonic_widget (GTK_LABEL (label), slots[ARC_LINK_IGNORE]);
	gtk_widget_set_valign (label, GTK_ALIGN_START);

	/* show_all on the dialog would bring them back. */
	gtk_widget_show_all (box);
	gtk_widget_show (label);
	gtk_widget_set_no_show_all (box, TRUE);
	gtk_widget_set_no_show_all (label, TRUE);
	gtk_widget_set_visible (box, self->links_or_folders);
	gtk_widget_set_visible (label, self->links_or_folders);
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
dialog_size_allocated (G_GNUC_UNUSED GtkWidget     *widget,
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

int
nemo_archive_options_room (int work_height,
			   int closed_height)
{
	return MAX (120, work_height - closed_height);
}

/* The window grows and shrinks from its top left, so opening the options walks
   the dialog down the screen and can push its buttons off the bottom. */
static void
expander_toggled (GObject       *expander,
		  G_GNUC_UNUSED GParamSpec    *pspec,
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

/* The level and the size are the 2 that aren't boxes, so they're set here
   rather than by update_for_format. */
static void
show_level_and_size (ArchiveDialog *self)
{
	int preset = nemo_archive_volume_preset_find (self->chosen.split_size);

	self->showing = TRUE;
	gtk_range_set_value (GTK_RANGE (self->level_scale), self->chosen.level);
	if (preset >= 0) {
		gtk_combo_box_set_active (GTK_COMBO_BOX (self->split_combo), preset);
	} else {
		gtk_entry_set_text (GTK_ENTRY (split_entry (self)),
				    self->chosen.split_size != NULL ? self->chosen.split_size : "");
	}
	self->showing = FALSE;
}

/* Every choice under Options back to its default, the remembered ones
   dropped from the settings file, and Options closed. */
static void
reset_clicked (GtkButton     *button,
	       ArchiveDialog *self)
{
	(void) button;

	nemo_archive_choices_clear (&self->chosen);
	nemo_archive_choices_init (&self->chosen);
	g_clear_pointer (&self->password, g_free);
	self->delete_sources = FALSE;
	nemo_archive_choices_forget ();

	show_level_and_size (self);
	update_for_format (self);

	/* The button grays out under the focus. */
	gtk_widget_grab_focus (self->expander);
	gtk_expander_set_expanded (GTK_EXPANDER (self->expander), FALSE);
}

/* Once, when the dialog is first on screen, so opening goes the same way as
   a click: the options scroll on a short screen and the dialog keeps its
   place. */
static gboolean
open_remembered (GtkWidget     *widget,
		 GdkEvent      *event,
		 ArchiveDialog *self)
{
	(void) event;

	g_signal_handlers_disconnect_by_func (widget, open_remembered, self);
	gtk_expander_set_expanded (GTK_EXPANDER (self->expander), TRUE);

	return FALSE;
}

static void
build_options (ArchiveDialog *self,
	       GtkWidget     *box)
{
	static const LinkButton symlink_buttons[] = {
		{ ARC_LINK_IGNORE,        N_("Ignore"),            "symlinks-ignore" },
		{ ARC_LINK_FOLLOW,        N_("Follow"),            "symlinks-follow" },
		{ ARC_LINK_STORE_SYMLINK, N_("Store as symlinks"), "symlinks-store" },
	};
#ifdef G_OS_WIN32
	static const LinkButton junction_buttons[] = {
		{ ARC_LINK_IGNORE,         N_("Ignore"),             "junctions-ignore" },
		{ ARC_LINK_FOLLOW,         N_("Follow"),             "junctions-follow" },
		{ ARC_LINK_STORE_JUNCTION, N_("Store as junctions"), "junctions-store-junction" },
		{ ARC_LINK_STORE_SYMLINK,  N_("Store as symlinks"),  "junctions-store-symlink" },
	};
#endif
	GtkWidget *expander;
	GtkWidget *header;
	GtkWidget *grid;
	const char *preset;
	int row = 0;
	guint i;

	/* Reset sits beside Options rather than in its title, where a click
	   would only open and close it. */
	header = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_box_pack_start (GTK_BOX (box), header, FALSE, FALSE, 0);

	expander = gtk_expander_new_with_mnemonic (_("_Options"));
	self->expander = expander;
	gtk_box_pack_start (GTK_BOX (header), expander, TRUE, TRUE, 0);
	g_signal_connect (expander, "notify::expanded", G_CALLBACK (expander_toggled), self);

	self->reset_button = gtk_button_new_with_mnemonic (_("R_eset"));
	gtk_widget_set_name (self->reset_button, "reset-options");
	gtk_button_set_relief (GTK_BUTTON (self->reset_button), GTK_RELIEF_NONE);
	gtk_widget_set_valign (self->reset_button, GTK_ALIGN_START);
	gtk_widget_set_tooltip_text (self->reset_button,
				     _("Puts every option back to its default, and forgets the ones "
				       "remembered from last time."));
	gtk_box_pack_end (GTK_BOX (header), self->reset_button, FALSE, FALSE, 0);
	g_signal_connect (self->reset_button, "clicked", G_CALLBACK (reset_clicked), self);
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
	g_signal_connect (self->level_scale, "value-changed", G_CALLBACK (level_changed), self);

	self->password_entry = gtk_entry_new ();
	gtk_entry_set_visibility (GTK_ENTRY (self->password_entry), FALSE);
	gtk_entry_set_input_purpose (GTK_ENTRY (self->password_entry), GTK_INPUT_PURPOSE_PASSWORD);
	gtk_entry_set_placeholder_text (GTK_ENTRY (self->password_entry), _("None"));
	gtk_widget_set_name (self->password_entry, "password");
	self->password_label = add_row (grid, row++, _("Pass_word"), self->password_entry);
	g_signal_connect (self->password_entry, "changed", G_CALLBACK (password_changed), self);

	self->encrypt_names_check = add_choice (self, grid, row++, _("Encrypt the _file names too"),
						&self->chosen.encrypt_names);

	self->split_check = add_choice (self, grid, row++, _("Split into _volumes"), &self->chosen.split);
	gtk_widget_set_name (self->split_check, "split");

	self->split_combo = gtk_combo_box_text_new_with_entry ();
	gtk_widget_set_name (self->split_combo, "volume-size");
	for (i = 0; (preset = nemo_archive_volume_preset (i, NULL)) != NULL; i++) {
		gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (self->split_combo), preset);
	}
	gtk_combo_box_set_active (GTK_COMBO_BOX (self->split_combo),
				  nemo_archive_volume_preset_find (NEMO_ARCHIVE_STATE_DEFAULT_SPLIT_SIZE));
	add_row (grid, row++, _("Volume si_ze"), self->split_combo);
	g_signal_connect (self->split_combo, "changed", G_CALLBACK (split_size_changed), self);

	self->solid_check = add_choice (self, grid, row++, _("_Solid archive"), &self->chosen.solid);
	gtk_widget_set_name (self->solid_check, "solid");
	self->dedupe_check = add_choice (self, grid, row++, _("Store _duplicate files once"),
					 &self->chosen.dedupe);

	add_link_group (self, grid, row++, "symlinks", _("S_ymlinks"),
			symlink_buttons, G_N_ELEMENTS (symlink_buttons), self->symlinks);
#ifdef G_OS_WIN32
	add_link_group (self, grid, row++, "junctions", _("_Junctions"),
			junction_buttons, G_N_ELEMENTS (junction_buttons), self->junctions);
#else
	/* Windows has no nested kind: a volume mounted in a folder is another
	   filesystem there. */
	self->nested_check = add_choice (self, grid, row++, _("Follow nes_ted filesystems"),
					 &self->chosen.links.follow_nested);
	gtk_widget_set_name (self->nested_check, "follow-nested");
	keep_follow_text (self->nested_check);
	gtk_widget_set_tooltip_text (self->nested_check,
				     _("Goes into a filesystem mounted from the same pool or disk, "
				       "such as a ZFS dataset or a Btrfs subvolume."));
#endif
	self->other_check = add_choice (self, grid, row++, _("Follow ot_her filesystems"),
					&self->chosen.links.follow_other);
	gtk_widget_set_name (self->other_check, "follow-other");
	keep_follow_text (self->other_check);
	gtk_widget_set_tooltip_text (self->other_check,
				     _("Goes into another drive or a share mounted below, or one a "
				       "link leads to. Off each time this opens."));

	self->recovery_check = add_choice (self, grid, row++, _("Add a _recovery record"),
					   &self->chosen.recovery);
	gtk_widget_set_name (self->recovery_check, "recovery");
	self->lock_check = add_choice (self, grid, row++, _("Loc_k the archive against changes"),
				       &self->chosen.lock);
	gtk_widget_set_name (self->lock_check, "lock");

	self->delete_check = add_choice (self, grid, row++,
					 _("De_lete originals after verification"),
					 &self->delete_sources);
	gtk_widget_set_name (self->delete_check, "delete-originals");
	gtk_widget_set_tooltip_text (self->delete_check,
				     _("The archive is read back first. Unless every file is in "
				       "there at the same size, nothing is deleted."));
}

/* What the dialog starts from next time. The password is never written
   anywhere, and the delete box has to be ticked afresh each time - neither is
   a setting so much as a decision about one archive. Everything else under
   Options is shown once the format is set. */
static void
restore_remembered (ArchiveDialog *self)
{
	NemoConfigGroup *group = nemo_config_get_group (NEMO_ARCHIVE_COMMANDS_GROUP);

	if (group == NULL) {
		return;
	}

	/* load starts from nothing, so the defaults' volume size goes first. */
	nemo_archive_choices_clear (&self->chosen);
	nemo_archive_choices_load (&self->chosen);
	show_level_and_size (self);

	/* Ticking a box nothing can act on would just be confusing, so the one
	   that depends on the selection is only put back where it applies. */
	if (gtk_widget_get_sensitive (self->each_check)) {
		set_check (self->each_check, group, NEMO_ARCHIVE_STATE_KEY_EACH);
	}
}

/* As chosen, so one the format or the selection turned off is still there
   next time. */
static void
remember_settings (ArchiveDialog *self)
{
	NemoConfigGroup *group = nemo_config_get_group (NEMO_ARCHIVE_COMMANDS_GROUP);
	const char *id;

	if (group == NULL) {
		return;
	}

	id = gtk_combo_box_get_active_id (GTK_COMBO_BOX (self->format_combo));
	if (id != NULL) {
		nemo_config_set_string (group, NEMO_ARCHIVE_STATE_KEY_FORMAT, id);
	}

	nemo_archive_choices_save (&self->chosen);
	if (gtk_widget_get_sensitive (self->each_check)) {
		save_check (self->each_check, group, NEMO_ARCHIVE_STATE_KEY_EACH);
	}
}

/* What the controls show, which is what the format can do of what was
   chosen. A size that can't be read never gets here: Compress is grayed. */
static void
collect_options (ArchiveDialog      *self,
		 NemoArchiveOptions *options)
{
	NemoArchiveChoices shown = { 0 };
	NemoArchiveCaps caps;

	nemo_archive_options_init (options);

	options->format = current_format (self);
	caps = nemo_archive_format_caps (options->format);
	nemo_archive_choices_shown (&self->chosen, caps, has_password (self, caps), &shown);

	options->level = shown.level;

	if (has_password (self, caps)) {
		options->password = g_strdup (self->password);
		options->encrypt_names = shown.encrypt_names;
	}

	if (shown.split) {
		guint64 bytes = 0;

		if (nemo_archive_parse_size (gtk_entry_get_text (GTK_ENTRY (split_entry (self))), &bytes)) {
			options->split_size = bytes;
		}
	}

	options->solid = shown.solid;
	options->dedupe = shown.dedupe;
	/* As chosen. The job has a store its writer can't do fall back to
	   Ignore, as the buttons show it. */
	options->links = self->chosen.links;
	options->recovery_record = shown.recovery;
	options->lock = shown.lock;

	options->delete_sources = gtk_widget_get_sensitive (self->delete_check) && self->delete_sources;

	nemo_archive_choices_clear (&shown);
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

	mismatch = gtk_label_new (_("That isn't the same password."));
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

static ArcMountTable *(*mounts_for_test) (void);
static guint scans_running;

void
nemo_archive_dialog_set_mounts_for_test (ArcMountTable *(*mounts) (void))
{
	mounts_for_test = mounts;
}

guint
nemo_archive_dialog_scans_for_test (void)
{
	return scans_running;
}

static char *
size_text (guint64 bytes)
{
	return g_format_size_full (bytes, nemo_global_preferences_get_size_prefix_preference ());
}

/* What turning the option off would take out, shown only while it's on. Off,
   the scan hasn't walked what it would add. */
static void
show_change (GtkWidget           *button,
	     const ArcScanReport *report,
	     ArcFollow            option)
{
	const char *text;

	if (button == NULL) {
		return;
	}

	text = g_object_get_data (G_OBJECT (button), "nemo-follow-text");
	if (report->follow & option) {
		char *size = size_text (arc_scan_report_change (report, option));
		/* Translators: a Follow option, then what it adds to the archive. */
		char *label = g_strdup_printf (_("%s (+%s)"), text, size);

		gtk_button_set_label (GTK_BUTTON (button), label);
		g_free (label);
		g_free (size);
	} else {
		gtk_button_set_label (GTK_BUTTON (button), text);
	}
}

static void
show_total (ArchiveDialog       *self,
	    const ArcScanReport *report)
{
	char *size = size_text (arc_scan_report_total (report));
	GString *text = g_string_new (NULL);
	GString *tip = g_string_new (_("What the options as set put in the archive."));

	g_string_printf (text, report->done ? _("Total size to include: %s")
			 : _("Total size to include: %s so far"), size);

	if (report->shares > 0) {
		g_string_append_printf (text, ngettext (", not counting %u on a share",
							", not counting %u on a share",
							report->shares), report->shares);
		g_string_append (tip, " ");
		g_string_append (tip, _("Links onto a network share, and shares mounted in a folder, "
					"aren't counted here."));
	}
	if (report->unreadable > 0) {
		g_string_append_printf (text, ngettext (", %u unreadable", ", %u unreadable",
							report->unreadable), report->unreadable);
		g_string_append (tip, " ");
		g_string_append_printf (tip, ngettext ("%u folder or file couldn't be read.",
						       "%u folders or files couldn't be read.",
						       report->unreadable), report->unreadable);
	}

	gtk_label_set_text (GTK_LABEL (self->total_label), text->str);
	gtk_widget_set_tooltip_text (self->total_label, tip->str);

	g_string_free (tip, TRUE);
	g_string_free (text, TRUE);
	g_free (size);
}

/* On this thread, through its main loop. */
static void
scan_reported (gpointer             data,
	       const ArcScanReport *report)
{
	ArchiveDialog *self = data;

	show_total (self, report);
	show_change (self->symlinks[ARC_LINK_FOLLOW], report, ARC_FOLLOW_SYMLINKS);
	show_change (self->junctions[ARC_LINK_FOLLOW], report, ARC_FOLLOW_JUNCTIONS);
	show_change (self->nested_check, report, ARC_FOLLOW_NESTED_FS);
	show_change (self->other_check, report, ARC_FOLLOW_OTHER_FS);
}

/* The folder the selection is in is where the walk starts from, for telling
   a crossing. A selection on a share isn't walked: that would be a visit the
   person didn't ask for, and stopping a walk stuck on one would hang the
   window. */
static void
start_scan (ArchiveDialog *self)
{
	GPtrArray *paths = g_ptr_array_new_with_free_func (g_free);
	ArcScanReport empty = { 0 };
	ArcHost host;
	char *base;
	GList *l;

	for (l = self->files; l != NULL; l = l->next) {
		char *path = g_file_get_path (G_FILE (l->data));

		if (path == NULL || nemo_path_is_on_a_share (path)) {
			if (path != NULL) {
				gtk_label_set_text (GTK_LABEL (self->total_label),
						    _("Total size to include: not counted on a share"));
			} else {
				gtk_widget_set_no_show_all (self->total_label, TRUE);
				gtk_widget_hide (self->total_label);
			}
			g_free (path);
			g_ptr_array_unref (paths);
			return;
		}
		g_ptr_array_add (paths, path);
	}
	g_ptr_array_add (paths, NULL);

	base = g_path_get_dirname (g_ptr_array_index (paths, 0));
	nemo_archive_host_init (&host);
	self->scan = arc_scan_new (&host, base, (const char * const *) paths->pdata, self->follow,
				   FALSE, mounts_for_test != NULL ? mounts_for_test () : NULL);
	self->scan_follow = self->follow;
	scans_running++;

	empty.follow = self->follow;
	scan_reported (self, &empty);
	arc_scan_start (self->scan, scan_reported, self);

	g_free (base);
	g_ptr_array_unref (paths);
}

static void
stop_scan (ArchiveDialog *self)
{
	if (self->scan != NULL) {
		arc_scan_free (self->scan);
		self->scan = NULL;
		scans_running--;
	}
}

/* The parent going takes the dialog with it, mid-run. */
static void
dialog_destroyed (GtkWidget     *widget,
		  ArchiveDialog *self)
{
	(void) widget;

	stop_scan (self);
	self->destroyed = TRUE;
}

static void
dialog_free (ArchiveDialog *self)
{
	if (self->recenter_id != 0) {
		g_source_remove (self->recenter_id);
	}

	g_list_free_full (self->files, g_object_unref);
	nemo_archive_choices_clear (&self->chosen);
	g_free (self->password);
	g_free (self);
}

void
nemo_archive_dialog_show (GtkWindow *parent_window,
			  GList     *files,
			  GFile     *default_dir,
			  gboolean   whole_folder,
			  gboolean   links_or_folders)
{
	ArchiveDialog *self;
	GtkWidget *content;
	GtkWidget *box;
	GtkWidget *grid;
	GtkWidget *compress_button;
	GtkWidget *action_area;
	GList *l;
	int i;
	int row = 0;
	gboolean any_format = FALSE;

	g_return_if_fail (files != NULL);

	self = g_new0 (ArchiveDialog, 1);
	nemo_archive_choices_init (&self->chosen);

	for (l = files; l != NULL; l = l->next) {
		self->files = g_list_prepend (self->files, g_object_ref (G_FILE (l->data)));
	}
	self->files = g_list_reverse (self->files);
	self->whole_folder = whole_folder;
	self->links_or_folders = links_or_folders;

	self->dialog = gtk_dialog_new_with_buttons (_("Compress"), parent_window,
						    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
						    GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
						    _("C_ompress"), GTK_RESPONSE_OK,
						    NULL);
	gtk_dialog_set_default_response (GTK_DIALOG (self->dialog), GTK_RESPONSE_OK);
	compress_button = gtk_dialog_get_widget_for_response (GTK_DIALOG (self->dialog), GTK_RESPONSE_OK);
	gtk_widget_set_can_default (compress_button, TRUE);
	g_signal_connect (self->dialog, "destroy", G_CALLBACK (dialog_destroyed), self);

	/* On the left of the buttons. A set width, so the dialog doesn't change
	   width as the numbers do; a note wraps below. */
	action_area = gtk_dialog_get_action_area (GTK_DIALOG (self->dialog));
	self->total_label = gtk_label_new (NULL);
	gtk_widget_set_name (self->total_label, "total-size");
	gtk_label_set_xalign (GTK_LABEL (self->total_label), 0.0);
	gtk_label_set_line_wrap (GTK_LABEL (self->total_label), TRUE);
	gtk_label_set_width_chars (GTK_LABEL (self->total_label), 34);
	gtk_label_set_max_width_chars (GTK_LABEL (self->total_label), 34);
	gtk_widget_set_margin_start (self->total_label, 6);
	gtk_box_pack_start (GTK_BOX (action_area), self->total_label, TRUE, TRUE, 0);
	gtk_button_box_set_child_secondary (GTK_BUTTON_BOX (action_area), self->total_label, TRUE);
	gtk_button_box_set_child_non_homogeneous (GTK_BUTTON_BOX (action_area), self->total_label, TRUE);

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
	gtk_widget_set_name (self->name_entry, "name");
	gtk_entry_set_activates_default (GTK_ENTRY (self->name_entry), TRUE);
	gtk_entry_set_width_chars (GTK_ENTRY (self->name_entry), 36);
	self->name_label = add_row (grid, row++, _("_Name"), self->name_entry);

	self->format_combo = gtk_combo_box_text_new ();
	gtk_widget_set_name (self->format_combo, "format");
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

		/* By what's remembered, so one the format turns off counts. */
		if (!nemo_archive_choices_at_default (&self->chosen)) {
			g_signal_connect_after (self->dialog, "map-event",
						G_CALLBACK (open_remembered), self);
		}

		if (group != NULL) {
			format_id = nemo_config_get_string (group, NEMO_ARCHIVE_STATE_KEY_FORMAT);
		}
		if (format_id == NULL ||
		    !gtk_combo_box_set_active_id (GTK_COMBO_BOX (self->format_combo), format_id)) {
			gtk_combo_box_set_active (GTK_COMBO_BOX (self->format_combo), 0);
		}
		g_free (format_id);

		start_scan (self);
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

		/* Gone with its parent during a question asked from here. */
		if (self->destroyed || gtk_dialog_run (GTK_DIALOG (self->dialog)) != GTK_RESPONSE_OK) {
			break;
		}

		each = compressing_each (self);

		/* A response sent some other way than the button can still
		   get past it grayed. */
		if (size_unreadable (self)) {
			gtk_widget_grab_focus (split_entry (self));
			continue;
		}

		name = gtk_entry_get_text (GTK_ENTRY (self->name_entry));
		if (!each && (name == NULL || name[0] == '\0')) {
			gtk_widget_grab_focus (self->name_entry);
			continue;
		}
		if (!each) {
			g_autofree char *bad = nemo_file_name_bad_char_message (name);

			if (bad != NULL) {
				eel_show_error_dialog (_("The archive couldn't be made."), bad,
						       GTK_WINDOW (self->dialog));
				gtk_widget_grab_focus (self->name_entry);
				continue;
			}
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

		/* The job counts again for itself. */
		stop_scan (self);
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

	stop_scan (self);
	if (!self->destroyed) {
		gtk_widget_destroy (self->dialog);
	}
	dialog_free (self);
}
