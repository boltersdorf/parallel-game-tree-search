# Builds the measurement driver and the game programs into build/<config>/.
#
#   make                 icpx with OpenMP, the configuration of all measurements
#   make PORTABLE=1      any C++20 compiler with OpenMP (tests, CI)
#
# abtest      every search strategy behind one driver, fixed depth, no network
# abtest-eq   the same with the table cutoff restricted to equal depth, the
#             build in which the table versions must match the reference exactly
# player, start, referee   the game over TCP, player holds every strategy
# checks      unit and soundness checks of the test suite, equal-depth build
#
# EXTRA_CXXFLAGS and EXTRA_LDFLAGS are appended, for example for sanitizers:
#   make PORTABLE=1 EXTRA_CXXFLAGS="-fsanitize=address,undefined -g" EXTRA_LDFLAGS=-fsanitize=address,undefined

PORTABLE ?= 0

ifeq ($(PORTABLE),1)
  BUILD    = build/portable
  CXX     ?= g++
  CXXFLAGS = -O2 -std=c++20
  OMPFLAGS = -fopenmp
else
  BUILD    = build/native
  CXX      = icpx
  CXXFLAGS = -O3 -fno-omit-frame-pointer -std=c++20
  OMPFLAGS = -qopenmp
endif

INC = -Isrc/framework -Isrc/search

FRAMEWORK  = src/framework/board.cpp src/framework/move.cpp src/framework/eval.cpp \
             src/framework/network.cpp src/framework/search.cpp
SEARCH     = src/search/zobrist.cpp src/search/table.cpp src/search/counters.cpp
STRATEGIES = $(wildcard src/sequential/*.cpp src/parallel/*.cpp \
                        src/parallel/cumulative/*.cpp src/parallel/isolated/*.cpp)
HEADERS    = $(wildcard src/framework/*.h src/search/*.h)

COMPILE = $(CXX) $(CXXFLAGS) $(OMPFLAGS) $(INC) $(EXTRA_CXXFLAGS)

all: $(BUILD)/abtest $(BUILD)/abtest-eq $(BUILD)/player $(BUILD)/start $(BUILD)/referee

$(BUILD):
	mkdir -p $@

$(BUILD)/abtest: $(FRAMEWORK) $(SEARCH) $(STRATEGIES) src/driver/abtest.cpp $(HEADERS) | $(BUILD)
	$(COMPILE) $(FRAMEWORK) $(SEARCH) $(STRATEGIES) src/driver/abtest.cpp -o $@ $(EXTRA_LDFLAGS)

$(BUILD)/abtest-eq: $(FRAMEWORK) $(SEARCH) $(STRATEGIES) src/driver/abtest.cpp $(HEADERS) | $(BUILD)
	$(COMPILE) -DTT_DEPTH_EQ $(FRAMEWORK) $(SEARCH) $(STRATEGIES) src/driver/abtest.cpp -o $@ $(EXTRA_LDFLAGS)

$(BUILD)/checks: $(FRAMEWORK) $(SEARCH) $(STRATEGIES) tests/checks.cpp $(HEADERS) | $(BUILD)
	$(COMPILE) -DTT_DEPTH_EQ $(FRAMEWORK) $(SEARCH) $(STRATEGIES) tests/checks.cpp -o $@ $(EXTRA_LDFLAGS)

$(BUILD)/player: $(FRAMEWORK) $(SEARCH) $(STRATEGIES) src/engine/timed_engine.cpp src/framework/player.cpp $(HEADERS) | $(BUILD)
	$(COMPILE) $(FRAMEWORK) $(SEARCH) $(STRATEGIES) src/engine/timed_engine.cpp src/framework/player.cpp -o $@ $(EXTRA_LDFLAGS)

$(BUILD)/start: $(FRAMEWORK) src/framework/start.cpp $(HEADERS) | $(BUILD)
	$(COMPILE) $(FRAMEWORK) src/framework/start.cpp -o $@ $(EXTRA_LDFLAGS)

$(BUILD)/referee: $(FRAMEWORK) src/framework/referee.cpp $(HEADERS) | $(BUILD)
	$(COMPILE) $(FRAMEWORK) src/framework/referee.cpp -o $@ $(EXTRA_LDFLAGS)

test: $(BUILD)/abtest $(BUILD)/abtest-eq $(BUILD)/checks
	python3 tests/run_tests.py --build $(BUILD)

clean:
	rm -rf build

.PHONY: all test clean
.DEFAULT_GOAL := all
