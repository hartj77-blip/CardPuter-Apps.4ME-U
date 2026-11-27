// The Jellyfin app's background threads (work.h): what runs first, what can
// be dropped, and that stopping really stops. cmake ... && ctest, or run
// jellyfin_work_test.
#include <unistd.h>

#include <atomic>
#include <cstdio>
#include <thread>

#include "apps/jellyfin/work.h"

using zs::jfapp::Worker;

static int failures = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                \
        }                                                              \
    } while (0)

// poll() until `until` holds, for at most five seconds.
template <typename F>
static bool wait_for(Worker& w, F until) {
    for (int i = 0; i < 5000; ++i) {
        w.poll();
        if (until()) return true;
        usleep(1000);
    }
    return false;
}

// Leaving a screen drops the covers nobody has started on, and nothing else:
// the rest of a long list must still arrive.
static void test_drop_covers() {
    Worker w;
    std::atomic<bool> gate{false};
    std::atomic<int> held{0}, list_work{0}, cover_work{0};
    int held_done = 0, list_done = 0, cover_done = 0;
    // Every thread busy: what is posted next has to wait.
    for (int i = 0; i < 3; ++i)
        w.post(Worker::Urgent, [&](std::atomic<int>&) {
            ++held;
            while (!gate) usleep(500);
        }, [&] { ++held_done; });
    CHECK(wait_for(w, [&] { return held == 3; }));
    for (int i = 0; i < 4; ++i) w.post(Worker::Cover, [&](std::atomic<int>&) { ++cover_work; }, [&] { ++cover_done; });
    for (int i = 0; i < 2; ++i) w.post(Worker::Normal, [&](std::atomic<int>&) { ++list_work; }, [&] { ++list_done; });
    CHECK(w.pending() == 9);
    w.drop_covers();
    gate = true;
    CHECK(wait_for(w, [&] { return held_done == 3 && list_done == 2 && cover_done == 4; }));
    CHECK(list_work == 2);    // the list's requests ran
    CHECK(cover_work == 0);   // the dropped covers' didn't, though their `done` did
    CHECK(w.pending() == 0);
    // Covers posted afterwards run as usual.
    w.post(Worker::Cover, [&](std::atomic<int>&) { ++cover_work; }, [&] { ++cover_done; });
    CHECK(wait_for(w, [&] { return cover_done == 5; }));
    CHECK(cover_work == 1);
}

// stop(): what is queued never runs, what is running is waited for, and no
// `done` runs afterwards. The worker can be used again.
static void test_stop() {
    Worker w;
    std::atomic<bool> gate{false};
    std::atomic<int> started{0}, finished{0};
    int done = 0;
    for (int i = 0; i < 12; ++i)
        w.post(Worker::Normal, [&](std::atomic<int>&) {
            ++started;
            while (!gate) usleep(500);
            ++finished;
        }, [&] { ++done; });
    // Two threads take list work (the third keeps itself for urgent jobs).
    CHECK(wait_for(w, [&] { return started == 2; }));
    std::thread opener([&] {
        usleep(20000);
        gate = true;
    });
    w.stop();   // waits for the two that are running
    opener.join();
    CHECK(started == 2 && finished == 2);   // nothing cut off half-way, nothing new started
    w.poll();
    CHECK(done == 0);
    CHECK(w.pending() == 0);

    w.post(Worker::Urgent, [&](std::atomic<int>&) { ++started; }, [&] { ++done; });
    CHECK(wait_for(w, [&] { return done == 1; }));
    CHECK(started == 3);
}

// The simulator: work runs at once, `done` at the next poll().
static void test_inline() {
    Worker w;
    w.set_inline(true);
    int work = 0, done = 0;
    w.post(Worker::Cover, [&](std::atomic<int>&) { ++work; }, [&] { ++done; });
    CHECK(work == 1 && done == 0);
    w.drop_covers();   // nothing is ever waiting to drop
    w.poll();
    CHECK(done == 1 && w.pending() == 0);
}

int main() {
    test_drop_covers();
    test_stop();
    test_inline();
    if (failures) return 1;
    std::printf("ok   jellyfin_work_test\n");
    return 0;
}
