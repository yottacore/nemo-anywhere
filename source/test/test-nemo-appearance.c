/* Exercises theme discovery and the light/dark filtering behind the Appearance
 * settings: which themes are offered for the mode in force, how a theme that
 * never declared a mode is judged, and which variant is actually applied.
 * Then what applying one does to GTK, and how the icon set follows a style.
 * Runs against a throwaway config root holding hand-built theme folders, with
 * the system data dirs hidden so an icon can only come from what the app
 * itself puts on the search path. */

#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>

#include <libnemo-private/nemo-appearance.h>
#include <libnemo-private/nemo-config.h>
#include <libnemo-private/nemo-global-preferences.h>

#include "test-scratch.h"
#include "test-check.h"

static char *root;

static void
write_file (const char *path, const char *text)
{
	char *dir = g_path_get_dirname (path);

	g_mkdir_with_parents (dir, 0755);
	g_free (dir);

	if (!g_file_set_contents (path, text, -1, NULL)) {
		g_printerr ("could not write %s\n", path);
		failures++;
	}
}

static void
make_icon_theme (const char *name, const char *extra_keys)
{
	char *index;
	char *icon;
	char *text;

	index = g_build_filename (root, "icons", name, "index.theme", NULL);
	text = g_strdup_printf ("[Icon Theme]\n"
				"Name=%s\n"
				"%s"
				"Directories=scalable/places\n"
				"\n"
				"[scalable/places]\n"
				"Size=48\n"
				"Context=Places\n"
				"Type=Scalable\n",
				name, extra_keys != NULL ? extra_keys : "");
	write_file (index, text);
	g_free (text);
	g_free (index);

	icon = g_build_filename (root, "icons", name, "scalable", "places", "folder.svg", NULL);
	write_file (icon, "<svg xmlns=\"http://www.w3.org/2000/svg\"/>");
	g_free (icon);
}

static void
make_widget_theme (const char *name, gboolean with_dark_sheet, const char *extra_keys)
{
	char *path;

	path = g_build_filename (root, "themes", name, "gtk-3.0", "gtk.css", NULL);
	write_file (path, "/* test */\n");
	g_free (path);

	if (with_dark_sheet) {
		path = g_build_filename (root, "themes", name, "gtk-3.0", "gtk-dark.css", NULL);
		write_file (path, "/* test dark */\n");
		g_free (path);
	}

	if (extra_keys != NULL) {
		char *text = g_strdup_printf ("[Desktop Entry]\nType=X-GNOME-Metatheme\nName=%s\n%s",
					      name, extra_keys);
		path = g_build_filename (root, "themes", name, "index.theme", NULL);
		write_file (path, text);
		g_free (text);
		g_free (path);
	}
}

/* A widget theme whose sheet paints one class a color nothing else uses, so
   whether its sheet is on screen can be read back from a label. */
static void
make_probe_theme (const char *name, const char *color, const char *extra_keys)
{
	char *path;
	char *css;

	path = g_build_filename (root, "themes", name, "gtk-3.0", "gtk.css", NULL);
	css = g_strdup_printf (".zz-probe { color: %s; }\n", color);
	write_file (path, css);
	g_free (css);
	g_free (path);

	if (extra_keys != NULL) {
		char *text = g_strdup_printf ("[Desktop Entry]\nType=X-GNOME-Metatheme\nName=%s\n%s",
					      name, extra_keys);
		path = g_build_filename (root, "themes", name, "index.theme", NULL);
		write_file (path, text);
		g_free (text);
		g_free (path);
	}
}

/* The hicolor directories the app's art and the bundled sets use, as the
   freedesktop hicolor theme defines them. */
