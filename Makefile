# Makefile for CS 4390 P2P File Sharing Project

CC = gcc
CFLAGS = -Wall -Wextra -pthread

all: tracker peer

tracker:
	$(CC) $(CFLAGS) -o tracker/tracker tracker/tracker.c

peer:
	$(CC) $(CFLAGS) -o peer/peer peer/peer.c

clean:
	rm -f tracker/tracker peer/peer

.PHONY: all tracker peer clean
.PHONY: all tracker peer clean