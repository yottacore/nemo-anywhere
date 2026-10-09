/* nemo-instances-win32.c - the copies' named pipes, under nemo-instances.c
 *
 * On Windows nothing runs a session bus, and GLib would start one of its own
 * (gdbus.exe) the first time anything asked for it. So each copy serves a pipe
 * instead, named for the user, the logon session and the process:
 * \\.\pipe\NemoAnywhere.<user SID>.<logon id>.<pid>. The pipe goes when the
 * process does, however it ends, so a copy that died leaves nothing behind.
 * An elevated copy has a logon id of its own, so it and an ordinary one keep
 * apart.
 *
 * Only the same user may open a pipe, and only from the same integrity level
 * or above. Pipe names are shared by every session on the box, so a client
 * also checks that the pipe it opened belongs to the user before it says
 * anything, and lets the server identify it but not act as it.
 *
 * The list is every process in this session with a pipe by that name. Asking
 * the pipe file system for its names would be quicker, but wine cannot.
 *
 * One call per connection: an 8-byte length, then a GVariant, each way. Both
 * ends run on one box, so the GVariant goes in its own byte order. The
 * request is (interface, method, wants reply, parameters), the reply (ok,
 * error message, value). The handler runs on the main thread. A message
 * nobody waits on is answered as soon as it is read, so a sender never waits
 * on the other copy's main thread.
 *
 * Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */

#include <config.h>
#include <glib.h>

#include "nemo-instances-win32.h"

#include <string.h>
#include <windows.h>
#include <sddl.h>
#include <aclapi.h>
#include <tlhelp32.h>

#define BUFFER_SIZE (64 * 1024)
#define MESSAGE_MAX (64 * 1024 * 1024)

/* A client that connects and then says nothing is cut off after this. */
#define READ_TIMEOUT_MS 10000
/* How long the server waits for a client to read its answer and go. */
#define CLOSE_TIMEOUT_MS 2000
/* The caller gave up long before; this only bounds a thread. */
#define ANSWER_TIMEOUT_MS 60000

#define WORKERS 4

#define REQUEST_TYPE G_VARIANT_TYPE ("(ssbv)")
#define REPLY_TYPE   G_VARIANT_TYPE ("(bsv)")

static PSID     user_sid;
static wchar_t *user_sid_text;
static wchar_t *label_text;	/* integrity level SID, or NULL */
static char    *pipe_prefix;

typedef struct {
	NemoInstancesDispatch  dispatch;
	wchar_t               *name;
	PSECURITY_DESCRIPTOR   sd;
	HANDLE                 stop;
	HANDLE                 listening;
	GThreadPool           *workers;
} Server;

typedef struct {
	Server *server;
	HANDLE  pipe;
} Connection;

/* One call handed to the main thread. */
typedef struct {
	NemoInstancesDispatch  dispatch;
	char                  *interface_name;
	char                  *method_name;
	GVariant              *parameters;
	GMutex                 lock;
	GCond                  cond;
	gboolean               done;
	GVariant              *reply;
	char                  *error_message;
} Call;

static Server  *server;
static GThread *accept_thread;

static void
set_win32_error (GError **error, DWORD code, const char *what)
{
	char *text = g_win32_error_message ((gint) code);
	GIOErrorEnum kind;

	switch (code) {
	case ERROR_FILE_NOT_FOUND:
		kind = G_IO_ERROR_NOT_FOUND;
		break;
	case ERROR_ACCESS_DENIED:
		kind = G_IO_ERROR_PERMISSION_DENIED;
		break;
	case ERROR_BROKEN_PIPE:
	case ERROR_NO_DATA:
	case ERROR_PIPE_NOT_CONNECTED:
		kind = G_IO_ERROR_BROKEN_PIPE;
		break;
	default:
		kind = G_IO_ERROR_FAILED;
	}
	g_set_error (error, G_IO_ERROR, kind, "%s: %s", what, text);
	g_free (text);
}

static char *
to_utf8 (const wchar_t *text)
{
	return g_utf16_to_utf8 ((const gunichar2 *) text, -1, NULL, NULL, NULL);
}

static wchar_t *
to_utf16 (const char *text)
{
	return (wchar_t *) g_utf8_to_utf16 (text, -1, NULL, NULL, NULL);
}

static gpointer
token_info (HANDLE token, TOKEN_INFORMATION_CLASS what)
{
	DWORD size = 0;
	gpointer info;

	GetTokenInformation (token, what, NULL, 0, &size);
	if (size == 0) {
		return NULL;
	}
	info = g_malloc (size);
	if (!GetTokenInformation (token, what, info, size, &size)) {
		g_free (info);
		return NULL;
	}
	return info;
}

