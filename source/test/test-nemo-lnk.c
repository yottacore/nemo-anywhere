/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* test-nemo-lnk.c - reading, placing and writing Windows shortcuts off Windows.

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

/* Shortcuts are built here byte by byte, and the mount table, the volume id
 * folder and the gvfs folder are fakes in a scratch dir, so nothing depends on
 * what this machine has mounted. */

#include <config.h>

#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <glib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>

#include <libnemo-private/nemo-lnk.h>

#include "test-scratch.h"
#include "test-check.h"

typedef struct {
	guint32     attributes;
	gboolean    id_list;
	const char *local_base;       /* ANSI copy */
	const char *local_base_wide;  /* UTF-16 copy, when set */
	const char *suffix;
	guint32     serial;
	const char *net_share;
	const char *name;
	const char *relative;
	const char *working_dir;
	const char *arguments;
	const char *icon;
	const char *env;              /* the environment variable block */
	gboolean    env_unflagged;    /* the block, without the flag that says to read it */
	gboolean    ansi_strings;
} Spec;

static void
put16 (GByteArray *out, guint v)
{
	guint8 b[2] = { v, v >> 8 };

	g_byte_array_append (out, b, 2);
}

static void
put32 (GByteArray *out, guint32 v)
{
	guint8 b[4] = { v, v >> 8, v >> 16, v >> 24 };

	g_byte_array_append (out, b, 4);
}

static void
set32 (GByteArray *out, guint at, guint32 v)
{
	out->data[at] = v;
	out->data[at + 1] = v >> 8;
	out->data[at + 2] = v >> 16;
	out->data[at + 3] = v >> 24;
}

static void
put_ansi (GByteArray *out, const char *text)
{
	g_byte_array_append (out, (const guint8 *) text, strlen (text) + 1);
}

static void
put_wide (GByteArray *out, const char *text, gboolean terminated)
{
	glong units, i;
	gunichar2 *wide = g_utf8_to_utf16 (text, -1, NULL, &units, NULL);

	for (i = 0; i < units; i++) {
		put16 (out, wide[i]);
	}
	if (terminated) {
		put16 (out, 0);
	}
	g_free (wide);
}

static void
put_string (GByteArray *out, const char *text, gboolean ansi)
{
	if (ansi) {
		put16 (out, strlen (text));
		g_byte_array_append (out, (const guint8 *) text, strlen (text));
	} else {
		glong units;
		gunichar2 *wide = g_utf8_to_utf16 (text, -1, NULL, &units, NULL);

		put16 (out, units);
		g_free (wide);
		put_wide (out, text, FALSE);
	}
}

static GByteArray *
build (const Spec *spec)
{
	static const guint8 clsid[16] = {
		0x01, 0x14, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00,
		0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46
	};
	GByteArray *out = g_byte_array_new ();
	guint32 flags = 0;
	int i;

	if (spec->id_list)                         flags |= 0x01;
	if (spec->local_base || spec->net_share)   flags |= 0x02;
	if (spec->name)                            flags |= 0x04;
	if (spec->relative)                        flags |= 0x08;
	if (spec->working_dir)                     flags |= 0x10;
	if (spec->arguments)                       flags |= 0x20;
	if (spec->icon)                            flags |= 0x40;
	if (!spec->ansi_strings)                   flags |= 0x80;
	if (spec->env && !spec->env_unflagged)     flags |= 0x200;

	put32 (out, 0x4c);
	g_byte_array_append (out, clsid, 16);
	put32 (out, flags);
	put32 (out, spec->attributes);
	for (i = 0; i < 6; i++) {
		put32 (out, 0);            /* three FILETIMEs */
	}
	put32 (out, 0);                    /* size */
	put32 (out, 0);                    /* icon index */
	put32 (out, 1);                    /* show command */
	put16 (out, 0);                    /* hot key */
	put16 (out, 0);
	put32 (out, 0);
	put32 (out, 0);
	g_assert (out->len == 0x4c);

	if (spec->id_list) {
		put16 (out, 6);
		put16 (out, 4);            /* one item of nothing */
		put16 (out, 0);
		put16 (out, 0);            /* end of list */
	}

	if (flags & 0x02) {
		GByteArray *info = g_byte_array_new ();
		gboolean wide = spec->local_base_wide != NULL;
		guint32 header = wide ? 0x24 : 0x1c;
		guint32 info_flags = (spec->local_base ? 1 : 0) | (spec->net_share ? 2 : 0);

		for (i = 0; i < (int) header; i += 4) {
			put32 (info, 0);
		}
		set32 (info, 4, header);
		set32 (info, 8, info_flags);

		if (spec->local_base) {
			set32 (info, 12, info->len);
			put32 (info, 0x11);
			put32 (info, 3);            /* fixed drive */
			put32 (info, spec->serial);
			put32 (info, 0x10);
			g_byte_array_append (info, (const guint8 *) "", 1);

			set32 (info, 16, info->len);
			put_ansi (info, spec->local_base);
		}

		if (spec->net_share) {
			guint start = info->len;

			set32 (info, 20, start);
			put32 (info, 0);            /* size, set below */
			put32 (info, 0);
			put32 (info, 0x14);
			put32 (info, 0);
			put32 (info, 0);
			put_ansi (info, spec->net_share);
			set32 (info, start, info->len - start);
		}

		set32 (info, 24, info->len);
		put_ansi (info, spec->suffix ? spec->suffix : "");

		if (wide) {
			set32 (info, 28, info->len);
			put_wide (info, spec->local_base_wide, TRUE);
			set32 (info, 32, info->len);
			put_wide (info, spec->suffix ? spec->suffix : "", TRUE);
		}

		set32 (info, 0, info->len);
		g_byte_array_append (out, info->data, info->len);
		g_byte_array_free (info, TRUE);
	}

	if (spec->name)        put_string (out, spec->name, spec->ansi_strings);
	if (spec->relative)    put_string (out, spec->relative, spec->ansi_strings);
	if (spec->working_dir) put_string (out, spec->working_dir, spec->ansi_strings);
	if (spec->arguments)   put_string (out, spec->arguments, spec->ansi_strings);
	if (spec->icon)        put_string (out, spec->icon, spec->ansi_strings);

	/* Another block first, which the reader has to step over. */
	put32 (out, 0x10);
	put32 (out, 0xA0000005);
	put32 (out, 0);
	put32 (out, 0);
	if (spec->env) {
		guint start;

		put32 (out, 0x314);
		put32 (out, 0xA0000001);
		start = out->len;
		put_ansi (out, spec->env);
		g_byte_array_set_size (out, start + 260);
		memset (out->data + start + strlen (spec->env) + 1, 0, 260 - strlen (spec->env) - 1);
		start = out->len;
		put_wide (out, spec->env, TRUE);
		g_byte_array_set_size (out, start + 520);
		memset (out->data + start + 2 * g_utf8_strlen (spec->env, -1) + 2, 0,
			520 - 2 * g_utf8_strlen (spec->env, -1) - 2);
	}
	put32 (out, 0);                    /* end of extra data */

	return out;
}

