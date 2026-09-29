/* nemo-link-copy.c
 *
 * Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2, as published by the
 * Free Software Foundation.
 */

#include <config.h>
#include "nemo-link-copy.h"
#include "nemo-lnk.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <glib/gi18n.h>

#include <eel/eel-stock-dialogs.h>

#ifdef G_OS_WIN32
#include "nemo-link-win32.h"
#else
#include <fcntl.h>
#include <unistd.h>
#endif

NemoLinkKind
nemo_link_kind (GFile     *file,
                GFileInfo *info)
{
	const char *path;

	if (info != NULL &&
	    g_file_info_has_attribute (info, G_FILE_ATTRIBUTE_STANDARD_IS_SYMLINK) &&
	    !g_file_info_get_is_symlink (info)) {
		return NEMO_LINK_NONE;
	}

	path = g_file_peek_path (file);
	if (path == NULL) {
		return NEMO_LINK_NONE;
	}

#ifdef G_OS_WIN32
	/* The reparse tag is the only thing that tells a junction from a symlink,
	   and it also keeps the other things the file system uses reparse points
	   for - cloud placeholders, store app aliases - out of the way. */
	return nemo_win32_link_kind (path);
#else
	if (info == NULL && !g_file_test (path, G_FILE_TEST_IS_SYMLINK)) {
		return NEMO_LINK_NONE;
	}
	if (info != NULL && g_file_info_get_file_type (info) != G_FILE_TYPE_SYMBOLIC_LINK &&
	    !g_file_info_get_is_symlink (info)) {
		return NEMO_LINK_NONE;
	}

	/* Follows the link, which is the only way to know what it points at. One
	   extra stat per link, and links are rare in a copy. */
	return g_file_test (path, G_FILE_TEST_IS_DIR) ? NEMO_LINK_DIR_SYMLINK
						      : NEMO_LINK_FILE_SYMLINK;
#endif
}

gboolean
nemo_link_read_target (GFile   *file,
                       char   **target,
                       GError **error)
{
#ifdef G_OS_WIN32
	const char *path = g_file_peek_path (file);

	if (path == NULL) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
				     _("That is not a local file."));
		return FALSE;
	}

	return nemo_win32_link_read_target (path, target, error);
#else
	GFileInfo *info;
	const char *found;

	*target = NULL;

	info = g_file_query_info (file, G_FILE_ATTRIBUTE_STANDARD_SYMLINK_TARGET,
				  G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, error);
	if (info == NULL) {
		return FALSE;
	}

	found = g_file_info_get_symlink_target (info);
	if (found != NULL) {
		*target = g_strdup (found);
	}
	g_object_unref (info);

	if (*target == NULL) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
				     _("That is not a link."));
		return FALSE;
	}

	return TRUE;
#endif
}

gboolean
nemo_link_create (const char    *target,
                  const char    *link_path,
                  const char    *base_dir,
                  NemoLinkKind   kind,
                  GError       **error)
{
#ifdef G_OS_WIN32
	return nemo_win32_link_create (target, link_path, base_dir, kind, error);
#else
	GFile *link = g_file_new_for_path (link_path);
	gboolean ok;

	(void) base_dir;
	(void) kind;

	ok = g_file_make_symbolic_link (link, target, NULL, error);
	g_object_unref (link);

	return ok;
#endif
}

guint
nemo_link_kinds_supported (const char *dir_path)
{
	if (dir_path == NULL) {
		return 0;
	}

#ifdef G_OS_WIN32
	return nemo_win32_link_kinds_supported (dir_path);
#else
	/* Nothing short of trying says whether a given file system will take a
	   symlink, and a refusal comes back as an ordinary copy error. */
	return NEMO_LINK_FILE_SYMLINK | NEMO_LINK_DIR_SYMLINK;
#endif
}

static NemoLinkKind
first_allowed (const NemoLinkKind *order,
               guint               supported)
{
	int i;

	for (i = 0; order[i] != NEMO_LINK_NONE; i++) {
		if (supported & order[i]) {
			return order[i];
		}
	}

	return NEMO_LINK_NONE;
}

void
nemo_link_choice_init (NemoLinkChoice *choice,
                       guint           supported)
{
	/* Same kind first, then the nearest kind that still points at the original
	   target, then a plain copy. */
	static const NemoLinkKind file_order[] = {
		NEMO_LINK_FILE_SYMLINK, NEMO_LINK_NONE
	};
	static const NemoLinkKind dir_sym_order[] = {
		NEMO_LINK_DIR_SYMLINK, NEMO_LINK_JUNCTION, NEMO_LINK_NONE
	};
	static const NemoLinkKind junction_order[] = {
		NEMO_LINK_JUNCTION, NEMO_LINK_DIR_SYMLINK, NEMO_LINK_NONE
	};

	choice->file_symlink_as = first_allowed (file_order, supported);
	choice->dir_symlink_as = first_allowed (dir_sym_order, supported);
	choice->junction_as = first_allowed (junction_order, supported);
}

guint
nemo_link_counts_kinds (const NemoLinkCounts *counts)
{
	guint kinds = 0;

	if (counts->file_symlinks > 0) {
		kinds |= NEMO_LINK_FILE_SYMLINK;
	}
	if (counts->dir_symlinks > 0) {
		kinds |= NEMO_LINK_DIR_SYMLINK;
	}
	if (counts->junctions > 0) {
		kinds |= NEMO_LINK_JUNCTION;
	}

	return kinds;
}

