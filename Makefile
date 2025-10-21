CC      ?= gcc
AR      ?= ar
ARFLAGS ?= rcs
CFLAGS  ?= -std=c11 -Wall -Wextra -Wpedantic -O2 -D_XOPEN_SOURCE=700

ALL_OBJS := mlpt.o
ALL_HDRS := mlpt.h config.h

.PHONY: all clean
all: libmlpt.a

libmlpt.a: $(ALL_OBJS)
	$(AR) $(ARFLAGS) $@ $^

mlpt.o: mlpt.c $(ALL_HDRS)
	$(CC) $(CFLAGS) -c mlpt.c -o $@

clean:
	rm -f *.o libmlpt.a
