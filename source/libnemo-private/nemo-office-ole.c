/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-office-ole.c - text and preview of .doc, .xls and .ppt files.

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

#include <config.h>

#include "nemo-office-ole.h"

#include <string.h>

#define MAX_STREAM_BYTES  (64 * 1024 * 1024)
#define MAX_SUMMARY_BYTES (16 * 1024 * 1024)
#define MAX_PROPERTIES    4096
#define MAX_PPT_DEPTH     64

static guint16
rd16 (const guint8 *p)
{
	return (guint16) (p[0] | (p[1] << 8));
}

static guint32
rd32 (const guint8 *p)
{
	return (guint32) p[0] | ((guint32) p[1] << 8) | ((guint32) p[2] << 16) | ((guint32) p[3] << 24);
}

static double
rd_double (const guint8 *p)
{
	double v;

	memcpy (&v, p, sizeof v);
	return v;
}

/* The text so far, and where it stops. A piece is cut to the room left
   before it is converted, since one byte in never makes less than one out. */
typedef struct {
	GString *s;
	gsize    max;
} Out;

static gboolean
out_full (const Out *out)
{
	return out->s->len >= out->max;
}

static gsize
out_room (const Out *out)
{
	return out_full (out) ? 0 : out->max - out->s->len;
}

static void
append_utf16 (Out *out, const guint8 *data, gsize n_units)
{
	gsize written = 0;
	gchar *utf8;

	n_units = MIN (n_units, out_room (out));
	if (n_units == 0) {
		return;
	}

	utf8 = g_convert ((const gchar *) data, n_units * 2, "UTF-8", "UTF-16LE", NULL, &written, NULL);
	if (utf8 != NULL) {
		g_string_append_len (out->s, utf8, written);
		g_free (utf8);
	}
}

/* The one-byte form of a UTF-16 string: every byte is a code unit's low half. */
static void
append_latin1 (Out *out, const guint8 *data, gsize n)
{
	gsize i;

	n = MIN (n, out_room (out));
	for (i = 0; i < n; i++) {
		g_string_append_unichar (out->s, data[i]);
	}
}

static void
append_cp1252 (Out *out, const guint8 *data, gsize n)
{
	gsize written = 0;
	gchar *utf8;

	n = MIN (n, out_room (out));
	if (n == 0) {
		return;
	}

	utf8 = g_convert ((const gchar *) data, n, "UTF-8", "WINDOWS-1252", NULL, &written, NULL);
	if (utf8 != NULL) {
		g_string_append_len (out->s, utf8, written);
		g_free (utf8);
	} else {
		append_latin1 (out, data, n);
	}
}

/* Control characters mark paragraphs, cells and fields in the binary formats;
 * a line break for the paragraph ones, a space for the rest. UTF-8 never puts a
 * byte under 0x80 inside a multibyte sequence, so a byte-wise pass is safe. */
static char *
out_finish (Out *out)
{
	gsize i;

	if (out->s->len > out->max) {
		g_string_truncate (out->s, out->max);
	}

	for (i = 0; i < out->s->len; i++) {
		guchar c = (guchar) out->s->str[i];

		if (c >= 0x20 || c == '\t' || c == '\n') {
			continue;
		}

		out->s->str[i] = (c == '\r' || c == 0x0B || c == 0x0C) ? '\n' : ' ';
	}

	return g_string_free (out->s, FALSE);
}

/* Word 97 and later keep the text as pieces listed in a table stream, each
 * piece either one-byte code page text or UTF-16; Word 6 and 95 keep it in
 * one run. Both are read. Headers, footnotes and text boxes come out along
 * with the body. */

#define FIB_IDENT        0xA5EC
#define FIB_NFIB_97      0x00C1
#define FIB_FLAGS        0x0A
#define FIB_F_ENCRYPTED  0x0100
#define FIB_F_TABLE_1    0x0200
#define FIB_FC_MIN       0x18
#define FIB_FC_MAC       0x1C
#define FIB_FC_CLX       0x1A2
#define FIB_LCB_CLX      0x1A6
#define FIB_MIN_LEN      0x1AA

