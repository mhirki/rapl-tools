/*
 * linux-print-clocks.c
 * Print out the values of various clock sources.
 * Author: Mikael Hirki
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

int main() {
#ifdef HAVE_RDTSC
	uint64_t tsc = 0;
	RDTSC(tsc);
	printf("%lld\n", (long long)tsc);
#else
	printf("RDTSC only works on x86 platforms!\n");
#endif
	return 0;
}
