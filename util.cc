#include <sched.h>
#include <stdio.h>

#include <papi.h>

#include "util.h"

int do_affinity(int core) {
	cpu_set_t mask;
	CPU_ZERO(&mask);
	CPU_SET(core, &mask);
	int result = sched_setaffinity(0, sizeof(mask), &mask);
	if (result < 0) {
		perror("sched_setaffinity");
	}
	return result;
}

int do_affinity_all() {
	cpu_set_t mask;
	int i;
	CPU_ZERO(&mask);
	for (i = 0; i < CPU_SETSIZE; i++) {
		CPU_SET(i, &mask);
	}
	int result = sched_setaffinity(0, sizeof(mask), &mask);
	if (result < 0) {
		perror("sched_setaffinity");
	}
	return result;
}
