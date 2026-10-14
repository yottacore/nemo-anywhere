/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-prefs-file-cache.c - the thumbnail cache section on Preview.

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

#include "nemo-prefs-file-cache.h"

#include <glib/gi18n.h>
#include <glib/gstdio.h>

#include <libnemo-private/nemo-cache-db.h>
#include <libnemo-private/nemo-cache-db-prune.h>
#include <libnemo-private/nemo-global-preferences.h>

/* Hung off the usage label, so it goes when the dialog does. Work started from
 * here holds a weak reference to the label and finds this again through it,
 * since a cleanup can outlast the dialog. */
typedef struct {
	GtkLabel  *usage;
	GtkWidget *cleanup;
	GtkWidget *empty;
} Page;

typedef struct {
	GWeakRef *ref;
	gint64    thumbnails;
	gint64    file_bytes;
	gboolean  opened;
	char     *note;
} Usage;

static Page *
page_from_ref (GWeakRef *ref)
{
	g_autoptr (GObject) label = g_weak_ref_get (ref);

	/* The label is only borrowed back to find the page; the dialog owns it. */
	return label != NULL ? g_object_get_data (label, "file-cache-page") : NULL;
}

static GWeakRef *
ref_to (Page *page)
{
	GWeakRef *ref = g_new0 (GWeakRef, 1);

	g_weak_ref_init (ref, page->usage);

	return ref;
}

static void
drop_ref (gpointer data)
{
	GWeakRef *ref = data;

	g_weak_ref_clear (ref);
	g_free (ref);
}

static void
set_busy (Page *page, gboolean busy)
{
	gtk_widget_set_sensitive (page->cleanup, !busy);
	gtk_widget_set_sensitive (page->empty, !busy);
}

static void
usage_free (gpointer data)
{
	Usage *usage = data;

	drop_ref (usage->ref);
	g_free (usage->note);
	g_free (usage);
}

static void
usage_in_thread (GTask *task, gpointer source, gpointer task_data, GCancellable *cancellable)
{
	Usage *usage = task_data;
	NemoCacheDb *db = nemo_cache_db_get ();
	g_autofree char *path = nemo_cache_db_path ();
	GStatBuf info;

	(void) source;
	(void) cancellable;

	usage->opened = db != NULL;
	nemo_cache_db_usage (db, &usage->thumbnails, NULL);

	/* New rows sit in the journal until it is folded back in, so the main
	 * file alone can read as nearly empty. */
	if (path != NULL && g_stat (path, &info) == 0)
		usage->file_bytes = info.st_size;
	if (path != NULL) {
		g_autofree char *wal = g_strconcat (path, "-wal", NULL);

		if (g_stat (wal, &info) == 0)
			usage->file_bytes += info.st_size;
	}

	g_task_return_boolean (task, TRUE);
}

static void
usage_done (GObject *source, GAsyncResult *res, gpointer user_data)
{
	Usage *usage = g_task_get_task_data (G_TASK (res));
	Page *page = page_from_ref (usage->ref);
	g_autofree char *size = NULL;
	g_autofree char *line = NULL;

	(void) source;
	(void) user_data;

	if (page == NULL)
		return;

	if (!usage->opened) {
		line = g_strdup (_("The cache couldn't be opened."));
	} else {
		size = g_format_size_full (usage->file_bytes, G_FORMAT_SIZE_IEC_UNITS);
		line = g_strdup_printf (g_dngettext (NULL, "%" G_GINT64_FORMAT " thumbnail, %s on disk.",
							   "%" G_GINT64_FORMAT " thumbnails, %s on disk.",
							   (gulong) MIN (usage->thumbnails, G_MAXLONG)),
					usage->thumbnails, size);
	}

	if (usage->note != NULL) {
		g_autofree char *both = g_strconcat (line, " ", usage->note, NULL);

		gtk_label_set_text (page->usage, both);
	} else {
		gtk_label_set_text (page->usage, line);
	}

	set_busy (page, FALSE);
}

/* Counting reads the whole thumbnails table, so not on the main loop. */
static void
refresh_usage (Page *page, const char *note)
{
	Usage *usage = g_new0 (Usage, 1);
	GTask *task;

	usage->ref = ref_to (page);
	usage->note = g_strdup (note);

	set_busy (page, TRUE);

	task = g_task_new (NULL, NULL, usage_done, NULL);
	g_task_set_task_data (task, usage, usage_free);
	g_task_run_in_thread (task, usage_in_thread);
	g_object_unref (task);
}

static void
cleanup_done (NemoCachePruneResult result, gint64 removed, gpointer user_data)
{
	Page *page = page_from_ref (user_data);
	g_autofree char *note = NULL;

	if (page != NULL) {
		switch (result) {
		case NEMO_CACHE_PRUNE_DONE:
			note = g_strdup_printf (g_dngettext (NULL, "Cleaned up, %" G_GINT64_FORMAT " removed.",
							     "Cleaned up, %" G_GINT64_FORMAT " removed.",
							     (gulong) MIN (removed, G_MAXLONG)),
						removed);
			break;
		case NEMO_CACHE_PRUNE_BUSY:
			note = g_strdup (_("Another window is cleaning it up already."));
			break;
		case NEMO_CACHE_PRUNE_DAMAGED:
			note = g_strdup (_("It was damaged, and starts over the next time this program starts."));
			break;
		case NEMO_CACHE_PRUNE_FAILED:
			note = g_strdup (_("Cleaning up didn't work."));
			break;
		case NEMO_CACHE_PRUNE_NOT_DUE:
		case NEMO_CACHE_PRUNE_CANCELLED:
		default:
			break;
		}

		refresh_usage (page, note);
	}

	drop_ref (user_data);
}

