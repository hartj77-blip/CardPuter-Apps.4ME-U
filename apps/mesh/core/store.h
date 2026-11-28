// The message history: one text file, a line per event, appended to as
// messages arrive and their delivery is confirmed. The radio daemon
// (zeroos-meshd) writes it while the mesh is on, whether or not the app is
// open; the app reads it, and follows it while it's open.
//
//   M <time> <id> <from> <to> <channel> <flags> <state> <error> <snr*4> <rssi> <hops> <reply id> <text>
//   A <time> <id> <state> <error> <acked by>       delivery news for a message of ours
//   D <time> <chat key>                            that chat's history was deleted here
//
// Tab-separated; numbers in hex for node numbers and ids, decimal otherwise;
// the text has tab, newline and backslash escaped. Flags are letters: m ours,
// p sent with their public key, q came by MQTT, e a reaction (text is the
// emoji, reply id the message), b rang the bell, w a waypoint.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace zs::mesh {

// How far a message of ours got. Ordered: a later state never goes back,
// except that Delivered may still follow Failed (a late ack).
enum class Delivery : uint8_t {
    Received = 0,   // not ours
    Queued = 1,     // handed to the radio
    Sent = 2,       // the radio took it into its queue
    Relayed = 3,    // another node was heard passing it on
    Delivered = 4,  // the node it was for confirmed it
    Failed = 5,
};

// Errors of our own, beside Routing.Error (which stops at 39).
constexpr uint32_t kErrLocalRadioOff = 0x101;   // region not set, or transmit off
constexpr uint32_t kErrLocalNoRadio = 0x102;
constexpr uint32_t kErrLocalQueue = 0x103;      // the radio wouldn't queue it
constexpr uint32_t kErrLocalNoAnswer = 0x104;   // nothing came back at all
// For a person. Covers Routing.Error too.
const char* delivery_error_text(uint32_t error);

struct Record {
    enum Kind : char { Msg = 'M', Ack = 'A', Deleted = 'D' };
    char kind = Msg;
    long time = 0;                 // unix seconds, this device's clock
    uint32_t id = 0;               // packet id
    // Msg
    uint32_t from = 0, to = 0, channel = 0;
    bool mine = false, pki = false, via_mqtt = false, reaction = false, bell = false, waypoint = false;
    Delivery state = Delivery::Received;
    uint32_t error = 0;
    int snr_q = 0;                 // dB * 4
    int rssi = 0;
    int hops = -1;
    uint32_t reply_id = 0;
    std::string text;
    // Ack
    uint32_t acker = 0;
    // Deleted
    std::string key;

    bool direct() const { return to != 0xffffffffu; }
    // Which chat it belongs to: "c<channel>" or "d<the other node, hex>".
    std::string chat_key() const;
};

std::string chat_key_channel(uint32_t channel);
std::string chat_key_dm(uint32_t node);

std::string format_record(const Record&);          // one line, with its newline
bool parse_record(std::string_view line, Record&); // without the newline

// Apply delivery news to a message's state. True if it changed.
bool advance_delivery(Delivery& state, uint32_t& error, Delivery news, uint32_t news_error);

class Journal {
public:
    Journal() = default;
    explicit Journal(std::string path) : path_(std::move(path)) {}
    ~Journal();
    Journal(const Journal&) = delete;
    Journal& operator=(const Journal&) = delete;

    void set_path(std::string path);
    const std::string& path() const { return path_; }

    // Add a line. Makes the folder (mode 700) and the file (mode 600).
    bool append(const Record&);

    // What was added since the last poll. `reset` is true when the file was
    // replaced or cut short: forget what you had, `out` starts from its top.
    void poll(std::vector<Record>& out, bool& reset);

    // Rewrite the file with at most `keep` messages per chat, deliveries
    // folded into their messages and deleted chats gone. Only when it has
    // grown past `min_bytes`. Atomic (a temp file, then rename).
    bool compact(size_t keep, size_t min_bytes);

private:
    void close_reader();
    std::string path_;
    int rfd_ = -1;
    long long rpos_ = 0;
    unsigned long long rino_ = 0;
    std::string partial_;
};

// Fold a run of records into what a reader shows: messages in order, each
// with its latest delivery state; deleted chats dropped.
std::vector<Record> fold_records(const std::vector<Record>& in, size_t keep_per_chat);

}  // namespace zs::mesh
