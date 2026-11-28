// The app's connection to the mesh: a Client, and what feeds it.
//   On the device: a socket to the radio daemon on this machine (port 4403,
//     zeroos-meshd in front of meshtasticd), and the message history the
//     daemon keeps, followed as it grows.
//   In the simulator: a made-up radio (FakeRadio) in this process, with a
//     recorder of our own turning its traffic into history.
// Nothing here blocks: tick() does a little each time it's called.
#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "apps/mesh/core/client.h"
#include "apps/mesh/core/fake_radio.h"
#include "apps/mesh/core/recorder.h"
#include "apps/mesh/core/store.h"

namespace zs::mesh {

class Session {
public:
    struct Options {
        bool simulated = false;
        std::string data_dir;        // history and unread marks; "" keeps them in memory
        uint16_t port = 4403;
        FakeRadio::Options fake;
        bool seed_history = true;    // simulated: start with a few old messages
    };
    explicit Session(const Options& o);
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    Client& client() { return client_; }
    const Client& client() const { return client_; }
    FakeRadio* fake() { return fake_.get(); }
    bool simulated() const { return opt_.simulated; }

    // The app is on the screen (connect) or not (let go of the socket; what
    // was known stays, to show at once next time).
    void resume();
    void suspend();
    bool running() const { return running_; }

    // now: unix seconds. now_ms: a clock that only goes forward.
    void tick(long now, long now_ms);

    // Seconds since we started trying to reach the radio without getting
    // its settings; 0 once they're here. For "can't reach the radio".
    long waiting(long now) const;

    // Delete a chat's history ("*": every chat), here and in the journal.
    void forget_chat(const std::string& key, long now);
    // Write the unread marks if they changed.
    void save_marks();
    // The whole history as text, for exporting: a line per message.
    std::string export_text(const std::string& key, long now) const;

private:
    void open_socket(long now_ms);
    void close_socket();
    void pump_socket(long now, long now_ms);
    void pump_fake(long now, long now_ms);
    void read_journal();
    void seed(long now);

    Options opt_;
    Client client_;
    bool running_ = false;
    long started_ = 0;
    // Device
    int fd_ = -1;
    bool connecting_ = false;
    long retry_at_ms_ = 0, connect_started_ms_ = 0;
    Deframer in_;
    std::string out_;
    Journal journal_;
    std::string marks_path_, marks_saved_;
    long journal_at_ms_ = 0;
    // Simulator
    std::unique_ptr<FakeRadio> fake_;
    Recorder recorder_;
    Deframer fake_in_;
    long fake_start_ms_ = -1, fake_up_at_ms_ = 0;
    bool fake_linked_ = false, seeded_ = false;
};

}  // namespace zs::mesh
