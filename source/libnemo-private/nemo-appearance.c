/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-appearance.c - light/dark mode and theme selection.

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

#include "nemo-appearance.h"

#include "nemo-config.h"
#include "nemo-file-utilities.h"
#include "nemo-global-preferences.h"

#include <eel/eel-debug.h>
#include <gtk/gtk.h>
#include <string.h>

#ifdef G_OS_WIN32
#include "nemo-win32-appearance.h"
#endif

/* Bundled themes sit beside the app; drop-ins go in the user's own copy of
 * the same two folders. Widget themes keep GTK's layout (<root>/themes/<name>/
 * gtk-3.0/gtk.css) so a theme downloaded for any GTK app works unchanged. */
#define THEME_SUBDIR	"themes"
#define ICON_SUBDIR	"icons"

/* GTK resolves a named widget theme against its own search list only. A theme
 * dropped into one of our folders is not on that list, so we load its CSS
 * ourselves - above GTK's own theme provider, below nemo's application sheet,
 * which is exactly where a theme belongs. */
#define DROPIN_THEME_PRIORITY	GTK_STYLE_PROVIDER_PRIORITY_SETTINGS

/* GTK resolves gtk-theme-name against its own search path and nothing else. A
 * theme we load ourselves is not on that path, so leaving the name pointing at
 * it makes GTK fall back to its packaged default sheet - and that fallback
 * drops the dark variant. The base layer under our sheet then comes up light,
 * and everything the sheet does not itself style reads light in dark mode: the
 * breadcrumb buttons and any checked toolbar button, most visibly. Naming a
 * theme GTK really has keeps that base following the mode. */
#define BASE_WIDGET_THEME	"Adwaita"

/* The bundled set rides inside the binary as one resource rather than as a
 * couple of thousand loose files, which the packed Windows exe would charge
 * for at every launch. Three prefixes, laid out by
 * cicd/utility/gen-theme-resources.py:
 *
 *	icontheme/<name>/...	pointed at with gtk_icon_theme_add_resource_path
 *	widgettheme/<name>/...	CSS we load ourselves, same as a drop-in
 *	catalog/<kind>/<name>/index.theme
 *
 * The catalog exists because GTK never reads index.theme off a resource path -
 * it treats what it finds there as part of hicolor - and ours carry the style,
 * the modes and the light/dark counterpart the picker needs. */
#define BUNDLE_ICONS	"/org/nemo/themes/icontheme"
#define BUNDLE_WIDGETS	"/org/nemo/themes/widgettheme"
#define BUNDLE_CATALOG	"/org/nemo/themes/catalog"

/* Nemo's own art, in the main resource rather than the theme bundle - it is not
 * a theme and is never picked. Installed as loose files too, but the portable
 * Windows bundle stages only the GTK stack's own hicolor, so without this every
 * nemo-* name misses there. See the note in gresources/nemo.gresource.xml. */
#define APP_ICONS	"/org/nemo/appicons"

/* Under whatever the user picked, in this order - Adwaita stopped drawing the
 * emblems and the color mimetypes, and the legacy set is where they went. */
static const char *fallback_icon_themes[] = { "AdwaitaLegacy", "Adwaita", NULL };

static char           **theme_roots;		/* user first, NULL-terminated */
static GtkCssProvider  *dropin_provider;
static gboolean         initialized;
static gboolean         applying;

/* Off Windows the platform's answer IS the GTK property we write to, so a
 * reading taken after we have written one would just echo us back and "system"
 * would latch on the last explicit choice. Keep the desktop's own answer
 * separately, refreshed only when the change came from outside. */
static gboolean         desktop_dark;

/* What the platform had chosen before we touched anything - the Windows bundle
 * names both in its settings.ini, a desktop names them in its own settings.
 * The "Nemo Anywhere" row in the picker means going back to exactly these,
 * without keeping them would leave the last explicit choice in place until the
 * next launch. */
static char            *platform_gtk_theme;
static char            *platform_icon_theme;

/* Where the widget theme in force came from - installed, drop-in, or inside
 * the binary. Reported in the one debug line this box can check a launch by. */
static const char      *widget_theme_source = "none";

/* Roots */

static void
build_theme_roots (void)
{
	GPtrArray *roots;
	char      *user_dir;

	if (theme_roots != NULL) {
		return;
	}

	roots = g_ptr_array_new ();

	user_dir = nemo_get_user_directory ();
	if (user_dir != NULL) {
		g_ptr_array_add (roots, user_dir);
	}

	g_ptr_array_add (roots, g_strdup (nemo_get_data_dir ()));
	g_ptr_array_add (roots, NULL);

	theme_roots = (char **) g_ptr_array_free (roots, FALSE);
}

