/* What shcl-old-writer wrote, built with SHCL_OLD_STAMPED against the SHCL
 * header the app had from 20260925 to 20261105, before 3.0.0-beta1 (dev at
 * 00fbb3e, then f2a8ad2; both write these files byte for byte alike). Those
 * builds stamped the file format 3 but wrote a list bare. The header is in
 * git as bb8be972:vendor/shcl/shcl.h, and isn't kept in vendor/shcl-old,
 * since its 10,000 lines would cost every analyzer lane a slow file for 3
 * fixed files. Same arguments as shcl-old-writer.
 *
 * Those builds read a backslash in double quotes as an escape, as for the
 * tab in "rar\ta", where today's rules read it as text. The app reads such
 * a value as they did when the file has a bare list, as plain and hand do.
 * 2 hand lines are marked as read another way today, so the check fails
 * once they read as that build read them: `a\,b`, which it took for a list
 * and today's rules for text, and 'it''s'.
 *
 *   shcl-dev-writer <settings.shcl> <expect.txt> plain|hand|nobackslash */

#include <stdio.h>
#include <string.h>

typedef struct {
	const char *variant;
	const char *settings;
	const char *expect;
} Variant;

static const Variant variants[] = {
	{ "plain",
	  "appearance:\n"
	  "\t# Theme, with a # and a \\ in the note\n"
	  "\tgtk-theme: Adwaita-dark\n"
	  "\ticon-theme: \"caf\303\251 icons\"\n"
	  "\tmode: dark\n"
	  "\n"
	  "terminal:\n"
	  "\texec: \\\\server\\share\\term.exe\n"
	  "\t# Argument that terminal takes before a command\n"
	  "\texec-arg: '--title=\"a # b\"'\n"
	  "\n"
	  "list-view:\n"
	  "\trow-shading-color: \"rgba(0, 0, 0, 0.1)\"\n"
	  "\n"
	  "preferences:\n"
	  "\tbulk-rename-tool: 'C:\\Program Files\\Renamer\\r.exe --flag'\n"
	  "\tclick-policy: single\n"
	  "\tshow-hidden-files: true\n"
	  "\tmenu-config:\n"
	  "\t\tbackground-menu-compress: false\n"
	  "\tthumbnail-threads: -3\n"
	  "\tthumbnail-limit: 1000000\n"
	  "\n"
	  "archive:\n"
	  "\tcreate-with-7z: tools\\7z.exe\n"
	  "\tcreate-with-rar: \"rar\\ta\"\n"
	  "\textract-with-7z: \"\"\n"
	  "\textract-with-rar: \" lead and trail \"\n"
	  "\n"
	  "windows:\n"
	  "\tpath-separator: slash\n"
	  "\tassociations: \"txt=notepad.exe %1\", 'log=C:\\Tools\\view.exe \"%1\"', \"md=typora, with comma\"\n"
	  "\tterminal-candidates: \"say \\\"it's\\\" C:\\\\x\", wt.exe\n"
	  "\n"
	  "search:\n"
	  "\tsearch-skip-folders: node_modules, \"a, b\", \"\", \"x#y\", \"'q'\"\n"
	  "\tsearch-sort-column: \"it's \\\"x # y\\\" C:\\\\a\"\n"
	  "\n"
	  "plugins:\n"
	  "\tdisabled-extensions:\n"
	  "\n"
	  "thumbnailers:\n"
	  "\tdisable: image/x-one\n"
	  "\n"
	  "file-cache:\n"
	  "\tmax-size-gib: 2.5\n"
	  "\tmemory-gib: 0.125\n"
	  "\n"
	  "window-state:\n"
	  "\tgeometry: \"line 1\\nline 2\"\n"
	  "\n"
	  "##\n"
	  "## This config file format is SHCL.\n"
	  "## \"Simple Hierarchical Config Language\"\n"
	  "##    Format   3\n"
	  "##    Home     https://github.com/yottacore/shcl\n"
	  "##    Syntax   https://github.com/yottacore/shcl/blob/v3.0.0-beta1/project/spec.md\n"
	  "##    Legal    SHCL is Copyright \302\251 2026 Jim Collier [ID: 2\341\203\243N\341\203\220\311\230\302\253\322\202\325\251\310\271\311\244\316\276\317\200\340\271\231\302\277\325\261\317\226]. License: MIT. No warranty.\n"
	  "##\n",
	  "appearance.gtk-theme\ts\t0\t416477616974612d6461726b\n"
	  "appearance.icon-theme\ts\t0\t636166c3a92069636f6e73\n"
	  "terminal.exec\ts\t0\t5c5c7365727665725c73686172655c7465726d2e657865\n"
	  "terminal.exec-arg\ts\t0\t2d2d7469746c653d22612023206222\n"
	  "list-view.row-shading-color\ts\t0\t7267626128302c20302c20302c20302e3129\n"
	  "preferences.bulk-rename-tool\ts\t0\t433a5c50726f6772616d2046696c65735c52656e616d65725c722e657865202d2d666c6167\n"
	  "archive.create-with-7z\ts\t0\t746f6f6c735c377a2e657865\n"
	  "archive.create-with-rar\ts\t0\t7261720961\n"
	  "archive.extract-with-7z\ts\t0\t\n"
	  "archive.extract-with-rar\ts\t0\t206c65616420616e6420747261696c20\n"
	  "appearance.mode\ts\t0\t6461726b\n"
	  "preferences.click-policy\ts\t0\t73696e676c65\n"
	  "windows.path-separator\ts\t0\t736c617368\n"
	  "windows.associations\tl\t0\t3:7478743d6e6f74657061642e657865202531,6c6f673d433a5c546f6f6c735c766965772e6578652022253122,6d643d7479706f72612c207769746820636f6d6d61\n"
	  "search.search-skip-folders\tl\t0\t5:6e6f64655f6d6f64756c6573,612c2062,,782379,277127\n"
	  "plugins.disabled-extensions\tl\t0\t0:\n"
	  "thumbnailers.disable\tl\t0\t1:696d6167652f782d6f6e65\n"
	  "preferences.show-hidden-files\tb\t0\t1\n"
	  "preferences.menu-config.background-menu-compress\tb\t0\t0\n"
	  "preferences.thumbnail-threads\ti\t0\t-3\n"
	  "preferences.thumbnail-limit\ti\t0\t1000000\n"
	  "file-cache.max-size-gib\tf\t0\t2.5\n"
	  "file-cache.memory-gib\tf\t0\t0.125\n"
	  "window-state.geometry\ts\t0\t6c696e6520310a6c696e652032\n"
	  "search.search-sort-column\ts\t0\t69742773202278202320792220433a5c61\n"
	  "windows.terminal-candidates\tl\t0\t2:7361792022697427732220433a5c78,77742e657865\n" },
	{ "hand",
	  "appearance:\n"
	  "\t# Theme, with a # and a \\ in the note\n"
	  "\tgtk-theme: Adwaita-dark\n"
	  "\ticon-theme: \"caf\303\251 icons\"\n"
	  "\tmode: dark\n"
	  "\n"
	  "terminal:\n"
	  "\texec: \\\\server\\share\\term.exe\n"
	  "\t# Argument that terminal takes before a command\n"
	  "\texec-arg: '--title=\"a # b\"'\n"
	  "\n"
	  "list-view:\n"
	  "\trow-shading-color: \"rgba(0, 0, 0, 0.1)\"\n"
	  "\n"
	  "preferences:\n"
	  "\tbulk-rename-tool: 'C:\\Program Files\\Renamer\\r.exe --flag'\n"
	  "\tclick-policy: single\n"
	  "\tshow-hidden-files: true\n"
	  "\tmenu-config:\n"
	  "\t\tbackground-menu-compress: false\n"
	  "\tthumbnail-threads: -3\n"
	  "\tthumbnail-limit: 1000000\n"
	  "\n"
	  "archive:\n"
	  "\tcreate-with-7z: tools\\7z.exe\n"
	  "\tcreate-with-rar: \"rar\\ta\"\n"
	  "\textract-with-7z: \"\"\n"
	  "\textract-with-rar: \" lead and trail \"\n"
	  "\n"
	  "windows:\n"
	  "\tpath-separator: slash\n"
	  "\tassociations: \"txt=notepad.exe %1\", 'log=C:\\Tools\\view.exe \"%1\"', \"md=typora, with comma\"\n"
	  "\tterminal-candidates: \"say \\\"it's\\\" C:\\\\x\", wt.exe\n"
	  "\n"
	  "search:\n"
	  "\tsearch-skip-folders: node_modules, \"a, b\", \"\", \"x#y\", \"'q'\"\n"
	  "\tsearch-sort-column: \"it's \\\"x # y\\\" C:\\\\a\"\n"
	  "\n"
	  "plugins:\n"
	  "\tdisabled-extensions:\n"
	  "\n"
	  "thumbnailers:\n"
	  "\tdisable: image/x-one\n"
	  "\n"
	  "file-cache:\n"
	  "\tmax-size-gib: 2.5\n"
	  "\tmemory-gib: 0.125\n"
	  "\n"
	  "window-state:\n"
	  "\tgeometry: \"line 1\\nline 2\"\n"
	  "search.search-regex-format: a\\,b\n"
	  "list-view.row-hover-color: 'it''s'\n"
	  "preferences.image-viewers-with-external-sort: a, b\\, c\n"
	  "\n"
	  "##\n"
	  "## This config file format is SHCL.\n"
	  "## \"Simple Hierarchical Config Language\"\n"
	  "##    Format   3\n"
	  "##    Home     https://github.com/yottacore/shcl\n"
	  "##    Syntax   https://github.com/yottacore/shcl/blob/v3.0.0-beta1/project/spec.md\n"
	  "##    Legal    SHCL is Copyright \302\251 2026 Jim Collier [ID: 2\341\203\243N\341\203\220\311\230\302\253\322\202\325\251\310\271\311\244\316\276\317\200\340\271\231\302\277\325\261\317\226]. License: MIT. No warranty.\n"
	  "##\n",
	  "appearance.gtk-theme\ts\t0\t416477616974612d6461726b\n"
	  "appearance.icon-theme\ts\t0\t636166c3a92069636f6e73\n"
	  "terminal.exec\ts\t0\t5c5c7365727665725c73686172655c7465726d2e657865\n"
	  "terminal.exec-arg\ts\t0\t2d2d7469746c653d22612023206222\n"
	  "list-view.row-shading-color\ts\t0\t7267626128302c20302c20302c20302e3129\n"
	  "preferences.bulk-rename-tool\ts\t0\t433a5c50726f6772616d2046696c65735c52656e616d65725c722e657865202d2d666c6167\n"
	  "archive.create-with-7z\ts\t0\t746f6f6c735c377a2e657865\n"
	  "archive.create-with-rar\ts\t0\t7261720961\n"
	  "archive.extract-with-7z\ts\t0\t\n"
	  "archive.extract-with-rar\ts\t0\t206c65616420616e6420747261696c20\n"
	  "appearance.mode\ts\t0\t6461726b\n"
	  "preferences.click-policy\ts\t0\t73696e676c65\n"
	  "windows.path-separator\ts\t0\t736c617368\n"
	  "windows.associations\tl\t0\t3:7478743d6e6f74657061642e657865202531,6c6f673d433a5c546f6f6c735c766965772e6578652022253122,6d643d7479706f72612c207769746820636f6d6d61\n"
	  "search.search-skip-folders\tl\t0\t5:6e6f64655f6d6f64756c6573,612c2062,,782379,277127\n"
	  "plugins.disabled-extensions\tl\t0\t0:\n"
	  "thumbnailers.disable\tl\t0\t1:696d6167652f782d6f6e65\n"
	  "preferences.show-hidden-files\tb\t0\t1\n"
	  "preferences.menu-config.background-menu-compress\tb\t0\t0\n"
	  "preferences.thumbnail-threads\ti\t0\t-3\n"
	  "preferences.thumbnail-limit\ti\t0\t1000000\n"
	  "file-cache.max-size-gib\tf\t0\t2.5\n"
	  "file-cache.memory-gib\tf\t0\t0.125\n"
	  "window-state.geometry\ts\t0\t6c696e6520310a6c696e652032\n"
	  "search.search-sort-column\ts\t0\t69742773202278202320792220433a5c61\n"
	  "windows.terminal-candidates\tl\t0\t2:7361792022697427732220433a5c78,77742e657865\n"
	  "search.search-regex-format\ts\t1\t615c2c2062\n"
	  "list-view.row-hover-color\ts\t1\t27697427277327\n"
	  "preferences.image-viewers-with-external-sort\tl\t0\t3:61,625c,63\n" },
	{ "nobackslash",
	  "appearance:\n"
	  "\t# Theme, with a # and a \\ in the note\n"
	  "\tgtk-theme: Adwaita-dark\n"
	  "\ticon-theme: \"caf\303\251 icons\"\n"
	  "\tmode: dark\n"
	  "\n"
	  "terminal:\n"
	  "\t# Argument that terminal takes before a command\n"
	  "\texec-arg: '--title=\"a # b\"'\n"
	  "\n"
	  "list-view:\n"
	  "\trow-shading-color: \"rgba(0, 0, 0, 0.1)\"\n"
	  "\n"
	  "archive:\n"
	  "\textract-with-7z: \"\"\n"
	  "\textract-with-rar: \" lead and trail \"\n"
	  "\n"
	  "preferences:\n"
	  "\tclick-policy: single\n"
	  "\tshow-hidden-files: true\n"
	  "\tmenu-config:\n"
	  "\t\tbackground-menu-compress: false\n"
	  "\tthumbnail-threads: -3\n"
	  "\tthumbnail-limit: 1000000\n"
	  "\n"
	  "windows:\n"
	  "\tpath-separator: slash\n"
	  "\n"
	  "plugins:\n"
	  "\tdisabled-extensions:\n"
	  "\n"
	  "thumbnailers:\n"
	  "\tdisable: image/x-one\n"
	  "\n"
	  "file-cache:\n"
	  "\tmax-size-gib: 2.5\n"
	  "\tmemory-gib: 0.125\n"
	  "\n"
	  "##\n"
	  "## This config file format is SHCL.\n"
	  "## \"Simple Hierarchical Config Language\"\n"
	  "##    Format   3\n"
	  "##    Home     https://github.com/yottacore/shcl\n"
	  "##    Syntax   https://github.com/yottacore/shcl/blob/v3.0.0-beta1/project/spec.md\n"
	  "##    Legal    SHCL is Copyright \302\251 2026 Jim Collier [ID: 2\341\203\243N\341\203\220\311\230\302\253\322\202\325\251\310\271\311\244\316\276\317\200\340\271\231\302\277\325\261\317\226]. License: MIT. No warranty.\n"
	  "##\n",
	  "appearance.gtk-theme\ts\t0\t416477616974612d6461726b\n"
	  "appearance.icon-theme\ts\t0\t636166c3a92069636f6e73\n"
	  "terminal.exec-arg\ts\t0\t2d2d7469746c653d22612023206222\n"
	  "list-view.row-shading-color\ts\t0\t7267626128302c20302c20302c20302e3129\n"
	  "archive.extract-with-7z\ts\t0\t\n"
	  "archive.extract-with-rar\ts\t0\t206c65616420616e6420747261696c20\n"
	  "appearance.mode\ts\t0\t6461726b\n"
	  "preferences.click-policy\ts\t0\t73696e676c65\n"
	  "windows.path-separator\ts\t0\t736c617368\n"
	  "plugins.disabled-extensions\tl\t0\t0:\n"
	  "thumbnailers.disable\tl\t0\t1:696d6167652f782d6f6e65\n"
	  "preferences.show-hidden-files\tb\t0\t1\n"
	  "preferences.menu-config.background-menu-compress\tb\t0\t0\n"
	  "preferences.thumbnail-threads\ti\t0\t-3\n"
	  "preferences.thumbnail-limit\ti\t0\t1000000\n"
	  "file-cache.max-size-gib\tf\t0\t2.5\n"
	  "file-cache.memory-gib\tf\t0\t0.125\n" },
};

static int
put (const char *path, const char *text)
{
	FILE  *out = fopen (path, "wb");
	size_t n = strlen (text);
	int    ok;

	if (out == NULL)
		return 0;
	ok = fwrite (text, 1, n, out) == n;
	return fclose (out) == 0 && ok;
}

int
main (int argc, char *argv[])
{
	size_t i;

	for (i = 0; argc == 4 && i < sizeof variants / sizeof variants[0]; i++) {
		if (strcmp (argv[3], variants[i].variant) != 0)
			continue;
		if (!put (argv[1], variants[i].settings) || !put (argv[2], variants[i].expect)) {
			fprintf (stderr, "%s: cannot write %s or %s\n", argv[0], argv[1], argv[2]);
			return 1;
		}
		return 0;
	}
	fprintf (stderr, "usage: %s <settings.shcl> <expect.txt> plain|hand|nobackslash\n", argv[0]);
	return 2;
}
