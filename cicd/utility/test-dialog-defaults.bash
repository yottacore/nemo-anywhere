#!/usr/bin/env bash

##	- Purpose: Every dialog the app makes says which button Enter takes. Each
##	  place in source/ that makes one, in C or in Python, has to set a default
##	  response in the same function, with gtk_dialog_set_default_response or
##	  eel_dialog_set_default. df05h opens the dialogs that can be made without
##	  the app and checks the button and its focus; this holds the rest.
##	- A file chooser is left out: GTK makes its accept button the default when
##	  it maps, and the focus belongs in the file list.
##	- Also runs the checker over made-up sources, so one that passes everything
##	  is caught.
##	- Runs in the lint stage.
##	- Syntax: cicd/utility/test-dialog-defaults.bash
##	- Test ID: df05p

##	Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT

set -Eeuo pipefail
echo "[ Test $(sed -n 's/^##[[:space:]]*\(- \)\{0,1\}Test ID: //p' "${BASH_SOURCE[0]}") ${BASH_SOURCE[0]##*/} ]"

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "${here}/../.." && pwd)"
# shellcheck source=include/run-temp.bash
source "${here}/include/run-temp.bash"

fEcho(){ echo "[ $* ]"; }
failures=0
fFail(){ fEcho "FAILED: $*"; failures=$((failures + 1)); }

## Prints "file:line" for each site with no default in its function. A C
## function body runs from a "{" to a "}" in column 0; a Python one from a def
## to the next def or class. Comments are dropped first, so a name in one
## isn't a site.
fSites(){
	awk '
		function flush(   i) {
			if (!has_default) {
				for (i = 1; i <= n; i++) print sites[i]
			}
			n = 0; has_default = 0
		}
		FNR == 1 {
			if (NR > 1) flush()
			py = (FILENAME ~ /\.py$/); in_comment = 0; in_body = 0
		}
		{
			line = $0
			if (py) {
				sub(/#.*/, "", line)
				if (line ~ /^[[:space:]]*(def|class)[[:space:]]/) flush()
				if (line ~ /Gtk\.(Message)?Dialog\(/) sites[++n] = FILENAME ":" FNR
				if (line ~ /set_default_response\(|set_default_button\(/) has_default = 1
				next
			}
			if (in_comment) {
				if (line !~ /\*\//) next
				sub(/^.*\*\//, "", line); in_comment = 0
			}
			gsub(/\/\*([^*]|\*+[^*\/])*\*+\//, "", line)
			if (line ~ /\/\*/) { sub(/\/\*.*/, "", line); in_comment = 1 }
			sub(/\/\/.*/, "", line)
			if ($0 ~ /^\{/) { in_body = 1; n = 0; has_default = 0 }
			if (in_body && line ~ /(^|[^A-Za-z0-9_])(gtk_dialog_new(_with_buttons)?|gtk_message_dialog_new(_with_markup)?)[[:space:]]*\(/)
				sites[++n] = FILENAME ":" FNR
			if (in_body && line ~ /(gtk_dialog_set_default_response|eel_dialog_set_default)[[:space:]]*\(/)
				has_default = 1
			if ($0 ~ /^\}/) { flush(); in_body = 0 }
		}
		END { flush() }
	' "$@"
}

scratch="$(mktemp -d)"
trap 'fSafeRm_IfThere "${scratch}"' EXIT
cat > "${scratch}/bare.c" <<'EOF'
static void
ask (void)
{
	/* gtk_dialog_new () in a comment is not a site */
	GtkWidget *d = gtk_message_dialog_new (NULL, 0, GTK_MESSAGE_INFO, GTK_BUTTONS_OK, "x");
	gtk_dialog_run (GTK_DIALOG (d));
}

static void
ask_well (void)
{
	GtkWidget *d = gtk_dialog_new_with_buttons ("x", NULL, 0, "_OK", GTK_RESPONSE_OK, NULL);
	eel_dialog_set_default (GTK_DIALOG (d), GTK_RESPONSE_OK);
}

static void
noted (void)
{
	/* like gtk_dialog_new_with_buttons () does,
	 * gtk_message_dialog_new () */
	int x = 0;
}
EOF
cat > "${scratch}/bare.py" <<'EOF'
class A:
    def ask(self):
        dialog = Gtk.MessageDialog(buttons=Gtk.ButtonsType.YES_NO)
        dialog.run()

    def ask_well(self):
        dialog = Gtk.MessageDialog(buttons=Gtk.ButtonsType.OK)  # Gtk.Dialog( in a comment
        set_default_button(dialog, Gtk.ResponseType.OK)
EOF
said="$(fSites "${scratch}/bare.c" "${scratch}/bare.py")"
want="${scratch}/bare.c:5
${scratch}/bare.py:3"
[[ "$said" == "$want" ]] || fFail "the checker found [${said//$'\n'/, }] in the made-up sources, wanted bare.c:5 and bare.py:3"

mapfile -t files < <(find "${root}/source" -path "${root}/source/test" -prune -o \( -name '*.c' -o -name '*.py' \) -type f -print | sort)
((${#files[@]} > 100)) || fFail "found only ${#files[@]} source files under source/"
while IFS= read -r site; do
	[[ -n "$site" ]] || continue
	fFail "${site#"${root}/"} makes a dialog with no default response in the same function"
done < <(fSites "${files[@]}")

if ((failures)); then fEcho "FAILED: dialog defaults, ${failures} problem(s)"; exit 1; fi
fEcho "OK: every dialog made in source/ sets a default response"
