/*
 * test-setitimer-resolution.cc
 * Test the value returned by getitimer() after setting a timer using setitimer().
 */

#include <stdio.h>
#include <sys/time.h>
#include <signal.h>

static void signal_handler(int sig) {
	(void)sig;
	// Ignore
}

static void check_resolution(int which, struct itimerval *request) {
	struct itimerval value = { { 0, 0 }, { 0, 0 } };
	setitimer(which, request, NULL);
	getitimer(which, &value);
	printf("Got %ld microseconds\n", value.it_interval.tv_usec);
}

static void print_current_value(int which, const char *name) {
	struct itimerval value = { { 0, 0 }, { 0, 0 } };
	getitimer(which, &value);
	printf("%s value: it_interval = %ld.%06ld, it_value = %ld.%06ld\n", name, value.it_interval.tv_sec, value.it_interval.tv_usec, value.it_value.tv_sec, value.it_value.tv_usec);
}

static void test_timer(int which, const char *name) {
	struct itimerval request = { { 0, 0 }, { 0, 1000 } };
	const struct itimerval zero = { { 0, 0 }, { 0, 0 } };
	
	printf("Testing %s\n", name);
	printf("Requesting 1 ms\n");
	request.it_interval.tv_usec = 1000;
	check_resolution(which, &request);
	printf("Requesting 2 ms\n");
	request.it_interval.tv_usec = 2000;
	check_resolution(which, &request);
	printf("Requesting 3 ms\n");
	request.it_interval.tv_usec = 3000;
	check_resolution(which, &request);
	printf("Requesting 4 ms\n");
	request.it_interval.tv_usec = 4000;
	check_resolution(which, &request);
	printf("Requesting 5 ms\n");
	request.it_interval.tv_usec = 5000;
	check_resolution(which, &request);
	setitimer(which, &zero, NULL);
}

int main() {
	signal(SIGALRM, &signal_handler);
	signal(SIGVTALRM, &signal_handler);
	signal(SIGPROF, &signal_handler);
	
	print_current_value(ITIMER_REAL, "ITIMER_REAL");
	print_current_value(ITIMER_VIRTUAL, "ITIMER_VIRTUAL");
	print_current_value(ITIMER_PROF, "ITIMER_PROF");
	
	test_timer(ITIMER_REAL, "ITIMER_REAL");
	test_timer(ITIMER_VIRTUAL, "ITIMER_VIRTUAL");
	test_timer(ITIMER_PROF, "ITIMER_PROF");
	
	return 0;
}
