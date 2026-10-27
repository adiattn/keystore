#include <fcntl.h>
#include <unistd.h>

#include <cassert>
#include <iostream>

#include "engine/wal.hpp"

int main() {
    std::cout << "[Test] Running WAL and Crash Recovery tests...\n";

    std::string test_wal_path = "./test_kvstore.wal";
    ::unlink(test_wal_path.c_str());

    // 1. Create WAL and write records
    {
        kvstore::WAL wal(test_wal_path, true);
        assert(wal.open());

        assert(wal.appendSet(1, "key1", kvstore::Value::fromString("value_one")));
        assert(wal.appendSet(2, "key2", kvstore::Value::fromInt(42)));
        assert(wal.appendSet(3, "key3", kvstore::Value::fromFloat(9.99)));
        assert(wal.appendSet(4, "key4", kvstore::Value::fromBool(true)));
        assert(wal.appendDel(5, "key1"));
        wal.sync();
    }

    // 2. Normal Recovery
    {
        kvstore::WAL wal(test_wal_path, true);
        auto records = wal.recover();
        assert(records.size() == 5);
        assert(records[0].lsn == 1 && records[0].key == "key1");
        assert(records[1].lsn == 2 && records[1].value.asInt() == 42);
        assert(records[4].lsn == 5 && records[4].op == kvstore::WalOpType::DEL);
        std::cout << "[Test] Normal recovery succeeded with 5 records.\n";
    }

    // 3. Simulate Crash Mid-Write: Append corrupted partial bytes
    {
        int fd = ::open(test_wal_path.c_str(), O_WRONLY | O_APPEND);
        assert(fd >= 0);
        // Write incomplete header / corrupt bytes
        const char corrupt_bytes[] = {
            static_cast<char>(kvstore::WAL::WAL_MAGIC), 0x06, 0x00, 0x00, 0x00, 0x55, 0x66, 0x77};
        ssize_t w = ::write(fd, corrupt_bytes, sizeof(corrupt_bytes));
        assert(w == sizeof(corrupt_bytes));
        ::close(fd);
    }

    // 4. Recover after crash: Verify corrupted tail is detected and truncated
    {
        kvstore::WAL wal(test_wal_path, true);
        auto records = wal.recover();
        assert(records.size() == 5);  // Must still recover the 5 valid records
        std::cout << "[Test] Crash recovery successfully recovered 5 valid records and truncated corrupt tail.\n";

        // 5. Verify WAL can continue to append new records cleanly after recovery
        assert(wal.open());
        assert(wal.appendSet(6, "key6", kvstore::Value::fromString("new_val_after_crash")));
        wal.sync();
        wal.close();

        // 6. Verify full recovery again
        auto final_records = wal.recover();
        assert(final_records.size() == 6);
        assert(final_records.back().lsn == 6 && final_records.back().key == "key6");
    }

    ::unlink(test_wal_path.c_str());
    std::cout << "[Test] WAL and Crash Recovery tests PASSED!\n";
    return 0;
}
