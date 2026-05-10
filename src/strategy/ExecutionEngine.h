#pragma once

#include "network/OuchClient.h"

#include <array>

namespace strategy {

/*
* This class is currently a mock class.
* ExecutionEngine is used to observe the LimitOrderBook
* and will execute trades depending on the TradingStrategy used.
*/
class ExecutionEngine {
public:
    ExecutionEngine(network::OuchClient& client)
        : ouch_client_ { client }
    { /* not implemented yet */ }

    void execute_trade() { /* not implemented yet */ }

private:
    std::array<network::OuchPacket, 2048> egress_packet_pool_;
    size_t curr_seq_num { 0 };
    network::OuchClient& ouch_client_;
};

} // namespace strategy