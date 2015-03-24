/* 
 * papi-poll-energy.cc
 * Poll RAPL counters at a given frequency for a given number of times.
 * Code based on IgProf energy profiling module by Filip Nybäck.
 * 
 * Usage: papi-poll-energy <number of polls> <polling frequency>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <math.h>

#include <vector>

#include <papi.h>

#include "util.h"

#define READ_ENERGY(a) PAPI_read(s_event_set, a)

bool do_rapl(int argc, char **argv) {
	int s_event_set = 0;
	int s_num_events = 0;
	long long *s_values = NULL;
	int i = 0;
	int idx_pkg_energy = -1;
	int idx_pp0_energy = -1;
	int idx_pp1_energy = -1;
	int idx_dram_energy = -1;
	struct timespec sleep_time = { 0, 1000000 };
	int num_iterations = 1000;
	
	// Set the scale factor for the RAPL energy readings:
	// one integer step is 15.3 microjoules, scale everything to joules.
	double scaleFactor = 1e-9;
	
	// Optional command-line parameters
	if (argc > 1) {
		num_iterations = atoi(argv[1]);
	}
	if (argc > 2) {
		double frequency = atof(argv[2]);
		double stime = 1.0 / frequency;
		double seconds = floor(stime);
		double nanoseconds = (stime - seconds) * 1e9;
		sleep_time.tv_sec = seconds;
		sleep_time.tv_nsec = nanoseconds;
	}
	
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
	
	// Allocate memory for reading the counters
	s_values = (long long *)calloc(s_num_events, sizeof(long long));
	
	// Activate the event set.
	if (PAPI_start(s_event_set) != PAPI_OK) {
		fprintf(stderr, "Could not activate the event set.\n");
		return false;
	}
	
	// Do an extra iteration because first reading is always zeros
	num_iterations++;
	
	std::vector<long long> pkg_energy_numbers(num_iterations, 0);
	std::vector<long long> pp0_energy_numbers(num_iterations, 0);
	std::vector<long long> pp1_energy_numbers(num_iterations, 0);
	std::vector<long long> dram_energy_numbers(num_iterations, 0);
	
	READ_ENERGY(s_values);
	if (idx_pkg_energy != -1)
		pkg_energy_numbers[0] = s_values[idx_pkg_energy];
	if (idx_pp0_energy != -1)
		pp0_energy_numbers[0] = s_values[idx_pp0_energy];
	if (idx_pp1_energy != -1)
		pp1_energy_numbers[0] = s_values[idx_pp1_energy];
	if (idx_dram_energy != -1)
		dram_energy_numbers[0] = s_values[idx_dram_energy];
	
	for (i = 1; i < num_iterations; i++) {
		nanosleep(&sleep_time, NULL);
		READ_ENERGY(s_values);
		if (idx_pkg_energy != -1)
			pkg_energy_numbers[i] = s_values[idx_pkg_energy];
		if (idx_pp0_energy != -1)
			pp0_energy_numbers[i] = s_values[idx_pp0_energy];
		if (idx_pp1_energy != -1)
			pp1_energy_numbers[i] = s_values[idx_pp1_energy];
		if (idx_dram_energy != -1)
			dram_energy_numbers[i] = s_values[idx_dram_energy];
	}
	
	double total_pkg_energy = 0.0;
	double total_pp0_energy = 0.0;
	double total_pp1_energy = 0.0;
	double total_dram_energy = 0.0;
	
	// Dump to a file
	FILE *fp = fopen("energy.csv", "w");
	if (!fp) {
		fprintf(stderr, "Failed to open energy.csv!\n");
	} else {
		printf("Dumping data to energy.csv\n");
		for (i = 1; i < num_iterations; i++) {
			double pkg_energy = scaleFactor * (pkg_energy_numbers[i] - pkg_energy_numbers[i - 1]);
			double pp0_energy = scaleFactor * (pp0_energy_numbers[i] - pp0_energy_numbers[i - 1]);
			double pp1_energy = scaleFactor * (pp1_energy_numbers[i] - pp1_energy_numbers[i - 1]);
			double dram_energy = scaleFactor * (dram_energy_numbers[i] - dram_energy_numbers[i - 1]);
			fprintf(fp, "%f, %f, %f, %f\n", pkg_energy, pp0_energy, pp1_energy, dram_energy);
			total_pkg_energy += pkg_energy;
			total_pp0_energy += pp0_energy;
			total_pp1_energy += pp1_energy;
			total_dram_energy += dram_energy;
		}
		fclose(fp);
	}
	
	printf("Total PKG energy: %f\n", total_pkg_energy);
	printf("Total PP0 energy: %f\n", total_pp0_energy);
	printf("Total PP1 energy: %f\n", total_pp1_energy);
	printf("Total DRAM energy: %f\n", total_dram_energy);
	
	return true;
}

int main(int argc, char **argv) {
	do_affinity(0);
	do_rapl(argc, argv);
	return 0;
}
