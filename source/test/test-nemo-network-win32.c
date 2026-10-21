/* The uri algebra of the Windows network:/// backend. Enumeration itself needs a
 * network - and an unreachable host costs the full timeout - so what is checked
 * here is the part that has to hold with no network at all: how a child uri is
 * built, and that two different server/share pairs cannot share one uri.
 * Windows-only. */

#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <gio/gio.h>

#include <windows.h>

#include <eel/eel-vfs-extensions.h>
#include <libnemo-private/nemo-network-win32.h>
#include "test-check.h"

/* uri of root's child @server, or of that server's child @share when given */
static char *
child_uri (const char *server, const char *share)
{
	GFile *root, *node, *leaf;
	char *uri;

	root = g_file_new_for_uri ("network:///");
	node = g_file_get_child (root, server);

	if (share == NULL) {
		uri = g_file_get_uri (node);
	} else {
		leaf = g_file_get_child (node, share);
		uri = g_file_get_uri (leaf);
		g_object_unref (leaf);
	}

	g_object_unref (node);
	g_object_unref (root);
	return uri;
}

/* What each WNet answer is shown as. This is the half that can actually be
 * wrong, and it used to be: the lookup worded every failure as a missing name,
 * so a refused share said it could not be found. No network needed. */
static void
check_error (gulong rc, const char *server, GIOErrorEnum want_code, const char *want_text)
{
	GError *error = NULL;

	nemo_network_win32_set_error (&error, rc, server);

	check (error != NULL);
	if (error == NULL) {
		return;
	}

	check (error->domain == G_IO_ERROR);
	check (error->code == (gint) want_code);
	check (error->message != NULL && strstr (error->message, want_text) != NULL);

	if (error->code != (gint) want_code || strstr (error->message, want_text) == NULL) {
		g_printerr ("  rc %lu gave [%d] %s\n", rc, error->code, error->message);
	}

	g_clear_error (&error);
}

static void
test_error_wording (void)
{
	check_error (ERROR_ACCESS_DENIED, "FILESRV", G_IO_ERROR_PERMISSION_DENIED, "denied");
	check_error (ERROR_ACCESS_DENIED, NULL, G_IO_ERROR_PERMISSION_DENIED, "denied");
	check_error (ERROR_LOGON_FAILURE, "FILESRV", G_IO_ERROR_PERMISSION_DENIED, "denied");

	check_error (ERROR_NO_NETWORK, NULL, G_IO_ERROR_NOT_FOUND, "unavailable");
	check_error (ERROR_NO_NETWORK, "FILESRV", G_IO_ERROR_NOT_FOUND, "unavailable");

	check_error (ERROR_BAD_NETPATH, "FILESRV", G_IO_ERROR_NOT_FOUND, "FILESRV");
	check_error (ERROR_BAD_NET_NAME, "FILESRV", G_IO_ERROR_NOT_FOUND, "FILESRV");
	check_error (ERROR_REM_NOT_LIST, NULL, G_IO_ERROR_NOT_FOUND, "network");

	/* An rc nobody wrote a case for keeps the system's own words rather than
	 * being reworded into one of the above. */
	check_error (ERROR_NOT_ENOUGH_MEMORY, "FILESRV", G_IO_ERROR_NOT_FOUND, "FILESRV");
}

/* A name that cannot be on any network. The list has to come back refused with
 * something to show, not as an empty folder that loaded fine. */
static void
test_missing_server (void)
{
	GFile *root = g_file_new_for_uri ("network:///");
	GFile *server = g_file_get_child (root, "zz-no-such-host-zz");
	GFileEnumerator *enumerator;
	GFileInfo *info;
	GError *error = NULL;
	gint64 started = g_get_monotonic_time ();

	enumerator = g_file_enumerate_children (server, "standard::*", 0, NULL, &error);
	check (enumerator == NULL);
	check (error != NULL);
	check (error == NULL || error->message != NULL);
	g_print ("  browsing a missing server took %.1fs and said: %s\n",
		 (g_get_monotonic_time () - started) / 1000000.0,
		 error != NULL ? error->message : "nothing");
	g_clear_error (&error);
	g_clear_object (&enumerator);

	/* And the lookup too, or a typed name still opens as a good empty folder. */
	info = g_file_query_info (server, "standard::*", 0, NULL, &error);
	check (info == NULL);
	check (error != NULL);
	g_clear_error (&error);
	g_clear_object (&info);

	g_object_unref (server);
	g_object_unref (root);
}

/* The neighborhood, against what the machine says about itself. With no network
 * the enumeration still succeeds and hands back an empty list, so this is the
 * check that the empty answer is not passed off as a loaded folder. */
