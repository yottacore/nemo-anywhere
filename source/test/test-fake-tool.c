/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-fake-tool.c - stands in for rar, a search converter and a thumbnailer
   in test-nemo-tool-start-win32, and for action programs in
   test-nemo-action-start-win32.

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

/* Reads stdin to the end, unless it is a console, then writes <name>.report in
 * NEMO_FAKE_TOOL_DIR, or beside itself when that is not set, named after the
 * program it was started as: whether it was given a console window, how many
 * bytes reached its stdin, its folder, the program that started it and its
 * arguments.
 *
 * Then, as rar or unrar ("a" or "x" first): creates the archive it was asked for, says
 * " 40%" on stdout, and with NEMO_FAKE_TOOL_FAIL says why on both outputs and
 * exits 2. As a thumbnailer: writes a 3x2 PNG to its last argument. Anything
 * else prints NEMO_FAKE_TOOL_SAYS and exits NEMO_FAKE_TOOL_EXIT, or 0.
 * NEMO_FAKE_TOOL_SLEEP makes it hang after its first output. */

#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <tlhelp32.h>

static const unsigned char picture[] = {
	0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d,
	0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x02,
	0x08, 0x02, 0x00, 0x00, 0x00, 0x12, 0x16, 0xf1, 0x4d, 0x00, 0x00, 0x00,
	0x10, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0xf8, 0xcf, 0xc0, 0x00,
	0x41, 0x0c, 0x70, 0x16, 0x00, 0x41, 0xd2, 0x05, 0xfb, 0x87, 0xf0, 0xb9,
	0x48, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60,
	0x82
};

static void
write_file (const char *path, const void *data, size_t size)
{
	FILE *out = fopen (path, "wb");

	if (out != NULL) {
		fwrite (data, 1, size, out);
		fclose (out);
	}
}

/* Whoever started it, by name. A broker outside the app's own process tree
 * shows up here as explorer.exe or WmiPrvSE.exe. */
static void
parent_name (char *name, size_t size)
{
	HANDLE snapshot = CreateToolhelp32Snapshot (TH32CS_SNAPPROCESS, 0);
	PROCESSENTRY32 entry;
	DWORD self = GetCurrentProcessId (), parent = 0;

	snprintf (name, size, "%s", "?");
	if (snapshot == INVALID_HANDLE_VALUE) {
		return;
	}

	entry.dwSize = sizeof entry;
	for (BOOL more = Process32First (snapshot, &entry); more; more = Process32Next (snapshot, &entry)) {
		if (entry.th32ProcessID == self) {
			parent = entry.th32ParentProcessID;
		}
	}
	entry.dwSize = sizeof entry;
	for (BOOL more = Process32First (snapshot, &entry); more && parent != 0; more = Process32Next (snapshot, &entry)) {
		if (entry.th32ProcessID == parent) {
			snprintf (name, size, "%s", entry.szExeFile);
		}
	}
	CloseHandle (snapshot);
}

static void
hang_if_asked (void)
{
	if (getenv ("NEMO_FAKE_TOOL_SLEEP") != NULL) {
		Sleep (120000);
	}
}

int
main (int argc, char *argv[])
{
	const char *dir = getenv ("NEMO_FAKE_TOOL_DIR");
	char name[MAX_PATH], cwd[MAX_PATH], report_path[2 * MAX_PATH];
	char own_dir[MAX_PATH], parent[MAX_PATH];
	char buffer[4096], *dot;
	const char *base, *slash;
	size_t got, total = 0;
	FILE *report;
	int i;

	_setmode (_fileno (stdin), _O_BINARY);
	_setmode (_fileno (stdout), _O_BINARY);

	/* A console has nothing to read, and would wait for the keyboard. */
	while (GetFileType (GetStdHandle (STD_INPUT_HANDLE)) != FILE_TYPE_CHAR &&
	       (got = fread (buffer, 1, sizeof buffer, stdin)) > 0) {
		total += got;
	}

	/* argv[0], since the single exe runs a program packed inside it from a
	 * copy with a made-up name. */
	base = argv[0];
	if ((slash = strrchr (base, '\\')) != NULL || (slash = strrchr (base, '/')) != NULL) {
		base = slash + 1;
	}
	snprintf (name, sizeof name, "%s", base);
	if ((dot = strrchr (name, '.')) != NULL) {
		*dot = '\0';
	}
	GetCurrentDirectoryA (sizeof cwd, cwd);
	parent_name (parent, sizeof parent);

	/* Started by a broker, it has the desktop's environment, not the test's. */
	if (dir == NULL && GetModuleFileNameA (NULL, own_dir, sizeof own_dir) > 0 &&
	    (slash = strrchr (own_dir, '\\')) != NULL) {
		own_dir[slash - own_dir] = '\0';
		dir = own_dir;
	}

	if (dir != NULL) {
		snprintf (report_path, sizeof report_path, "%s\\%s.report", dir, name);
		if ((report = fopen (report_path, "wb")) != NULL) {
			fprintf (report, "window=%d\nbytes=%lu\ncwd=%s\nparent=%s\n", GetConsoleWindow () != NULL,
				 (unsigned long) total, cwd, parent);
			for (i = 1; i < argc; i++) {
				fprintf (report, "arg=%s\n", argv[i]);
			}
			fclose (report);
		}
	}

	if ((_stricmp (name, "rar") == 0 || _stricmp (name, "unrar") == 0) && argc > 1 &&
	    (strcmp (argv[1], "a") == 0 || strcmp (argv[1], "x") == 0)) {
		for (i = 2; i < argc && strcmp (argv[1], "a") == 0; i++) {
			size_t length = strlen (argv[i]);

			if (argv[i][0] != '-' && length > 4 && _stricmp (argv[i] + length - 4, ".rar") == 0) {
				write_file (argv[i], "Rar!", 4);
				break;
			}
		}

		fputs (" 40%\n", stdout);
		fflush (stdout);
		hang_if_asked ();

		if (getenv ("NEMO_FAKE_TOOL_FAIL") != NULL) {
			fputs ("fake out said no\n", stdout);
			fputs ("fake err said no\n", stderr);
			return 2;
		}
		return 0;
	}

	if (strstr (name, "thumb") != NULL && argc > 1) {
		hang_if_asked ();
		write_file (argv[argc - 1], picture, sizeof picture);
		return 0;
	}

	if (getenv ("NEMO_FAKE_TOOL_SAYS") != NULL) {
		fputs (getenv ("NEMO_FAKE_TOOL_SAYS"), stdout);
	}
	fflush (stdout);
	hang_if_asked ();
	return getenv ("NEMO_FAKE_TOOL_EXIT") != NULL ? atoi (getenv ("NEMO_FAKE_TOOL_EXIT")) : 0;
}
