/*
 * linux-test-clocks.cc
 * Test various clock sources available through clock_gettime().
 */

#include <stdio.h>
#include <stdint.h>
#include <sys/time.h>
#include <time.h>

#if __x86_64__ || __i386__
#define HAVE_RDTSC
#define RDTSC(v)							\
  do { unsigned lo, hi;							\
    __asm__ volatile("rdtsc" : "=a" (lo), "=d" (hi));			\
    (v) = ((uint64_t) lo) | ((uint64_t) hi << 32);			\
  } while (0)
#endif

clockid_t clk_ids[] = {
	CLOCK_REALTIME,
	CLOCK_REALTIME_COARSE,
	CLOCK_MONOTONIC,
	CLOCK_MONOTONIC_COARSE,
	CLOCK_MONOTONIC_RAW,
#ifdef CLOCK_BOOTTIME
	CLOCK_BOOTTIME,
#endif
	CLOCK_PROCESS_CPUTIME_ID,
	CLOCK_THREAD_CPUTIME_ID,
};

const char *clk_names[] = {
	"CLOCK_REALTIME",
	"CLOCK_REALTIME_COARSE",
	"CLOCK_MONOTONIC",
	"CLOCK_MONOTONIC_COARSE",
	"CLOCK_MONOTONIC_RAW",
#ifdef CLOCK_BOOTTIME
	"CLOCK_BOOTTIME",
#endif
	"CLOCK_PROCESS_CPUTIME_ID",
	"CLOCK_THREAD_CPUTIME_ID",
};

const int num_clocks = sizeof(clk_ids) / sizeof(*clk_ids);
const int num_iterations = 10000000;

int main() {
	int i = 0;
	struct timespec res = {0, 0};
	struct timeval now = {0, 0};
	printf("Current values\n");
	printf("==============\n\n");
	
	for (i = 0; i < num_clocks; i++) {
		clock_gettime(clk_ids[i], &res);
		printf("%s : %lld.%09lld\n", clk_names[i], (long long)res.tv_sec, (long long)res.tv_nsec);
	}
	
	// Special case: gettimeofday
	gettimeofday(&now, NULL);
	printf("gettimeofday : %lld.%09lld\n", (long long)now.tv_sec, ((long long)now.tv_usec) * 1000);
	
#ifdef HAVE_RDTSC
	uint64_t tsc = 0;
	RDTSC(tsc);
	printf("RDTSC : %lld\n", (long long)tsc);
#endif
	return 0;
}