#define PIECE_COMPRESSED 0x40000000
#define PIECE_FC_MASK    0x3FFFFFFF

static void
append_piece (Out *out, const guint8 *doc, gsize doc_len, guint32 fc, gsize cp_count)
{
	gsize off = fc & PIECE_FC_MASK;

	if (fc & PIECE_COMPRESSED) {
		off /= 2;
		if (off < doc_len) {
			append_cp1252 (out, doc + off, MIN (cp_count, doc_len - off));
		}
	} else if (off < doc_len) {
		append_utf16 (out, doc + off, MIN (cp_count, (doc_len - off) / 2));
	}
}

/* The Clx: any number of property blobs, then the piece table proper - an array
 * of character positions and one descriptor per piece. */
static gboolean
read_pieces (Out *out, const guint8 *doc, gsize doc_len, const guint8 *clx, gsize clx_len)
{
	gsize pos = 0;

	while (pos < clx_len) {
		guint8 kind = clx[pos];

		if (kind == 0x01) {
			if (pos + 3 > clx_len) {
				return FALSE;
			}
			pos += 3 + rd16 (clx + pos + 1);
		} else if (kind == 0x02) {
			gsize lcb, n, i;
			const guint8 *plc;

			if (pos + 5 > clx_len) {
				return FALSE;
			}

			lcb = rd32 (clx + pos + 1);
			plc = clx + pos + 5;
			lcb = MIN (lcb, clx_len - (pos + 5));

			if (lcb < 4) {
				return FALSE;
			}

			n = (lcb - 4) / 12;

			/* Pieces may all point at the same text, so the room
			   left is what bounds this. */
			for (i = 0; i < n && !out_full (out); i++) {
				guint32 cp_start = rd32 (plc + i * 4);
				guint32 cp_end = rd32 (plc + (i + 1) * 4);
				const guint8 *pcd = plc + (n + 1) * 4 + i * 8;

				if (cp_end > cp_start) {
					append_piece (out, doc, doc_len, rd32 (pcd + 2), cp_end - cp_start);
				}
			}

			return n > 0;
		} else {
			return FALSE;
		}
	}

	return FALSE;
}

static void
word_text (Out *out, const guint8 *doc, gsize doc_len, const guint8 *table, gsize table_len)
{
	guint16 flags;
	gboolean have_text = FALSE;

	if (doc == NULL || doc_len < 0x20 || rd16 (doc) != FIB_IDENT) {
		return;
	}

	flags = rd16 (doc + FIB_FLAGS);
	if (flags & FIB_F_ENCRYPTED) {
		return;
	}

	if (rd16 (doc + 2) >= FIB_NFIB_97 && doc_len >= FIB_MIN_LEN && table != NULL) {
		gsize fc = rd32 (doc + FIB_FC_CLX);
		gsize lcb = rd32 (doc + FIB_LCB_CLX);

		if (fc < table_len) {
			have_text = read_pieces (out, doc, doc_len, table + fc, MIN (lcb, table_len - fc));
		}
	}

	if (!have_text) {
		/* Word 6/95, or a piece table that could not be read: the text sits in
		 * one run between fcMin and fcMac. */
		gsize fc_min = rd32 (doc + FIB_FC_MIN);
		gsize fc_mac = rd32 (doc + FIB_FC_MAC);

		if (fc_min < fc_mac && fc_mac <= doc_len) {
			append_cp1252 (out, doc + fc_min, fc_mac - fc_min);
		}
	}
}

/* Returns: (transfer full): free with g_free */
char *
nemo_office_word_text (const guint8 *doc, gsize doc_len, const guint8 *table, gsize table_len, gsize max_len)
{
	Out out = { g_string_new (NULL), max_len };

	word_text (&out, doc, doc_len, table, table_len);
	return out_finish (&out);
}

/* Excel, BIFF5 through BIFF8. Shared strings, plain labels, formula results,
 * numbers and sheet names are taken; layout and formatting are not. */

typedef struct {
	const guint8 *data;
	gsize len;
	gsize pos;
	gsize rec_end;
	gboolean biff8;
	Out *out;
} Biff;

