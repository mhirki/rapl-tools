/*
 * trace-energy-v2.cc: Runs a command and produces an energy trace of its execution.
 *
 * This is an improved version that records timestamps.
 * Added support for changing the frequency using the -F command line switch.
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
#include <sys/utsname.h>

#include <vector>
#include <string>
#include <sstream>

#include <papi.h>

#include "util.h"

// Version string
const char *trace_energy_version = "2.1";

// Frequency can be changed using the -F command line switch
// Defaults to 200 Hz
static double sampling_frequency = 200.0;

// Output file can be changed using the -o command line switch
static std::string output_file = "energy-trace.csv";

// The entire command line is stored in this string
static std::string cmdline;

// Timestamp at start
time_t start_time = 0;

// Child process CPU affinity to a specific core
// -1 means to specific affinity
static int child_cpu_affinity_core = -1;

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
static const clockid_t gettime_clockid = CLOCK_REALTIME;
static timer_t rapl_timer = 0;

static void setup_timer() {
	const double timer_period_in_nanosec = 1e9 / sampling_frequency;
	struct sigevent ev;
	memset(&ev, 0, sizeof(ev));
	ev.sigev_notify = SIGEV_SIGNAL;
	ev.sigev_signo = SIGALRM;
	if (timer_create(timer_clockid, &ev, &rapl_timer) < 0) {
		perror("timer_create");
		return;
	}
	struct itimerspec timer_value = { { 0, round(timer_period_in_nanosec) }, { 0, 1 } };
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
	struct timespec now = { 0, 0 };
	int idx_prev_sample = v_energy_numbers.size() - 1;
	bool is_duplicate = false; // Ignore duplicates in case we are supersampling
	
	READ_ENERGY(s_rapl_values);
	clock_gettime(gettime_clockid, &now);
	
	if (likely(idx_pkg_energy != -1)) {
		pkg_energy = s_rapl_values[idx_pkg_energy];
		// PKG energy should always grow between samples
		if (likely(idx_prev_sample >= 0)) {
			if (unlikely(pkg_energy == v_energy_numbers[idx_prev_sample].pkg)) {
				is_duplicate = true;
			}
		}
	}
	if (likely(idx_pp0_energy != -1)) {
		pp0_energy = s_rapl_values[idx_pp0_energy];
	}
	if (likely(idx_pp1_energy != -1)) {
		pp1_energy = s_rapl_values[idx_pp1_energy];
	}
	if (likely(idx_dram_energy != -1)) {
		dram_energy = s_rapl_values[idx_dram_energy];
		// DRAM energy should always grow between samples
		// Sometimes PKG energy updates before DRAM does, so check both
		if (likely(idx_prev_sample >= 0)) {
			if (unlikely(dram_energy == v_energy_numbers[idx_prev_sample].dram)) {
				is_duplicate = true;
			}
		}
	}
	
	if (likely(!is_duplicate)) {
		struct energy_numbers numbers = { now, pkg_energy, pp0_energy, pp1_energy, dram_energy };
		v_energy_numbers.push_back(numbers);
	}
}

static void wait_for_child() {
	FILE *fp = NULL;
	struct timespec sleep_time = { 1, 0 };
	int i;
	
	setup_timer();

	while (likely(child_pid > 0)) {
		/* Sleep until interrupted by signal */
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
	
	fp = fopen(output_file.c_str(), "w");
	if (!fp) {
		fprintf(stderr, "Error: Could not open '%s' for writing!\n", output_file.c_str());
		exit(-1);
	}
	
	fprintf(fp, "# trace-energy version %s output\n", trace_energy_version);
	// Print formatted time
	{
		char formatted_time[256] = { '\0' };
		struct tm *tmp = NULL;
		tmp = localtime(&start_time);
		if (tmp == NULL) {
			perror("localtime");
		} else {
			if (strftime(formatted_time, sizeof(formatted_time), "%a, %d %b %Y %H:%M:%S %z", tmp) == 0) {
				fprintf(stderr, "strftime returned 0");
			}
		}
		fprintf(fp, "# Capture started: %s\n", formatted_time);
	}
	// Print uname information
	{
		struct utsname info;
		memset(&info, 0, sizeof(info));
		uname(&info);
		fprintf(fp, "# System name: %s\n", info.sysname);
		fprintf(fp, "# Hostname: %s\n", info.nodename);
		fprintf(fp, "# System release: %s\n", info.release);
		fprintf(fp, "# System version: %s\n", info.version);
		fprintf(fp, "# Architecture: %s\n", info.machine);
	}
	// Get the CPU information from /proc/cpuinfo
	{
		FILE *cpuinfo = fopen("/proc/cpuinfo", "r");
		if (!cpuinfo) {
			fprintf(stderr, "Error: Failed to open /proc/cpuinfo\n");
		} else {
			char line[1024];
			while (fgets(line, sizeof(line), cpuinfo)) {
				// Find the first line containing "model name"
				if (memcmp(line, "model name", strlen("model name")) == 0) {
					char *colon = strchr(line, ':');
					if (colon && colon[1]) {
						char *model = colon + 2;
						char *newline = strchr(model, '\n');
						if (newline) *newline = '\0';
						fprintf(fp, "# CPU model: %s\n", model);
						break;
					}
				}
			}
		}
		fclose(cpuinfo);
	}
	// Use sysconf() to get the number of CPUs
	{
		long cpus_available = 0, cpus_online = 0;
		cpus_available = sysconf(_SC_NPROCESSORS_CONF);
		cpus_online = sysconf(_SC_NPROCESSORS_ONLN);
		fprintf(fp, "# CPUs available: %ld\n", cpus_available);
		fprintf(fp, "# CPUs online: %ld\n", cpus_online);
	}
	// Get total memory from /proc/meminfo
	{
		int mem_total = 0;
		FILE *meminfo = fopen("/proc/meminfo", "r");
		if (!meminfo) {
			fprintf(stderr, "Error: Failed to open /proc/meminfo\n");
		} else {
			fscanf(meminfo, "MemTotal: %d", &mem_total);
			fclose(meminfo);
			fprintf(fp, "# Total memory: %d kB\n", mem_total);
		}
	}
	// Print current working directory
	{
		char wd[PATH_MAX] = { '\0' };
		getcwd(wd, sizeof(wd));
		fprintf(fp, "# Working directory: %s\n", wd);
	}
	fprintf(fp, "# Command line: %s\n", cmdline.c_str());
	
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
	// Warmup of the signal handler etc.
	struct timespec sleep_time_warm = { 0, 1000 };
	sigalrm_handler(0);
	nanosleep(&sleep_time_warm, NULL);
	sigalrm_received = 0;
	handle_sigalrm();
	v_energy_numbers.pop_back();
}

