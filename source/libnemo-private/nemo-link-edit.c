/* nemo-link-edit.c
 *
 * Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2, as published by the
 * Free Software Foundation.
 */

#include <config.h>
#include "nemo-link-edit.h"
#include "nemo-delete-guard.h"
#include "nemo-link-copy.h"
#include "nemo-lnk.h"

#include <errno.h>
#include <string.h>

#include <glib/gi18n.h>
#include <glib/gstdio.h>

#include <eel/eel-stock-dialogs.h>

static gboolean
check_name (const char *name, GError **error)
{
	if (name == NULL || name[0] == '\0' || strcmp (name, ".") == 0 || strcmp (name, "..") == 0 ||
	    strchr (name, '/') != NULL
#ifdef G_OS_WIN32
	    || strchr (name, '\\') != NULL
#endif
	    ) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_FILENAME,
			     _("\"%s\" cannot be used as a name."), name != NULL ? name : "");
		return FALSE;
	}

	return TRUE;
}

/* Whether a and b are one file, links not followed. On Windows that is a
   change of case only. */
static gboolean
same_file (const char *a, const char *b)
{
#ifdef G_OS_WIN32
	char *fa = g_utf8_casefold (a, -1);
	char *fb = g_utf8_casefold (b, -1);
	gboolean same = strcmp (fa, fb) == 0;

	g_free (fa);
	g_free (fb);
	return same;
#else
	GStatBuf sa, sb;

	return g_lstat (a, &sa) == 0 && g_lstat (b, &sb) == 0 &&
	       sa.st_dev == sb.st_dev && sa.st_ino == sb.st_ino;
#endif
}

#ifndef G_OS_WIN32
/* Whether a and b are one directory entry, under two spellings where the
   file system ignores case or normalization. A symlink with a second hard
   link could pass the inode test alone. */
static gboolean
one_entry (const char *a, const char *b)
{
	GStatBuf sa, sb;

	return g_lstat (a, &sa) == 0 && g_lstat (b, &sb) == 0 &&
	       sa.st_dev == sb.st_dev && sa.st_ino == sb.st_ino && sa.st_nlink == 1;
}
#endif

static gboolean
is_taken (const char *path)
{
	GStatBuf info;

	return g_lstat (path, &info) == 0;
}

/* Where the item will be once renamed, or NULL with error set when the name
   will not do or is taken by something else. */
static char *
renamed_path (const char *path, const char *new_name, GError **error)
{
	char *dir, *new_path;

	if (!check_name (new_name, error)) {
		return NULL;
	}

	dir = g_path_get_dirname (path);
	new_path = g_build_filename (dir, new_name, NULL);
	g_free (dir);

	if (strcmp (new_path, path) != 0 && is_taken (new_path) && !same_file (new_path, path)) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_EXISTS,
			     _("There is already something named \"%s\" here."), new_name);
		g_free (new_path);
		return NULL;
	}

	return new_path;
}

static gboolean
rename_to (const char *path, const char *new_path, GError **error)
{
	int saved;

	if (strcmp (path, new_path) == 0 || g_rename (path, new_path) == 0) {
		return TRUE;
	}

	saved = errno;
	g_set_error (error, G_IO_ERROR, g_io_error_from_errno (saved),
		     _("Could not rename it: %s"), g_strerror (saved));
	return FALSE;
}

/* A name beside path that nothing has, for the new link to be made under
   before it takes the old one's place. */
static char *
spare_path (const char *dir, const char *name)
{
	for (;;) {
		char *spare_name = g_strdup_printf (".%s.edit-%06x", name, g_random_int_range (0, 0xffffff));
		char *spare = g_build_filename (dir, spare_name, NULL);

		g_free (spare_name);
		if (!is_taken (spare)) {
			return spare;
		}
		g_free (spare);
	}
}

static gboolean
remove_link (const char *path, GError **error)
{
	GFile *file = g_file_new_for_path (path);
	gboolean ok = nemo_delete_guard_remove_link (file, error);

	g_object_unref (file);
	return ok;
}