static char *
write_lnk (const char *dir, const char *name, const Spec *spec)
{
	GByteArray *bytes = build (spec);
	char *path = g_build_filename (dir, name, NULL);

	g_assert (g_file_set_contents (path, (const char *) bytes->data, bytes->len, NULL));
	g_byte_array_free (bytes, TRUE);

	return path;
}

static void
touch (const char *dir, const char *name)
{
	char *path = g_build_filename (dir, name, NULL);
	char *parent = g_path_get_dirname (path);

	g_mkdir_with_parents (parent, 0700);
	g_assert (g_file_set_contents (path, "x", 1, NULL));
	g_free (parent);
	g_free (path);
}

static char *
uri_of (const char *dir, const char *name)
{
	char *path = g_build_filename (dir, name, NULL);
	char *uri = g_filename_to_uri (path, NULL, NULL);

	g_free (path);

	return uri;
}

static gboolean
parse (const Spec *spec, NemoLnk *lnk)
{
	GByteArray *bytes = build (spec);
	gboolean ok = nemo_lnk_parse (bytes->data, bytes->len, lnk);

	g_byte_array_free (bytes, TRUE);

	return ok;
}

static void
test_parse_local (void)
{
	Spec spec = {
		.attributes = 0x20, .id_list = TRUE,
		.local_base = "C:\\Data\\", .suffix = "Report.txt", .serial = 0x89abcdef,
		.relative = "..\\Data\\Report.txt", .working_dir = "C:\\Data",
		.arguments = "/q",
	};
	NemoLnk lnk;
	char *shown;

	check (parse (&spec, &lnk));
	check (g_strcmp0 (lnk.local_path, "C:\\Data\\Report.txt") == 0);
	check (lnk.has_serial && lnk.drive_serial == 0x89abcdef);
	check (g_strcmp0 (lnk.relative_path, "..\\Data\\Report.txt") == 0);
	check (g_strcmp0 (lnk.working_dir, "C:\\Data") == 0);
	check (g_strcmp0 (lnk.arguments, "/q") == 0);
	check (lnk.net_share == NULL);
	check (!nemo_lnk_is_dir (&lnk));

	shown = nemo_lnk_display_target (&lnk);
	check (g_strcmp0 (shown, "C:\\Data\\Report.txt") == 0);
	g_free (shown);
	nemo_lnk_clear (&lnk);

	/* No suffix, and a folder. */
	spec.local_base = "C:\\Data";
	spec.suffix = NULL;
	spec.attributes = 0x10;
	check (parse (&spec, &lnk));
	check (g_strcmp0 (lnk.local_path, "C:\\Data") == 0);
	check (nemo_lnk_is_dir (&lnk));
	nemo_lnk_clear (&lnk);
}

static void
test_parse_text (void)
{
	Spec spec = { .local_base = "C:\\?", .local_base_wide = "C:\\\xc3\x9c" "ber", .serial = 1 };
	NemoLnk lnk;

	/* The UTF-16 copy wins over the code page one. */
	check (parse (&spec, &lnk));
	check (g_strcmp0 (lnk.local_path, "C:\\\xc3\x9c" "ber") == 0);
	nemo_lnk_clear (&lnk);

	/* Code page 1252 when there is no UTF-16 copy. */
	spec.local_base = "C:\\caf\xe9";
	spec.local_base_wide = NULL;
	check (parse (&spec, &lnk));
	check (g_strcmp0 (lnk.local_path, "C:\\caf\xc3\xa9") == 0);
	nemo_lnk_clear (&lnk);

	/* Strings stored in the code page too. */
	spec.ansi_strings = TRUE;
	spec.working_dir = "C:\\Temp";
	check (parse (&spec, &lnk));
	check (g_strcmp0 (lnk.working_dir, "C:\\Temp") == 0);
	nemo_lnk_clear (&lnk);
}

static void
test_parse_network (void)
{
	Spec spec = { .net_share = "\\\\srv\\Share", .suffix = "Dir\\a.txt" };
	NemoLnk lnk;
	char *shown;

	check (parse (&spec, &lnk));
	check (g_strcmp0 (lnk.net_share, "\\\\srv\\Share") == 0);
	check (g_strcmp0 (lnk.net_path, "Dir\\a.txt") == 0);
	check (lnk.local_path == NULL);

	shown = nemo_lnk_display_target (&lnk);
	check (g_strcmp0 (shown, "\\\\srv\\Share\\Dir\\a.txt") == 0);
	g_free (shown);
	nemo_lnk_clear (&lnk);
}

/* Every cut and a few thousand flipped bytes. What is being looked for is a bad
   read, which the sanitizer build turns into a crash. */
static void
test_damaged (void)
{
	Spec spec = {
		.attributes = 0x20, .id_list = TRUE,
		.local_base = "C:\\Data", .local_base_wide = "C:\\Data", .suffix = "a.txt",
		.serial = 7, .net_share = "\\\\srv\\share", .relative = ".\\a.txt",
		.working_dir = "C:\\", .arguments = "x",
	};
	GByteArray *bytes = build (&spec);
	GRand *rand = g_rand_new_with_seed (20260924);
	NemoLnk lnk;
	guint cut;
	int round;

	for (cut = 0; cut <= bytes->len; cut++) {
		guint8 *copy = g_memdup2 (bytes->data, cut);

		if (nemo_lnk_parse (copy != NULL ? copy : (const guint8 *) "", cut, &lnk)) {
			nemo_lnk_clear (&lnk);
		}
		g_free (copy);
	}
	check (!nemo_lnk_parse (bytes->data, 0x4b, &lnk));

	for (round = 0; round < 4000; round++) {
		guint8 *copy = g_memdup2 (bytes->data, bytes->len);
		int flips = g_rand_int_range (rand, 1, 6);

		while (flips-- > 0) {
			/* Past the header, or nearly every copy is refused at once. */
			copy[g_rand_int_range (rand, 0x4c, bytes->len)] = g_rand_int_range (rand, 0, 256);
		}
		if (nemo_lnk_parse (copy, bytes->len, &lnk)) {
			nemo_lnk_clear (&lnk);
		}
		g_free (copy);
	}

	bytes->data[4] ^= 1;
	check (!nemo_lnk_parse (bytes->data, bytes->len, &lnk));

	g_rand_free (rand);
	g_byte_array_free (bytes, TRUE);
}

typedef struct {
	char *root;
	char *mountinfo;
	char *by_uuid;
	char *gvfs;
	GString *table;
} Machine;

static void
machine_init (Machine *machine)
{
	machine->root = test_scratch_dir ("nemo-lnk-XXXXXX", NULL);
	g_assert (machine->root != NULL);
	machine->mountinfo = g_build_filename (machine->root, "mountinfo", NULL);
	machine->by_uuid = g_build_filename (machine->root, "by-uuid", NULL);
	machine->gvfs = g_build_filename (machine->root, "gvfs", NULL);
	machine->table = g_string_new ("22 1 0:21 / / rw - ext4 /dev/root rw\n");
	g_mkdir (machine->by_uuid, 0700);
	g_mkdir (machine->gvfs, 0700);
}

static void
machine_apply (Machine *machine)
{
	g_assert (g_file_set_contents (machine->mountinfo, machine->table->str, -1, NULL));
	nemo_lnk_set_system_paths (machine->mountinfo, machine->by_uuid, machine->gvfs);
}

