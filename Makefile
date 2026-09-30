CXX := g++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -Iinclude -I.

.PHONY: all clean test experiments

all: capability_embedding test_runner

capability_embedding: src/main.cpp include/*.hpp
	$(CXX) $(CXXFLAGS) src/main.cpp -o capability_embedding

test_runner: tests/test_cases.cpp include/*.hpp
	$(CXX) $(CXXFLAGS) tests/test_cases.cpp -o test_runner

test: test_runner
	./test_runner

experiments: capability_embedding
	mkdir -p results
	./capability_embedding --experiments

clean:
	rm -f capability_embedding test_runner
	rm -f results/*.csv
