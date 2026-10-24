#include "web/page.h"

#include <algorithm>
#include <functional>
#include <cctype>

#include <litehtml/encodings.h>

#include "net/async.h"
#include "net/http.h"
#include "ui/images.h"
#include "ui/input.h"
#include "util/log.h"
#include "util/strings.h"
#include "web/urlutil.h"

namespace {

std::string ErrorHtml(const std::string& url, const std::string& what) {
    return "<html><head><title>Couldn't open page</title></head>"
           "<body style='background:#0e1116;color:#c9d1d9;font-family:sans-serif;padding:60px 120px'>"
           "<h1 style='color:#f0f6fc'>Couldn't open this page</h1>"
           "<p style='font-size:18px'>" + UrlUtil::HtmlEscape(what) + "</p>"
           "<p style='color:#8b949e'>" + UrlUtil::HtmlEscape(url) + "</p>"
           "<p><a style='color:#58a6ff' href='" + UrlUtil::HtmlEscape(url) + "'>Try again</a></p></body></html>";
}

std::string Charset(const std::string& contentType) {
    size_t p = contentType.find("charset=");
    if (p == std::string::npos) return "";
    std::string v = contentType.substr(p + 8);
    v = v.substr(0, v.find(';'));
    v.erase(std::remove(v.begin(), v.end(), '"'), v.end());
    return Str::Trim(v);
}

}  // namespace

void Page::Load(const std::string& target, const std::string& method, const std::string& body, float restoreScroll) {
    const int gen = ++gen_;
    loading = true;
    dirty_ = true;
    Http::Request req;
    req.method = method;
    req.url = target;
    req.body = body;
    req.stopOnFile = true;
    req.timeoutMs = 45000;
    req.maxBytes = 12u << 20;
    req.headers.push_back("Accept: text/html,application/xhtml+xml,*/*;q=0.8");
    req.headers.push_back("Accept-Language: en-US,en;q=0.7");
    if (method == "POST") req.headers.push_back("Content-Type: application/x-www-form-urlencoded");
    if (!url.empty() && Str::StartsWith(url, "http")) req.headers.push_back("Referer: " + url);
    Async::Run(Async::Pool::Api, [this, gen, req, restoreScroll] {
        auto res = std::make_shared<Http::Response>(Http::Perform(req));
        Async::Post([this, gen, req, res, restoreScroll] {
            if (gen != gen_) return;  // the user went somewhere else meanwhile
            loading = false;
            dirty_ = true;
            if (res->isFile) {
                FileHit hit{res->finalUrl, UrlUtil::FileName(res->finalUrl, res->disposition), res->contentType,
                            res->contentLength};
                if (onFile) onFile(hit);
                return;
            }
            if (!res->error.empty() && res->body.empty()) {
                Show(req.url, ErrorHtml(req.url, res->error));
                return;
            }
            std::string type = res->contentType;
            std::string html;
            if (Str::StartsWith(type, "image/")) {
                html = "<html><body style='margin:0;background:#202124;text-align:center'><img src='" +
                       UrlUtil::HtmlEscape(res->finalUrl) + "' style='max-width:100%'></body></html>";
            } else if (Str::StartsWith(type, "text/plain") || Str::StartsWith(type, "text/css") ||
                       Str::StartsWith(type, "text/javascript")) {
                html = "<html><body><pre style='white-space:pre-wrap'>" + UrlUtil::HtmlEscape(res->body) +
                       "</pre></body></html>";
            } else {
                html = std::move(res->body);
                if (html.empty()) html = ErrorHtml(req.url, "The server sent an empty page (HTTP " +
                                                                 std::to_string(res->status) + ").");
            }
            Commit(res->finalUrl.empty() ? req.url : res->finalUrl, html, type, restoreScroll);
        });
    });
}

void Page::Show(const std::string& u, const std::string& html, float restoreScroll) {
    ++gen_;
    loading = false;
    Commit(u, html, "text/html; charset=utf-8", restoreScroll);
}

void Page::Stop() {
    ++gen_;
    loading = false;
    dirty_ = true;
}