/* A drive: a stand-in device file, its volume id link, and a mount line. The
   mount table spells a space as \040. */
static char *
machine_add_drive (Machine *machine, const char *uuid, const char *fstype)
{
	char *device = g_strdup_printf ("%s/dev-%s", machine->root, uuid);
	char *link = g_build_filename (machine->by_uuid, uuid, NULL);
	char *mount = g_strdup_printf ("%s/mnt %s", machine->root, uuid);
	char *escaped_mount = g_strdup_printf ("%s/mnt\\040%s", machine->root, uuid);

	g_assert (g_file_set_contents (device, "", 0, NULL));
	g_assert (symlink (device, link) == 0);
	g_mkdir (mount, 0700);
	g_string_append_printf (machine->table, "40 22 8:1 / %s rw,relatime shared:1 - %s %s rw\n",
				escaped_mount, fstype, device);

	g_free (device);
	g_free (link);
	g_free (escaped_mount);

	return mount;
}

static void
machine_clear (Machine *machine)
{
	nemo_lnk_set_system_paths (NULL, NULL, NULL);
	g_free (machine->root);
	g_free (machine->mountinfo);
	g_free (machine->by_uuid);
	g_free (machine->gvfs);
	g_string_free (machine->table, TRUE);
}

static char *
resolve (const Spec *spec, const char *lnk_path)
{
	NemoLnk lnk;
	char *uri;

	g_assert (parse (spec, &lnk));
	uri = nemo_lnk_resolve (lnk_path, &lnk);
	nemo_lnk_clear (&lnk);

	return uri;
}

static void
test_resolve_volume (void)
{
	Machine machine;
	char *ntfs, *fat, *uri, *want, *lnk_path;
	Spec spec = { .local_base = "C:\\DATA\\report.TXT", .serial = 0x89abcdef };

	machine_init (&machine);
	ntfs = machine_add_drive (&machine, "0011223389ABCDEF", "ntfs3");
	fat = machine_add_drive (&machine, "ABCD-1234", "vfat");
	machine_apply (&machine);
	touch (ntfs, "Data/Report.txt");
	touch (fat, "Photos/one.jpg");
	touch (ntfs, "Twin/Pair.txt");
	touch (ntfs, "Twin/pair.txt");
	lnk_path = g_build_filename (machine.root, "x.lnk", NULL);

	/* Matched by serial, then by name ignoring case. */
	uri = resolve (&spec, lnk_path);
	want = uri_of (ntfs, "Data/Report.txt");
	check (g_strcmp0 (uri, want) == 0);
	g_free (uri);
	g_free (want);

	spec.local_base = "E:\\photos\\ONE.jpg";
	spec.serial = 0xabcd1234;
	uri = resolve (&spec, lnk_path);
	want = uri_of (fat, "Photos/one.jpg");
	check (g_strcmp0 (uri, want) == 0);
	g_free (uri);
	g_free (want);

	/* No drive with that serial: no answer, even though the path would fit. */
	spec.local_base = "C:\\Data\\Report.txt";
	spec.serial = 0x12345678;
	uri = resolve (&spec, lnk_path);
	check (uri == NULL);
	g_free (uri);

	/* Two names that differ only in case, and neither spelled as asked. */
	spec.serial = 0x89abcdef;
	spec.local_base = "C:\\Twin\\PAIR.txt";
	uri = resolve (&spec, lnk_path);
	check (uri == NULL);
	g_free (uri);

	spec.local_base = "C:\\Twin\\Pair.txt";
	uri = resolve (&spec, lnk_path);
	want = uri_of (ntfs, "Twin/Pair.txt");
	check (g_strcmp0 (uri, want) == 0);
	g_free (uri);
	g_free (want);

	/* A missing file is missing. */
	spec.local_base = "C:\\Data\\Gone.txt";
	uri = resolve (&spec, lnk_path);
	check (uri == NULL);
	g_free (uri);

	g_free (lnk_path);
	g_free (ntfs);
	g_free (fat);
	machine_clear (&machine);
}

static void
test_resolve_relative (void)
{
	Machine machine;
	char *links, *uri, *want, *lnk_path;
	Spec spec = {
		.local_base = "C:\\Elsewhere\\b.txt", .serial = 0x1,
		.relative = "..\\Target\\B.TXT",
	};

	machine_init (&machine);
	machine_apply (&machine);
	links = g_build_filename (machine.root, "links", NULL);
	g_mkdir (links, 0700);
	touch (machine.root, "target/b.txt");
	lnk_path = g_build_filename (links, "b.lnk", NULL);

	/* The absolute path finds nothing, so the relative one is used. */
	uri = resolve (&spec, lnk_path);
	want = uri_of (machine.root, "target/b.txt");
	check (g_strcmp0 (uri, want) == 0);
	g_free (uri);
	g_free (want);

	g_free (lnk_path);
	g_free (links);
	machine_clear (&machine);
}

static void
test_resolve_share (void)
{
	Machine machine;
	char *cifs, *sub, *gvfs_share, *uri, *want, *lnk_path;
	Spec spec = { .net_share = "\\\\srv\\Share", .suffix = "Dir\\A.txt" };
	const gchar * const *schemes;
	gboolean smb;

	machine_init (&machine);
	cifs = g_build_filename (machine.root, "cifs", NULL);
	sub = g_build_filename (machine.root, "cifs-sub", NULL);
	g_mkdir (cifs, 0700);
	g_mkdir (sub, 0700);
	g_string_append_printf (machine.table, "50 22 0:60 / %s rw - cifs //SRV/share rw\n", cifs);
	g_string_append_printf (machine.table, "51 22 0:61 / %s rw - cifs //other/music/Live rw\n", sub);
	machine_apply (&machine);
	touch (cifs, "dir/a.txt");
	touch (sub, "set.flac");
	lnk_path = g_build_filename (machine.root, "x.lnk", NULL);

	uri = resolve (&spec, lnk_path);
	want = uri_of (cifs, "dir/a.txt");
	check (g_strcmp0 (uri, want) == 0);
	g_free (uri);
	g_free (want);

	/* A mount of a folder inside the share. */
	spec.net_share = "\\\\other\\music";
	spec.suffix = "live\\set.flac";
	uri = resolve (&spec, lnk_path);
	want = uri_of (sub, "set.flac");
	check (g_strcmp0 (uri, want) == 0);
	g_free (uri);
	g_free (want);

	/* gvfs has its own folder of mounts. */
	gvfs_share = g_build_filename (machine.gvfs, "smb-share:server=nas,share=media", NULL);
	g_mkdir (gvfs_share, 0700);
	touch (gvfs_share, "Movies/m.mkv");
	spec.net_share = "\\\\NAS\\Media";
	spec.suffix = "movies\\m.mkv";
	uri = resolve (&spec, lnk_path);
	want = uri_of (gvfs_share, "Movies/m.mkv");
	check (g_strcmp0 (uri, want) == 0);
	g_free (uri);
	g_free (want);

	/* Mounted nowhere: an smb:// address when gvfs can open one, and
	   nothing when it cannot. Not checked for, since that would mean going
	   to the network. */
	schemes = g_vfs_get_supported_uri_schemes (g_vfs_get_default ());
	smb = schemes != NULL && g_strv_contains (schemes, "smb");
	spec.net_share = "\\\\far\\docs";
	spec.suffix = "a b.txt";
	uri = resolve (&spec, lnk_path);
	if (smb) {
		check (g_strcmp0 (uri, "smb://far/docs/a%20b.txt") == 0);
	} else {
		check (uri == NULL);
	}
	g_free (uri);

	g_free (lnk_path);
	g_free (gvfs_share);
	g_free (cifs);
	g_free (sub);
	machine_clear (&machine);
}

