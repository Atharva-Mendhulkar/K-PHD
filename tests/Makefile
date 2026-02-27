# K-PHD Stress Tests
# Build all stress test programs

CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -pthread

TARGETS = cpu_hog io_stall

.PHONY: all clean

all: $(TARGETS)

cpu_hog: cpu_hog.c
	$(CC) $(CFLAGS) -o $@ $<

io_stall: io_stall.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f $(TARGETS)