bool Page::JumpTo(const std::string& u) {
    const size_t a = u.find('#');
    if (a == std::string::npos || !doc_ || u.substr(0, a) != url.substr(0, url.find('#'))) return false;
    url = u;
    fragment_ = u.substr(a + 1);
    ScrollToFragment();
    dirty_ = true;
    return true;
}

void Page::Commit(const std::string& finalUrl, const std::string& html, const std::string& contentType,
                  float restoreScroll) {
    url = finalUrl;
    html_ = html;
    charset_ = Charset(contentType);
    size_t hash = finalUrl.find('#');
    fragment_ = hash == std::string::npos ? "" : finalUrl.substr(hash + 1);
    js_.reset();  // the old page's scripts stop before its document goes
    cssPass_ = 0;
    cssWaiting_ = 0;
    cssInFlight_.clear();
    restyleDue_ = false;
    box_.cssCache.clear();
    box_.waitingImages.clear();
    pendingScroll_ = restoreScroll;
    scrollY = 0;
    Build(true);
    if (onCommit) onCommit();
}

void Page::Build(bool first) {
    const float keep = scrollY;
    box_.baseUrl = url;
    box_.title.clear();
    box_.missingCss.clear();
    box_.viewW = view_.w;
    box_.viewH = view_.h;
    litehtml::estring src(html_);
    if (!charset_.empty()) {
        litehtml::encoding enc = litehtml::get_encoding(charset_);
        if (enc != litehtml::encoding::null) src = litehtml::estring(html_, enc);
    }
    static const std::string kMaster = std::string(litehtml::master_css) + kUserCss;
    static const std::string kMasterJs = kMaster + "noscript { display: none; }";
    const bool js = jsEnabled && !Str::StartsWith(url, "about:");
    js_.reset();
    doc_.reset();  // fonts of the old document go before the new one makes its own
    doc_ = litehtml::document::createFromString(src, &box_, js ? kMasterJs : kMaster);
    title = box_.title.empty() ? url : box_.title;
    if (doc_) doc_->render(view_.w);
    height = doc_ ? doc_->height() : 0;
    lastLayout_ = Ui::Time();
    dirty_ = true;
    if (!box_.missingCss.empty() && cssPass_ < 3) FetchMissingCss();
    layoutDue_ = false;
    if (cssWaiting_ == 0) StartJs();  // scripts run on the final document, once its stylesheets are in
    if (!first) SetScroll(keep);  // stylesheets arrived: stay where the reader is
    else if (pendingScroll_ > 0) SetScroll(pendingScroll_);
    else ScrollToFragment();
}

void Page::FetchMissingCss() {
    cssPass_++;
    const int gen = gen_;
    for (const std::string& u : box_.missingCss) {
        if (!cssInFlight_.insert(u).second) continue;
        cssWaiting_++;
        Async::Run(Async::Pool::Api, [this, gen, u] {
            Http::Request req;
            req.url = u;
            req.timeoutMs = 20000;
            req.maxBytes = 4u << 20;
            auto res = std::make_shared<Http::Response>(Http::Perform(req));
            Async::Post([this, gen, u, res] {
                if (gen != gen_) return;
                box_.cssCache[u] = res->Ok() ? res->body : "";
                cssInFlight_.erase(u);
                if (--cssWaiting_ > 0) return;
                // Every sheet is in: lay out again with them. Once scripts run, the document can't be
                // rebuilt (it would lose their changes), so a sheet a script added is applied by restyling.
                if (js_) restyleDue_ = true;
                else Build(false);
            });
        });
    }
}

void Page::ScrollToFragment() {
    if (fragment_.empty() || !doc_) return;
    litehtml::element::ptr el;
    bool plain = std::all_of(fragment_.begin(), fragment_.end(),
                             [](char c) { return isalnum((unsigned char)c) || c == '-' || c == '_'; });
    if (plain && !isdigit((unsigned char)fragment_[0])) el = doc_->root()->select_one("#" + fragment_);
    if (!el && plain) el = doc_->root()->select_one("a[name=\"" + fragment_ + "\"]");
    if (el) SetScroll(el->get_placement().y);
}

