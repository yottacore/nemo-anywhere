/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-dir-enum.c - directory enumeration that survives a long path on Windows.

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

#include <config.h>
#include "nemo-dir-enum.h"

#include <string.h>

#ifdef G_OS_WIN32
#include <windows.h>
#endif

/* Windows caps a directory at MAX_PATH-12 for the 8.3 name it reserves inside,
   and the walk appends "\*" on top of that, so treat anything at or past the
   cap as long rather than picking the exact byte where GLib gives up. */
#define LONG_PATH_FLOOR 248

gboolean
nemo_dir_enum_path_is_long (GFile *dir)
{
#ifdef G_OS_WIN32
	const char *path = g_file_peek_path (dir);

	return path != NULL && strlen (path) >= LONG_PATH_FLOOR;
#else
	(void) dir;
	return FALSE;
#endif
}

#ifdef G_OS_WIN32

/* "C:\a\b" -> "\\?\C:\a\b\*", "\\host\share\a" -> "\\?\UNC\host\share\a\*".
   The extended form wants backslashes and a fully qualified path; GFile gives
   us the second already, and paths typed with forward slashes the first. */
static wchar_t *
extended_search_pattern (const char *path)
{
	GString *native;
	wchar_t *wide;
	gsize i;

	if (path == NULL || path[0] == '\0') {
		return NULL;
	}

	native = g_string_new (path);
	for (i = 0; i < native->len; i++) {
		if (native->str[i] == '/') {
			native->str[i] = '\\';
		}
	}

	while (native->len > 1 && native->str[native->len - 1] == '\\') {
		g_string_truncate (native, native->len - 1);
	}

	if (g_str_has_prefix (native->str, "\\\\?\\")) {
		/* already extended */
	} else if (g_str_has_prefix (native->str, "\\\\")) {
		g_string_erase (native, 0, 2);
		g_string_prepend (native, "\\\\?\\UNC\\");
	} else {
		g_string_prepend (native, "\\\\?\\");
	}

	g_string_append (native, "\\*");

	wide = g_utf8_to_utf16 (native->str, -1, NULL, NULL, NULL);
	g_string_free (native, TRUE);

	return wide;
}

static gboolean
is_dot_entry (const wchar_t *name)
{
	return name[0] == L'.' && (name[1] == L'\0' || (name[1] == L'.' && name[2] == L'\0'));
}

#define NEMO_TYPE_LONG_ENUMERATOR (nemo_long_enumerator_get_type ())
G_DECLARE_FINAL_TYPE (NemoLongEnumerator, nemo_long_enumerator, NEMO, LONG_ENUMERATOR, GFileEnumerator)

struct _NemoLongEnumerator {
	GFileEnumerator parent_instance;

	GFile *dir;
	char *attributes;
	GFileQueryInfoFlags flags;

	HANDLE find;
	WIN32_FIND_DATAW data;
	gboolean have_pending;	/* data holds an entry not yet handed out */
	gboolean exhausted;
};

G_DEFINE_TYPE (NemoLongEnumerator, nemo_long_enumerator, G_TYPE_FILE_ENUMERATOR)

static GFileInfo *
long_enumerator_next_file (GFileEnumerator  *enumerator,
			   GCancellable     *cancellable,
			   GError          **error)
{
	NemoLongEnumerator *self = NEMO_LONG_ENUMERATOR (enumerator);

	while (!self->exhausted) {
		char *name;
		GFile *child;
		GFileInfo *info;

		if (g_cancellable_set_error_if_cancelled (cancellable, error)) {
			return NULL;
		}

		if (self->have_pending) {
			self->have_pending = FALSE;
		} else if (!FindNextFileW (self->find, &self->data)) {
			self->exhausted = TRUE;
			break;
		}

		if (is_dot_entry (self->data.cFileName)) {
			continue;
		}

		name = g_utf16_to_utf8 (self->data.cFileName, -1, NULL, NULL, NULL);
		if (name == NULL) {
			continue;
		}

		child = g_file_get_child (self->dir, name);
		info = g_file_query_info (child, self->attributes, self->flags, cancellable, NULL);
		g_object_unref (child);

		if (info == NULL) {
			/* deleted between the walk and the question, or unreadable */
			g_free (name);
			continue;
		}

		if (!g_file_info_has_attribute (info, G_FILE_ATTRIBUTE_STANDARD_NAME)) {
			g_file_info_set_name (info, name);
		}

		g_free (name);
		return info;
	}

	return NULL;
}

static gboolean
long_enumerator_close (GFileEnumerator  *enumerator,
		       G_GNUC_UNUSED GCancellable     *cancellable,
		       G_GNUC_UNUSED GError          **error)
{
	NemoLongEnumerator *self = NEMO_LONG_ENUMERATOR (enumerator);

	if (self->find != INVALID_HANDLE_VALUE) {
		FindClose (self->find);
		self->find = INVALID_HANDLE_VALUE;
	}

	return TRUE;
}

static void
long_enumerator_finalize (GObject *object)
{
	NemoLongEnumerator *self = NEMO_LONG_ENUMERATOR (object);

	if (self->find != INVALID_HANDLE_VALUE) {
		FindClose (self->find);
	}
	g_clear_object (&self->dir);
	g_free (self->attributes);

	G_OBJECT_CLASS (nemo_long_enumerator_parent_class)->finalize (object);
}

