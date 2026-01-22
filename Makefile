CXX := g++
CXXFLAGS := -std=c++20 -Wall -Wextra -pedantic -I include
CXXFLAGS_DEBUG := $(CXXFLAGS) -g -O0
CXXFLAGS_RELEASE := $(CXXFLAGS) -O2

HEADERS := $(wildcard include/oram/*.hpp) $(wildcard include/oram/encryption/*.hpp)

.PHONY: all clean debug release demo tests

all: release

release: demo

debug: CXXFLAGS := $(CXXFLAGS_DEBUG)
debug: demo

demo: examples/demo.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS_RELEASE) -o $@ $<

tests: oram_tests encrypted_oram_tests

oram_tests: tests/functional_oram_tests.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS_DEBUG) -o $@ $<

encrypted_oram_tests: tests/encrypted_oram_tests.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS_DEBUG) -o $@ $<

clean:
	rm -f demo
	rm -f oram_tests
	rm -f encrypted_oram_tests
	rm -rf /tmp/oram_test
