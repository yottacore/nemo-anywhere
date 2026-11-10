/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-security-win32.h - where a file's Windows permissions come from, and
   who owns it.

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

#ifndef NEMO_SECURITY_WIN32_H
#define NEMO_SECURITY_WIN32_H

#include <glib.h>

G_BEGIN_DECLS

typedef enum {
	NEMO_WIN32_PERM_SOURCE_NOT_COMPUTED = 0,
	NEMO_WIN32_PERM_SOURCE_NONE,	/* no ACL to speak of, or it couldn't be read */
	NEMO_WIN32_PERM_SOURCE_INHERITED,
	NEMO_WIN32_PERM_SOURCE_LOCAL,
	NEMO_WIN32_PERM_SOURCE_MIXED
} NemoWin32PermSource;

/* Never returns NOT_COMPUTED - that value is for callers caching the answer. */
NemoWin32PermSource nemo_security_win32_permissions_source (const char *path);

/* The full name of the local account that owns path, or NULL when the owner
   isn't a local user or has no full name. */
char *nemo_security_win32_owner_full_name (const char *path);

G_END_DECLS

#endif /* NEMO_SECURITY_WIN32_H */
