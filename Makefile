CFLAGS = -Wall -Wextra -O2 -g
CC = g++
LIBS_PAPI = -lpapi

BINARY_TARGETS = papi-poll-gaps papi-poll-energy papi-poll-pkg get-energy linux-test-clocks linux-print-clocks papi-poll-latency papi-poll-perf-latency msr-poll-latency msr-get-core-voltage papi-poll-timings papi-poll-tsc-gaps papi-poll-latency-multiple papi-measure-instruction papi-list-components papi-list-perf-events papi-measure-exp papi-measure-malloc papi-measure-calloc test-setitimer-resolution test-itimer-prof watcher trace-energy trace-energy-1khz trace-energy-time papi-perf-counters papi-perf-counters-latency

all: $(BINARY_TARGETS)

clean:
	rm -f $(BINARY_TARGETS)

papi-poll-gaps: papi-poll-gaps.cc util.cc
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS_PAPI)

papi-poll-energy: papi-poll-energy.cc util.cc
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS_PAPI)

papi-poll-pkg: papi-poll-pkg.cc util.cc
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS_PAPI)

get-energy: get-energy.cc
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS_PAPI)

linux-test-clocks: linux-test-clocks.cc
	$(CC) $(CFLAGS) -o $@ $^ -lrt

linux-print-clocks: linux-print-clocks.cc
	$(CC) $(CFLAGS) -o $@ $^ -lrt

papi-poll-latency: papi-poll-latency.cc util.cc
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS_PAPI)

papi-poll-perf-latency: papi-poll-perf-latency.cc util.cc
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS_PAPI)

papi-poll-latency-multiple: papi-poll-latency-multiple.cc util.cc
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS_PAPI)

papi-poll-timings: papi-poll-timings.cc util.cc
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS_PAPI) -lrt

papi-poll-tsc-gaps: papi-poll-tsc-gaps.cc util.cc
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS_PAPI)

msr-poll-latency: msr-poll-latency.cc
	$(CC) $(CFLAGS) -o $@ $^

msr-get-core-voltage: msr-get-core-voltage.cc
	$(CC) $(CFLAGS) -o $@ $^

papi-measure-instruction: papi-measure-instruction.cc util.cc
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS_PAPI)

papi-measure-exp: papi-measure-exp.cc util.cc
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS_PAPI) -lm

papi-measure-malloc: papi-measure-malloc.cc util.cc
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS_PAPI)

papi-measure-calloc: papi-measure-calloc.cc util.cc
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS_PAPI)

papi-list-components: papi-list-components.cc
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS_PAPI)

papi-list-perf-events: papi-list-perf-events.cc
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS_PAPI)

test-setitimer-resolution: test-setitimer-resolution.cc
	$(CC) $(CFLAGS) -o $@ $^

test-itimer-prof: test-itimer-prof.cc
	$(CC) $(CFLAGS) -o $@ $^

watcher: watcher.cc
	$(CC) $(CFLAGS) -o $@ $^

trace-energy: trace-energy.cc util.cc
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS_PAPI)

trace-energy-1khz: trace-energy-1khz.cc util.cc
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS_PAPI)

trace-energy-time: trace-energy-time.cc util.cc
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS_PAPI) -lrt

papi-perf-counters: papi-perf-counters.c
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS_PAPI)

papi-perf-counters-latency: papi-perf-counters-latency.c
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS_PAPI)