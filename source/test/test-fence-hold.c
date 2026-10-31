/* Linked whole into every test binary. It matters on macOS, where the fence
 * is a dylib. The linker there drops a dylib nothing calls into, and a test
 * binary that never calls the fence by name would then run with no fence at
 * all. This call keeps it, and makes the fence check it's in front of the
 * calls it guards before the test's own code runs. Elsewhere the fence has
 * already started by now, and this changes nothing. */

#include <config.h>

#include "test-fence.h"

__attribute__ ((constructor)) static void
hold_fence (void)
{
	test_fence_start ();
}
