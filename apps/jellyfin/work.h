// Work off the screen's thread: the Jellyfin app's requests and pictures.
//
//   worker.post(Worker::Urgent, [](std::atomic<int>& pid) { ...slow, no LVGL... },
//               [this] { ...back on the screen's thread, from poll()... });
//
// Three threads and three kinds of job. Urgent ones (the list you just
// opened, the search you are typing) go first and one thread does nothing
// else, so they never wait behind a screenful of covers. Normal ones (the
// rest of a long list) come next. Covers come last, and the ones nobody has
// started on can be dropped when the screen they were for is gone. stop()
// ends everything: queued jobs are dropped, a curl that is running is
// stopped, and no `done` runs after it.
//
// The simulator and the browser build have no server to wait for: with
// set_inline(true) the work runs at once, in post(), and only `done` waits
// for poll(). Timing there is the script's, not a thread's.
#pragma once

#include <atomic>
#include <functional>

#ifndef __EMSCRIPTEN__
#include <signal.h>
#include <unistd.h>

#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#else
#include <deque>
#endif

namespace zs::jfapp {

class Worker {
public:
    using Work = std::function<void(std::atomic<int>& pid)>;
    using Done = std::function<void()>;
    enum Priority { Urgent, Normal, Cover };

    Worker() = default;
    Worker(const Worker&) = delete;
    Worker& operator=(const Worker&) = delete;
    ~Worker() { stop(); }

    void set_inline(bool on) { inline_ = on; }

    void post(Priority priority, Work work, Done done) {
#ifdef __EMSCRIPTEN__
        (void)priority;
        std::atomic<int> pid{0};
        work(pid);
        finished_.push_back(std::move(done));
#else
        if (inline_) {
            std::atomic<int> pid{0};
            work(pid);
            std::lock_guard<std::mutex> l(m_);
            finished_.push_back(std::move(done));
            return;
        }
        {
            std::lock_guard<std::mutex> l(m_);
            queues_[priority].push_back(Job{std::move(work), std::move(done)});
            if (!started_) {
                started_ = true;
                quit_ = false;
                for (int i = 0; i < kThreads; ++i) threads_[i] = std::thread([this, i] { run(i); });
            }
        }
        cv_.notify_all();
#endif
    }

    // From the screen's thread, often: runs the `done` of every finished job.
    void poll() {
        std::deque<Done> ready;
#ifdef __EMSCRIPTEN__
        ready.swap(finished_);
#else
        {
            std::lock_guard<std::mutex> l(m_);
            ready.swap(finished_);
        }
#endif
        for (auto& d : ready)
            if (d) d();
    }

    // Jobs waiting or running, and finished ones poll() hasn't seen.
    int pending() {
#ifdef __EMSCRIPTEN__
        return int(finished_.size());
#else
        std::lock_guard<std::mutex> l(m_);
        return int(queues_[Urgent].size() + queues_[Normal].size() + queues_[Cover].size() + finished_.size()) +
               running_;
#endif
    }

    // Forget the Cover jobs that haven't started (the covers of a screen
    // that is gone). Their `done` runs, with their work not done.
    void drop_covers() {
#ifndef __EMSCRIPTEN__
        std::lock_guard<std::mutex> l(m_);
        for (auto& j : queues_[Cover]) finished_.push_back(std::move(j.done));
        queues_[Cover].clear();
#endif
    }

    void stop() {
#ifdef __EMSCRIPTEN__
        finished_.clear();
#else
        {
            std::lock_guard<std::mutex> l(m_);
            for (auto& q : queues_) q.clear();
            finished_.clear();
            if (!started_) return;
            quit_ = true;
        }
        cv_.notify_all();
        // A curl may start just after we looked: keep stopping them until
        // every thread is out.
        while (exited_.load() < kThreads) {
            for (auto& p : pids_) {
                int pid = p.load();
                if (pid > 0) kill(pid, SIGTERM);
            }
            usleep(2000);
        }
        for (auto& t : threads_)
            if (t.joinable()) t.join();
        std::lock_guard<std::mutex> l(m_);
        finished_.clear();
        started_ = false;
        exited_ = 0;
#endif
    }

private:
#ifndef __EMSCRIPTEN__
    struct Job {
        Work work;
        Done done;
    };
    static constexpr int kThreads = 3;

    void run(int me) {
        while (true) {
            Job job;
            {
                std::unique_lock<std::mutex> l(m_);
                // Thread 0 keeps itself for urgent work.
                auto next = [&]() -> std::deque<Job>* {
                    if (!queues_[Urgent].empty()) return &queues_[Urgent];
                    if (me == 0) return nullptr;
                    if (!queues_[Normal].empty()) return &queues_[Normal];
                    return queues_[Cover].empty() ? nullptr : &queues_[Cover];
                };
                cv_.wait(l, [&] { return quit_ || next() != nullptr; });
                if (quit_) break;
                std::deque<Job>* q = next();
                job = std::move(q->front());
                q->pop_front();
                ++running_;
            }
            job.work(pids_[me]);
            std::lock_guard<std::mutex> l(m_);
            --running_;
            if (!quit_) finished_.push_back(std::move(job.done));
        }
        exited_.fetch_add(1);
    }

    std::mutex m_;
    std::condition_variable cv_;
    std::deque<Job> queues_[3];   // by Priority
    std::thread threads_[kThreads];
    std::atomic<int> pids_[kThreads] = {};
    std::atomic<int> exited_{0};
    bool started_ = false, quit_ = false;
    int running_ = 0;
#endif
    std::deque<Done> finished_;
    bool inline_ = false;
};

}  // namespace zs::jfapp