#define REC_BOF8       0x0809
#define REC_BOF2       0x0009
#define REC_BOF3       0x0209
#define REC_BOF4       0x0409
#define REC_EOF        0x000A
#define REC_FORMULA    0x0006
#define REC_CONTINUE   0x003C
#define REC_BOUNDSHEET 0x0085
#define REC_MULRK      0x00BD
#define REC_RSTRING    0x00D6
#define REC_SST        0x00FC
#define REC_NUMBER     0x0203
#define REC_LABEL      0x0204
#define REC_STRING     0x0207
#define REC_RK         0x027E

/* Long strings run on into CONTINUE records. Step into the next one, if that is
 * what follows. */
static gboolean
biff_continue (Biff *b)
{
	gsize len;

	if (b->rec_end + 4 > b->len || rd16 (b->data + b->rec_end) != REC_CONTINUE) {
		return FALSE;
	}

	len = rd16 (b->data + b->rec_end + 2);
	b->pos = b->rec_end + 4;
	b->rec_end = MIN (b->pos + len, b->len);
	return TRUE;
}

/* n more bytes of a field, which may sit at the start of the next CONTINUE. A
 * field split down the middle is given up on. */
static gboolean
biff_need (Biff *b, gsize n)
{
	while (b->pos + n > b->rec_end) {
		if (b->pos < b->rec_end || !biff_continue (b)) {
			return FALSE;
		}
	}

	return TRUE;
}

static guint8
biff_u8 (Biff *b)
{
	return b->data[b->pos++];
}

static guint16
biff_u16 (Biff *b)
{
	guint16 v = rd16 (b->data + b->pos);

	b->pos += 2;
	return v;
}

static guint32
biff_u32 (Biff *b)
{
	guint32 v = rd32 (b->data + b->pos);

	b->pos += 4;
	return v;
}

static void
biff_skip (Biff *b, gsize n)
{
	while (n > 0) {
		gsize take;

		if (b->pos >= b->rec_end && !biff_continue (b)) {
			return;
		}

		take = MIN (n, b->rec_end - b->pos);
		b->pos += take;
		n -= take;
	}
}

/* cch characters of a BIFF8 string body. Where the body crosses into a CONTINUE
 * record, the continuation opens with a fresh flags byte of its own. */
static void
biff_chars (Biff *b, gsize cch, gboolean wide)
{
	while (cch > 0) {
		gsize unit, take;

		if (b->pos >= b->rec_end) {
			if (!biff_continue (b) || b->pos >= b->rec_end) {
				break;
			}
			wide = biff_u8 (b) & 1;
		}

		unit = wide ? 2 : 1;
		take = MIN ((b->rec_end - b->pos) / unit, cch);

		if (take == 0) {
			b->pos = b->rec_end;
			continue;
		}

		if (wide) {
			append_utf16 (b->out, b->data + b->pos, take);
		} else {
			append_latin1 (b->out, b->data + b->pos, take);
		}

		b->pos += take * unit;
		cch -= take;
	}

	g_string_append_c (b->out->s, ' ');
}

/* XLUnicodeRichExtendedString: the shape of every BIFF8 string, with optional
 * formatting runs and Asian phonetic data hanging off the end. */
static void
biff_string8 (Biff *b)
{
	guint16 cch, runs = 0;
	guint32 ext = 0;
	guint8 flags;

	if (!biff_need (b, 3)) {
		return;
	}

	cch = biff_u16 (b);
	flags = biff_u8 (b);

	if ((flags & 0x08) && biff_need (b, 2)) {
		runs = biff_u16 (b);
	}
	if ((flags & 0x04) && biff_need (b, 4)) {
		ext = biff_u32 (b);
	}

	biff_chars (b, cch, flags & 1);
	biff_skip (b, (gsize) runs * 4 + ext);
}

/* The older one-byte strings, in the workbook's code page. */
static void
biff_string5 (Biff *b, gsize cch)
{
	if (!biff_need (b, cch)) {
		return;
	}

	append_cp1252 (b->out, b->data + b->pos, cch);
	g_string_append_c (b->out->s, ' ');
	b->pos += cch;
}