gboolean
nemo_link_edit_symlink (const char  *link_path,
                        const char  *new_name,
                        const char  *new_target,
                        GError     **error)
{
	GFile *file;
	NemoLinkKind kind;
	char *dir = NULL, *new_path = NULL, *old_target = NULL, *spare = NULL;
	gboolean ok = FALSE;
#ifndef G_OS_WIN32
	gboolean same_entry;
#endif

	if (new_target == NULL || new_target[0] == '\0') {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
				     _("A link has to point at something."));
		return FALSE;
	}

	file = g_file_new_for_path (link_path);
	kind = nemo_link_kind (file, NULL);
	if (kind == NEMO_LINK_NONE || !nemo_link_read_target (file, &old_target, NULL)) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
			     _("\"%s\" is not a link any more."), link_path);
		goto out;
	}

	new_path = renamed_path (link_path, new_name, error);
	if (new_path == NULL) {
		goto out;
	}

	if (strcmp (old_target, new_target) == 0) {
		ok = rename_to (link_path, new_path, error);
		goto out;
	}

	dir = g_path_get_dirname (link_path);

#ifdef G_OS_WIN32
	/* Windows keeps file and folder symlinks apart. A new target that is
	   there decides; one that is not keeps the kind the link had. */
	if (kind != NEMO_LINK_JUNCTION) {
		char *resolved = g_path_is_absolute (new_target)
			? g_strdup (new_target) : g_build_filename (dir, new_target, NULL);

		if (g_file_test (resolved, G_FILE_TEST_IS_DIR)) {
			kind = NEMO_LINK_DIR_SYMLINK;
		} else if (g_file_test (resolved, G_FILE_TEST_EXISTS)) {
			kind = NEMO_LINK_FILE_SYMLINK;
		}
		g_free (resolved);
	}
#endif

#ifndef G_OS_WIN32
	/* Asked before the rename. After it, where the new name is the old link
	   under another spelling, removing the old name would take the new link
	   with it, and a cached lookup of the old spelling can still show the
	   old link. */
	same_entry = strcmp (new_path, link_path) == 0 || one_entry (new_path, link_path);
#endif

	spare = spare_path (dir, new_name);
	if (!nemo_link_create (new_target, spare, dir, kind, error)) {
		goto out;
	}

#ifdef G_OS_WIN32
	/* Nothing takes the place of a folder in one step here, so the old link
	   goes first. If it will not go, the new one is taken back. */
	if (!remove_link (link_path, error)) {
		remove_link (spare, NULL);
		goto out;
	}
	if (g_rename (spare, new_path) != 0) {
		int saved = errno;
		char *spare_name = g_path_get_basename (spare);

		g_set_error (error, G_IO_ERROR, g_io_error_from_errno (saved),
			     _("The new link was left as \"%s\": %s"), spare_name, g_strerror (saved));
		g_free (spare_name);
		goto out;
	}
#else
	/* A rename over a symlink replaces the link, so under the same name the
	   old one is never missing. */
	if (g_rename (spare, new_path) != 0) {
		int saved = errno;

		remove_link (spare, NULL);
		g_set_error (error, G_IO_ERROR, g_io_error_from_errno (saved),
			     _("Could not put the new link in place: %s"), g_strerror (saved));
		goto out;
	}
	if (!same_entry && !remove_link (link_path, error)) {
		goto out;
	}
#endif
	ok = TRUE;

 out:
	g_object_unref (file);
	g_free (dir);
	g_free (new_path);
	g_free (old_target);
	g_free (spare);

	return ok;
}

static gboolean
has_lnk (const char *name)
{
	char *lower = g_ascii_strdown (name, -1);
	gboolean yes = g_str_has_suffix (lower, ".lnk");

	g_free (lower);
	return yes;
}

gboolean
nemo_link_edit_is_shortcut (GFile *link)
{
	const char *path = g_file_peek_path (link);
	char *name;
	gboolean yes;

	if (path == NULL || nemo_link_kind (link, NULL) != NEMO_LINK_NONE) {
		return FALSE;
	}
	name = g_path_get_basename (path);
	yes = has_lnk (name);
	g_free (name);

	return yes;
}

