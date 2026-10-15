/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-drop-win32.c - files dropped in from another program on Windows.

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

/* The toolkit's drop target only finds files in the shell's id list, which
 * Explorer puts in its drags and most other programs don't. A program that
 * offers CF_HDROP alone, which is all Windows asks of a file drag, gets the
 * no-drop pointer, or a drop taht does nothing. It also answers move or copy
 * by its own lights, never by what the program allows, so a drag that only
 * allows copy is refused wherever we'd pick move.
 *
 * The toolkit has no way in, so its drop target is wrapped as it registers
 * one: a drag that has CF_HDROP and none of the formats the toolkit reads gets
 * a text/uri-list beside it, and what the program allows is kept for the drop
 * code to read. The toolkit never takes its targets back, so neither do we.
 */

#include <config.h>

#include "nemo-drop-win32.h"

#include <string.h>

#define COBJMACROS
#include <windows.h>
#include <ole2.h>
#include <shlobj.h>
#include <shellapi.h>
#include <gdk/gdkwin32.h>

#define URI_LIST_FORMAT L"text/uri-list"
#define ICON_LIST_FORMAT L"x-special/gnome-icon-list"
#define SHELL_ID_LIST_FORMAT L"Shell IDList Array"

#define ALL_EFFECTS (DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK)

typedef HRESULT (WINAPI *RegisterFunc) (HWND, IDropTarget *);

static RegisterFunc real_register = NULL;
static GHashTable *targets = NULL;	/* HWND -> DropTarget, holding a ref */
static DWORD offered = ALL_EFFECTS;

typedef struct {
	IDataObject iface;
	LONG ref;
	IDataObject *inner;
	FORMATETC uri_fmt;
	HGLOBAL uri_list;
} FileDrop;

typedef struct {
	IDropTarget iface;
	LONG ref;
	IDropTarget *inner;
	IDataObject *entered;	/* what the drag came in with */
	IDataObject *shown;	/* what the toolkit was handed for it */
} DropTarget;

static FORMATETC
hglobal_format (UINT cf)
{
	FORMATETC fmt;

	memset (&fmt, 0, sizeof (fmt));
	fmt.cfFormat = (CLIPFORMAT) cf;
	fmt.dwAspect = DVASPECT_CONTENT;
	fmt.lindex = -1;
	fmt.tymed = TYMED_HGLOBAL;

	return fmt;
}

static gboolean
offers (IDataObject *data, UINT cf)
{
	FORMATETC fmt = hglobal_format (cf);

	return IDataObject_QueryGetData (data, &fmt) == S_OK;
}

/* Zeroed, so whatever the block is rounded up to reads as the end of the
 * list - the toolkit takes the block's whole size as the data's length. */
static HGLOBAL
block_from (gconstpointer data, gsize len)
{
	HGLOBAL block;
	gpointer copy;

	block = GlobalAlloc (GMEM_MOVEABLE | GMEM_ZEROINIT, len);
	if (block == NULL) {
		return NULL;
	}

	copy = GlobalLock (block);
	memcpy (copy, data, len);
	GlobalUnlock (block);

	return block;
}

/* The paths in the drag's CF_HDROP as a uri list, nul included, or NULL if
 * there are none. */
