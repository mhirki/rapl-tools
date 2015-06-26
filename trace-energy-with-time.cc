/*
 * trace-energy-time.cc: Runs a command and produces an energy trace of its execution.
 *
 * This is an improved version that records timestamps.
 *
 * Author: Mikael Hirki <mikael.hirki@aalto.fi>
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <string.h>
#include <math.h>

#include <vector>

#include <papi.h>

#include "util.h"

static pid_t child_pid = -1;
static int exit_code = EXIT_SUCCESS;
static int sigchld_received = 0;
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
	struct timespec timestamp;
	long long pkg;
	long long pp0;
	long long pp1;
	long long dram;
};

static std::vector<energy_numbers> v_energy_numbers;

static void sigchld_handler(int sig) {
	(void)sig;
	sigchld_received = 1;
}

static void sigalrm_handler(int sig) {
	(void)sig;
	sigalrm_received = 1;
}

static void do_signals() {
	signal(SIGCHLD, &sigchld_handler);
	signal(SIGALRM, &sigalrm_handler);
}

static const clockid_t timer_clockid = CLOCK_REALTIME;
static timer_t rapl_timer = 0;
static double rapl_period_in_nanosec = 1e6;

static void setup_timer() {
	const int interval_multiplier = 5;
	struct sigevent ev;
	memset(&ev, 0, sizeof(ev));
	ev.sigev_notify = SIGEV_SIGNAL;
	ev.sigev_signo = SIGALRM;
	if (timer_create(timer_clockid, &ev, &rapl_timer) < 0) {
		perror("timer_create");
		return;
	}
	struct itimerspec timer_value = { { 0, round(interval_multiplier * rapl_period_in_nanosec) }, { 0, 1 } };
	if (timer_settime(rapl_timer, 0, &timer_value, NULL) < 0) {
		perror("timer_settime");
		return;
	}
}

static void reset_timer() {
	struct itimerspec timer_value = { { 0, 0 }, { 0, 0 } };
	if (timer_settime(rapl_timer, 0, &timer_value, NULL) < 0) {
		perror("timer_settime");
		return;
	}
	if (timer_delete(rapl_timer) < 0) {
		perror("timer_delete");
		return;
	}
}

/*
 * Based on Filip Nybäck's energy profiling module in IgProf
 */
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

#if 0
static void calibrate_rapl() {
	long long old_rapl_value = 0;
	int updates = 0, num_updates = 1000;
	struct timespec time_start, time_end;
	
	if (idx_pkg_energy == -1) {
		fprintf(stderr, "trace-energy: No RAPL socket energy found, cannot calibrate!\n");
		return;
	}
	
	// Warmup
	clock_gettime(CLOCK_REALTIME, &time_start);
	clock_gettime(CLOCK_REALTIME, &time_end);
	
	READ_ENERGY(s_rapl_values);
	old_rapl_value = s_rapl_values[idx_pkg_energy];
	
	// Wait for a RAPL update
	while (true) {
		READ_ENERGY(s_rapl_values);
		if (unlikely(old_rapl_value != s_rapl_values[idx_pkg_energy])) {
			old_rapl_value = s_rapl_values[idx_pkg_energy];
			break;
		}
	}
	
	clock_gettime(CLOCK_REALTIME, &time_start);
	
	while (likely(updates < num_updates)) {
		READ_ENERGY(s_rapl_values);
		if (unlikely(old_rapl_value != s_rapl_values[idx_pkg_energy])) {
			old_rapl_value = s_rapl_values[idx_pkg_energy];
			updates++;
		}
	}
	
	clock_gettime(CLOCK_REALTIME, &time_end);
	double time_delta = (time_end.tv_sec - time_start.tv_sec) + (time_end.tv_nsec - time_start.tv_nsec) * 1e-9;
	double time_per_update = time_delta / num_updates;
	printf("trace-energy: Calibration result: %.6f ms between RAPL updates.\n", time_per_update * 1000.0);
	rapl_period_in_nanosec = time_per_update * 1e9;
}
#endif