gboolean
nemo_link_choice_makes_links (const NemoLinkChoice *choice)
{
	return choice->file_symlink_as != NEMO_LINK_NONE ||
	       choice->dir_symlink_as != NEMO_LINK_NONE ||
	       choice->junction_as != NEMO_LINK_NONE;
}

NemoLinkKind
nemo_link_choice_for (const NemoLinkChoice *choice,
                      NemoLinkKind          found)
{
	switch (found) {
	case NEMO_LINK_FILE_SYMLINK:
		return choice->file_symlink_as;
	case NEMO_LINK_DIR_SYMLINK:
		return choice->dir_symlink_as;
	case NEMO_LINK_JUNCTION:
		return choice->junction_as;
	default:
		return NEMO_LINK_NONE;
	}
}

/* One column per kind, so the rows line up whatever they offer. The junction
   column is only built where junctions exist at all. */
enum {
	COLUMN_SYMLINK,
	COLUMN_JUNCTION,
	COLUMN_COPY,
	N_COLUMNS
};

typedef struct {
	NemoLinkKind  found;
	guint         count;
	NemoLinkKind  offers[N_COLUMNS];
	GtkWidget    *buttons[N_COLUMNS];
} Row;

const char *
nemo_link_choice_row_label (NemoLinkKind found,
                            guint        count)
{
	switch (found) {
	case NEMO_LINK_FILE_SYMLINK:
		return ngettext ("File symlink:", "File symlinks:", count);
	case NEMO_LINK_DIR_SYMLINK:
		return ngettext ("Folder symlink:", "Folder symlinks:", count);
	default:
		return ngettext ("Folder junction:", "Folder junctions:", count);
	}
}

/* Said as what happens to this link, since "Symlink" beside a symlink read as
   though a new one would be made. */
const char *
nemo_link_choice_label (NemoLinkKind found,
                        NemoLinkKind offer,
                        guint        count,
                        gboolean     is_move)
{
	if (offer == NEMO_LINK_NONE) {
		/* A folder always has contents, and one file has content. */
		if (found == NEMO_LINK_FILE_SYMLINK) {
			return ngettext ("Copy content", "Copy contents", count);
		}
		return _("Copy contents");
	}
	if (offer == found) {
		return is_move ? ngettext ("Move link as-is", "Move links as-is", count)
			       : ngettext ("Copy link as-is", "Copy links as-is", count);
	}
	if (offer == NEMO_LINK_JUNCTION) {
		return is_move ? ngettext ("Move as a junction", "Move as junctions", count)
			       : ngettext ("Copy as a junction", "Copy as junctions", count);
	}
	return is_move ? ngettext ("Move as a symlink", "Move as symlinks", count)
		       : ngettext ("Copy as a symlink", "Copy as symlinks", count);
}

const char *
nemo_link_choice_tooltip (NemoLinkKind found,
                          NemoLinkKind offer)
{
	if (offer != NEMO_LINK_NONE || found == NEMO_LINK_FILE_SYMLINK) {
		return NULL;
	}
#ifdef G_OS_WIN32
	return _("Links inside are copied as links, and junctions as junctions. They are not followed.");
#else
	return _("Links inside are copied as links. They are not followed.");
#endif
}

static void
add_row (GtkGrid      *grid,
         int           at,
         Row          *row,
         guint         supported,
         gboolean      with_junctions,
         gboolean      is_move,
         NemoLinkKind  selected)
{
	GtkWidget *label;
	GtkWidget *group = NULL;
	const char *text;
	int column;

	label = gtk_label_new (nemo_link_choice_row_label (row->found, row->count));
	gtk_widget_set_halign (label, GTK_ALIGN_START);
	gtk_grid_attach (grid, label, 0, at, 1, 1);

	for (column = 0; column < N_COLUMNS; column++) {
		NemoLinkKind offer = row->offers[column];
		GtkWidget *button;

		row->buttons[column] = NULL;

		if (column == COLUMN_JUNCTION && !with_junctions) {
			continue;
		}
		if (offer == NEMO_LINK_NONE && column != COLUMN_COPY) {
			continue;
		}

		text = nemo_link_choice_label (row->found, offer, row->count, is_move);
		button = gtk_radio_button_new_with_label_from_widget (
				group ? GTK_RADIO_BUTTON (group) : NULL, text);
		if (group == NULL) {
			group = button;
		}

		/* Where nothing can be kept as a link, the ones inside get followed
		   too, and the note above the rows already says so. */
		text = nemo_link_choice_tooltip (row->found, offer);
		if (text != NULL && supported != 0) {
			gtk_widget_set_tooltip_text (button, text);
		}

		/* A copy is always possible, except on a move, which takes a link as
		   the link. The rest need the destination's blessing. */
		if (column == COLUMN_COPY ? is_move : !(supported & offer)) {
			gtk_widget_set_sensitive (button, FALSE);
		}
		if (offer == selected) {
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button), TRUE);
		}

		gtk_grid_attach (grid, button, column + 1, at, 1, 1);
		row->buttons[column] = button;
	}
}

static NemoLinkKind
read_row (const Row *row)
{
	int column;

	for (column = 0; column < N_COLUMNS; column++) {
		if (row->buttons[column] != NULL &&
		    gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (row->buttons[column]))) {
			return row->offers[column];
		}
	}

	return NEMO_LINK_NONE;
}

