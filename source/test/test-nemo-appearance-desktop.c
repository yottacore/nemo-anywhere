/* The desktop's own icon and widget theme, as XSETTINGS hands them to GTK.
 * With nothing picked in the app, a change on the desktop has to reach the
 * window while it runs. A pick in the app wins over the desktop until it is
 * cleared, and clearing it goes back to what the desktop says now, not what it
 * said at launch.
 *
 * The test is its own XSETTINGS manager, on a display of its own so no other
 * test sees the settings. */

#include <config.h>

#include <string.h>
#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <X11/Xlib.h>

#include <libnemo-private/nemo-appearance.h>
#include <libnemo-private/nemo-config.h>
#include <libnemo-private/nemo-global-preferences.h>

#include "test-scratch.h"
#include "test-check.h"

static char *data_home;

static Display *mgr_display;
static Window   mgr_window;
static Atom     mgr_prop;
static guint32  mgr_serial;

static void
write_file (const char *path, const char *text, gssize len)
{
	char *dir = g_path_get_dirname (path);

	g_mkdir_with_parents (dir, 0755);
	g_free (dir);

	if (!g_file_set_contents (path, text, len, NULL)) {
		g_printerr ("could not write %s\n", path);
		failures++;
	}
}

static void
make_icon_theme (const char *name)
{
	GdkPixbuf *pixbuf;
	char      *path;
	char      *text;

	path = g_build_filename (data_home, "icons", name, "index.theme", NULL);
	text = g_strdup_printf ("[Icon Theme]\nName=%s\nInherits=hicolor\n"
				"Directories=48x48/places\n\n"
				"[48x48/places]\nSize=48\nContext=Places\nType=Fixed\n", name);
	write_file (path, text, -1);
	g_free (text);
	g_free (path);

	path = g_build_filename (data_home, "icons", name, "48x48", "places", NULL);
	g_mkdir_with_parents (path, 0755);
	g_free (path);

	path = g_build_filename (data_home, "icons", name, "48x48", "places", "folder.png", NULL);
	pixbuf = gdk_pixbuf_new (GDK_COLORSPACE_RGB, TRUE, 8, 48, 48);
	gdk_pixbuf_fill (pixbuf, 0x808080ff);
	check (gdk_pixbuf_save (pixbuf, path, "png", NULL, NULL));
	g_object_unref (pixbuf);
	g_free (path);
}

/* Installed where GTK looks by itself, so it is a desktop theme like any other. */
static void
make_widget_theme (const char *name)
{
	char *path = g_build_filename (data_home, "themes", name, "gtk-3.0", "gtk.css", NULL);

	write_file (path, "/* test */\n", -1);
	g_free (path);
}

static void
put_card32 (GByteArray *buf, guint32 value)
{
	g_byte_array_append (buf, (const guint8 *) &value, 4);
}

static void
put_padded (GByteArray *buf, const char *text)
{
	static const guint8 zeros[4] = { 0 };
	gsize len = strlen (text);

	g_byte_array_append (buf, (const guint8 *) text, (guint) len);
	g_byte_array_append (buf, zeros, (guint) ((4 - len % 4) % 4));
}

/* The XSETTINGS wire format, strings only, in this host's byte order. */
static void
desktop_says (const char *icon_theme, const char *widget_theme)
{
	const char *names[] = { "Net/IconThemeName", "Net/ThemeName" };
	const char *values[] = { icon_theme, widget_theme };
	GByteArray *buf = g_byte_array_new ();
	guint8      head[4] = { G_BYTE_ORDER == G_LITTLE_ENDIAN ? LSBFirst : MSBFirst, 0, 0, 0 };
	int         i;

	mgr_serial++;
	g_byte_array_append (buf, head, 4);
	put_card32 (buf, mgr_serial);
	put_card32 (buf, G_N_ELEMENTS (names));

	for (i = 0; i < (int) G_N_ELEMENTS (names); i++) {
		guint8  type[2] = { 1, 0 };
		guint16 len = (guint16) strlen (names[i]);

		g_byte_array_append (buf, type, 2);
		g_byte_array_append (buf, (const guint8 *) &len, 2);
		put_padded (buf, names[i]);
		put_card32 (buf, mgr_serial);
		put_card32 (buf, (guint32) strlen (values[i]));
		put_padded (buf, values[i]);
	}

	XChangeProperty (mgr_display, mgr_window, mgr_prop, mgr_prop, 8, PropModeReplace,
			 buf->data, (int) buf->len);
	XSync (mgr_display, False);
	g_byte_array_free (buf, TRUE);
}

/* Before gtk_init, so GDK finds a manager already there. */
static gboolean
become_manager (void)
{
	Atom selection;

	mgr_display = XOpenDisplay (NULL);
	if (mgr_display == NULL) {
		return FALSE;
	}

	selection = XInternAtom (mgr_display, "_XSETTINGS_S0", False);
	mgr_prop = XInternAtom (mgr_display, "_XSETTINGS_SETTINGS", False);
	if (XGetSelectionOwner (mgr_display, selection) != None) {
		return FALSE;
	}

	mgr_window = XCreateSimpleWindow (mgr_display, DefaultRootWindow (mgr_display),
					  0, 0, 1, 1, 0, 0, 0);
	XSetSelectionOwner (mgr_display, selection, mgr_window, CurrentTime);
	XSync (mgr_display, False);

	return XGetSelectionOwner (mgr_display, selection) == mgr_window;
}

