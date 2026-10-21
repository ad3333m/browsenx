#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include <litehtml.h>

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
    bool Busy() const;              // loading, or still waiting on stylesheets/images

private:
    std::shared_ptr<litehtml::document> doc_;
    WebContainer box_;
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
};