static void
test_root_against_the_machine (void)
{
	GFile *root;
	GFileEnumerator *enumerator;
	GFileInfo *info;
	GError *error = NULL;
	int servers = 0;

	/* Walking the whole neighborhood costs the best part of a minute on a
	 * connected box and proves nothing this check exists for. Run it where it
	 * means something: a machine with no network. */
	if (nemo_network_win32_is_available ()) {
		g_print ("  network is up here, so the empty-neighborhood check is skipped\n");
		return;
	}

	root = g_file_new_for_uri ("network:///");
	enumerator = g_file_enumerate_children (root, "standard::*", 0, NULL, &error);

	if (enumerator != NULL) {
		while ((info = g_file_enumerator_next_file (enumerator, NULL, NULL)) != NULL) {
			servers++;
			g_object_unref (info);
		}
		g_file_enumerator_close (enumerator, NULL, NULL);
	}

	/* What must not happen is an empty list handed back with nothing said - a
	 * folder that looks like it loaded fine. Being refused is right, and so is
	 * a list with something in it: a local provider can offer entries with no
	 * network at all, and the remote desktop channel does exactly that. */
	check (enumerator == NULL || servers > 0);

	if (enumerator == NULL) {
		check (error != NULL);
		g_print ("  no network here, and the neighborhood said: %s\n",
			 error != NULL ? error->message : "nothing, which is the bug");
	} else {
		g_print ("  no network here, but %d local entries to list\n", servers);
	}

	g_clear_error (&error);
	g_clear_object (&enumerator);
	g_object_unref (root);
}

/* Browse a server that really is there, if this box is one. Enumeration is the
 * half the uri checks cannot reach, and a share only proves anything if it can
 * be followed - so the target each one points at is opened for real. Skips, out
 * loud, on a box that shares nothing. */
static void
test_live_shares (void)
{
	const char *host = g_get_host_name ();
	GFile *root, *server;
	GFileEnumerator *enumerator;
	GFileInfo *info;
	GError *error = NULL;
	int shares = 0, followed = 0;

	if (host == NULL || *host == '\0') {
		g_printerr ("SKIP live shares: no host name\n");
		return;
	}

	root = g_file_new_for_uri ("network:///");
	server = g_file_get_child (root, host);

	/* The folder view reads this of every file, and GIO logs a critical
	 * when it was never set. */
	info = g_file_query_info (server, "standard::*", 0, NULL, NULL);
	check (info != NULL && g_file_info_has_attribute (info, G_FILE_ATTRIBUTE_STANDARD_IS_SYMLINK));
	g_clear_object (&info);
	info = g_file_query_info (root, "standard::*", 0, NULL, NULL);
	check (info != NULL && g_file_info_has_attribute (info, G_FILE_ATTRIBUTE_STANDARD_IS_SYMLINK));
	g_clear_object (&info);

	enumerator = g_file_enumerate_children (server, "standard::*", 0, NULL, &error);
	if (enumerator == NULL) {
		g_printerr ("SKIP live shares: cannot browse \\\\%s: %s\n",
			    host, error != NULL ? error->message : "?");
		g_clear_error (&error);
		g_object_unref (server);
		g_object_unref (root);
		return;
	}

	while ((info = g_file_enumerator_next_file (enumerator, NULL, NULL)) != NULL) {
		const char *target_uri;

		shares++;

		/* A share is a link to its UNC path - that is what makes opening
		 * one work through the activation nemo already had. */
		check (g_file_info_get_file_type (info) == G_FILE_TYPE_SHORTCUT);
		check (g_file_info_has_attribute (info, G_FILE_ATTRIBUTE_STANDARD_IS_SYMLINK));

		target_uri = g_file_info_get_attribute_string (
			info, G_FILE_ATTRIBUTE_STANDARD_TARGET_URI);
		check (target_uri != NULL);

		if (target_uri != NULL) {
			GFile *target = g_file_new_for_uri (target_uri);
			GFileInfo *reached;
			char *lowered = g_ascii_strdown (target_uri, -1);
			char *wanted = g_ascii_strdown (host, -1);

			/* Every share the enumeration offered has to sit under
			 * the server it was listed beneath, or the uri built for
			 * it names some other machine entirely. The two are not
			 * comparable as files - the share is network:///HOST and
			 * its target is a UNC path - so the host name is what is
			 * checked. */
			check (strstr (lowered, wanted) != NULL);
			g_free (lowered);
			g_free (wanted);

			reached = g_file_query_info (target, "standard::type",
						     0, NULL, NULL);
			if (reached != NULL) {
				followed++;
				g_object_unref (reached);
			} else {
				/* An empty optical drive shares fine and opens
				 * to nothing, so name it rather than counting
				 * it silently against the backend. */
				g_print ("  share %s did not open (%s)\n",
					 g_file_info_get_display_name (info),
					 target_uri);
			}
			g_object_unref (target);
		}

		g_object_unref (info);
	}

	g_file_enumerator_close (enumerator, NULL, NULL);
	g_object_unref (enumerator);

	if (shares == 0) {
		g_printerr ("SKIP live shares: \\\\%s offers none\n", host);
	} else {
		g_print ("  browsed \\\\%s: %d share(s), %d opened\n",
			 host, shares, followed);
		/* At least one has to actually open, or "browsing works" is a
		 * claim about a list nobody can follow. */
		check (followed > 0);
	}

	g_object_unref (server);
	g_object_unref (root);
}

