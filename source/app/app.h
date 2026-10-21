#pragma once

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "platform/platform.h"
#include "web/page.h"

// A centred card with a list of choices: the + menu, <select> pickers and the
// "What type of file is this?" download prompt all use it.
struct ListMenu {
    struct Item {
        std::string label, detail;
    };
    std::string title, subtitle, note;
    std::vector<Item> items;
    int sel = 0;
    std::function<void(int)> onPick;  // index, or -1 when cancelled
};

struct App {
    bool quit = false;
    Page page;

    void Init();
    void Shutdown();
    bool Frame(const Platform::Input& in, float dt);
    void Draw();

    // Address-bar style navigation: handles about:, nx: and file links too.
    void Navigate(const std::string& url, const std::string& method = "GET", const std::string& body = "");
    bool Busy() const;
    bool PointAt(const std::string& selector);  // moves the pointer onto an element (CI script)
    void PointerTo(float x, float y);

private:
    struct Hist {
        std::string url;
        float scroll = 0;
    };
    struct Toast {
        std::string text;
        double until = 0;
        bool error = false;
    };

    std::vector<Hist> back_, fwd_;
    std::unique_ptr<ListMenu> menu_;
    std::vector<Toast> toasts_;
    std::vector<std::pair<std::string, std::string>> bookmarks_;  // url, title
    float px_ = 640, py_ = 360;  // pointer
    bool pointerShown_ = true;
    bool tapDown_ = false, tapMoved_ = false;
    float tapX_ = 0, tapY_ = 0;
    float touchScroll_ = 0;
    double lastDraw_ = 0, lastDlRefresh_ = 0;
    int redraw_ = 2;
    size_t doneDownloads_ = 0;

    Rect PageRect() const;
    void Open(const std::string& url, const std::string& method, const std::string& body, float scroll);
    void GoBack();
    void GoForward();
    void Reload();
    void AskAddress();
    void OpenMainMenu();
    void AskFileType(const Page::FileHit& hit);
    void ShowHome(float scroll = 0);
    void ShowDownloads(float scroll = 0);
    void Click(float x, float y);
    void HandleControl(const litehtml::element::ptr& el);
    void HandleNx(const std::string& link);
    void ShowToast(const std::string& text, bool error = false);
    void LoadBookmarks();
    void SaveBookmarks();
    int BookmarkIndex(const std::string& url) const;

    void UpdateBrowse();
    void UpdateMenu();
    void DrawChrome();
    void DrawStatus();
    void DrawPointer();
    void DrawMenu();
    void DrawToasts();
};
