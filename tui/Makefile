# pmtop - user-space TUI for procmon
#
#   make          build ./pmtop
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

clean:
	rm -f pmtop $(OBJS) $(DEPS)

-include $(DEPS)

.PHONY: clean
