# KVStore: Multithreaded Key-Value Database with Persistence

A high-performance in-memory key-value database written in C++17 for Operating Systems coursework. Implements fine-grained sharded concurrency, Write-Ahead Logging (WAL) with CRC32 checksums, point-in-time snapshotting via fork(2) Copy-On-Write, and crash recovery. Wire-compatible with the Redis RESP protocol.

## 1. System Architecture & OS Primitives

```
                +-------------------------------------------------------+
                |                       KVServer                        |
                |             Network Reactor & Event Loop              |
                +---------------------------+---------------------------+
                                            |
                                            v
                +-------------------------------------------------------+
                |                      ThreadPool                       |
                |                  Worker Task Queue                    |
                +---------------------------+---------------------------+
                                            |
                                            v
                +-------------------------------------------------------+
                |                       KVEngine                        |
                |                  Core Database Engine                 |
                +---------------------------+---------------------------+
                     |                      |                      |
                     v                      v                      v
        +-----------------------+ +--------------------+ +--------------------+
        |       HashIndex       | |        WAL         | |  SnapshotManager   |
        |    In-Memory Hash     | |  Write-Ahead Log   | |  Disk Checkpoints  |
        |     Table Shards      | |   (Persistence)    | |    & Snapshots     |
        +-----------------------+ +--------------------+ +--------------------+
```

### OS Primitives Summary

| Subsystem | OS Primitives / System Calls | Purpose |
| :--- | :--- | :--- |
| **Networking** | `socket(2)`, `fcntl(2)` (`O_NONBLOCK`), `setsockopt(2)` (`TCP_NODELAY`, `SO_REUSEADDR`), `epoll_create1(2)`, `epoll_ctl(2)`, `epoll_wait(2)` | Event-driven non-blocking I/O reactor multiplexing concurrent connections without thread-per-client overhead. |
| **Concurrency** | `pthread` / `std::thread`, `std::mutex`, `std::condition_variable` | Worker thread pool eliminating busy-waiting; handles task dispatch and life cycle synchronization. |
| **Locking** | `std::shared_mutex`, `std::shared_lock`, `std::unique_lock` | 32-way sharded read-write locking; permits concurrent multi-core reads without reader-reader contention. |
| **Persistence** | `open(2)` (`O_APPEND`), `write(2)`, `fdatasync(2)`, `ftruncate(2)` | Append-only Write-Ahead Logging with page cache flushing and trailing corrupt log trimming on crash recovery. |
| **Snapshotting**| `fork(2)`, `waitpid(2)`, `rename(2)`, `_exit(2)` | Non-blocking background checkpointing via virtual memory Copy-On-Write (COW) and atomic file replacement. |
| **Signals** | `sigaction(2)` (`SIGINT`, `SIGTERM`, `SIGPIPE`), `pipe(2)` | Graceful shutdown handling and reactor wakeup via the self-pipe trick. |
| **Integrity** | IEEE 802.3 CRC32 | Frame-level checksumming across WAL records and snapshot binary images. |


## 2. Build & Test

### Requirements
- GCC >= 9.0 or Clang >= 10.0 (C++17 support)
- CMake >= 3.14
- Linux x86_64 / aarch64 (or Windows via WSL / WSL2)

### Build Targets & Help
```bash
make help       # View all available make targets
make            # Build core engine, server, cli, and tests
make test       # Run 6/6 unit test suites
make bench      # Run multi-thread stress benchmark (1-32 threads)
make server     # Launch KVStore server on port 6379
make cli        # Launch interactive client REPL
make compare    # Run head-to-head comparison with official Redis
make clean      # Clean build artifacts
```

### Windows Setup (via WSL)
KVStore leverages POSIX/Linux system primitives (`epoll`, `fork` COW, `fdatasync`). On Windows, run the automated setup launcher:
```cmd
# Command Prompt / Double-Click:
scripts\setup_windows.bat

# PowerShell:
.\scripts\setup_windows.ps1
```

## 3. Supported Commands & Data Types

The engine supports strongly-typed values: `STRING`, `INTEGER` (`int64_t`), `FLOAT` (`double`), and `BOOLEAN` (`bool`).

