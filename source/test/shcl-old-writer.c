/* Writes a settings file the way the app did under an older SHCL release, and
 * what that release read back from it. Built once per release in
 * vendor/shcl-old, against that release's own header, so the file and the
 * readings come from the old code and nothing in today's SHCL.
 *
 *   shcl-old-writer <settings.shcl> <expect.txt> plain|hand|nobackslash
 *
 * 1.0.0-beta1 had no setters, so its file is typed out the way a person would
 * have written one (SHCL_OLD_NO_WRITER).
 *
 * plain is only what the app's own setters wrote. hand adds lines spelled the
 * way a person might have written them under the old rules. nobackslash is
 * plain without the values the old setters escaped and the lists of more than
 * one item, so both rule sets read it the same.
 *
 * expect.txt has one line per key: path, type, whether today's app is known to
 * read it another way, and the value. Strings are hex, lists are a count and
 * hex elements, numbers are decimal. Plain C on purpose: no GLib, nothing of
 * the app. */

#define SHCL_NO_FILE_IO
#define SHCL_IMPLEMENTATION
#include "shcl.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { SET, HAND };

typedef struct {
	const char *path;
	char        type;          /* s string or enum nick, l list, b, i, f */
	int         how;
	int         misread;       /* known fault, filed in the backlog */
	const char *text;          /* s, and a HAND line's value text */
	const char *list[6];
	int         n;
	int64_t     num;
	double      real;
	const char *comment;
} Entry;

static const Entry entries[] = {
	{ "appearance.gtk-theme", 's', SET, 0, "Adwaita-dark", { 0 }, 0, 0, 0,
	  "Theme, with a # and a \\ in the note" },
	{ "appearance.icon-theme", 's', SET, 0, "caf\xc3\xa9 icons", { 0 }, 0, 0, 0, NULL },
	{ "terminal.exec", 's', SET, 0, "\\\\server\\share\\term.exe", { 0 }, 0, 0, 0, NULL },
	{ "terminal.exec-arg", 's', SET, 0, "--title=\"a # b\"", { 0 }, 0, 0, 0,
	  "Argument that terminal takes before a command" },
	{ "list-view.row-shading-color", 's', SET, 0, "rgba(0, 0, 0, 0.1)", { 0 }, 0, 0, 0, NULL },
	{ "preferences.bulk-rename-tool", 's', SET, 0, "C:\\Program Files\\Renamer\\r.exe --flag",
	  { 0 }, 0, 0, 0, NULL },
	{ "archive.create-with-7z", 's', SET, 0, "tools\\7z.exe", { 0 }, 0, 0, 0, NULL },
	{ "archive.create-with-rar", 's', SET, 0, "rar\ta", { 0 }, 0, 0, 0, NULL },
	{ "archive.extract-with-7z", 's', SET, 0, "", { 0 }, 0, 0, 0, NULL },
	{ "archive.extract-with-rar", 's', SET, 0, " lead and trail ", { 0 }, 0, 0, 0, NULL },
	{ "appearance.mode", 's', SET, 0, "dark", { 0 }, 0, 0, 0, NULL },
	{ "preferences.click-policy", 's', SET, 0, "single", { 0 }, 0, 0, 0, NULL },
	{ "windows.path-separator", 's', SET, 0, "slash", { 0 }, 0, 0, 0, NULL },
	{ "windows.associations", 'l', SET, 0, NULL,
	  { "txt=notepad.exe %1", "log=C:\\Tools\\view.exe \"%1\"", "md=typora, with comma" },
	  3, 0, 0, NULL },
	{ "search.search-skip-folders", 'l', SET, 0, NULL,
	  { "node_modules", "a, b", "", "x#y", "'q'" }, 5, 0, 0, NULL },
	{ "plugins.disabled-extensions", 'l', SET, 0, NULL, { 0 }, 0, 0, 0, NULL },
	{ "thumbnailers.disable", 'l', SET, 0, NULL, { "image/x-one" }, 1, 0, 0, NULL },
	{ "preferences.show-hidden-files", 'b', SET, 0, NULL, { 0 }, 0, 1, 0, NULL },
	{ "preferences.menu-config.background-menu-compress", 'b', SET, 0, NULL, { 0 }, 0, 0, 0, NULL },
	{ "preferences.thumbnail-threads", 'i', SET, 0, NULL, { 0 }, 0, -3, 0, NULL },
	{ "preferences.thumbnail-limit", 'i', SET, 0, NULL, { 0 }, 0, 1000000, 0, NULL },
	{ "file-cache.max-size-gib", 'f', SET, 0, NULL, { 0 }, 0, 0, 2.5, NULL },
	{ "file-cache.memory-gib", 'f', SET, 0, NULL, { 0 }, 0, 0, 0.125, NULL },

	/* Escapes in bare text, which format 3 reads another way. */
	{ "search.search-regex-format", 's', HAND, 0, "a\\,b", { 0 }, 0, 0, 0, NULL },
	{ "list-view.row-hover-color", 's', HAND, 0, "'it''s'", { 0 }, 0, 0, 0, NULL },
	{ "preferences.image-viewers-with-external-sort", 'l', HAND, 0, "a, b\\, c",
	  { 0 }, 0, 0, 0, NULL },
};

