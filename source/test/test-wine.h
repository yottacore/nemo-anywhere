/* Whether a test runs under wine, for checks of what wine's own DLLs do
 * differently from Windows. Always FALSE off Windows. */

#ifndef TEST_WINE_H
#define TEST_WINE_H

#include <glib.h>

#ifdef G_OS_WIN32
#include <windows.h>
#endif

static inline gboolean
test_under_wine (void)
{
#ifdef G_OS_WIN32
	HMODULE ntdll = GetModuleHandleW (L"ntdll.dll");

	return ntdll != NULL && GetProcAddress (ntdll, "wine_get_version") != NULL;
#else
	return FALSE;
#endif
}

#endif /* TEST_WINE_H */