const char * const *
nemo_appearance_get_theme_roots (void)
{
	build_theme_roots ();
	return (const char * const *) theme_roots;
}

/* The user's drop-in folders are created empty so the place to put a theme is
 * discoverable without documentation. The app's own copy is installed. */
static void
ensure_user_dirs (void)
{
	char *user_dir;
	char *sub;
	int   i;
	const char *subs[] = { THEME_SUBDIR, ICON_SUBDIR };

	user_dir = nemo_get_user_directory ();
	if (user_dir == NULL) {
		return;
	}

	for (i = 0; i < (int) G_N_ELEMENTS (subs); i++) {
		sub = g_build_filename (user_dir, subs[i], NULL);
		if (g_mkdir_with_parents (sub, 0755) != 0) {
			g_debug ("appearance: could not create %s", sub);
		}
		g_free (sub);
	}

	g_free (user_dir);
}

/* index.theme reading */

static guint
parse_modes (const char *value)
{
	guint  fits = 0;
	char **parts;
	int    i;

	parts = g_strsplit_set (value, ";,", -1);
	for (i = 0; parts[i] != NULL; i++) {
		char *token = g_strstrip (parts[i]);

		if (g_ascii_strcasecmp (token, "light") == 0) {
			fits |= NEMO_THEME_FITS_LIGHT;
		} else if (g_ascii_strcasecmp (token, "dark") == 0) {
			fits |= NEMO_THEME_FITS_DARK;
		}
	}
	g_strfreev (parts);

	return fits != 0 ? fits : NEMO_THEME_FITS_BOTH;
}

/* Trailing "-dark" / "_Dark" / " dark" and the light equivalent, case-blind.
 * Returns the stem with the suffix removed, or NULL if there is none. */
static char *
strip_variant_suffix (const char *name, const char *word)
{
	gsize name_len = strlen (name);
	gsize word_len = strlen (word);

	if (name_len <= word_len + 1) {
		return NULL;
	}
	if (g_ascii_strcasecmp (name + name_len - word_len, word) != 0) {
		return NULL;
	}

	switch (name[name_len - word_len - 1]) {
	case '-':
	case '_':
	case ' ':
		return g_strndup (name, name_len - word_len - 1);
	default:
		return NULL;
	}
}

/* Same separator and capitalisation as @sibling_of, so a guessed counterpart
 * matches how the theme's own author spelled it. */
static char *
variant_sibling (const char *name, const char *word, gboolean capitalize)
{
	char *suffix = capitalize ? g_strdup (word) : g_ascii_strdown (word, -1);
	char *out;

	if (capitalize) {
		suffix[0] = g_ascii_toupper (suffix[0]);
	}

	out = g_strconcat (name, "-", suffix, NULL);
	g_free (suffix);

	return out;
}

/* Enumeration */

typedef struct {
	NemoThemeKind kind;
	GHashTable   *by_name;		/* name -> NemoThemeInfo*, first wins */
	GPtrArray    *order;		/* NemoThemeInfo*, discovery order */
} ScanState;

void
nemo_theme_info_free (NemoThemeInfo *info)
{
	if (info == NULL) {
		return;
	}

	g_free (info->name);
	g_free (info->display);
	g_free (info->style);
	g_free (info->dir);
	g_free (info->counterpart);
	g_free (info);
}

static gboolean
widget_theme_at (const char *dir, const char *name, gboolean *has_dark_css)
{
	char     *css;
	gboolean  found;

	css = g_build_filename (dir, name, "gtk-3.0", "gtk.css", NULL);
	found = g_file_test (css, G_FILE_TEST_IS_REGULAR);
	g_free (css);

	if (!found) {
		return FALSE;
	}

	css = g_build_filename (dir, name, "gtk-3.0", "gtk-dark.css", NULL);
	*has_dark_css = g_file_test (css, G_FILE_TEST_IS_REGULAR);
	g_free (css);

	return TRUE;
}

/* An icon theme is one with an index.theme carrying [Icon Theme] and at least
 * one directory. That filters out cursor-only themes and stray folders. */
static GKeyFile *
icon_theme_index (const char *dir, const char *name)
{
	GKeyFile *keys;
	char     *index;
	char     *dirs;

	index = g_build_filename (dir, name, "index.theme", NULL);
	keys = g_key_file_new ();

	if (!g_key_file_load_from_file (keys, index, G_KEY_FILE_NONE, NULL)) {
		g_key_file_free (keys);
		g_free (index);
		return NULL;
	}
	g_free (index);

	dirs = g_key_file_get_string (keys, "Icon Theme", "Directories", NULL);
	if (dirs == NULL || dirs[0] == '\0') {
		g_free (dirs);
		g_key_file_free (keys);
		return NULL;
	}
	g_free (dirs);

	return keys;
}