static void
test_resolve_dir (void)
{
	Machine machine;
	char *ntfs, *dir, *want;
	Spec spec = { .local_base = "C:\\Data\\a.txt", .serial = 0x89abcdef };
	NemoLnk lnk;

	machine_init (&machine);
	ntfs = machine_add_drive (&machine, "0011223389ABCDEF", "ntfs");
	machine_apply (&machine);
	touch (ntfs, "Data/a.txt");
	touch (ntfs, "Work/keep");

	g_assert (parse (&spec, &lnk));

	dir = nemo_lnk_resolve_dir (&lnk, "c:\\work");
	want = g_build_filename (ntfs, "Work", NULL);
	check (g_strcmp0 (dir, want) == 0);
	g_free (dir);
	g_free (want);

	/* Another drive, a variable, a file and a missing folder. */
	dir = nemo_lnk_resolve_dir (&lnk, "D:\\Work");
	check (dir == NULL);
	g_free (dir);
	dir = nemo_lnk_resolve_dir (&lnk, "%USERPROFILE%");
	check (dir == NULL);
	g_free (dir);
	dir = nemo_lnk_resolve_dir (&lnk, "C:\\Data\\a.txt");
	check (dir == NULL);
	g_free (dir);
	dir = nemo_lnk_resolve_dir (&lnk, "C:\\Nope");
	check (dir == NULL);
	g_free (dir);
	dir = nemo_lnk_resolve_dir (&lnk, NULL);
	check (dir == NULL);

	nemo_lnk_clear (&lnk);
	g_free (ntfs);
	machine_clear (&machine);
}

static void
test_follow (void)
{
	Machine machine;
	Spec to_b = { .relative = ".\\b.lnk" };
	Spec to_file = { .relative = ".\\target.txt", .working_dir = "C:\\" };
	Spec to_a = { .relative = ".\\a.lnk" };
	Spec to_fake = { .relative = ".\\fake.lnk" };
	char *a, *b, *uri, *want;
	NemoLnk last;

	machine_init (&machine);
	machine_apply (&machine);
	touch (machine.root, "target.txt");

	a = write_lnk (machine.root, "a.lnk", &to_b);
	b = write_lnk (machine.root, "b.lnk", &to_file);
	uri = nemo_lnk_follow (a, &last);
	want = uri_of (machine.root, "target.txt");
	check (g_strcmp0 (uri, want) == 0);
	check (g_strcmp0 (last.working_dir, "C:\\") == 0);
	nemo_lnk_clear (&last);
	g_free (uri);
	g_free (want);
	g_free (b);

	/* a -> b -> a */
	b = write_lnk (machine.root, "b.lnk", &to_a);
	uri = nemo_lnk_follow (a, &last);
	check (uri == NULL);
	nemo_lnk_clear (&last);
	g_free (b);

	/* A file named like a shortcut that is not one ends the chain. */
	b = write_lnk (machine.root, "b.lnk", &to_fake);
	touch (machine.root, "fake.lnk");
	uri = nemo_lnk_follow (a, &last);
	want = uri_of (machine.root, "fake.lnk");
	check (g_strcmp0 (uri, want) == 0);
	nemo_lnk_clear (&last);
	g_free (uri);
	g_free (want);

	/* ...but the first one has to be a shortcut. */
	g_free (a);
	a = g_build_filename (machine.root, "fake.lnk", NULL);
	uri = nemo_lnk_follow (a, &last);
	check (uri == NULL);
	nemo_lnk_clear (&last);

	g_free (a);
	g_free (b);
	machine_clear (&machine);
}

/* Write a shortcut to dir/name from dir/lnk_name with the parts given, read
   it back, and check it leads to the same place. The caller clears *lnk. */
static void
write_parts (const char *dir, const char *name, const char *lnk_name, guint parts, NemoLnk *lnk)
{
	char *target = g_build_filename (dir, name, NULL);
	char *lnk_path = g_build_filename (dir, lnk_name, NULL);
	char *uri, *want;
	GError *error = NULL;

	check (nemo_lnk_write (lnk_path, target, parts, &error));
	g_clear_error (&error);
	check (nemo_lnk_read (lnk_path, lnk));
	uri = nemo_lnk_resolve (lnk_path, lnk);
	want = uri_of (dir, name);
	check (g_strcmp0 (uri, want) == 0);
	check (nemo_lnk_is_dir (lnk) == g_file_test (target, G_FILE_TEST_IS_DIR));

	g_free (uri);
	g_free (want);
	g_free (lnk_path);
	g_free (target);
}

static void
write_and_read (const char *dir, const char *name, const char *lnk_name, NemoLnk *lnk)
{
	write_parts (dir, name, lnk_name, NEMO_LNK_ABSOLUTE | NEMO_LNK_RELATIVE, lnk);
}

static char *
backslashed (const char *path)
{
	char *out = g_strdup (path);

	g_strdelimit (out, "/", '\\');
	return out;
}

/* U with diaeresis, then a CJK character. Split so no escape runs on. */
#define UNICODE_NAME "\xc3\x9c" "ber \xe6\x96\x87" ".txt"

