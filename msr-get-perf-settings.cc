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

#include "msr-index.h"

#define MSR_IA32_PM_ENABLE 0x770

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

static int do_affinity(int core) {
	cpu_set_t mask;
	CPU_ZERO(&mask);
	CPU_SET(core, &mask);
	int result = sched_setaffinity(0, sizeof(mask), &mask);
	return result >= 0;
}

int main(int argc, char **argv) {
	
	int fd = -1;
	int core = 0;
	int c = 0;
	uint64_t result = 0;
	int cpu_model = -1;
	
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
	
	fd=open_msr(core);
	
	// Read MSR_IA32_ENERGY_PERF_BIAS
	result = read_msr(fd, MSR_IA32_ENERGY_PERF_BIAS);
	printf("MSR_IA32_ENERGY_PERF_BIAS reads %016llx\n", (unsigned long long)result);
	
	// Read MSR_IA32_PM_ENABLE
	result = read_msr(fd, MSR_IA32_PM_ENABLE);
	printf("MSR_IA32_PM_ENABLE reads %016llx\n", (unsigned long long)result);
	
	// Kill compiler warnings
	(void)argc;
	(void)argv;
	(void)result;
	
	return 0;
}