static void
scan_add (ScanState  *state,
	  const char *dir,
	  const char *name,
	  GKeyFile   *keys,
	  gboolean    has_dark_css,
	  gboolean    bundled)
{
	NemoThemeInfo *info;
	char          *value;

	if (g_hash_table_contains (state->by_name, name)) {
		return;
	}

	info = g_new0 (NemoThemeInfo, 1);
	info->name = g_strdup (name);
	info->dir = g_strdup (dir);
	info->bundled = bundled;
	info->fits = NEMO_THEME_FITS_BOTH;

	if (keys != NULL) {
		const char *group = state->kind == NEMO_THEME_KIND_ICON
			? "Icon Theme" : "Desktop Entry";

		info->display = g_key_file_get_locale_string (keys, group, "Name", NULL, NULL);

		value = g_key_file_get_string (keys, group, "X-Nemo-Modes", NULL);
		if (value != NULL) {
			info->fits = parse_modes (value);
			info->declared = TRUE;
			g_free (value);
		}

		info->style = g_key_file_get_string (keys, group, "X-Nemo-Style", NULL);
		info->counterpart = g_key_file_get_string (keys, group, "X-Nemo-Counterpart", NULL);
	}

	if (info->display == NULL || info->display[0] == '\0') {
		g_free (info->display);
		info->display = g_strdup (name);
	}

	/* A widget theme carrying gtk-dark.css covers both on its own - GTK swaps
	 * sheets on gtk-application-prefer-dark-theme without changing the name.
	 * A theme that stated its own modes is not second-guessed, same rule as
	 * infer_fits: a dropped-in theme can carry a dark sheet and still be half
	 * of a declared pair, and taking the sheet's word for it would put both
	 * halves in the dark list under one name. */
	if (has_dark_css && !info->declared) {
		info->fits = NEMO_THEME_FITS_BOTH;
		info->declared = TRUE;
		g_clear_pointer (&info->counterpart, g_free);
	}

	g_hash_table_insert (state->by_name, info->name, info);
	g_ptr_array_add (state->order, info);
}

/* The bundled set, out of the binary */

static const char *
catalog_dir (NemoThemeKind kind)
{
	return kind == NEMO_THEME_KIND_ICON
		? BUNDLE_CATALOG "/icons" : BUNDLE_CATALOG "/themes";
}

static gboolean
resource_exists (const char *path)
{
	return g_resources_get_info (path, G_RESOURCE_LOOKUP_FLAGS_NONE,
				     NULL, NULL, NULL);
}

static gboolean
bundled_theme_exists (NemoThemeKind kind, const char *name)
{
	char     *path;
	gboolean  found;

	if (name == NULL || name[0] == '\0') {
		return FALSE;
	}

	path = g_strconcat (catalog_dir (kind), "/", name, "/index.theme", NULL);
	found = resource_exists (path);
	g_free (path);

	return found;
}

static GKeyFile *
bundled_theme_index (NemoThemeKind kind, const char *name)
{
	GKeyFile *keys;
	GBytes   *bytes;
	char     *path;

	path = g_strconcat (catalog_dir (kind), "/", name, "/index.theme", NULL);
	bytes = g_resources_lookup_data (path, G_RESOURCE_LOOKUP_FLAGS_NONE, NULL);
	g_free (path);

	if (bytes == NULL) {
		return NULL;
	}

	keys = g_key_file_new ();
	if (!g_key_file_load_from_bytes (keys, bytes, G_KEY_FILE_NONE, NULL)) {
		g_clear_pointer (&keys, g_key_file_free);
	}
	g_bytes_unref (bytes);

	return keys;
}

/* Nothing at all on a build with no bundled set - the resource simply is not
 * there, and every caller here degrades to the filesystem. */
static void
scan_bundled (ScanState *state)
{
	char **children;
	int    i;

	children = g_resources_enumerate_children (catalog_dir (state->kind),
						   G_RESOURCE_LOOKUP_FLAGS_NONE, NULL);

	for (i = 0; children != NULL && children[i] != NULL; i++) {
		char     *name = g_strdup (children[i]);
		gsize     len  = strlen (name);
		gboolean  has_dark_css = FALSE;
		GKeyFile *keys;

		/* A directory child comes back with its separator still on. */
		if (len > 0 && name[len - 1] == '/') {
			name[len - 1] = '\0';
		}

		/* The legacy set is a shim carrying the two dozen names Adwaita
		 * dropped, not something anyone would choose to look at. */
		if (state->kind == NEMO_THEME_KIND_ICON &&
		    g_strcmp0 (name, "AdwaitaLegacy") == 0) {
			g_free (name);
			continue;
		}

		if (state->kind == NEMO_THEME_KIND_WIDGET) {
			char *css = g_strconcat (BUNDLE_WIDGETS "/", name,
						 "/gtk-3.0/gtk-dark.css", NULL);
			has_dark_css = resource_exists (css);
			g_free (css);
		}

		keys = bundled_theme_index (state->kind, name);
		scan_add (state, NULL, name, keys, has_dark_css, TRUE);

		if (keys != NULL) {
			g_key_file_free (keys);
		}
		g_free (name);
	}

	g_strfreev (children);
}

