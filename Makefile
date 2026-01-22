CXX := g++
CXXFLAGS := -std=c++20 -Wall -Wextra -pedantic -I include
CXXFLAGS_DEBUG := $(CXXFLAGS) -g -O0
CXXFLAGS_RELEASE := $(CXXFLAGS) -O2

HEADERS := $(wildcard include/oram/*.hpp)

.PHONY: all clean debug release demo

all: release

release: demo

debug: CXXFLAGS := $(CXXFLAGS_DEBUG)
debug: demo

demo: examples/demo.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS_RELEASE) -o $@ $<

clean:
	rm -f demo
	rm -rf /tmp/oram_test
