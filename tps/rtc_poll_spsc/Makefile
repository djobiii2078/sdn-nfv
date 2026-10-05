CXX ?= g++
CXXFLAGS ?= -O3 -std=c++17 -Wall -Wextra -pthread
all: netbench
netbench: netbench.cpp
	$(CXX) $(CXXFLAGS) $< -o $@
clean:
	rm -f netbench
.PHONY: all clean