/* Where an smb:// address goes, however it arrives. want is a UNC path for a
 * share or a folder in one, or a network:/// uri for a server or no server.
 * Before, every one of these became a file GLib can't open, and the window
 * said it can't handle "smb" locations. */
static void
check_smb_one (const char *how, GFile *file, const char *address, const char *want)
{
	char *got;

	if (file == NULL) {
		g_printerr ("FAIL %s %s: no file\n", how, address);
		failures++;
		return;
	}

	if (g_str_has_prefix (want, "\\\\")) {
		got = g_file_get_path (file);
		check (g_file_is_native (file));
	} else {
		got = g_file_get_uri (file);
	}

	if (g_strcmp0 (got, want) != 0) {
		g_printerr ("FAIL %s %s: got %s, want %s\n", how, address, got != NULL ? got : "(null)", want);
		failures++;
	}
	g_free (got);
}

static void
check_smb (const char *address, const char *want)
{
	GFile *file;

	file = nemo_network_win32_file_for_smb_uri (address);
	check_smb_one ("mapped", file, address, want);
	g_clear_object (&file);

	file = g_file_parse_name (address);
	check_smb_one ("parsed", file, address, want);
	g_clear_object (&file);

	file = eel_g_file_new_for_user_input (address);
	check_smb_one ("typed", file, address, want);
	g_clear_object (&file);

	/* A bare space isn't a uri, so only the typed forms above take it. GLib
	 * matches a uri's scheme by case, so SMB:// is only caught when typed. */
	if (strchr (address, ' ') == NULL && strncmp (address, "smb:", 4) == 0) {
		file = g_file_new_for_uri (address);
		check_smb_one ("uri", file, address, want);
		g_clear_object (&file);

		file = g_file_new_for_commandline_arg (address);
		check_smb_one ("argument", file, address, want);
		g_clear_object (&file);
	}
}

static void
test_smb_addresses (void)
{
	GFile *file;

	check_smb ("smb://FILESRV/public", "\\\\FILESRV\\public");
	check_smb ("smb://FILESRV/public/", "\\\\FILESRV\\public");
	check_smb ("smb://FILESRV/public/sub/deeper", "\\\\FILESRV\\public\\sub\\deeper");
	check_smb ("smb://FILESRV/public/two%20words", "\\\\FILESRV\\public\\two words");
	check_smb ("smb://FILESRV/public/two words", "\\\\FILESRV\\public\\two words");
	check_smb ("smb://FILESRV/public/100% done", "\\\\FILESRV\\public\\100% done");
	check_smb ("SMB://FILESRV/public", "\\\\FILESRV\\public");
	check_smb ("smb://someone@FILESRV/public", "\\\\FILESRV\\public");
	check_smb ("smb://DOMAIN;someone:secret@FILESRV:445/public", "\\\\FILESRV\\public");
	check_smb ("smb://192.168.1.10/public", "\\\\192.168.1.10\\public");
	check_smb ("smb://[fe80::1%254]/public", "\\\\fe80--1s4.ipv6-literal.net\\public");
	check_smb ("smb://localhost/c$", "\\\\localhost\\c$");

	check_smb ("smb://FILESRV", "network:///FILESRV");
	check_smb ("smb://FILESRV/", "network:///FILESRV");
	check_smb ("smb:///", "network:///");
	check_smb ("smb://", "network:///");

	/* not an smb address: left to GLib */
	file = nemo_network_win32_file_for_smb_uri ("network:///FILESRV");
	check (file == NULL);
	g_clear_object (&file);
	file = nemo_network_win32_file_for_smb_uri ("C:\\smb\\x");
	check (file == NULL);
	g_clear_object (&file);

	/* the UNC form was already taken, and still is */
	file = eel_g_file_new_for_user_input ("\\\\FILESRV\\public");
	check_smb_one ("typed", file, "\\\\FILESRV\\public", "\\\\FILESRV\\public");
	g_clear_object (&file);
}

