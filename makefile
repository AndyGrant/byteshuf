
CC  = gcc
EXE = byteshuf.out
SRC = byteshuf.c

WFLAGS  = -Wall -Wshadow -Wextra
CCFLAGS = -march=native -flto -O3 -DNDEBUG

N = 32

all:
	$(CC) $(CCFLAGS) -DBYTES_PER=$(N) $(WFLAGS) $(SRC) -o $(EXE)