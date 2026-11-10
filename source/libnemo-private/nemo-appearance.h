/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-appearance.h - light/dark mode and theme selection.

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

/* There are 3 settings behind everything here: appearance.mode (system/light/dark),
 * appearance.gtk-theme and appearance.icon-theme. "system" defers to whatever
 * the platform reports - the Windows AppsUseLightTheme registry value, and on
 * a desktop that already told GTK, GTK's own answer - so it is a no-op where
 * the desktop is already in charge.
 *
 * Themes come from 4 places, searched user-first: the user's own drop-in
 * folder, the folder installed beside the app, the set compiled into the
 * binary, and whatever GTK already knows about. Each is offered to the picker
 * only for the backgrounds it was drawn for; see nemo_appearance_list_themes.
 */

#ifndef NEMO_APPEARANCE_H
#define NEMO_APPEARANCE_H

#include <glib.h>

G_BEGIN_DECLS

typedef enum {
	NEMO_APPEARANCE_MODE_SYSTEM = 0,
	NEMO_APPEARANCE_MODE_LIGHT  = 1,
	NEMO_APPEARANCE_MODE_DARK   = 2
} NemoAppearanceMode;

/* Which backgrounds a theme suits. A theme with no opinion claims both. */
typedef enum {
	NEMO_THEME_FITS_LIGHT = 1 << 0,
	NEMO_THEME_FITS_DARK  = 1 << 1,
	NEMO_THEME_FITS_BOTH  = (1 << 0) | (1 << 1)
} NemoThemeFit;

typedef enum {
	NEMO_THEME_KIND_WIDGET,
	NEMO_THEME_KIND_ICON
} NemoThemeKind;

typedef struct {
	char     *name;			/* directory name - what GTK is told */
	char     *display;		/* label for the picker */
	char     *style;		/* the look it imitates, e.g. "Windows 11" */
	char     *dir;			/* directory holding it, or NULL if GTK found it */
	char     *counterpart;		/* theme to swap to when the mode flips */
	guint     fits;			/* NemoThemeFit bits */
	gboolean  declared;		/* @fits is the theme's own answer, not a guess */
	/* Shipped with the app rather than found anywhere GTK looks - either
	 * compiled into the binary, or dropped beside it. Bundled themes sort
	 * ahead of the rest in the picker. */
	gboolean  bundled;
} NemoThemeInfo;

void     nemo_appearance_init      (void);
void     nemo_appearance_shutdown  (void);

/* The mode in force, with "system" already resolved. */
gboolean nemo_appearance_is_dark   (void);

/* Themes that suit @fits, newly allocated, sorted by display name. Pass
 * NEMO_THEME_FITS_BOTH to list everything. Free with
 * g_list_free_full (list, (GDestroyNotify) nemo_theme_info_free). */
GList   *nemo_appearance_list_themes (NemoThemeKind kind, guint fits);

void     nemo_theme_info_free      (NemoThemeInfo *info);

/* Whichever of @name's variants suits the mode in force, or @name itself.
 * Newly allocated; NULL in, NULL out. */
char    *nemo_appearance_theme_for_mode (NemoThemeKind kind, const char *name);

/* The icon theme drawn to match widget theme @widget_name. Both kinds record
 * the look they imitate, so Fluent's window frames pair with Fluent's icons.
 * NULL when the style has no icon set of its own, or @widget_name is unknown.
 * Newly allocated. */
char    *nemo_appearance_icons_for_widget_theme (const char *widget_name);

/* Where drop-in themes go, user first, NULL-terminated. Owned by us. */
const char * const *nemo_appearance_get_theme_roots (void);

G_END_DECLS

#endif /* NEMO_APPEARANCE_H */
