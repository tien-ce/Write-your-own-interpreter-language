CC = gcc
CFLAGS = -Wall -Wextra
MEMCHECK_FLAGS = -fsanitize=address -g
DEBUG_FLAGS = -g -O0
LOG_FILE = memory_check.txt
TEST_FILE = while_loop.ti 

HEADERS = $(wildcard src/include/*.h)
SOURCES = $(wildcard src/*.c)
OBJECTS = $(SOURCES:.c=.o)

EXEC = ti.out
MEMCHECK_EXEC = ti_memcheck.out
DEBUG_EXEC = ti_debug.out

# Default build (Release/Normal)
all: $(EXEC)

$(EXEC): $(OBJECTS)
	$(CC) $(OBJECTS) $(CFLAGS) -o $(EXEC)

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

# Target memcheck: build, run and overwrite log
memcheck: CFLAGS += $(MEMCHECK_FLAGS)
memcheck: clean_objs $(MEMCHECK_EXEC)
	./$(MEMCHECK_EXEC) $(TEST_FILE) > $(LOG_FILE) 2>&1 || true

$(MEMCHECK_EXEC): $(OBJECTS)
	$(CC) $(OBJECTS) $(CFLAGS) -o $(MEMCHECK_EXEC)

# Target debug: build with debug symbols and launch gdb in TUI mode
debug: CFLAGS += $(DEBUG_FLAGS)
debug: clean_objs $(DEBUG_EXEC)
	gdb -tui --args ./$(DEBUG_EXEC) $(TEST_FILE)

$(DEBUG_EXEC): $(OBJECTS)
	$(CC) $(OBJECTS) $(CFLAGS) -o $(DEBUG_EXEC)

clean_objs:
	rm -f $(OBJECTS)

clean:
	rm -f $(OBJECTS) $(EXEC) $(MEMCHECK_EXEC) $(DEBUG_EXEC) $(LOG_FILE)

.PHONY: all memcheck debug clean clean_objs