/* Which theme the folder icon comes from now. */
static char *
folder_theme (void)
{
	GtkIconInfo *info;
	char        *out = NULL;

	info = gtk_icon_theme_lookup_icon (gtk_icon_theme_get_default (), "folder", 48, 0);
	if (info != NULL) {
		const char *file = gtk_icon_info_get_filename (info);

		if (file != NULL) {
			char *places = g_path_get_dirname (file);
			char *size = g_path_get_dirname (places);
			char *theme = g_path_get_dirname (size);

			out = g_path_get_basename (theme);
			g_free (theme);
			g_free (size);
			g_free (places);
		}
		g_object_unref (info);
	}

	return out;
}

static char *
widget_theme (void)
{
	char *name = NULL;

	g_object_get (gtk_settings_get_default (), "gtk-theme-name", &name, NULL);
	return name;
}

static gboolean
showing (const char *icon_theme, const char *widget)
{
	char     *icon = folder_theme ();
	char     *gtk = widget_theme ();
	gboolean  same = g_strcmp0 (icon, icon_theme) == 0 && g_strcmp0 (gtk, widget) == 0;

	g_free (icon);
	g_free (gtk);
	return same;
}

/* The change comes in as an X event, so let GDK read it. A wrong answer waits
 * out the whole limit and then fails, which is the point. */
static gboolean
settle_on (const char *icon_theme, const char *widget)
{
	gint64 until = g_get_monotonic_time () + (gint64) (5 * G_USEC_PER_SEC * test_slowness ());

	while (!showing (icon_theme, widget)) {
		if (g_get_monotonic_time () > until) {
			char *icon = folder_theme ();
			char *gtk = widget_theme ();

			g_printerr ("want icons %s, widgets %s; showing %s, %s\n",
				    icon_theme, widget, icon, gtk);
			g_free (icon);
			g_free (gtk);
			return FALSE;
		}
		while (g_main_context_iteration (NULL, FALSE)) {
		}
		g_usleep (10000);
	}

	return TRUE;
}

static void
choose (const char *key, const char *name)
{
	nemo_config_set_string (nemo_appearance_preferences, key, name);
}

int
main (int argc, char *argv[])
{
	char *tmp;
	char *system_data;
	char *hicolor;

	test_own_display (argc, argv, NULL);

	tmp = test_scratch_config_home ("nemo-appearance-desktop-XXXXXX");
	data_home = g_build_filename (tmp, "data", NULL);
	g_setenv ("XDG_DATA_HOME", data_home, TRUE);

	/* Nothing from the box, only an empty hicolor so GTK doesn't warn. */
	system_data = g_build_filename (tmp, "system-data", NULL);
	hicolor = g_build_filename (system_data, "icons", "hicolor", "index.theme", NULL);
	write_file (hicolor, "[Icon Theme]\nName=Hicolor\nDirectories=\n", -1);
	g_setenv ("XDG_DATA_DIRS", system_data, TRUE);

	make_icon_theme ("ZzDeskOne");
	make_icon_theme ("ZzDeskTwo");
	make_icon_theme ("ZzDeskThree");
	make_icon_theme ("ZzPicked");
	make_widget_theme ("ZzWidgetOne");
	make_widget_theme ("ZzWidgetTwo");
	make_widget_theme ("ZzWidgetPicked");

	if (!become_manager ()) {
		g_print ("SKIP: no X display, or a settings manager is already running\n");
		return 77;
	}
	desktop_says ("ZzDeskOne", "ZzWidgetOne");

	if (!gtk_init_check (&argc, &argv) || !GDK_IS_X11_DISPLAY (gdk_display_get_default ())) {
		g_print ("SKIP: no X11 display\n");
		return 77;
	}

	nemo_global_preferences_init ();
	nemo_appearance_init ();

	check (settle_on ("ZzDeskOne", "ZzWidgetOne"));

	/* Nothing picked: the desktop's change shows up while running. */
	desktop_says ("ZzDeskTwo", "ZzWidgetTwo");
	check (settle_on ("ZzDeskTwo", "ZzWidgetTwo"));

	/* A pick wins over the desktop for as long as it stands. */
	choose (NEMO_PREFERENCES_APPEARANCE_ICON_THEME, "ZzPicked");
	choose (NEMO_PREFERENCES_APPEARANCE_GTK_THEME, "ZzWidgetPicked");
	check (settle_on ("ZzPicked", "ZzWidgetPicked"));
	desktop_says ("ZzDeskThree", "ZzWidgetOne");
	check (settle_on ("ZzPicked", "ZzWidgetPicked"));

	/* Cleared, it's the desktop's answer from now, and the desktop leads again. */
	choose (NEMO_PREFERENCES_APPEARANCE_ICON_THEME, "");
	choose (NEMO_PREFERENCES_APPEARANCE_GTK_THEME, "");
	check (settle_on ("ZzDeskThree", "ZzWidgetOne"));
	desktop_says ("ZzDeskOne", "ZzWidgetTwo");
	check (settle_on ("ZzDeskOne", "ZzWidgetTwo"));

	XCloseDisplay (mgr_display);
	g_free (hicolor);
	g_free (system_data);
	g_free (data_home);
	g_free (tmp);

	if (failures > 0) {
		g_printerr ("%d check(s) failed\n", failures);
		return 1;
	}

	g_print ("OK\n");
	return 0;
}
