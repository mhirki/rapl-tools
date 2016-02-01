/*
 * trace-energy-and-temp-msr.cc: Runs a command and produces both a energy and temperature trace of the execution.
 *
 * This tool is based on trace-energy-v2.
 * It uses the MSR driver directly.
 *
 * Compilation: g++ -Wall -Wextra -O2 -g -o trace-energy-and-temp-msr trace-energy-and-temp-msr.cc util.cc -lpapi -lrt
 *
 * Dependencies: PAPI (Performance Application Programming Interface)
 *
 * Usage: ./trace-energy-and-temp-msr [ -F <frequency> ] [ -o <output file> ] [ -c <child CPU affinity core> ] <program> [parameters]
 *
 * Author: Mikael Hirki <mikael.hirki@aalto.fi>
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
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
#include <fcntl.h>
#include <limits.h>

#include <vector>
#include <string>
#include <sstream>

#include "util.h"

#define MSR_IA32_THERM_STATUS		0x0000019c
#define MSR_IA32_TEMPERATURE_TARGET	0x000001a2
#define MSR_IA32_PACKAGE_THERM_STATUS		0x000001b1

#define MSR_PKG_ENERGY_STATUS		0x611
#define MSR_PP0_ENERGY_STATUS		0x639
#define MSR_PP1_ENERGY_STATUS		0x641
#define MSR_DRAM_ENERGY_STATUS		0x619

// Name of this program
const char *trace_temp_name = "trace-energy-and-temp-msr";

// Version string
const char *trace_temp_version = "2.2";

// Frequency can be changed using the -F command line switch
// Defaults to 200 Hz
static double sampling_frequency = 250.0;

// Output file can be changed using the -o command line switch
static std::string output_file = "energy-and-temp-trace.csv";

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
static const char *argv0 = NULL;

// Default value for critical temperate is 100 degrees C
static short tjmax = 100;

// File descriptor fore core 0 MSR file
// Right now this is hard-coded to support up to 4 cores
static int core0_fd = -1;
static int core1_fd = -1;
static int core2_fd = -1;
static int core3_fd = -1;

// Hardcoded energy unit size for Haswell
static double energyUnits = 0.00006103515625; // 0.5^14

struct temp_numbers {
	struct timespec timestamp;
	uint32_t pkg_energy;
	uint32_t pp0_energy;
	uint32_t pp1_energy;
	uint32_t dram_energy;
	short pkg_temp;
	short core0_temp;
	short core1_temp;
	short core2_temp;
	short core3_temp;
};

static std::vector<temp_numbers> v_temp_numbers;

static void sigchld_handler(int sig) {
	(void)sig;
	sigchld_received = 1;
}

static void sigalrm_handler(int sig) {
	(void)sig;
	sigalrm_received = 1;
}

static void sigint_handler(int sig) {
	if (child_pid > 0) {
		kill(child_pid, sig);
	} else {
		exit(-1);
	}
}

static void do_signals() {
	signal(SIGCHLD, &sigchld_handler);
	signal(SIGALRM, &sigalrm_handler);
	signal(SIGINT, &sigint_handler);
	signal(SIGTERM, &sigint_handler);
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

static int open_msr(int core) {
	char msr_filename[1024] = { '\0' };
	int fd = -1;
	
	snprintf(msr_filename, sizeof(msr_filename), "/dev/cpu/%d/msr", core);
	
	fd = open(msr_filename, O_RDONLY);
	if (fd < 0) {
		perror("open");
		fprintf(stderr, "open_msr failed while trying to open %s!\n", msr_filename);
		return fd;
	}
	
	return fd;
}

static bool read_msr(int fd, unsigned msr_offset, uint64_t *msr_out) {
	if (pread(fd, msr_out, sizeof(*msr_out), msr_offset) != sizeof(*msr_out)) {
		perror("pread");
		fprintf(stderr, "read_msr failed while trying to read offset 0x%04x!\n", msr_offset);
		return false;
	}
	
	return true;
}

static bool init_temp() {
	uint64_t msr_temp_target = 0;
	
	if ((core0_fd = open_msr(0)) < 0) {
		return false;
	}
	
	core1_fd = open_msr(1);
	core2_fd = open_msr(2);
	core3_fd = open_msr(3);
	
	if (read_msr(core0_fd, MSR_IA32_TEMPERATURE_TARGET, &msr_temp_target)) {
		unsigned tjmax_new = (msr_temp_target >> 16) & 0xff;
		printf("%s: TjMax is %u degrees C\n", trace_temp_name, tjmax_new);
		tjmax = tjmax_new;
	} else {
		fprintf(stderr, "Failed to read MSR_IA32_TEMPERATURE_TARGET!\n");
		fprintf(stderr, "Using the default value of %d for tjmax.", (int)tjmax);
	}
	
	return true;
}

static short read_temp(int fd, unsigned msr_offset) {
	uint64_t msr_therm_status = 0;
	
	if (read_msr(fd, msr_offset, &msr_therm_status)) {
		return tjmax - ((msr_therm_status >> 16) & 0x7f);
	} else {
		fprintf(stderr, "%s: Failed to read MSR offset 0x%04x\n", __func__, msr_offset);
		return -1;
	}
}

static uint32_t read_energy(int fd, unsigned msr_offset) {
	uint64_t energy;
	
	if (read_msr(fd, msr_offset, &energy)) {
		return energy;
	} else {
		fprintf(stderr, "%s: Failed to read MSR offset 0x%04x\n", __func__, msr_offset);
		return -1;
	}
}

static void handle_sigchld() {
	int status = 0;
	if (child_pid > 0) {
		while (waitpid(child_pid, &status, WNOHANG) > 0) {
			if (WIFEXITED(status)) {
				int child_exit_code = WEXITSTATUS(status);
				printf("%s: Child exited normally with exit code %d\n", trace_temp_name, child_exit_code);
				exit_code = child_exit_code;
				child_pid = -1;
				break;
			}
			else if (WIFSIGNALED(status)) {
				printf("%s: Child was terminated by a signal\n", trace_temp_name);
				exit_code = EXIT_FAILURE;
				child_pid = -1;
				break;
			}
		}
	}
}

static void handle_sigalrm() {
	short pkg_temp = 0, core0_temp = 0, core1_temp = 0, core2_temp = 0, core3_temp = 0;
	uint32_t pkg_energy = 0, pp0_energy = 0, pp1_energy = 0, dram_energy = 0;
	struct timespec now = { 0, 0 };
//	int idx_prev_sample = v_temp_numbers.size() - 1;
	bool is_duplicate = true; // Ignore duplicates in case we are supersampling
	
	pkg_energy = read_energy(core0_fd, MSR_PKG_ENERGY_STATUS);
	pp0_energy = read_energy(core0_fd, MSR_PP0_ENERGY_STATUS);
	pp1_energy = read_energy(core0_fd, MSR_PP1_ENERGY_STATUS);
	dram_energy = read_energy(core0_fd, MSR_DRAM_ENERGY_STATUS);
	pkg_temp = read_temp(core0_fd, MSR_IA32_PACKAGE_THERM_STATUS);
	core0_temp = read_temp(core0_fd, MSR_IA32_THERM_STATUS);
	core1_temp = read_temp(core1_fd, MSR_IA32_THERM_STATUS);
	core2_temp = read_temp(core2_fd, MSR_IA32_THERM_STATUS);
	core3_temp = read_temp(core3_fd, MSR_IA32_THERM_STATUS);
	clock_gettime(gettime_clockid, &now);
	
	/* Disabled because the energy data becomes spiky */