/* Point GTK at the bundled icon themes, fallbacks first and the chosen theme
 * last. GTK offers no way to take a resource path back off the list, but the
 * most recently added one wins a lookup, so re-pushing the whole order on each
 * apply is what makes switching themes behave. */
static void
push_bundled_icon_paths (const char *chosen)
{
	static char     *pushed;		/* what the list already ends with */
	static gboolean  pushed_once;		/* NULL is a real answer, so track it */
	GtkIconTheme    *theme = gtk_icon_theme_get_default ();
	char            *path;
	int              i;

	/* Nothing to re-order when the answer has not moved, and apply_appearance
	 * runs for a mode change too. Without this the list would grow by three
	 * every time the desktop flipped between light and dark. */
	if (pushed_once && g_strcmp0 (pushed, chosen) == 0) {
		return;
	}
	pushed_once = TRUE;
	g_free (pushed);
	pushed = g_strdup (chosen);

	for (i = 0; fallback_icon_themes[i] != NULL; i++) {
		if (!bundled_theme_exists (NEMO_THEME_KIND_ICON, fallback_icon_themes[i])) {
			continue;
		}
		path = g_strconcat (BUNDLE_ICONS "/", fallback_icon_themes[i], NULL);
		gtk_icon_theme_add_resource_path (theme, path);
		g_free (path);
	}

	if (!bundled_theme_exists (NEMO_THEME_KIND_ICON, chosen)) {
		return;
	}

	path = g_strconcat (BUNDLE_ICONS "/", chosen, NULL);
	gtk_icon_theme_add_resource_path (theme, path);
	g_free (path);
}

static void
scan_dir (ScanState *state, const char *dir, gboolean bundled)
{
	GDir       *handle;
	const char *name;

	handle = g_dir_open (dir, 0, NULL);
	if (handle == NULL) {
		return;
	}

	while ((name = g_dir_read_name (handle)) != NULL) {
		gboolean  has_dark_css = FALSE;
		GKeyFile *keys = NULL;

		if (state->kind == NEMO_THEME_KIND_WIDGET) {
			char *index;

			if (!widget_theme_at (dir, name, &has_dark_css)) {
				continue;
			}

			index = g_build_filename (dir, name, "index.theme", NULL);
			keys = g_key_file_new ();
			if (!g_key_file_load_from_file (keys, index, G_KEY_FILE_NONE, NULL)) {
				g_clear_pointer (&keys, g_key_file_free);
			}
			g_free (index);
		} else {
			/* hicolor is the end of every fallback chain, never a choice. */
			if (g_ascii_strcasecmp (name, "hicolor") == 0) {
				continue;
			}

			keys = icon_theme_index (dir, name);
			if (keys == NULL) {
				continue;
			}

			if (g_key_file_get_boolean (keys, "Icon Theme", "Hidden", NULL)) {
				g_key_file_free (keys);
				continue;
			}
		}

		scan_add (state, dir, name, keys, has_dark_css, bundled);

		if (keys != NULL) {
			g_key_file_free (keys);
		}
	}

	g_dir_close (handle);
}

/* Everywhere GTK itself would look, in GTK's own order. */
static void
scan_gtk_dirs (ScanState *state)
{
	const gchar * const *sys;
	char  *dir;
	int    i;

	if (state->kind == NEMO_THEME_KIND_ICON) {
		gchar **paths = NULL;
		gint    n_paths = 0;

		gtk_icon_theme_get_search_path (gtk_icon_theme_get_default (), &paths, &n_paths);
		for (i = 0; i < n_paths; i++) {
			scan_dir (state, paths[i], FALSE);
		}
		g_strfreev (paths);
		return;
	}

	dir = g_build_filename (g_get_user_data_dir (), THEME_SUBDIR, NULL);
	scan_dir (state, dir, FALSE);
	g_free (dir);

	dir = g_build_filename (g_get_home_dir (), ".themes", NULL);
	scan_dir (state, dir, FALSE);
	g_free (dir);

	sys = nemo_get_system_data_dirs ();
	for (i = 0; sys != NULL && sys[i] != NULL; i++) {
		dir = g_build_filename (sys[i], THEME_SUBDIR, NULL);
		scan_dir (state, dir, FALSE);
		g_free (dir);
	}
}

