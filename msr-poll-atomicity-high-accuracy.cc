/* 
 * msr-poll-atomicity-high-accuracy.cc
 * Attempt to detect signs of non-atomicity, i.e. one of the RAPL registers gets updated before the others.
 * 
 * This version doubles the accuracy by splitting the polling loop into two
 * subloops. This works properly only if the second register is always updated
 * after the first register.
 *
 * Author: Mikael Hirki <mikael.hirki@aalto.fi>
 */

#include <vector>
#include <time.h>

/* The number of iterations to poll */
#define MAX_UPDATES 10000

/* Read the RAPL registers on a sandybridge-ep machine                */
/* Code based on Intel RAPL driver by Zhang Rui <rui.zhang@intel.com> */
/*                                                                    */
/* The /dev/cpu/??/msr driver must be enabled and permissions set     */
/* to allow read access for this to work.                             */
/*                                                                    */
/* Code to properly get this info from Linux through a real device    */
/*   driver and the perf tool should be available as of Linux 3.14    */
/* Compile with:   gcc -O2 -Wall -o rapl-read rapl-read.c -lm         */
/*                                                                    */
/* Vince Weaver -- vincent.weaver @ maine.edu -- 29 November 2013     */
/*                                                                    */
/* Additional contributions by:                                       */
/*   Romain Dolbeau -- romain @ dolbeau.org                           */
/*                                                                    */
/* Latency polling modification by:                                   */
/*   Mikael Hirki <mikael.hirki@aalto.fi>                             */

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <errno.h>
#include <inttypes.h>
#include <unistd.h>
#include <math.h>
#include <string.h>
#include <sched.h>

#define MSR_RAPL_POWER_UNIT		0x606

/*
 * Platform specific RAPL Domains.
 * Note that PP1 RAPL Domain is supported on 062A only
 * And DRAM RAPL Domain is supported on 062D only
 */
/* Package RAPL Domain */
#define MSR_PKG_RAPL_POWER_LIMIT	0x610
#define MSR_PKG_ENERGY_STATUS		0x611
#define MSR_PKG_PERF_STATUS		0x613
#define MSR_PKG_POWER_INFO		0x614

/* PP0 RAPL Domain */
#define MSR_PP0_POWER_LIMIT		0x638
#define MSR_PP0_ENERGY_STATUS		0x639
#define MSR_PP0_POLICY			0x63A
#define MSR_PP0_PERF_STATUS		0x63B

/* PP1 RAPL Domain, may reflect to uncore devices */
#define MSR_PP1_POWER_LIMIT		0x640
#define MSR_PP1_ENERGY_STATUS		0x641
#define MSR_PP1_POLICY			0x642

/* DRAM RAPL Domain */
#define MSR_DRAM_POWER_LIMIT		0x618
#define MSR_DRAM_ENERGY_STATUS		0x619
#define MSR_DRAM_PERF_STATUS		0x61B
#define MSR_DRAM_POWER_INFO		0x61C

/* RAPL UNIT BITMASK */
#define POWER_UNIT_OFFSET	0
#define POWER_UNIT_MASK		0x0F

#define ENERGY_UNIT_OFFSET	0x08
#define ENERGY_UNIT_MASK	0x1F00

#define TIME_UNIT_OFFSET	0x10
#define TIME_UNIT_MASK		0xF000

static int open_msr(int core) {
	
	char msr_filename[BUFSIZ];
	int fd;
	
	sprintf(msr_filename, "/dev/cpu/%d/msr", core);
	fd = open(msr_filename, O_RDONLY);
	if ( fd < 0 ) {
		if ( errno == ENXIO ) {
			fprintf(stderr, "rdmsr: No CPU %d\n", core);
			exit(2);
		} else if ( errno == EIO ) {
			fprintf(stderr, "rdmsr: CPU %d doesn't support MSRs\n", core);
			exit(3);
		} else {
			perror("rdmsr:open");
			fprintf(stderr,"Trying to open %s\n",msr_filename);
			exit(127);
		}
	}
	
	return fd;
}

static uint64_t read_msr(int fd, int which) {
	uint64_t data;
	
	if (pread(fd, &data, sizeof(data), which) != sizeof(data)) {
		perror("rdmsr:pread");
		exit(127);
	}
	
	return data;
}

