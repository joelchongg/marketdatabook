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
    explicit RecoveryEngine(book::LimitOrderBook& order_book)
        : order_book_(order_book) {}

    /*
    * Takes in the WAL filepath as argument in order to reconstruct the
    * current state of the orderbook from the WAL.
    */
    void run_recovery(const char* filepath) {
        int fd = open(filepath, O_RDONLY | O_LARGEFILE);
        if (fd == -1) [[unlikely]] {
            throw std::runtime_error("RecoveryEngine::run_recovery: unable to open file for recovery. Error: " 
                + std::string(strerror(errno)));
        }

        struct stat buf;
        fstat(fd, &buf);
        
        void* start_addr = mmap(NULL, buf.st_size, PROT_READ, MAP_SHARED, fd, 0);
        if (start_addr == MAP_FAILED) [[unlikely]] {
            close(fd);
            throw std::runtime_error("RecoveryEngine::run_recovery: unable to mmap file for recovery. Error: "
                + std::string(strerror(errno)));
        }

        int ret = madvise(start_addr, buf.st_size, MADV_WILLNEED | MADV_SEQUENTIAL);
        if (ret == -1) [[unlikely]] {
            close(fd);
            munmap(start_addr, buf.st_size);
            throw std::runtime_error("RecoveryEngine::run_recovery: unable to use madvise. Error: "
                + std::string(strerror(errno)));
        }

        const WalFrame* curr_wal_ptr = static_cast<const WalFrame*>(start_addr);
        while (curr_wal_ptr->marker == VALID_FRAME_MARKER) {
            // prefetch WAL data
            __builtin_prefetch(curr_wal_ptr + 8, 0, 0); // we only read once, so use non temporal reads instead

            uint32_t calculated_checksum = utils::calculate_crc32(curr_wal_ptr);
            if (calculated_checksum != curr_wal_ptr->checksum) [[unlikely]] {
                // torn write / corruption detected
                report_.torn_write_detected = true;
                break;
            }

            // process frame
            const uint8_t* payload = curr_wal_ptr->payload;
            const char msg_type = static_cast<char>(curr_wal_ptr->msg_type);

            protocol::Header header = ItchParserType::extract_header(payload);

            switch (msg_type) {
                case 'A': {
                    protocol::NormalizedOrder order = ItchParserType::parse_add_order(payload, header);
                    order_book_.add_order(order.add_order);
                    break;
                }
                case 'E': {
                    protocol::NormalizedOrder order = ItchParserType::parse_executed_order(payload, header);
                    order_book_.execute_order(order.execute_order);
                    break;
                }
                case 'X': {
                    protocol::NormalizedOrder order = ItchParserType::parse_cancel_order(payload, header);
                    order_book_.cancel_order(order.cancel_order.order_reference_number);
                    break;
                }
                default:
                    // currently unsupported order messages
                    break;
            }

            ++report_.records_processed;
            report_.last_valid_seq = curr_wal_ptr->seq_num;

            ++curr_wal_ptr;
        }

        // clean up resources
        close(fd);
        munmap(start_addr, buf.st_size);
    }

    RecoveryReport get_recovery_report() {
        return report_;
    }

private:
    using ItchParserType = protocol::ItchParser<protocol::NormalizedOrder, 4096>; // should correspond to the main ITCH parser object type used
    constexpr static uint8_t VALID_FRAME_MARKER = 0xAA;
    book::LimitOrderBook& order_book_;
    RecoveryReport report_{};
};

} // namespace recovery