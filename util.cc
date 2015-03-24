#include <sched.h>

#include <papi.h>

#include "util.h"

int do_affinity(int core) {
	cpu_set_t mask;
	CPU_ZERO(&mask);
	CPU_SET(core, &mask);
	int result = sched_setaffinity(0, sizeof(mask), &mask);
	return result >= 0;
}
