# Makefile for the Transport Protocol Forensics Lab (self-contained)
# Builds the event-driven emulator plus the black-box transport implementation.
#
# Files produced by this package:
#   - emulator.cpp         (event-driven simulator + main)
#   - simulator.hpp        (shared types + hook declarations)
#   - mystery_transport.cpp (black-box transport)
#
# Commands:
#   make
#   make run ARGS="--msgs 2000 --interval 5 --loss 0.2 --corrupt 0.1 --seed 1234 --out out"
#   make clean

CXX      ?= g++
CXXFLAGS ?= -O2 -std=c++17 -Wall -Wextra -pedantic

BIN = sim
SRCS = emulator.cpp mystery_transport.cpp
OBJS = $(SRCS:.cpp=.o)

all: $(BIN)

$(BIN): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS)

%.o: %.cpp simulator.hpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

run: $(BIN)
	@echo "Running: ./$(BIN) $(ARGS)"
	./$(BIN) $(ARGS)

clean:
	rm -f $(BIN) *.o

.PHONY: all run clean