gboolean
nemo_link_edit_shortcut (const char  *lnk_path,
                         const char  *new_name,
                         gboolean     set_paths,
                         const char  *absolute,
                         const char  *relative,
                         const char  *portable,
                         GError     **error)
{
	GFile *file;
	char *full_name, *new_path;
	gboolean ok;

	/* Written through a symlink, the save would replace the symlink with a
	   copy of what it points at. */
	file = g_file_new_for_path (lnk_path);
	ok = nemo_link_kind (file, NULL) == NEMO_LINK_NONE;
	g_object_unref (file);
	if (!ok) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
			     _("\"%s\" is a symlink or junction, not a shortcut."), lnk_path);
		return FALSE;
	}

	full_name = new_name != NULL && new_name[0] != '\0' && !has_lnk (new_name)
		? g_strconcat (new_name, ".lnk", NULL) : g_strdup (new_name);
	new_path = renamed_path (lnk_path, full_name, error);
	g_free (full_name);
	if (new_path == NULL) {
		return FALSE;
	}

	ok = (!set_paths || nemo_lnk_set_paths (lnk_path, absolute, relative, portable, error)) &&
	     rename_to (lnk_path, new_path, error);
	g_free (new_path);

	return ok;
}

/* The shortcut's full path to its target, local if it has one. */
static char *
lnk_absolute (const NemoLnk *lnk)
{
	if (lnk->local_path != NULL) {
		return g_strdup (lnk->local_path);
	}
	if (lnk->net_share != NULL) {
		if (lnk->net_path == NULL || lnk->net_path[0] == '\0') {
			return g_strdup (lnk->net_share);
		}
		return g_strconcat (lnk->net_share, "\\", lnk->net_path, NULL);
	}

	return g_strdup ("");
}

static GtkWidget *
add_field (GtkGrid *grid, int row, const char *label_text, const char *value, const char *tip)
{
	GtkWidget *label = gtk_label_new_with_mnemonic (label_text);
	GtkWidget *entry = gtk_entry_new ();

	gtk_widget_set_halign (label, GTK_ALIGN_END);
	gtk_label_set_mnemonic_widget (GTK_LABEL (label), entry);
	gtk_entry_set_text (GTK_ENTRY (entry), value != NULL ? value : "");
	gtk_entry_set_width_chars (GTK_ENTRY (entry), 48);
	gtk_entry_set_activates_default (GTK_ENTRY (entry), TRUE);
	gtk_widget_set_hexpand (entry, TRUE);
	if (tip != NULL) {
		gtk_widget_set_tooltip_text (label, tip);
		gtk_widget_set_tooltip_text (entry, tip);
	}

	gtk_grid_attach (grid, label, 0, row, 1, 1);
	gtk_grid_attach (grid, entry, 1, row, 1, 1);

	return entry;
}

static const char *
entry_text (GtkWidget *entry)
{
	return entry != NULL ? gtk_entry_get_text (GTK_ENTRY (entry)) : NULL;
}