static char *
hicolor_index (void)
{
	static const int sizes[] = { 16, 22, 24, 32, 48, 64, 96, 128, 256, 512 };
	static const char *const contexts[] = {
		"actions", "apps", "categories", "devices", "emblems",
		"mimetypes", "places", "status", NULL
	};
	GString *text = g_string_new ("[Icon Theme]\nName=Hicolor\nHidden=true\nDirectories=");
	GString *groups = g_string_new (NULL);
	guint i;
	int c;

	for (c = 0; contexts[c] != NULL; c++) {
		for (i = 0; i < G_N_ELEMENTS (sizes); i++) {
			g_string_append_printf (text, "%dx%d/%s,", sizes[i], sizes[i], contexts[c]);
			g_string_append_printf (groups, "\n[%dx%d/%s]\nSize=%d\nType=Threshold\n",
						sizes[i], sizes[i], contexts[c], sizes[i]);
		}
		g_string_append_printf (text, "scalable/%s,", contexts[c]);
		g_string_append_printf (groups, "\n[scalable/%s]\nSize=16\nMinSize=1\n"
					"MaxSize=512\nType=Scalable\n", contexts[c]);
	}

	/* The last comma becomes the end of the Directories line. */
	text->str[text->len - 1] = '\n';
	g_string_append (text, groups->str);
	g_string_free (groups, TRUE);

	return g_string_free (text, FALSE);
}

static NemoThemeInfo *
find (GList *themes, const char *name)
{
	GList *node;

	for (node = themes; node != NULL; node = node->next) {
		NemoThemeInfo *info = node->data;

		if (strcmp (info->name, name) == 0) {
			return info;
		}
	}

	return NULL;
}

static void
set_mode (const char *mode)
{
	nemo_config_set_string (nemo_appearance_preferences,
				NEMO_PREFERENCES_APPEARANCE_MODE, mode);
}

/* A theme that never said which background it was drawn for is judged by its
 * name. That guess is what decides whether it appears in the picker at all. */
static void
test_inferred_pairing (void)
{
	GList         *themes;
	NemoThemeInfo *info;

	set_mode ("light");

	themes = nemo_appearance_list_themes (NEMO_THEME_KIND_ICON, NEMO_THEME_FITS_LIGHT);

	info = find (themes, "ZzTestPair");
	check (info != NULL);
	if (info != NULL) {
		check (info->fits == NEMO_THEME_FITS_LIGHT);
		check (g_strcmp0 (info->counterpart, "ZzTestPair-dark") == 0);
	}

	/* The dark half of a pair has no business in the light list. */
	check (find (themes, "ZzTestPair-dark") == NULL);

	/* No suffix and no dark sibling means it serves both. */
	info = find (themes, "ZzTestLone");
	check (info != NULL);
	if (info != NULL) {
		check (info->fits == NEMO_THEME_FITS_BOTH);
		check (info->counterpart == NULL);
	}

	/* hicolor is the end of every fallback chain, never a choice. */
	check (find (themes, "hicolor") == NULL);

	g_list_free_full (themes, (GDestroyNotify) nemo_theme_info_free);

	set_mode ("dark");
	themes = nemo_appearance_list_themes (NEMO_THEME_KIND_ICON, NEMO_THEME_FITS_DARK);

	check (find (themes, "ZzTestPair") == NULL);
	info = find (themes, "ZzTestPair-dark");
	check (info != NULL);
	if (info != NULL) {
		check (info->fits == NEMO_THEME_FITS_DARK);
		check (g_strcmp0 (info->counterpart, "ZzTestPair") == 0);
	}
	check (find (themes, "ZzTestLone") != NULL);

	g_list_free_full (themes, (GDestroyNotify) nemo_theme_info_free);
}

/* X-Nemo-Modes is the theme's own answer and outranks the name. */
static void
test_declared_modes (void)
{
	GList         *themes;
	NemoThemeInfo *info;

	set_mode ("light");
	themes = nemo_appearance_list_themes (NEMO_THEME_KIND_ICON, NEMO_THEME_FITS_LIGHT);

	/* Named like a dark theme, declares itself fit for both. */
	info = find (themes, "ZzTestDeclared-dark");
	check (info != NULL);
	if (info != NULL) {
		check (info->fits == NEMO_THEME_FITS_BOTH);
		check (g_strcmp0 (info->style, "Test Style") == 0);
	}

	g_list_free_full (themes, (GDestroyNotify) nemo_theme_info_free);
}