#if 0
	if (likely(idx_prev_sample >= 0)) {
		if (unlikely(pkg_temp != v_temp_numbers[idx_prev_sample].pkg_temp)) {
			is_duplicate = false;
		} else if (unlikely(core0_temp != v_temp_numbers[idx_prev_sample].core0_temp)) {
			is_duplicate = false;
		} else if (unlikely(core1_temp != v_temp_numbers[idx_prev_sample].core1_temp)) {
			is_duplicate = false;
		} else if (unlikely(core2_temp != v_temp_numbers[idx_prev_sample].core2_temp)) {
			is_duplicate = false;
		} else if (unlikely(core3_temp != v_temp_numbers[idx_prev_sample].core3_temp)) {
			is_duplicate = false;
		}
	} else {
		is_duplicate = false;
	}
#else
	is_duplicate = false;
#endif
	
	if (likely(!is_duplicate)) {
		struct temp_numbers numbers = { now, pkg_energy, pp0_energy, pp1_energy, dram_energy, pkg_temp, core0_temp, core1_temp, core2_temp, core3_temp };
		v_temp_numbers.push_back(numbers);
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
	
	fprintf(fp, "# %s version %s output\n", trace_temp_name, trace_temp_version);
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
		if (cpuinfo) {
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
			fclose(cpuinfo);
		} else {
			fprintf(stderr, "Warning: Failed to open /proc/cpuinfo\n");
		}
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
		if (meminfo) {
			fscanf(meminfo, "MemTotal: %d", &mem_total);
			fclose(meminfo);
			fprintf(fp, "# Total memory: %d kB\n", mem_total);
		} else {
			fprintf(stderr, "Warning: Failed to open /proc/meminfo\n");
		}
	}
	// Print current working directory
	{
		char wd[PATH_MAX] = { '\0' };
		getcwd(wd, sizeof(wd));
		fprintf(fp, "# Working directory: %s\n", wd);
	}
	fprintf(fp, "# Command line: %s\n", cmdline.c_str());
	
	const int n = v_temp_numbers.size();
	for (i = 1; i < n; i++) {
		double timestamp = v_temp_numbers[i].timestamp.tv_sec + v_temp_numbers[i].timestamp.tv_nsec * 1e-9;
		// Calculate energy deltas
		double pkg_energy = (v_temp_numbers[i].pkg_energy - v_temp_numbers[i - 1].pkg_energy) * energyUnits;
		double pp0_energy = (v_temp_numbers[i].pp0_energy - v_temp_numbers[i - 1].pp0_energy) * energyUnits;
		double pp1_energy = (v_temp_numbers[i].pp1_energy - v_temp_numbers[i - 1].pp1_energy) * energyUnits;
		double dram_energy = (v_temp_numbers[i].dram_energy - v_temp_numbers[i - 1].dram_energy) * energyUnits;
		int pkg_temp = v_temp_numbers[i].pkg_temp;
		int core0_temp = v_temp_numbers[i].core0_temp;
		int core1_temp = v_temp_numbers[i].core1_temp;
		int core2_temp = v_temp_numbers[i].core2_temp;
		int core3_temp = v_temp_numbers[i].core3_temp;
		fprintf(fp, "%.6f, %.6f, %.6f, %.6f, %.6f, %d, %d, %d, %d, %d\n", timestamp, pkg_energy, pp0_energy, pp1_energy, dram_energy, pkg_temp, core0_temp, core1_temp, core2_temp, core3_temp);
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
	v_temp_numbers.pop_back();
}

