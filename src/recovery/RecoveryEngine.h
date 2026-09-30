#pragma once

#include "MmapLogger.h"
#include "book/LimitOrderBook.h"
#include "protocol/ItchOrderTypes.h"

#include <stdexcept>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace recovery {

struct RecoveryReport {
    size_t records_processed = 0;
    uint32_t last_valid_seq = 0;
    bool torn_write_detected = false;
};

/*
* Used for deterministic recovery in the event of unexpected crashes
*/
class RecoveryEngine {
public:
    explicit RecoveryEngine(book::LimitOrderBook& order_book);

    /*
    * Takes in the WAL filepath as argument in order to reconstruct the
    * current state of the orderbook from the WAL.
    */
    void run_recovery(const char* filepath);
    RecoveryReport get_recovery_report() { return report_; }

private:
    using ItchParserType = protocol::ItchParser<protocol::NormalizedOrder, 4096>; // should correspond to the main ITCH parser object type used
    constexpr static uint8_t VALID_FRAME_MARKER = 0xAA;
    book::LimitOrderBook& order_book_;
    RecoveryReport report_{};
};

} // namespace recovery