static void
biff_cell_string (Biff *b)
{
	if (b->biff8) {
		biff_string8 (b);
	} else if (biff_need (b, 2)) {
		biff_string5 (b, biff_u16 (b));
	}
}

/* RK: a number squeezed into 30 bits, as an integer or the top of a double. */
static double
rk_value (guint32 rk)
{
	double v;

	if (rk & 2) {
		v = (double) ((gint32) rk >> 2);
	} else {
		guint64 bits = ((guint64) (rk & ~3u)) << 32;

		memcpy (&v, &bits, sizeof v);
	}

	return (rk & 1) ? v / 100 : v;
}

static void
append_number (Out *out, double v)
{
	if (!out_full (out)) {
		g_string_append_printf (out->s, "%.15g ", v);
	}
}

static void
parse_biff (const guint8 *data, gsize len, Out *out)
{
	Biff b = { data, len, 0, 0, TRUE, out };
	gsize pos = 0;

	while (pos + 4 <= len && !out_full (out)) {
		guint16 type = rd16 (data + pos);
		guint16 rlen = rd16 (data + pos + 2);

		b.pos = pos + 4;
		b.rec_end = MIN (b.pos + rlen, len);

		switch (type) {
		case REC_BOF8:
		case REC_BOF2:
		case REC_BOF3:
		case REC_BOF4:
			if (biff_need (&b, 2)) {
				b.biff8 = biff_u16 (&b) >= 0x0600;
			}
			break;

		case REC_SST:
			if (biff_need (&b, 8)) {
				guint32 unique;

				biff_u32 (&b);
				unique = biff_u32 (&b);

				/* The count comes off the file, so it can claim far more
				   strings than the record holds. biff_string8 leaves pos
				   alone when the record has run out and no CONTINUE
				   follows it, so without the progress test a truncated
				   workbook spins here once per claimed string, up to four
				   billion times. */
				while (unique-- > 0 && b.pos < b.len && !out_full (out)) {
					gsize before = b.pos;

					biff_string8 (&b);
					if (b.pos == before) {
						break;
					}
				}
			}
			break;

		case REC_LABEL:
		case REC_RSTRING:
			if (biff_need (&b, 6)) {
				b.pos += 6;
				biff_cell_string (&b);
			}
			break;

		case REC_STRING:
			biff_cell_string (&b);
			break;

		case REC_BOUNDSHEET:
			if (biff_need (&b, 8)) {
				guint8 cch;

				b.pos += 6;
				cch = biff_u8 (&b);

				if (b.biff8) {
					guint8 flags = biff_u8 (&b);

					biff_chars (&b, cch, flags & 1);
				} else {
					biff_string5 (&b, cch);
				}
			}
			break;

		case REC_NUMBER:
			if (biff_need (&b, 14)) {
				append_number (out, rd_double (data + b.pos + 6));
			}
			break;

		case REC_RK:
			if (biff_need (&b, 10)) {
				append_number (out, rk_value (rd32 (data + b.pos + 6)));
			}
			break;

		case REC_MULRK:
			/* row, first column, then (ixfe, rk) pairs up to a trailing last column */
			b.pos += 4;
			while (b.pos + 8 <= b.rec_end) {
				append_number (out, rk_value (rd32 (data + b.pos + 2)));
				b.pos += 6;
			}
			break;

		case REC_FORMULA:
			/* A string result is carried by the STRING record that follows. */
			if (biff_need (&b, 14) && rd16 (data + b.pos + 12) != 0xFFFF) {
				append_number (out, rd_double (data + b.pos + 6));
			}
			break;

		default:
			break;
		}

		pos = b.rec_end;
	}
}

gboolean
nemo_office_biff_magic (const guint8 *p, gsize len)
{
	guint16 type;

	if (len < 4) {
		return FALSE;
	}

	type = rd16 (p);
	return type == REC_BOF8 || type == REC_BOF2 || type == REC_BOF3 || type == REC_BOF4;
}

/* Returns: (transfer full): free with g_free */
char *
nemo_office_biff_text (const guint8 *data, gsize len, gsize max_len)
{
	Out out = { g_string_new (NULL), max_len };

	parse_biff (data, len, &out);
	return out_finish (&out);
}

