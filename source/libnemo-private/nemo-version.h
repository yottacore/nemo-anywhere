/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-version.h - the version line people see.

   Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu).

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; version 2 of the
   License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, see <http://www.gnu.org/licenses/>.
*/

#ifndef NEMO_VERSION_H
#define NEMO_VERSION_H

#include <glib.h>
#include <nemo-build-number.h>

/* "v<version> build <number>", and on a build that isn't a release the build
   date after it. The date is the viewer's local day, so it can't be baked in. */
static inline const char *
nemo_version_string (void)
{
#if NEMO_RELEASE_BUILD
	return NEMO_VERSION_STRING;
#else
	static char *text;

	if (text == NULL) {
		GDateTime *when = g_date_time_new_from_unix_local (NEMO_BUILD_EPOCH);
		char *day = when != NULL ? g_date_time_format (when, "%Y-%m-%d") : NULL;

		text = day != NULL ? g_strdup_printf ("%s (%s)", NEMO_VERSION_STRING, day)
				   : g_strdup (NEMO_VERSION_STRING);
		g_free (day);
		if (when != NULL) {
			g_date_time_unref (when);
		}
	}

	return text;
#endif
}

#endif /* NEMO_VERSION_H */