/* Themes that never said which background they were drawn for get judged by
 * their name: an explicit -dark or -light suffix, or the existence of a -dark
 * sibling, which makes this one the light half of a pair. */
static void
infer_fits (ScanState *state)
{
	guint i;

	for (i = 0; i < state->order->len; i++) {
		NemoThemeInfo *info = g_ptr_array_index (state->order, i);
		char          *stem;
		char          *sibling;
		gboolean       capitalized;

		/* A theme that answered for itself is not second-guessed. Naming is
		 * only ever the fallback: "Foo-dark" that declares itself fit for
		 * both really is fit for both. */
		if (info->declared || info->counterpart != NULL) {
			continue;
		}

		stem = strip_variant_suffix (info->name, "dark");
		if (stem != NULL) {
			info->fits = NEMO_THEME_FITS_DARK;
			if (g_hash_table_contains (state->by_name, stem)) {
				info->counterpart = stem;
			} else {
				sibling = g_strconcat (stem, "-light", NULL);
				if (g_hash_table_contains (state->by_name, sibling)) {
					info->counterpart = sibling;
					sibling = NULL;
				}
				g_free (sibling);
				g_free (stem);
			}
			continue;
		}

		stem = strip_variant_suffix (info->name, "light");
		if (stem != NULL) {
			info->fits = NEMO_THEME_FITS_LIGHT;
			capitalized = g_ascii_isupper (info->name[strlen (info->name) - 5]);
			sibling = variant_sibling (stem, "dark", capitalized);
			if (g_hash_table_contains (state->by_name, sibling)) {
				info->counterpart = sibling;
				sibling = NULL;
			}
			g_free (sibling);
			g_free (stem);
			continue;
		}

		sibling = g_strconcat (info->name, "-dark", NULL);
		if (!g_hash_table_contains (state->by_name, sibling)) {
			g_free (sibling);
			sibling = g_strconcat (info->name, "-Dark", NULL);
		}
		if (g_hash_table_contains (state->by_name, sibling)) {
			info->fits = NEMO_THEME_FITS_LIGHT;
			info->counterpart = sibling;
		} else {
			g_free (sibling);
		}
	}
}

static gint
compare_display (gconstpointer a, gconstpointer b)
{
	const NemoThemeInfo *ia = a;
	const NemoThemeInfo *ib = b;

	if (ia->bundled != ib->bundled) {
		return ia->bundled ? -1 : 1;
	}

	return g_utf8_collate (ia->display, ib->display);
}

GList *
nemo_appearance_list_themes (NemoThemeKind kind, guint fits)
{
	ScanState  state;
	GList     *out = NULL;
	guint      i;
	int        r;

	build_theme_roots ();

	state.kind = kind;
	state.by_name = g_hash_table_new (g_str_hash, g_str_equal);
	state.order = g_ptr_array_new ();

	/* Ours first so a drop-in shadows a same-named system theme. */
	for (r = 0; theme_roots != NULL && theme_roots[r] != NULL; r++) {
		char *dir = g_build_filename (theme_roots[r],
					      kind == NEMO_THEME_KIND_ICON
						? ICON_SUBDIR : THEME_SUBDIR,
					      NULL);
		scan_dir (&state, dir, r > 0);
		g_free (dir);
	}

	scan_bundled (&state);
	scan_gtk_dirs (&state);
	infer_fits (&state);

	for (i = 0; i < state.order->len; i++) {
		NemoThemeInfo *info = g_ptr_array_index (state.order, i);

		if ((info->fits & fits) != 0) {
			out = g_list_prepend (out, info);
		} else {
			nemo_theme_info_free (info);
		}
	}

	g_ptr_array_free (state.order, TRUE);
	g_hash_table_destroy (state.by_name);

	return g_list_sort (out, compare_display);
}

char *
nemo_appearance_theme_for_mode (NemoThemeKind kind, const char *name)
{
	GList    *themes;
	GList    *node;
	char     *out = NULL;
	gboolean  dark;
	guint     wanted;

	if (name == NULL || name[0] == '\0') {
		return NULL;
	}

	dark = nemo_appearance_is_dark ();
	wanted = dark ? NEMO_THEME_FITS_DARK : NEMO_THEME_FITS_LIGHT;

	themes = nemo_appearance_list_themes (kind, NEMO_THEME_FITS_BOTH);

	for (node = themes; node != NULL; node = node->next) {
		NemoThemeInfo *info = node->data;

		if (strcmp (info->name, name) != 0) {
			continue;
		}

		if ((info->fits & wanted) != 0 || info->counterpart == NULL) {
			out = g_strdup (info->name);
		} else {
			out = g_strdup (info->counterpart);
		}
		break;
	}

	g_list_free_full (themes, (GDestroyNotify) nemo_theme_info_free);

	return out != NULL ? out : g_strdup (name);
}