static void print_usage() {
	fprintf(stderr, "Usage: %s [ -F <frequency> ] [ -o <output file> ] [ -c <child CPU affinity core> ] <program> [parameters]\n", argv0);
	fprintf(stderr, "\n");
	fprintf(stderr, "Execute the given program as a child process and record a trace of CPU power consumption while it is running.\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "Options:\n");
	fprintf(stderr, "  -F <frequency>                  Record power consumption at a given frequency (in Hz, defaults to %.0f)\n", sampling_frequency);
	fprintf(stderr, "  -o <output file>                Write the output to a specific file (defaults to %s)\n", output_file.c_str());
	fprintf(stderr, "  -c <child CPU affinity core>    Set the affinity for the child process to a specific core\n");
	fprintf(stderr, "  -h, --help                      Display this usage information\n");
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
		} else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
			print_usage();
			exit_code = EXIT_FAILURE;
			break;
		} else if (argv[i][0] == '-') {
			fprintf(stderr, "Error: Unrecognized option '%s'\n", argv[i]);
			exit_code = EXIT_FAILURE;
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
		fprintf(stderr, "Error: Not enough parameters!\n");
		print_usage();
		exit_code = EXIT_FAILURE;
	}
}

int main(int argc, char **argv) {
	argv0 = argv[0];
	// Set our affinity to core 0 because PAPI reads the MSRs from core 0
	do_affinity(0);
	int args_consumed = process_command_line(argc, argv);
	if (exit_code != EXIT_SUCCESS) {
		return exit_code;
	}
	v_temp_numbers.reserve(1000);
	do_signals();
	if (!init_temp()) {
		return EXIT_FAILURE;
	}
	do_warmup();
	start_time = time(NULL);
	do_fork_and_exec(argc - args_consumed, argv + args_consumed);
	return exit_code;
}
