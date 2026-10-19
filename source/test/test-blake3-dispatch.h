#ifndef TEST_BLAKE3_DISPATCH_H
#define TEST_BLAKE3_DISPATCH_H

#include <stddef.h>

/* One of blake3's routines, and what a processor that has it and nothing
   newer reports. */
typedef struct {
	const char *name;
	int         features;
	size_t      degree;
	int         built;
} TestBlake3Path;

extern const TestBlake3Path test_blake3_paths[];
extern const size_t         test_blake3_n_paths;

/* What this processor reports, as blake3 reads it. */
int  test_blake3_cpu    (void);

/* Makes blake3 believe the processor reports only these. */
void test_blake3_limit  (int features);

size_t test_blake3_degree (void);

#endif /* TEST_BLAKE3_DISPATCH_H */