/* A widget theme carrying its own dark sheet needs no counterpart: GTK swaps
 * between gtk.css and gtk-dark.css without the name changing. */
static void
test_widget_dark_sheet (void)
{
	GList         *themes;
	NemoThemeInfo *info;

	set_mode ("dark");
	themes = nemo_appearance_list_themes (NEMO_THEME_KIND_WIDGET, NEMO_THEME_FITS_DARK);

	info = find (themes, "ZzWidgetBoth");
	check (info != NULL);
	if (info != NULL) {
		check (info->fits == NEMO_THEME_FITS_BOTH);
		check (info->counterpart == NULL);
	}

	info = find (themes, "ZzWidgetPair-dark");
	check (info != NULL);

	g_list_free_full (themes, (GDestroyNotify) nemo_theme_info_free);
}

/* What actually gets handed to GTK: the chosen theme, or its other half when
 * the mode has moved away from it. */
static void
test_theme_for_mode (void)
{
	char *resolved;

	set_mode ("dark");

	resolved = nemo_appearance_theme_for_mode (NEMO_THEME_KIND_ICON, "ZzTestPair");
	check (g_strcmp0 (resolved, "ZzTestPair-dark") == 0);
	g_free (resolved);

	/* Already the right half - left alone. */
	resolved = nemo_appearance_theme_for_mode (NEMO_THEME_KIND_ICON, "ZzTestPair-dark");
	check (g_strcmp0 (resolved, "ZzTestPair-dark") == 0);
	g_free (resolved);

	/* Serves both, so no swap. */
	resolved = nemo_appearance_theme_for_mode (NEMO_THEME_KIND_ICON, "ZzTestLone");
	check (g_strcmp0 (resolved, "ZzTestLone") == 0);
	g_free (resolved);

	resolved = nemo_appearance_theme_for_mode (NEMO_THEME_KIND_WIDGET, "ZzWidgetPair");
	check (g_strcmp0 (resolved, "ZzWidgetPair-dark") == 0);
	g_free (resolved);

	set_mode ("light");

	resolved = nemo_appearance_theme_for_mode (NEMO_THEME_KIND_ICON, "ZzTestPair-dark");
	check (g_strcmp0 (resolved, "ZzTestPair") == 0);
	g_free (resolved);

	/* A name nobody installed comes back unchanged rather than as NULL, so a
	 * stale setting never silently becomes "no theme at all". */
	resolved = nemo_appearance_theme_for_mode (NEMO_THEME_KIND_ICON, "ZzNotInstalled");
	check (g_strcmp0 (resolved, "ZzNotInstalled") == 0);
	g_free (resolved);

	check (nemo_appearance_theme_for_mode (NEMO_THEME_KIND_ICON, NULL) == NULL);
}

/* The bundled set is compiled into the binary rather than installed as files.
 * Three things have to hold or it silently stops working: the catalog the
 * picker reads has to be there, the widget sheets have to be reachable by the
 * path the loader builds, and every icon has to sit under a directory name
 * hicolor defines - GTK reads a resource path as part of hicolor and knows
 * nothing about a symbolic/ folder, so anything left in one is invisible.
 * A build with no bundled set (Linux) has nothing to check. */
#define BUNDLE_ICONS	"/org/nemo/themes/icontheme"
#define BUNDLE_WIDGETS	"/org/nemo/themes/widgettheme"
#define BUNDLE_CATALOG	"/org/nemo/themes/catalog"

static gboolean
resource_exists (const char *path)
{
	return g_resources_get_info (path, G_RESOURCE_LOOKUP_FLAGS_NONE, NULL, NULL, NULL);
}

