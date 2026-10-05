/* --version and --about print and leave, so they must work with no display at
 * all. They used to fail with "Cannot open display" before the flag was read.
 * Both lead with "nemo-anywhere v<version> build <build number>", and the build
 * number is worked out by build-number.py, which is run here on fixed times.
 * The option parse is checked here too, since it runs before any window.
 * Takes the path to nemo-anywhere, the project version, and optionally the
 * path to build-number.py and a python to run it with. */

#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <glib.h>
#include "test-check.h"

/* Crockford base32, lower case: no i, l, o or u. */
static const char build_digits[] = "0123456789abcdefghjkmnpqrstvwxyz";

#define EPOCH_2000 946684800

/* Minutes since 2000 the build number stands for, or -1 when it is not one. */
static gint64
decode_build (const char *text)
{
	gint64 minutes = 0;
	const char *p;

	if (*text == '\0' || strlen (text) > 12) {
		return -1;
	}
	for (p = text; *p != '\0'; p++) {
		const char *digit = strchr (build_digits, *p);

		if (digit == NULL) {
			return -1;
		}
		minutes = minutes * 32 + (digit - build_digits);
	}

	return minutes;
}

/* The first line is "nemo-anywhere v<version> build <number>". The number moves
   with every commit, so only its shape and the time it names are checked. */
static void
check_version_line (const char *out, const char *version)
{
	char *line = g_strndup (out, strcspn (out, "\r\n"));
	char *lead = g_strdup_printf ("nemo-anywhere v%s build ", version);
	gint64 minutes;

	check (g_str_has_prefix (line, lead));
	if (g_str_has_prefix (line, lead)) {
		minutes = decode_build (line + strlen (lead));
		check (minutes >= 0);

		/* Stamped from a commit or the clock, so it is no older than the
		   fork and no later than now. */
		check (minutes >= (1767225600 - EPOCH_2000) / 60);
		check (minutes <= ((gint64) time (NULL) - EPOCH_2000) / 60 + 1);
	} else {
		g_printerr ("version line: %s\n", line);
	}

	g_free (lead);
	g_free (line);
}

static char *
build_number_at (const char *python, const char *script, gint64 when)
{
	const char *argv[] = { python, script, NULL };
	char **envp;
	char *stamp, *out = NULL;
	int status = -1;

	stamp = g_strdup_printf ("%" G_GINT64_FORMAT, when);
	envp = g_environ_setenv (g_get_environ (), "SOURCE_DATE_EPOCH", stamp, TRUE);

	if (!g_spawn_sync (NULL, (char **) argv, envp, G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL,
			   &out, NULL, &status, NULL) ||
	    !g_spawn_check_wait_status (status, NULL)) {
		g_clear_pointer (&out, g_free);
	}

	g_strfreev (envp);
	g_free (stamp);

	return out;
}

static void
check_build_number (const char *python, const char *script, gint64 when, const char *expect)
{
	char *got = build_number_at (python, script, when);

	if (g_strcmp0 (got, expect) != 0) {
		g_printerr ("FAIL build-number.py at %" G_GINT64_FORMAT ": got \"%s\", wanted \"%s\"\n",
			    when, got != NULL ? got : "(nothing)", expect);
		failures++;
	}
	g_free (got);
}

static void
check_prints (const char *program, const char *flag, const char *version)
{
	const char *argv[] = { program, flag, NULL };
	char **envp;
	char *out = NULL, *err = NULL;
	int status = -1;
	GError *error = NULL;

	envp = g_get_environ ();
	envp = g_environ_unsetenv (envp, "DISPLAY");
	envp = g_environ_unsetenv (envp, "WAYLAND_DISPLAY");

	check (g_spawn_sync (NULL, (char **) argv, envp, G_SPAWN_DEFAULT, NULL, NULL,
			     &out, &err, &status, &error));
	if (error != NULL) {
		g_printerr ("%s: %s\n", flag, error->message);
		g_clear_error (&error);
	} else {
		check (g_spawn_check_wait_status (status, NULL));
		check (strstr (out, "nemo-anywhere ") != NULL);
		check (strstr (out, "Copyright ") != NULL);

		/* --about opens with a blank line, then the same line --version prints. */
		check_version_line (out + strspn (out, "\r\n"), version);
		if (strcmp (flag, "--about") == 0) {
			check (strstr (out, "https://github.com/yottacore/nemo-anywhere") != NULL);
			check (strstr (out, "GPL-2.0-only") != NULL);
			check (strstr (out, "version 2 only") != NULL);
			check (strstr (out, "later version") == NULL);
		}
		if (err != NULL && *err != '\0') {
			g_printerr ("%s wrote to stderr: %s", flag, err);
		}
		check (err == NULL || strstr (err, "display") == NULL);
	}

	g_free (out);
	g_free (err);
	g_strfreev (envp);
}

/* An option that takes a value is read with it, and one that is not known
   stops the run. --version keeps the display closed in both. */
static void
check_parse (const char *program, const char *flag, gboolean want_ok)
{
	const char *argv[] = { program, flag, "--version", NULL };
	char *out = NULL, *err = NULL;
	int status = -1;
	int before = failures;
	GError *error = NULL;

	check (g_spawn_sync (NULL, (char **) argv, NULL, G_SPAWN_DEFAULT, NULL, NULL,
			     &out, &err, &status, &error));
	if (error != NULL) {
		g_printerr ("%s: %s\n", flag, error->message);
		g_clear_error (&error);
	} else if (want_ok) {
		check (g_spawn_check_wait_status (status, NULL));
		check (strstr (out, "nemo-anywhere v") != NULL);
	} else {
		check (!g_spawn_check_wait_status (status, NULL));
		check (strstr (out, "nemo-anywhere v") == NULL);
		check (strstr (err, "Could not parse arguments") != NULL);
	}
	if (failures > before) {
		g_printerr ("%s: out [%s] err [%s]\n", flag, out, err);
	}

	g_free (out);
	g_free (err);
}

int
main (int argc, char *argv[])
{
	if (argc < 3) {
		g_printerr ("usage: %s <path to nemo-anywhere> <version> [<build-number.py> <python>]\n", argv[0]);
		return 77;
	}

	check_prints (argv[1], "--version", argv[2]);
	check_prints (argv[1], "--about", argv[2]);
	check_parse (argv[1], "--geometry=600x400+10+10", TRUE);
	check_parse (argv[1], "--no-such-option", FALSE);

	if (argc < 5) {
		goto done;
	}

	/* Minutes since 2000, rounded to the nearest, in the digits above. */
	check_build_number (argv[4], argv[3], EPOCH_2000, "0");
	check_build_number (argv[4], argv[3], EPOCH_2000 + 18 * 60, "j");
	check_build_number (argv[4], argv[3], EPOCH_2000 + 27 * 60, "v");
	check_build_number (argv[4], argv[3], EPOCH_2000 + 32 * 60 * 32 * 32 * 32 - 31, "zzzz");
	check_build_number (argv[4], argv[3], EPOCH_2000 + 32 * 60 * 32 * 32 * 32 - 30, "10000");
	check_build_number (argv[4], argv[3], EPOCH_2000 + 32 * 60 * 32 * 32 * 32, "10000");
	/* 2026-08-27 00:00 UTC, the day the number was first shown. */
	check_build_number (argv[4], argv[3], 1787788800, "dbsv0");

done:
	if (failures > 0) {
		g_printerr ("%d failure(s)\n", failures);
		return EXIT_FAILURE;
	}

	g_print ("OK\n");
	return EXIT_SUCCESS;
}
