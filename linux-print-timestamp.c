/*
 * linux-print-timestamp.c
 * Print the current UNIX timestamp at nanosecond precision.
 * Author: Mikael Hirki
 */

#include <stdio.h>
#include <stdint.h>
#include <sys/time.h>
#include <time.h>

int main() {
	struct timespec res = {0, 0};
	clock_gettime(CLOCK_REALTIME, &res);
	printf("%lld.%09lld\n", (long long)res.tv_sec, (long long)res.tv_nsec);
	return 0;
}
