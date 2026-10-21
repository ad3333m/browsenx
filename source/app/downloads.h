#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// Downloads run one after another on their own thread, so a 10 GB game never holds up
// page loads or images. Each file streams to <path>.part and is renamed when complete.
namespace Downloads {

struct Task {
    std::string url;
    std::string path;  // final destination, e.g. "sdmc:/games/nsp/Game.nsp"
    std::string name;  // file name shown in the UI
    std::atomic<uint64_t> received{0};
    std::atomic<uint64_t> total{0};
    std::atomic<bool> started{false};
    std::atomic<bool> done{false};
    std::atomic<bool> cancel{false};
    std::atomic<bool> ok{false};
    std::string error;  // valid once done
    double speed = 0;   // bytes/s, smoothed by Tick()
    uint64_t lastBytes = 0;
    double lastTime = 0;
    bool reported = false;  // main thread: the "Saved" toast was shown
};

// Picks a free name in dir (appends " (1)", " (2)" ...) and queues the download.
std::shared_ptr<Task> Add(const std::string& url, const std::string& dir, const std::string& fileName);
const std::vector<std::shared_ptr<Task>>& All();
std::shared_ptr<Task> Active();  // the one transferring now, or null
bool AnyRunning();
void Tick(double now);           // updates speeds; call every frame
void Cancel(const std::shared_ptr<Task>& t);
void Shutdown();                 // cancels everything and joins the thread

}  // namespace Downloads