static gboolean
know_who_we_are (GError **error)
{
	TOKEN_USER *user = NULL;
	TOKEN_MANDATORY_LABEL *label = NULL;
	TOKEN_STATISTICS stats;
	DWORD size;
	HANDLE token;
	wchar_t *text = NULL;
	char *sid;
	gboolean ok;

	if (pipe_prefix != NULL) {
		return TRUE;
	}
	if (!OpenProcessToken (GetCurrentProcess (), TOKEN_QUERY, &token)) {
		set_win32_error (error, GetLastError (), "Could not read who this is");
		return FALSE;
	}
	user = token_info (token, TokenUser);
	ok = user != NULL &&
	     GetTokenInformation (token, TokenStatistics, &stats, sizeof stats, &size) &&
	     ConvertSidToStringSidW (user->User.Sid, &text);
	if (!ok) {
		set_win32_error (error, GetLastError (), "Could not read who this is");
		CloseHandle (token);
		g_free (user);
		return FALSE;
	}

	/* Without it the pipe gets the default, which a lower level can write. */
	label = token_info (token, TokenIntegrityLevel);
	if (label != NULL) {
		wchar_t *label_sid = NULL;

		if (ConvertSidToStringSidW (label->Label.Sid, &label_sid)) {
			label_text = g_memdup2 (label_sid, (wcslen (label_sid) + 1) * sizeof (wchar_t));
			LocalFree (label_sid);
		}
		g_free (label);
	}
	CloseHandle (token);

	user_sid = g_memdup2 (user->User.Sid, GetLengthSid (user->User.Sid));
	user_sid_text = g_memdup2 (text, (wcslen (text) + 1) * sizeof (wchar_t));
	sid = to_utf8 (text);
	pipe_prefix = g_strdup_printf ("\\\\.\\pipe\\NemoAnywhere.%s.%08lx%08lx.", sid,
	                               (unsigned long) stats.AuthenticationId.HighPart,
	                               (unsigned long) stats.AuthenticationId.LowPart);
	g_free (sid);
	LocalFree (text);
	g_free (user);

	return TRUE;
}

static char *
pipe_name_of (DWORD pid)
{
	return g_strdup_printf ("%s%lu", pipe_prefix, (unsigned long) pid);
}

/* Moves all len bytes, or fails once the deadline passes. */
static gboolean
transfer (HANDLE pipe, gboolean writing, guint8 *buffer, gsize len, gint64 deadline, GError **error)
{
	OVERLAPPED overlapped = { 0 };
	gsize moved = 0;
	gboolean ok = TRUE;

	overlapped.hEvent = CreateEventW (NULL, TRUE, FALSE, NULL);
	while (ok && moved < len) {
		DWORD chunk = (DWORD) MIN (len - moved, (gsize) BUFFER_SIZE);
		DWORD n = 0;
		gint64 left;
		BOOL started;

		ResetEvent (overlapped.hEvent);
		started = writing ? WriteFile (pipe, buffer + moved, chunk, NULL, &overlapped)
		                  : ReadFile (pipe, buffer + moved, chunk, NULL, &overlapped);
		if (!started && GetLastError () != ERROR_IO_PENDING) {
			set_win32_error (error, GetLastError (), writing ? "Could not send" : "Could not read");
			ok = FALSE;
			break;
		}
		left = MAX ((deadline - g_get_monotonic_time ()) / 1000, 0);
		if (WaitForSingleObject (overlapped.hEvent, (DWORD) left) != WAIT_OBJECT_0) {
			CancelIoEx (pipe, &overlapped);
			GetOverlappedResult (pipe, &overlapped, &n, TRUE);
			g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT, "The other copy took too long");
			ok = FALSE;
			break;
		}
		if (!GetOverlappedResult (pipe, &overlapped, &n, FALSE)) {
			set_win32_error (error, GetLastError (), writing ? "Could not send" : "Could not read");
			ok = FALSE;
			break;
		}
		if (n == 0) {
			set_win32_error (error, ERROR_BROKEN_PIPE, "Could not read");
			ok = FALSE;
			break;
		}
		moved += n;
	}
	CloseHandle (overlapped.hEvent);

	return ok;
}

