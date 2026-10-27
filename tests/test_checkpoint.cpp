#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cassert>
#include <iostream>

#include "engine/engine.hpp"
#include "engine/snapshot.hpp"

int main() {
    std::cout << "[Test] Running Checkpointing & Snapshot tests...\n";

    std::string test_snap_path = "./test_kvstore.snapshot";
    ::unlink(test_snap_path.c_str());

    // 1. Test synchronous snapshot creation and loading
    {
        kvstore::SnapshotManager mgr(test_snap_path);
        std::vector<std::pair<std::string, kvstore::Value>> entries = {{"user:1", kvstore::Value::fromString("Alice")},
                                                                       {"score:1", kvstore::Value::fromInt(95)},
                                                                       {"ratio:1", kvstore::Value::fromFloat(0.95)},
                                                                       {"active:1", kvstore::Value::fromBool(true)}};

        assert(mgr.createSnapshot(100, entries));
        assert(mgr.snapshotExists());

        uint64_t loaded_lsn = 0;
        std::vector<std::pair<std::string, kvstore::Value>> loaded_entries;
        assert(mgr.loadSnapshot(loaded_lsn, loaded_entries));
        assert(loaded_lsn == 100);
        assert(loaded_entries.size() == 4);
        std::cout << "[Test] Snapshot creation and loading verified.\n";
    }

    // 2. Test CRC32 validation on corrupted snapshot
    {
        // Flip one byte in the snapshot file
        int fd = ::open(test_snap_path.c_str(), O_RDWR);
        assert(fd >= 0);
        char b = 0xFF;
        ::lseek(fd, 10, SEEK_SET);
        ::write(fd, &b, 1);
        ::close(fd);

        kvstore::SnapshotManager mgr(test_snap_path);
        uint64_t loaded_lsn = 0;
        std::vector<std::pair<std::string, kvstore::Value>> loaded_entries;
        bool loaded = mgr.loadSnapshot(loaded_lsn, loaded_entries);
        assert(!loaded);  // Must reject corrupted snapshot due to CRC32 mismatch!
        std::cout << "[Test] Checksum verification rejected corrupted snapshot successfully.\n";
    }
    ::unlink(test_snap_path.c_str());

    // 3. Test BGSAVE fork-based snapshotting
    {
        kvstore::SnapshotManager mgr(test_snap_path);
        std::vector<std::pair<std::string, kvstore::Value>> entries = {
            {"bgsave:k1", kvstore::Value::fromString("bg_val1")}, {"bgsave:k2", kvstore::Value::fromInt(2026)}};

        pid_t child_pid = mgr.bgSaveFork(200, entries);
        assert(child_pid > 0);

        // Wait for child to complete
        int status = 0;
        ::waitpid(child_pid, &status, 0);
        assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);

        // Verify snapshot written by child
        uint64_t loaded_lsn = 0;
        std::vector<std::pair<std::string, kvstore::Value>> loaded_entries;
        assert(mgr.loadSnapshot(loaded_lsn, loaded_entries));
        assert(loaded_lsn == 200);
        assert(loaded_entries.size() == 2);
        std::cout << "[Test] Fork-based BGSAVE (Copy-on-Write) verified successfully.\n";
    }
    ::unlink(test_snap_path.c_str());

    // 4. Full End-to-End Engine Snapshot + WAL Recovery test
    {
        std::string test_dir = "./test_engine_data";
        ::system(("rm -rf " + test_dir).c_str());

        // Step 4a: Start engine, insert data, take snapshot
        {
            kvstore::KVEngine engine(test_dir, true);
            assert(engine.open());
            assert(engine.set("snap_key1", kvstore::Value::fromString("val1")));
            assert(engine.set("snap_key2", kvstore::Value::fromInt(42)));
            assert(engine.save());  // Takes checkpoint and truncates WAL
            // Step 4b: Insert more data after checkpoint into new WAL
            assert(engine.set("wal_key3", kvstore::Value::fromFloat(3.14)));
            assert(engine.set("wal_key4", kvstore::Value::fromBool(true)));
            engine.close();
        }

        // Step 4c: Reopen engine (simulating restart) and verify all data is present
        {
            kvstore::KVEngine recovered_engine(test_dir, true);
            assert(recovered_engine.open());
            assert(recovered_engine.size() == 4);

            auto v1 = recovered_engine.get("snap_key1");
            assert(v1.has_value() && v1->asString() == "val1");

            auto v2 = recovered_engine.get("snap_key2");
            assert(v2.has_value() && v2->asInt() == 42);

            auto v3 = recovered_engine.get("wal_key3");
            assert(v3.has_value() && std::abs(v3->asFloat() - 3.14) < 1e-5);

            auto v4 = recovered_engine.get("wal_key4");
            assert(v4.has_value() && v4->asBool() == true);

            recovered_engine.close();
        }

        ::system(("rm -rf " + test_dir).c_str());
        std::cout << "[Test] Full Engine Snapshot + WAL Recovery test verified successfully.\n";
    }

    std::cout << "[Test] Checkpointing & Snapshot tests PASSED!\n";
    return 0;
}