static int do_affinity(int core) {
	cpu_set_t mask;
	CPU_ZERO(&mask);
	CPU_SET(core, &mask);
	int result = sched_setaffinity(0, sizeof(mask), &mask);
	return result >= 0;
}

static void timedelta(struct timespec *result, struct timespec *a, struct timespec *b) {
	time_t sec_delta = a->tv_sec - b->tv_sec;
	long nsec_delta = a->tv_nsec - b->tv_nsec;
	if (nsec_delta < 0) {
		sec_delta--;
		nsec_delta += 1000000000L;
	}
	result->tv_sec = sec_delta;
	result->tv_nsec = nsec_delta;
}

static double timespec_to_double(struct timespec *a) {
	return a->tv_sec + a->tv_nsec * 1e-9;
}

int main(int argc, char **argv) {
	int fd = -1;
	int core = 0;
	int c = 0;
	
	opterr=0;
	
	while ((c = getopt (argc, argv, "c:t:")) != -1) {
		switch (c)
		{
			case 'c':
				core = atoi(optarg);
				break;
			default:
				exit(-1);
		}
	}
	
	do_affinity(core);
	
	fd=open_msr(core);
	
	// Order for checking RAPL registers
	const int first_register = MSR_PKG_ENERGY_STATUS; const int second_register = MSR_DRAM_ENERGY_STATUS;
	//const int first_register = MSR_PKG_ENERGY_STATUS; const int second_register = MSR_PP0_ENERGY_STATUS;
	//const int first_register = MSR_DRAM_ENERGY_STATUS; const int second_register = MSR_PKG_ENERGY_STATUS;
	//const int first_register = MSR_DRAM_ENERGY_STATUS; const int second_register = MSR_PP0_ENERGY_STATUS;
	//const int first_register = MSR_PP0_ENERGY_STATUS; const int second_register = MSR_PKG_ENERGY_STATUS;
	//const int first_register = MSR_PP0_ENERGY_STATUS; const int second_register = MSR_DRAM_ENERGY_STATUS;
	
	// Benchmark MSR register reads
	std::vector<double> first_update_times;
	std::vector<double> second_update_times;
	first_update_times.reserve(MAX_UPDATES);
	second_update_times.reserve(MAX_UPDATES);
	uint64_t prev_energy_first = read_msr(fd, first_register);
	uint64_t prev_energy_second = read_msr(fd, second_register);
	struct timespec tstart = {0, 0};
	clock_gettime(CLOCK_REALTIME, &tstart);
	struct timespec tnow = {0, 0};
	struct timespec tdelta = {0, 0};
	uint64_t first_energy = 0;
	uint64_t second_energy = 0;
	long num_updates = 0;
	long i;
	for (i = 0; num_updates < MAX_UPDATES; i++) {
		/* Poll only one register at a time for higher accuracy */
		while (1) {
			first_energy = read_msr(fd, first_register);
			if (first_energy != prev_energy_first) {
				clock_gettime(CLOCK_REALTIME, &tnow);
				timedelta(&tdelta, &tnow, &tstart);
				first_update_times.push_back(timespec_to_double(&tdelta));
				prev_energy_first = first_energy;
				num_updates = std::min(first_update_times.size(), second_update_times.size());
				break;
			}
		}
		while (1) {
			second_energy = read_msr(fd, second_register);
			if (second_energy != prev_energy_second) {
				clock_gettime(CLOCK_REALTIME, &tnow);
				timedelta(&tdelta, &tnow, &tstart);
				second_update_times.push_back(timespec_to_double(&tdelta));
				prev_energy_second = second_energy;
				num_updates = std::min(first_update_times.size(), second_update_times.size());
				break;
			}
		}
	}
	
	// Dump results to a file
	const char *filename = "atomicity-timings.csv";
	FILE *fp = fopen(filename, "w");
	if (!fp) {
		fprintf(stderr, "Failed to open file \"%s\" for writing!\n", filename);
		exit(EXIT_FAILURE);
	}
	for (i = 0; i < MAX_UPDATES; i++) {
		fprintf(fp, "%.9f, %.9f, %.9f\n", first_update_times[i], second_update_times[i], second_update_times[i] - first_update_times[i]);
	}
	fclose(fp);
	
	// Kill compiler warnings
	(void)argc;
	(void)argv;
	
	return 0;
}