static gboolean
send_message (HANDLE pipe, GVariant *message, gint64 deadline, GError **error)
{
	gsize size = g_variant_get_size (message);
	guint64 length = GUINT64_TO_LE ((guint64) size);
	guint8 *buffer;
	gboolean ok;

	if (size > MESSAGE_MAX) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_MESSAGE_TOO_LARGE, "Too much to send at once");
		return FALSE;
	}
	buffer = g_malloc (sizeof length + size);
	memcpy (buffer, &length, sizeof length);
	g_variant_store (message, buffer + sizeof length);
	ok = transfer (pipe, TRUE, buffer, sizeof length + size, deadline, error);
	g_free (buffer);

	return ok;
}

/* Returns: (transfer full): NULL with error set */
static GVariant *
receive_message (HANDLE pipe, const GVariantType *type, gint64 deadline, GError **error)
{
	guint64 length;
	guint8 *data;
	GBytes *bytes;
	GVariant *message;

	if (!transfer (pipe, FALSE, (guint8 *) &length, sizeof length, deadline, error)) {
		return NULL;
	}
	length = GUINT64_FROM_LE (length);
	if (length > MESSAGE_MAX) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_MESSAGE_TOO_LARGE, "Too much at once");
		return NULL;
	}
	data = g_malloc (MAX (length, 1));
	if (length > 0 && !transfer (pipe, FALSE, data, length, deadline, error)) {
		g_free (data);
		return NULL;
	}
	/* Not trusted: GVariant reads defaults out of anything malformed. */
	bytes = g_bytes_new_take (data, length);
	message = g_variant_ref_sink (g_variant_new_from_bytes (type, bytes, FALSE));
	g_bytes_unref (bytes);

	return message;
}

static void
server_clear (gpointer data)
{
	Server *s = data;

	if (s->listening != NULL && s->listening != INVALID_HANDLE_VALUE) {
		CloseHandle (s->listening);
	}
	CloseHandle (s->stop);
	LocalFree (s->sd);
	g_free (s->name);
}

static HANDLE
new_instance (Server *s, gboolean first)
{
	SECURITY_ATTRIBUTES attributes = { sizeof attributes, s->sd, FALSE };

	return CreateNamedPipeW (s->name,
	                         PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED |
	                         (first ? FILE_FLAG_FIRST_PIPE_INSTANCE : 0),
	                         PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
	                         PIPE_UNLIMITED_INSTANCES, BUFFER_SIZE, BUFFER_SIZE, 0, &attributes);
}

static void
call_clear (gpointer data)
{
	Call *call = data;

	g_free (call->interface_name);
	g_free (call->method_name);
	g_variant_unref (call->parameters);
	g_clear_pointer (&call->reply, g_variant_unref);
	g_free (call->error_message);
	g_mutex_clear (&call->lock);
	g_cond_clear (&call->cond);
}

static void
call_release (gpointer data)
{
	g_atomic_rc_box_release_full (data, call_clear);
}

static gboolean
run_call (gpointer data)
{
	Call *call = data;
	GError *error = NULL;
	GVariant *reply;

	reply = call->dispatch (call->interface_name, call->method_name, call->parameters, &error);
	if (reply == NULL) {
		g_debug ("%s.%s from another copy: %s", call->interface_name, call->method_name, error->message);
	}

	g_mutex_lock (&call->lock);
	call->reply = reply;
	call->error_message = error != NULL ? g_strdup (error->message) : NULL;
	call->done = TRUE;
	g_cond_signal (&call->cond);
	g_mutex_unlock (&call->lock);
	g_clear_error (&error);

	return G_SOURCE_REMOVE;
}

/* Returns: (transfer full): the reply message to send */
static GVariant *
answer (Call *call, gboolean wants_reply)
{
	gint64 end = g_get_monotonic_time () + ANSWER_TIMEOUT_MS * G_TIME_SPAN_MILLISECOND;
	GVariant *reply;

	g_main_context_invoke_full (NULL, G_PRIORITY_DEFAULT, run_call,
	                            g_atomic_rc_box_acquire (call), call_release);
	if (!wants_reply) {
		return g_variant_new ("(bsv)", TRUE, "", g_variant_new ("()"));
	}

	g_mutex_lock (&call->lock);
	while (!call->done) {
		if (!g_cond_wait_until (&call->cond, &call->lock, end)) {
			break;
		}
	}
	if (!call->done) {
		reply = g_variant_new ("(bsv)", FALSE, "The other copy did not get to it", g_variant_new ("()"));
	} else if (call->reply == NULL) {
		reply = g_variant_new ("(bsv)", FALSE, call->error_message, g_variant_new ("()"));
	} else {
		reply = g_variant_new ("(bsv)", TRUE, "", call->reply);
	}
	g_mutex_unlock (&call->lock);

	return reply;
}