gboolean
nemo_link_choice_ask (GtkWindow            *parent,
                      GFile                *destination,
                      const NemoLinkCounts *counts,
                      guint                 supported,
                      gboolean              is_move,
                      NemoLinkChoice       *choice)
{
	guint present = nemo_link_counts_kinds (counts);
	GtkWidget *dialog, *area, *box, *grid, *note;
	Row rows[3];
	gboolean with_junctions;
	int used = 0;
	int i;
	char *dest_name;
	char *secondary;
	int response;

	nemo_link_choice_init (choice, supported);
	with_junctions = ((present | supported) & NEMO_LINK_JUNCTION) != 0;

	dest_name = destination ? g_file_get_basename (destination) : NULL;
	if (dest_name != NULL) {
		secondary = g_strdup_printf (_("Choose what to put in \"%s\"."), dest_name);
	} else {
		secondary = g_strdup (_("Choose what to put in the destination folder."));
	}
	g_free (dest_name);

	dialog = gtk_message_dialog_new (parent, 0, GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE, NULL);
	g_object_set (dialog,
		      "text", _("Some of these are links"),
		      "secondary-text", secondary,
		      NULL);
	g_free (secondary);

	gtk_dialog_add_button (GTK_DIALOG (dialog), GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);
	gtk_dialog_add_button (GTK_DIALOG (dialog),
			       is_move ? _("_Move") : _("_Copy"),
			       GTK_RESPONSE_OK);
	gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);

	if (present & NEMO_LINK_FILE_SYMLINK) {
		Row row = { NEMO_LINK_FILE_SYMLINK, counts->file_symlinks,
			    { NEMO_LINK_FILE_SYMLINK, NEMO_LINK_NONE, NEMO_LINK_NONE },
			    { NULL, NULL, NULL } };
		rows[used++] = row;
	}
	if (present & NEMO_LINK_DIR_SYMLINK) {
		Row row = { NEMO_LINK_DIR_SYMLINK, counts->dir_symlinks,
			    { NEMO_LINK_DIR_SYMLINK, NEMO_LINK_JUNCTION, NEMO_LINK_NONE },
			    { NULL, NULL, NULL } };
		rows[used++] = row;
	}
	if (present & NEMO_LINK_JUNCTION) {
		Row row = { NEMO_LINK_JUNCTION, counts->junctions,
			    { NEMO_LINK_DIR_SYMLINK, NEMO_LINK_JUNCTION, NEMO_LINK_NONE },
			    { NULL, NULL, NULL } };
		rows[used++] = row;
	}

	box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);

	/* Say why an option is grayed out, for the two reasons a whole column can
	   be. Junctions without symlinks is only ever Windows without the
	   privilege; nothing at all is a file system that keeps no links. */
	if (is_move && supported == 0) {
		note = gtk_label_new (_("This folder cannot hold links, so the links cannot be moved here."));
	} else if (supported == 0) {
		note = gtk_label_new (_("This folder cannot hold links, so only a copy is possible."));
	} else if ((supported & NEMO_LINK_JUNCTION) && !(supported & NEMO_LINK_DIR_SYMLINK)) {
		note = gtk_label_new (_("Symlinks need Developer Mode turned on, or nemo running as administrator."));
	} else if (is_move) {
		note = gtk_label_new (_("A link is moved as a link. Only a copy can take what it points at."));
	} else {
		note = NULL;
	}
	if (note != NULL) {
		gtk_widget_set_halign (note, GTK_ALIGN_START);
		gtk_box_pack_start (GTK_BOX (box), note, FALSE, FALSE, 0);
	}

	grid = gtk_grid_new ();
	gtk_widget_set_halign (grid, GTK_ALIGN_START);
	gtk_grid_set_column_spacing (GTK_GRID (grid), 12);
	gtk_grid_set_row_spacing (GTK_GRID (grid), 6);
	for (i = 0; i < used; i++) {
		add_row (GTK_GRID (grid), i, &rows[i], supported, with_junctions, is_move,
			 nemo_link_choice_for (choice, rows[i].found));
	}
	gtk_box_pack_start (GTK_BOX (box), grid, FALSE, FALSE, 0);

	area = gtk_message_dialog_get_message_area (GTK_MESSAGE_DIALOG (dialog));
	gtk_box_pack_start (GTK_BOX (area), box, FALSE, FALSE, 6);
	gtk_widget_show_all (box);

	response = gtk_dialog_run (GTK_DIALOG (dialog));

	if (response == GTK_RESPONSE_OK) {
		for (i = 0; i < used; i++) {
			NemoLinkKind picked = read_row (&rows[i]);

			switch (rows[i].found) {
			case NEMO_LINK_FILE_SYMLINK:
				choice->file_symlink_as = picked;
				break;
			case NEMO_LINK_DIR_SYMLINK:
				choice->dir_symlink_as = picked;
				break;
			default:
				choice->junction_as = picked;
				break;
			}
		}
	}

	gtk_widget_destroy (dialog);

	return response == GTK_RESPONSE_OK;
}

gboolean
nemo_link_create_hard (const char  *existing_path,
                       const char  *link_path,
                       GError     **error)
{
#ifdef G_OS_WIN32
	return nemo_win32_link_create_hard (existing_path, link_path, error);
#else
	int saved;

	/* A plain link() gives a second name to a symlink rather than to its
	   file, and a relative one then dangles from anywhere else. */
	if (linkat (AT_FDCWD, existing_path, AT_FDCWD, link_path, AT_SYMLINK_FOLLOW) == 0) {
		return TRUE;
	}

	saved = errno;
	if (saved == EXDEV) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
				     _("A hardlink has to be on the same drive as the original."));
	} else {
		g_set_error_literal (error, G_IO_ERROR, g_io_error_from_errno (saved),
				     g_strerror (saved));
	}

	return FALSE;