static void
test_write (void)
{
	Machine machine;
	NemoLnk lnk;
	char *root, *cifs, *sub, *gvfs_share, *lnk_path, *target, *before, *after, *uri, *want, *windows;
	gsize before_length, after_length;
	GError *error = NULL;

	machine_init (&machine);
	/* The writer takes the target as it really is, so the mount table has to
	   name the real folders too. */
	root = realpath (machine.root, NULL);
	cifs = g_build_filename (root, "cifs", NULL);
	sub = g_build_filename (root, "cifs-sub", NULL);
	g_mkdir (cifs, 0700);
	g_mkdir (sub, 0700);
	g_string_append_printf (machine.table, "50 22 0:60 / %s rw - cifs //SRV/share rw\n", cifs);
	g_string_append_printf (machine.table, "51 22 0:61 / %s rw - cifs //other/music/Live rw\n", sub);
	machine_apply (&machine);

	/* Nothing on a share: the relative path alone, in Windows spelling. */
	touch (root, "docs/plain.txt");
	write_and_read (root, "docs/plain.txt", "plain.txt.lnk", &lnk);
	check (g_strcmp0 (lnk.relative_path, ".\\docs\\plain.txt") == 0);
	/* The absolute path in Windows spelling too, from the root with no drive. */
	target = g_build_filename (root, "docs", "plain.txt", NULL);
	windows = backslashed (target);
	check (lnk.net_share == NULL && g_strcmp0 (lnk.local_path, windows) == 0);
	check (lnk.env_path == NULL);
	nemo_lnk_clear (&lnk);

	/* Absolute only: no relative path, and it still leads there. */
	lnk_path = g_build_filename (root, "abs.lnk", NULL);
	check (nemo_lnk_write (lnk_path, target, NEMO_LNK_ABSOLUTE, NULL));
	check (nemo_lnk_read (lnk_path, &lnk));
	check (lnk.relative_path == NULL && g_strcmp0 (lnk.local_path, windows) == 0);
	uri = nemo_lnk_resolve (lnk_path, &lnk);
	want = uri_of (root, "docs/plain.txt");
	check (g_strcmp0 (uri, want) == 0);
	g_free (uri);
	g_free (want);
	nemo_lnk_clear (&lnk);
	g_free (lnk_path);

	/* Relative only: no absolute path at all. */
	write_parts (root, "docs/plain.txt", "rel.lnk", NEMO_LNK_RELATIVE, &lnk);
	check (lnk.local_path == NULL && lnk.net_share == NULL && lnk.env_path == NULL);
	check (g_strcmp0 (lnk.relative_path, ".\\docs\\plain.txt") == 0);
	nemo_lnk_clear (&lnk);

	/* Outside home and off any share, no variable covers it. Portable alone
	   has nothing to hold, and says so rather than write an empty link. */
	lnk_path = g_build_filename (root, "port.lnk", NULL);
	check (!nemo_lnk_write (lnk_path, target, NEMO_LNK_PORTABLE, &error));
	check (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT));
	g_clear_error (&error);
	check (!g_file_test (lnk_path, G_FILE_TEST_EXISTS));
	check (!nemo_lnk_write (lnk_path, target, 0, NULL));
	check (!g_file_test (lnk_path, G_FILE_TEST_EXISTS));
	g_free (lnk_path);

	/* Taking the relative path back out leaves the rest as it was. */
	lnk_path = g_build_filename (root, "drop.lnk", NULL);
	check (nemo_lnk_write (lnk_path, target, NEMO_LNK_ALL_PARTS, NULL));
	check (nemo_lnk_drop_relative (lnk_path, NULL));
	check (nemo_lnk_read (lnk_path, &lnk));
	check (lnk.relative_path == NULL && g_strcmp0 (lnk.local_path, windows) == 0);
	uri = nemo_lnk_resolve (lnk_path, &lnk);
	want = uri_of (root, "docs/plain.txt");
	check (g_strcmp0 (uri, want) == 0);
	g_free (uri);
	g_free (want);
	nemo_lnk_clear (&lnk);
	/* And once it is gone, again is a no-op. */
	check (nemo_lnk_drop_relative (lnk_path, NULL));
	g_free (lnk_path);
	g_free (windows);
	g_free (target);

	target = g_build_filename (root, "docs", "sub", NULL);
	g_mkdir (target, 0700);
	g_free (target);
	write_and_read (root, "docs/sub", "docs/sub.lnk", &lnk);
	check (g_strcmp0 (lnk.relative_path, ".\\sub") == 0);
	nemo_lnk_clear (&lnk);

	touch (root, "docs/deep/one.txt");
	write_and_read (root, "docs/plain.txt", "docs/deep/up.lnk", &lnk);
	check (g_strcmp0 (lnk.relative_path, "..\\plain.txt") == 0);
	nemo_lnk_clear (&lnk);

	/* Names past ASCII come back exactly. */
	touch (root, "docs/" UNICODE_NAME);
	write_and_read (root, "docs/" UNICODE_NAME, "u.lnk", &lnk);
	check (g_strcmp0 (lnk.relative_path, ".\\docs\\" UNICODE_NAME) == 0);
	nemo_lnk_clear (&lnk);

	/* On a share, the \\server\share path goes in as well. */
	touch (cifs, "dir/a.txt");
	write_and_read (root, "cifs/dir/a.txt", "a.lnk", &lnk);
	check (g_strcmp0 (lnk.net_share, "\\\\SRV\\share") == 0);
	check (g_strcmp0 (lnk.net_path, "dir\\a.txt") == 0);
	/* The share path is the absolute one; the mount point here is not. */
	check (lnk.local_path == NULL);
	nemo_lnk_clear (&lnk);

	/* Portable on a share is the share path too, where Windows reads it. */
	write_parts (root, "cifs/dir/a.txt", "pa.lnk", NEMO_LNK_PORTABLE, &lnk);
	check (g_strcmp0 (lnk.env_path, "\\\\SRV\\share\\dir\\a.txt") == 0);
	check (lnk.net_share == NULL && lnk.local_path == NULL && lnk.relative_path == NULL);
	nemo_lnk_clear (&lnk);

	/* A share mounted at a folder inside it. */
	touch (sub, "set.flac");
	write_and_read (root, "cifs-sub/set.flac", "set.lnk", &lnk);
	check (g_strcmp0 (lnk.net_share, "\\\\other\\music") == 0);
	check (g_strcmp0 (lnk.net_path, "Live\\set.flac") == 0);
	nemo_lnk_clear (&lnk);

	/* gvfs has its own folder of mounts. */
	gvfs_share = g_build_filename (machine.gvfs, "smb-share:server=nas,share=media", NULL);
	g_mkdir (gvfs_share, 0700);
	touch (gvfs_share, "Movies/m.mkv");
	lnk_path = g_build_filename (root, "m.lnk", NULL);
	target = g_build_filename (gvfs_share, "Movies", "m.mkv", NULL);
	check (nemo_lnk_write (lnk_path, target, NEMO_LNK_ABSOLUTE | NEMO_LNK_RELATIVE, NULL));
	check (nemo_lnk_read (lnk_path, &lnk));
	check (g_strcmp0 (lnk.net_share, "\\\\nas\\media") == 0);
	check (g_strcmp0 (lnk.net_path, "Movies\\m.mkv") == 0);
	nemo_lnk_clear (&lnk);

	/* Never over something already there. */
	g_assert (g_file_get_contents (lnk_path, &before, &before_length, NULL));
	check (!nemo_lnk_write (lnk_path, target, NEMO_LNK_ABSOLUTE | NEMO_LNK_RELATIVE, &error));
	check (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_EXISTS));
	g_clear_error (&error);
	g_assert (g_file_get_contents (lnk_path, &after, &after_length, NULL));
	check (before_length == after_length && memcmp (before, after, after_length) == 0);
	g_free (before);
	g_free (after);
	g_free (lnk_path);
	g_free (target);

	/* Nothing to point at. */
	lnk_path = g_build_filename (root, "gone.lnk", NULL);
	target = g_build_filename (root, "gone.txt", NULL);
	check (!nemo_lnk_write (lnk_path, target, NEMO_LNK_ABSOLUTE | NEMO_LNK_RELATIVE, NULL));
	check (!g_file_test (lnk_path, G_FILE_TEST_EXISTS));
	g_free (lnk_path);
	g_free (target);

	g_free (gvfs_share);
	g_free (cifs);
	g_free (sub);
	free (root);
	machine_clear (&machine);
}

/* Home is moved into the scratch folder before anything reads it. */
static char *home;