/* Every file under @path, recursively, as full resource paths. */
static void
collect_resources (const char *path, GPtrArray *out)
{
	char **children = g_resources_enumerate_children (path, G_RESOURCE_LOOKUP_FLAGS_NONE, NULL);
	int i;

	for (i = 0; children != NULL && children[i] != NULL; i++) {
		char *child = g_strconcat (path, "/", children[i], NULL);

		if (g_str_has_suffix (children[i], "/")) {
			child[strlen (child) - 1] = '\0';
			collect_resources (child, out);
			g_free (child);
		} else {
			g_ptr_array_add (out, child);
		}
	}

	g_strfreev (children);
}

static void
test_bundled_set (void)
{
	char      **names;
	GList      *themes, *node;
	GPtrArray  *files;
	gboolean    listed = FALSE;
	guint       i;
	int         symbolic_under_scalable = 0;

	names = g_resources_enumerate_children (BUNDLE_CATALOG "/icons",
						G_RESOURCE_LOOKUP_FLAGS_NONE, NULL);
	if (names == NULL || names[0] == NULL) {
		g_print ("no bundled theme set in this build - skipping\n");
		g_strfreev (names);
		return;
	}
	g_strfreev (names);

	/* The catalog reaches the picker, carrying what the picker needs. */
	themes = nemo_appearance_list_themes (NEMO_THEME_KIND_ICON, NEMO_THEME_FITS_BOTH);
	for (node = themes; node != NULL; node = node->next) {
		NemoThemeInfo *info = node->data;

		/* Adwaita is the sample deliberately: it is the tail of the fallback
		 * chain, so it is the one icon set that cannot be dropped from the
		 * bundle by a change of mind about which themes to ship. This check
		 * used to name Fluent, and went red the day that set was dropped. */
		if (g_strcmp0 (info->name, "Adwaita") == 0) {
			listed = TRUE;
			check (info->bundled);
			check (info->dir == NULL);		/* not a directory anywhere */
			check (info->style != NULL && info->style[0] != '\0');
		}

		/* The legacy shim is a fallback, never something to choose. */
		check (g_strcmp0 (info->name, "AdwaitaLegacy") != 0);
	}
	check (listed);
	g_list_free_full (themes, (GDestroyNotify) nemo_theme_info_free);

	/* A bundled widget theme's sheet, at exactly the path the loader builds. */
	check (resource_exists (BUNDLE_WIDGETS "/Fluent/gtk-3.0/gtk.css"));

	/* No icon may be left in a symbolic/ folder, and the monochrome ones
	 * still have to be there under their own name. */
	files = g_ptr_array_new_with_free_func (g_free);
	collect_resources (BUNDLE_ICONS "/Adwaita", files);
	check (files->len > 0);
	for (i = 0; i < files->len; i++) {
		const char *path = g_ptr_array_index (files, i);

		check (strstr (path, "/symbolic/") == NULL);
		if (strstr (path, "-symbolic.") != NULL && strstr (path, "/scalable/") != NULL) {
			symbolic_under_scalable++;
		}
	}
	check (symbolic_under_scalable > 0);
	g_ptr_array_unref (files);
}

/* The color a label of the probe class is drawn in, as 0-255 red. */
static int
probe_red (void)
{
	GtkWidget       *label = gtk_label_new ("x");
	GtkStyleContext *context;
	GdkRGBA          color;

	g_object_ref_sink (label);
	context = gtk_widget_get_style_context (label);
	gtk_style_context_add_class (context, "zz-probe");
	gtk_style_context_get_color (context, GTK_STATE_FLAG_NORMAL, &color);
	g_object_unref (label);

	return (int) (color.red * 255.0 + 0.5);
}

static char *
gtk_theme_name (void)
{
	char *name = NULL;

	g_object_get (gtk_settings_get_default (), "gtk-theme-name", &name, NULL);
	return name;
}

static void
choose_widget_theme (const char *name)
{
	nemo_config_set_string (nemo_appearance_preferences,
				NEMO_PREFERENCES_APPEARANCE_GTK_THEME, name);
}