static HGLOBAL
uri_list_from_hdrop (IDataObject *data)
{
	FORMATETC fmt = hglobal_format (CF_HDROP);
	STGMEDIUM medium;
	GString *list;
	HGLOBAL block = NULL;
	UINT count, i;

	memset (&medium, 0, sizeof (medium));
	if (IDataObject_GetData (data, &fmt, &medium) != S_OK) {
		return NULL;
	}
	if (medium.tymed != TYMED_HGLOBAL) {
		ReleaseStgMedium (&medium);
		return NULL;
	}

	list = g_string_new (NULL);
	count = DragQueryFileW ((HDROP) medium.hGlobal, 0xFFFFFFFF, NULL, 0);

	for (i = 0; i < count; i++) {
		UINT len = DragQueryFileW ((HDROP) medium.hGlobal, i, NULL, 0);
		wchar_t *wide = g_new0 (wchar_t, len + 1);
		gchar *path = NULL;
		gchar *uri = NULL;

		if (DragQueryFileW ((HDROP) medium.hGlobal, i, wide, len + 1) > 0) {
			path = g_utf16_to_utf8 ((const gunichar2 *) wide, -1, NULL, NULL, NULL);
		}
		if (path != NULL) {
			uri = g_filename_to_uri (path, NULL, NULL);
		}
		if (uri != NULL) {
			g_string_append (list, uri);
			g_string_append (list, "\r\n");
		}

		g_free (uri);
		g_free (path);
		g_free (wide);
	}

	ReleaseStgMedium (&medium);

	if (list->len > 0) {
		block = block_from (list->str, list->len + 1);
	}
	g_string_free (list, TRUE);

	return block;
}

/* The data object the toolkit sees */

static gboolean
is_uri_list (FileDrop *self, const FORMATETC *want)
{
	return want->cfFormat == self->uri_fmt.cfFormat &&
		want->dwAspect == DVASPECT_CONTENT &&
		(want->tymed & TYMED_HGLOBAL) != 0;
}

