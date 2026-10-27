.PHONY: all build clean test bench server cli compare help

BUILD_DIR ?= build

all: build

help:
	@echo "#########################################################"
	@echo "           KVStore Build & Run Help Menu                 "
	@echo "#########################################################"
	@echo "  make               - Build all targets (Release mode)"
	@echo "  make build         - Compile core library, server, cli, tests"
	@echo "  make test          - Run full unit test suite (6/6 tests)"
	@echo "  make bench         - Run multi-thread scalability benchmark"
	@echo "  make server        - Start KVStore database server (port 6379)"
	@echo "  make cli           - Launch interactive CLI client"
	@echo "  make compare       - Compare head-to-head with official Redis"
	@echo "  make clean         - Remove build artifacts and temp data"
	@echo "  make help          - Display this help message"
	@echo "#########################################################"

build:
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && cmake -DCMAKE_BUILD_TYPE=Release .. && $(MAKE) -j$$(nproc)
	@ln -sf $(BUILD_DIR)/compile_commands.json compile_commands.json

clean:
	@rm -rf $(BUILD_DIR) data/ test_* bench_*

test: build
	@cd $(BUILD_DIR) && ctest --output-on-failure

bench: build
	@./$(BUILD_DIR)/kvstore-benchmark

compare: build
	@./scripts/benchmark_compare.sh

server: build
	@./$(BUILD_DIR)/kvstore-server

cli: build
	@./$(BUILD_DIR)/kvstore-cli
