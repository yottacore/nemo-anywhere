/* nemo-link-copy.h - what happens to a link when it is copied somewhere else.
 * Every platform can hold a symlink; Windows adds the junction. The copy engine
 * asks here what a link should become, and the answer comes from a dialog when
 * there is more than one sensible one.
 *
 * Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2, as published by the
 * Free Software Foundation.
 */

#ifndef NEMO_LINK_COPY_H
#define NEMO_LINK_COPY_H

#include <gtk/gtk.h>
#include <gio/gio.h>

G_BEGIN_DECLS

/* A bitmask so a set of files can report every kind it holds at once. NONE
   doubles as "copy what it points at" wherever a choice is being carried. */
typedef enum {
	NEMO_LINK_NONE         = 0,
	NEMO_LINK_FILE_SYMLINK = 1 << 0,
	NEMO_LINK_DIR_SYMLINK  = 1 << 1,
	NEMO_LINK_JUNCTION     = 1 << 2   /* Windows only */
} NemoLinkKind;

#define NEMO_LINK_ANY (NEMO_LINK_FILE_SYMLINK | NEMO_LINK_DIR_SYMLINK | NEMO_LINK_JUNCTION)

/* How many of each kind the source holds, links inside its folders
   included. The dialog words each row to match. */
typedef struct {
	guint file_symlinks;
	guint dir_symlinks;
	guint junctions;
} NemoLinkCounts;

/* The kinds counts holds at least one of, as a NemoLinkKind mask. */
guint nemo_link_counts_kinds (const NemoLinkCounts *counts);

/* What each kind found in the source should become. */
typedef struct {
	NemoLinkKind file_symlink_as;
	NemoLinkKind dir_symlink_as;
	NemoLinkKind junction_as;
} NemoLinkChoice;

/* What kind of link this is, or NONE. info may be NULL, and is only read for
   the answer it already holds - nothing is queried through it. */
NemoLinkKind nemo_link_kind (GFile     *file,
                             GFileInfo *info);

/* What a link points at, spelled the way the link itself spells it, so a
   relative one answers with its relative text. Caller frees. */
gboolean nemo_link_read_target (GFile   *file,
                                char   **target,
                                GError **error);

/* Make a link of the kind asked for. A junction wants an absolute local
   directory, so a relative target is resolved against base_dir first (the
   directory the original link sat in; NULL to skip). */
gboolean nemo_link_create (const char    *target,
                           const char    *link_path,
                           const char    *base_dir,
                           NemoLinkKind   kind,
                           GError       **error);

/* A second name for existing_path, which has to be a file on the same drive.
   A change made through either name shows through the other. A symlink is
   followed, so the second name is for the file it leads to. */
gboolean nemo_link_create_hard (const char  *existing_path,
                                const char  *link_path,
                                GError     **error);

/* target_path as a symlink sitting in dir would spell it relative to itself.
   The answer always holds from the folder the link really sits in. Off
   Windows it is the shortest of the paths as spelled and as they really are,
   links in the folders resolved, so a symlinked folder on the way does not
   send it up to the root when it need not. NULL when no relative spelling
   exists, which on Windows means another drive. */
char    *nemo_link_relative_target (const char *target_path,
                                    const char *dir);

/* Which kinds can be created inside dir_path. */
guint nemo_link_kinds_supported (const char *dir_path);

/* Every kind keeps its own kind where the destination allows it, otherwise the
   nearest one that still points at the same target, otherwise a copy. */
void nemo_link_choice_init (NemoLinkChoice *choice,
                            guint           supported);

/* Whether choice asks for anything other than a plain copy. */
gboolean nemo_link_choice_makes_links (const NemoLinkChoice *choice);

/* What to make for a source of this kind. */
NemoLinkKind nemo_link_choice_for (const NemoLinkChoice *choice,
                                   NemoLinkKind          found);

/* The dialog's words, apart so they can be checked without it. A row names
   the kind found, and a choice says what becomes of it, NONE for a copy of
   what it points at. Both are singular for a count of one. */
const char *nemo_link_choice_row_label (NemoLinkKind found,
                                        guint        count);
const char *nemo_link_choice_label     (NemoLinkKind found,
                                        NemoLinkKind offer,
                                        guint        count,
                                        gboolean     is_move);
/* NULL where the choice needs no more said. */
const char *nemo_link_choice_tooltip   (NemoLinkKind found,
                                        NemoLinkKind offer);

/* Returns FALSE if the operation was cancelled. supported is a NemoLinkKind
   bitmask. */
gboolean nemo_link_choice_ask (GtkWindow            *parent,
                               GFile                *destination,
                               const NemoLinkCounts *counts,
                               guint                 supported,
                               gboolean              is_move,
                               NemoLinkChoice       *choice);

/* A kind the Make link dialog can make. */
typedef enum {
	NEMO_MAKE_SYMLINK,
	NEMO_MAKE_JUNCTION,   /* folders, on Windows */
	NEMO_MAKE_HARDLINK,   /* files */
	NEMO_MAKE_SHORTCUT    /* a Windows .lnk */
} NemoMakeLink;

/* What the Make link dialog asked for. */
typedef struct {
	NemoMakeLink folder_kind;
	NemoMakeLink file_kind;
	gboolean     relative;    /* for a symlink */
} NemoLinkOptions;

/* Where the dialog starts, every time: a junction for folders where the
   destination allows one, else a symlink, else a shortcut; a symlink for files,
   else a shortcut; and an absolute path for a symlink. A shortcut always
   carries every path it can. supported is a NemoLinkKind mask. */
void     nemo_link_options_initial (guint                  supported,
                                    NemoLinkOptions       *options);

/* Whether the relative or absolute choice changes anything that comes out.
   It does only for a symlink: a junction is always absolute, a hardlink has
   no path, and a shortcut always carries every kind. */
gboolean nemo_link_options_uses_path (const NemoLinkOptions *options,
                                      int                    n_folders,
                                      int                    n_files);

/* The Make link dialog. FALSE if canceled. say_where adds a line naming the
   destination, for a drop, where it need not be the folder in view. */
gboolean nemo_link_options_ask (GtkWindow       *parent,
                                GFile           *destination,
                                int              n_folders,
                                int              n_files,
                                gboolean         say_where,
                                NemoLinkOptions *options);

G_END_DECLS

#endif /* NEMO_LINK_COPY_H */