#endif
}

/* The folder as it really is. A folder that cannot be resolved is left as
   spelled, cleaned up. */
static char *
real_dir (const char *dir)
{
#ifndef G_OS_WIN32
	char *real = realpath (dir, NULL);

	if (real != NULL) {
		char *copy = g_strdup (real);

		free (real);
		return copy;
	}
#endif
	return g_canonicalize_filename (dir, NULL);
}

static char **
split_path (const char *path)
{
	char **parts = g_strsplit_set (path, G_DIR_SEPARATOR_S "/", -1);
	GPtrArray *kept = g_ptr_array_new ();
	int i;

	for (i = 0; parts[i] != NULL; i++) {
		if (parts[i][0] != '\0') {
			g_ptr_array_add (kept, g_strdup (parts[i]));
		}
	}
	g_strfreev (parts);
	g_ptr_array_add (kept, NULL);

	return (char **) g_ptr_array_free (kept, FALSE);
}

static gboolean
same_part (const char *a, const char *b)
{
#ifdef G_OS_WIN32
	char *fa = g_utf8_casefold (a, -1);
	char *fb = g_utf8_casefold (b, -1);
	gboolean same = strcmp (fa, fb) == 0;

	g_free (fa);
	g_free (fb);
	return same;
#else
	return strcmp (a, b) == 0;
#endif
}

#ifdef G_OS_WIN32
/* How many leading parts of a path are its root: the drive, or the server and
   share, after "?" and "UNC" in the long form. */
static int
root_parts (const char *path)
{
	gboolean two_slashes = G_IS_DIR_SEPARATOR (path[0]) && G_IS_DIR_SEPARATOR (path[1]);

	if (two_slashes && (path[2] == '?' || path[2] == '.') && G_IS_DIR_SEPARATOR (path[3])) {
		return g_ascii_strncasecmp (path + 4, "UNC", 3) == 0 && G_IS_DIR_SEPARATOR (path[7]) ? 4 : 2;
	}

	return two_slashes ? 2 : 1;
}
#endif

/* The steps from dir to target, both taken as spelled: up past what they do
   not share, then down. */
static char *
spell_relative (const char *dir, const char *target)
{
	char **from = split_path (dir);
	char **to = split_path (target);
	GString *text;
	int common = 0, i;

	while (from[common] != NULL && to[common] != NULL && same_part (from[common], to[common])) {
		common++;
	}

#ifdef G_OS_WIN32
	/* There is no way up and over the drive, or over the server and share of
	   a UNC path. */
	if (common < root_parts (dir) || common < root_parts (target)) {
		g_strfreev (from);
		g_strfreev (to);
		return NULL;
	}
#endif

	text = g_string_new (NULL);
	for (i = common; from[i] != NULL; i++) {
		g_string_append (text, text->len > 0 ? G_DIR_SEPARATOR_S ".." : "..");
	}
	for (i = common; to[i] != NULL; i++) {
		if (text->len > 0) {
			g_string_append (text, G_DIR_SEPARATOR_S);
		}
		g_string_append (text, to[i]);
	}
	if (text->len == 0) {
		g_string_append (text, ".");
	}
	g_strfreev (from);
	g_strfreev (to);

	return g_string_free (text, FALSE);
}

#ifndef G_OS_WIN32
/* Whether text, followed from the folder the link really sits in, comes out
   at real_target. The kernel takes each ".." from where the folder before it
   really is, and realpath walks the same way. */
static gboolean
leads_to (const char *real_link_dir, const char *text, const char *real_target)
{
	char *joined = g_build_filename (real_link_dir, text, NULL);
	char *parent = g_path_get_dirname (joined);
	char *base = g_path_get_basename (joined);
	char *real = NULL;
	gboolean same = FALSE;

	/* A text ending in a dot or two can only name a folder above, which is
	   no link, so both ends can be resolved whole. */
	if (strcmp (base, ".") == 0 || strcmp (base, "..") == 0) {
		char *want = realpath (real_target, NULL);

		real = realpath (joined, NULL);
		same = real != NULL && want != NULL && strcmp (real, want) == 0;
		free (want);
	} else {
		real = realpath (parent, NULL);
		if (real != NULL) {
			char *arrived = g_build_filename (real, base, NULL);

			same = strcmp (arrived, real_target) == 0;
			g_free (arrived);
		}
	}
	free (real);
	g_free (joined);
	g_free (parent);
	g_free (base);

	return same;
}
#endif

