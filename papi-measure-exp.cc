/* 
 * papi-measure-exp.cc
 * Attempt to measure the average energy consumption of the exp() function.
 * Code based on IgProf energy profiling module by Filip Nybäck.
 *
 * Author: Mikael Hirki <mikael.hirki@aalto.fi>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <math.h>
#include <stdint.h>

#include <vector>

#include <papi.h>

#include "util.h"

#define READ_ENERGY(a) PAPI_read(s_event_set, a)
#define READ_PERF_EVENTS(a) PAPI_read(s_perf_event_set, a)

#if __x86_64__ || __i386__
#define HAVE_RDTSC
#define RDTSC(v)							\
  do { unsigned lo, hi;							\
    __asm__ volatile("rdtsc" : "=a" (lo), "=d" (hi));			\
    (v) = ((uint64_t) lo) | ((uint64_t) hi << 32);			\
  } while (0)
#endif

static double timeval_to_double(struct timeval *tv) {
	return tv->tv_sec + tv->tv_usec * 1e-6;
}

bool do_rapl(double input) {
	int s_event_set = 0;
	int s_perf_event_set = 0;
	int s_num_events = 0;
	int s_perf_events = 0;
	long long *s_values_before = NULL;
	long long *s_values_after = NULL;
	long long *s_perf_values_before = NULL;
	long long *s_perf_values_after = NULL;
	int i = 0;
	int idx_pkg_energy = -1;
	int idx_pp0_energy = -1;
	int idx_pp1_energy = -1;
	int idx_dram_energy = -1;
	int num_iterations = 100000000;
	uint64_t tsc_before = 0;
	uint64_t tsc_after = 0;
	struct timeval now;
	
	// Set the scale factor for the RAPL energy readings:
	// one integer step is 15.3 microjoules, scale everything to joules.
	double scaleFactor = 1e-9;
	
	if (PAPI_library_init(PAPI_VER_CURRENT) != PAPI_VER_CURRENT) {
		fprintf(stderr, "PAPI library initialisation failed.\n");
		return false;
	}
	
	// Find the RAPL component of PAPI.
	int num_components = PAPI_num_components();
	int component_id;
	const PAPI_component_info_t *component_info = 0;
	for (component_id = 0; component_id < num_components; ++component_id) {
		component_info = PAPI_get_component_info(component_id);
		if (component_info && strstr(component_info->name, "rapl")) {
			break;
		}
	}
	if (component_id == num_components) {
		fprintf(stderr, "No RAPL component found in PAPI library.\n");
		return false;
	}
	
	if (component_info->disabled) {
		fprintf(stderr, "RAPL component of PAPI disabled: %s.\n",
			component_info->disabled_reason);
		return false;
	}
	
	// Create an event set.
	s_event_set = PAPI_NULL;
	if (PAPI_create_eventset(&s_event_set) != PAPI_OK) {
		fprintf(stderr, "Could not create PAPI event set.\n");
		return false;
	}
	
	s_perf_event_set = PAPI_NULL;
	if (PAPI_create_eventset(&s_perf_event_set) != PAPI_OK) {
		fprintf(stderr, "Could not create PAPI event set.\n");
		return false;
	}
	
	int code = PAPI_NATIVE_MASK;
	for (int retval = PAPI_enum_cmp_event(&code, PAPI_ENUM_FIRST, component_id); retval == PAPI_OK; retval = PAPI_enum_cmp_event(&code, PAPI_ENUM_EVENTS, component_id)) {
		char event_name[PAPI_MAX_STR_LEN];
		if (PAPI_event_code_to_name(code, event_name) != PAPI_OK) {
			fprintf(stderr, "Could not get PAPI event name.\n");
			return false;
		}
		
		PAPI_event_info_t event_info;
		if (PAPI_get_event_info(code, &event_info) != PAPI_OK) {
			fprintf(stderr, "Could not get PAPI event info.\n");
			return false;
		}
		if (event_info.data_type != PAPI_DATATYPE_UINT64) {
			continue;
		}
		
		if (strstr(event_name, "PACKAGE_ENERGY:")) {
			idx_pkg_energy = s_num_events;
		} else if (strstr(event_name, "PP0_ENERGY:")) {
			idx_pp0_energy = s_num_events;
		} else if (strstr(event_name, "PP1_ENERGY:")) {
			idx_pp1_energy = s_num_events;
		} else if (strstr(event_name, "DRAM_ENERGY:")) {
			idx_dram_energy = s_num_events;
		} else {
			continue; // Skip other counters
		}
		
		printf("Adding %s to event set.\n", event_name);
		if (PAPI_add_event(s_event_set, code) != PAPI_OK) {
			break;
		}
		++s_num_events;
	}
	if (s_num_events == 0) {
		fprintf(stderr, "Could not find any RAPL events.\n");
		return false;
	}
	
	if (PAPI_event_name_to_code(strdup("INSTRUCTIONS_RETIRED"), &code) != PAPI_OK) {
		fprintf(stderr, "No event found INSTRUCTIONS_RETIRED!\n");
	} else {
		if (PAPI_add_event(s_perf_event_set, code) != PAPI_OK) {
			fprintf(stderr, "PAPI_add_event failed!\n");
		} else {
			++s_perf_events;
		}
	}
	
	// Allocate memory for reading the counters
	s_values_before = (long long *)calloc(s_num_events, sizeof(long long));
	s_values_after = (long long *)calloc(s_num_events, sizeof(long long));
	s_perf_values_before = (long long *)calloc(s_perf_events, sizeof(long long));
	s_perf_values_after = (long long *)calloc(s_perf_events, sizeof(long long));
	
	// Activate the event set.
	if (PAPI_start(s_event_set) != PAPI_OK) {
		fprintf(stderr, "Could not activate the event set.\n");
		return false;
	}
	if (PAPI_start(s_perf_event_set) != PAPI_OK) {
		fprintf(stderr, "Could not activate the perf event set.\n");
		return false;
	}
	
	READ_ENERGY(s_values_before);
	READ_PERF_EVENTS(s_perf_values_before);
	gettimeofday(&now, NULL);
	double tstart = timeval_to_double(&now);
	RDTSC(tsc_before);
	
	// Simple exp benchmark
	double result = 0;
	for (i = 0; i < num_iterations; i++) {
		result = exp(input);
	}
	
	RDTSC(tsc_after);
	gettimeofday(&now, NULL);
	double tend = timeval_to_double(&now);
	READ_PERF_EVENTS(s_perf_values_after);
	READ_ENERGY(s_values_after);
	
	long long cycles = tsc_after - tsc_before;
	double pkg_energy = scaleFactor * (s_values_after[idx_pkg_energy] - s_values_before[idx_pkg_energy]);
	double pp0_energy = scaleFactor * (s_values_after[idx_pp0_energy] - s_values_before[idx_pp0_energy]);
	double pp1_energy = 0;
	if (idx_pp1_energy != -1)
		pp1_energy = scaleFactor * (s_values_after[idx_pp1_energy] - s_values_before[idx_pp1_energy]);
	double dram_energy = 0;
	if (idx_dram_energy != -1)
		dram_energy = scaleFactor * (s_values_after[idx_dram_energy] - s_values_before[idx_dram_energy]);
	long long instructions_retired = s_perf_values_after[0] - s_perf_values_before[0];
	
	printf("Final result: %f\n", result);
	double time_spent = tend - tstart;
	printf("Real time spent: %f seconds\n", time_spent);
	printf("Cycles spent: %lld\n", cycles);
	printf("Instructions retired: %lld\n", instructions_retired);
	printf("Instructions per cycle: %f\n", (double)instructions_retired / cycles);
	
	printf("\n");
	printf("PKG energy spent: %f joules\n", pkg_energy);
	printf("PP0 energy spent: %f joules\n", pp0_energy);
	printf("PP1 energy spent: %f joules\n", pp1_energy);
	printf("DRAM energy spent: %f joules\n", dram_energy);
	
	printf("\n");
	printf("Average PKG power consumption: %f watts\n", pkg_energy / time_spent);
	printf("Average PP0 power consumption: %f watts\n", pp0_energy / time_spent);
	printf("Average PP1 power consumption: %f watts\n", pp1_energy / time_spent);
	printf("Average DRAM power consumption: %f watts\n", dram_energy / time_spent);
	
	printf("\n");
	printf("PKG energy per cycle: %f nanojoules\n", pkg_energy * 1e9 / cycles);
	printf("PP0 energy per cycle: %f nanojoules\n", pp0_energy * 1e9 / cycles);
	printf("PP1 energy per cycle: %f nanojoules\n", pp1_energy * 1e9 / cycles);
	printf("DRAM energy per cycle: %f nanojoules\n", dram_energy * 1e9 / cycles);
	
	printf("\n");
	printf("PKG energy per instruction: %f nanojoules\n", pkg_energy * 1e9 / instructions_retired);
	printf("PP0 energy per instruction: %f nanojoules\n", pp0_energy * 1e9 / instructions_retired);
	printf("PP1 energy per instruction: %f nanojoules\n", pp1_energy * 1e9 / instructions_retired);
	printf("DRAM energy per instruction: %f nanojoules\n", dram_energy * 1e9 / instructions_retired);
	
	return true;
}

int main(int argc, char **argv) {
	if (argc < 2) {
		fprintf(stderr, "Usage: %s <number>\n", argv[0]);
		return 1;
	}
	double input = atof(argv[1]);
	do_rapl(input);
	return 0;
}
