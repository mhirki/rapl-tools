/*
 * Utility functions
 *
 * Author: Mikael Hirki <mikael.hirki@aalto.fi>
 */

#include <sched.h>
#include <stdio.h>
#include <unistd.h>

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
	int i = 0, num_cpus = sysconf( _SC_NPROCESSORS_ONLN );
	CPU_ZERO(&mask);
	for (i = 0; i < num_cpus; i++) {
		CPU_SET(i, &mask);
	}
	int result = sched_setaffinity(0, sizeof(mask), &mask);
	if (result < 0) {
		perror("sched_setaffinity");
	}
	return result;
}
