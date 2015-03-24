/*
 * trace-energy.cc: Runs a command and produces an energy trace of its execution.
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <string.h>

#include <vector>

#include <papi.h>

#include "util.h"

static pid_t child_pid = -1;
static int exit_code = EXIT_SUCCESS;
static int sigalrm_received = 0;

static int s_event_set = 0;
static int s_num_events = 0;
static long long *s_rapl_values = NULL;

#define READ_ENERGY(a) PAPI_read(s_event_set, a)

static int idx_pkg_energy = -1;
static int idx_pp0_energy = -1;
static int idx_pp1_energy = -1;
static int idx_dram_energy = -1;

static const double scaleFactor = 1e-9;

struct energy_numbers {
	double timestamp;
	long long pkg;
	long long pp0;
	long long pp1;
	long long dram;
};

static std::vector<energy_numbers> v_energy_numbers;

static void sigchld_handler(int sig) {
	(void)sig;
	int status = 0;
	if (child_pid > 0) {
		int rval = waitpid(child_pid, &status, WNOHANG);
		if (rval < 0) {
			perror("waitpid");
		} else if (rval > 0) {
			if (WIFEXITED(status)) {
				int child_exit_code = WEXITSTATUS(status);
				printf("trace-energy: Child exited normally with exit code %d\n", child_exit_code);
				exit_code = child_exit_code;
				child_pid = -1;
			}
			else if (WIFSIGNALED(status)) {
				printf("trace-energy: Child was terminated by a signal\n");
				exit_code = EXIT_FAILURE;
				child_pid = -1;
			}
		}
	}
}

static void sigalrm_handler(int sig) {
	(void)sig;
	sigalrm_received = 1;
}

static void do_signals() {
	signal(SIGCHLD, &sigchld_handler);
	signal(SIGALRM, &sigalrm_handler);
}

static const int timer_which = ITIMER_REAL;

static void setup_timer() {
	struct itimerval timer_value = { { 0, 5000 }, { 0, 1 } };
	setitimer(timer_which, &timer_value, NULL);
}

static void reset_timer() {
	struct itimerval timer_value = { { 0, 0 }, { 0, 0 } };
	setitimer(timer_which, &timer_value, NULL);
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
	s_rapl_values = (long long *)calloc(s_num_events, sizeof(long long));
	
	// Activate the event set.
	if (PAPI_start(s_event_set) != PAPI_OK) {
		fprintf(stderr, "Could not activate the event set.\n");
		return false;
	}
	
	return true;
}

static double gettimeofday_double() {
	struct timeval now;
	gettimeofday(&now, NULL);
	return now.tv_sec + now.tv_usec * 0.000001;
}

static void handle_sigalrm() {
	long long pkg_energy = 0, pp0_energy = 0, pp1_energy = 0, dram_energy = 0;
	double now;
	
	READ_ENERGY(s_rapl_values);
	now = gettimeofday_double();
	
	if (idx_pkg_energy != -1) {
		pkg_energy = s_rapl_values[idx_pkg_energy];
	}
	if (idx_pp0_energy != -1) {
		pp0_energy = s_rapl_values[idx_pp0_energy];
	}
	if (idx_pp1_energy != -1) {
		pp1_energy = s_rapl_values[idx_pp1_energy];
	}
	if (idx_dram_energy != -1) {
		dram_energy = s_rapl_values[idx_dram_energy];
	}
	
	struct energy_numbers numbers = { now, pkg_energy, pp0_energy, pp1_energy, dram_energy };
	v_energy_numbers.push_back(numbers);
}

static void wait_for_child() {
	struct timespec sleep_time = { 1, 0 };
	int i;
	
	setup_timer();

	while (child_pid > 0) {
		/* Sleep for one second */
		nanosleep(&sleep_time, NULL);
		if (sigalrm_received) {
			handle_sigalrm();
			sigalrm_received = 0;
		}
	}
	
	reset_timer();
	
	FILE *fp = fopen("energy-trace.csv", "w");
	if (!fp) {
		fprintf(stderr, "Error: Could not open energy-trace.csv for writing!\n");
		exit(-1);
	}
	
	const int n = v_energy_numbers.size();
	for (i = 1; i < n; i++) {
		double pkg_energy = (v_energy_numbers[i].pkg - v_energy_numbers[i - 1].pkg) * scaleFactor;
		double pp0_energy = (v_energy_numbers[i].pp0 - v_energy_numbers[i - 1].pp0) * scaleFactor;
		double pp1_energy = (v_energy_numbers[i].pp1 - v_energy_numbers[i - 1].pp1) * scaleFactor;
		double dram_energy = (v_energy_numbers[i].dram - v_energy_numbers[i - 1].dram) * scaleFactor;
		fprintf(fp, "%.6f, %f, %f, %f, %f\n", v_energy_numbers[i].timestamp, pkg_energy, pp0_energy, pp1_energy, dram_energy);
	}
	
	fclose(fp);
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
			// Set affinity to core 0
			do_affinity(0);
			wait_for_child();
		}
	} else {
		printf("Usage: %s <program> [parameters]\n", argv[0]);
	}
}

int main(int argc, char **argv) {
	v_energy_numbers.reserve(1000);
	do_signals();
	init_rapl();
	do_fork_and_exec(argc, argv);
	return exit_code;
}
