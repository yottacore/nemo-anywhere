/* nemo-search-engine-win32.h - search through the Windows Search index
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */

#ifndef NEMO_SEARCH_ENGINE_WIN32_H
#define NEMO_SEARCH_ENGINE_WIN32_H

#include <libnemo-private/nemo-search-engine.h>

#define NEMO_TYPE_SEARCH_ENGINE_WIN32		(nemo_search_engine_win32_get_type ())
#define NEMO_SEARCH_ENGINE_WIN32(obj)		(G_TYPE_CHECK_INSTANCE_CAST ((obj), NEMO_TYPE_SEARCH_ENGINE_WIN32, NemoSearchEngineWin32))
#define NEMO_SEARCH_ENGINE_WIN32_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST ((klass), NEMO_TYPE_SEARCH_ENGINE_WIN32, NemoSearchEngineWin32Class))
#define NEMO_IS_SEARCH_ENGINE_WIN32(obj)	(G_TYPE_CHECK_INSTANCE_TYPE ((obj), NEMO_TYPE_SEARCH_ENGINE_WIN32))

typedef struct NemoSearchEngineWin32Details NemoSearchEngineWin32Details;

typedef struct NemoSearchEngineWin32 {
	NemoSearchEngine parent;
	NemoSearchEngineWin32Details *details;
} NemoSearchEngineWin32;

typedef struct {
	NemoSearchEngineClass parent_class;
} NemoSearchEngineWin32Class;

GType             nemo_search_engine_win32_get_type (void);
NemoSearchEngine *nemo_search_engine_win32_new      (void);

/* The query the index is asked, spelled out so it can be checked without an index. */
gchar   *nemo_search_win32_build_sql        (const gchar *folder,
					     gboolean     recurse,
					     const gchar *file_pattern,
					     gboolean     file_pattern_is_regex,
					     const gchar *content_text);

/* Whether the index has anything under the folder. Opens the index itself. */
gboolean nemo_search_win32_folder_is_indexed (const gchar *folder);

#endif /* NEMO_SEARCH_ENGINE_WIN32_H */