static int process_command_line(int argc, char **argv) {
	int consumed = 0, i = 0;
	
	// Convert the command line to a string
	{
		std::ostringstream ss;
		ss << argv[0];
		for (i = 1; i < argc; i++) {
			ss << ' ';
			if (strstr(argv[i], " ") != NULL) {
				// Turn into a quoted string
				char *tmp = argv[i];
				char c;
				ss << '\'';
				while ((c = *tmp++)) {
					if (c == '\'') {
						ss << "\\'";
					} else {
						ss << c;
					}
				}
				ss << '\'';
			} else {
				ss << argv[i];
			}
		}
		cmdline = ss.str();
	}
	
	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-F") == 0) {
			if (argc > i + 1) {
				double freq = atof(argv[i + 1]);
				if (freq > 0) {
					sampling_frequency = freq;
				} else {
					fprintf(stderr, "Error: Frequency must be greater than zero\n");
				}
				i++;
				consumed += 2;
			} else {
				fprintf(stderr, "Error: Not enough arguments to -F\n");
				consumed += 1;
			}
		} else if (strcmp(argv[i], "-o") == 0) {
			if (argc > i + 1) {
				const char *name = argv[i + 1];
				output_file = name;
				i++;
				consumed += 2;
			} else {
				fprintf(stderr, "Error: Not enough arguments to -o\n");
				consumed += 1;
			}
		} else if (strcmp(argv[i], "-c") == 0) {
			if (argc > i + 1) {
				int core = atoi(argv[i + 1]);
				if (core >= 0) {
					child_cpu_affinity_core = core;
				} else {
					child_cpu_affinity_core = -1;
				}
				i++;
				consumed += 2;
			} else {
				fprintf(stderr, "Error: Not enough arguments to -c\n");
				consumed += 1;
			}
		} else if (argv[i][0] == '-') {
			fprintf(stderr, "Error: Unrecognized option '%s'\n", argv[i]);
			break;
		} else {
			break;
		}
	}
	return consumed;
}

static void do_fork_and_exec(int argc, char **argv) {
	if (argc > 1) {
		child_pid = fork();
		if (child_pid == 0) {
			if (child_cpu_affinity_core == -1) {
				do_affinity_all();
			} else {
				do_affinity(child_cpu_affinity_core);
			}
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
		fprintf(stderr, "Usage: %s [ -F <frequency> ] [ -o <output file> ] [ -c <child CPU affinity core> ] <program> [parameters]\n", argv[0]);
		exit_code = EXIT_FAILURE;
	}
}

int main(int argc, char **argv) {
	// Set affinity to core 0
	do_affinity(0);
	int args_consumed = process_command_line(argc, argv);
	v_energy_numbers.reserve(1000);
	do_signals();
	init_rapl();
	do_warmup();
	start_time = time(NULL);
	do_fork_and_exec(argc - args_consumed, argv + args_consumed);
	return exit_code;
}
