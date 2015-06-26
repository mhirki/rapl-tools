/*
 * Utility functions
 *
 * Author: Mikael Hirki <mikael.hirki@aalto.fi>
 */

extern "C" {
	int do_affinity(int core);
	int do_affinity_all();
}

#define likely(x)       __builtin_expect(!!(x), 1)
#define unlikely(x)     __builtin_expect(!!(x), 0)
