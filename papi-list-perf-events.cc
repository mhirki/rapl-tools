/* 
 * papi-list-perf-events.cc
 * List the available events in perf_event component.
 * Code based on IgProf energy profiling module by Filip Nybäck.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <math.h>
#include <stdint.h>

#include <papi.h>

bool do_papi() {
	if (PAPI_library_init(PAPI_VER_CURRENT) != PAPI_VER_CURRENT) {
		fprintf(stderr, "PAPI library initialisation failed.\n");
		return false;
	}
	
	// Find the perf_event component of PAPI.
	int num_components = PAPI_num_components();
	int component_id;
	const PAPI_component_info_t *component_info = 0;
	for (component_id = 0; component_id < num_components; ++component_id) {
		component_info = PAPI_get_component_info(component_id);
		if (component_info && strstr(component_info->name, "perf_event")) {
			break;
		}
	}
	if (component_id == num_components) {
		fprintf(stderr, "No perf_event component found in PAPI library.\n");
		return false;
	}
	
	if (component_info->disabled) {
		fprintf(stderr, "Perf_event component of PAPI disabled: %s.\n",
			component_info->disabled_reason);
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
		
		printf("name: %s\n", event_name);
	}
	
	return true;
}

int main() {
	do_papi();
	return 0;
}
