/* 
 * papi-list-components.cc
 * List the available PAPI components.
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

#include <vector>

#include <papi.h>

bool do_papi() {
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
		if (component_info)
			printf("%s\n", component_info->name);
	}
	
	return true;
}

int main() {
	do_papi();
	return 0;
}
