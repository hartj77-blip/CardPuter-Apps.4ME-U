// The car's play, pause and skip buttons, when they arrive over USB (USB
// live sound reads them from how the car reads its stick, iPod mode is told
// them by the car): pressed on a made-up keyboard (uinput), which ZeroShell
// reads like a Bluetooth car's buttons, so they reach whatever is playing.
// Needs root, or write access to /dev/uinput.
#pragma once

#include <fcntl.h>
#include <linux/uinput.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>

namespace car_keys {

// The keyboard. -1 if it can't be made.
inline int open_keys() {
    int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) return -1;
    ioctl(fd, UI_SET_EVBIT, EV_KEY);
    for (int k : {KEY_PLAYCD, KEY_PAUSECD, KEY_PLAYPAUSE, KEY_NEXTSONG, KEY_PREVIOUSSONG}) ioctl(fd, UI_SET_KEYBIT, k);
    uinput_setup setup{};
    setup.id.bustype = BUS_VIRTUAL;
    snprintf(setup.name, sizeof setup.name, "ZeroOS car USB buttons");
    if (ioctl(fd, UI_DEV_SETUP, &setup) != 0 || ioctl(fd, UI_DEV_CREATE) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

// Presses and lets go of `key` (KEY_NEXTSONG and so on).
inline void press(int fd, int key) {
    if (fd < 0) return;
    input_event ev[4]{};
    ev[0].type = EV_KEY; ev[0].code = uint16_t(key); ev[0].value = 1;
    ev[1].type = EV_SYN; ev[1].code = SYN_REPORT;
    ev[2].type = EV_KEY; ev[2].code = uint16_t(key); ev[2].value = 0;
    ev[3].type = EV_SYN; ev[3].code = SYN_REPORT;
    if (write(fd, ev, sizeof ev) < 0) fprintf(stderr, "car keys: can't press a key: %s\n", strerror(errno));
}

inline void close_keys(int fd) {
    if (fd < 0) return;
    ioctl(fd, UI_DEV_DESTROY);
    close(fd);
}

// The key for a button's name ("play", "pause", "playpause", "next",
// "previous"), or 0.
inline int key_for(const char* name) {
    if (!strcmp(name, "playpause")) return KEY_PLAYPAUSE;
    if (!strcmp(name, "play")) return KEY_PLAYCD;
    if (!strcmp(name, "pause")) return KEY_PAUSECD;
    if (!strcmp(name, "next")) return KEY_NEXTSONG;
    if (!strcmp(name, "previous")) return KEY_PREVIOUSSONG;
    return 0;
}

}  // namespace car_keys
