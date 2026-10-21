#include "app/app.h"

#include <algorithm>
#include <cmath>
#include <fstream>

#include "app/downloads.h"
#include "net/async.h"
#include "ui/gfx.h"
#include "ui/icons.h"
#include "ui/images.h"
#include "ui/input.h"
#include "ui/text.h"
#include "ui/theme.h"
#include "util/log.h"
#include "util/strings.h"
#include "web/urlutil.h"

using namespace Platform;

namespace {

constexpr float kTop = 56;     // address bar
constexpr float kBottom = 30;  // status bar

std::string NoScheme(const std::string& u) {
    size_t p = u.find("://");
    std::string s = p == std::string::npos ? u : u.substr(p + 3);
    if (Str::EndsWith(s, "/") && std::count(s.begin(), s.end(), '/') == 1) s.pop_back();
    return s;
}

std::string Host(const std::string& u) {
    std::string s = NoScheme(u);
    return s.substr(0, s.find('/'));
}

const char* kHomeCss = R"css(
body { background:#0e1116; color:#c9d1d9; font-family:sans-serif; margin:0; padding:44px 96px; }
h1 { color:#f0f6fc; font-size:40px; margin:0 0 6px 0; }
h2 { color:#f0f6fc; font-size:20px; margin:30px 0 12px 0; }
.sub { color:#8b949e; font-size:18px; margin-bottom:30px; }
.search { display:block; background:#171b22; border:1px solid #30363d; border-radius:26px; padding:13px 24px;
          color:#8b949e; font-size:19px; text-decoration:none; }
.tile { display:inline-block; width:236px; background:#171b22; border:1px solid #30363d; border-radius:12px;
        padding:14px 16px; margin:0 12px 12px 0; text-decoration:none; vertical-align:top; }
.tile b { color:#f0f6fc; font-size:17px; display:block; margin-bottom:4px; }
.tile span { color:#8b949e; font-size:13px; }
table { border-collapse:collapse; }
td { padding:7px 22px 7px 0; font-size:15px; border-bottom:1px solid #21262d; }
.path { color:#58a6ff; font-family:monospace; }
.muted { color:#8b949e; font-size:14px; }
.bar { background:#21262d; height:8px; border-radius:4px; width:520px; margin:6px 0 2px 0; }
.fill { background:#3d7bff; height:8px; border-radius:4px; }
.row { background:#171b22; border:1px solid #30363d; border-radius:12px; padding:14px 18px; margin-bottom:12px; }
.row b { color:#f0f6fc; font-size:17px; }
.ok { color:#3fb950; } .bad { color:#f85149; }
a.btn { color:#58a6ff; text-decoration:none; font-size:15px; }
)css";

}  // namespace

// ---------------------------------------------------------------------------
// lifecycle
// ---------------------------------------------------------------------------

void App::Init() {
    LoadBookmarks();
    page.onFile = [this](const Page::FileHit& hit) {
        // The link turned out to be a file: the page we were on stays, so undo its history push.
        if (!back_.empty() && back_.back().url == page.url) back_.pop_back();
        AskFileType(hit);
    };
    page.onCommit = [this] { redraw_ = 2; };
    ShowHome();
    if (AppletMode())
        ShowToast("Opened from the album: memory is tight. Hold R while starting a game for full speed.", true);
}

void App::Shutdown() { Downloads::Shutdown(); }

Rect App::PageRect() const { return Rect{0, kTop, (float)Gfx::W, Gfx::H - kTop - kBottom}; }

bool App::Busy() const { return page.Busy() || Images::Busy(); }

bool App::Frame(const Input& in, float dt) {
    Ui::BeginFrame(in, dt);
    const int pumped = Async::Pump();
    const double now = Ui::Time();
    Downloads::Tick(now);

    if (menu_) UpdateMenu();
    else UpdateBrowse();

    // Live downloads page, and a toast when a download finishes.
    size_t done = 0;
    for (auto& t : Downloads::All()) done += t->done ? 1 : 0;
    if (done != doneDownloads_) {
        for (auto& t : Downloads::All())
            if (t->done && !t->reported) {
                if (t->ok) ShowToast("Saved " + t->path);
                else if (t->error != "Cancelled") ShowToast(t->name + ": " + t->error, true);
                t->reported = true;
            }
        doneDownloads_ = done;
        if (page.url == "about:downloads") ShowDownloads(page.scrollY);
    } else if (page.url == "about:downloads" && Downloads::AnyRunning() && now - lastDlRefresh_ > 1.0) {
        ShowDownloads(page.scrollY);
        lastDlRefresh_ = now;
    }

    const bool dirty = page.Tick();
    toasts_.erase(std::remove_if(toasts_.begin(), toasts_.end(), [&](const Toast& t) { return t.until < now; }),
                  toasts_.end());

    const bool want = redraw_ > 0 || pumped > 0 || dirty || Ui::Active() || Ui::Animating() || page.loading ||
                      Images::Busy() || Downloads::AnyRunning() || !toasts_.empty() || now - lastDraw_ > 0.5;
    if (want) {
        Draw();
        if (redraw_ > 0) redraw_--;
        lastDraw_ = now;
    }
    Ui::EndFrame(want);
    Images::EndFrame(want);
    return want;
}

// ---------------------------------------------------------------------------
// navigation
// ---------------------------------------------------------------------------

void App::Navigate(const std::string& raw, const std::string& method, const std::string& body) {
    const std::string url = Str::Trim(raw);
    if (url.empty()) return;
    if (Str::StartsWith(url, "nx:")) {
        HandleNx(url);
        return;
    }
    if (Str::StartsWith(url, "javascript:")) {
        ShowToast("That link needs JavaScript. Try + > Open in Nintendo's browser.", true);
        return;
    }
    if (Str::StartsWith(url, "mailto:") || Str::StartsWith(url, "tel:")) {
        ShowToast("Email and phone links can't be opened on the Switch.", true);
        return;
    }
    if (method == "GET" && page.JumpTo(url)) return;
    if (method == "GET" && UrlUtil::LooksLikeFile(url)) {
        AskFileType({url, UrlUtil::FileName(url, ""), "", 0});
        return;
    }
    if (!page.url.empty()) back_.push_back({page.url, page.scrollY});
    if (back_.size() > 100) back_.erase(back_.begin());
    fwd_.clear();
    Open(url, method, body, 0);
}

void App::Open(const std::string& url, const std::string& method, const std::string& body, float scroll) {
    if (url == "about:home") ShowHome(scroll);
    else if (url == "about:downloads") ShowDownloads(scroll);
    else page.Load(url, method, body, scroll);
    redraw_ = 2;
}

void App::GoBack() {
    if (page.loading) {
        page.Stop();
        return;
    }
    if (back_.empty()) return;
    fwd_.push_back({page.url, page.scrollY});
    Hist h = back_.back();
    back_.pop_back();
    Open(h.url, "GET", "", h.scroll);
}

void App::GoForward() {
    if (fwd_.empty()) return;
    back_.push_back({page.url, page.scrollY});
    Hist h = fwd_.back();
    fwd_.pop_back();
    Open(h.url, "GET", "", h.scroll);
}

void App::Reload() {
    if (page.loading) page.Stop();
    else Open(page.url, "GET", "", page.scrollY);
}

void App::AskAddress() {
    std::string out;
    const std::string initial = Str::StartsWith(page.url, "about:") ? "" : page.url;
    if (Keyboard("Search or type an address", initial, out, 2048)) Navigate(UrlUtil::ToUrl(out));
    redraw_ = 2;
}

void App::HandleNx(const std::string& link) {
    if (link == "nx:address") {
        AskAddress();
    } else if (Str::StartsWith(link, "nx:cancel?")) {
        size_t i = (size_t)atoi(link.c_str() + 10);
        if (i < Downloads::All().size()) Downloads::Cancel(Downloads::All()[i]);
    } else if (link == "nx:home") {
        Navigate("about:home");
    }
}

// ---------------------------------------------------------------------------
// clicks and forms
// ---------------------------------------------------------------------------

void App::PointerTo(float x, float y) {
    const Rect r = PageRect();
    px_ = std::clamp(x, 0.0f, (float)Gfx::W - 1);
    py_ = std::clamp(y, r.y, r.Bottom() - 1);
    page.Hover(px_, py_);
}

bool App::PointAt(const std::string& selector) {
    float x, y;
    if (!page.Locate(selector, x, y)) return false;
    PointerTo(x, y);
    return true;
}

void App::Click(float x, float y) {
    std::string link;
    litehtml::element::ptr control;
    page.Click(x, y, link, control);
    if (control) HandleControl(control);
    else if (!link.empty()) Navigate(link);
    redraw_ = 2;
}

void App::HandleControl(const litehtml::element::ptr& el) {
    const std::string tag = Forms::Tag(el), type = Forms::Type(el);
    if (tag == "select") {
        auto opts = Forms::Options(el);
        if (opts.empty()) return;
        auto m = std::make_unique<ListMenu>();
        m->title = "Choose an option";
        for (auto& o : opts) m->items.push_back({o.first.empty() ? o.second : o.first, ""});
        m->sel = std::clamp(Forms::SelectedIndex(el), 0, (int)opts.size() - 1);
        m->onPick = [this, el](int i) {
            if (i < 0) return;
            Forms::Select(el, i);
            page.Relayout();
        };
        menu_ = std::move(m);
    } else if (type == "checkbox" || type == "radio") {
        Forms::Toggle(el);
        page.Relayout();
    } else if (Forms::IsTextField(el)) {
        std::string guide = el->get_attr("placeholder", "");
        if (guide.empty()) guide = el->get_attr("aria-label", "");
        if (guide.empty()) guide = tag == "textarea" ? "Type your text" : "Type here";
        std::string out;
        if (!Keyboard(guide, Forms::Value(el), out, tag == "textarea" ? 4000 : 500, type == "password",
                      tag == "textarea"))
            return;
        Forms::SetValue(el, out);
        page.Relayout();
        // Like pressing Enter: a form with a single text box (a search) sends at once.
        auto form = Forms::FormOf(el);
        if (tag == "input" && form && Forms::TextFieldCount(form) == 1) {
            auto s = Forms::Build(form, nullptr, page.url);
            Navigate(s.url, s.method, s.body);
        }
    } else if (Forms::IsSubmit(el)) {
        auto form = Forms::FormOf(el);
        if (!form) {
            ShowToast("That button needs JavaScript. Try + > Open in Nintendo's browser.", true);
            return;
        }
        auto s = Forms::Build(form, el, page.url);
        Navigate(s.url, s.method, s.body);
    } else if (type == "file") {
        ShowToast("Uploading files isn't supported.", true);
    } else {
        ShowToast("That button needs JavaScript. Try + > Open in Nintendo's browser.", true);
    }
}

// ---------------------------------------------------------------------------
// downloads
// ---------------------------------------------------------------------------

void App::AskFileType(const Page::FileHit& hit) {
    auto m = std::make_unique<ListMenu>();
    m->title = "What type of file is this?";
    m->subtitle = hit.name + (hit.size ? "   (" + Str::HumanSize(hit.size) + ")" : "");
    if (hit.size >= 0xFFFFFFFFull) m->note = "Over 4 GB: this needs an exFAT SD card (FAT32 can't hold it).";
    for (int k = 0; k < (int)UrlUtil::Kind::Count; k++) {
        auto kind = (UrlUtil::Kind)k;
        m->items.push_back({UrlUtil::Label(kind), SdRoot() + UrlUtil::Folder(kind)});
    }
    m->items.push_back({"Cancel", ""});
    m->sel = (int)UrlUtil::Guess(hit.name);
    const std::string url = hit.url, name = hit.name;
    m->onPick = [this, url, name](int i) {
        if (i < 0 || i >= (int)UrlUtil::Kind::Count) {
            ShowToast("Download cancelled.");
            return;
        }
        auto kind = (UrlUtil::Kind)i;
        const std::string dir = SdRoot() + UrlUtil::Folder(kind);
        auto t = Downloads::Add(url, dir, UrlUtil::FixExtension(name, kind));
        ShowToast("Downloading " + t->name + " to " + dir);
    };
    menu_ = std::move(m);
    redraw_ = 2;
}

void App::ShowDownloads(float scroll) {
    std::string h = std::string("<html><head><title>Downloads</title><style>") + kHomeCss +
                    "</style></head><body><h1>Downloads</h1>";
    const auto& all = Downloads::All();
    if (all.empty()) h += "<p class='muted'>Nothing yet. Open a link to a file and pick its type to start.</p>";
    for (size_t i = all.size(); i-- > 0;) {
        const auto& t = all[i];
        h += "<div class='row'><b>" + UrlUtil::HtmlEscape(t->name) + "</b><br>";
        const uint64_t got = t->received, total = t->total;
        if (t->done && t->ok) {
            h += "<span class='ok'>Saved</span> <span class='path'>" + UrlUtil::HtmlEscape(t->path) + "</span>";
        } else if (t->done) {
            h += "<span class='bad'>" + UrlUtil::HtmlEscape(t->error) + "</span>";
        } else if (!t->started) {
            h += "<span class='muted'>Waiting for the download before it</span> &nbsp; <a class='btn' href='nx:cancel?" +
                 std::to_string(i) + "'>Cancel</a>";
        } else {
            int pct = total ? (int)(got * 100 / total) : 0;
            h += "<div class='bar'><div class='fill' style='width:" + std::to_string(total ? pct * 520 / 100 : 0) +
                 "px'></div></div><span class='muted'>" + Str::HumanSize(got) +
                 (total ? " of " + Str::HumanSize(total) + " (" + std::to_string(pct) + "%)" : "") + " &middot; " +
                 Str::HumanSize((uint64_t)t->speed) + "/s</span> &nbsp; <a class='btn' href='nx:cancel?" +
                 std::to_string(i) + "'>Cancel</a>";
        }
        h += "</div>";
    }
    h += "<p class='muted'>Keep BrowseNX open while files download. The console won't go to sleep meanwhile.</p>";
    h += "</body></html>";
    page.Show("about:downloads", h, scroll);
}

void App::ShowHome(float scroll) {
    std::string h = std::string("<html><head><title>BrowseNX</title><style>") + kHomeCss +
                    "</style></head><body><h1>BrowseNX</h1><div class='sub'>Browse the web and save games and apps "
                    "straight to your SD card.</div><a class='search' href='nx:address'>Search or type an address "
                    "&nbsp;(Y)</a><h2>Bookmarks</h2>";
    for (auto& b : bookmarks_)
        h += "<a class='tile' href='" + UrlUtil::HtmlEscape(b.first) + "'><b>" + UrlUtil::HtmlEscape(b.second) +
             "</b><span>" + UrlUtil::HtmlEscape(Host(b.first)) + "</span></a>";
    h += "<h2>Where downloads go</h2><table>";
    for (int k = 0; k < (int)UrlUtil::Kind::Count; k++) {
        auto kind = (UrlUtil::Kind)k;
        h += "<tr><td>" + std::string(UrlUtil::Label(kind)) + "</td><td class='path'>" + SdRoot() +
             UrlUtil::Folder(kind) + "</td></tr>";
    }
    h += "</table><p class='muted'>Left stick moves the pointer, A clicks, right stick scrolls, B goes back, "
         "+ opens the menu, &minus; shows downloads.</p></body></html>";
    page.Show("about:home", h, scroll);
}

// ---------------------------------------------------------------------------
// bookmarks and toasts
// ---------------------------------------------------------------------------

void App::LoadBookmarks() {
    bookmarks_.clear();
    std::ifstream f(DataDir() + "bookmarks.txt");
    if (!f) {
        bookmarks_ = {{"https://html.duckduckgo.com/html/", "DuckDuckGo"},
                      {"https://en.wikipedia.org/wiki/Main_Page", "Wikipedia"},
                      {"https://github.com/ad3333m", "ad3333m on GitHub"},
                      {"https://news.ycombinator.com/", "Hacker News"}};
        return;
    }
    std::string line;
    while (std::getline(f, line)) {
        size_t tab = line.find('\t');
        if (tab != std::string::npos) bookmarks_.push_back({line.substr(0, tab), line.substr(tab + 1)});
    }
}

void App::SaveBookmarks() {
    std::ofstream f(DataDir() + "bookmarks.txt", std::ios::trunc);
    for (auto& b : bookmarks_) f << b.first << '\t' << b.second << '\n';
}

int App::BookmarkIndex(const std::string& url) const {
    for (size_t i = 0; i < bookmarks_.size(); i++)
        if (bookmarks_[i].first == url) return (int)i;
    return -1;
}

void App::ShowToast(const std::string& text, bool error) {
    toasts_.push_back({text, Ui::Time() + (error ? 5.0 : 3.5), error});
    if (toasts_.size() > 3) toasts_.erase(toasts_.begin());
}

void App::OpenMainMenu() {
    auto m = std::make_unique<ListMenu>();
    m->title = "BrowseNX";
    m->subtitle = page.url;
    const bool internal = Str::StartsWith(page.url, "about:");
    const int bm = BookmarkIndex(page.url);
    std::vector<std::function<void()>> acts;
    auto add = [&](const std::string& label, const std::string& detail, std::function<void()> fn) {
        m->items.push_back({label, detail});
        acts.push_back(std::move(fn));
    };
    add("Home", "", [this] { Navigate("about:home"); });
    if (!internal) {
        if (bm < 0)
            add("Bookmark this page", page.title, [this] {
                bookmarks_.push_back({page.url, page.title.empty() ? Host(page.url) : page.title});
                SaveBookmarks();
                ShowToast("Bookmarked.");
            });
        else
            add("Remove bookmark", page.title, [this, bm] {
                bookmarks_.erase(bookmarks_.begin() + bm);
                SaveBookmarks();
                ShowToast("Bookmark removed.");
            });
        add("Open in Nintendo's browser", "For pages that need JavaScript (no downloads there)", [this] {
            if (!OpenSystemBrowser(page.url))
                ShowToast("Nintendo's browser needs BrowseNX opened through a game (hold R), not the album.", true);
        });
    }
    add("Downloads", Downloads::AnyRunning() ? "In progress" : "", [this] { Navigate("about:downloads"); });
    add("Reload", "", [this] { Reload(); });
    add("Quit BrowseNX", Downloads::AnyRunning() ? "Stops the downloads in progress" : "", [this] { quit = true; });
    m->onPick = [acts](int i) {
        if (i >= 0 && i < (int)acts.size()) acts[i]();
    };
    menu_ = std::move(m);
}

// ---------------------------------------------------------------------------
// input
// ---------------------------------------------------------------------------

void App::UpdateBrowse() {
    const Input& in = Ui::In();
    const Rect pr = PageRect();

    if (Ui::Pressed(BTN_PLUS)) return OpenMainMenu();
    if (Ui::Pressed(BTN_MINUS)) return Navigate("about:downloads");
    if (Ui::Pressed(BTN_Y)) return AskAddress();
    if (Ui::Pressed(BTN_X)) return Reload();
    if (Ui::Pressed(BTN_B) || Ui::Pressed(BTN_L)) return GoBack();
    if (Ui::Pressed(BTN_R)) return GoForward();

    // Left stick: the pointer. A gentle push creeps for small links, a full push crosses the screen.
    float lx = in.lx, ly = in.ly;
    float mag = std::sqrt(lx * lx + ly * ly);
    bool moved = false;
    if (mag > 0.15f) {
        float m = std::min(1.0f, (mag - 0.15f) / 0.85f);
        float speed = 120 + 1150 * m * m;
        float dx = lx / mag * speed * Ui::Dt(), dy = -ly / mag * speed * Ui::Dt();
        float ny = py_ + dy;
        // At the top or bottom edge the page scrolls instead, like Nintendo's browser.
        if (ny < pr.y + 4 && dy < 0) page.ScrollBy(dy * 1.6f);
        if (ny > pr.Bottom() - 6 && dy > 0) page.ScrollBy(dy * 1.6f);
        px_ = std::clamp(px_ + dx, 0.0f, (float)Gfx::W - 1);
        py_ = std::clamp(ny, pr.y, pr.Bottom() - 1);
        moved = true;
        pointerShown_ = true;
    }

    float scroll = Ui::StickScrollDelta(2200);
    if (Ui::Repeat(BTN_DOWN)) scroll += 110;
    if (Ui::Repeat(BTN_UP)) scroll -= 110;
    if (int p = Ui::PageY()) scroll += p * pr.h * 0.85f;
    if (scroll != 0) {
        page.ScrollBy(scroll);
        moved = true;
    }

    // Touch: drags scroll (with a fling) through Ui's scroll zone; a tap clicks where it lands.
    if (Ui::Dragging() || Ui::Animating()) page.SetScroll(touchScroll_);
    touchScroll_ = page.scrollY;
    Ui::ScrollZone(pr, &touchScroll_, page.MaxScroll());
    if (in.touching) {
        if (!tapDown_ && pr.Contains(in.tx, in.ty)) {
            tapDown_ = true;
            tapMoved_ = false;
            tapX_ = in.tx;
            tapY_ = in.ty;
        } else if (tapDown_ && std::hypot(in.tx - tapX_, in.ty - tapY_) > 12) {
            tapMoved_ = true;
        }
    } else if (tapDown_) {
        tapDown_ = false;
        if (!tapMoved_) {
            pointerShown_ = false;
            PointerTo(tapX_, tapY_);
            Click(tapX_, tapY_);
            return;
        }
    }

    if (moved) page.Hover(px_, py_);
    if (Ui::Pressed(BTN_A)) Click(px_, py_);
}

void App::UpdateMenu() {
    ListMenu& m = *menu_;
    const int n = (int)m.items.size();
    auto finish = [this](int i) {
        auto fn = menu_->onPick;
        menu_.reset();
        Ui::Consume();
        redraw_ = 2;
        if (fn) fn(i);
    };
    if (Ui::Pressed(BTN_B)) return finish(-1);
    if (Ui::Pressed(BTN_A)) return finish(m.sel);
    if (int d = Ui::NavY()) m.sel = (m.sel + d + n) % n;
    if (int p = Ui::PageY()) m.sel = std::clamp(m.sel + p * 8, 0, n - 1);
}

// ---------------------------------------------------------------------------
// drawing
// ---------------------------------------------------------------------------

void App::Draw() {
    Gfx::Clear(T().bg);
    page.Draw(PageRect());
    DrawChrome();
    DrawStatus();
    if (!menu_ && pointerShown_) DrawPointer();
    if (menu_) DrawMenu();
    DrawToasts();
    Text::Flush();
}

void App::DrawChrome() {
    const Theme& th = T();
    Gfx::FillRect(Rect{0, 0, (float)Gfx::W, kTop}, th.header);
    Gfx::FillRect(Rect{0, kTop - 1, (float)Gfx::W, 1}, th.border);
    const float cy = kTop / 2;

    auto button = [&](float cx, bool enabled, std::function<void()> tap) {
        Rect r{cx - 20, cy - 20, 40, 40};
        if (Ui::Pressing(r)) Gfx::FillCircle(cx, cy, 20, th.surface3);
        Ui::Hot(r, std::move(tap));
        return enabled ? th.textStrong : th.textFaint;
    };
    Icon::Chevron(34, cy, 18, button(34, !back_.empty() || page.loading, [this] { GoBack(); }), 2);
    Icon::Chevron(78, cy, 18, button(78, !fwd_.empty(), [this] { GoForward(); }), 0);
    Color rc = button(122, true, [this] { Reload(); });
    if (page.loading) {
        Icon::Close(122, cy, 16, rc);
    } else {
        Gfx::Arc(122, cy, 8, 0.9f, 6.0f, 2.2f, rc);
        Gfx::FillPolygon({{128.5f, cy - 9}, {131, cy - 1.5f}, {123.5f, cy - 3}}, rc);
    }

    // Address pill.
    const Rect pill{152, 10, (float)Gfx::W - 152 - 112, 36};
    Gfx::FillRound(pill, 18, Ui::Pressing(pill) ? th.surface3 : th.surface2);
    Ui::Hot(pill, [this] { AskAddress(); });
    float tx = pill.x + 18;
    if (Str::StartsWith(page.url, "https://")) {
        Icon::Lock(tx + 6, cy, 16, th.textMuted);
        tx += 22;
    }
    const float maxW = pill.Right() - tx - (page.loading ? 44 : 16);
    if (Str::StartsWith(page.url, "about:"))
        Text::Draw(page.url == "about:home" ? "Search or type an address" : "Downloads", tx, cy - 10, Face::Regular,
                   16, th.textMuted, maxW);
    else
        Text::Draw(NoScheme(page.url), tx, cy - 10, Face::Regular, 16, th.text, maxW);
    if (page.loading) Icon::Spinner(pill.Right() - 22, cy, 9, th.accent, Ui::Time());

    // Downloads (with a progress ring) and the menu.
    const float dx = (float)Gfx::W - 84;
    Color dc = button(dx, true, [this] { Navigate("about:downloads"); });
    if (auto t = Downloads::Active()) {
        uint64_t total = t->total;
        float f = total ? (float)t->received / (float)total : 0.f;
        Gfx::StrokeCircle(dx, cy, 15, 2, th.surface3);
        Gfx::Arc(dx, cy, 15, -1.5708f, -1.5708f + 6.2832f * f, 2.5f, th.accent);
        dc = th.accent;
    }
    Gfx::Line(dx, cy - 8, dx, cy + 5, 2.2f, dc);
    Gfx::Polyline({{dx - 6, cy - 1}, {dx, cy + 5}, {dx + 6, cy - 1}}, 2.2f, dc);
    Gfx::Line(dx - 8, cy + 9, dx + 8, cy + 9, 2.2f, dc);

    const float mx = (float)Gfx::W - 36;
    Color mc = button(mx, true, [this] { OpenMainMenu(); });
    for (int i = -1; i <= 1; i++) Gfx::FillCircle(mx, cy + i * 7, 2.4f, mc);
}

void App::DrawStatus() {
    const Theme& th = T();
    const Rect bar{0, (float)Gfx::H - kBottom, (float)Gfx::W, kBottom};
    Gfx::FillRect(bar, th.header);
    Gfx::FillRect(Rect{0, bar.y, (float)Gfx::W, 1}, th.border);
    const float ty = bar.y + 6;

    float right = (float)Gfx::W - 14;
    if (auto t = Downloads::Active()) {
        uint64_t got = t->received, total = t->total;
        std::string s = t->name + "   " +
                        (total ? std::to_string((int)(got * 100 / total)) + "%" : Str::HumanSize(got)) + "   " +
                        Str::HumanSize((uint64_t)t->speed) + "/s";
        float w = std::min(520.0f, Text::Width(s, Face::Medium, 14));
        Text::Draw(s, right - w, ty, Face::Medium, 14, th.textStrong, 520);
        right -= w + 16;
        if (total) Gfx::FillRect(Rect{0, (float)Gfx::H - 2, (float)Gfx::W * got / total, 2}, th.accent);
    } else {
        std::vector<std::pair<std::string, std::string>> hints = {{"A", "Click"}, {"B", "Back"}, {"Y", "Address"},
                                                                  {"+", "Menu"}};
        Icon::Hints(right, bar.y + kBottom / 2, hints, th.textMuted, th.surface3, th.textStrong);
        right -= Icon::HintsWidth(hints) + 16;
    }
    std::string left = page.HoverIsLink() ? page.HoverLink() : page.title;
    Text::Draw(left, 14, ty, Face::Regular, 14, page.HoverIsLink() ? th.link : th.textMuted, right - 28);
}

void App::DrawPointer() {
    const Theme& th = T();
    const float x = px_, y = py_;
    const bool link = page.HoverIsLink();
    std::vector<SDL_FPoint> arrow = {{x, y}, {x, y + 22}, {x + 15, y + 16}};
    Gfx::FillPolygon({{x + 1, y + 2}, {x + 1, y + 24}, {x + 16, y + 18}}, Color(0, 0, 0, 70));  // shadow
    Gfx::FillPolygon(arrow, link ? th.accent : Color(255, 255, 255));
    Gfx::Polyline(arrow, 1.5f, link ? Color(255, 255, 255) : Color(20, 20, 20), true);
}

void App::DrawMenu() {
    const Theme& th = T();
    const ListMenu& m = *menu_;
    Gfx::FillRect(Rect{0, 0, (float)Gfx::W, (float)Gfx::H}, th.scrim);
    Ui::Block(Rect{0, 0, (float)Gfx::W, (float)Gfx::H}, [this] {
        auto fn = menu_->onPick;
        menu_.reset();
        if (fn) fn(-1);
    });

    const int visible = std::min((int)m.items.size(), 8);
    const float rowH = 54;
    const float head = 70 + (m.subtitle.empty() ? 0 : 26) + (m.note.empty() ? 0 : 26);
    const float w = 720, h = head + visible * rowH + 54;
    const Rect card{(Gfx::W - w) / 2, (Gfx::H - h) / 2, w, h};
    Gfx::Shadow(Rect{card.x, card.y + 8, card.w, card.h}, 16, 30, Color(0, 0, 0, 160));
    Gfx::FillRound(card, 16, th.surface);
    Gfx::StrokeRound(card, 16, 1, th.border);
    Ui::Block(card);

    float y = card.y + 24;
    Text::Draw(m.title, card.x + 28, y, Face::Bold, 24, th.textStrong, w - 56);
    y += 38;
    if (!m.subtitle.empty()) {
        Text::Draw(m.subtitle, card.x + 28, y, Face::Mono, 15, th.text, w - 56);
        y += 26;
    }
    if (!m.note.empty()) {
        Text::Draw(m.note, card.x + 28, y, Face::Medium, 14, th.warn, w - 56);
        y += 26;
    }
    y += 8;

    const int first = std::clamp(m.sel - visible / 2, 0, std::max(0, (int)m.items.size() - visible));
    for (int i = first; i < first + visible; i++) {
        const Rect r{card.x + 16, y, w - 32, rowH - 6};
        const bool sel = i == m.sel;
        Gfx::FillRound(r, 10, sel ? th.accent : (Ui::Pressing(r) ? th.surface3 : th.surface2));
        const auto& it = m.items[i];
        const float textW = it.detail.empty() ? r.w - 32 : r.w * 0.42f;
        Text::Draw(it.label, r.x + 16, r.y + r.h / 2 - 11, Face::SemiBold, 17, sel ? th.accentText : th.textStrong,
                   textW);
        if (!it.detail.empty())
            Text::DrawRight(Text::Ellipsize(it.detail, Face::Regular, 14, r.w * 0.52f), r.Right() - 16,
                            r.y + r.h / 2 - 10, Face::Regular, 14, sel ? th.accentText : th.textMuted);
        Ui::Hot(r, [this, i] {
            auto fn = menu_->onPick;
            menu_.reset();
            if (fn) fn(i);
        });
        y += rowH;
    }
    std::vector<std::pair<std::string, std::string>> hints = {{"A", "Choose"}, {"B", "Cancel"}};
    Icon::Hints(card.Right() - 24, card.Bottom() - 26, hints, th.textMuted, th.surface3, th.textStrong);
}

void App::DrawToasts() {
    const Theme& th = T();
    float y = (float)Gfx::H - kBottom - 16;
    for (auto it = toasts_.rbegin(); it != toasts_.rend(); ++it) {
        float w = std::min(1000.0f, Text::Width(it->text, Face::Medium, 16) + 40);
        Rect r{(Gfx::W - w) / 2, y - 44, w, 40};
        Gfx::FillRound(r, 20, it->error ? Mix(th.danger, th.surface, 0.55f) : th.surface3);
        Text::DrawCentered(it->text, r.x + w / 2, r.y + 9, Face::Medium, 16, th.textStrong, w - 40);
        y -= 50;
    }
}
