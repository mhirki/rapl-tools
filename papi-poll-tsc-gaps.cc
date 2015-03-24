/* 
 * papi-poll-gaps.cc
 * Find the average gap between RAPL updates by polling via PAPI at max frequency.
 * Code based on IgProf energy profiling module by Filip Nybäck.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <papi.h>
#include <math.h>
#include <stdint.h>
#include <vector>
#include <unistd.h>

#include "util.h"

#define READ_ENERGY(a) PAPI_read(s_event_set, a)

#if __x86_64__ || __i386__
#define HAVE_RDTSC
#define RDTSC(v)							\
  do { unsigned lo, hi;							\
    __asm__ volatile("rdtsc" : "=a" (lo), "=d" (hi));			\
    (v) = ((uint64_t) lo) | ((uint64_t) hi << 32);			\
  } while (0)
#endif

static double gettimeofday_double() {
	struct timeval now;
	gettimeofday(&now, NULL);
	return now.tv_sec + now.tv_usec * 1e-6;
}

bool do_rapl() {
	int s_event_set = 0;
	int s_num_events = 0;
	long long *s_values = NULL;
	int i = 0, iteration = 0;
	int idx_pkg_energy = -1;
	
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
		
		if (strstr(event_name, "PACKAGE_ENERGY_CNT:")) {
			idx_pkg_energy = s_num_events;
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
	
	// Allocate memory for reading the counters
	s_values = (long long *)calloc(s_num_events, sizeof(long long));
	
	// Activate the event set.
	if (PAPI_start(s_event_set) != PAPI_OK) {
		fprintf(stderr, "Could not activate the event set.\n");
		return false;
	}
	
	uint64_t tsc = 0;
	uint64_t tsc_prev = 0;
	uint64_t tsc_freq = 0;
	printf("Calibrating TSC frequency.\n");
	RDTSC(tsc_prev);
	double fstart = gettimeofday_double();
	sleep(1);
	RDTSC(tsc);
	double fnow = gettimeofday_double();
	printf("Time spent: %f seconds\n", fnow - fstart);
	tsc_freq = (tsc - tsc_prev) / (fnow - fstart);
	printf("Measured tsc_freq is %llu\n", (long long unsigned) tsc_freq);
	
	// Rounding to closest .1 GHz
	tsc_freq = (tsc_freq + 50000000) / 100000000 * 100000000;
	printf("Guessing that ideal tsc_freq is %llu\n", (long long unsigned) tsc_freq);
	
	long long prev_energy = 0;
	fstart = gettimeofday_double();
	const int num_iterations = 500000;
	uint64_t biggest_gap = 0;
	uint64_t sum_gaps = 0;
	RDTSC(tsc_prev);
	int num_gaps = -1;
	std::vector<uint64_t> gaps;
	for (iteration = 0; iteration < num_iterations; iteration++) {
		READ_ENERGY(s_values);
		if (s_values[idx_pkg_energy] != prev_energy) {
			prev_energy = s_values[idx_pkg_energy];
			RDTSC(tsc);
			uint64_t gap = tsc - tsc_prev;
			num_gaps++;
			if (num_gaps > 0) {
				sum_gaps += gap;
				if (gap > biggest_gap)
					biggest_gap = gap;
				gaps.push_back(gap);
			}
			printf("%llu at %llu TSC, %llu cycles gap since previous, frequency modulus is %llu\n", prev_energy, (long long unsigned)tsc, (long long unsigned)(tsc - tsc_prev), (long long unsigned)(tsc % (tsc_freq / 1000)));
			tsc_prev = tsc;
		}
	}
	
	fnow = gettimeofday_double();
	printf("%d iterations in %f seconds.\n", num_iterations, fnow - fstart);
	printf("Polling rate of %f hz.\n", num_iterations / (fnow - fstart));
	printf("PAPI polling delay of %f microseconds.\n", (fnow - fstart) / num_iterations * 1000000.0);
	printf("Biggest gap was %llu cycles.\n", (long long unsigned)biggest_gap);
	double avg_gap = (double)sum_gaps / num_gaps;
	printf("Average gap of %f cycles.\n", avg_gap);
	
	// Calculate standard deviation
	double sum_squares = 0.0;
	for (i = 0; i < num_gaps; i++) {
		double diff = gaps[i] - avg_gap;
		sum_squares += diff * diff;
	}
	printf("Standard deviation of the gaps is %f cycles.\n", sqrt(sum_squares / num_gaps));
	
	// Dump the gaps to a file
	FILE *fp = fopen("gaps.csv", "w");
	for (i = 0; i < num_gaps; i++) {
		fprintf(fp, "%llu\n", (long long unsigned)gaps[i]);
	}
	fclose(fp);
	
	return true;
}

int main() {
	do_affinity(0);
	do_rapl();
	return 0;
}
