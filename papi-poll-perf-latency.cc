/* 
 * papi-poll-perf-latency.cc
 * Benchmark the latency of calling PAPI_read() to read performance counters.
 * Code based on IgProf energy profiling module by Filip Nybäck.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <papi.h>
#include <math.h>
#include <vector>

#include "util.h"

static double gettimeofday_double() {
	struct timeval now;
	gettimeofday(&now, NULL);
	return now.tv_sec + now.tv_usec * 1e-6;
}

bool do_rapl() {
	int s_perf_event_set = 0;
	int s_perf_events = 0;
	long long *s_values = NULL;
	int i = 0, num_iterations = 1000000;
	
	if (PAPI_library_init(PAPI_VER_CURRENT) != PAPI_VER_CURRENT) {
		fprintf(stderr, "PAPI library initialisation failed.\n");
		return false;
	}
	
	// Create an event set.
	s_perf_event_set = PAPI_NULL;
	if (PAPI_create_eventset(&s_perf_event_set) != PAPI_OK) {
		fprintf(stderr, "Could not create PAPI event set.\n");
		return false;
	}
	
	int code = PAPI_NATIVE_MASK;
	const char *event_name = "INSTRUCTIONS_RETIRED";
	if (PAPI_event_name_to_code(strdup(event_name), &code) != PAPI_OK) {
		fprintf(stderr, "No event found %s!\n", event_name);
	} else {
		if (PAPI_add_event(s_perf_event_set, code) != PAPI_OK) {
			fprintf(stderr, "PAPI_add_event failed!\n");
		} else {
			++s_perf_events;
		}
	}
	
	if (s_perf_events == 0) {
		fprintf(stderr, "No perf events added.\n");
		return false;
	}
	
	// Allocate memory for reading the counters
	s_values = (long long *)calloc(s_perf_events, sizeof(long long));
	
	// Activate the event set.
	if (PAPI_start(s_perf_event_set) != PAPI_OK) {
		fprintf(stderr, "Could not activate the event set.\n");
		return false;
	}
	
	double fstart = gettimeofday_double();
	for (i = 0; i < num_iterations; i++) {
		PAPI_read(s_perf_event_set, s_values);
	}
	double fend = gettimeofday_double();
	
	printf("Average PAPI_read() latency: %f nanoseconds\n", (fend - fstart) * 1000000000.0 / num_iterations);
	
	return true;
}

int main() {
	do_affinity(0);
	do_rapl();
	return 0;
}
