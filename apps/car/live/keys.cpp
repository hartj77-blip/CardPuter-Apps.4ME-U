// zeroos-car-keys: presses the car's buttons that iPod mode's client reports
// (README.md). It reads the client's output: a line "button next" (or play,
// pause, playpause, previous) is pressed on the made-up keyboard of
// car_keys.h; every other line is the client's log and goes on to ours.
//
//   ipod serve /dev/iap0 | zeroos-car-keys [--dry-run]
//
// --dry-run says what it would press instead of pressing it (tests).
#include <cstdio>
#include <cstring>

#include "car_keys.h"

int main(int argc, char** argv) {
    bool dry = argc > 1 && !strcmp(argv[1], "--dry-run");
    int fd = dry ? -1 : car_keys::open_keys();
    if (!dry && fd < 0) fprintf(stderr, "zeroos-car-keys: no /dev/uinput, so the car's buttons won't work\n");
    char line[1024];
    while (fgets(line, sizeof line, stdin)) {
        line[strcspn(line, "\r\n")] = 0;
        int key = strncmp(line, "button ", 7) == 0 ? car_keys::key_for(line + 7) : 0;
        if (!key) {
            fprintf(stderr, "%s\n", line);
            continue;
        }
        fprintf(stderr, "zeroos-car-keys: the car says %s\n", line + 7);
        if (dry) printf("%d\n", key);
        else car_keys::press(fd, key);
        fflush(stdout);
    }
    car_keys::close_keys(fd);
    return 0;
}
