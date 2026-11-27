/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-instances-serve.c - what the copies offer each other, served once.

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

/* Each interface the copies offer is served once, however many times the
 * service starts. On Windows every start served the settings and actions
 * interfaces again, so a second start had each one twice. */

#include <config.h>

#include <gio/gio.h>

#include <libnemo-private/nemo-instances.h>

#include "test-check.h"

static const char probe_xml[] =
	"<node><interface name='org.NemoAnywhere.ServeProbe'>"
	"<method name='Poke'/>"
	"</interface></node>";

static GVariant *
probe (G_GNUC_UNUSED const char *method_name,
       G_GNUC_UNUSED GVariant   *parameters,
       G_GNUC_UNUSED gpointer    user_data,
       G_GNUC_UNUSED GError    **error)
{
	return NULL;
}

int
main (void)
{
	GApplication *app;
	guint before;
	gboolean up;

	nemo_instances_serve (probe_xml, probe, NULL);
	nemo_instances_serve (probe_xml, probe, GINT_TO_POINTER (1));
	check (nemo_instances_served_count_for_test () == 1);

	app = g_application_new ("org.NemoAnywhere.ServeTest", G_APPLICATION_NON_UNIQUE);
	before = nemo_instances_served_count_for_test ();
	up = nemo_instances_start (app);
#ifdef G_OS_WIN32
	check (up);
#endif
	nemo_instances_stop ();
	check (nemo_instances_start (app) == up);
	nemo_instances_stop ();
#ifdef G_OS_WIN32
	/* The settings and actions interfaces, once each. */
	check (nemo_instances_served_count_for_test () == before + 2);
#else
	/* Linux serves those through its own registrations. */
	check (nemo_instances_served_count_for_test () == before);
#endif
	g_object_unref (app);

	if (failures > 0) {
		g_printerr ("%d check(s) failed\n", failures);
		return 1;
	}
	g_print ("PASS\n");
	return 0;
}