char *
nemo_link_relative_target (const char *target_path,
                           const char *dir)
{
	char *target_parent, *target_base, *real_parent, *real_target, *real_link_dir;
	char *answer;

	/* The target itself is not resolved, since it may be a link that is
	   meant to be pointed at. */
	target_parent = g_path_get_dirname (target_path);
	target_base = g_path_get_basename (target_path);
	real_parent = real_dir (target_parent);
	real_target = g_build_filename (real_parent, target_base, NULL);
	real_link_dir = real_dir (dir);

	/* From the real folders it always works, but where a symlinked folder
	   sits on either path the two real paths can have nothing in common but
	   the root. The paths as spelled usually share more, so each mix is tried
	   and the shortest that still arrives wins. */
	answer = spell_relative (real_link_dir, real_target);
#ifndef G_OS_WIN32
	{
		char *spelled_dir = g_canonicalize_filename (dir, NULL);
		char *spelled_target = g_canonicalize_filename (target_path, NULL);
		/* Later ones win a tie, so the spelling the folders are seen by
		   comes first. */
		const char *mixes[][2] = {
			{ spelled_dir, real_target },
			{ real_link_dir, spelled_target },
			{ spelled_dir, spelled_target },
		};
		guint i;

		for (i = 0; i < G_N_ELEMENTS (mixes); i++) {
			char *text = spell_relative (mixes[i][0], mixes[i][1]);

			if (strlen (text) <= strlen (answer) && strcmp (text, answer) != 0 &&
			    leads_to (real_link_dir, text, real_target)) {
				g_free (answer);
				answer = text;
			} else {
				g_free (text);
			}
		}
		g_free (spelled_dir);
		g_free (spelled_target);
	}
#endif

	g_free (target_parent);
	g_free (target_base);
	g_free (real_parent);
	g_free (real_target);
	g_free (real_link_dir);

	return answer;
}

void
nemo_link_options_initial (guint            supported,
                           NemoLinkOptions *options)
{
	/* A hardlink is never picked for anyone. It is the one choice here that
	   can cost something, so it is only ever chosen. A shortcut can always be
	   made, so it is the fallback. */
	if (supported & NEMO_LINK_JUNCTION) {
		options->folder_kind = NEMO_MAKE_JUNCTION;
	} else if (supported & NEMO_LINK_DIR_SYMLINK) {
		options->folder_kind = NEMO_MAKE_SYMLINK;
	} else {
		options->folder_kind = NEMO_MAKE_SHORTCUT;
	}
	options->file_kind = (supported & NEMO_LINK_FILE_SYMLINK) ? NEMO_MAKE_SYMLINK
								 : NEMO_MAKE_SHORTCUT;
	options->relative = FALSE;
	options->lnk_parts = NEMO_LNK_ALL_PARTS;
}

/* A junction is always absolute, a hardlink has no path at all, and a
   shortcut can carry both kinds at once. */
static gboolean
kind_uses_path (NemoMakeLink kind)
{
	return kind == NEMO_MAKE_SYMLINK;
}

gboolean
nemo_link_options_uses_path (const NemoLinkOptions *options,
                             int                    n_folders,
                             int                    n_files)
{
	return (n_folders > 0 && kind_uses_path (options->folder_kind)) ||
	       (n_files > 0 && kind_uses_path (options->file_kind));
}

typedef struct {
	GtkWidget *dialog;
	GtkWidget *folder_junction;
	GtkWidget *folder_shortcut;
	GtkWidget *file_hardlink;
	GtkWidget *file_shortcut;
	GtkWidget *relative;
	GtkWidget *absolute;
	GtkWidget *path_label;
	int        n_folders;
	int        n_files;
	guint      supported;
} MakeLinkDialog;

static gboolean
is_active (GtkWidget *button)
{
	return button != NULL && gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (button));
}

static void
read_options (MakeLinkDialog *d, NemoLinkOptions *options)
{
	options->folder_kind = is_active (d->folder_junction) ? NEMO_MAKE_JUNCTION
			     : is_active (d->folder_shortcut) ? NEMO_MAKE_SHORTCUT
			     : NEMO_MAKE_SYMLINK;
	options->file_kind = is_active (d->file_hardlink) ? NEMO_MAKE_HARDLINK
			   : is_active (d->file_shortcut) ? NEMO_MAKE_SHORTCUT
			   : NEMO_MAKE_SYMLINK;
	options->relative = is_active (d->relative);
	options->lnk_parts = NEMO_LNK_ALL_PARTS;
}

static void
update_make_link_dialog (GtkToggleButton *button, MakeLinkDialog *d)
{
	NemoLinkOptions options;
	GtkWidget *path_row[3] = { d->path_label, d->absolute, d->relative };
	gboolean uses_path, folders_ok, files_ok;
	guint i;

	read_options (d, &options);

	/* Hidden rather than grayed, but still laid out, so the dialog does not
	   change height as the choices change. Insensitive too, which keeps
	   focus and mnemonics off it. */
	uses_path = nemo_link_options_uses_path (&options, d->n_folders, d->n_files);
	for (i = 0; i < G_N_ELEMENTS (path_row); i++) {
		gtk_widget_set_sensitive (path_row[i], uses_path);
		gtk_widget_set_child_visible (path_row[i], uses_path);
	}

	/* Nothing is made until every row holds a choice this folder allows. */
	folders_ok = d->n_folders == 0 || options.folder_kind == NEMO_MAKE_SHORTCUT ||
		     (d->supported & (options.folder_kind == NEMO_MAKE_JUNCTION
				      ? NEMO_LINK_JUNCTION : NEMO_LINK_DIR_SYMLINK));
	files_ok = d->n_files == 0 || options.file_kind != NEMO_MAKE_SYMLINK ||
		   (d->supported & NEMO_LINK_FILE_SYMLINK);
	gtk_dialog_set_response_sensitive (GTK_DIALOG (d->dialog), GTK_RESPONSE_OK,
					   folders_ok && files_ok);
}

static GtkWidget *
add_choice (GtkGrid *grid, int row, int column, GtkWidget *group,
	    const char *label, gboolean allowed, gboolean active)
{
	GtkWidget *button;

	button = gtk_radio_button_new_with_mnemonic_from_widget (
			group != NULL ? GTK_RADIO_BUTTON (group) : NULL, label);
	gtk_widget_set_sensitive (button, allowed);
	if (active) {
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button), TRUE);
	}
	gtk_grid_attach (grid, button, column, row, 1, 1);

	return button;
}