static void
test_portable (void)
{
	Machine machine;
	NemoLnk lnk;
	char *outside, *lnk_path, *target, *uri, *want;

	machine_init (&machine);
	machine_apply (&machine);
	outside = realpath (machine.root, NULL);

	/* Under home: the variable Windows has for it, and nothing else. */
	touch (home, "Documents/p.txt");
	target = g_build_filename (home, "Documents", "p.txt", NULL);
	lnk_path = g_build_filename (outside, "p.lnk", NULL);
	check (nemo_lnk_write (lnk_path, target, NEMO_LNK_PORTABLE, NULL));
	check (nemo_lnk_read (lnk_path, &lnk));
	check (g_strcmp0 (lnk.env_path, "%USERPROFILE%\\Documents\\p.txt") == 0);
	check (lnk.local_path == NULL && lnk.relative_path == NULL);
	uri = nemo_lnk_resolve (lnk_path, &lnk);
	want = uri_of (home, "Documents/p.txt");
	check (g_strcmp0 (uri, want) == 0);
	g_free (uri);
	nemo_lnk_clear (&lnk);

	/* All three: the variable is tried first, so a link copied to where the
	   absolute path is wrong still finds it. */
	g_free (lnk_path);
	lnk_path = g_build_filename (outside, "all.lnk", NULL);
	check (nemo_lnk_write (lnk_path, target, NEMO_LNK_ALL_PARTS, NULL));
	check (nemo_lnk_read (lnk_path, &lnk));
	check (lnk.env_path != NULL && lnk.local_path != NULL && lnk.relative_path != NULL);
	uri = nemo_lnk_resolve (lnk_path, &lnk);
	check (g_strcmp0 (uri, want) == 0);
	g_free (uri);
	nemo_lnk_clear (&lnk);
	g_free (want);
	g_free (target);

	/* Home itself, and a name past ASCII under it. */
	g_free (lnk_path);
	lnk_path = g_build_filename (outside, "home.lnk", NULL);
	check (nemo_lnk_write (lnk_path, home, NEMO_LNK_PORTABLE, NULL));
	check (nemo_lnk_read (lnk_path, &lnk));
	check (g_strcmp0 (lnk.env_path, "%USERPROFILE%") == 0);
	check (nemo_lnk_is_dir (&lnk));
	nemo_lnk_clear (&lnk);

	touch (home, UNICODE_NAME);
	target = g_build_filename (home, UNICODE_NAME, NULL);
	g_free (lnk_path);
	lnk_path = g_build_filename (outside, "u.lnk", NULL);
	check (nemo_lnk_write (lnk_path, target, NEMO_LNK_PORTABLE, NULL));
	check (nemo_lnk_read (lnk_path, &lnk));
	check (g_strcmp0 (lnk.env_path, "%USERPROFILE%\\" UNICODE_NAME) == 0);
	uri = nemo_lnk_resolve (lnk_path, &lnk);
	want = uri_of (home, UNICODE_NAME);
	check (g_strcmp0 (uri, want) == 0);
	g_free (uri);
	g_free (want);
	nemo_lnk_clear (&lnk);
	g_free (target);

	/* Too long for the block: left out, and with nothing else, refused. */
	target = g_build_filename (home, "long", NULL);
	g_mkdir (target, 0700);
	{
		GString *deep = g_string_new (target);
		int i;

		for (i = 0; i < 30; i++) {
			g_string_append (deep, "/abcdefghij");
			g_mkdir (deep->str, 0700);
		}
		g_free (lnk_path);
		lnk_path = g_build_filename (outside, "long.lnk", NULL);
		check (!nemo_lnk_write (lnk_path, deep->str, NEMO_LNK_PORTABLE, NULL));
		check (nemo_lnk_write (lnk_path, deep->str, NEMO_LNK_PORTABLE | NEMO_LNK_ABSOLUTE, NULL));
		check (nemo_lnk_read (lnk_path, &lnk));
		check (lnk.env_path == NULL && lnk.local_path != NULL);
		nemo_lnk_clear (&lnk);
		g_string_free (deep, TRUE);
	}
	g_free (target);

	g_free (lnk_path);
	free (outside);
	machine_clear (&machine);
}

static void
test_expand (void)
{
	char *text, *want;

	want = g_build_filename (home, "a", "b.txt", NULL);
	text = nemo_lnk_expand ("%USERPROFILE%\\a\\b.txt");
	check (g_strcmp0 (text, want) == 0);
	g_free (text);
	/* Windows names are not case sensitive. */
	text = nemo_lnk_expand ("%UserProfile%\\a\\b.txt");
	check (g_strcmp0 (text, want) == 0);
	g_free (text);
	g_free (want);

	/* A variable this machine has, in the Windows spelling only. */
	g_setenv ("NEMO_LNK_TEST", "/srv/v", TRUE);
	text = nemo_lnk_expand ("%NEMO_LNK_TEST%\\q");
	check (g_strcmp0 (text, "/srv/v/q") == 0);
	g_free (text);
	text = nemo_lnk_expand ("$NEMO_LNK_TEST\\q");
	check (g_strcmp0 (text, "$NEMO_LNK_TEST/q") == 0);
	g_free (text);

	/* One it does not have: nothing, rather than a guess. */
	check (nemo_lnk_expand ("%NEMO_LNK_NOT_SET%\\q") == NULL);

	/* A lone percent sign is text. */
	text = nemo_lnk_expand ("\\\\srv\\share\\100%");
	check (g_strcmp0 (text, "//srv/share/100%") == 0);
	g_free (text);

	text = nemo_lnk_portable_path (home);
	check (g_strcmp0 (text, "%USERPROFILE%") == 0);
	g_free (text);
	check (nemo_lnk_portable_path ("/") == NULL);
	want = g_strconcat (home, "x", NULL);
	check (nemo_lnk_portable_path (want) == NULL);
	g_free (want);
}

