# pmtop - user-space TUI for procmon
#
#   make          build ./pmtop
#   make test     build + run unit tests (no kernel module needed)
#   make clean

CC      ?= gcc
CFLAGS  ?= -O2 -g
CFLAGS  += -std=gnu11 -Wall -Wextra -I../include -D_GNU_SOURCE
LDLIBS  := -lncursesw

SRCS := $(wildcard src/*.c src/views/*.c)
OBJS := $(SRCS:.c=.o)
DEPS := $(OBJS:.o=.d)

pmtop: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

%.o: %.c
	$(CC) $(CFLAGS) -MMD -MP -c -o $@ $<

# tests link everything except main.c and the ncurses-only files
TEST_OBJS := src/source.o src/metrics.o

tests/test_source: tests/test_source.c $(TEST_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ -lm

tests/test_actions: tests/test_actions.c src/actions.o
	$(CC) $(CFLAGS) -o $@ $^

test: tests/test_source tests/test_actions
	./tests/test_source
	./tests/test_actions

clean:
	rm -f pmtop tests/test_source tests/test_actions $(OBJS) $(DEPS)

-include $(DEPS)

.PHONY: clean test