/* Both kinds carry X-Nemo-Style, so pairing them is a lookup rather than a
 * table that would go stale every time a theme is added. */
char *
nemo_appearance_icons_for_widget_theme (const char *widget_name)
{
	GList *themes;
	GList *node;
	char  *style = NULL;
	char  *out = NULL;
	guint  fits;

	if (widget_name == NULL || widget_name[0] == '\0') {
		return NULL;
	}

	themes = nemo_appearance_list_themes (NEMO_THEME_KIND_WIDGET, NEMO_THEME_FITS_BOTH);
	for (node = themes; node != NULL; node = node->next) {
		NemoThemeInfo *info = node->data;

		if (g_strcmp0 (info->name, widget_name) == 0 && info->style != NULL) {
			style = g_strdup (info->style);
			break;
		}
	}
	g_list_free_full (themes, (GDestroyNotify) nemo_theme_info_free);

	if (style == NULL) {
		return NULL;
	}

	/* Only what suits the mode in force, so the answer is one the picker is
	 * actually offering rather than the other half of a light/dark pair. */
	fits = nemo_appearance_is_dark () ? NEMO_THEME_FITS_DARK : NEMO_THEME_FITS_LIGHT;
	themes = nemo_appearance_list_themes (NEMO_THEME_KIND_ICON, fits);
	for (node = themes; node != NULL; node = node->next) {
		NemoThemeInfo *info = node->data;

		if (g_strcmp0 (info->style, style) == 0) {
			out = g_strdup (info->name);
			break;
		}
	}
	g_list_free_full (themes, (GDestroyNotify) nemo_theme_info_free);
	g_free (style);

	return out;
}

/* Applying */

/* TRUE once GTK can resolve @name by itself, so we can hand it the name and
 * let its own machinery do the rest. */
static gboolean
gtk_knows_widget_theme (const char *name)
{
	const gchar * const *sys;
	gboolean  unused;
	char     *dir;
	gboolean  found;
	int       i;

	/* GTK carries this one in its own resources, so it resolves everywhere and
	 * is never on disk. Searching for it finds nothing and warns about a theme
	 * that works perfectly well. */
	if (g_strcmp0 (name, BASE_WIDGET_THEME) == 0) {
		return TRUE;
	}

	dir = g_build_filename (g_get_user_data_dir (), THEME_SUBDIR, NULL);
	found = widget_theme_at (dir, name, &unused);
	g_free (dir);
	if (found) {
		return TRUE;
	}

	dir = g_build_filename (g_get_home_dir (), ".themes", NULL);
	found = widget_theme_at (dir, name, &unused);
	g_free (dir);
	if (found) {
		return TRUE;
	}

	sys = nemo_get_system_data_dirs ();
	for (i = 0; sys != NULL && sys[i] != NULL; i++) {
		dir = g_build_filename (sys[i], THEME_SUBDIR, NULL);
		found = widget_theme_at (dir, name, &unused);
		g_free (dir);
		if (found) {
			return TRUE;
		}
	}

	return FALSE;
}

static void
clear_dropin_provider (void)
{
	if (dropin_provider == NULL) {
		return;
	}

	gtk_style_context_remove_provider_for_screen (gdk_screen_get_default (),
						      GTK_STYLE_PROVIDER (dropin_provider));
	g_clear_object (&dropin_provider);
}

/* Find @name among our roots and load its sheet directly. */
static gboolean
load_dropin_widget_theme (const char *name, gboolean dark)
{
	int r;

	build_theme_roots ();

	for (r = 0; theme_roots != NULL && theme_roots[r] != NULL; r++) {
		char     *css;
		gboolean  ok;

		css = g_build_filename (theme_roots[r], THEME_SUBDIR, name, "gtk-3.0",
					dark ? "gtk-dark.css" : "gtk.css", NULL);

		if (dark && !g_file_test (css, G_FILE_TEST_IS_REGULAR)) {
			g_free (css);
			css = g_build_filename (theme_roots[r], THEME_SUBDIR, name,
						"gtk-3.0", "gtk.css", NULL);
		}

		if (!g_file_test (css, G_FILE_TEST_IS_REGULAR)) {
			g_free (css);
			continue;
		}

		clear_dropin_provider ();
		dropin_provider = gtk_css_provider_new ();
		ok = gtk_css_provider_load_from_path (dropin_provider, css, NULL);
		g_free (css);

		if (!ok) {
			g_clear_object (&dropin_provider);
			return FALSE;
		}

		gtk_style_context_add_provider_for_screen (gdk_screen_get_default (),
							   GTK_STYLE_PROVIDER (dropin_provider),
							   DROPIN_THEME_PRIORITY);
		return TRUE;
	}

	return FALSE;
}

