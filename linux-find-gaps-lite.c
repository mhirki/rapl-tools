/*
 * linux-find-gaps.c
 * Find any gaps in the execution of a busy loop program.
 * Goal is to potentially find signs of System Management Mode (SMM).
 * This the "lite" version which doesn't store any arrays in memory.
 *
 * Author: Mikael Hirki <mikael.hirki@aalto.fi>
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#define NUM_ITERATIONS 8000000ULL

#if __x86_64__ || __i386__
#define HAVE_RDTSC
#define RDTSC(v)							\
  do { unsigned lo, hi;							\
    __asm__ volatile("rdtsc" : "=a" (lo), "=d" (hi));			\
    (v) = ((uint64_t) lo) | ((uint64_t) hi << 32);			\
  } while (0)
#endif

int main() {
#ifdef HAVE_RDTSC
	uint64_t tsc = 0, prev_tsc = 0;
	unsigned long long i = 0;
	RDTSC(tsc);
	prev_tsc = tsc;
	// Statistics
	double gaps_sum = 0;
	uint64_t min_gap = -1LL;
	uint64_t max_gap = 0;
	for (i = 0; i < NUM_ITERATIONS; i++) {
		RDTSC(tsc);
		uint64_t gap = tsc - prev_tsc;
		prev_tsc = tsc;
		gaps_sum += gap;
		if (gap < min_gap) min_gap = gap;
		if (gap > max_gap) max_gap = gap;
	}
	double avg_gap = gaps_sum / NUM_ITERATIONS;
	printf("Avg gap = %f cycles, min gap = %llu cycles, max gap = %llu cycles\n", avg_gap, (unsigned long long)min_gap, (unsigned long long)max_gap);
#else
	printf("RDTSC only works on x86 platforms!\n");
#endif
	return 0;
}