/* A warning sign after the button's label, so the risky choice stands out
   before anyone reads its tooltip. The theme's icon, so it follows light
   and dark. */
static void
add_warning_icon (GtkWidget *button)
{
	GtkWidget *label = gtk_bin_get_child (GTK_BIN (button));
	GtkWidget *box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 4);
	GtkWidget *icon = gtk_image_new_from_icon_name ("dialog-warning-symbolic", GTK_ICON_SIZE_MENU);

	g_object_ref (label);
	gtk_container_remove (GTK_CONTAINER (button), label);
	gtk_box_pack_start (GTK_BOX (box), label, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (box), icon, FALSE, FALSE, 0);
	gtk_container_add (GTK_CONTAINER (button), box);
	g_object_unref (label);
}

static GtkWidget *
add_row_label (GtkGrid *grid, int row, const char *text)
{
	GtkWidget *label = gtk_label_new (text);

	gtk_widget_set_halign (label, GTK_ALIGN_START);
	gtk_grid_attach (grid, label, 0, row, 1, 1);

	return label;
}

static GtkWidget *
warning_text (const char *text)
{
	GtkWidget *label = gtk_label_new (text);

	gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
	gtk_label_set_max_width_chars (GTK_LABEL (label), 60);
	gtk_label_set_xalign (GTK_LABEL (label), 0.0);
	gtk_label_set_selectable (GTK_LABEL (label), TRUE);
	/* A selectable label takes the focus from Cancel otherwise, as eel's
	   dialogs note. Copying still works with the pointer. */
	gtk_widget_set_can_focus (label, FALSE);

	return label;
}

/* Asked every time, with Cancel first. A hardlink is the one kind of link
   that can quietly ruin work, years later, with nothing to show it happened.
   The list is a grid rather than text so a wrapped line starts under the
   words, not under the bullet. */
static gboolean
confirm_hardlinks (GtkWindow *parent, int n_files)
{
	const char *risks[] = {
		N_("An edit through either name changes both. Hours of work on one document "
		   "can quietly change a \"different\" one nobody has opened in years, and it "
		   "may not be found until backups of the original are gone."),
		N_("Many programs save by replacing the file. That splits the two apart "
		   "without a word, and later edits no longer match."),
		N_("Permissions and dates are shared. A change to one name is a change to all."),
		N_("Deleting one name frees no space until every name is gone."),
		N_("Copying to another drive, zipping, cloud sync and many backups and "
		   "restores turn each name back into a full, separate copy."),
	};
	GtkDialog *dialog;
	GtkWidget *box, *grid, *label, *area;
	GList *children;
	PangoFontMetrics *metrics;
	int line, response;
	guint i;

	dialog = eel_create_question_dialog (
		ngettext ("Make a hardlink anyway?", "Make hardlinks anyway?", n_files),
		/* As the secondary text, since without any GTK draws the title
		   small, unlike every other question. */
		_("A hardlink is not a copy, and not a pointer either. It is the same file "
		  "under a second name, and nothing on screen shows which files are tied "
		  "together."),
		GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
		ngettext ("Make _hardlink", "Make _hardlinks", n_files), GTK_RESPONSE_OK,
		parent);
	g_object_set (dialog, "message-type", GTK_MESSAGE_WARNING, NULL);
	gtk_dialog_set_default_response (dialog, GTK_RESPONSE_CANCEL);

	/* Spacing in lines of the dialog's own font, so it scales with it. */
	metrics = pango_context_get_metrics (gtk_widget_get_pango_context (GTK_WIDGET (dialog)),
					     NULL, NULL);
	line = PANGO_PIXELS (pango_font_metrics_get_ascent (metrics) +
			     pango_font_metrics_get_descent (metrics));
	pango_font_metrics_unref (metrics);

	box = gtk_box_new (GTK_ORIENTATION_VERTICAL, line);

	grid = gtk_grid_new ();
	gtk_grid_set_row_spacing (GTK_GRID (grid), line * 2 / 3);
	gtk_grid_set_column_spacing (GTK_GRID (grid), line / 2);
	for (i = 0; i < G_N_ELEMENTS (risks); i++) {
		label = gtk_label_new ("\xe2\x80\xa2");  /* bullet */
		gtk_widget_set_valign (label, GTK_ALIGN_START);
		gtk_grid_attach (GTK_GRID (grid), label, 0, i, 1, 1);

		label = warning_text (_(risks[i]));
		gtk_widget_set_hexpand (label, TRUE);
		gtk_grid_attach (GTK_GRID (grid), label, 1, i, 1, 1);
	}
	gtk_box_pack_start (GTK_BOX (box), grid, FALSE, FALSE, 0);

	gtk_box_pack_start (GTK_BOX (box),
			    warning_text (_("Hardlinks are only safe where they plainly mean the same "
					    "file, as in backup tools that keep versions of whole folder "
					    "trees. Files that just happen to have the same content are "
					    "not that.")),
			    FALSE, FALSE, 0);

	area = gtk_message_dialog_get_message_area (GTK_MESSAGE_DIALOG (dialog));

	/* GTK centers the secondary text as a block, which leaves it a few
	   pixels off the list below. The title is the first label; leave it. */
	children = gtk_container_get_children (GTK_CONTAINER (area));
	if (children != NULL && children->next != NULL && GTK_IS_LABEL (children->next->data)) {
		gtk_label_set_xalign (GTK_LABEL (children->next->data), 0.0);
		gtk_widget_set_halign (GTK_WIDGET (children->next->data), GTK_ALIGN_START);
	}
	g_list_free (children);

	gtk_box_pack_start (GTK_BOX (area), box, FALSE, FALSE, line / 2);
	gtk_widget_show_all (box);

	response = gtk_dialog_run (dialog);
	gtk_widget_destroy (GTK_WIDGET (dialog));

	return response == GTK_RESPONSE_OK;
}