void Page::StartJs() {
    if (js_ || !jsEnabled || !doc_ || Str::StartsWith(url, "about:")) return;
    JsEngine::Host h;
    const int gen = gen_;
    h.navigate = [this, gen](const std::string& u, const std::string& method, const std::string& body, bool replace) {
        // Runs after the script returns: navigating replaces this page and its engine.
        Async::Post([this, gen, u, method, body, replace] {
            if (gen == gen_ && onNavigate) onNavigate(u, method, body, replace);
        });
    };
    h.toast = [this](const std::string& t) {
        if (onToast) onToast(t);
    };
    h.setUrl = [this](const std::string& u) {
        url = u;
        dirty_ = true;
    };
    h.scrollY = [this] { return scrollY; };
    h.scrollTo = [this](float y) { SetScroll(y); };
    h.over = [this] {
        return doc_ ? std::const_pointer_cast<litehtml::element>(doc_->get_over_element()) : litehtml::element::ptr();
    };
    lastJsScroll_ = scrollY;
    js_ = std::make_unique<JsEngine>(doc_, url, h);
    js_->Start();
    if (js_->TakeDirty()) Restyle();
}

void Page::Restyle() {
    if (!doc_) return;
    box_.missingCss.clear();
    doc_->restyle();
    Relayout();
    lastRestyle_ = Ui::Time();
    restyleDue_ = false;
    if (!box_.missingCss.empty()) FetchMissingCss();
}

bool Page::JsEvent(const litehtml::element::ptr& el, const char* type) {
    if (!js_ || !el) return false;
    bool prevented = js_->Dispatch(el, type);
    if (js_->TakeDirty()) restyleDue_ = true;
    dirty_ = true;
    return prevented;
}

bool Page::RunJs(const std::string& code) {
    if (!js_) return false;
    js_->RunUrl(code);
    if (js_->TakeDirty()) restyleDue_ = true;
    dirty_ = true;
    return true;
}

std::vector<std::string> Page::JsConsole() const {
    if (!js_) return {};
    return std::vector<std::string>(js_->Console().begin(), js_->Console().end());
}

bool Page::Tick() {
    if (js_) {
        const bool scrolled = scrollY != lastJsScroll_;
        lastJsScroll_ = scrollY;
        if (js_->Tick(0, scrolled)) restyleDue_ = true;
        // ponytail: one restyle per 50 ms at most, so a script animating every frame can't stall the console.
        if (restyleDue_ && Ui::Time() - lastRestyle_ > 0.05) Restyle();
    }
    if (doc_) {
        for (auto it = box_.waitingImages.begin(); it != box_.waitingImages.end();) {
            if (Images::Get(*it)) {  // ready or failed: either way its size is settled
                it = box_.waitingImages.erase(it);
                layoutDue_ = true;
            } else {
                ++it;
            }
        }
        // ponytail: at most one layout per 0.4 s while pictures stream in, so a page of thumbnails stays smooth.
        if (layoutDue_ && Ui::Time() - lastLayout_ > 0.4) Relayout();
    }
    bool d = dirty_;
    dirty_ = false;
    return d;
}

void Page::Relayout() {
    if (!doc_) return;
    doc_->render(view_.w);
    height = doc_->height();
    SetScroll(scrollY);
    layoutDue_ = false;
    lastLayout_ = Ui::Time();
    dirty_ = true;
}

float Page::MaxScroll() const { return std::max(0.0f, height - view_.h); }

void Page::SetScroll(float y) {
    float ny = std::clamp(y, 0.0f, MaxScroll());
    if (ny != scrollY) dirty_ = true;
    scrollY = ny;
}

void Page::Draw(const Rect& view) {
    if (view.w != view_.w || view.h != view_.h) {
        view_ = view;
        Relayout();
    }
    view_ = view;
    Gfx::PushClip(view);
    Gfx::FillRect(view, Color(255, 255, 255));
    if (doc_) {
        box_.originX = view.x;
        box_.originY = view.y - scrollY;
        litehtml::position clip(view.x, view.y, view.w, view.h);
        doc_->draw(0, view.x, view.y - scrollY, &clip);
        box_.FlushText();
    }
    Gfx::PopClip();
}

