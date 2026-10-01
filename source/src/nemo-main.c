/* -*- Mode: C; tab-width: 8; indent-tabs-mode: 8; c-basic-offset: 8 -*- */

/*
 * Nemo
 *
 * Copyright (C) 1999, 2000 Red Hat, Inc.
 * Copyright (C) 1999, 2000 Eazel, Inc.
 *
 * Nemo is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * Nemo is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Suite 500, MA 02110-1335, USA.
 *
 * Authors: Elliot Lee <sopwith@redhat.com>,
 *          Darin Adler <darin@bentspoon.com>,
 *          John Sullivan <sullivan@eazel.com>
 *
 */

/* nemo-main.c: Implementation of the routines that drive program lifecycle and main window creation/destruction. */

#include <config.h>

#include "nemo-main-application.h"
#include "nemo-splash.h"
#ifndef G_OS_WIN32
#include "nemo-extensions-list.h"
#endif

#include <libnemo-private/nemo-file-utilities.h>
#include <libnemo-private/nemo-debug.h>
#include <libnemo-private/nemo-metadata-store.h>
#include <libnemo-private/nemo-config.h>
#include <libnemo-private/nemo-crash.h>
#include <libnemo-private/nemo-cache-db.h>
#include <libnemo-private/nemo-cache-db-prune.h>
#ifdef G_OS_WIN32
#include <libnemo-private/nemo-dnd-win32.h>
#endif
#include <eel/eel-debug.h>

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#ifdef G_OS_UNIX
#include <gio/gdesktopappinfo.h>
#endif

#ifdef HAVE_LOCALE_H
#include <locale.h>
#endif
#ifdef HAVE_MALLOC_H
#include <malloc.h>
#endif
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef HAVE_EXEMPI
#include <exempi/xmp.h>
#endif

/* A flag that answers and exits rather than opening a window. Showing a splash
 * for one would flash a panel on screen for no reason. */
static gboolean
prints_and_exits (int argc, char *argv[])
{
	static const char *quiet[] = {
		"--version", "--about", "--help", "--help-all", "--help-gtk",
		"--quit", "-q", "--check", "-c", "--fix-cache", "--reset", "-?", NULL
	};
	int i, q;

	for (i = 1; i < argc; i++) {
		for (q = 0; quiet[q] != NULL; q++) {
			if (strcmp (argv[i], quiet[q]) == 0) {
				return TRUE;
			}
		}
	}

	return FALSE;
}

int
main (int argc, char *argv[])
{
	gint retval;
	NemoApplication *application;

#if defined (HAVE_MALLOPT) && defined(M_MMAP_THRESHOLD)
	/* Nemo uses lots and lots of small and medium size allocations,
	 * and then a few large ones for the desktop background. By default
	 * glibc uses a dynamic treshold for how large allocations should
	 * be mmaped. Unfortunately this triggers quickly for nemo when
	 * it does the desktop background allocations, raising the limit
	 * such that a lot of temporary large allocations end up on the
	 * heap and are thus not returned to the OS. To fix this we set
	 * a hardcoded limit. I don't know what a good value is, but 128K
	 * was the old glibc static limit, lets use that.
	 */
	mallopt (M_MMAP_THRESHOLD, 128 *1024);
#endif

	nemo_note_process_start ();

	/* Before anything reads a data dir, because GLib caches the list. */
	nemo_setup_runtime_environment ();

	/* Early enough to catch a crash in startup itself. Needs the runtime
	   environment first: the report goes beside the settings file. */
	nemo_crash_handler_install ();

	/* This will be done by gtk+ later, but for now, force it to GNOME */
#ifdef G_OS_UNIX
	g_desktop_app_info_set_desktop_env ("GNOME");
#endif

	if (g_getenv ("NEMO_DEBUG") != NULL) {
		eel_make_warnings_and_criticals_stop_in_debugger ();
	}

#ifndef G_OS_WIN32
	/* Answered before anything else starts up: this run exists only to print the
	 * list and go away, and loading the extensions to do it is one-way. Not in
	 * the option table, because it must not reach a window that is already open. */
	if (argc == 2 && strcmp (argv[1], "--extensions-list") == 0) {
		nemo_extensions_list_print ();
		return 0;
	}
#endif

#ifdef G_OS_WIN32
	/* Freetype's default v40 interpreter hints lighter/thinner than native
	 * Windows text; v35 is the classic grid-fitted GDI/ClearType look.
	 * Must be set before pango and freetype start; a user-set env still wins. */
	g_setenv ("FREETYPE_PROPERTIES", "truetype:interpreter-version=35", FALSE);

	/* Our drags speak the protocol that reaches other programs, so the toolkit
	 * has to be listening on the same one. Only read at startup. */
	nemo_dnd_win32_prepare ();
#endif
	
	/* Initialize gettext support */
	bindtextdomain (GETTEXT_PACKAGE, nemo_get_locale_dir ());
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	/* As early as anything can be said in the user's own language, and well
	 * before gtk_init: on Windows the packed single exe takes a noticeable
	 * moment to get a window up, and an icon that does nothing for several
	 * seconds reads as a failed launch. No-op on every other platform. */
	if (!prints_and_exits (argc, argv)) {
		nemo_splash_show ();
		nemo_splash_note (_("Starting"));
	}

	g_set_prgname (NEMO_APP_SLUG);

#ifdef HAVE_EXEMPI
	xmp_init();
#endif

	/* Run the nemo application. */
	application = nemo_main_application_get_singleton ();

    /* hold indefinitely if we're asked to persist */
    if (g_getenv ("NEMO_PERSIST") != NULL) {
        g_application_hold (G_APPLICATION (application));
    }

	retval = g_application_run (G_APPLICATION (application),
				    argc, argv);

	/* Normally the window's first draw takes it down. This catches the runs
	 * that never get one: an option that does its work and exits, or one
	 * that fails to parse. */
	nemo_splash_hide ();

	/* don't lose a save still sitting in its debounce window */
	nemo_metadata_store_flush ();
	nemo_config_flush ();

	g_object_unref (application);

	/* A prune pass has a connection of its own, and folding the journal in
	 * has to wait for it to finish. */
	nemo_cache_db_prune_stop ();

 	eel_debug_shut_down ();

	/* After the shutdown list, which waits for the thumbnail threads, so the
	 * last thumbnails they stored are folded in too. GApplication's
	 * quit_mainloop is never called, so nothing in there can do this. */
	nemo_cache_db_quit ();

	return retval;
}
