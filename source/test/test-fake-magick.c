/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-fake-magick.c - stands in for ImageMagick in test-nemo-magick-win32.

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

/* Reads stdin to the end, writes a 3x2 PNG, and notes in the file named by
 * NEMO_FAKE_MAGICK_REPORT whether it was given a console window, how many
 * bytes it read and the arguments it got. NEMO_FAKE_MAGICK_SLEEP makes it
 * hang and NEMO_FAKE_MAGICK_FAIL makes it fail. */

#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>

static const unsigned char picture[] = {
	0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d,
	0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x02,
	0x08, 0x02, 0x00, 0x00, 0x00, 0x12, 0x16, 0xf1, 0x4d, 0x00, 0x00, 0x00,
	0x10, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0xf8, 0xcf, 0xc0, 0x00,
	0x41, 0x0c, 0x70, 0x16, 0x00, 0x41, 0xd2, 0x05, 0xfb, 0x87, 0xf0, 0xb9,
	0x48, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60,
	0x82
};

int
main (int argc, char *argv[])
{
	const char *report_path = getenv ("NEMO_FAKE_MAGICK_REPORT");
	char buffer[4096];
	size_t got, total = 0;
	FILE *report;
	int i;

	_setmode (_fileno (stdin), _O_BINARY);
	_setmode (_fileno (stdout), _O_BINARY);

	while ((got = fread (buffer, 1, sizeof buffer, stdin)) > 0) {
		total += got;
	}

	if (report_path != NULL && (report = fopen (report_path, "wb")) != NULL) {
		fprintf (report, "window=%d\nbytes=%lu\n", GetConsoleWindow () != NULL,
			 (unsigned long) total);
		for (i = 1; i < argc; i++) {
			fprintf (report, "arg=%s\n", argv[i]);
		}
		fclose (report);
	}

	if (getenv ("NEMO_FAKE_MAGICK_SLEEP") != NULL) {
		Sleep (120000);
	}
	if (getenv ("NEMO_FAKE_MAGICK_FAIL") != NULL) {
		return 1;
	}

	fwrite (picture, 1, sizeof picture, stdout);
	return 0;
}