void Page::Hover(float x, float y) {
    if (!doc_) return;
    litehtml::position::vector boxes;
    if (doc_->on_mouse_over(x - view_.x, y - view_.y + scrollY, x - view_.x, y - view_.y, boxes)) dirty_ = true;
}

void Page::Click(float x, float y, std::string& link, litehtml::element::ptr& control) {
    link.clear();
    control.reset();
    if (!doc_) return;
    box_.clickedLink.clear();
    box_.clickedControl.reset();
    litehtml::position::vector boxes;
    const float dx = x - view_.x, dy = y - view_.y + scrollY;
    doc_->on_mouse_over(dx, dy, x - view_.x, y - view_.y, boxes);
    if (js_) {
        auto target = std::const_pointer_cast<litehtml::element>(doc_->get_over_element());
        if (target && JsEvent(target, "click")) return;  // a handler took over the click
        if (!doc_) return;
    }
    doc_->on_lbutton_down(dx, dy, x - view_.x, y - view_.y, boxes);
    doc_->on_lbutton_up(dx, dy, x - view_.x, y - view_.y, boxes);
    link = box_.clickedLink;
    control = box_.clickedControl;
    box_.clickedControl.reset();
    dirty_ = true;
}

std::string Page::HoverLink() const {
    if (!doc_) return "";
    for (auto el = std::const_pointer_cast<litehtml::element>(doc_->get_over_element()); el; el = el->parent()) {
        if (Forms::Tag(el) == "a") {
            const char* href = el->get_attr("href");
            return href ? box_.Resolve(href) : "";
        }
    }
    return "";
}

bool Page::Locate(const std::string& selector, float& x, float& y) {
    if (!doc_) return false;
    auto el = doc_->root()->select_one(selector);
    if (!el) return false;
    litehtml::position p = el->get_placement();
    SetScroll(p.y - view_.h / 3);
    x = view_.x + p.x + p.width / 2;
    y = view_.y + p.y - scrollY + p.height / 2;
    return true;
}

bool Page::Busy() const {
    return loading || cssWaiting_ > 0 || !box_.waitingImages.empty() || (js_ && js_->Loading());
}

std::string Page::Probe(float x, float y) {
    if (!doc_) return "";
    Hover(x, y);
    std::string out;
    for (auto el = std::const_pointer_cast<litehtml::element>(doc_->get_over_element()); el; el = el->parent()) {
        std::string tag = Forms::Tag(el);
        if (tag.empty()) continue;
        std::string part = tag;
        if (const char* id = el->get_attr("id")) part += std::string("#") + id;
        if (const char* cls = el->get_attr("class")) part += "." + Str::Replace(cls, " ", ".");
        litehtml::position p = el->get_placement();
        part += Str::Format(" [%.0f,%.0f %.0fx%.0f]", p.x, p.y, p.width, p.height);
        out += (out.empty() ? "" : " < ") + part;
    }
    return out;
}

std::string Page::Outline(int depth) {
    std::string out;
    std::function<void(const litehtml::element::ptr&, int)> walk = [&](const litehtml::element::ptr& el, int d) {
        std::string tag = Forms::Tag(el);
        if (tag.empty() || d > depth) return;
        litehtml::position p = el->get_placement();
        std::string line(d * 2, ' ');
        line += tag;
        if (const char* id = el->get_attr("id")) line += std::string("#") + id;
        if (const char* cls = el->get_attr("class")) line += "." + Str::Replace(Str::Utf8Truncate(cls, 60), " ", ".");
        line += Str::Format(" [%.0f,%.0f %.0fx%.0f] disp=%d pos=%d", p.x, p.y, p.width, p.height,
                            (int)el->css().get_display(), (int)el->css().get_position());
        out += line + "|";
        for (auto& c : el->children()) walk(c, d + 1);
    };
    if (doc_) walk(doc_->root(), 0);
    return out;
}
