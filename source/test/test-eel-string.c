/* Runs eel-string's own checks. Most of them go through the custom printf,
 * which copies the caller's va_list once per width, precision and argument,
 * and file operation messages depend on it. */

#include <config.h>

#include <eel/eel-lib-self-check-functions.h>

int
main (void)
{
	eel_self_check_string ();
	eel_exit_if_self_checks_failed ();
	return 0;
}
