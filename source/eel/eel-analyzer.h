/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* eel-analyzer.h - quiet a static analyzer finding at one statement.

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

#ifndef EEL_ANALYZER_H
#define EEL_ANALYZER_H

/* For a finding that's wrong, with the reason on the same line:
 *
 *	EEL_ANALYZER_SKIP ("null-dereference") // why it can't be NULL
 *	link->prev->next = link->next;
 *	EEL_ANALYZER_END
 *
 * The argument is gcc's -Wanalyzer- name. clang skips the one statement after
 * it whatever the name, so give a clang-only finding gcc's nearest one. Both
 * compilers' plain builds see nothing. */

#if defined(__clang_analyzer__)
#define EEL_ANALYZER_SKIP(gcc_name) [[clang::suppress]]
#define EEL_ANALYZER_END
#elif defined(__GNUC__) && !defined(__clang__)
#define EEL_ANALYZER_PRAGMA(text) _Pragma (#text)
#define EEL_ANALYZER_SKIP(gcc_name) \
	_Pragma ("GCC diagnostic push") \
	EEL_ANALYZER_PRAGMA (GCC diagnostic ignored "-Wanalyzer-" gcc_name)
#define EEL_ANALYZER_END _Pragma ("GCC diagnostic pop")
#else
#define EEL_ANALYZER_SKIP(gcc_name)
#define EEL_ANALYZER_END
#endif

#endif /* EEL_ANALYZER_H */
