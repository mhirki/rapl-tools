/* 
 * papi-poll-timings.cc
 * Attempt to correlate RAPL updates with various timing sources.
 * Code based on IgProf energy profiling module by Filip Nybäck.
 *
 * Author: Mikael Hirki <mikael.hirki@aalto.fi>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <papi.h>
#include <math.h>
#include <stdint.h>
#include <vector>

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

#if 0
static double gettimeofday_double() {
	struct timeval now;
	gettimeofday(&now, NULL);
	return now.tv_sec + now.tv_usec * 1e-6;
}
#endif

clockid_t clk_ids[] = {
	CLOCK_REALTIME,
	CLOCK_MONOTONIC,
	CLOCK_MONOTONIC_RAW,
};

const char *clk_names[] = {
	"CLOCK_REALTIME",
	"CLOCK_MONOTONIC",
	"CLOCK_MONOTONIC_RAW",
};

const int num_clocks = sizeof(clk_ids) / sizeof(*clk_ids);

bool do_rapl() {
	int s_event_set = 0;
	int s_num_events = 0;
	long long *s_values = NULL;
	int i = 0;
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
	const int num_iterations = 100000;
	std::vector<timespec> realtime_times;
	std::vector<timespec> monotonic_times;
	std::vector<timespec> monotonic_raw_times;
	realtime_times.reserve(1000);
	monotonic_times.reserve(1000);
	monotonic_raw_times.reserve(1000);
	struct timespec now;
	std::vector<uint64_t> rdtsc_times;
	rdtsc_times.reserve(1000);
	uint64_t tsc = 0;
	for (i = 0; i < num_iterations; i++) {
		READ_ENERGY(s_values);
		if (s_values[idx_pkg_energy] != prev_energy) {
			prev_energy = s_values[idx_pkg_energy];
			clock_gettime(CLOCK_REALTIME, &now);
			realtime_times.push_back(now);
			clock_gettime(CLOCK_MONOTONIC, &now);
			monotonic_times.push_back(now);
			clock_gettime(CLOCK_MONOTONIC_RAW, &now);
			monotonic_raw_times.push_back(now);
#ifdef HAVE_RDTSC
			RDTSC(tsc);
#endif
			rdtsc_times.push_back(tsc);
		}
	}
	
	int num_times = realtime_times.size();
	for (i = 0; i < num_times; i++) {
		printf("%lld.%09lld\t%lld.%09lld\t%lld.%09lld\t%llu\n",
		       (long long)realtime_times[i].tv_sec, (long long)realtime_times[i].tv_nsec,
		       (long long)monotonic_times[i].tv_sec, (long long)monotonic_times[i].tv_nsec,
		       (long long)monotonic_raw_times[i].tv_sec, (long long)monotonic_raw_times[i].tv_nsec,
		       (long long unsigned)rdtsc_times[i]);
	}
	
	return true;
}

int main() {
	do_affinity(0);
	do_rapl();
	return 0;
}