#define N_ENTRIES (sizeof entries / sizeof entries[0])

static void
put_hex (FILE *out, const char *p, size_t n)
{
	size_t i;

	for (i = 0; i < n; i++)
		fprintf (out, "%02x", (unsigned char) p[i]);
}

/* Whether the old setters wrote an escape for this value. */
static int
escaped (const Entry *e)
{
	int i;

	if (e->text != NULL && strpbrk (e->text, "\\\t") != NULL)
		return 1;
	for (i = 0; i < e->n; i++) {
		if (strpbrk (e->list[i], "\\\t") != NULL)
			return 1;
	}
	return 0;
}

/* Left out of nobackslash. A comma list is one string to format 3. */
static int
left_out (const Entry *e)
{
	return escaped (e) || e->n > 1;
}

static int
same (const char *p, size_t n, const char *want)
{
	return n == strlen (want) && memcmp (p, want, n) == 0;
}

/* What this release reads at path, onto out. Returns 0 when it reads nothing
 * or, for a value the setters wrote, something other than what was set. */
static int
put_reading (FILE *out, shcl_doc *d, const Entry *e)
{
	size_t plen = strlen (e->path);
	int    ok = 1;

	fprintf (out, "%s\t%c\t%d\t", e->path, e->type, e->misread);
	switch (e->type) {
	case 's': {
		shcl_read_str r = shcl_read_string (d, e->path, plen);

		if (r.status != SHCL_GOOD && r.status != SHCL_EMPTY)
			return 0;
		put_hex (out, r.value.p, r.value.n);
		ok = e->how == HAND || same (r.value.p, r.value.n, e->text);
		break;
	}
	case 'l': {
		shcl_read_str_arr r = shcl_read_string_array (d, e->path, plen);
		size_t            i;

		if (r.status != SHCL_GOOD && r.status != SHCL_EMPTY)
			return 0;
		fprintf (out, "%zu:", r.n);
		for (i = 0; i < r.n; i++) {
			if (i > 0)
				fputc (',', out);
			put_hex (out, r.values[i].p, r.values[i].n);
			if (e->how == SET && ((int) i >= e->n ||
			                      !same (r.values[i].p, r.values[i].n, e->list[i])))
				ok = 0;
		}
		if (e->how == SET && (int) r.n != e->n)
			ok = 0;
		break;
	}
	case 'b': {
		shcl_read_bool r = shcl_read_bool_ (d, e->path, plen);

		if (r.status != SHCL_GOOD)
			return 0;
		fprintf (out, "%d", r.value);
		ok = r.value == (int) e->num;
		break;
	}
	case 'i': {
		shcl_read_i64 r = shcl_read_int (d, e->path, plen);

		if (r.status != SHCL_GOOD)
			return 0;
		fprintf (out, "%lld", (long long) r.value);
		ok = r.value == e->num;
		break;
	}
	case 'f': {
		shcl_read_f64 r = shcl_read_float (d, e->path, plen);

		if (r.status != SHCL_GOOD)
			return 0;
		fprintf (out, "%.17g", r.value);
		ok = r.value == e->real;
		break;
	}
	default:
		return 0;
	}
	fputc ('\n', out);
	return ok;
}

#ifdef SHCL_OLD_NO_WRITER
/* 1.0.0-beta1 had no writer, so a file then was typed by hand, quoted where
 * bare text wouldn't read back. That release's parser has the last word. */
static void
put_typed (char *buf, size_t cap, size_t *len, const char *s)
{
	size_t n = strlen (s);
	size_t i;

	if (cap - *len < 2 * n + 3)
		return;
	if (n > 0 && s[0] != ' ' && s[n - 1] != ' ' && strpbrk (s, "\"'#,\\\t[]*:") == NULL) {
		memcpy (buf + *len, s, n);
		*len += n;
	} else if (strchr (s, '"') != NULL && strpbrk (s, "'\\\t") == NULL) {
		/* single quotes need no escape, as the later writers chose */
		*len += (size_t) sprintf (buf + *len, "'%s'", s);
	} else {
		buf[(*len)++] = '"';
		for (i = 0; i < n; i++) {
			if (s[i] == '"' || s[i] == '\\' || s[i] == '\t')
				buf[(*len)++] = '\\';
			buf[(*len)++] = s[i] == '\t' ? 't' : s[i];
		}
		buf[(*len)++] = '"';
	}
}