static void
choose_icon_theme (const char *name)
{
	nemo_config_set_string (nemo_appearance_preferences,
				NEMO_PREFERENCES_APPEARANCE_ICON_THEME, name);
}

/* A drop-in or bundled theme goes on as a sheet of our own over a base GTK can
 * resolve. Pointing gtk-theme-name at the drop-in's name instead made GTK fall
 * back to its packaged sheet and drop the dark half, so in dark mode anything
 * our sheet did not paint itself, the breadcrumb and checked buttons among
 * them, stayed light. A name nobody has takes the last sheet off. */
static void
test_dropin_applied (void)
{
	gboolean  prefer_dark = FALSE;
	char     *name;

	set_mode ("light");
	check (probe_red () != 1);

	choose_widget_theme ("ZzProbe");
	check (probe_red () == 1);
	name = gtk_theme_name ();
	check (g_strcmp0 (name, "Adwaita") == 0);
	g_free (name);

	set_mode ("dark");
	g_object_get (gtk_settings_get_default (),
		      "gtk-application-prefer-dark-theme", &prefer_dark, NULL);
	check (prefer_dark);
	check (probe_red () == 1);
	name = gtk_theme_name ();
	check (g_strcmp0 (name, "Adwaita") == 0);
	g_free (name);

	/* Warns that it is not found, which is expected here. */
	choose_widget_theme ("ZzNotInstalled");
	check (probe_red () != 1);

	choose_widget_theme ("");
	set_mode ("light");
}

/* Count of entries called @name in a list. */
static int
count_named (GList *themes, const char *name)
{
	GList *node;
	int    n = 0;

	for (node = themes; node != NULL; node = node->next) {
		if (g_strcmp0 (((NemoThemeInfo *) node->data)->name, name) == 0) {
			n++;
		}
	}

	return n;
}

/* A light half that also carries a dark sheet of its own, with a separately
 * drawn dark half beside it. Both halves claimed dark, so the dark picker
 * listed the style twice, one of them the light theme. The pair it names wins
 * over the sheet. The macOS and Windows 10 themes come this way upstream. */
static void
test_named_pair_listed_once (void)
{
	GList *themes;
	GList *node, *other;

	set_mode ("dark");
	themes = nemo_appearance_list_themes (NEMO_THEME_KIND_WIDGET, NEMO_THEME_FITS_DARK);

	check (count_named (themes, "ZzNamed") == 0);
	check (count_named (themes, "ZzNamed-dark") == 1);

	/* Every bundled style once at most, whatever its halves carry. */
	for (node = themes; node != NULL; node = node->next) {
		NemoThemeInfo *a = node->data;

		if (!a->bundled || a->dir != NULL || a->style == NULL) {
			continue;
		}
		for (other = node->next; other != NULL; other = other->next) {
			NemoThemeInfo *b = other->data;

			if (b->bundled && b->dir == NULL && g_strcmp0 (a->style, b->style) == 0) {
				g_printerr ("FAIL style %s is in the dark list twice: %s and %s\n",
					    a->style, a->name, b->name);
				failures++;
			}
		}
	}

	g_list_free_full (themes, (GDestroyNotify) nemo_theme_info_free);
	set_mode ("light");
}

/* Picking a style moves the icon choice to the set drawn for the same look,
 * the half that suits the mode. No style of its own, no move. */
