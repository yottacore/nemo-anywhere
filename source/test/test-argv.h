/* Reading a built command line, for the archive and extract tests. Both build
 * an argv from a command template and then ask what came out of it. */

#ifndef TEST_ARGV_H
#define TEST_ARGV_H

#include <string.h>
#include <glib.h>

/* A placeholder that survived means a token name that doesn't match the one
   the default command line was written with - which no switch check would
   notice on its own, since the argument is still there. */
static gboolean
has_unexpanded (char **argv)
{
	int i;

	for (i = 0; argv != NULL && argv[i] != NULL; i++) {
		if (strstr (argv[i], "{{") != NULL) {
			return TRUE;
		}
	}

	return FALSE;
}

/* Does argv have this exact switch? */
static gboolean
has_arg (char       **argv,
	 const char  *wanted)
{
	int i;

	for (i = 0; argv != NULL && argv[i] != NULL; i++) {
		if (g_strcmp0 (argv[i], wanted) == 0) {
			return TRUE;
		}
	}

	return FALSE;
}

static gboolean
has_prefix_arg (char       **argv,
		const char  *prefix)
{
	int i;

	for (i = 0; argv != NULL && argv[i] != NULL; i++) {
		if (g_str_has_prefix (argv[i], prefix)) {
			return TRUE;
		}
	}

	return FALSE;
}

#endif /* TEST_ARGV_H */