static char *
make_text (int bare, size_t room, size_t *len)
{
	size_t cap = 16384 + room;
	char  *buf = malloc (cap);
	size_t i;

	if (buf == NULL)
		return NULL;
	*len = 0;
	for (i = 0; i < N_ENTRIES; i++) {
		const Entry *e = &entries[i];
		int          j;

		if (e->how != SET || (bare && left_out (e)))
			continue;
		if (e->comment != NULL)
			*len += (size_t) snprintf (buf + *len, cap - *len, "# %s\n", e->comment);
		*len += (size_t) snprintf (buf + *len, cap - *len, "%s:", e->path);
		if (e->type == 's' || (e->type == 'l' && e->n > 0))
			buf[(*len)++] = ' ';
		switch (e->type) {
		case 's':
			put_typed (buf, cap, len, e->text);
			break;
		case 'l':
			for (j = 0; j < e->n; j++) {
				if (j > 0)
					*len += (size_t) snprintf (buf + *len, cap - *len, ", ");
				put_typed (buf, cap, len, e->list[j]);
			}
			break;
		case 'b':
			*len += (size_t) snprintf (buf + *len, cap - *len, " %s", e->num ? "true" : "false");
			break;
		case 'i':
			*len += (size_t) snprintf (buf + *len, cap - *len, " %lld", (long long) e->num);
			break;
		case 'f':
			*len += (size_t) snprintf (buf + *len, cap - *len, " %.17g", e->real);
			break;
		}
		buf[(*len)++] = '\n';
		if (*len + 1024 > cap) {
			free (buf);
			return NULL;
		}
	}
	return buf;
}
#else
#ifdef SHCL_OLD_VOID_SETTERS
/* 1.0.0-beta2's setters return nothing */
#define SET_DONE(call) ((call), 1)
#else
#define SET_DONE(call) (call)
#endif

/* The setters the app called then, in nemo-config.c. */
static char *
make_text (int bare, size_t room, size_t *len)
{
	shcl_doc *d = shcl_new ();
	shcl_str  canon;
	char     *text;
	size_t    i;

	for (i = 0; i < N_ENTRIES; i++) {
		const Entry *e = &entries[i];
		size_t       plen = strlen (e->path);
		int          set = 1;

		if (e->how != SET || (bare && left_out (e)))
			continue;
		switch (e->type) {
		case 's':
			set = SET_DONE (shcl_set_string (d, e->path, plen, e->text, strlen (e->text)));
			break;
		case 'l': {
			size_t lens[6];
			int    j;

			for (j = 0; j < e->n; j++)
				lens[j] = strlen (e->list[j]);
			set = SET_DONE (shcl_set_string_array (d, e->path, plen, e->list, lens, (size_t) e->n));
			break;
		}
		case 'b':
			set = SET_DONE (shcl_set_bool (d, e->path, plen, (int) e->num));
			break;
		case 'i':
			set = SET_DONE (shcl_set_int (d, e->path, plen, e->num));
			break;
		case 'f':
			set = SET_DONE (shcl_set_float (d, e->path, plen, e->real));
			break;
		}
		if (!set) {
			fprintf (stderr, "could not set %s\n", e->path);
			shcl_free (d);
			return NULL;
		}
		if (e->comment != NULL)
			shcl_set_comment (d, e->path, plen, e->comment, strlen (e->comment));
	}

	canon = shcl_to_canonical (d);
	*len = canon.n;
	text = malloc (canon.n + room);
	if (text != NULL)
		memcpy (text, canon.p, canon.n);
	shcl_free (d);
	return text;
}
#endif

int
main (int argc, char *argv[])
{
	shcl_doc *back;
	FILE     *out;
	size_t    i, len, cap;
	int       hand, bare, bad = 0;
	char     *text;

	if (argc != 4 || (strcmp (argv[3], "plain") != 0 && strcmp (argv[3], "hand") != 0 &&
	                  strcmp (argv[3], "nobackslash") != 0)) {
		fprintf (stderr, "usage: %s <settings.shcl> <expect.txt> plain|hand|nobackslash\n",
		         argv[0]);
		return 2;
	}
	hand = strcmp (argv[3], "hand") == 0;
	bare = strcmp (argv[3], "nobackslash") == 0;

	text = make_text (bare, 4096, &len);
	if (text == NULL)
		return 1;
	cap = len + 4096;
	for (i = 0; hand && i < N_ENTRIES; i++) {
		int n;

		if (entries[i].how != HAND)
			continue;
		n = snprintf (text + len, cap - len, "%s: %s\n",
		              entries[i].path, entries[i].text);
		if (n < 0 || (size_t) n >= cap - len)
			return 1;
		len += (size_t) n;
	}

	out = fopen (argv[1], "wb");
	if (out == NULL || fwrite (text, 1, len, out) != len || fclose (out) != 0) {
		fprintf (stderr, "%s: cannot write %s\n", argv[0], argv[1]);
		return 1;
	}

	/* What this release made of its own file is what the app showed then. */
	back = shcl_parse (text, len);
	out = fopen (argv[2], "wb");
	if (back == NULL || out == NULL) {
		shcl_free (back);
		if (out != NULL)
			fclose (out);
		return 1;
	}
	for (i = 0; i < N_ENTRIES; i++) {
		if ((entries[i].how == HAND && !hand) || (bare && left_out (&entries[i])))
			continue;
		if (!put_reading (out, back, &entries[i])) {
			fprintf (stderr, "%s: %s does not read back as written\n",
			         argv[0], entries[i].path);
			bad = 1;
		}
	}
	if (fclose (out) != 0)
		bad = 1;

	free (text);
	shcl_free (back);
	return bad;
}
