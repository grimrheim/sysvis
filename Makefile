CC = gcc
CFLAGS 	= -Wall -Wextra -std=c17 -O2 -D_GNU_SOURCE
LIBS	= -lncurses -lpthread
TARGET 	= sysvis
SRC 	= src/main.c src/process.c src/eventbuf.c src/syscalls.c src/tracer.c src/gui/gui.c

all: build

build: $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET) $(LIBS)

run: build
	./$(TARGET)

clean:
	rm -f $(TARGET)