static void
serve_one (gpointer data, G_GNUC_UNUSED gpointer user_data)
{
	Connection *connection = data;
	GError *error = NULL;
	GVariant *request, *parameters = NULL, *reply = NULL;
	gint64 deadline = g_get_monotonic_time () + READ_TIMEOUT_MS * G_TIME_SPAN_MILLISECOND;
	gboolean wants_reply = FALSE;
	const char *interface_name, *method_name;
	guint8 last;

	request = receive_message (connection->pipe, REQUEST_TYPE, deadline, &error);
	if (request == NULL) {
		g_debug ("A message from another copy: %s", error->message);
		g_clear_error (&error);
		goto out;
	}
	g_variant_get (request, "(&s&sbv)", &interface_name, &method_name, &wants_reply, &parameters);
	if (!g_variant_is_of_type (parameters, G_VARIANT_TYPE_TUPLE)) {
		reply = g_variant_new ("(bsv)", FALSE, "Parameters are not a tuple", g_variant_new ("()"));
	} else {
		Call *call = g_atomic_rc_box_new0 (Call);

		call->dispatch = connection->server->dispatch;
		call->interface_name = g_strdup (interface_name);
		call->method_name = g_strdup (method_name);
		call->parameters = g_variant_ref (parameters);
		g_mutex_init (&call->lock);
		g_cond_init (&call->cond);
		reply = answer (call, wants_reply);
		call_release (call);
	}
	g_variant_ref_sink (reply);

	deadline = g_get_monotonic_time () + CLOSE_TIMEOUT_MS * G_TIME_SPAN_MILLISECOND;
	if (!send_message (connection->pipe, reply, deadline, &error)) {
		g_debug ("An answer to another copy: %s", error->message);
		g_clear_error (&error);
	} else {
		/* Disconnecting would throw away an answer not read yet, so the
		 * client hangs up first. */
		transfer (connection->pipe, FALSE, &last, 1, deadline, NULL);
	}

out:
	g_clear_pointer (&parameters, g_variant_unref);
	g_clear_pointer (&request, g_variant_unref);
	g_clear_pointer (&reply, g_variant_unref);
	CloseHandle (connection->pipe);
	g_atomic_rc_box_release_full (connection->server, server_clear);
	g_free (connection);
}

typedef enum {
	CLIENT_CAME,
	CLIENT_FAILED,
	SERVER_STOPPING
} Waited;

static Waited
wait_for_client (Server *s, HANDLE pipe, OVERLAPPED *overlapped)
{
	HANDLE events[2] = { overlapped->hEvent, s->stop };
	DWORD n;

	ResetEvent (overlapped->hEvent);
	if (ConnectNamedPipe (pipe, overlapped)) {
		return CLIENT_CAME;
	}
	switch (GetLastError ()) {
	case ERROR_PIPE_CONNECTED:
		return CLIENT_CAME;
	case ERROR_IO_PENDING:
		break;
	default:
		return CLIENT_FAILED;
	}
	if (WaitForMultipleObjects (2, events, FALSE, INFINITE) != WAIT_OBJECT_0) {
		CancelIoEx (pipe, overlapped);
		GetOverlappedResult (pipe, overlapped, &n, TRUE);
		return SERVER_STOPPING;
	}
	return GetOverlappedResult (pipe, overlapped, &n, FALSE) ? CLIENT_CAME : CLIENT_FAILED;
}

static gpointer
accept_clients (gpointer data)
{
	Server *s = data;
	OVERLAPPED overlapped = { 0 };

	overlapped.hEvent = CreateEventW (NULL, TRUE, FALSE, NULL);
	for (;;) {
		Waited waited = wait_for_client (s, s->listening, &overlapped);
		Connection *connection;
		HANDLE next;

		if (waited == SERVER_STOPPING) {
			break;
		}
		if (waited == CLIENT_FAILED) {
			DisconnectNamedPipe (s->listening);
			if (WaitForSingleObject (s->stop, 10) == WAIT_OBJECT_0) {
				break;
			}
			continue;
		}

		/* The next one is up before this one can close, or for a moment
		 * there would be no pipe by this name and the copy would drop out
		 * of every list. */
		while ((next = new_instance (s, FALSE)) == INVALID_HANDLE_VALUE) {
			g_warning ("Could not open another pipe for other copies: %lu", GetLastError ());
			if (WaitForSingleObject (s->stop, 100) == WAIT_OBJECT_0) {
				break;
			}
		}
		connection = g_new0 (Connection, 1);
		connection->server = g_atomic_rc_box_acquire (s);
		connection->pipe = s->listening;
		s->listening = next;
		g_thread_pool_push (s->workers, connection, NULL);
		if (next == INVALID_HANDLE_VALUE) {
			break;
		}
	}
	CloseHandle (overlapped.hEvent);

	/* Out of the lists now, not when the last call in hand is done. */
	if (s->listening != INVALID_HANDLE_VALUE) {
		CloseHandle (s->listening);
	}
	s->listening = NULL;
	g_atomic_rc_box_release_full (s, server_clear);

	return NULL;
}