static void
test_icons_follow_style (void)
{
	GList *widgets, *icons, *node;
	char  *icons_for;
	int    mode;

	set_mode ("light");
	icons_for = nemo_appearance_icons_for_widget_theme ("ZzStyled");
	check (g_strcmp0 (icons_for, "ZzLook") == 0);
	g_free (icons_for);

	set_mode ("dark");
	icons_for = nemo_appearance_icons_for_widget_theme ("ZzStyled");
	check (g_strcmp0 (icons_for, "ZzLook-dark") == 0);
	g_free (icons_for);

	check (nemo_appearance_icons_for_widget_theme ("ZzWidgetBoth") == NULL);
	check (nemo_appearance_icons_for_widget_theme ("") == NULL);

	/* Each bundled Windows look has an icon set of its own in the bundle, in
	   both modes. Nothing to check on a build without the bundle. */
	for (mode = 0; mode < 2; mode++) {
		set_mode (mode == 0 ? "light" : "dark");
		widgets = nemo_appearance_list_themes (NEMO_THEME_KIND_WIDGET, NEMO_THEME_FITS_BOTH);
		icons = nemo_appearance_list_themes (NEMO_THEME_KIND_ICON, NEMO_THEME_FITS_BOTH);

		for (node = widgets; node != NULL; node = node->next) {
			NemoThemeInfo *info = node->data;
			NemoThemeInfo *set;

			if (!info->bundled || info->dir != NULL ||
			    !g_str_has_prefix (info->style != NULL ? info->style : "", "Windows")) {
				continue;
			}

			icons_for = nemo_appearance_icons_for_widget_theme (info->name);
			set = icons_for != NULL ? find (icons, icons_for) : NULL;
			if (set == NULL || !set->bundled || g_strcmp0 (set->style, info->style) != 0) {
				g_printerr ("FAIL %s (%s, %s mode) moves the icons to %s\n",
					    info->name, info->style, mode == 0 ? "light" : "dark",
					    icons_for != NULL ? icons_for : "nothing");
				failures++;
			}
			g_free (icons_for);
		}

		g_list_free_full (widgets, (GDestroyNotify) nemo_theme_info_free);
		g_list_free_full (icons, (GDestroyNotify) nemo_theme_info_free);
	}

	set_mode ("light");
}

/* A theme dropped in under the name of a bundled one is the one listed and
 * the one applied - drop-ins are searched first. On a build with the bundle
 * there is also a bundled Fluent. Made here rather than with the rest, so the
 * checks before this one see the bundled Fluent. */
static void
test_dropin_shadows_bundled (void)
{
	GList         *themes;
	NemoThemeInfo *info;

	/* Red 4, where ZzProbe draws red 1. */
	make_probe_theme ("Fluent", "rgb(4,5,6)", "X-Nemo-Style=Windows 11\n");

	themes = nemo_appearance_list_themes (NEMO_THEME_KIND_WIDGET, NEMO_THEME_FITS_BOTH);
	check (count_named (themes, "Fluent") == 1);
	info = find (themes, "Fluent");
	check (info != NULL && !info->bundled && info->dir != NULL);
	g_list_free_full (themes, (GDestroyNotify) nemo_theme_info_free);

	set_mode ("light");
	choose_widget_theme ("Fluent");
	check (probe_red () == 4);
	choose_widget_theme ("");
}

static gboolean
icon_resolves (const char *name, int size)
{
	GtkIconInfo *info = gtk_icon_theme_lookup_icon (gtk_icon_theme_get_default (),
							name, size, 0);

	if (info == NULL) {
		return FALSE;
	}
	g_object_unref (info);
	return TRUE;
}

/* The app's own art is not a theme and is never picked, so it has to be on the
 * icon path whatever set is chosen. The sort menu and eject icons drew as
 * missing on Windows before it rode in the binary. */
static void
test_app_icons_resolve (void)
{
	check (icon_resolves ("nemo-eject", 16));
	check (icon_resolves ("menu-sort-up", 16));
}

/* Every bundled icon set, chosen, resolves the two names every folder view
 * draws: a folder, and the emblem a symlink wears. A trimmed set leans on the
 * Adwaita fallbacks for the second, and the resolver once lost it. */
