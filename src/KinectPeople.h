#pragma once
// Who is in front of the Kinect, one entry per LED of the Kinect's model, for
// other plugins to read. fpp-kinect writes it into POSIX shared memory every
// Kinect frame; FPP Arcade's Orb Pop reads it. Shared memory rather than a
// function exported from libfpp-kinect.so keeps the two plugins independent:
// either can be installed, rebuilt or reloaded without the other, and a reader
// never holds a pointer into a library that FPP may unload.
//
// This file is identical in fpp-kinect and fpp-arcade. Change both together,
// and bump VERSION when the layout changes.

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

namespace KinectPeople {

constexpr const char *SHM_NAME = "/fpp-kinect-people";
constexpr uint32_t MAGIC = 0x4C504B46;   // "FKPL"
constexpr uint32_t VERSION = 1;
constexpr int MAX_W = 256;
constexpr int MAX_H = 256;

// flags[] bits
constexpr uint8_t LIT = 1;      // something inside the distance range: what the Kinect picture shows
constexpr uint8_t PERSON = 2;   // LIT, and at least 30 cm nearer than the learned empty scene

static_assert(std::atomic<uint32_t>::is_always_lock_free, "the sequence counter is shared between processes");

struct Frame {
    uint32_t magic;
    uint32_t version;
    std::atomic<uint32_t> seq;   // odd while a frame is being written
    uint32_t width;
    uint32_t height;
    uint32_t reserved;
    int64_t updatedMs;           // monotonicMs() when written
    // Row-major, width * height entries, already flipped the way the Kinect
    // picture is drawn on the model: [y * width + x] is the LED at model (x, y).
    uint16_t depthMm[MAX_W * MAX_H];   // 0 = nothing in range
    uint8_t flags[MAX_W * MAX_H];
};

inline int64_t monotonicMs() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

class Writer {
public:
    ~Writer() {
        if (frame) {
            munmap(frame, sizeof(Frame));
        }
    }
    // Publish one frame. Opens the shared memory on first use and keeps
    // trying on later frames if that fails.
    void publish(int w, int h, const uint16_t *depthMm, const uint8_t *flags) {
        if (!frame && !open()) {
            return;
        }
        w = w < MAX_W ? w : MAX_W;
        h = h < MAX_H ? h : MAX_H;
        const uint32_t s = frame->seq.load(std::memory_order_relaxed);
        frame->seq.store(s + 1, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_release);
        frame->width = w;
        frame->height = h;
        memcpy(frame->depthMm, depthMm, sizeof(uint16_t) * w * h);
        memcpy(frame->flags, flags, (size_t)w * h);
        frame->updatedMs = monotonicMs();
        frame->seq.store(s + 2, std::memory_order_release);
    }
private:
    bool open() {
        const int fd = shm_open(SHM_NAME, O_RDWR | O_CREAT, 0644);
        if (fd < 0) {
            return false;
        }
        if (ftruncate(fd, sizeof(Frame)) != 0) {
            close(fd);
            return false;
        }
        void *p = mmap(nullptr, sizeof(Frame), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        close(fd);
        if (p == MAP_FAILED) {
            return false;
        }
        frame = (Frame *)p;
        frame->magic = MAGIC;
        frame->version = VERSION;
        return true;
    }
    Frame *frame = nullptr;
};

class Reader {
public:
    ~Reader() {
        if (frame) {
            munmap((void *)frame, sizeof(Frame));
        }
    }
    // Copy the latest frame. Returns false when there is none yet (the Kinect
    // plugin is not installed, or has not started), or it is older than
    // maxAgeMs (the Kinect was stopped or unplugged).
    bool read(int &w, int &h, uint16_t *depthMm, uint8_t *flags, int maxAgeMs = 1000) {
        if (!frame && !open()) {
            return false;
        }
        for (int attempt = 0; attempt < 8; ++attempt) {
            const uint32_t s1 = frame->seq.load(std::memory_order_acquire);
            if (s1 & 1) {
                continue;   // mid-write; the writer takes microseconds
            }
            w = std::min<int>(frame->width, MAX_W);
            h = std::min<int>(frame->height, MAX_H);
            const int64_t updated = frame->updatedMs;
            memcpy(depthMm, (const void *)frame->depthMm, sizeof(uint16_t) * w * h);
            memcpy(flags, (const void *)frame->flags, (size_t)w * h);
            std::atomic_thread_fence(std::memory_order_acquire);
            if (frame->seq.load(std::memory_order_relaxed) != s1) {
                continue;
            }
            return w > 0 && h > 0 && monotonicMs() - updated <= maxAgeMs;
        }
        return false;
    }
private:
    bool open() {
        // Opening is only retried once a second, so a game polling every frame
        // with no Kinect plugin installed doesn't make a system call each time.
        const int64_t now = monotonicMs();
        if (now - lastTryMs < 1000) {
            return false;
        }
        lastTryMs = now;
        const int fd = shm_open(SHM_NAME, O_RDONLY, 0);
        if (fd < 0) {
            return false;
        }
        void *p = mmap(nullptr, sizeof(Frame), PROT_READ, MAP_SHARED, fd, 0);
        close(fd);
        if (p == MAP_FAILED) {
            return false;
        }
        const Frame *f = (const Frame *)p;
        if (f->magic != MAGIC || f->version != VERSION) {
            munmap(p, sizeof(Frame));
            return false;
        }
        frame = f;
        return true;
    }
    const Frame *frame = nullptr;
    int64_t lastTryMs = -1000000;
};

}  // namespace KinectPeople
