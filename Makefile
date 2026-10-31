CC = gcc
CFLAGS = -Wall -Wextra -O2 -Iserver/include -pthread
SRC = server/src/main.c server/src/http.c server/src/data.c server/src/pool.c \
      server/src/mmap_data.c server/src/response_queue.c server/src/logger.c \
      server/src/config.c server/src/proc_monitor.c
TARGET = bin/server

all: $(TARGET)

$(TARGET): $(SRC)
	@mkdir -p bin server/logs server/data server/config
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET)
	@chmod +x $(TARGET)
	@echo "=========================================="
	@echo "Build successful: ./$(TARGET)"
	@echo "Run with: ./$(TARGET) [port]  (or 'make run')"
	@echo "=========================================="

clean:
	rm -rf bin
	rm -rf server/logs/*.log
	@echo "Cleaned build artifacts."

run: $(TARGET)
	./$(TARGET) 3000

test: $(TARGET)
	@chmod +x tests/test_phase1.sh
	./tests/test_phase1.sh

test-phase2: $(TARGET)
	@chmod +x tests/test_phase2.sh
	./tests/test_phase2.sh

test-concurrency: $(TARGET)
	@chmod +x tests/test_concurrency.sh
	./tests/test_concurrency.sh

test-phase3: test-phase2 test-concurrency

test-phase4: $(TARGET)
	@chmod +x tests/test_phase4.sh
	./tests/test_phase4.sh

test-phase5: $(TARGET)
	@chmod +x tests/test_phase5.sh
	./tests/test_phase5.sh

test-all: $(TARGET)
	@chmod +x tests/test_all.sh
	./tests/test_all.sh

.PHONY: all clean run test test-phase2 test-concurrency test-phase3 test-phase4 test-phase5 test-all
