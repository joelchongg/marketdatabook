#include <catch2/catch_test_macros.hpp>
#include <unistd.h>

#include "book/LimitOrderBook.h"
#include "protocol/ItchOrderTypes.h"
#include "recovery/MmapLogger.h"
#include "recovery/RecoveryEngine.h"
#include "utils/spscqueue.h"

TEST_CASE("Crash Recovery Correctness Test") {
    using Orders = protocol::NormalizedOrder;
    
    recovery::MmapLogger logger("logs/event_logs.txt");
    utils::SPSCQueue<Orders, 4096> queue;
    book::LimitOrderBook book(1000);

    protocol::ItchParser parser {queue, logger};

    uint8_t raw_msg[36] = {0};
    raw_msg[0] = 'A';

    for (int i = 0; i < 10; ++i) {
        parser.parse_block(reinterpret_cast<char*>(raw_msg), 36);
    }

    logger.flush_to_disk(MS_SYNC);

    int fd = open("logs/event_logs.txt", O_RDWR);
    REQUIRE(fd != -1);

    // corrupt a middle frame (5th frame)
    off_t target_offset = (4 * 64) + 16;
    off_t ret = lseek(fd, target_offset, SEEK_SET);
    REQUIRE(ret == target_offset);

    uint32_t corrupted = 0xDEADBEEF;

    ssize_t written = write(fd, &corrupted, sizeof(corrupted));
    REQUIRE(written == sizeof(corrupted));

    close(fd);

    // test recovery engine
    recovery::RecoveryEngine recovery_engine(book);
    recovery_engine.run_recovery("logs/event_logs.txt");

    recovery::RecoveryReport report = recovery_engine.get_recovery_report();
    REQUIRE(report.torn_write_detected == true);
    REQUIRE(report.records_processed == 4);
    REQUIRE(report.last_valid_seq == 3);
}