/* Same idea as a drop-in, out of the binary instead of off disk. GTK resolves
 * a sheet's own url() references against wherever it was loaded from, so the
 * theme's assets come along with no rewriting. */
static gboolean
load_bundled_widget_theme (const char *name, gboolean dark)
{
	char *path;

	path = g_strconcat (BUNDLE_WIDGETS "/", name, "/gtk-3.0/",
			    dark ? "gtk-dark.css" : "gtk.css", NULL);

	if (dark && !resource_exists (path)) {
		g_free (path);
		path = g_strconcat (BUNDLE_WIDGETS "/", name, "/gtk-3.0/gtk.css", NULL);
	}

	if (!resource_exists (path)) {
		g_free (path);
		return FALSE;
	}

	clear_dropin_provider ();
	dropin_provider = gtk_css_provider_new ();
	gtk_css_provider_load_from_resource (dropin_provider, path);
	g_free (path);

	gtk_style_context_add_provider_for_screen (gdk_screen_get_default (),
						   GTK_STYLE_PROVIDER (dropin_provider),
						   DROPIN_THEME_PRIORITY);
	return TRUE;
}

static gboolean
system_prefers_dark (void)
{
#ifdef G_OS_WIN32
	/* Windows publishes its own answer, independent of anything we set. */
	return nemo_win32_prefers_dark ();
#else
	/* Elsewhere the desktop has already told GTK, so "system" means leaving
	 * that answer alone. macOS gets its own probe when that target is taken up. */
	return desktop_dark;
#endif
}

gboolean
nemo_appearance_is_dark (void)
{
	switch (nemo_config_get_enum (nemo_appearance_preferences, NEMO_PREFERENCES_APPEARANCE_MODE)) {
	case NEMO_APPEARANCE_MODE_LIGHT:
		return FALSE;
	case NEMO_APPEARANCE_MODE_DARK:
		return TRUE;
	default:
		return system_prefers_dark ();
	}
}

static void
apply_appearance (void)
{
	GtkSettings *settings = gtk_settings_get_default ();
	gboolean     dark;
	char        *wanted;
	char        *resolved;

	if (settings == NULL || applying) {
		return;
	}

	/* system_prefers_dark () reads back the property we are about to write,
	 * so settle the mode before touching it. */
	dark = nemo_appearance_is_dark ();

	applying = TRUE;

	g_object_set (settings, "gtk-application-prefer-dark-theme", dark, NULL);

	/* The app's own default is not "do nothing": the Windows bundle names a theme
	 * of its own in settings.ini, and that theme lives inside the binary. So
	 * the platform's answer goes through exactly the same loaders as an
	 * explicit choice - on a desktop that already has its theme installed the
	 * first of them matches and nothing changes. */
	wanted = nemo_config_get_string (nemo_appearance_preferences,
					 NEMO_PREFERENCES_APPEARANCE_GTK_THEME);
	resolved = (wanted != NULL && wanted[0] != '\0')
		? nemo_appearance_theme_for_mode (NEMO_THEME_KIND_WIDGET, wanted)
		: g_strdup (platform_gtk_theme);

	widget_theme_source = "none";
	if (resolved == NULL || resolved[0] == '\0') {
		clear_dropin_provider ();
	} else if (gtk_knows_widget_theme (resolved)) {
		clear_dropin_provider ();
		g_object_set (settings, "gtk-theme-name", resolved, NULL);
		widget_theme_source = "installed";
	} else if (load_dropin_widget_theme (resolved, dark)) {
		widget_theme_source = "drop-in";
	} else if (load_bundled_widget_theme (resolved, dark)) {
		widget_theme_source = "bundled";
	} else {
		/* Without this the sheet from the last choice that did resolve
		 * stays on screen, so a bad name looks like nothing happened. */
		clear_dropin_provider ();
		g_warning ("appearance: widget theme \"%s\" not found", resolved);
	}

	if (strcmp (widget_theme_source, "installed") != 0) {
		g_object_set (settings, "gtk-theme-name", BASE_WIDGET_THEME, NULL);
	}
	g_free (resolved);
	g_free (wanted);

	wanted = nemo_config_get_string (nemo_appearance_preferences,
					 NEMO_PREFERENCES_APPEARANCE_ICON_THEME);
	resolved = (wanted != NULL && wanted[0] != '\0')
		? nemo_appearance_theme_for_mode (NEMO_THEME_KIND_ICON, wanted)
		: g_strdup (platform_icon_theme);

	push_bundled_icon_paths (resolved);
	if (resolved != NULL && resolved[0] != '\0') {
		g_object_set (settings, "gtk-icon-theme-name", resolved, NULL);
	}
	g_free (resolved);
	g_free (wanted);

	applying = FALSE;

	/* What actually reached GTK, which is the only way to check the chain on a
	 * box that cannot show a window. G_MESSAGES_DEBUG="Nemo Anywhere" to see it. */
	{
		char *gtk_theme = NULL;
		char *icon_theme = NULL;

		g_object_get (settings,
			      "gtk-theme-name", &gtk_theme,
			      "gtk-icon-theme-name", &icon_theme,
			      NULL);
		g_debug ("appearance: %s, widget theme \"%s\" (%s), icon theme \"%s\"",
			 dark ? "dark" : "light",
			 gtk_theme != NULL ? gtk_theme : "",
			 widget_theme_source,
			 icon_theme != NULL ? icon_theme : "");
		g_free (gtk_theme);
		g_free (icon_theme);
	}

	gtk_style_context_reset_widgets (gdk_screen_get_default ());
}

