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

#if 1
static uint64_t read_msr(int fd, int which) {
	uint64_t data;
	
	if (pread(fd, &data, sizeof(data), which) != sizeof(data)) {
		perror("rdmsr:pread");
		exit(127);
	}
	
	return data;
}
#else
static uint64_t read_msr(int fd, int which) {
	uint64_t data;
	
	if (lseek(fd, which, SEEK_SET) == -1) {
		perror("lseek");
		exit(127);
	}
	
	if (read(fd, &data, sizeof(data)) != sizeof(data)) {
		perror("read");
		exit(127);
	}
	
	return data;
}
#endif

#define CPU_SANDYBRIDGE		42
#define CPU_SANDYBRIDGE_EP	45
#define CPU_IVYBRIDGE		58
#define CPU_IVYBRIDGE_EP	62
#define CPU_HASWELL		60

static int detect_cpu(void) {
	
	FILE *fff;
	
	int family,model=-1;
	char buffer[BUFSIZ],*result;
	char vendor[BUFSIZ];
	
	fff=fopen("/proc/cpuinfo","r");
	if (fff==NULL) return -1;
	
	while(1) {
		result=fgets(buffer,BUFSIZ,fff);
		if (result==NULL) break;
		
		if (!strncmp(result,"vendor_id",8)) {
			sscanf(result,"%*s%*s%s",vendor);
			
			if (strncmp(vendor,"GenuineIntel",12)) {
				printf("%s not an Intel chip\n",vendor);
				return -1;
			}
		}
		
		if (!strncmp(result,"cpu family",10)) {
			sscanf(result,"%*s%*s%*s%d",&family);
			if (family!=6) {
				printf("Wrong CPU family %d\n",family);
				return -1;
			}
		}
		
		if (!strncmp(result,"model",5)) {
			sscanf(result,"%*s%*s%d",&model);
		}
		
	}
	
	fclose(fff);
	
	switch(model) {
		case CPU_SANDYBRIDGE:
			printf("Found Sandybridge CPU\n");
			break;
		case CPU_SANDYBRIDGE_EP:
			printf("Found Sandybridge-EP CPU\n");
			break;
		case CPU_IVYBRIDGE:
			printf("Found Ivybridge CPU\n");
			break;
		case CPU_IVYBRIDGE_EP:
			printf("Found Ivybridge-EP CPU\n");
			break;
		case CPU_HASWELL:
			printf("Found Haswell CPU\n");
			break;
		default:	printf("Unsupported model %d\n",model);
		model=-1;
		break;
	}
	
	return model;
}

#define RAPL_HAVE_PKG_ENERGY_STATUS		0x0001
#define RAPL_HAVE_PP0_ENERGY_STATUS		0x0002
#define RAPL_HAVE_PP1_ENERGY_STATUS		0x0004
#define RAPL_HAVE_DRAM_ENERGY_STATUS		0x0008
#define RAPL_HAVE_PKG_PERF_STATUS		0x0010
#define RAPL_HAVE_PP0_PERF_STATUS		0x0020
#define RAPL_HAVE_PP1_PERF_STATUS		0x0040
#define RAPL_HAVE_DRAM_PERF_STATUS		0x0080

static unsigned detect_rapl(int cpu_model) {
	unsigned capab = (RAPL_HAVE_PKG_ENERGY_STATUS | RAPL_HAVE_PP0_ENERGY_STATUS);
	
	/* only available on *Bridge-EP */
	if ((cpu_model==CPU_SANDYBRIDGE_EP) || (cpu_model==CPU_IVYBRIDGE_EP)) {
		capab |= (RAPL_HAVE_PKG_PERF_STATUS | RAPL_HAVE_PP0_PERF_STATUS);
	}
	
	/* not available on *Bridge-EP */
	if ((cpu_model==CPU_SANDYBRIDGE) || (cpu_model==CPU_IVYBRIDGE) || (cpu_model==CPU_HASWELL)) {
		capab |= RAPL_HAVE_PP1_ENERGY_STATUS;
	}
	
	/* Despite documentation saying otherwise, it looks like */
	/* You can get DRAM readings on regular Haswell          */
	if ((cpu_model==CPU_SANDYBRIDGE_EP) || (cpu_model==CPU_IVYBRIDGE_EP) || (cpu_model==CPU_HASWELL)) {
		capab |= RAPL_HAVE_DRAM_ENERGY_STATUS;
	}
	
	return capab;
}

static int do_affinity(int core) {
	cpu_set_t mask;
	CPU_ZERO(&mask);
	CPU_SET(core, &mask);
	int result = sched_setaffinity(0, sizeof(mask), &mask);
	return result >= 0;
}

static double gettimeofday_double() {
	struct timeval now;
	gettimeofday(&now, NULL);
	return now.tv_sec + now.tv_usec * 0.000001;
}

int main(int argc, char **argv) {
	
	int fd = -1;
	int core = 0;
	int c = 0;
	uint64_t result = 0;
	int cpu_model = -1;
	unsigned capab = 0;
	int i = 0;
	int num_iterations = 1000000;
	
	opterr=0;
	
	while ((c = getopt (argc, argv, "c:")) != -1) {
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
	
	cpu_model=detect_cpu();
	if (cpu_model<0) {
		printf("Unsupported CPU type\n");
		return -1;
	}
	
	capab = detect_rapl(cpu_model);
	
	fd=open_msr(core);
	
	// Benchmark MSR register reads
	double tbegin = gettimeofday_double();
	for (i = 0; i < num_iterations; i++) {
		result = read_msr(fd, MSR_PKG_ENERGY_STATUS);
	}
	double tend = gettimeofday_double();
	
	printf("MSR read latency: %f nanosecond\n", (tend - tbegin) * 1000000000.0 / num_iterations);
	
	// Kill compiler warnings
	(void)argc;
	(void)argv;
	(void)capab;
	(void)result;
	
	return 0;
}
