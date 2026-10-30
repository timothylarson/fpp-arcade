#pragma once
// Stands in for FPP's Timers.h in tools/host: named callbacks run every so
// many ms from the host's main loop, as FPP runs them from its own.

#include <cstdint>
#include <functional>
#include <map>
#include <string>

class Timers {
public:
    static Timers INSTANCE;

    void addPeriodicTimer(const std::string &name, int ms, std::function<void()> fn) {
        timers[name] = {ms, nowMs + ms, std::move(fn)};
    }
    void stopPeriodicTimer(const std::string &name) { timers.erase(name); }

    // The host calls this every pass of its loop.
    void run(int64_t now) {
        nowMs = now;
        auto due = timers;   // a callback may add or stop timers
        for (auto &kv : due) {
            auto it = timers.find(kv.first);
            if (it != timers.end() && now >= it->second.next) {
                it->second.next = now + it->second.ms;
                kv.second.fn();
            }
        }
    }

private:
    struct Timer {
        int ms;
        int64_t next;
        std::function<void()> fn;
    };
    std::map<std::string, Timer> timers;
    int64_t nowMs = 0;
};