void
nemo_link_edit_ask (GtkWindow *parent,
                    GFile     *link)
{
	GtkWidget *dialog, *grid, *name_entry;
	GtkWidget *target_entry = NULL, *absolute_entry = NULL, *relative_entry = NULL, *portable_entry = NULL;
	char *path, *name, *shown_name = NULL, *target = NULL, *absolute = NULL;
	gboolean is_lnk;
	NemoLnk lnk = { 0 };
	int row = 0;

	path = g_file_get_path (link);
	if (path == NULL) {
		eel_show_error_dialog (_("This link cannot be edited"),
				       _("Only a link in a local folder can be edited."), parent);
		return;
	}
	name = g_path_get_basename (path);
	is_lnk = nemo_link_edit_is_shortcut (link);

	if (is_lnk) {
		if (!nemo_lnk_read (path, &lnk)) {
			char *text = g_strdup_printf (_("\"%s\" is not a shortcut that can be read."), name);

			eel_show_error_dialog (_("This link cannot be edited"), text, parent);
			g_free (text);
			goto out;
		}
		/* Shown the way the shell shows it, without the extension. */
		shown_name = g_strndup (name, strlen (name) - 4);
		absolute = lnk_absolute (&lnk);
	} else {
		GError *error = NULL;

		if (nemo_link_kind (link, NULL) == NEMO_LINK_NONE ||
		    !nemo_link_read_target (link, &target, &error)) {
			char *text = g_strdup_printf (_("\"%s\" is not a symlink or junction."), name);

			eel_show_error_dialog (_("This link cannot be edited"), text, parent);
			g_free (text);
			g_clear_error (&error);
			goto out;
		}
		shown_name = g_strdup (name);
	}

	dialog = gtk_dialog_new_with_buttons (_("Edit link"), parent,
					      GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
					      _("_Cancel"), GTK_RESPONSE_CANCEL,
					      _("_Save"), GTK_RESPONSE_OK,
					      NULL);
	gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);
	gtk_window_set_resizable (GTK_WINDOW (dialog), TRUE);

	grid = gtk_grid_new ();
	gtk_grid_set_row_spacing (GTK_GRID (grid), 6);
	gtk_grid_set_column_spacing (GTK_GRID (grid), 12);
	gtk_container_set_border_width (GTK_CONTAINER (grid), 12);
	gtk_box_pack_start (GTK_BOX (gtk_dialog_get_content_area (GTK_DIALOG (dialog))), grid, TRUE, TRUE, 0);

	name_entry = add_field (GTK_GRID (grid), row++, _("_Name:"), shown_name, NULL);
	if (is_lnk) {
		absolute_entry = add_field (GTK_GRID (grid), row++, _("_Absolute path:"), absolute,
					    _("The full path, such as C:\\Users\\me\\file.txt, or a share path, "
					      "such as \\\\server\\share\\file.txt."));
		relative_entry = add_field (GTK_GRID (grid), row++, _("_Relative path:"), lnk.relative_path,
					    _("The way from the folder the shortcut is in, such as ..\\docs\\file.txt."));
		portable_entry = add_field (GTK_GRID (grid), row++, _("_Portable path:"), lnk.env_path,
					    _("A path with environment variables in it, such as "
					      "%USERPROFILE%\\Documents\\file.txt. Windows follows this one "
					      "from a shortcut made anywhere."));
		{
			GtkWidget *note = gtk_label_new (_("An empty path is left out. At least one is needed."));

			gtk_widget_set_halign (note, GTK_ALIGN_START);
			gtk_style_context_add_class (gtk_widget_get_style_context (note), "dim-label");
			gtk_grid_attach (GTK_GRID (grid), note, 1, row++, 1, 1);
		}
	} else {
		target_entry = add_field (GTK_GRID (grid), row++, _("_Points to:"), target,
					  _("A relative path is followed from the folder the link is in."));
	}
	gtk_widget_show_all (grid);

	for (;;) {
		GError *error = NULL;
		gboolean ok;

		if (gtk_dialog_run (GTK_DIALOG (dialog)) != GTK_RESPONSE_OK) {
			break;
		}

		if (is_lnk) {
			gboolean changed =
				g_strcmp0 (entry_text (absolute_entry), absolute) != 0 ||
				g_strcmp0 (entry_text (relative_entry), lnk.relative_path ? lnk.relative_path : "") != 0 ||
				g_strcmp0 (entry_text (portable_entry), lnk.env_path ? lnk.env_path : "") != 0;

			ok = nemo_link_edit_shortcut (path, entry_text (name_entry), changed,
						      entry_text (absolute_entry),
						      entry_text (relative_entry),
						      entry_text (portable_entry), &error);
		} else {
			ok = nemo_link_edit_symlink (path, entry_text (name_entry),
						     entry_text (target_entry), &error);
		}
		if (ok) {
			break;
		}

		/* The dialog stays, so a typo can be fixed rather than typed again. */
		{
			GtkWidget *message = gtk_message_dialog_new (GTK_WINDOW (dialog), GTK_DIALOG_MODAL,
								     GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "%s",
								     _("The link could not be changed"));

			gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (message), "%s", error->message);
			gtk_dialog_run (GTK_DIALOG (message));
			gtk_widget_destroy (message);
		}
		g_error_free (error);
	}

	gtk_widget_destroy (dialog);

 out:
	g_free (shown_name);
	nemo_lnk_clear (&lnk);
	g_free (absolute);
	g_free (target);
	g_free (name);
	g_free (path);
}
