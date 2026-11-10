/* The rule behind the drop confirmation: which action, and which setting,
 * and the trash and delete test guard exceptions. The dialog itself isn't driven here - only the
 * decision that puts it up. */

#include <config.h>

#include <gtk/gtk.h>
#include <glib/gstdio.h>

#include <libnemo-private/nemo-config.h>
#include <libnemo-private/nemo-dnd.h>
#include <libnemo-private/nemo-delete-testguard.h>
#include <libnemo-private/nemo-global-preferences.h>

#include "test-scratch.h"
#include "test-check.h"

static void
set_prefs (gboolean move, gboolean copy)
{
	nemo_config_set_boolean (nemo_preferences, NEMO_PREFERENCES_CONFIRM_DRAG_MOVE, move);
	nemo_config_set_boolean (nemo_preferences, NEMO_PREFERENCES_CONFIRM_DRAG_COPY, copy);
}

static void
set_guard (gboolean on)
{
	nemo_config_set_boolean (nemo_config_get_group (NEMO_DEBUG_GROUP),
				 NEMO_PREFERENCES_TESTGUARD_ALL_DELETES, on);
}

/* Armed, the guard asks about the move itself, so the drop doesn't. A copy
   isn't the guard's business and still asks. */
static void
check_armed (void)
{
	gboolean armed;

	set_prefs (TRUE, TRUE);
	set_guard (TRUE);
	armed = nemo_delete_testguard_armed ();
	check (armed);
	if (!armed) {
		return;
	}
	check (!nemo_drag_confirm_needed (GDK_ACTION_MOVE, "file:///tmp"));
	check (nemo_drag_confirm_needed (GDK_ACTION_COPY, "file:///tmp"));
}

int
main (int argc, char *argv[])
{
	char *tmp;

	tmp = test_scratch_config_home ("nemo-drag-confirm-test-XXXXXX");

	/* The guard is on by default and takes over the move question, so it is
	   off for everything but its own check at the end. The variable would beat
	   the setting, and it is read once, so it goes before anything asks. */
	g_unsetenv (NEMO_TESTGUARD_ENV_VAR);

	gtk_init (&argc, &argv);
	nemo_global_preferences_init ();
	set_guard (FALSE);

	/* A build compiled armed can't be quieted, so the rest has nothing to
	   say about it. */
	if (NEMO_TESTGUARD_ALL_DELETES) {
		check_armed ();
		goto done;
	}

	/* Defaults: a move asks, a copy doesn't. */
	check (nemo_drag_confirm_needed (GDK_ACTION_MOVE, "file:///tmp"));
	check (!nemo_drag_confirm_needed (GDK_ACTION_COPY, "file:///tmp"));
	check (!nemo_drag_confirm_needed (GDK_ACTION_LINK, "file:///tmp"));

	set_prefs (TRUE, TRUE);
	check (nemo_drag_confirm_needed (GDK_ACTION_MOVE, "file:///tmp"));
	check (nemo_drag_confirm_needed (GDK_ACTION_COPY, "file:///tmp"));
	/* Links followed copy until 20260924, when a link drop started opening
	   the Make link dialog, which is its own question:
	check (nemo_drag_confirm_needed (GDK_ACTION_LINK, "file:///tmp"));
	check (nemo_drag_confirm_needed (GDK_ACTION_DEFAULT, "file:///tmp"));
	*/
	check (!nemo_drag_confirm_needed (GDK_ACTION_LINK, "file:///tmp"));
	check (!nemo_drag_confirm_needed (GDK_ACTION_DEFAULT, "file:///tmp"));

	set_prefs (FALSE, FALSE);
	check (!nemo_drag_confirm_needed (GDK_ACTION_MOVE, "file:///tmp"));
	check (!nemo_drag_confirm_needed (GDK_ACTION_COPY, "file:///tmp"));

	/* Each setting applies only to its own action. */
	set_prefs (TRUE, FALSE);
	check (nemo_drag_confirm_needed (GDK_ACTION_MOVE, "file:///tmp"));
	check (!nemo_drag_confirm_needed (GDK_ACTION_COPY, "file:///tmp"));
	set_prefs (FALSE, TRUE);
	check (!nemo_drag_confirm_needed (GDK_ACTION_MOVE, "file:///tmp"));
	check (nemo_drag_confirm_needed (GDK_ACTION_COPY, "file:///tmp"));

	/* The trash asks for itself, whatever these say. */
	set_prefs (TRUE, TRUE);
	check (!nemo_drag_confirm_needed (GDK_ACTION_MOVE, "trash:///"));
	check (!nemo_drag_confirm_needed (GDK_ACTION_COPY, "trash:///"));

	/* Nothing to ask about for an action no drop performs. */
	check (!nemo_drag_confirm_needed (GDK_ACTION_PRIVATE, "file:///tmp"));
	check (!nemo_drag_confirm_needed (GDK_ACTION_ASK, "file:///tmp"));

	/* An empty drop goes through without a dialog, so this can't block. */
	check (nemo_drag_confirm_drop (NULL, GDK_ACTION_MOVE, NULL, "file:///tmp"));
	/* Same for one the settings say not to ask about. */
	set_prefs (FALSE, FALSE);
	{
		GList *uris = g_list_append (NULL, (gpointer) "file:///tmp/a");
		check (nemo_drag_confirm_drop (NULL, GDK_ACTION_MOVE, uris, "file:///tmp"));
		g_list_free (uris);
	}

	check_armed ();

done:
	nemo_config_shutdown ();
	g_free (tmp);

	if (failures == 0)
		g_print ("nemo-drag-confirm: all checks passed\n");

	return failures == 0 ? 0 : 1;
}