/* The user owns it and only the user gets in, and nothing below this
   integrity level can write to it. */
static gboolean
make_permissions (PSECURITY_DESCRIPTOR *sd)
{
	char *owner = to_utf8 (user_sid_text);
	char *label = label_text != NULL ? to_utf8 (label_text) : NULL;
	char *text;
	wchar_t *sddl;
	gboolean ok;

	text = label != NULL ? g_strdup_printf ("O:%sD:P(A;;GA;;;%s)S:(ML;;NW;;;%s)", owner, owner, label)
	                     : g_strdup_printf ("O:%sD:P(A;;GA;;;%s)", owner, owner);
	sddl = to_utf16 (text);
	ok = ConvertStringSecurityDescriptorToSecurityDescriptorW (sddl, SDDL_REVISION_1, sd, NULL);
	g_free (sddl);
	g_free (text);
	g_free (label);
	g_free (owner);

	return ok;
}

gboolean
nemo_instances_win32_start (NemoInstancesDispatch   dispatch,
                            GError                **error)
{
	Server *s;
	char *name;
	DWORD code;

	g_return_val_if_fail (server == NULL, FALSE);

	if (!know_who_we_are (error)) {
		return FALSE;
	}

	s = g_atomic_rc_box_new0 (Server);
	s->dispatch = dispatch;
	name = pipe_name_of (GetCurrentProcessId ());
	s->name = to_utf16 (name);
	g_free (name);

	if (!make_permissions (&s->sd)) {
		code = GetLastError ();
		g_free (s->name);
		g_atomic_rc_box_release (s);
		set_win32_error (error, code, "Could not make the pipe's permissions");
		return FALSE;
	}

	/* First, so a pipe someone else made under this name first is refused
	 * rather than joined. */
	s->listening = new_instance (s, TRUE);
	if (s->listening == INVALID_HANDLE_VALUE) {
		code = GetLastError ();
		LocalFree (s->sd);
		g_free (s->name);
		g_atomic_rc_box_release (s);
		set_win32_error (error, code, "Could not open a pipe for the other copies");
		return FALSE;
	}
	s->stop = CreateEventW (NULL, TRUE, FALSE, NULL);
	s->workers = g_thread_pool_new (serve_one, NULL, WORKERS, FALSE, NULL);

	server = s;
	accept_thread = g_thread_new ("copies-pipe", accept_clients, g_atomic_rc_box_acquire (s));

	return TRUE;
}

void
nemo_instances_win32_stop (void)
{
	GThreadPool *workers;

	if (server == NULL) {
		return;
	}
	SetEvent (server->stop);
	g_thread_join (accept_thread);
	accept_thread = NULL;

	/* A call still waiting on the main thread is answered as failed, and
	 * the thread goes when it is done. */
	workers = server->workers;
	server->workers = NULL;
	g_thread_pool_free (workers, FALSE, FALSE);
	g_atomic_rc_box_release_full (server, server_clear);
	server = NULL;
}

static gboolean
pipe_exists (const char *name)
{
	wchar_t *wide = to_utf16 (name);
	gboolean exists;

	/* 0 would mean the pipe's default wait. Busy still means it is there. */
	exists = WaitNamedPipeW (wide, 1) || GetLastError () == ERROR_SEM_TIMEOUT;
	g_free (wide);

	return exists;
}

