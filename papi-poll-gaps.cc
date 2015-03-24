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

#include <vector>

#include "util.h"

#define READ_ENERGY(a) PAPI_read(s_event_set, a)

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
	
	long long prev_energy = 0;
	double fstart = gettimeofday_double();
	double fprev = fstart;
	double fnow = fstart;
	double gap = 0.0;
	std::vector<double> gaps;
	gaps.reserve(1000);
	double sum_gaps = 0.0;
	double biggest_gap = 0.0;
	int num_gaps = -1;
	const int num_iterations = 1000000;
	for (iteration = 0; iteration < num_iterations; iteration++) {
		READ_ENERGY(s_values);
		if (s_values[idx_pkg_energy] != prev_energy) {
			prev_energy = s_values[idx_pkg_energy];
			fnow = gettimeofday_double();
			gap = fnow - fprev;
			num_gaps++;
			// Ignore the first gap
			if (num_gaps > 0) {
				sum_gaps += gap;
				if (gap > biggest_gap)
					biggest_gap = gap;
				gaps.push_back(gap);
			}
			printf("%lld at %f seconds, %f second gap since previous\n", prev_energy, fnow - fstart, gap);
			fprev = fnow;
		}
	}
	
	fnow = gettimeofday_double();
	printf("%d iterations in %f seconds.\n", num_iterations, fnow - fstart);
	printf("Polling rate of %f hz.\n", num_iterations / (fnow - fstart));
	printf("PAPI polling delay of %f microseconds.\n", (fnow - fstart) / num_iterations * 1000000.0);
	printf("Biggest gap was %f millisecond.\n", biggest_gap * 1000.0);
	double avg_gap = sum_gaps / num_gaps;
	printf("Average gap of %f milliseconds.\n", avg_gap * 1000.0);
	
	// Calculate standard deviation
	double sum_squares = 0.0;
	for (i = 0; i < num_gaps; i++) {
		double diff = gaps[i] - avg_gap;
		sum_squares += diff * diff;
	}
	printf("Standard deviation of the gaps is %f microseconds.\n", sqrt(sum_squares / num_gaps) * 1000000.0);
	
	// Dump the gaps to a file
	FILE *fp = fopen("gaps.csv", "w");
	if (!fp) {
		fprintf(stderr, "Failed to open gaps.csv!\n");
	} else {
		printf("Dumping data to gaps.csv\n");
		for (i = 0; i < num_gaps; i++) {
			fprintf(fp, "%f\n", gaps[i]);
		}
		fclose(fp);
	}
	
	return true;
}

int main() {
	do_affinity(0);
	do_rapl();
	return 0;
}
