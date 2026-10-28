##	- Purpose: rewrite meson test's result lines so each test has one line that
##	  starts with its status, then its ID and name, as in
##	  "PASS dcsvh Extension load test  0.39s". The round reads a run's log
##	  for those. Everything else meson prints goes through as is.
##	- A result line is "  12/236 <id> <name>   OK   0.39s   exit status 77".
##	  The status is the last word that is one, with a time after it.
##	- Flushes every line, so a run still reads as it goes.
##	- Syntax: meson test ... | awk -f result-lines.awk

##	Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT

BEGIN {
	status["OK"] = "PASS"; status["EXPECTEDFAIL"] = "PASS"; status["SKIP"] = "SKIP"
	status["FAIL"] = "FAIL"; status["TIMEOUT"] = "FAIL"; status["ERROR"] = "FAIL"; status["UNEXPECTEDPASS"] = "FAIL"
}

/^ *[0-9]+\/[0-9]+ / {
	for (i = NF - 1; i > 2; i--)
		if (($i in status) && $(i + 1) ~ /^[0-9.]+s$/) break
	if (i > 2) {
		line = status[$i]
		for (j = 2; j < i; j++) line = line " " $j
		line = line " "
		for (j = i + 1; j <= NF; j++) line = line " " $j
		print line
		fflush()
		next
	}
}

{ print; fflush() }
