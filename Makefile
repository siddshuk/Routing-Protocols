TARGETS = manager distvec linkstate

CC=g++
CCOPTS= -Wall -Wextra -pthread -g

.PHONY: all clean

all: $(TARGETS)

clean:
	rm -f $(TARGETS)

manager: manager.cpp
	$(CC) $(CCOPTS) -o manager manager.cpp

distvec: distvec.cpp
	$(CC) $(CCOPTS) -o distvec distvec.cpp

linkstate: linkstate.cpp
	$(CC) $(CCOPTS) -o linkstate linkstate.cpp
