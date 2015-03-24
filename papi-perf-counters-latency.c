/*
* papi-perf-counters-timing.c
*
* Test the latency of reading performance counters.
*
* Based on: http://stackoverflow.com/questions/8091182/how-to-read-performance-counters-on-i5-i7-cpus
*/

#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <papi.h>

#define NUM_EVENTS 4

static double gettimeofday_double() {
	struct timeval now;
	gettimeofday(&now, NULL);
	return now.tv_sec + now.tv_usec * 1e-6;
}

int main(int /* argc */, char ** /* argv[] */)
{
	int event[NUM_EVENTS] = {PAPI_TOT_INS, PAPI_TOT_CYC, PAPI_BR_MSP, PAPI_L1_DCM };
	long long values[NUM_EVENTS];
	int i, n = 100000;
	
	/* Start counting events */
	if (PAPI_start_counters(event, NUM_EVENTS) != PAPI_OK) {
		fprintf(stderr, "PAPI_start_counters - FAILED\n");
		exit(1);
	}
	
	double start = gettimeofday_double();
	
	/* Read the counters */
	for (i = 0; i < n; i++) {
		if (PAPI_read_counters(values, NUM_EVENTS) != PAPI_OK) {
			fprintf(stderr, "PAPI_read_counters - FAILED\n");
			exit(1);
		}
	}
	
	double end = gettimeofday_double();
	
	printf("Average PAPI_read_counters() latency: %f nanoseconds\n", (end - start) / n * 1000000000.0);
	
	printf("Total instructions: %lld\n", values[0]);
	printf("Total cycles: %lld\n", values[1]);
	printf("Instr per cycle: %2.3f\n", (double)values[0] / (double) values[1]);
	printf("Branches mispredicted: %lld\n", values[2]);
	printf("L1 Cache misses: %lld\n", values[3]);
	
	/* Stop counting events */
	if (PAPI_stop_counters(values, NUM_EVENTS) != PAPI_OK) {
		fprintf(stderr, "PAPI_stoped_counters - FAILED\n");
		exit(1);
	}
	
	return 0;
} 
