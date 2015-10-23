/*
 * Measure the average latency of calling pread.
 *
 * Author: Mikael Hirki <mikael.hirki@aalto.fi>
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/time.h>
#include <fcntl.h>
#include <stdio.h>

int do_read(int fd) {
	char buf[8];
	return pread(fd, buf, 8, 0);
}

static double gettimeofday_double() {
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return tv.tv_sec + tv.tv_usec * 1e-6;
}

int main() {
	double start = gettimeofday_double();
	int i = 0, iterations = 1000000;
	int fd = open("/dev/zero", O_RDONLY);
	for (i = 0; i < iterations; i++) {
		do_read(fd);
	}
	double end = gettimeofday_double();
	close(fd);
	printf("%d iterations in %f seconds\n", iterations, (end - start));
	printf("pread latency: %f nanoseconds\n", (end - start) / iterations * 1e9);
	return 0;
}