static void
test_bundled_icons_resolve (void)
{
	GList *themes, *node;
	int    sets = 0;

	set_mode ("light");
	themes = nemo_appearance_list_themes (NEMO_THEME_KIND_ICON, NEMO_THEME_FITS_BOTH);

	for (node = themes; node != NULL; node = node->next) {
		NemoThemeInfo *info = node->data;

		if (!info->bundled || info->dir != NULL) {
			continue;
		}
		sets++;

		choose_icon_theme (info->name);
		if (!icon_resolves ("folder", 48)) {
			g_printerr ("FAIL %s: no folder icon\n", info->name);
			failures++;
		}
		if (!icon_resolves ("emblem-symbolic-link", 16)) {
			g_printerr ("FAIL %s: no emblem-symbolic-link\n", info->name);
			failures++;
		}
	}

	g_list_free_full (themes, (GDestroyNotify) nemo_theme_info_free);
	choose_icon_theme ("");

	if (sets == 0) {
		g_print ("no bundled icon sets in this build - skipping their lookups\n");
	}
}

static void
test_mode_resolution (void)
{
	set_mode ("light");
	check (nemo_appearance_is_dark () == FALSE);

	set_mode ("dark");
	check (nemo_appearance_is_dark () == TRUE);
}

int
main (int argc, char *argv[])
{
	char *tmp;

	tmp = test_scratch_config_home ("nemo-appearance-test-XXXXXX");

	/* Nothing from the box: no installed icon or widget themes, only the
	   hicolor index every GTK stack carries, which says what a resource path
	   holds. */
	{
		char *data = g_build_filename (tmp, "system-data", NULL);
		char *index = g_build_filename (data, "icons", "hicolor", "index.theme", NULL);
		char *text = hicolor_index ();

		write_file (index, text);
		g_setenv ("XDG_DATA_DIRS", data, TRUE);
		g_free (text);
		g_free (index);
		g_free (data);
	}

	if (!gtk_init_check (&argc, &argv)) {
		g_print ("SKIP: no display\n");
		g_free (tmp);
		return 77;
	}

	nemo_global_preferences_init ();

	/* The drop-in folders nemo_appearance_get_theme_roots () points at. */
	root = g_build_filename (tmp, "nemo-anywhere", NULL);

	make_icon_theme ("ZzTestPair", NULL);
	make_icon_theme ("ZzTestPair-dark", NULL);
	make_icon_theme ("ZzTestLone", NULL);
	make_icon_theme ("ZzTestDeclared-dark",
			 "X-Nemo-Modes=light;dark\nX-Nemo-Style=Test Style\n");
	make_icon_theme ("hicolor", NULL);

	make_widget_theme ("ZzWidgetPair", FALSE, NULL);
	make_widget_theme ("ZzWidgetPair-dark", FALSE, NULL);
	make_widget_theme ("ZzWidgetBoth", TRUE, NULL);

	make_widget_theme ("ZzNamed", TRUE,
			   "X-Nemo-Modes=light\nX-Nemo-Counterpart=ZzNamed-dark\n");
	make_widget_theme ("ZzNamed-dark", FALSE,
			   "X-Nemo-Modes=dark\nX-Nemo-Counterpart=ZzNamed\n");

	make_widget_theme ("ZzStyled", FALSE, "X-Nemo-Style=Zz Look\n");
	make_icon_theme ("ZzLook", "X-Nemo-Modes=light\nX-Nemo-Style=Zz Look\n");
	make_icon_theme ("ZzLook-dark", "X-Nemo-Modes=dark\nX-Nemo-Style=Zz Look\n");

	make_probe_theme ("ZzProbe", "rgb(1,2,3)", NULL);

	test_mode_resolution ();
	test_inferred_pairing ();
	test_declared_modes ();
	test_widget_dark_sheet ();
	test_theme_for_mode ();
	test_bundled_set ();

	test_named_pair_listed_once ();
	test_icons_follow_style ();

	/* From here on what is chosen reaches GTK. */
	nemo_appearance_init ();
	test_app_icons_resolve ();
	test_dropin_applied ();
	test_dropin_shadows_bundled ();
	test_bundled_icons_resolve ();

	g_free (root);
	g_free (tmp);

	if (failures > 0) {
		g_printerr ("%d check(s) failed\n", failures);
		return 1;
	}

	g_print ("OK\n");
	return 0;
}
