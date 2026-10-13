/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* eel-gnome-extensions.h - interface for new functions that operate on
                                 gnome classes. Perhaps some of these should be
  			         rolled into gnome someday.

   Copyright (C) 1999, 2000, 2001 Eazel, Inc.

   The Gnome Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   The Gnome Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public
   License along with the Gnome Library; see the file COPYING.LIB.  If not,
   write to the Free Software Foundation, Inc., 51 Franklin Street - Suite 500,
   Boston, MA 02110-1335, USA.

   Authors: Darin Adler <darin@eazel.com>
*/

#ifndef EEL_GNOME_EXTENSIONS_H
#define EEL_GNOME_EXTENSIONS_H

#include <gtk/gtk.h>

/* Open up a new terminal, optionally passing in a command to execute. A NULL
   context makes one for the screen. */
void          eel_gnome_open_terminal_on_screen                       (const char               *command,
								       GdkScreen                *screen,
								       GAppLaunchContext        *context);

/* First of the known terminals present on PATH, or NULL if there is none.
   The fallback the launch path already used, so callers that only honored a
   configured terminal can agree with it. Caller frees. */
char *        eel_gnome_get_fallback_terminal_exec                    (void);
								 
#endif /* EEL_GNOME_EXTENSIONS_H */
