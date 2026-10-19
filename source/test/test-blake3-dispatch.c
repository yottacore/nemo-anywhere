/* blake3's dispatcher, built again for the path test. Included whole, so the
 * vendored file stays as it came and the feature word it keeps to itself can
 * still be set. */

#include "blake3_dispatch.c"

#include "test-blake3-dispatch.h"

#if defined(IS_X86) && !defined(BLAKE3_NO_SSE2)
#define BUILT_SSE2 1
#else
#define BUILT_SSE2 0
#endif
#if defined(IS_X86) && !defined(BLAKE3_NO_SSE41)
#define BUILT_SSE41 1
#else
#define BUILT_SSE41 0
#endif
#if defined(IS_X86) && !defined(BLAKE3_NO_AVX2)
#define BUILT_AVX2 1
#else
#define BUILT_AVX2 0
#endif

/* Newest last. SSE4.1 also takes over the single-block compress from SSE2,
 * and AVX2 has none of its own, so it runs beside SSE4.1's. */
const TestBlake3Path test_blake3_paths[] = {
	{ "plain C", 0,                                 1, 1 },
	{ "SSE2",    SSE2,                              4, BUILT_SSE2 },
	{ "SSE4.1",  SSE2 | SSSE3 | SSE41,              4, BUILT_SSE41 },
	{ "AVX2",    SSE2 | SSSE3 | SSE41 | AVX | AVX2, 8, BUILT_AVX2 },
};
const size_t test_blake3_n_paths = sizeof (test_blake3_paths) / sizeof (test_blake3_paths[0]);

int
test_blake3_cpu (void)
{
	ATOMIC_STORE (g_cpu_features, UNDEFINED);
	return (int) get_cpu_features ();
}

void
test_blake3_limit (int features)
{
	ATOMIC_STORE (g_cpu_features, features);
}

size_t
test_blake3_degree (void)
{
	return blake3_simd_degree ();
}
