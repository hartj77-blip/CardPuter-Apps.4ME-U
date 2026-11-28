// The radio daemon's heart (zeroos-meshd, core/meshd.cpp).
//
// meshtasticd's client API takes one connection: a second one cuts the first
// off (firmware, src/mesh/api/ServerAPI.cpp). ZeroShell holds one for its
// "new message" notes, the Mesh app wants one, and so does a phone running
// Meshtastic's own app. So meshtasticd is moved to port 4404, where only this
// daemon talks to it, and the daemon answers port 4403 for everyone else:
//
//   - each client's "send me everything" (want_config) is passed to the radio
//     one at a time, and the radio's answer goes back to the client that asked;
//   - what a client sends goes to the radio; what the radio says (messages,
//     acks, positions) goes to every client;
//   - a text one client sends is shown to the others, as the radio's own;
//   - every text, and what became of it, is written to the history file
//     (core/store.h), so messages that arrive while the app is closed are
//     there when it opens;
//   - with "share my position" on, the GPS fix from gpsd is given to the
//     radio now and then, the way a phone gives it its own.
//
// One thread, poll(), nothing blocking.
#pragma once

#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <vector>

#include "apps/mesh/core/proto.h"
#include "apps/mesh/core/recorder.h"
#include "apps/mesh/core/store.h"

namespace zs::mesh {

class Hub {
public:
    struct Options {
        std::string listen_addr = "0.0.0.0";
        uint16_t listen_port = 4403;      // 0: any free port (tests)
        uint16_t radio_port = 4404;
        std::string data_dir;             // history: <data_dir>/messages.log
        std::string conf_path;            // "share_position=1" and the like; "" for none
        uint16_t gpsd_port = 2947;
        int max_clients = 8;
        bool quiet = false;               // no log lines (tests)
    };
    explicit Hub(const Options& o);
    ~Hub();
    Hub(const Hub&) = delete;
    Hub& operator=(const Hub&) = delete;

    // Listen. False (with why) if the port can't be had.
    bool start(std::string* why = nullptr);
    // Wait up to timeout_ms for something to do, and do it.
    void step(int timeout_ms);

    uint16_t port() const { return port_; }
    bool radio_connected() const { return up_ready_; }
    bool radio_refused() const { return up_refused_; }   // nothing is listening on the radio's port
    int clients() const { return int(clients_.size()); }
    int clients_synced() const;
    uint32_t my_num() const { return recorder_.my_num(); }
    long positions_sent() const { return positions_sent_; }

private:
    struct Peer {
        int fd = -1;
        int id = 0;
        Deframer in;
        std::string out;
        bool synced = false;          // has had a full answer to want_config
        bool closing = false;
    };
    struct Ask {
        int client = 0;               // 0: the daemon's own
        uint32_t nonce = 0;
    };
    void log(const char* fmt, ...);
    long now_s() const;
    long now_ms() const;

    void radio_connect();
    void radio_lost(const char* why);
    void radio_read();
    void radio_write();
    void radio_send(const std::string& frame);
    void on_radio(const std::string& payload);
    void next_ask();

    void accept_client();
    void client_read(Peer& c);
    void client_write(Peer& c);
    void client_send(Peer& c, const std::string& frame);
    void on_client(Peer& c, const std::string& payload);
    void drop_closing();
    Peer* peer(int id);

    void record(std::vector<Record>& recs);
    void housekeeping();
    void read_conf();
    void gps_step();
    void gps_line(const std::string& line);
    void gps_close();

    Options opt_;
    int listen_fd_ = -1;
    uint16_t port_ = 0;
    // The radio
    int up_fd_ = -1;
    bool up_connecting_ = false, up_ready_ = false, up_refused_ = false;
    Deframer up_in_;
    std::string up_out_;
    long up_retry_ms_ = 0, up_last_tx_s_ = 0, up_connect_ms_ = 0;
    uint32_t own_nonce_ = 0;
    // Clients
    std::vector<std::unique_ptr<Peer>> clients_;
    int next_client_ = 1;
    std::deque<Ask> asks_;
    bool asking_ = false;
    Ask current_;
    long ask_started_ms_ = 0;
    // History
    Recorder recorder_;
    Journal journal_;
    long last_house_s_ = 0, last_compact_s_ = 0;
    // Position sharing
    bool share_position_ = false;
    long conf_checked_s_ = 0, conf_mtime_ = -1;
    int gps_fd_ = -1;
    bool gps_connecting_ = false;
    std::string gps_in_;
    long gps_retry_s_ = 0, gps_sent_s_ = 0;
    bool gps_fix_ = false, gps_sent_any_ = false;
    double gps_lat_ = 0, gps_lon_ = 0, gps_alt_ = 0, sent_lat_ = 0, sent_lon_ = 0;
    bool gps_has_alt_ = false;
    long gps_fix_s_ = 0;
    long positions_sent_ = 0;
    uint32_t packet_id_ = 0;
};

// One number out of a line of gpsd's JSON ("lat":46.63). False if it isn't there.
bool json_number(const std::string& line, const char* key, double& value);

}  // namespace zs::mesh
