# K-PHD Userspace Daemon
# Requires: libnl-3-dev libnl-genl-3-dev (Debian/Ubuntu)
#           libnl (Arch Linux: sudo pacman -S libnl)

CC      = gcc
CFLAGS  = -Wall -Wextra -O2 $(shell pkg-config --cflags libnl-genl-3.0)
LDFLAGS = $(shell pkg-config --libs libnl-genl-3.0) -lm
TARGET  = kphd_daemon
SRC     = kphd_daemon.c

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f $(TARGET)