static void
set_shortcut_tooltip (GtkWidget *button)
{
	gtk_widget_set_tooltip_text (button,
		_("A Windows shortcut file (.lnk) that opens a folder, file or program at its "
		  "original location. On Windows, it's limited to programs that use the Windows "
		  "shell library, such as Explorer (and Nemo Anywhere). Off Windows, only Nemo "
		  "Anywhere can use them. Everything else sees a small file of that name."));
}

static void
set_symlink_tooltip (GtkWidget *button)
{
#ifdef G_OS_WIN32
	gtk_widget_set_tooltip_text (button,
		_("Points to the original by its path. Programs treat it as the original. "
		  "The computer opening the link follows the path, so over a network share an "
		  "absolute one can lead to that computer's own disk, and Windows skips symlinks "
		  "on shares by default. Making one needs Developer Mode or admin rights."));
#else
	gtk_widget_set_tooltip_text (button,
		_("Points to the original by its path. Programs treat it as the original. "
		  "The computer opening the link follows the path, so over a network share an "
		  "absolute one can lead to that computer's own disk, and Windows skips symlinks "
		  "on shares by default."));
#endif
}

gboolean
nemo_link_options_ask (GtkWindow       *parent,
                       GFile           *destination,
                       int              n_folders,
                       int              n_files,
                       gboolean         say_where,
                       NemoLinkOptions *options)
{
	MakeLinkDialog d = { 0 };
	GtkWidget *area, *box, *grid, *note;
	GList *children, *l;
	char *dest_path, *title, *text;
	const char *why = NULL;
	gboolean both = n_folders > 0 && n_files > 0;
	int total = n_folders + n_files;
	int row = 0;
	int response;

	d.n_folders = n_folders;
	d.n_files = n_files;

	dest_path = destination != NULL ? g_file_get_path (destination) : NULL;
#ifdef G_OS_WIN32
	d.supported = nemo_link_kinds_supported (dest_path);
#else
	/* A remote folder can take a symlink as well, through GIO. */
	d.supported = NEMO_LINK_FILE_SYMLINK | NEMO_LINK_DIR_SYMLINK;
#endif
	g_free (dest_path);

	nemo_link_options_initial (d.supported, options);

	if (total == 1) {
		title = g_strdup (_("Make a link"));
	} else {
		title = g_strdup_printf (ngettext ("Make links to %d item", "Make links to %d items", total), total);
	}

	d.dialog = gtk_dialog_new_with_buttons (title, parent,
						GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
						GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
						ngettext ("_Make link", "_Make links", total), GTK_RESPONSE_OK,
						NULL);
	gtk_window_set_resizable (GTK_WINDOW (d.dialog), FALSE);
	g_free (title);
	gtk_dialog_set_default_response (GTK_DIALOG (d.dialog), GTK_RESPONSE_OK);

	grid = gtk_grid_new ();
	gtk_widget_set_halign (grid, GTK_ALIGN_START);
	gtk_grid_set_column_spacing (GTK_GRID (grid), 12);
	gtk_grid_set_row_spacing (GTK_GRID (grid), 6);

	if (n_folders > 0) {
		GtkWidget *group = NULL;
		int column = 1;

		if (both) {
			text = g_strdup_printf (ngettext ("%d folder:", "%d folders:", n_folders), n_folders);
			add_row_label (GTK_GRID (grid), row, text);
			g_free (text);
		} else {
			add_row_label (GTK_GRID (grid), row, _("Link type:"));
		}
#ifdef G_OS_WIN32
		d.folder_junction = add_choice (GTK_GRID (grid), row, column++, NULL,
						ngettext ("_Junction", "_Junctions", n_folders),
						(d.supported & NEMO_LINK_JUNCTION) != 0,
						options->folder_kind == NEMO_MAKE_JUNCTION);
		gtk_widget_set_tooltip_text (d.folder_junction,
			_("Points to a folder by its full path. It is followed by the computer that "
			  "holds it, so it works the same for everyone on a network share, and needs "
			  "no special rights. Folders only, on a drive of this computer, and only on "
			  "NTFS or ReFS."));
		group = d.folder_junction;
#endif
		group = add_choice (GTK_GRID (grid), row, column++, group,
				    ngettext ("_Symlink", "_Symlinks", n_folders),
				    (d.supported & NEMO_LINK_DIR_SYMLINK) != 0,
				    options->folder_kind == NEMO_MAKE_SYMLINK);
		set_symlink_tooltip (group);
		/* Under the file row's Shortcut when both rows show, so the two line up
		   rather than sitting under Hardlink. */
		d.folder_shortcut = add_choice (GTK_GRID (grid), row, both ? 3 : column, group,
						ngettext ("Sh_ortcut", "Sh_ortcuts", n_folders),
						TRUE, options->folder_kind == NEMO_MAKE_SHORTCUT);
		set_shortcut_tooltip (d.folder_shortcut);
		row++;
	}

	if (n_files > 0) {
		GtkWidget *symlink;

		if (both) {
			text = g_strdup_printf (ngettext ("%d file:", "%d files:", n_files), n_files);
			add_row_label (GTK_GRID (grid), row, text);
			g_free (text);
		} else {
			add_row_label (GTK_GRID (grid), row, _("Link type:"));
		}
		symlink = add_choice (GTK_GRID (grid), row, 1, NULL,
				      ngettext ("S_ymlink", "S_ymlinks", n_files),
				      (d.supported & NEMO_LINK_FILE_SYMLINK) != 0,
				      options->file_kind == NEMO_MAKE_SYMLINK);
		set_symlink_tooltip (symlink);
		d.file_hardlink = add_choice (GTK_GRID (grid), row, 2, symlink,
					      ngettext ("_Hardlink", "_Hardlinks", n_files),
					      TRUE, options->file_kind == NEMO_MAKE_HARDLINK);
		add_warning_icon (d.file_hardlink);
		gtk_widget_set_tooltip_text (d.file_hardlink,
			_("Careful: a hardlink is a second name for the same file, not a pointer to it. "
			  "Editing through either name changes both, but many programs save by replacing "
			  "the file, which quietly splits the two apart. Space is freed only when every "
			  "name is deleted. Both names have to be on the same drive."));
		d.file_shortcut = add_choice (GTK_GRID (grid), row, 3, symlink,
					      ngettext ("Shortc_ut", "Shortc_uts", n_files),
					      TRUE, options->file_kind == NEMO_MAKE_SHORTCUT);
		set_shortcut_tooltip (d.file_shortcut);
		row++;
	}

	d.path_label = add_row_label (GTK_GRID (grid), row, _("Symlink path:"));
	d.absolute = add_choice (GTK_GRID (grid), row, 1, NULL, _("_Absolute"), TRUE, !options->relative);
	d.relative = add_choice (GTK_GRID (grid), row, 2, d.absolute, _("_Relative"), TRUE, options->relative);
	gtk_widget_set_tooltip_text (d.relative,
		_("Keeps working when the link and the original move together. Stops working "
		  "when either one moves alone."));
	gtk_widget_set_tooltip_text (d.absolute,
		_("Keeps working when the link is moved. Stops working when the original is "
		  "moved, or its drive letter or mount point changes."));
	/* The path is a separate question from the link type, so it sits a group
	   apart. The GNOME HIG puts 12 px between groups and 6 within one. A grid
	   has one row spacing, so the rest goes on the row's widgets. */
	gtk_widget_set_margin_top (d.path_label, 6);
	gtk_widget_set_margin_top (d.absolute, 6);
	gtk_widget_set_margin_top (d.relative, 6);
	row++;

	/* Say why something is grayed out. */
	if (d.supported == 0) {
		why = _("This folder cannot hold symlinks or junctions.");
	} else if (!(d.supported & NEMO_LINK_FILE_SYMLINK)) {
		why = _("Symlinks need Developer Mode turned on, or nemo running as administrator.");
	}

	box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
	gtk_container_set_border_width (GTK_CONTAINER (box), 12);

	/* From the menu the links go in the folder in view, which needs no
	   saying. A drop can be onto another folder, or another window. */
	if (say_where && destination != NULL) {
		char *dest_name = g_file_get_basename (destination);

		text = g_strdup_printf (ngettext ("The new link goes in \"%s\".",
						  "The new links go in \"%s\".", total),
					dest_name != NULL ? dest_name : "");
		note = gtk_label_new (text);
		gtk_widget_set_halign (note, GTK_ALIGN_START);
		gtk_label_set_line_wrap (GTK_LABEL (note), TRUE);
		gtk_box_pack_start (GTK_BOX (box), note, FALSE, FALSE, 0);
		g_free (text);
		g_free (dest_name);
	}

	gtk_box_pack_start (GTK_BOX (box), grid, FALSE, FALSE, 0);
	if (why != NULL) {
		note = gtk_label_new (why);
		gtk_widget_set_halign (note, GTK_ALIGN_START);
		gtk_label_set_line_wrap (GTK_LABEL (note), TRUE);
		gtk_box_pack_start (GTK_BOX (box), note, FALSE, FALSE, 0);
	}

	area = gtk_dialog_get_content_area (GTK_DIALOG (d.dialog));
	gtk_box_pack_start (GTK_BOX (area), box, TRUE, TRUE, 0);
	gtk_widget_show_all (box);

	/* Any change rechecks the lot. */
	children = gtk_container_get_children (GTK_CONTAINER (grid));
	for (l = children; l != NULL; l = l->next) {
		if (GTK_IS_TOGGLE_BUTTON (l->data)) {
			g_signal_connect (l->data, "toggled", G_CALLBACK (update_make_link_dialog), &d);
		}
	}
	g_list_free (children);
	update_make_link_dialog (NULL, &d);

	/* Backing out of the hardlink warning comes back here with the
	   choices as they were, rather than dropping the whole thing. */
	do {
		response = gtk_dialog_run (GTK_DIALOG (d.dialog));
		if (response == GTK_RESPONSE_OK) {
			read_options (&d, options);
		}
	} while (response == GTK_RESPONSE_OK && n_files > 0 && options->file_kind == NEMO_MAKE_HARDLINK &&
		 !confirm_hardlinks (GTK_WINDOW (d.dialog), n_files));

	gtk_widget_destroy (d.dialog);

	return response == GTK_RESPONSE_OK;
}
