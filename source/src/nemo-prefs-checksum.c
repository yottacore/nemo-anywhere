/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-prefs-checksum.c - the Blake3 checksum section on Advanced.

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

#include "nemo-prefs-checksum.h"

#include <glib/gi18n.h>

#include <libnemo-private/nemo-global-preferences.h>

/* The external drives row only counts while both boxes above it are on. */
static void
update_external_row (GtkWidget *row)
{
	GtkToggleButton *full_reads = g_object_get_data (G_OBJECT (row), "full-reads");
	GtkToggleButton *checksum = g_object_get_data (G_OBJECT (row), "checksum");

	gtk_widget_set_sensitive (row, gtk_toggle_button_get_active (full_reads) && gtk_toggle_button_get_active (checksum));
}

static void
on_checksum_box_toggled (GtkToggleButton *button, GtkWidget *row)
{
	(void) button;
	update_external_row (row);
}

void
nemo_prefs_checksum_setup (GtkBuilder *builder)
{
	NemoConfigGroup *group = nemo_config_get_group (NEMO_FILE_CACHE_GROUP);
	GtkWidget *full_reads, *checksum, *external, *external_row, *warning;
	const char *external_tip;

	full_reads = GTK_WIDGET (gtk_builder_get_object (builder, "file_cache_checksum_full_reads_checkbutton"));
	nemo_config_bind (group, NEMO_FILE_CACHE_CHECKSUM_FULL_READS, G_OBJECT (full_reads),
			  "active", NEMO_CONFIG_BIND_DEFAULT);
	gtk_widget_set_tooltip_text (full_reads,
		_("Highly recommended. Note: Always enabled for thumbnail cache operations, unless that feature is turned off."));

	checksum = GTK_WIDGET (gtk_builder_get_object (builder, "file_cache_save_checksum_checkbutton"));
	nemo_config_bind (group, NEMO_FILE_CACHE_SAVE_CHECKSUM, G_OBJECT (checksum),
			  "active", NEMO_CONFIG_BIND_DEFAULT);
	/* Only grayed here. nemo_thumbnail_saves_checksum is what ignores it. */
	g_object_bind_property (full_reads, "active", checksum, "sensitive", G_BINDING_SYNC_CREATE);
#ifdef G_OS_WIN32
	gtk_button_set_label (GTK_BUTTON (checksum), _("_Persist across hosts via alternate data streams."));
	gtk_widget_set_tooltip_text (checksum,
		_("A cheap way to tell unique or changed content apart, that works across platforms and hosts.\n\n"
		  "But not every file tool or filesystem keeps alternate data streams. Only local NTFS and ReFS drives do. "
		  "Writing one counts as a change to the file, which can throw off the rare tool that relies on that. "
		  "The modified time stays as it was. Some filesystems limit the total size of them per file."));
#else
	gtk_widget_set_tooltip_text (checksum,
		_("A cheap way to tell unique or changed content apart, that works across platforms and hosts.\n\n"
		  "But not every file tool or filesystem keeps xattrs. Writing one moves the file's change time (ctime), "
		  "which can throw off the rare file tool that relies on it. Most use the modified time, which this doesn't "
		  "change. Some filesystems limit the total xattr size per file."));
#endif

	external = GTK_WIDGET (gtk_builder_get_object (builder, "file_cache_save_checksum_external_checkbutton"));
	external_row = GTK_WIDGET (gtk_builder_get_object (builder, "file_cache_save_checksum_external_box"));
	warning = GTK_WIDGET (gtk_builder_get_object (builder, "file_cache_save_checksum_external_warning"));
	nemo_config_bind (group, NEMO_FILE_CACHE_SAVE_CHECKSUM_EXTERNAL, G_OBJECT (external),
			  "active", NEMO_CONFIG_BIND_DEFAULT);
	g_object_set_data (G_OBJECT (external_row), "full-reads", full_reads);
	g_object_set_data (G_OBJECT (external_row), "checksum", checksum);
	g_signal_connect_object (full_reads, "toggled", G_CALLBACK (on_checksum_box_toggled), external_row, 0);
	g_signal_connect_object (checksum, "toggled", G_CALLBACK (on_checksum_box_toggled), external_row, 0);
	update_external_row (external_row);
#ifdef G_OS_WIN32
	external_tip = _("Not recommended. A USB stick, a USB disk, or an SD card often goes to other machines, "
			 "and their tools don't expect alternate data streams there.\n\n"
			 "Checksums of files on them still go in the local file cache when they're read normally.");
#else
	external_tip = _("Not recommended. A USB stick, a USB disk, or an SD card often goes to other machines, "
			 "and their tools don't expect xattrs there.\n\n"
			 "Checksums of files on them still go in the local file cache when they're read normally.");
#endif
	gtk_widget_set_tooltip_text (external, external_tip);
	gtk_widget_set_tooltip_text (warning, external_tip);
}
