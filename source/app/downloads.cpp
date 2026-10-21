#include "app/downloads.h"

#include <sys/stat.h>

#include <condition_variable>
#include <cstdio>
#include <deque>
#include <mutex>
#include <thread>

#include <curl/curl.h>

#include "net/http.h"
#include "platform/platform.h"
#include "util/log.h"
#include "util/strings.h"

namespace Downloads {
namespace {

std::vector<std::shared_ptr<Task>> g_all;  // main thread only
std::mutex g_mu;
std::condition_variable g_cv;
std::deque<std::shared_ptr<Task>> g_queue;
std::shared_ptr<Task> g_active;
std::thread g_thread;
bool g_stop = false;

bool Exists(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 || stat((path + ".part").c_str(), &st) == 0;
}

size_t OnBody(char* ptr, size_t size, size_t nmemb, void* user) {
    auto* t = static_cast<std::pair<FILE*, Task*>*>(user);
    const size_t n = size * nmemb;
    if (fwrite(ptr, 1, n, t->first) != n) return 0;
    t->second->received.fetch_add(n);
    return n;
}

int OnProgress(void* user, curl_off_t dltotal, curl_off_t, curl_off_t, curl_off_t) {
    Task* t = static_cast<Task*>(user);
    if (dltotal > 0) t->total.store((uint64_t)dltotal);
    return t->cancel.load() ? 1 : 0;
}

void Run(Task& t) {
    const std::string part = t.path + ".part";
    FILE* f = fopen(part.c_str(), "wb");
    if (!f) {
        t.error = "Couldn't create " + part;
        return;
    }
    setvbuf(f, nullptr, _IOFBF, 1 << 20);  // big writes: the SD card is far faster that way
    CURL* c = curl_easy_init();
    std::pair<FILE*, Task*> sink{f, &t};
    curl_easy_setopt(c, CURLOPT_URL, t.url.c_str());
    curl_easy_setopt(c, CURLOPT_USERAGENT, Http::UserAgent());
    Http::ShareCookies(c);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_MAXREDIRS, 10L);
    curl_easy_setopt(c, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT_MS, 15000L);
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 1L);  // give up after a minute with no data
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, 60L);
    curl_easy_setopt(c, CURLOPT_BUFFERSIZE, 256L * 1024);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, OnBody);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &sink);
    curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, OnProgress);
    curl_easy_setopt(c, CURLOPT_XFERINFODATA, &t);
    CURLcode rc = curl_easy_perform(c);
    curl_easy_cleanup(c);
    const bool closed = fclose(f) == 0;

    if (rc == CURLE_OK && closed && t.received > 0) {
        remove(t.path.c_str());
        if (rename(part.c_str(), t.path.c_str()) == 0) {
            t.ok = true;
            LOG("download: saved %s (%llu bytes)", t.path.c_str(), (unsigned long long)t.received.load());
            return;
        }
        t.error = "Couldn't rename " + part;
    } else if (t.cancel) {
        t.error = "Cancelled";
    } else if (rc == CURLE_WRITE_ERROR || !closed) {
        t.error = t.received >= 0xFFFF0000ull
                      ? "The SD card is FAT32, which can't hold files over 4 GB. Use an exFAT card."
                      : "Couldn't write to the SD card. Is it full?";
    } else {
        t.error = rc == CURLE_OK ? "The server sent an empty file" : curl_easy_strerror(rc);
    }
    remove(part.c_str());
    LOG("download: %s failed: %s", t.url.c_str(), t.error.c_str());
}

void Worker() {
    while (true) {
        std::shared_ptr<Task> t;
        {
            std::unique_lock<std::mutex> lk(g_mu);
            g_cv.wait(lk, [] { return g_stop || !g_queue.empty(); });
            if (g_stop) return;
            t = g_queue.front();
            g_queue.pop_front();
            g_active = t;
        }
        if (!t->cancel) {
            t->started = true;
            Run(*t);
        } else {
            t->error = "Cancelled";
        }
        t->done = true;
        std::lock_guard<std::mutex> lk(g_mu);
        g_active.reset();
    }
}

}  // namespace

std::shared_ptr<Task> Add(const std::string& url, const std::string& dir, const std::string& fileName) {
    Platform::MakeDirs(dir);
    std::string name = fileName;
    const size_t dot = fileName.find_last_of('.');
    const std::string stem = dot == std::string::npos ? fileName : fileName.substr(0, dot);
    const std::string ext = dot == std::string::npos ? "" : fileName.substr(dot);
    for (int i = 1; Exists(dir + name) && i < 1000; i++) name = Str::Format("%s (%d)%s", stem.c_str(), i, ext.c_str());

    auto t = std::make_shared<Task>();
    t->url = url;
    t->path = dir + name;
    t->name = name;
    g_all.push_back(t);
    {
        std::lock_guard<std::mutex> lk(g_mu);
        g_queue.push_back(t);
        if (!g_thread.joinable()) {
            g_stop = false;
            g_thread = std::thread(Worker);
        }
    }
    g_cv.notify_one();
    return t;
}

const std::vector<std::shared_ptr<Task>>& All() { return g_all; }

std::shared_ptr<Task> Active() {
    std::lock_guard<std::mutex> lk(g_mu);
    return g_active;
}

bool AnyRunning() {
    for (auto& t : g_all)
        if (!t->done) return true;
    return false;
}

void Tick(double now) {
    for (auto& t : g_all) {
        if (t->done || !t->started) continue;
        if (t->lastTime == 0) {
            t->lastTime = now;
            t->lastBytes = t->received;
            continue;
        }
        double dt = now - t->lastTime;
        if (dt < 0.5) continue;
        uint64_t got = t->received;
        double inst = (double)(got - t->lastBytes) / dt;
        t->speed = t->speed == 0 ? inst : t->speed * 0.6 + inst * 0.4;
        t->lastBytes = got;
        t->lastTime = now;
    }
    Platform::KeepAwake(AnyRunning());
}

void Cancel(const std::shared_ptr<Task>& t) {
    if (t && !t->done) t->cancel = true;
}

void Shutdown() {
    for (auto& t : g_all) t->cancel = true;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        g_stop = true;
    }
    g_cv.notify_all();
    if (g_thread.joinable()) g_thread.join();
}

}  // namespace Downloads
