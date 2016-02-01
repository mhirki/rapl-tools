CFLAGS = -Wall -Wextra -O2 -g
CXXFLAGS = $(CFLAGS)
CC = gcc
CXX = g++
LIBS_PAPI = -lpapi
LDFLAGS = -Wl,-z,now

BINARY_TARGETS = papi-poll-gaps papi-poll-energy papi-poll-pkg get-energy linux-test-clocks linux-print-clocks linux-print-timestamp linux-print-tsc papi-poll-latency papi-poll-perf-latency msr-poll-atomicity msr-poll-atomicity-high-accuracy msr-poll-gaps msr-poll-gaps-nsec msr-poll-gaps-nsec-and-power msr-poll-latency msr-get-core-voltage msr-get-perf-bias msr-set-perf-bias papi-poll-timings papi-poll-tsc-gaps papi-poll-latency-multiple papi-measure-instruction papi-list-components papi-list-perf-events papi-measure-exp papi-measure-malloc papi-measure-calloc test-setitimer-resolution test-itimer-prof watcher trace-energy trace-energy-1khz trace-energy-with-time trace-energy-v2 trace-temp-msr trace-energy-and-temp-msr papi-perf-counters papi-perf-counters-latency linux-find-gaps linux-find-gaps-lite linux-pread-latency gaps-stats

all: $(BINARY_TARGETS)

clean:
	rm -f $(BINARY_TARGETS)

.PHONY: all clean

papi-poll-gaps: papi-poll-gaps.cc util.cc
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^ $(LIBS_PAPI)

papi-poll-energy: papi-poll-energy.cc util.cc
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^ $(LIBS_PAPI)

papi-poll-pkg: papi-poll-pkg.cc util.cc
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^ $(LIBS_PAPI)

get-energy: get-energy.cc
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^ $(LIBS_PAPI)

linux-find-gaps: linux-find-gaps.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ -lm

linux-test-clocks: linux-test-clocks.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ -lrt

linux-print-clocks: linux-print-clocks.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ -lrt

linux-print-timestamp: linux-print-timestamp.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ -lrt

msr-poll-atomicity: msr-poll-atomicity.cc
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^ -lrt

msr-poll-atomicity-high-accuracy: msr-poll-atomicity-high-accuracy.cc
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^ -lrt

msr-poll-gaps-nsec: msr-poll-gaps-nsec.cc
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^ -lrt

msr-poll-gaps-nsec-and-power: msr-poll-gaps-nsec-and-power.cc
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^ -lrt

papi-poll-latency: papi-poll-latency.cc util.cc
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^ $(LIBS_PAPI)

papi-poll-perf-latency: papi-poll-perf-latency.cc util.cc
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^ $(LIBS_PAPI)

papi-poll-latency-multiple: papi-poll-latency-multiple.cc util.cc
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^ $(LIBS_PAPI)

papi-poll-timings: papi-poll-timings.cc util.cc
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^ $(LIBS_PAPI) -lrt

papi-poll-tsc-gaps: papi-poll-tsc-gaps.cc util.cc
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^ $(LIBS_PAPI)

papi-measure-instruction: papi-measure-instruction.cc util.cc
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^ $(LIBS_PAPI)

papi-measure-exp: papi-measure-exp.cc util.cc
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^ $(LIBS_PAPI) -lm

papi-measure-malloc: papi-measure-malloc.cc util.cc
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^ $(LIBS_PAPI)

papi-measure-calloc: papi-measure-calloc.cc util.cc
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^ $(LIBS_PAPI)

papi-list-components: papi-list-components.cc
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^ $(LIBS_PAPI)

papi-list-perf-events: papi-list-perf-events.cc
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^ $(LIBS_PAPI)

trace-energy: trace-energy.cc util.cc
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^ $(LIBS_PAPI)

trace-energy-1khz: trace-energy-1khz.cc util.cc
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^ $(LIBS_PAPI)

trace-energy-with-time: trace-energy-with-time.cc util.cc
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^ $(LIBS_PAPI) -lrt

trace-energy-v2: trace-energy-v2.cc util.cc
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^ $(LIBS_PAPI) -lrt

trace-temp-msr: trace-temp-msr.cc util.cc
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^ -lrt

trace-energy-and-temp-msr: trace-energy-and-temp-msr.cc util.cc
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^ -lrt

papi-perf-counters: papi-perf-counters.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LIBS_PAPI)

papi-perf-counters-latency: papi-perf-counters-latency.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LIBS_PAPI)