static HRESULT STDMETHODCALLTYPE
file_drop_query_interface (IDataObject *iface, REFIID iid, void **out)
{
	if (out == NULL) {
		return E_INVALIDARG;
	}

	if (IsEqualIID (iid, &IID_IUnknown) || IsEqualIID (iid, &IID_IDataObject)) {
		*out = iface;
		IDataObject_AddRef (iface);
		return S_OK;
	}

	*out = NULL;
	return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE
file_drop_add_ref (IDataObject *iface)
{
	return (ULONG) InterlockedIncrement (&((FileDrop *) iface)->ref);
}

static ULONG STDMETHODCALLTYPE
file_drop_release (IDataObject *iface)
{
	FileDrop *self = (FileDrop *) iface;
	LONG left = InterlockedDecrement (&self->ref);

	if (left == 0) {
		IDataObject_Release (self->inner);
		GlobalFree (self->uri_list);
		g_free (self);
	}

	return (ULONG) left;
}

static HRESULT STDMETHODCALLTYPE
file_drop_get_data (IDataObject *iface, FORMATETC *want, STGMEDIUM *out)
{
	FileDrop *self = (FileDrop *) iface;
	gconstpointer from;
	SIZE_T len;

	if (want == NULL || out == NULL) {
		return E_INVALIDARG;
	}
	if (!is_uri_list (self, want)) {
		return IDataObject_GetData (self->inner, want, out);
	}

	memset (out, 0, sizeof (*out));
	out->tymed = TYMED_HGLOBAL;

	len = GlobalSize (self->uri_list);
	from = GlobalLock (self->uri_list);
	out->hGlobal = from != NULL ? block_from (from, len) : NULL;
	GlobalUnlock (self->uri_list);

	return out->hGlobal != NULL ? S_OK : E_OUTOFMEMORY;
}

static HRESULT STDMETHODCALLTYPE
file_drop_get_data_here (IDataObject *iface, FORMATETC *want, STGMEDIUM *out)
{
	return IDataObject_GetDataHere (((FileDrop *) iface)->inner, want, out);
}

static HRESULT STDMETHODCALLTYPE
file_drop_query_get_data (IDataObject *iface, FORMATETC *want)
{
	FileDrop *self = (FileDrop *) iface;

	if (want == NULL) {
		return E_INVALIDARG;
	}

	return is_uri_list (self, want) ? S_OK : IDataObject_QueryGetData (self->inner, want);
}

static HRESULT STDMETHODCALLTYPE
file_drop_get_canonical_format_etc (IDataObject *iface, FORMATETC *in, FORMATETC *out)
{
	return IDataObject_GetCanonicalFormatEtc (((FileDrop *) iface)->inner, in, out);
}

/* The program reads back what was done with its files through here. */
static HRESULT STDMETHODCALLTYPE
file_drop_set_data (IDataObject *iface, FORMATETC *want, STGMEDIUM *medium, BOOL take)
{
	return IDataObject_SetData (((FileDrop *) iface)->inner, want, medium, take);
}

static HRESULT STDMETHODCALLTYPE
file_drop_enum_format_etc (IDataObject *iface, DWORD direction, IEnumFORMATETC **out)
{
	FileDrop *self = (FileDrop *) iface;
	IEnumFORMATETC *theirs = NULL;
	GArray *list;
	FORMATETC fmt;
	ULONG got = 0;
	HRESULT hr;

	if (out == NULL) {
		return E_INVALIDARG;
	}
	if (direction != DATADIR_GET) {
		return IDataObject_EnumFormatEtc (self->inner, direction, out);
	}

	list = g_array_new (FALSE, FALSE, sizeof (FORMATETC));
	g_array_append_val (list, self->uri_fmt);

	if (SUCCEEDED (IDataObject_EnumFormatEtc (self->inner, DATADIR_GET, &theirs)) && theirs != NULL) {
		while (IEnumFORMATETC_Next (theirs, 1, &fmt, &got) == S_OK && got == 1) {
			if (fmt.ptd != NULL) {
				CoTaskMemFree (fmt.ptd);
				fmt.ptd = NULL;
			}
			g_array_append_val (list, fmt);
		}
		IEnumFORMATETC_Release (theirs);
	}

	hr = SHCreateStdEnumFmtEtc (list->len, (FORMATETC *) list->data, out);
	g_array_free (list, TRUE);

	return hr;
}

static HRESULT STDMETHODCALLTYPE
file_drop_advise (IDataObject *iface, FORMATETC *fmt, DWORD flags, IAdviseSink *sink, DWORD *connection)
{
	return IDataObject_DAdvise (((FileDrop *) iface)->inner, fmt, flags, sink, connection);
}

static HRESULT STDMETHODCALLTYPE
file_drop_unadvise (IDataObject *iface, DWORD connection)
{
	return IDataObject_DUnadvise (((FileDrop *) iface)->inner, connection);
}

static HRESULT STDMETHODCALLTYPE
file_drop_enum_advise (IDataObject *iface, IEnumSTATDATA **out)
{
	return IDataObject_EnumDAdvise (((FileDrop *) iface)->inner, out);
}

static IDataObjectVtbl file_drop_vtbl = {
	file_drop_query_interface,
	file_drop_add_ref,
	file_drop_release,
	file_drop_get_data,
	file_drop_get_data_here,
	file_drop_query_get_data,
	file_drop_get_canonical_format_etc,
	file_drop_set_data,
	file_drop_enum_format_etc,
	file_drop_advise,
	file_drop_unadvise,
	file_drop_enum_advise
};

/* @data itself when the toolkit can read it as it is, else @data with a uri
 * list beside its CF_HDROP. A new reference either way. */
static IDataObject *
readable (IDataObject *data)
{
	FileDrop *self;
	HGLOBAL uri_list;
	UINT uri_cf = RegisterClipboardFormatW (URI_LIST_FORMAT);

	if (!offers (data, CF_HDROP) ||
	    offers (data, uri_cf) ||
	    offers (data, RegisterClipboardFormatW (SHELL_ID_LIST_FORMAT)) ||
	    offers (data, RegisterClipboardFormatW (ICON_LIST_FORMAT)) ||
	    (uri_list = uri_list_from_hdrop (data)) == NULL) {
		IDataObject_AddRef (data);
		return data;
	}

	self = g_new0 (FileDrop, 1);
	self->iface.lpVtbl = &file_drop_vtbl;
	self->ref = 1;
	self->inner = data;
	IDataObject_AddRef (data);
	self->uri_fmt = hglobal_format (uri_cf);
	self->uri_list = uri_list;

	return &self->iface;
}

/* The drop target Windows sees */

static void
note_offered (const DWORD *effect)
{
	if (effect != NULL) {
		offered = *effect & ALL_EFFECTS;
	}
}

static void
forget_drag (DropTarget *self)
{
	if (self->entered != NULL) {
		IDataObject_Release (self->entered);
		self->entered = NULL;
	}
	if (self->shown != NULL) {
		IDataObject_Release (self->shown);
		self->shown = NULL;
	}
}

static HRESULT STDMETHODCALLTYPE
target_query_interface (IDropTarget *iface, REFIID iid, void **out)
{
	if (out == NULL) {
		return E_INVALIDARG;
	}

	if (IsEqualIID (iid, &IID_IUnknown) || IsEqualIID (iid, &IID_IDropTarget)) {
		*out = iface;
		IDropTarget_AddRef (iface);
		return S_OK;
	}

	*out = NULL;
	return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE
target_add_ref (IDropTarget *iface)
{
	return (ULONG) InterlockedIncrement (&((DropTarget *) iface)->ref);
}

static ULONG STDMETHODCALLTYPE
target_release (IDropTarget *iface)
{
	DropTarget *self = (DropTarget *) iface;
	LONG left = InterlockedDecrement (&self->ref);

	if (left == 0) {
		forget_drag (self);
		IDropTarget_Release (self->inner);
		g_free (self);
	}

	return (ULONG) left;
}

static HRESULT STDMETHODCALLTYPE
target_drag_enter (IDropTarget *iface, IDataObject *data, DWORD keys, POINTL pt, DWORD *effect)
{
	DropTarget *self = (DropTarget *) iface;

	note_offered (effect);
	forget_drag (self);

	if (data != NULL) {
		self->entered = data;
		IDataObject_AddRef (data);
		self->shown = readable (data);
	}

	return IDropTarget_DragEnter (self->inner, self->shown, keys, pt, effect);
}

static HRESULT STDMETHODCALLTYPE
target_drag_over (IDropTarget *iface, DWORD keys, POINTL pt, DWORD *effect)
{
	note_offered (effect);

	return IDropTarget_DragOver (((DropTarget *) iface)->inner, keys, pt, effect);
}

static HRESULT STDMETHODCALLTYPE
target_drag_leave (IDropTarget *iface)
{
	DropTarget *self = (DropTarget *) iface;
	HRESULT hr;

	hr = IDropTarget_DragLeave (self->inner);
	forget_drag (self);

	return hr;
}

static HRESULT STDMETHODCALLTYPE
target_drop (IDropTarget *iface, IDataObject *data, DWORD keys, POINTL pt, DWORD *effect)
{
	DropTarget *self = (DropTarget *) iface;
	IDataObject *shown = NULL;
	HRESULT hr;

	note_offered (effect);

	if (data != NULL && data == self->entered && self->shown != NULL) {
		shown = self->shown;
		IDataObject_AddRef (shown);
	} else if (data != NULL) {
		shown = readable (data);
	}

	hr = IDropTarget_Drop (self->inner, shown, keys, pt, effect);

	if (shown != NULL) {
		IDataObject_Release (shown);
	}
	forget_drag (self);

	return hr;
}

static IDropTargetVtbl target_vtbl = {
	target_query_interface,
	target_add_ref,
	target_release,
	target_drag_enter,
	target_drag_over,
	target_drag_leave,
	target_drop
};

static void
release_target (gpointer target)
{
	IDropTarget_Release ((IDropTarget *) target);
}

static HRESULT WINAPI
register_drag_drop (HWND hwnd, IDropTarget *inner)
{
	DropTarget *self;
	HRESULT hr;

	if (inner == NULL) {
		return real_register (hwnd, inner);
	}

	self = g_new0 (DropTarget, 1);
	self->iface.lpVtbl = &target_vtbl;
	self->ref = 1;
	self->inner = inner;
	IDropTarget_AddRef (inner);

	hr = real_register (hwnd, &self->iface);
	g_debug ("drop target for %p: 0x%lx", (void *) hwnd, (unsigned long) hr);
	if (SUCCEEDED (hr)) {
		g_hash_table_replace (targets, hwnd, self);
	} else {
		IDropTarget_Release (&self->iface);
	}

	return hr;
}

/* Points @module's import of @real at @hook. Matched by address rather than
 * by name, so it doesn't matter which dll name the import went through. */
static gboolean
patch_import (HMODULE module, FARPROC real, FARPROC hook)
{
	const BYTE *base = (const BYTE *) module;
	const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *) base;
	const IMAGE_NT_HEADERS *nt;
	const IMAGE_DATA_DIRECTORY *dir;
	const IMAGE_IMPORT_DESCRIPTOR *imp;

	if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
		return FALSE;
	}
	nt = (const IMAGE_NT_HEADERS *) (base + dos->e_lfanew);
	if (nt->Signature != IMAGE_NT_SIGNATURE) {
		return FALSE;
	}
	dir = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
	if (dir->VirtualAddress == 0) {
		return FALSE;
	}

	for (imp = (const IMAGE_IMPORT_DESCRIPTOR *) (base + dir->VirtualAddress); imp->Name != 0; imp++) {
		IMAGE_THUNK_DATA *slot = (IMAGE_THUNK_DATA *) (base + imp->FirstThunk);

		for (; slot->u1.Function != 0; slot++) {
			DWORD was;

			if (slot->u1.Function != (ULONG_PTR) real) {
				continue;
			}
			if (!VirtualProtect (&slot->u1.Function, sizeof (slot->u1.Function), PAGE_READWRITE, &was)) {
				return FALSE;
			}
			slot->u1.Function = (ULONG_PTR) hook;
			VirtualProtect (&slot->u1.Function, sizeof (slot->u1.Function), was, &was);
			return TRUE;
		}
	}

	return FALSE;
}

