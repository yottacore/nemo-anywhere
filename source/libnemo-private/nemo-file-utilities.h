/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-file-utilities.h - interface for file manipulation routines.

   Copyright (C) 1999, 2000, 2001 Eazel, Inc.

   The Gnome Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   The Gnome Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public
   License along with the Gnome Library; see the file COPYING.LIB.  If not,
   write to the Free Software Foundation, Inc., 51 Franklin Street - Suite 500,
   Boston, MA 02110-1335, USA.

   Authors: John Sullivan <sullivan@eazel.com>
*/

#ifndef NEMO_FILE_UTILITIES_H
#define NEMO_FILE_UTILITIES_H

#include <gio/gio.h>
#include <gtk/gtk.h>

/* Used in view classes to mark pinned and favorite files */
#define UNAVAILABLE_TEXT_WEIGHT PANGO_WEIGHT_LIGHT
#define NORMAL_TEXT_WEIGHT PANGO_WEIGHT_NORMAL
#define PINNED_TEXT_WEIGHT PANGO_WEIGHT_BOLD

#define DEFAULT_NEMO_DIRECTORY_MODE (0755)
#define DEFAULT_DESKTOP_DIRECTORY_MODE (0755)

/* The per-user dir our config dir sits in - roaming AppData on Windows,
 * Application Support on macOS, XDG elsewhere. Not freed by the caller. */
const char *nemo_get_user_config_root            (void);

/* The per-user dir our cache dir sits in - local AppData on Windows,
 * ~/Library/Caches on macOS, XDG elsewhere. Not freed by the caller. */
const char *nemo_get_user_cache_root             (void);

/* <cache root>/nemo-anywhere, created if it is not there. Freed by the caller,
 * and NULL if the dir could not be made. */
char *   nemo_get_user_cache_directory           (void);

/* These functions all return something something that needs to be
 * freed with g_free, is not NULL, and is guaranteed to exist.
 */
char *   nemo_get_xdg_dir                        (const char *type);
char *   nemo_get_user_directory                 (void);
char *   nemo_get_desktop_directory              (void);
GFile *  nemo_get_desktop_location               (void);
char *   nemo_get_desktop_directory_uri          (void);
char *   nemo_get_home_directory_uri             (void);
gboolean nemo_is_desktop_directory_file          (GFile *dir,
						      const char *filename);
gboolean nemo_is_root_directory                  (GFile *dir);
gboolean nemo_is_desktop_directory               (GFile *dir);
gboolean nemo_is_home_directory                  (GFile *dir);
gboolean nemo_is_home_directory_file             (GFile *dir,
						      const char *filename);
gboolean nemo_is_in_system_dir                   (GFile *location);
/* TRUE when uri is root itself or lies under it. A bare prefix is not enough:
   ".../ab" is not under ".../a". A root that already ends in '/' (file:///)
   matches by prefix alone. */
gboolean nemo_uri_is_at_or_under                 (const char *uri,
						      const char *root);
char *   nemo_get_gmc_desktop_directory          (void);

gboolean nemo_should_use_templates_directory     (void);
char *   nemo_get_templates_directory            (void);
void     nemo_ensure_valid_templates_directory   (void);
char *   nemo_get_templates_directory_uri        (void);

char *   nemo_get_searches_directory             (void);

char *	 nemo_compute_title_for_location	     (GFile *file);
char *	 nemo_compute_window_title		     (const char *location_title);
char *   nemo_compute_search_title_for_location (GFile *location);
void     nemo_note_process_start                 (void);
gint64   nemo_get_uptime_seconds                 (void);
char *   nemo_format_uptime                      (gint64 seconds);
/* This function returns something that needs to be freed with g_free,
 * is not NULL, but is not garaunteed to exist */
char *   nemo_get_desktop_directory_uri_no_create (void);

/* Locate a file in either the uers directory or the datadir. */
/* Installed data, translations and helper binaries, resolved against the
   running executable so a relocated prefix (and every Windows layout) works.
   Never NULL; owned by nemo. */
const char * nemo_get_data_dir                   (void);
const char * nemo_get_locale_dir                 (void);
const char * nemo_get_bin_dir                    (void);
/* g_get_system_data_dirs without repeated entries. Owned by nemo. */
const char * const * nemo_get_system_data_dirs   (void);
/* The running executable itself. NULL when the platform gives no answer. */
char *       nemo_get_exe_path                   (void);
/* Point the runtime at our own prefix. Must be the first thing main does. */
void         nemo_setup_runtime_environment      (void);

char *   nemo_get_data_file_path                 (const char *partial_path);

gboolean nemo_is_file_roller_installed           (void);

/* Inhibit/Uninhibit GNOME Power Manager */
int    nemo_inhibit_power_manager                (const char *message) G_GNUC_WARN_UNUSED_RESULT;
void     nemo_uninhibit_power_manager            (int cookie);

