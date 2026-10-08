/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* arc-scan.h - the walk behind the size totals.

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

/* Walks the selection and everything below it by the 4 follow options, and
 * hands each path to a path list wiht what it needs of them. "Background
 * scan" in project/design_docs/20260929-101432_compression.md.
 *
 * Folders are listed through the host's walk. Paths go in canonical, and a
 * folder already walked by a path needing no more options isn't walked
 * again, which is also what ends a loop. A link onto a network share, or a
 * share mounted in a folder, is left out unless the scan was made with
 * shares on, and the report counts them. A .lnk is a file like any other.
 *
 * The walk runs on a thread of its own. The calls here are for the thread
 * that started it, and the report comes there through its main context.
 */

#ifndef ARC_SCAN_H
#define ARC_SCAN_H

#include <gio/gio.h>

#include "arc-host.h"
#include "arc-mounts.h"
#include "arc-path-list.h"

G_BEGIN_DECLS

typedef struct {
	guint64  totals[ARC_MIX_COUNT];
	guint    follow;	/* the options as last set */
	guint    files;		/* in the list, for any mix */
	guint    shares;	/* links and folders left out for being on a share */
	guint    unreadable;	/* folders that couldn't be listed */
	/* Every path the options allow was walked, so the total for them and
	   each change are exact. */
	gboolean done;
} ArcScanReport;

typedef void (*ArcScanReported) (gpointer data, const ArcScanReport *report);

typedef struct _ArcScan ArcScan;

/* paths is the selection, absolute, and base the folder it is in. shares is
   for the job's own scan after OK, which may follow a link onto a share.
   mounts is taken over, or NULL for the system's table. The host is copied,
   and its data has to last as long as the scan.
   Returns: (transfer full): free with arc_scan_free */
ArcScan *arc_scan_new          (const ArcHost       *host,
				const char          *base,
				const char * const  *paths,
				guint                follow,
				gboolean             shares,
				ArcMountTable       *mounts);

/* Reports come at most every 0.25 s, and only when something changed. */
void     arc_scan_start        (ArcScan             *scan,
				ArcScanReported      reported,
				gpointer             data);

/* An option turned off backs the walk out of what it now leaves out, and
   it goes on. One turned on starts it again, and only paths not seen yet
   add to the list. */
void     arc_scan_set_follow   (ArcScan             *scan,
				guint                follow);

/* Stops it, and frees the list and the totals. Fine from inside the
   report. */
void     arc_scan_free         (ArcScan             *scan);

/* What the options as set would put in the archive, and what turning one off
   would take from that. */
guint64  arc_scan_report_total  (const ArcScanReport *report);
guint64  arc_scan_report_change (const ArcScanReport *report,
				 ArcFollow            option);

G_END_DECLS

#endif /* ARC_SCAN_H */