gboolean
nemo_drop_win32_prepare (void)
{
	HMODULE gdk = GetModuleHandleW (L"libgdk-3-0.dll");
	HMODULE ole = GetModuleHandleW (L"ole32.dll");
	FARPROC real;

	if (real_register != NULL) {
		return TRUE;
	}
	if (gdk == NULL || ole == NULL) {
		g_debug ("toolkit or OLE not loaded");
		return FALSE;
	}

	real = GetProcAddress (ole, "RegisterDragDrop");
	if (real == NULL) {
		return FALSE;
	}

	targets = g_hash_table_new_full (NULL, NULL, NULL, release_target);
	real_register = (RegisterFunc) (void (*) (void)) real;

	if (!patch_import (gdk, real, (FARPROC) (void (*) (void)) register_drag_drop)) {
		g_debug ("no import of RegisterDragDrop to point at ours");
		g_hash_table_destroy (targets);
		targets = NULL;
		real_register = NULL;
		return FALSE;
	}

	return TRUE;
}

GdkDragAction
nemo_drop_win32_offered_actions (void)
{
	GdkDragAction actions = 0;

	if (offered & DROPEFFECT_COPY) {
		actions |= GDK_ACTION_COPY;
	}
	if (offered & DROPEFFECT_MOVE) {
		actions |= GDK_ACTION_MOVE;
	}
	if (offered & DROPEFFECT_LINK) {
		actions |= GDK_ACTION_LINK;
	}

	return actions;
}

/* Returns: (transfer none) */
gpointer
nemo_drop_win32_target (GdkWindow *toplevel)
{
	if (targets == NULL || toplevel == NULL) {
		return NULL;
	}

	return g_hash_table_lookup (targets, GDK_WINDOW_HWND (toplevel));
}