static void
test_parse_env (void)
{
	Spec spec = {
		.attributes = 0x20, .name = "Notes", .relative = ".\\n.txt",
		.working_dir = "C:\\Temp", .icon = "C:\\x.ico",
		.env = "%USERPROFILE%\\Documents\\n.txt",
	};
	Spec unflagged = { .attributes = 0x20, .relative = ".\\n.txt",
			   .env = "%USERPROFILE%\\n.txt", .env_unflagged = TRUE };
	Machine machine;
	NemoLnk lnk;
	char *lnk_path, *uri, *want, *shown, *copy;

	check (parse (&spec, &lnk));
	check (g_strcmp0 (lnk.env_path, "%USERPROFILE%\\Documents\\n.txt") == 0);
	check (g_strcmp0 (lnk.description, "Notes") == 0);
	check (g_strcmp0 (lnk.working_dir, "C:\\Temp") == 0);
	shown = nemo_lnk_display_target (&lnk);
	check (g_strcmp0 (shown, "%USERPROFILE%\\Documents\\n.txt") == 0);
	g_free (shown);
	nemo_lnk_clear (&lnk);

	/* Windows ignores the block unless the flag says to read it. */
	check (parse (&unflagged, &lnk));
	check (lnk.env_path == NULL);
	nemo_lnk_clear (&lnk);

	machine_init (&machine);
	machine_apply (&machine);

	/* The variable is tried first, and the relative path when it leads
	   nowhere, as Windows does. */
	touch (home, "Documents/n.txt");
	lnk_path = write_lnk (machine.root, "n.lnk", &spec);
	check (nemo_lnk_read (lnk_path, &lnk));
	uri = nemo_lnk_resolve (lnk_path, &lnk);
	want = uri_of (home, "Documents/n.txt");
	check (g_strcmp0 (uri, want) == 0);
	g_free (uri);
	g_free (want);
	nemo_lnk_clear (&lnk);

	touch (machine.root, "n.txt");
	copy = g_strdup (spec.env);
	spec.env = "%USERPROFILE%\\gone\\n.txt";
	g_free (lnk_path);
	lnk_path = write_lnk (machine.root, "n.lnk", &spec);
	check (nemo_lnk_read (lnk_path, &lnk));
	uri = nemo_lnk_resolve (lnk_path, &lnk);
	want = uri_of (machine.root, "n.txt");
	check (g_strcmp0 (uri, want) == 0);
	g_free (uri);
	g_free (want);
	nemo_lnk_clear (&lnk);

	/* Dropping the relative path keeps the name before it and the rest after. */
	check (nemo_lnk_drop_relative (lnk_path, NULL));
	check (nemo_lnk_read (lnk_path, &lnk));
	check (lnk.relative_path == NULL);
	check (g_strcmp0 (lnk.description, "Notes") == 0);
	check (g_strcmp0 (lnk.working_dir, "C:\\Temp") == 0);
	check (g_strcmp0 (lnk.env_path, "%USERPROFILE%\\gone\\n.txt") == 0);
	nemo_lnk_clear (&lnk);

	g_free (copy);
	g_free (lnk_path);
	machine_clear (&machine);
}

static gboolean
icon_has_name (GIcon *icon, const char *name)
{
	return icon != NULL && G_IS_THEMED_ICON (icon) &&
	       g_strv_contains ((const gchar * const *) g_themed_icon_get_names (G_THEMED_ICON (icon)), name);
}

static void
test_icon (void)
{
	Machine machine;
	Spec folder = { .attributes = 0x10, .local_base = "C:\\Music", .serial = 1 };
	Spec pdf = { .attributes = 0x20, .local_base = "C:\\Docs\\Plan.pdf", .serial = 1 };
	char *path, *plain;
	GIcon *icon, *want;
	char *type;

	machine_init (&machine);
	machine_apply (&machine);

	path = write_lnk (machine.root, "m.lnk", &folder);
	icon = nemo_lnk_icon_for_path (path, 100);
	check (icon_has_name (icon, "folder"));
	g_clear_object (&icon);

	/* Rewritten with the same mtime: the cached answer stands. */
	g_free (write_lnk (machine.root, "m.lnk", &pdf));
	icon = nemo_lnk_icon_for_path (path, 100);
	check (icon_has_name (icon, "folder"));
	g_clear_object (&icon);

	/* A new mtime reads the file again, and the type comes from the name. */
	type = g_content_type_guess ("Plan.pdf", NULL, 0, NULL);
	want = g_content_type_get_icon (type);
	icon = nemo_lnk_icon_for_path (path, 200);
	check (icon != NULL && g_icon_equal (icon, want));
	g_clear_object (&icon);
	g_object_unref (want);
	g_free (type);

	plain = g_build_filename (machine.root, "plain.lnk", NULL);
	g_assert (g_file_set_contents (plain, "not a shortcut", -1, NULL));
	icon = nemo_lnk_icon_for_path (plain, 1);
	check (icon == NULL);

	g_free (plain);
	g_free (path);
	machine_clear (&machine);
}

static gboolean
spec_on_share (const Spec *spec)
{
	NemoLnk lnk;
	gboolean share;

	g_assert (parse (spec, &lnk));
	share = nemo_lnk_points_at_share (&lnk);
	nemo_lnk_clear (&lnk);

	return share;
}

/* Windows takes a shortcut's icon from the shell only when nothing it records
   is on a share, so this is what keeps the window off the network. */
static void
test_share (void)
{
	Spec icon_wide = { .local_base = "C:\\Tools\\app.exe", .icon = "%SystemRoot%\\system32\\shell32.dll" };
	Spec icon_ansi = { .local_base = "C:\\Tools\\app.exe", .icon = "C:\\Tools\\app.ico", .ansi_strings = TRUE };
	Spec local = { .local_base = "C:\\Tools\\app.exe", .serial = 1 };
	Spec relative = { .relative = "..\\..\\app.exe" };
	Spec long_local = { .local_base = "\\\\?\\C:\\Tools\\app.exe" };
	Spec device = { .local_base = "\\\\.\\pipe\\x" };
	Spec network = { .net_share = "\\\\srv\\Share", .suffix = "app.exe" };
	Spec unc_base = { .local_base = "\\\\srv\\Share\\app.exe" };
	Spec long_unc = { .local_base = "\\\\?\\UNC\\srv\\Share\\app.exe" };
	Spec env_unc = { .env = "\\\\srv\\Share\\app.exe" };
	Spec env_var = { .env = "%NEMO_TEST_SHARE%\\app.exe" };
	Spec env_unset = { .env = "%NEMO_TEST_UNSET%\\app.exe" };
	Spec icon_share = { .local_base = "C:\\Tools\\app.exe", .icon = "\\\\srv\\Share\\app.ico" };
	Spec icon_var = { .local_base = "C:\\Tools\\app.exe", .icon = "%NEMO_TEST_SHARE%\\app.ico" };
	NemoLnk lnk;

	g_setenv ("NEMO_TEST_SHARE", "\\\\srv\\Share", TRUE);
	g_unsetenv ("NEMO_TEST_UNSET");

	check (parse (&icon_wide, &lnk));
	check (g_strcmp0 (lnk.icon_location, "%SystemRoot%\\system32\\shell32.dll") == 0);
	check (g_strcmp0 (lnk.local_path, "C:\\Tools\\app.exe") == 0);
	nemo_lnk_clear (&lnk);
	check (parse (&icon_ansi, &lnk));
	check (g_strcmp0 (lnk.icon_location, "C:\\Tools\\app.ico") == 0);
	nemo_lnk_clear (&lnk);
	check (parse (&local, &lnk));
	check (lnk.icon_location == NULL);
	nemo_lnk_clear (&lnk);

	check (!spec_on_share (&local));
	check (!spec_on_share (&relative));
	check (!spec_on_share (&long_local));
	check (!spec_on_share (&device));
	check (!spec_on_share (&icon_wide));
	check (!spec_on_share (&icon_ansi));
	check (!spec_on_share (&env_unset));

	check (spec_on_share (&network));
	check (spec_on_share (&unc_base));
	check (spec_on_share (&long_unc));
	check (spec_on_share (&env_unc));
	check (spec_on_share (&env_var));
	check (spec_on_share (&icon_share));
	check (spec_on_share (&icon_var));

	g_unsetenv ("NEMO_TEST_SHARE");
}

static void
fifo_timed_out (int sig)
{
	static const char message[] = "FAIL: reading a FIFO named .lnk blocked\n";
	ssize_t written;

	(void) sig;
	written = write (STDERR_FILENO, message, sizeof message - 1);
	(void) written;
	_exit (1);
}