static void
cleanup_clicked (GtkButton *button, Page *page)
{
	GWeakRef *ref;

	(void) button;

	ref = ref_to (page);
	if (!nemo_cache_db_prune_now (cleanup_done, ref)) {
		drop_ref (ref);
		gtk_label_set_text (page->usage, _("Already cleaning up."));
		return;
	}

	set_busy (page, TRUE);
	gtk_label_set_text (page->usage, _("Cleaning up..."));
}

static void
empty_in_thread (GTask *task, gpointer source, gpointer task_data, GCancellable *cancellable)
{
	(void) source;
	(void) task_data;
	(void) cancellable;

	g_task_return_boolean (task, nemo_cache_db_empty (nemo_cache_db_get ()));
}

static void
empty_done (GObject *source, GAsyncResult *res, gpointer user_data)
{
	Page *page = page_from_ref (user_data);
	gboolean emptied = g_task_propagate_boolean (G_TASK (res), NULL);

	(void) source;

	if (page != NULL)
		refresh_usage (page, emptied ? _("Emptied.") : _("Emptying didn't work."));
}

static void
empty_clicked (GtkButton *button, Page *page)
{
	GtkWidget *ask;
	GWeakRef *ref;
	GTask *task;
	gint answer;

	ask = gtk_message_dialog_new (GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (button))),
				      GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
				      GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE,
				      _("Empty the thumbnail cache?"));
	gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (ask),
		_("Thumbnails are made again as folders are shown, which takes a while in a big folder of pictures."));
	gtk_dialog_add_buttons (GTK_DIALOG (ask),
				_("_Cancel"), GTK_RESPONSE_CANCEL,
				_("_Empty"), GTK_RESPONSE_ACCEPT,
				NULL);
	gtk_dialog_set_default_response (GTK_DIALOG (ask), GTK_RESPONSE_CANCEL);

	answer = gtk_dialog_run (GTK_DIALOG (ask));
	gtk_widget_destroy (ask);

	if (answer != GTK_RESPONSE_ACCEPT)
		return;

	set_busy (page, TRUE);
	gtk_label_set_text (page->usage, _("Emptying..."));

	ref = ref_to (page);
	task = g_task_new (NULL, NULL, empty_done, ref);
	g_task_set_task_data (task, ref, drop_ref);
	g_task_run_in_thread (task, empty_in_thread);
	g_object_unref (task);
}

/* The day count is a whole number and a spin box holds a double. */
static gboolean
days_get_mapping (GValue *value, const NemoConfigValue *config_value, gpointer user_data)
{
	(void) user_data;

	g_value_set_double (value, (gdouble) config_value->i);
	return TRUE;
}

static gboolean
days_set_mapping (const GValue *value, NemoConfigValue *config_value, gpointer user_data)
{
	(void) user_data;

	config_value->i = (gint64) (g_value_get_double (value) + 0.5);
	return TRUE;
}

void
nemo_prefs_file_cache_setup (GtkBuilder *builder)
{
	NemoConfigGroup *group = nemo_config_get_group (NEMO_FILE_CACHE_GROUP);
	GtkWidget *checksum;
	Page *page;

	nemo_config_bind (group, NEMO_FILE_CACHE_MAX_SIZE_GIB,
			  gtk_builder_get_object (builder, "file_cache_size_spinbutton"),
			  "value", NEMO_CONFIG_BIND_DEFAULT);
	nemo_config_bind (group, NEMO_FILE_CACHE_MEMORY_GIB,
			  gtk_builder_get_object (builder, "file_cache_memory_spinbutton"),
			  "value", NEMO_CONFIG_BIND_DEFAULT);
	nemo_config_bind_with_mapping (group, NEMO_FILE_CACHE_MAX_AGE_DAYS,
				       gtk_builder_get_object (builder, "file_cache_age_spinbutton"),
				       "value", NEMO_CONFIG_BIND_DEFAULT,
				       days_get_mapping, days_set_mapping, NULL, NULL);
	nemo_config_bind (group, NEMO_FILE_CACHE_DROP_MISSING,
			  gtk_builder_get_object (builder, "file_cache_drop_missing_checkbutton"),
			  "active", NEMO_CONFIG_BIND_DEFAULT);

	checksum = GTK_WIDGET (gtk_builder_get_object (builder, "file_cache_save_checksum_checkbutton"));
	nemo_config_bind (group, NEMO_FILE_CACHE_SAVE_CHECKSUM, G_OBJECT (checksum),
			  "active", NEMO_CONFIG_BIND_DEFAULT);
#ifdef G_OS_WIN32
	gtk_widget_set_tooltip_text (checksum,
		_("Fairly cheap to do, and the file's modified time stays as it was. Writing one does count as a change to "
		  "the file, which can wake a backup tool, though most ignore it. Only local NTFS and ReFS drives "
		  "keep them."));
#else
	gtk_widget_set_tooltip_text (checksum,
		_("Fairly cheap to do, and the file's modified time stays as it was. Writing one moves its change time "
		  "(ctime), which can wake a backup tool, though most ignore it."));
#endif

	page = g_new0 (Page, 1);
	page->usage = GTK_LABEL (gtk_builder_get_object (builder, "file_cache_usage_label"));
	page->cleanup = GTK_WIDGET (gtk_builder_get_object (builder, "file_cache_cleanup_button"));
	page->empty = GTK_WIDGET (gtk_builder_get_object (builder, "file_cache_empty_button"));
	g_object_set_data_full (G_OBJECT (page->usage), "file-cache-page", page, g_free);

	g_signal_connect (page->cleanup, "clicked", G_CALLBACK (cleanup_clicked), page);
	g_signal_connect (page->empty, "clicked", G_CALLBACK (empty_clicked), page);

	refresh_usage (page, NULL);
}