/* PowerPoint. The record tree in the PowerPoint Document stream is walked and
 * every text atom taken, wherever it sits: slides, notes, titles, outline text. */

#define RT_TEXT_CHARS 0x0FA0
#define RT_TEXT_BYTES 0x0FA8
#define RT_CSTRING    0x0FBA

/* Every record is an 8-byte header and a payload; a version nibble of 0xF marks a
 * container whose payload is more records. Atoms of any other kind are stepped
 * over, which is what keeps embedded pictures out of the output. */
static void
walk_records (const guint8 *data, gsize len, Out *out, int depth)
{
	gsize pos = 0;

	while (pos + 8 <= len && !out_full (out)) {
		guint16 ver_inst = rd16 (data + pos);
		guint16 type = rd16 (data + pos + 2);
		gsize rlen = rd32 (data + pos + 4);

		pos += 8;
		rlen = MIN (rlen, len - pos);

		if ((ver_inst & 0x0F) == 0x0F) {
			if (depth < MAX_PPT_DEPTH) {
				walk_records (data + pos, rlen, out, depth + 1);
			}
		} else if (type == RT_TEXT_CHARS || type == RT_CSTRING) {
			append_utf16 (out, data + pos, rlen / 2);
			g_string_append_c (out->s, '\n');
		} else if (type == RT_TEXT_BYTES) {
			append_latin1 (out, data + pos, rlen);
			g_string_append_c (out->s, '\n');
		}

		pos += rlen;
	}
}

/* Returns: (transfer full): free with g_free */
char *
nemo_office_ppt_text (const guint8 *data, gsize len, gsize max_len)
{
	Out out = { g_string_new (NULL), max_len };

	walk_records (data, len, &out, 0);
	return out_finish (&out);
}

/* Returns: (transfer full): free with g_free */
char *
nemo_office_ole_text (NemoOle2 *ole, gsize max_len)
{
	g_autoptr (GBytes) stream = NULL;
	const guint8 *p;
	gsize len = 0;

	stream = nemo_ole2_read (ole, "WordDocument", MAX_STREAM_BYTES);
	if (stream != NULL) {
		g_autoptr (GBytes) table = NULL;
		const guint8 *t = NULL;
		gsize table_len = 0;

		p = g_bytes_get_data (stream, &len);
		if (len >= FIB_FLAGS + 2) {
			table = nemo_ole2_read (ole, (rd16 (p + FIB_FLAGS) & FIB_F_TABLE_1) ? "1Table" : "0Table",
						MAX_STREAM_BYTES);
		}
		if (table != NULL) {
			t = g_bytes_get_data (table, &table_len);
		}

		return nemo_office_word_text (p, len, t, table_len, max_len);
	}

	stream = nemo_ole2_read (ole, "Workbook", MAX_STREAM_BYTES);
	if (stream == NULL) {
		stream = nemo_ole2_read (ole, "Book", MAX_STREAM_BYTES);
	}
	if (stream != NULL) {
		p = g_bytes_get_data (stream, &len);
		return nemo_office_biff_text (p, len, max_len);
	}

	stream = nemo_ole2_read (ole, "PowerPoint Document", MAX_STREAM_BYTES);
	if (stream != NULL) {
		p = g_bytes_get_data (stream, &len);
		return nemo_office_ppt_text (p, len, max_len);
	}

	return NULL;
}

/* The summary stream is a property set. Its thumbnail property is
 * clipboard data: a size, -1 for a Windows clipboard format, the format, then
 * the data. A bitmap (CF_DIB, or CF_BITMAP, which is kept the same way) is a
 * .bmp file without its 14 byte file header. A metafile is left alone, since
 * no loader here draws one. */

#define PID_THUMBNAIL 0x11
#define VT_CF         0x47
#define CF_BITMAP     2
#define CF_DIB        8

static const guint8 summary_fmtid[16] = {
	0xe0, 0x85, 0x9f, 0xf2, 0xf9, 0x4f, 0x68, 0x10, 0xab, 0x91, 0x08, 0x00, 0x2b, 0x27, 0xb3, 0xd9
};

