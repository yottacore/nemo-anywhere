/* Guards the no-session-bus path: on a headless/minimal system, or Windows
 * where D-Bus autolaunch is unavailable, g_application_get_dbus_connection is
 * NULL and nemo_dbus_manager_new() must not trip a GLib assertion by exporting
 * onto it. Forcing DBUS_SESSION_BUS_ADDRESS=disabled: reproduces that on any
 * host. CRITICAL/WARNING are made fatal so a regression aborts the test. */

#include <gio/gio.h>
#include <libnemo-private/nemo-dbus-manager.h>

int
main (int argc, char **argv)
{
	g_setenv ("DBUS_SESSION_BUS_ADDRESS", "disabled:", TRUE);
	g_log_set_always_fatal (G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_WARNING);

	GApplication *app = g_application_new ("org.NemoAnywhere.dbustest",
					       G_APPLICATION_HANDLES_OPEN);
	GError *error = NULL;

	if (!g_application_register (app, NULL, &error)) {
		g_printerr ("FAIL     register with no bus: %s\n",
			    error ? error->message : "(no error)");
		return 1;
	}
	g_clear_error (&error);

	/* cppcheck-suppress leakReturnValNotUsed ; the connection is owned by app, not returned to the caller */
	if (g_application_get_dbus_connection (app) != NULL) {
		g_printerr ("FAIL     expected NULL dbus connection with disabled bus\n");
		return 1;
	}
	if (g_application_get_is_remote (app)) {
		g_printerr ("FAIL     no-bus instance should be primary, not remote\n");
		return 1;
	}

	/* the actual guard under test: must not warn/assert with a NULL connection */
	NemoDBusManager *mgr = nemo_dbus_manager_new ();
	g_object_unref (mgr);

	g_print ("ok       nemo_dbus_manager_new survives with no session bus\n");
	g_object_unref (app);
	return 0;
}