| Command | Arguments | Semantics |
| :--- | :--- | :--- |
| `SET` | `<key> <val>` | Sets key to auto-detected type (`string`, `int`, `float`, `bool`) |
| `SETSTR` | `<key> <val>` | Sets key explicitly to `STRING` |
| `SETINT` | `<key> <val>` | Sets key explicitly to `INTEGER` |
| `SETFLOAT`| `<key> <val>` | Sets key explicitly to `FLOAT` |
| `SETBOOL` | `<key> <true/false>` | Sets key explicitly to `BOOLEAN` |
| `GET` | `<key>` | Retrieves typed value or `(nil)` |
| `DEL` | `<key> [key2 ...]` | Deletes one or more keys |
| `EXISTS` | `<key> [key2 ...]` | Checks existence of keys |
| `TYPE` | `<key>` | Returns data type name |
| `INCR` | `<key>` | Atomic integer increment by 1 |
| `INCRBY` | `<key> <delta>` | Atomic integer increment by delta |
| `INCRBYFLOAT` | `<key> <delta>` | Atomic float increment by delta |
| `CAS` | `<key> <exp> <des>` | Atomic Compare-And-Swap |
| `KEYS` | `[pattern]` | Wildcard search using `fnmatch(3)` |
| `DBSIZE` | None | Returns total active key count |
| `FLUSHDB`| None | Clears in-memory index and logs `CLEAR` to WAL |
| `SAVE` | None | Synchronous disk checkpoint |
| `BGSAVE` | None | Asynchronous `fork()` Copy-On-Write checkpoint |
| `STATS` | None | Returns active key count, LSN, WAL size, and snapshot state |
| `PING` | `[msg]` | Tests server connectivity |
| `QUIT` | None | Closes client connection |


## 4. Crash Recovery & Durability Lifecycle

1. **Write Path**:
   - Every mutating operation assigns a monotonic Log Sequence Number (`LSN`).
   - The operation is serialized into a framed binary record containing Magic byte `0xAA`, LSN, OpCode, Key, Value, and a 4-byte CRC32 checksum.
   - The record is written to `kvstore.wal` and flushed via `fdatasync(2)`.
   - The in-memory 32-shard hash index is updated under the shard's unique lock.

2. **Checkpointing**:
   - `SAVE`: Serializes in-memory state to `kvstore.snapshot.tmp` with CRC32 checksum, fsyncs, and invokes `rename(2)` to `kvstore.snapshot`. Truncates `kvstore.wal` to 0 bytes.
   - `BGSAVE`: Forks child process via `fork(2)`. Child inherits memory state via Copy-On-Write and writes snapshot while parent continues serving requests.

3. **Startup Recovery Flow**:
   - Checks for `kvstore.snapshot`. If valid (verified via magic header and CRC32), loads all key-values into the hash index and sets `current_lsn = snapshot_lsn`.
   - Opens `kvstore.wal` and replays records with `LSN > snapshot_lsn`.
   - If a power outage or crash caused a partial trailing record at the end of the WAL, the recovery routine detects the CRC32 failure, calls `ftruncate(2)` on the file descriptor to trim invalid bytes, and restores state up to the last valid transaction.

## 5. Performance Benchmarks

### In-Memory Multi-Thread Scalability (Throughput & Latency)

Measured on Linux across varying worker thread counts:

| Workload | 1 Thread | 2 Threads | 4 Threads | 8 Threads | 16 Threads | 32 Threads | Latency (32T) |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **GET (In-Memory)** | 1.16M ops/s | 2.87M ops/s | 4.03M ops/s | 6.07M ops/s | **10.12M ops/s** | 9.76M ops/s | **0.10 μs** |
| **SET (WAL Persisted)**| 206K ops/s | 131K ops/s | 91K ops/s | 84K ops/s | 83K ops/s | 89K ops/s | 11.21 μs |
| **INCR (Atomic Shared)**| 512K ops/s | 256K ops/s | 152K ops/s | 137K ops/s | 146K ops/s | 99K ops/s | 10.08 μs |
| **80/20 Mixed** | 544K ops/s | 459K ops/s | 310K ops/s | 306K ops/s | 327K ops/s | 247K ops/s | 4.04 μs |

### Official `redis-benchmark` Head-to-Head

Tested over localhost loopback TCP sockets using `/usr/bin/redis-benchmark` (50 concurrent connections, 100,000 requests):

| Target | SET Throughput | SET Latency (p50) | GET Throughput | GET Latency (p50) |
| :--- | :---: | :---: | :---: | :---: |
| **Official Redis** | 35,026 req/s | 0.719 ms | 32,499 req/s | 0.847 ms |
| **KVStore** | 26,645 req/s | 0.983 ms | **28,074 - 36,179 req/s** | **0.655 - 0.839 ms** |

---

## 6. Server & CLI Execution

### Starting the Server
```bash
./build/kvstore-server -p 6379 -d ./data --threads 16
```

### Launching Interactive CLI
```bash
./build/kvstore-cli -p 6379
```

### Non-Interactive Command Execution
```bash
./build/kvstore-cli -p 6379 "SET user:1 \"Alice\"" "GET user:1"
```
