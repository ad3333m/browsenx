#pragma once

#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <litehtml.h>

struct JSRuntime;
struct JSContext;
class Page;

// JavaScript for one page: QuickJS plus a DOM over litehtml's element tree. C++ hands out
// integer node ids (see the __nx natives in engine.cpp); source/js/prelude.js.inc builds
// document, Element, events, timers, fetch and the rest of the browser API on top of them.
class JsEngine {
public:
    struct Host {
        std::function<void(const std::string& url, const std::string& method, const std::string& body,
                            bool replace)> navigate;
        std::function<void(const std::string& text)> toast;
        std::function<void(const std::string& url)> setUrl;  // history.pushState
        std::function<float()> scrollY;
        std::function<void(float y)> scrollTo;
        std::function<litehtml::element::ptr()> over;  // element under the pointer
    };

    JsEngine(std::shared_ptr<litehtml::document> doc, const std::string& url, Host host);
    ~JsEngine();
    JsEngine(const JsEngine&) = delete;
    JsEngine& operator=(const JsEngine&) = delete;

    // Runs the page's <script>s in order (external ones are fetched first), then fires
    // DOMContentLoaded and load. Later script elements added by JS run as they're inserted.
    void Start();
    // Timers, animation frames, promise jobs and finished fetches. Returns true when the DOM
    // changed and needs a restyle.
    bool Tick(double nowMs, bool scrolled);
    bool TakeDirty();
    bool Loading() const { return pendingScripts_ > 0 || !started_; }

    // Events from the user. Each returns true when a handler called preventDefault().
    bool Dispatch(const litehtml::element::ptr& el, const char* type);
    // Runs a javascript: URL.
    void RunUrl(const std::string& code);

    const std::deque<std::string>& Console() const { return console_; }

    // Used by the native bindings.
    int Id(const litehtml::element::ptr& el);
    litehtml::element::ptr El(int id) const;
    void MarkDirty() { dirty_ = true; }
    void Log(const std::string& line);
    void QueueScripts(const litehtml::element::ptr& subtree);
    void Fetch(int requestId, const std::string& method, const std::string& url,
               const std::vector<std::string>& headers, const std::string& body);
    void Write(const std::string& html);  // document.write: inserted after the running script

    std::shared_ptr<litehtml::document> doc;
    std::string url;
    Host host;

private:
    JSRuntime* rt_ = nullptr;
    JSContext* ctx_ = nullptr;
    std::vector<litehtml::element::ptr> nodes_;  // id - 1
    std::unordered_map<const litehtml::element*, int> ids_;
    std::unordered_set<const litehtml::element*> seenScripts_;
    struct Script {
        litehtml::element::ptr el;
        std::string src, code;
        bool module = false, ready = false, failed = false;
    };
    std::deque<std::shared_ptr<Script>> scripts_;  // in document order
    litehtml::element::ptr current_;               // the script running now
    std::deque<std::string> console_;
    int pendingScripts_ = 0;
    bool started_ = false, loadFired_ = false;
    bool dirty_ = false;
    double deadline_ = 0;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);  // async callbacks check it

    void Eval(const std::string& code, const std::string& name, bool module = false);
    void Call(const char* fn, const std::string& argsJson);
    void RunReadyScripts();
    void Arm(double ms);  // interrupt deadline for the next call into JS
    void PumpJobs();
    static int Interrupt(JSRuntime* rt, void* opaque);
    void Install();
};
