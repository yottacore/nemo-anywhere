#ifndef TEST_FENCE_H
#define TEST_FENCE_H

/* Every test binary links the fence whole. From the first instruction it
   refuses a delete, or a move from or onto a path, outside the test's own
   folders, and the test dies on the spot with the path on stderr. See
   test-fence.c for what counts as the test's own. */

#ifdef __cplusplus
extern "C" {
#endif

/* Lets this process delete and move inside dir from now on. test_scratch_dir
   calls it for every directory it makes. */
void test_fence_allow   (const char *dir);

/* Nonzero when a delete of path would be let through. */
int  test_fence_allows  (const char *path);

#ifdef __cplusplus
}
#endif

#endif /* TEST_FENCE_H */
