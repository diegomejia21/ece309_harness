# ECE 309 Project 1 -- build rules.
#
#   make          build ./harness
#   make asan     build ./harness_asan with AddressSanitizer + LeakSanitizer
#   make test     build both and run the automated test suite
#   make clean    remove build products

CC      := gcc
CFLAGS  := -std=c11 -Wall -Wextra -pedantic -O2
LDLIBS  := -lm
SRCS    := harness.c context.c model.c tools.c
HDRS    := context.h model.h tools.h

all: harness

harness: $(SRCS) $(HDRS)
	$(CC) $(CFLAGS) -o $@ $(SRCS) $(LDLIBS)

# Instrumented build. Used by test.sh as the leak checker when valgrind
# is not installed.
harness_asan: $(SRCS) $(HDRS)
	$(CC) $(CFLAGS) -g -fsanitize=address,undefined -o $@ $(SRCS) $(LDLIBS)

asan: harness_asan

test: harness
	bash test.sh

clean:
	rm -f harness harness_asan
	rm -rf harness.dSYM harness_asan.dSYM

.PHONY: all asan test clean