static void handle_sigchld() {
	int status = 0;
	if (child_pid > 0) {
		while (waitpid(child_pid, &status, WNOHANG) > 0) {
			if (WIFEXITED(status)) {
				int child_exit_code = WEXITSTATUS(status);
				printf("trace-energy: Child exited normally with exit code %d\n", child_exit_code);
				exit_code = child_exit_code;
				child_pid = -1;
				break;
			}
			else if (WIFSIGNALED(status)) {
				printf("trace-energy: Child was terminated by a signal\n");
				exit_code = EXIT_FAILURE;
				child_pid = -1;
				break;
			}
		}
	}
}

static void handle_sigalrm() {
	long long pkg_energy = 0, pp0_energy = 0, pp1_energy = 0, dram_energy = 0;
	struct timespec now;
	
	READ_ENERGY(s_rapl_values);
	clock_gettime(CLOCK_REALTIME, &now);
	
	if (likely(idx_pkg_energy != -1)) {
		pkg_energy = s_rapl_values[idx_pkg_energy];
	}
	if (likely(idx_pp0_energy != -1)) {
		pp0_energy = s_rapl_values[idx_pp0_energy];
	}
	if (likely(idx_pp1_energy != -1)) {
		pp1_energy = s_rapl_values[idx_pp1_energy];
	}
	if (likely(idx_dram_energy != -1)) {
		dram_energy = s_rapl_values[idx_dram_energy];
	}
	
	struct energy_numbers numbers = { now, pkg_energy, pp0_energy, pp1_energy, dram_energy };
	v_energy_numbers.push_back(numbers);
}

static void wait_for_child() {
	struct timespec sleep_time = { 1, 0 };
	int i;
	
	setup_timer();

	while (likely(child_pid > 0)) {
		/* Sleep for one second */
		nanosleep(&sleep_time, NULL);
		if (unlikely(__sync_bool_compare_and_swap(&sigchld_received, 1, 0))) {
			handle_sigchld();
		}
		if (likely(__sync_bool_compare_and_swap(&sigalrm_received, 1, 0))) {
			sigalrm_received = 0;
			handle_sigalrm();
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
		double timestamp = v_energy_numbers[i].timestamp.tv_sec + v_energy_numbers[i].timestamp.tv_nsec * 1e-9;
		double pkg_energy = (v_energy_numbers[i].pkg - v_energy_numbers[i - 1].pkg) * scaleFactor;
		double pp0_energy = (v_energy_numbers[i].pp0 - v_energy_numbers[i - 1].pp0) * scaleFactor;
		double pp1_energy = (v_energy_numbers[i].pp1 - v_energy_numbers[i - 1].pp1) * scaleFactor;
		double dram_energy = (v_energy_numbers[i].dram - v_energy_numbers[i - 1].dram) * scaleFactor;
		fprintf(fp, "%.6f, %.6f, %.6f, %.6f, %.6f\n", timestamp, pkg_energy, pp0_energy, pp1_energy, dram_energy);
	}
	
	fclose(fp);
}

static void do_warmup() {
	// Warmup
	struct timespec sleep_time_warm = { 0, 1000 };
	sigalrm_handler(0);
	nanosleep(&sleep_time_warm, NULL);
	sigalrm_received = 0;
	handle_sigalrm();
	v_energy_numbers.pop_back();
}

static void do_fork_and_exec(int argc, char **argv) {
	if (argc > 1) {
		child_pid = fork();
		if (child_pid == 0) {
			do_affinity_all();
			execvp(argv[1], &argv[1]);
			perror("execlp");
			exit(-1);
		} else if (child_pid < 0) {
			perror("fork");
		} else {
			// Increase our priority
			if (setpriority(PRIO_PROCESS, 0, -5) < 0) {
				perror("setpriority");
			}
			wait_for_child();
		}
	} else {
		printf("Usage: %s <program> [parameters]\n", argv[0]);
	}
}

int main(int argc, char **argv) {
	// Set affinity to core 0
	do_affinity(0);
	v_energy_numbers.reserve(1000);
	do_signals();
	init_rapl();
	// Calibration is disabled because it causes more problems than it solves
	//calibrate_rapl();
	do_warmup();
	do_fork_and_exec(argc, argv);
	return exit_code;
}
