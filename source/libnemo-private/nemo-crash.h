/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-crash.h - write a report when the program dies unexpectedly.

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

#ifndef NEMO_CRASH_H
#define NEMO_CRASH_H

#include <glib.h>

G_BEGIN_DECLS

/* Install once, as early in main as possible. Also says whether an earlier run
   left a report behind, and drops the oldest so the folder can't grow forever.

   NEMO_NO_CRASH_HANDLER installs nothing at all. NEMO_NO_CRASH_DIALOG keeps the
   report but drops the message box a windowed build puts up, for anything
   running unattended. */
void nemo_crash_handler_install (void);

/* Where this run would write a report. The name is settled at install time and
   nothing is created until there is a crash, so the path is an intention, not a
   file that exists. NULL when no handler was installed. */
const char *nemo_crash_report_path (void);

G_END_DECLS

#endif /* NEMO_CRASH_H */