static void
appearance_changed_cb (G_GNUC_UNUSED NemoConfigGroup *group,
		       G_GNUC_UNUSED const char      *key,
		       G_GNUC_UNUSED gpointer         user_data)
{
	apply_appearance ();
}

/* Someone other than us moved the property - that is the desktop talking. */
static void
prefer_dark_notify_cb (GObject *settings, G_GNUC_UNUSED GParamSpec *pspec, G_GNUC_UNUSED gpointer user_data)
{
	gboolean was = desktop_dark;

	if (applying) {
		return;
	}

	g_object_get (settings, "gtk-application-prefer-dark-theme", &desktop_dark, NULL);

	/* Following the desktop can mean swapping to a theme's other variant. */
	if (was != desktop_dark &&
	    nemo_config_get_enum (nemo_appearance_preferences,
				  NEMO_PREFERENCES_APPEARANCE_MODE) == NEMO_APPEARANCE_MODE_SYSTEM) {
		apply_appearance ();
	}
}

#ifdef G_OS_WIN32
static void
system_dark_changed_cb (G_GNUC_UNUSED gboolean dark, G_GNUC_UNUSED gpointer user_data)
{
	apply_appearance ();
}
#endif

void
nemo_appearance_init (void)
{
	int r;

	if (initialized) {
		return;
	}
	initialized = TRUE;

	build_theme_roots ();
	ensure_user_dirs ();

	/* Whatever the platform had already decided, before we write anything. */
	g_object_get (gtk_settings_get_default (),
		      "gtk-application-prefer-dark-theme", &desktop_dark,
		      "gtk-theme-name", &platform_gtk_theme,
		      "gtk-icon-theme-name", &platform_icon_theme,
		      NULL);
	g_signal_connect (gtk_settings_get_default (),
			  "notify::gtk-application-prefer-dark-theme",
			  G_CALLBACK (prefer_dark_notify_cb), NULL);

	/* Goes on once and stays under whatever the user picks: a later resource
	 * path only wins for a name both carry, and every name here is nemo-*. */
	gtk_icon_theme_add_resource_path (gtk_icon_theme_get_default (), APP_ICONS);

	/* Icon themes need no special loader - GTK searches whatever it is told. */
	for (r = 0; theme_roots != NULL && theme_roots[r] != NULL; r++) {
		char *dir = g_build_filename (theme_roots[r], ICON_SUBDIR, NULL);

		gtk_icon_theme_append_search_path (gtk_icon_theme_get_default (), dir);
		g_free (dir);
	}

	g_signal_connect (nemo_appearance_preferences,
			  "changed::" NEMO_PREFERENCES_APPEARANCE_MODE,
			  G_CALLBACK (appearance_changed_cb), NULL);
	g_signal_connect (nemo_appearance_preferences,
			  "changed::" NEMO_PREFERENCES_APPEARANCE_GTK_THEME,
			  G_CALLBACK (appearance_changed_cb), NULL);
	g_signal_connect (nemo_appearance_preferences,
			  "changed::" NEMO_PREFERENCES_APPEARANCE_ICON_THEME,
			  G_CALLBACK (appearance_changed_cb), NULL);

	apply_appearance ();

	eel_debug_call_at_shutdown (nemo_appearance_shutdown);

#ifdef G_OS_WIN32
	/* Follow the Windows setting live, so "system" tracks it while running. */
	nemo_win32_watch_dark (system_dark_changed_cb, NULL);
#endif
}

void
nemo_appearance_shutdown (void)
{
	clear_dropin_provider ();
	g_clear_pointer (&theme_roots, g_strfreev);
	g_clear_pointer (&platform_gtk_theme, g_free);
	g_clear_pointer (&platform_icon_theme, g_free);
	initialized = FALSE;
}