/* A FIFO with nobody writing blocks the open and the read for good. Listing
   the folder it sits in asks for its icon and sort place, so only a regular
   file may be read. */
static void
test_fifo (void)
{
	Machine machine;
	NemoLnk lnk;
	NemoLnk followed;
	char *path, *uri;
	GIcon *icon;

	machine_init (&machine);
	machine_apply (&machine);

	path = g_build_filename (machine.root, "trap.lnk", NULL);
	check (mkfifo (path, 0600) == 0);

	signal (SIGALRM, fifo_timed_out);
	alarm (10);

	check (!nemo_lnk_read (path, &lnk));
	icon = nemo_lnk_icon_for_path (path, 1);
	check (icon == NULL);
	g_clear_object (&icon);
	check (!nemo_lnk_target_is_dir_for_path (path, 2));
	uri = nemo_lnk_follow (path, &followed);
	check (uri == NULL);
	g_free (uri);
	nemo_lnk_clear (&followed);

	alarm (0);
	signal (SIGALRM, SIG_DFL);

	g_free (path);
	machine_clear (&machine);
}

/* Whether a block with this signature is in the extra data. */
static gboolean
has_block (const guint8 *bytes, gsize length, guint32 signature)
{
	gsize at;

	for (at = 0x4c; at + 8 <= length; at++) {
		guint32 size = bytes[at] | (bytes[at + 1] << 8) | (bytes[at + 2] << 16) | ((guint32) bytes[at + 3] << 24);
		guint32 sig = bytes[at + 4] | (bytes[at + 5] << 8) | (bytes[at + 6] << 16) | ((guint32) bytes[at + 7] << 24);

		if (sig == signature && size >= 8 && size <= length - at) {
			return TRUE;
		}
	}

	return FALSE;
}

static void
test_set_paths (void)
{
	int ansi;

	for (ansi = 0; ansi <= 1; ansi++) {
		Spec spec = {
			.attributes = 0x20, .id_list = TRUE,
			.local_base = "C:\\old\\", .suffix = "file.txt", .serial = 0x1234,
			.name = "Note", .relative = "..\\old\\file.txt", .working_dir = "C:\\work",
			.arguments = "--flag", .icon = "C:\\icon.ico", .env = "%USERPROFILE%\\old.txt",
			.ansi_strings = ansi,
		};
		GByteArray *bytes = build (&spec);
		char *path = g_build_filename (home, "edit.lnk", NULL);
		char *contents = NULL, *before = NULL;
		gsize length = 0, before_length = 0;
		GError *error = NULL;
		NemoLnk lnk;
		char *long_path;

		/* A block that has nothing to do with the target, to be kept. */
		g_byte_array_set_size (bytes, bytes->len - 4);
		put32 (bytes, 0x10);
		put32 (bytes, 0xA0000008);
		put32 (bytes, 0x11111111);
		put32 (bytes, 0x22222222);
		put32 (bytes, 0);
		check (g_file_set_contents (path, (const char *) bytes->data, bytes->len, NULL));

		check (nemo_lnk_set_paths (path, "C:\\new\\file2.txt", "..\\new\\file2.txt",
					   "%USERPROFILE%\\new.txt", &error));
		g_clear_error (&error);
		check (nemo_lnk_read (path, &lnk));
		check (g_strcmp0 (lnk.local_path, "C:\\new\\file2.txt") == 0);
		check (lnk.has_serial && lnk.drive_serial == 0x1234);
		check (g_strcmp0 (lnk.relative_path, "..\\new\\file2.txt") == 0);
		check (g_strcmp0 (lnk.env_path, "%USERPROFILE%\\new.txt") == 0);
		check (g_strcmp0 (lnk.description, "Note") == 0);
		check (g_strcmp0 (lnk.working_dir, "C:\\work") == 0);
		check (g_strcmp0 (lnk.arguments, "--flag") == 0);
		nemo_lnk_clear (&lnk);

		check (g_file_get_contents (path, &contents, &length, NULL));
		check (length > 0x4c && (contents[20] & 0x01) == 0);
		check (!has_block ((const guint8 *) contents, length, 0xA0000005));
		check (has_block ((const guint8 *) contents, length, 0xA0000008));
		g_free (contents);

		/* Another drive loses the serial, which belongs to the old one. A
		   share path goes in as a share. */
		check (nemo_lnk_set_paths (path, "D:\\x.txt", NULL, NULL, NULL));
		check (nemo_lnk_read (path, &lnk));
		check (g_strcmp0 (lnk.local_path, "D:\\x.txt") == 0 && lnk.drive_serial == 0);
		check (lnk.relative_path == NULL && lnk.env_path == NULL);
		check (g_strcmp0 (lnk.arguments, "--flag") == 0);
		nemo_lnk_clear (&lnk);

		check (nemo_lnk_set_paths (path, "\\\\srv\\share\\dir\\f.txt", "", "", NULL));
		check (nemo_lnk_read (path, &lnk));
		check (lnk.local_path == NULL);
		check (g_strcmp0 (lnk.net_share, "\\\\srv\\share") == 0);
		check (g_strcmp0 (lnk.net_path, "dir\\f.txt") == 0);
		nemo_lnk_clear (&lnk);

		/* Refused, and the file is left alone. */
		check (g_file_get_contents (path, &before, &before_length, NULL));
		check (!nemo_lnk_set_paths (path, "", NULL, "", &error));
		check (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT));
		g_clear_error (&error);
		long_path = g_strnfill (300, 'a');
		check (!nemo_lnk_set_paths (path, NULL, NULL, long_path, &error));
		check (error != NULL);
		g_clear_error (&error);
		g_free (long_path);
		check (g_file_get_contents (path, &contents, &length, NULL));
		check (length == before_length && memcmp (contents, before, length) == 0);
		g_free (contents);
		g_free (before);

		g_remove (path);
		g_free (path);
		g_byte_array_free (bytes, TRUE);
	}
}

int
main (int argc, char **argv)
{
	char *scratch = test_scratch_dir ("nemo-lnk-home-XXXXXX", NULL);
	char *real;

	/* Real, since the writer reads the target as it really is. */
	real = realpath (scratch, NULL);
	home = g_build_filename (real, "home", NULL);
	free (real);
	g_mkdir (home, 0700);
	g_setenv ("HOME", home, TRUE);
	g_unsetenv ("USERPROFILE");
	if (g_strcmp0 (g_get_home_dir (), home) != 0) {
		g_printerr ("SKIP: the home folder was read before HOME could be moved\n");
		return 77;
	}

	test_parse_local ();
	test_parse_text ();
	test_parse_network ();
	test_damaged ();
	test_resolve_volume ();
	test_resolve_relative ();
	test_resolve_share ();
	test_resolve_dir ();
	test_follow ();
	test_write ();
	test_portable ();
	test_expand ();
	test_parse_env ();
	test_icon ();
	test_share ();
	test_fifo ();
	test_set_paths ();

	if (failures == 0)
		g_print ("nemo-lnk: all checks passed\n");

	return failures == 0 ? 0 : 1;
}
