/* The list of running copies has every other copy in it and not the one
 * asking, on whatever bus the session has. 3 connections publish the way a
 * copy does and each lists the others. The middle one drops off the bus as a
 * crash would, its slot passes up, and the 2 left see only each other. Then
 * the first leaves cleanly and a fourth joins. GLib's bus, the one Windows
 * runs, answered the old queue read with an empty list. Names are compared by
 * owner, so a copy of the app running on the same bus changes nothing.
 * Starts a bus under dbus-run-session where the environment has none. */

#include <config.h>

#include <gio/gio.h>

#ifndef G_OS_WIN32
#include <unistd.h>
#endif

#include <libnemo-private/nemo-instances.h>

#include "test-scratch.h"
#include "test-check.h"

#define COPIES 4
#define WAIT_SECONDS 10

static char *
owner_of (GDBusConnection *bus, const char *name)
{
	GVariant *reply;
	char *owner = NULL;

	reply = g_dbus_connection_call_sync (bus, "org.freedesktop.DBus", "/org/freedesktop/DBus",
	                                     "org.freedesktop.DBus", "GetNameOwner",
	                                     g_variant_new ("(s)", name), G_VARIANT_TYPE ("(s)"),
	                                     G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL);
	if (reply != NULL) {
		g_variant_get (reply, "(s)", &owner);
		g_variant_unref (reply);
	}
	return owner;
}

/* Whether the list from copies[asker] has exactly the live copies other than
 * itself, by owner. Outsiders are left out of the count. */
static gboolean
sees_the_others (GDBusConnection **copies, int asker, gboolean show)
{
	GStrv others = nemo_instance_list_others (copies[asker]);
	gboolean found[COPIES] = { FALSE };
	gboolean right = TRUE;
	int i, j;

	for (i = 0; others[i] != NULL; i++) {
		char *owner = owner_of (copies[asker], others[i]);

		for (j = 0; j < COPIES; j++) {
			if (copies[j] != NULL && owner != NULL &&
			    g_strcmp0 (owner, g_dbus_connection_get_unique_name (copies[j])) == 0) {
				found[j] = TRUE;
			}
		}
		g_free (owner);
	}
	for (j = 0; j < COPIES; j++) {
		gboolean wanted = copies[j] != NULL && j != asker;

		if (found[j] != wanted) {
			right = FALSE;
			if (show) {
				g_printerr ("copy %d (%s) %s copy %d in its list of %u\n", asker,
				            g_dbus_connection_get_unique_name (copies[asker]),
				            wanted ? "is missing" : "wrongly has", j,
				            g_strv_length (others));
			}
		}
	}
	g_strfreev (others);
	return right;
}

/* Publishing is async, so give the bus a while to catch up. */
static gboolean
all_see_the_others (GDBusConnection **copies)
{
	gint64 end = g_get_monotonic_time () +
	             (gint64) (WAIT_SECONDS * test_slowness () * G_USEC_PER_SEC);
	gboolean right = FALSE;

	while (!right && g_get_monotonic_time () < end) {
		while (g_main_context_iteration (NULL, FALSE)) {
		}
		right = TRUE;
		for (int i = 0; i < COPIES; i++) {
			if (copies[i] != NULL && !sees_the_others (copies, i, FALSE)) {
				right = FALSE;
			}
		}
		if (!right) {
			g_usleep (100 * 1000);
		}
	}
	if (!right) {
		for (int i = 0; i < COPIES; i++) {
			if (copies[i] != NULL) {
				sees_the_others (copies, i, TRUE);
			}
		}
	}
	return right;
}

