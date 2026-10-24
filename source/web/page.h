#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <set>
#include <vector>
#include <string>

#include <litehtml.h>

#include "js/engine.h"
#include "ui/gfx.h"
#include "web/container.h"

// One web page: fetches it, lays it out with litehtml and draws it into a rectangle.
// Navigation that turns out to be a file download is handed back through onFile.
class Page {
public:
    struct FileHit {
        std::string url, name, contentType;
        uint64_t size = 0;
    };

    std::string url;    // what's shown (the address bar), after redirects
    std::string title;
    float scrollY = 0;
    bool loading = false;
    float height = 0;   // laid-out document height
    std::function<void(const FileHit&)> onFile;
    std::function<void()> onCommit;  // a new document replaced the old one
    // Navigation a script asked for (location.href, form.submit(), history.back() as "nx:back" ...).
    std::function<void(const std::string& url, const std::string& method, const std::string& body, bool replace)>
        onNavigate;
    std::function<void(const std::string& text)> onToast;  // alert()
    bool jsEnabled = true;

    // Starts loading. The current document stays up until the new one arrives.
    void Load(const std::string& url, const std::string& method = "GET", const std::string& body = "",
              float restoreScroll = 0);
    // Shows HTML that didn't come from the network (start page, errors).
    void Show(const std::string& url, const std::string& html, float restoreScroll = 0);
    void Stop();
    // Same document, new #fragment: scrolls instead of reloading. False if it's another page.
    bool JumpTo(const std::string& url);
    // Per-frame upkeep: stylesheets and images that arrived trigger a new layout.
    // Returns true when the page needs drawing again.
    bool Tick();

    void Draw(const Rect& view);
    void ScrollBy(float dy) { SetScroll(scrollY + dy); }
    void SetScroll(float y);
    float MaxScroll() const;

    // Pointer input in screen coordinates (view is the rectangle passed to Draw).
    void Hover(float x, float y);
    // Returns what the click hit: a link (absolute URL) or a form control.
    void Click(float x, float y, std::string& link, litehtml::element::ptr& control);
    std::string HoverLink() const;  // URL under the pointer, if any
    bool HoverIsLink() const { return box_.cursor == "pointer"; }
    void Relayout();                // after a form control changed size or content
    // Screen centre of the first element matching a CSS selector (used by the CI script).
    bool Locate(const std::string& selector, float& x, float& y);
    bool Busy() const;
    // JavaScript: events from the user (true = a handler called preventDefault), javascript: URLs, console.
    bool JsEvent(const litehtml::element::ptr& el, const char* type);
    bool RunJs(const std::string& code);
    bool JsRunning() const { return js_ != nullptr; }
    std::vector<std::string> JsConsole() const;
    // Debug (CI script): the element chain under a screen point, e.g. "div#content.mw-body [0,1520 960x4000]".
    std::string Probe(float x, float y);
    // Debug (CI script): elements down to `depth` levels with their laid-out boxes, one per line.
    std::string Outline(int depth);              // loading, or still waiting on stylesheets/images

private:
    WebContainer box_;  // before doc_: the document hands its fonts back to it when destroyed
    std::shared_ptr<litehtml::document> doc_;
    std::unique_ptr<JsEngine> js_;  // after doc_: it goes first, while the elements it holds are still valid
    std::set<std::string> cssInFlight_;
    double lastRestyle_ = 0;
    bool restyleDue_ = false;
    float lastJsScroll_ = 0;
    Rect view_{0, 56, 1280, 636};
    std::string html_, charset_, fragment_;
    int gen_ = 0;       // bumps on every navigation; late results from old ones are dropped
    int cssPass_ = 0;
    int cssWaiting_ = 0;
    double lastLayout_ = 0;
    bool dirty_ = true;
    bool layoutDue_ = false;
    float pendingScroll_ = 0;

    void Commit(const std::string& finalUrl, const std::string& html, const std::string& contentType,
                float restoreScroll);
    void Build(bool first);
    void FetchMissingCss();
    void ScrollToFragment();
    void StartJs();
    void Restyle();
};
