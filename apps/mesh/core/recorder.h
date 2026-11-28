// Turns the traffic between a client and the radio into history records:
// a text we sent, a text that arrived, and what became of ours (queued,
// passed on, confirmed, failed). The daemon runs one over everything that
// goes through it and writes the records to the journal; in the simulator
// the app runs one itself.
#pragma once

#include <cstdint>
#include <deque>
#include <vector>

#include "apps/mesh/core/proto.h"
#include "apps/mesh/core/store.h"

namespace zs::mesh {

class Recorder {
public:
    // Give up on a message of ours that nothing came back for.
    static constexpr long kNoAnswerAfter = 300;

    uint32_t my_num() const { return me_; }

    void on_to_radio(const ToRadio&, long now, std::vector<Record>& out);
    void on_from_radio(const FromRadio&, long now, std::vector<Record>& out);
    // Now and then: times out messages nothing was heard about.
    void tick(long now, std::vector<Record>& out);
    // The radio went away (restart): what was in flight is lost.
    void link_lost(long now, std::vector<Record>& out);

private:
    struct Sent {
        uint32_t id = 0, to = 0;
        long at = 0;
        Delivery state = Delivery::Queued;
        uint32_t error = 0;
    };
    void news(Sent& s, Delivery state, uint32_t error, uint32_t acker, long now, std::vector<Record>& out);
    Sent* find(uint32_t id);
    bool seen(uint32_t from, uint32_t id);

    uint32_t me_ = 0;
    std::deque<Sent> sent_;                  // our last texts, for their acks
    std::deque<uint64_t> seen_;              // texts already recorded (from, id)
};

// True for the packets that are chat: text, an alert, a waypoint.
bool is_chat_port(uint32_t portnum);
// The record for a chat packet (either direction). `me` is our node number.
Record chat_record(const MeshPacket&, uint32_t me, bool mine, long now);

}  // namespace zs::mesh