/* Returns: (transfer full): free with g_strfreev */
GStrv
nemo_instances_win32_list_others (void)
{
	GPtrArray *others = g_ptr_array_new ();
	PROCESSENTRY32W entry = { 0 };
	DWORD self = GetCurrentProcessId ();
	DWORD session = 0;
	HANDLE snapshot;

	if (!know_who_we_are (NULL) || !ProcessIdToSessionId (self, &session)) {
		g_ptr_array_add (others, NULL);
		return (GStrv) g_ptr_array_free (others, FALSE);
	}

	snapshot = CreateToolhelp32Snapshot (TH32CS_SNAPPROCESS, 0);
	entry.dwSize = sizeof entry;
	if (snapshot != INVALID_HANDLE_VALUE && Process32FirstW (snapshot, &entry)) {
		do {
			DWORD theirs;
			char *name;

			if (entry.th32ProcessID == self || entry.th32ProcessID == 0 ||
			    !ProcessIdToSessionId (entry.th32ProcessID, &theirs) || theirs != session) {
				continue;
			}
			name = pipe_name_of (entry.th32ProcessID);
			if (pipe_exists (name)) {
				g_ptr_array_add (others, name);
			} else {
				g_free (name);
			}
		} while (Process32NextW (snapshot, &entry));
	}
	if (snapshot != INVALID_HANDLE_VALUE) {
		CloseHandle (snapshot);
	}
	g_ptr_array_add (others, NULL);

	return (GStrv) g_ptr_array_free (others, FALSE);
}

static gboolean
owned_by_user (HANDLE pipe)
{
	PSECURITY_DESCRIPTOR sd = NULL;
	PSID owner = NULL;
	gboolean ours;

	ours = GetSecurityInfo (pipe, SE_KERNEL_OBJECT, OWNER_SECURITY_INFORMATION,
	                        &owner, NULL, NULL, NULL, &sd) == ERROR_SUCCESS &&
	       owner != NULL && EqualSid (owner, user_sid);
	if (sd != NULL) {
		LocalFree (sd);
	}
	return ours;
}

static HANDLE
open_other (const char *other, gint64 deadline, GError **error)
{
	wchar_t *wide = to_utf16 (other);
	HANDLE pipe;

	for (;;) {
		DWORD code;
		gint64 left;

		pipe = CreateFileW (wide, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
		                    FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, NULL);
		if (pipe != INVALID_HANDLE_VALUE) {
			break;
		}
		code = GetLastError ();
		left = (deadline - g_get_monotonic_time ()) / 1000;
		if (code != ERROR_PIPE_BUSY) {
			set_win32_error (error, code, "Could not reach the other copy");
			break;
		}
		if (left <= 0) {
			g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT, "The other copy is busy");
			break;
		}
		WaitNamedPipeW (wide, (DWORD) MAX (left, 1));
	}
	g_free (wide);

	if (pipe != INVALID_HANDLE_VALUE && !owned_by_user (pipe)) {
		CloseHandle (pipe);
		pipe = INVALID_HANDLE_VALUE;
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED, "%s is not this user's", other);
	}
	return pipe;
}

/* Returns: (transfer full): NULL with error set */
GVariant *
nemo_instances_win32_call (const char  *other,
                           const char  *interface_name,
                           const char  *method_name,
                           GVariant    *parameters,
                           gboolean     wants_reply,
                           guint        timeout_ms,
                           GError     **error)
{
	gint64 deadline = g_get_monotonic_time () + (gint64) timeout_ms * G_TIME_SPAN_MILLISECOND;
	GVariant *request, *reply, *value = NULL;
	const char *message;
	gboolean ok;
	HANDLE pipe;

	if (!know_who_we_are (error)) {
		return NULL;
	}
	if (!g_str_has_prefix (other, pipe_prefix)) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "%s is not a copy of this one", other);
		return NULL;
	}
	pipe = open_other (other, deadline, error);
	if (pipe == INVALID_HANDLE_VALUE) {
		return NULL;
	}

	request = g_variant_ref_sink (g_variant_new ("(ssbv)", interface_name, method_name,
	                                             wants_reply, parameters));
	ok = send_message (pipe, request, deadline, error);
	g_variant_unref (request);
	reply = ok ? receive_message (pipe, REPLY_TYPE, deadline, error) : NULL;
	CloseHandle (pipe);
	if (reply == NULL) {
		return NULL;
	}

	g_variant_get (reply, "(b&sv)", &ok, &message, &value);
	if (!ok) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "%s", message);
		g_clear_pointer (&value, g_variant_unref);
	} else if (!g_variant_is_of_type (value, G_VARIANT_TYPE_TUPLE)) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "The other copy answered nonsense");
		g_clear_pointer (&value, g_variant_unref);
	}
	g_variant_unref (reply);

	return value;
}
