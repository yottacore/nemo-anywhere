/* eel_gtk_primary_mask has to agree with what GTK reads <Primary> as, since the
 * menu accelerators use <Primary> and the click and key handlers use the mask.
 * If they drift, a shortcut and the matching Ctrl+click (Cmd on macOS) stop
 * using the same key.
 *
 * Off macOS both must still be plain Control, so nothing moved for Linux or
 * Windows. Needs a display for the second half and skips without one. */

#include <config.h>

#include <gtk/gtk.h>
#include <eel/eel-gtk-extensions.h>

static int failures = 0;

static void
expect_mask (GdkModifierType got, GdkModifierType want, const char *what)
{
	if (got != want) {
		g_print ("FAIL: %s: got 0x%x, want 0x%x\n", what, got, want);
		failures++;
	}
}

/* A key can be named by its real modifier bit, as event state has it, or by
 * a virtual one, as an accelerator has it. On macOS Command is Mod2 in an
 * event and Meta in <Primary>. Both spellings, so either form compares. */
static GdkModifierType
both_spellings (GdkModifierType mask)
{
	GdkKeymap *keymap = gdk_keymap_get_default ();

	gdk_keymap_add_virtual_modifiers (keymap, &mask);
	gdk_keymap_map_virtual_modifiers (keymap, &mask);
	return mask;
}

int
main (int argc, char *argv[])
{
	GdkModifierType parsed;
	guint key;
	GtkWidget *window;

	/* Before there is a display, which is when the rename label's bindings
	 * could first be built. */
	expect_mask (eel_gtk_primary_mask (NULL), GDK_CONTROL_MASK, "no display");

	if (!gtk_init_check (&argc, &argv)) {
		g_print ("SKIP: no display\n");
		return failures ? 1 : 77;
	}

	gtk_accelerator_parse ("<Primary>a", &key, &parsed);
	expect_mask (both_spellings (eel_gtk_primary_mask (NULL)), both_spellings (parsed),
		     "matches <Primary>");

	window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	gtk_widget_realize (window);
	expect_mask (both_spellings (eel_gtk_primary_mask (gtk_widget_get_window (window))),
		     both_spellings (parsed), "from a window");
	gtk_widget_destroy (window);

#ifdef __APPLE__
	/* Command, as a click's event state has it. */
	expect_mask (eel_gtk_primary_mask (NULL), GDK_MOD2_MASK, "the mask is Command here");
#else
	expect_mask (eel_gtk_primary_mask (NULL), GDK_CONTROL_MASK, "the mask is Control here");
	expect_mask (parsed, GDK_CONTROL_MASK, "<Primary> is Control here");
#endif

	if (failures == 0) {
		g_print ("PASS\n");
	}
	return failures ? 1 : 0;
}