/* A server and no share, as Explorer takes it: its list of shares. Before, each
 * of these was a native file Windows can't open, and the window said
 * "Input/output error". want NULL means the text names something else. */
static void
check_unc_server (const char *text, const char *want)
{
	GFile *file;
	GFile *server;
	const char *ways[] = { "parsed", "typed", "argument" };
	int i;

	for (i = 0; i < 3; i++) {
		char *uri = NULL;

		file = i == 0 ? g_file_parse_name (text) :
		       i == 1 ? eel_g_file_new_for_user_input (text) :
				g_file_new_for_commandline_arg (text);
		/* the slot's own check, for what arrives as a path */
		server = nemo_network_win32_file_for_unc_server (file);
		if (server != NULL) {
			g_object_unref (file);
			file = server;
		}
		if (!g_file_is_native (file)) {
			uri = g_file_get_uri (file);
		}
		if (g_strcmp0 (uri, want) != 0) {
			g_printerr ("FAIL %s %s: got %s, want %s\n", ways[i], text,
				    uri != NULL ? uri : "(a path)", want != NULL ? want : "(a path)");
			failures++;
		}
		g_free (uri);
		g_object_unref (file);
	}

	/* parsed alone, with no help from the slot */
	file = g_file_parse_name (text);
	if (want != NULL && g_file_is_native (file)) {
		g_printerr ("FAIL parsed alone %s: still a path\n", text);
		failures++;
	}
	g_object_unref (file);
}

static void
test_unc_servers (void)
{
	GFile *file;
	GFile *server;

	check_unc_server ("\\\\FILESRV", "network:///FILESRV");
	check_unc_server ("\\\\FILESRV\\", "network:///FILESRV");
	check_unc_server ("\\\\FILESRV\\\\", "network:///FILESRV");
	check_unc_server ("//FILESRV", "network:///FILESRV");
	check_unc_server ("//FILESRV/", "network:///FILESRV");
	check_unc_server ("\\\\192.168.1.10", "network:///192.168.1.10");

	/* a share, a folder in one, and the device namespaces stay paths */
	check_unc_server ("\\\\FILESRV\\public", NULL);
	check_unc_server ("//FILESRV/public", NULL);
	check_unc_server ("\\\\FILESRV\\public\\sub", NULL);
	check_unc_server ("\\\\?\\UNC\\FILESRV\\public", NULL);
	check_unc_server ("\\\\?\\C:\\Windows", NULL);
	check_unc_server ("\\\\.\\pipe", NULL);
	check_unc_server ("C:\\", NULL);

	/* a bookmark or a link's target */
	file = g_file_new_for_uri ("file:////FILESRV");
	server = nemo_network_win32_file_for_unc_server (file);
	check (server != NULL && g_file_has_uri_scheme (server, "network"));
	g_clear_object (&server);
	g_object_unref (file);

	server = nemo_network_win32_file_for_unc_server (NULL);
	check (server == NULL);
}

int
main (void)
{
	GFile *root, *server;
	char *a, *b, *uri;

	nemo_network_win32_register ();

	uri = child_uri ("FILESRV", NULL);
	check (g_strcmp0 (uri, "network:///FILESRV") == 0);
	g_free (uri);

	/* The separator is the whole point: without it the share ran straight onto
	 * the server name and the result was a server called FILESRVpublic. */
	uri = child_uri ("FILESRV", "public");
	check (g_strcmp0 (uri, "network:///FILESRV/public") == 0);
	if (uri != NULL && strstr (uri, "FILESRVpublic") != NULL) {
		g_printerr ("  built %s\n", uri);
	}
	g_free (uri);

	/* Two different pairs that concatenate to the same string. With no
	 * separator both are network:///SRVAshare and one hides the other. */
	a = child_uri ("SRV", "Ashare");
	b = child_uri ("SRVA", "share");
	check (g_strcmp0 (a, b) != 0);
	g_free (a);
	g_free (b);

	/* The root is the top: nothing above it, and it is the parent of a server. */
	root = g_file_new_for_uri ("network:///");
	check (g_file_get_parent (root) == NULL);

	server = g_file_get_child (root, "FILESRV");
	check (g_file_has_prefix (server, root));
	{
		GFile *back = g_file_get_parent (server);

		check (back != NULL && g_file_equal (back, root));
		g_clear_object (&back);
	}
	g_object_unref (server);
	g_object_unref (root);

	test_smb_addresses ();
	test_unc_servers ();
	test_error_wording ();
	test_missing_server ();
	test_root_against_the_machine ();
	test_live_shares ();

	if (failures == 0) {
		g_print ("network-win32: all checks passed\n");
	}
	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