static GBytes *
dib_to_bmp (const guint8 *dib, gsize len)
{
	guint32 header, bits, colors;
	gsize extra = 0;
	guint8 *bmp;

	if (len < 12) {
		return NULL;
	}

	header = rd32 (dib);
	if (header == 12) {
		bits = rd16 (dib + 10);
		if (bits <= 8) {
			extra = ((gsize) 1 << bits) * 3;
		}
	} else if (header >= 40 && header <= 124 && len >= 40) {
		guint32 compression = rd32 (dib + 16);

		bits = rd16 (dib + 14);
		colors = rd32 (dib + 32);
		if (colors > 256) {
			return NULL;
		}
		if (bits <= 8) {
			extra = (colors != 0 ? colors : (1u << bits)) * 4;
		} else {
			extra = (gsize) colors * 4;
		}
		/* The masks, after a plain 40 byte header. */
		if (header == 40 && compression == 3) {
			extra += 12;
		} else if (header == 40 && compression == 6) {
			extra += 16;
		}
	} else {
		return NULL;
	}

	if (header > len || extra > len - header || len > G_MAXUINT32 - 14) {
		return NULL;
	}

	bmp = g_malloc (len + 14);
	bmp[0] = 'B';
	bmp[1] = 'M';
	bmp[2] = (guint8) ((len + 14) & 0xff);
	bmp[3] = (guint8) (((len + 14) >> 8) & 0xff);
	bmp[4] = (guint8) (((len + 14) >> 16) & 0xff);
	bmp[5] = (guint8) (((len + 14) >> 24) & 0xff);
	memset (bmp + 6, 0, 4);
	bmp[10] = (guint8) ((14 + header + extra) & 0xff);
	bmp[11] = (guint8) (((14 + header + extra) >> 8) & 0xff);
	bmp[12] = 0;
	bmp[13] = 0;
	memcpy (bmp + 14, dib, len);

	return g_bytes_new_take (bmp, len + 14);
}

/* Returns: (transfer full): unref with g_bytes_unref */
GBytes *
nemo_office_summary_preview (const guint8 *p, gsize len)
{
	gsize set, set_len, n, i;

	if (len < 0x30 || rd16 (p) != 0xfffe || rd32 (p + 0x18) < 1 || memcmp (p + 0x1c, summary_fmtid, 16) != 0) {
		return NULL;
	}

	set = rd32 (p + 0x2c);
	if (set > len - 8) {
		return NULL;
	}
	set_len = MIN ((gsize) rd32 (p + set), len - set);
	if (set_len < 8) {
		return NULL;
	}
	n = MIN ((gsize) rd32 (p + set + 4), (set_len - 8) / 8);
	n = MIN (n, (gsize) MAX_PROPERTIES);

	for (i = 0; i < n; i++) {
		const guint8 *entry = p + set + 8 + i * 8;
		gsize at = rd32 (entry + 4), avail, cb;
		const guint8 *value;

		if (rd32 (entry) != PID_THUMBNAIL) {
			continue;
		}
		if (at < 8 || set_len < 16 || at > set_len - 16) {
			return NULL;
		}

		value = p + set + at;
		avail = set_len - at - 16;
		cb = rd32 (value + 4);
		if (rd16 (value) != VT_CF || rd32 (value + 8) != G_MAXUINT32 ||
		    (rd32 (value + 12) != CF_DIB && rd32 (value + 12) != CF_BITMAP)) {
			return NULL;
		}

		/* The size counts the 2 format words. */
		return dib_to_bmp (value + 16, cb >= 8 ? MIN (cb - 8, avail) : avail);
	}

	return NULL;
}

/* Returns: (transfer full): unref with g_bytes_unref */
GBytes *
nemo_office_ole_preview (NemoOle2 *ole)
{
	g_autoptr (GBytes) summary = nemo_ole2_read (ole, "\005SummaryInformation", MAX_SUMMARY_BYTES);
	const guint8 *p;
	gsize len = 0;

	if (summary == NULL) {
		return NULL;
	}

	p = g_bytes_get_data (summary, &len);
	return nemo_office_summary_preview (p, len);
}