/* Return an allocated file name that is guranteed to be unique, but
 * tries to make the name readable to users.
 * This isn't race-free, so don't use for security-related things
 */
char *   nemo_ensure_unique_file_name            (const char *directory_uri,
						      const char *base_name,
			                              const char *extension);
char *   nemo_unique_temporary_file_name         (void);

GFile *  nemo_find_existing_uri_in_hierarchy     (GFile *location);

GFile *
nemo_find_file_insensitive (GFile *parent, const gchar *name);

char * nemo_get_accel_map_file (void);

char * nemo_get_scripts_directory_path (void);

GHashTable * nemo_trashed_files_get_original_directories (GList *files,
							      GList **unhandled_files);
void nemo_restore_files_from_trash (GList *files,
					GtkWindow *parent_window);

typedef void (*NemoMountGetContent) (const char **content, gpointer user_data);

char ** nemo_get_cached_x_content_types_for_mount (GMount *mount);
void nemo_get_x_content_types_for_mount_async (GMount *mount,
						   NemoMountGetContent callback,
						   GCancellable *cancellable,
						   gpointer user_data);

gchar *nemo_get_mount_icon_name (GMount *mount);
gchar *nemo_get_volume_icon_name (GVolume *volume);
gchar *nemo_get_drive_icon_name (GDrive *drive);

gchar *nemo_get_best_guess_file_mimetype (const gchar *filename,
                                          GFileInfo   *info,
                                          goffset      size);

gboolean nemo_treating_root_as_normal (void);
gboolean nemo_user_is_root (void);

GMount *nemo_get_mount_for_location_safe (GFile *location);
gboolean nemo_location_is_network_safe (GFile *location);
gboolean nemo_path_is_network_safe (const gchar *path);

/* A drive root such as "C:\". Every surface that names a location asks here, so
   the sidebar, the breadcrumb and the title agree. Always FALSE/NULL off win32. */
gboolean nemo_location_is_drive_root (GFile *location);
gchar *nemo_get_drive_root_name (GFile *location);
gchar *nemo_build_path_list_text (GList *locations, gchar separator);

gchar *nemo_filename_get_extension (const gchar *name);

/* Shorter and shorter spellings of a path for a crowded tab row, longest first.
   home is spelled with the same separator, or NULL for no ~. */
gchar **nemo_path_forms (const gchar *path, gchar separator, const gchar *home);

/* What nemo_path_forms should be given as home here: the home folder, or NULL
   on Windows, where no shell prints a ~ and the drive matters. */
const gchar *nemo_path_display_home (void);

/* Which form fits avail pixels, given one width per form, longest first. */
guint nemo_path_form_for_width (const gint *widths, guint count, gint avail);

/* Which of each tab's forms to show so the row fits in avail pixels. widths[i]
   holds the pixel width of each of tab i's forms, longest first. Tab active is
   the one in front and is not capped at max_px. */
void nemo_path_forms_fit (guint count, const gint *const *widths, const guint *form_counts,
                          guint active, gint min_px, gint max_px, gint avail, guint *chosen);

/* The path a tab spells out when the full-path preference is on, or NULL when
   it is off or the location has no local path. */
gchar *nemo_compute_title_path_for_location (GFile *location);

/* Which separator paths are shown with. Windows takes either, so it is the
   user's choice there; elsewhere there is only one and these do nothing. */
void     nemo_path_init_display_separator (void);
gchar    nemo_path_get_display_separator (void);
gchar    nemo_path_get_other_separator (void);
void     nemo_path_apply_separator (gchar *path, gchar separator);
void     nemo_path_apply_display_separator (gchar *path);
gchar   *nemo_location_get_display_name (GFile *location);
gboolean nemo_path_input_is_allowed (const gchar *text);

/* What a rename selects before anything is typed, in characters: the whole
   name when whole is TRUE or rename-selects-whole-name is on, otherwise the
   name without its extension. An end of -1 is the end of the name. */
void     nemo_rename_region (const char *name,
                             gboolean    whole,
                             int        *start_offset,
                             int        *end_offset);

/* Browsing inside an archive needs the gvfs archive backend. Asked once at
   startup, so the menu item is either there for the whole run or never. */
void     nemo_archive_mount_init (void);
gboolean nemo_archive_mount_supported (void);
GFile   *nemo_archive_mount_location (GFile *archive);

/* g_content_type_is_a, but also right on Windows for a mime pattern such as
   "image/" plus a star. A content type there is a file extension, which never
   matches one.
   Safe from any thread. */
gboolean nemo_content_type_is_a (const char *content_type, const char *mime_type);
#endif /* NEMO_FILE_UTILITIES_H */