static char *
session_address (int argc, char **argv)
{
	char *address = g_dbus_address_get_for_bus_sync (G_BUS_TYPE_SESSION, NULL, NULL);
	GDBusConnection *probe = NULL;

	if (address != NULL) {
		probe = g_dbus_connection_new_for_address_sync (address,
		                                                G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
		                                                G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION,
		                                                NULL, NULL, NULL);
	}
	if (probe != NULL) {
		g_dbus_connection_close_sync (probe, NULL, NULL);
		g_object_unref (probe);
		return address;
	}
	g_free (address);

#ifndef G_OS_WIN32
	/* meson hands every test a bus address that goes nowhere. */
	if (g_getenv ("NEMO_TEST_BUS_RELAUNCHED") == NULL) {
		char *dbus_run = g_find_program_in_path ("dbus-run-session");
		char **relaunch;

		if (dbus_run != NULL) {
			g_setenv ("NEMO_TEST_BUS_RELAUNCHED", "1", TRUE);
			relaunch = g_new0 (char *, argc + 3);
			relaunch[0] = dbus_run;
			relaunch[1] = (char *) "--";
			for (int i = 0; i < argc; i++) {
				relaunch[i + 2] = argv[i];
			}
			execv (dbus_run, relaunch);
			g_free (relaunch);
			g_free (dbus_run);
		}
	}
#else
	(void) argc;
	(void) argv;
#endif
	return NULL;
}

static GDBusConnection *
join (const char *address, guint *shared_id, guint *slots)
{
	GError *error = NULL;
	GDBusConnection *connection;

	connection = g_dbus_connection_new_for_address_sync (address,
	                                                     G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
	                                                     G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION,
	                                                     NULL, NULL, &error);
	if (connection == NULL) {
		g_printerr ("no connection: %s\n", error->message);
		g_clear_error (&error);
		return NULL;
	}
	*slots = nemo_instance_publish (connection, shared_id);
	return connection;
}

static gboolean
slot_held_by (GDBusConnection *asker, guint slot, GDBusConnection *holder)
{
	char *name = g_strdup_printf (NEMO_INSTANCE_SLOT_PREFIX "%u", slot);
	char *owner = owner_of (asker, name);
	gboolean held = g_strcmp0 (owner, g_dbus_connection_get_unique_name (holder)) == 0;

	g_free (owner);
	g_free (name);
	return held;
}

int
main (int argc, char **argv)
{
	GDBusConnection *copies[COPIES] = { NULL };
	guint shared_ids[COPIES] = { 0 };
	guint slots[COPIES] = { 0 };
	char *address;

	address = session_address (argc, argv);
	if (address == NULL) {
		g_print ("SKIP: no session bus\n");
		return 77;
	}

	for (int i = 0; i < 3; i++) {
		copies[i] = join (address, &shared_ids[i], &slots[i]);
		check (copies[i] != NULL);
		if (copies[i] == NULL) {
			return 1;
		}
	}
	check (all_see_the_others (copies));

	/* Gone with no goodbye, as a crash would be. Its slot passes to the copy
	 * above it, so the walk does not stop short of that one. */
	g_dbus_connection_close_sync (copies[1], NULL, NULL);
	g_clear_object (&copies[1]);
	check (all_see_the_others (copies));
	if (slots[1] > 0) {
		check (slot_held_by (copies[0], slots[1] - 1, copies[2]));
	}

	/* The first leaves cleanly, and a newcomer takes the first free slot. */
	nemo_instance_unpublish (copies[0], shared_ids[0], slots[0]);
	g_dbus_connection_close_sync (copies[0], NULL, NULL);
	g_clear_object (&copies[0]);
	copies[3] = join (address, &shared_ids[3], &slots[3]);
	check (copies[3] != NULL);
	check (all_see_the_others (copies));
	g_free (address);

	for (int i = 0; i < COPIES; i++) {
		if (copies[i] != NULL) {
			nemo_instance_unpublish (copies[i], shared_ids[i], slots[i]);
			g_dbus_connection_close_sync (copies[i], NULL, NULL);
			g_object_unref (copies[i]);
		}
	}

	if (failures == 0) {
		g_print ("OK\n");
	}
	return failures == 0 ? 0 : 1;
}
