// Game controls: which buttons each player is holding right now, from the
// keyboard and any gamepads (USB or Bluetooth), as libretro joypad bitmasks.
//
// ZeroShell's own key handling sees one key at a time and only presses, which
// is right for apps and wrong for games (hold Right and press Jump). So this
// reads the input devices itself, alongside ZeroShell (nothing is grabbed).
// In the simulator there are no devices: press_key() holds a key for a moment.
//
//   Keyboard: W A S D or the arrows move, K = A, J = B, I = X, U = Y,
//             Q = L, E = R, Enter = Start, Space = Select (Tab too)
//             (X and Z also work as A and B on a full-size keyboard)
//             Hold Backspace (Del) to fast forward
//   Gamepad:  as labelled; the Home/Guide button opens the menu, hold the
//             right stick in to fast forward
//
// Players: gamepads take players 1, 2, 3 and 4 in the order they connect. A
// gamepad that drops out (Bluetooth) gets its player back when it returns,
// if no one has taken it meanwhile. The keyboard is always player 1,
// alongside player 1's gamepad.
#pragma once

#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace zs::games {

class Pad {
public:
    static constexpr int kPlayers = 4;

    ~Pad();
    // Open the keyboards and gamepads there are now. False if there are none
    // (the simulator): use press_key().
    bool open();
    void close();
    // Read what happened since the last call; look for new devices now and then.
    void poll();
    // libretro joypad bits (1 << retro::kA ...) held now by player 0..3.
    uint16_t buttons(int player) const;
    // Every gamepad's buttons together, whichever player (for the menu).
    uint16_t gamepad_buttons() const { return pad_any_.load(); }
    // A gamepad's Home/Guide button was pressed since the last call.
    bool take_menu() { return menu_.exchange(false); }
    // Any input since the last call (keeps the screen awake for gamepads).
    bool take_activity() { return activity_.exchange(false); }
    // A gamepad connected since the last call: its player (1..4), else 0.
    int take_joined() { return joined_.exchange(0); }
    // Fast forward is held: Backspace (Del), or a gamepad's right stick click.
    bool fast_forward() const;
    bool has_devices() const { return devices_.load() > 0; }
    int gamepads() const { return gamepads_.load(); }

    // The gamepads of players 1 and 2 trade places (the wrong one connected
    // first, or the keyboard and one gamepad want to be two players).
    void swap_players();
    // A short line for the menu: "P1 keys+pad, P2 pad".
    std::string players_text() const;

    // Tests: a device that is an fd of input_events (a pipe); closing the
    // other end unplugs it. Read on the next poll().
    void add_device(int fd, bool gamepad, const std::string& uniq);

    // ZeroShell key (character or LV_KEY_*) when there are no input devices
    // (the simulator): held for 150 ms of LVGL's clock, for the keyboard's player.
    void press_key(uint32_t key);

private:
    struct Dev {
        int fd = -1;
        std::string path, uniq;          // uniq: the Bluetooth address, if the driver gives one
        bool gamepad = false;
        int player = 0;                  // gamepads (keyboards are player 1)
        int abs_min[2] = {0, 0}, abs_max[2] = {0, 0};   // left stick X, Y
        std::vector<uint8_t> keys = std::vector<uint8_t>(0x300, 0);   // evdev KEY_/BTN_ codes held
        int hat_x = 0, hat_y = 0, stick_x = 0, stick_y = 0;           // -1, 0, 1
    };
    void scan();
    void attach(int fd, const std::string& path, bool gamepad, const std::string& uniq);   // mu_ held
    void handle(Dev& d, uint16_t type, uint16_t code, int32_t value);
    int free_player(const std::string& uniq) const;
    void update();                       // the summaries below, from devs_ (mu_ held)
    static uint16_t dev_buttons(const Dev& d);

    mutable std::mutex mu_;              // devs_: poll() on the emulator thread, the menu on the UI's
    std::vector<Dev> devs_;
    std::map<std::string, int> last_player_;   // uniq -> the player it had
    uint32_t last_scan_ = 0;
    std::atomic<uint16_t> held_[kPlayers] = {};
    std::atomic<uint16_t> pad_any_{0};
    std::atomic<bool> menu_{false}, activity_{false}, fast_{false};
    std::atomic<int> joined_{0}, devices_{0}, gamepads_{0};
    // No devices: held until this lv_tick time (per joypad bit).
    std::atomic<uint32_t> sim_until_[16] = {};
    std::atomic<uint32_t> sim_fast_until_{0};
};

}  // namespace zs::games
