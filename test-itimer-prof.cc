/*
 * test-itimer-prof.cc
 * Test how well ITIMER_PROF works by counting the number of signals during a one second interval.
 *
 * Author: Mikael Hirki <mikael.hirki@aalto.fi>
 */

#include <stdio.h>
#include <sys/time.h>
#include <signal.h>

static int num_signals = 0;

static void signal_handler(int sig) {
	(void)sig;
	num_signals++;
}

static double timeval_to_double(struct timeval *tv) {
	return tv->tv_sec + tv->tv_usec * 1e-6;
}

int main() {
	const struct itimerval request = { { 0, 5000 }, { 0, 1 } };
	struct timeval now;
	
	// Set up signal handler
	signal(SIGPROF, &signal_handler);
	
	// Set up timer
	setitimer(ITIMER_PROF, &request, NULL);
	
	gettimeofday(&now, NULL);
	double tstart = timeval_to_double(&now);
	// Busy loop for 1 second
	while (true) {
		gettimeofday(&now, NULL);
		double tnow = timeval_to_double(&now);
		if (tnow - tstart >= 1.0) break;
	}
	
	printf("%d SIGPROF signals caught in 1 second\n", num_signals);
	
	return 0;
}
