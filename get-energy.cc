/* 
 * Measure the energy of executing another program.
 * Code based on IgProf energy profiling module by Filip Nybäck.
 * 
 * TODO:
 * Use waitpid() instead of wait().
 * Account for RAPL overflows.
 *
 * Author: Mikael Hirki <mikael.hirki@aalto.fi>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <math.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>
#include <signal.h>

#include <vector>

#include <papi.h>

#define READ_ENERGY(a) PAPI_read(s_event_set, a)

static int s_event_set = 0;
static int s_num_events = 0;
static long long *s_begin_values = NULL;
static long long *s_end_values = NULL;

static int idx_pkg_energy = -1;
static int idx_pp0_energy = -1;
static int idx_pp1_energy = -1;
static int idx_dram_energy = -1;

// Set the scale factor for the RAPL energy readings:
// one integer step is 15.3 microjoules, scale everything to joules.
static const double scaleFactor = 1e-9;

static pid_t child_pid = -1;

static double gettimeofday_double() {
	struct timeval now;
	gettimeofday(&now, NULL);
	return now.tv_sec + now.tv_usec * 1e-6;
}

static void sighandler(int signum) {
	printf("Received signal %d\n", signum);
	if (child_pid > 0) {
		kill(child_pid, signum);
	} else {
		exit(-1);
	}
}

static void do_signals() {
	signal(SIGQUIT, sighandler);
	signal(SIGTERM, sighandler);
	signal(SIGINT, sighandler);
}

static bool init_rapl() {
	if (PAPI_library_init(PAPI_VER_CURRENT) != PAPI_VER_CURRENT) {
		fprintf(stderr, "PAPI library initialisation failed.\n");
		return false;
	}
	
	// Find the RAPL component of PAPI.
	int num_components = PAPI_num_components();
	int component_id;
	const PAPI_component_info_t *component_info = 0;
	for (component_id = 0; component_id < num_components; ++component_id)
	{
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
		
		//printf("Adding %s to event set.\n", event_name);
		if (PAPI_add_event(s_event_set, code) != PAPI_OK)
			break;
		++s_num_events;
	}
	if (s_num_events == 0) {
		fprintf(stderr, "Could not find any RAPL events.\n");
		return false;
	}
	
	// Allocate memory for reading the counters
	s_begin_values = (long long *)calloc(s_num_events, sizeof(long long));
	s_end_values = (long long *)calloc(s_num_events, sizeof(long long));
	
	// Activate the event set.
	if (PAPI_start(s_event_set) != PAPI_OK) {
		fprintf(stderr, "Could not activate the event set.\n");
		return false;
	}
	
	return true;
}

static void do_fork_and_exec(int argc, char **argv) {
	if (argc > 1) {
		child_pid = fork();
		if (child_pid == 0) {
			execvp(argv[1], &argv[1]);
			perror("execlp");
			exit(-1);
		} else if (child_pid < 0) {
			perror("fork");
		} else {
			int status;
			while (true) {
				int rval = wait(&status);
				if (rval < 0) {
					if (errno == EINTR) continue;
					else break;
				}
				if (WIFEXITED(status) || WIFSIGNALED(status)) {
					child_pid = -1;
					break;
				}
			}
		}
	} else {
		printf("Usage: %s <program> [parameters]\n", argv[0]);
	}
}

int main(int argc, char **argv) {
	do_signals();
	if (init_rapl()) {
		double begin_time = gettimeofday_double();
		READ_ENERGY(s_begin_values);
		do_fork_and_exec(argc, argv);
		READ_ENERGY(s_end_values);
		double end_time = gettimeofday_double();
		
		double time_elapsed = end_time - begin_time;
		printf("Real time elapsed: %f seconds\n", time_elapsed);
		if (idx_pkg_energy != -1) {
			double pkg_energy = scaleFactor * (s_end_values[idx_pkg_energy] - s_begin_values[idx_pkg_energy]);
			printf("Package energy consumed: %f J\n", pkg_energy);
			printf("Package average power: %f W\n", pkg_energy / time_elapsed);
		}
		if (idx_pp0_energy != -1) {
			double pp0_energy = scaleFactor * (s_end_values[idx_pp0_energy] - s_begin_values[idx_pp0_energy]);
			printf("PP0 energy consumed: %f J\n", pp0_energy);
			printf("PP0 average power: %f W\n", pp0_energy / time_elapsed);
		}
		if (idx_pp1_energy != -1) {
			double pp1_energy = scaleFactor * (s_end_values[idx_pp1_energy] - s_begin_values[idx_pp1_energy]);
			printf("PP1 energy consumed: %f J\n", pp1_energy);
			printf("PP1 average power: %f W\n", pp1_energy / time_elapsed);
		}
		if (idx_dram_energy != -1) {
			double dram_energy = scaleFactor * (s_end_values[idx_dram_energy] - s_begin_values[idx_dram_energy]);
			printf("DRAM energy consumed: %f J\n", dram_energy);
			printf("DRAM average power: %f W\n", dram_energy / time_elapsed);
		}
	}
	return 0;
}
