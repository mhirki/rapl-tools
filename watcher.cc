/*
 * watcher.cc: Runs a command and monitors it.
 *
 * The child program is terminated if it doesn't spend enough user and system time.
 *
 * Exit code will be EXIT_FAILURE if the child gets terminated by a signal.
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>

static pid_t child_pid = -1;
static int exit_code = EXIT_SUCCESS;

static void sigchld_handler(int sig) {
	(void)sig;
	int status = 0;
	if (child_pid > 0) {
		int rval = waitpid(child_pid, &status, WNOHANG);
		if (rval < 0) {
			perror("waitpid");
		} else if (rval > 0) {
			if (WIFEXITED(status)) {
				int child_exit_code = WEXITSTATUS(status);
				printf("Watcher: Child exited normally with exit code %d\n", child_exit_code);
				exit_code = child_exit_code;
				child_pid = -1;
			}
			else if (WIFSIGNALED(status)) {
				printf("Watcher: Child was terminated by a signal\n");
				exit_code = EXIT_FAILURE;
				child_pid = -1;
			}
		}
	}
}

static void do_signals() {
	signal(SIGCHLD, &sigchld_handler);
}

static bool read_child_stats(double *double_utime, double *double_stime) {
	char filename[256];
	snprintf(filename, sizeof(filename), "/proc/%d/stat", (int)child_pid);
	/* Open the stat file for the child process */
	FILE *fp = fopen(filename, "r");
	if (!fp) {
		*double_utime = 0;
		*double_stime = 0;
		return false;
	}
	
	long clk_ticks = sysconf(_SC_CLK_TCK);
	double tick_period = 1.0 / clk_ticks;
	
	int pid;
	char comm[1025];
	char state;
	int ppid, pgrp, session, tty_nr, tpgid;
	unsigned flags;
	unsigned long minflt, cminflt, majflt, cmajflt, utime, stime;
	/* Parse up to 'stime' */
	fscanf(fp, "%d %1024s %c %d %d %d %d %d %u %lu %lu %lu %lu %lu %lu",
	       &pid, comm, &state, &ppid, &pgrp, &session, &tty_nr, &tpgid, &flags,
	       &minflt, &cminflt, &majflt, &cmajflt, &utime, &stime);
	fclose(fp);
	
	*double_utime = utime * tick_period;
	*double_stime = stime * tick_period;
	
	return true;
}

static void monitor_child() {
	int rval = 0;
	double prev_user_and_sys = -1.0;
	const double signal_threshold = 0.1;
	bool child_signaled = false;
	struct timespec sleep_time = { 1, 0 };
	struct timespec sleep_remaining = { 0, 0 };

	while (child_pid > 0) {
		double user_time = 0;
		double sys_time = 0;
		read_child_stats(&user_time, &sys_time);
		// For debugging
		//printf("user = %f, sys = %f\n", user_time, sys_time);
		double user_and_sys = user_time + sys_time;
		if (user_and_sys - prev_user_and_sys < signal_threshold) {
			if (!child_signaled) {
				fprintf(stderr, "Watcher: Sending SIGTERM\n");
				rval = kill(child_pid, SIGTERM);
				if (rval < 0) {
					perror("kill");
				}
				child_signaled = true;
			} else {
				fprintf(stderr, "Watcher: Sending SIGKILL\n");
				kill(child_pid, SIGKILL);
				if (rval < 0) {
					perror("kill");
				}
			}
		}
		prev_user_and_sys = user_and_sys;
		
		/* Sleep for one second */
		rval = nanosleep(&sleep_time, &sleep_remaining);
		if (rval < 0) {
			if (errno == EINTR) {
				/* Retry sleeping in case of interrupts */
				do {
					/* Exit immediately if child has terminated */
					if (child_pid < 0) return;
					rval = nanosleep(&sleep_remaining, &sleep_remaining);
				} while (rval < 0 && errno == EINTR);
			}
			/* Handle other nanosleep errors */
			if (rval < 0 && errno != EINTR) {
				perror("nanosleep");
			}
		}
	}
}

static void do_fork_and_exec(int argc, char **argv) {
	if (argc > 1) {
		child_pid = fork();
		if (child_pid == 0) {
			execvp(argv[1], &argv[1]);
			perror("execlp");
			exit(-1);
		} else if (child_pid < 0) {
			perror("fork");
		} else {
			monitor_child();
		}
	} else {
		printf("Usage: %s <program> [parameters]\n", argv[0]);
	}
}

int main(int argc, char **argv) {
	do_signals();
	do_fork_and_exec(argc, argv);
	return exit_code;
}