static void
nemo_long_enumerator_init (NemoLongEnumerator *self)
{
	self->find = INVALID_HANDLE_VALUE;
}

static void
nemo_long_enumerator_class_init (NemoLongEnumeratorClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = long_enumerator_finalize;
	G_FILE_ENUMERATOR_CLASS (klass)->next_file = long_enumerator_next_file;
	G_FILE_ENUMERATOR_CLASS (klass)->close_fn = long_enumerator_close;
}

static GFileEnumerator *
long_enumerator_open (GFile                *dir,
		      const char           *attributes,
		      GFileQueryInfoFlags   flags,
		      GCancellable         *cancellable,
		      GError              **error)
{
	NemoLongEnumerator *self;
	const char *path = g_file_peek_path (dir);
	wchar_t *pattern;
	HANDLE find;
	WIN32_FIND_DATAW data;

	if (g_cancellable_set_error_if_cancelled (cancellable, error)) {
		return NULL;
	}

	pattern = extended_search_pattern (path);
	if (pattern == NULL) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_FILENAME,
			     "Cannot build an extended-length pattern for \"%s\"",
			     path != NULL ? path : "");
		return NULL;
	}

	find = FindFirstFileW (pattern, &data);
	g_free (pattern);

	if (find == INVALID_HANDLE_VALUE) {
		DWORD code = GetLastError ();
		char *message = g_win32_error_message (code);

		g_set_error (error, G_IO_ERROR, g_io_error_from_win32_error ((gint) code),
			     "Error opening directory \"%s\": %s", path, message);
		g_free (message);
		return NULL;
	}

	self = g_object_new (NEMO_TYPE_LONG_ENUMERATOR, "container", dir, NULL);
	self->dir = g_object_ref (dir);
	self->attributes = g_strdup (attributes);
	self->flags = flags;
	self->find = find;
	self->data = data;
	self->have_pending = TRUE;

	return G_FILE_ENUMERATOR (self);
}

typedef struct {
	char *attributes;
	GFileQueryInfoFlags flags;
} OpenRequest;

static void
open_request_free (gpointer data)
{
	OpenRequest *request = data;

	g_free (request->attributes);
	g_free (request);
}

static void
open_in_thread (GTask        *task,
		gpointer      source,
		gpointer      task_data,
		GCancellable *cancellable)
{
	OpenRequest *request = task_data;
	GError *error = NULL;
	GFileEnumerator *enumerator;

	enumerator = long_enumerator_open (G_FILE (source), request->attributes,
					   request->flags, cancellable, &error);
	if (enumerator != NULL) {
		g_task_return_pointer (task, enumerator, g_object_unref);
	} else {
		g_task_return_error (task, error);
	}
}

#endif /* G_OS_WIN32 */

/* The forwarded case still completes through a task of our own, so the finish
   below has one kind of result to read whichever branch was taken. */
static void
forwarded_ready (GObject      *source,
		 GAsyncResult *result,
		 gpointer      user_data)
{
	GTask *task = user_data;
	GError *error = NULL;
	GFileEnumerator *enumerator;

	enumerator = g_file_enumerate_children_finish (G_FILE (source), result, &error);
	if (enumerator != NULL) {
		g_task_return_pointer (task, enumerator, g_object_unref);
	} else {
		g_task_return_error (task, error);
	}
	g_object_unref (task);
}

GFileEnumerator *
nemo_enumerate_children (GFile                *dir,
			 const char           *attributes,
			 GFileQueryInfoFlags   flags,
			 GCancellable         *cancellable,
			 GError              **error)
{
#ifdef G_OS_WIN32
	if (nemo_dir_enum_path_is_long (dir)) {
		return long_enumerator_open (dir, attributes, flags, cancellable, error);
	}
#endif
	return g_file_enumerate_children (dir, attributes, flags, cancellable, error);
}

void
nemo_enumerate_children_async (GFile                *dir,
			       const char           *attributes,
			       GFileQueryInfoFlags   flags,
			       int                   io_priority,
			       GCancellable         *cancellable,
			       GAsyncReadyCallback   callback,
			       gpointer              user_data)
{
	GTask *task = g_task_new (dir, cancellable, callback, user_data);

	g_task_set_priority (task, io_priority);
	g_task_set_source_tag (task, nemo_enumerate_children_async);

#ifdef G_OS_WIN32
	if (nemo_dir_enum_path_is_long (dir)) {
		OpenRequest *request = g_new0 (OpenRequest, 1);

		request->attributes = g_strdup (attributes);
		request->flags = flags;
		g_task_set_task_data (task, request, open_request_free);
		g_task_run_in_thread (task, open_in_thread);
		g_object_unref (task);
		return;
	}
#endif

	g_file_enumerate_children_async (dir, attributes, flags, io_priority,
					 cancellable, forwarded_ready, task);
}

GFileEnumerator *
nemo_enumerate_children_finish (GFile         *dir,
				GAsyncResult  *result,
				GError       **error)
{
	g_return_val_if_fail (g_task_is_valid (result, dir), NULL);

	return g_task_propagate_pointer (G_TASK (result), error);
}

GFileType
nemo_dir_enum_file_type (GFileInfo *info)
{
	if (info == NULL ||
	    !g_file_info_has_attribute (info, G_FILE_ATTRIBUTE_STANDARD_TYPE)) {
		return G_FILE_TYPE_UNKNOWN;
	}

	return g_file_info_get_file_type (info);
}
