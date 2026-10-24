# BrowseNX

A web browser for the Nintendo Switch (homebrew `.nro`) that can download files and put them
in the right place on the SD card.

BrowseNX doesn't use Nintendo's built-in browser, which can't download anything. It draws pages
itself with [litehtml](https://github.com/litehtml/litehtml) (HTML + CSS) on SDL2, so every link
to a file is caught. When you open a file it asks **"What type of file is this?"** and saves it to:

| Type | Folder |
| --- | --- |
| NRO app | `sdmc:/switch/` |
| NSP game | `sdmc:/games/nsp/` |
| NSZ game | `sdmc:/games/nsz/` |
| XCI / XCZ game | `sdmc:/games/xci/` |
| Something else | `sdmc:/downloads/` |

Downloads run in the background, one after another, and keep the console awake. Files over
4 GB need an exFAT SD card.

## Controls

| Input | Action |
| --- | --- |
| Left stick | Move the pointer (the page scrolls at the edges) |
| A / tap | Click |
| Right stick, D-pad, drag | Scroll |
| ZL / ZR | Page up / down |
| B or L / R | Back / forward |
| Y / tap the address bar | Search or type an address |
| X | Reload |
| + | Menu (home, bookmarks, downloads, open in Nintendo's browser, quit) |
| − | Downloads |

## Limits

JavaScript runs on [QuickJS](https://github.com/quickjs-ng/quickjs) with a DOM built over the page
(`source/js/`): scripts (inline, external and modules), DOM changes, events, timers, `fetch`,
`XMLHttpRequest`, `localStorage`, cookies, `URL` and `IntersectionObserver`. Menus, tabs, buttons,
script-driven forms and download buttons work. There's no canvas, video, WebSocket or shadow DOM,
and big app-style sites (Google Docs, YouTube) are too heavy. **+ › Turn JavaScript off** gives
simpler, faster pages, and **+ › JavaScript console** shows a page's messages and errors.

For anything BrowseNX can't show, **+ › Open in Nintendo's browser** opens the page in the system
web applet, which can't download. That needs BrowseNX started through a game (hold R), not from the
album.

Forms work (text boxes, checkboxes, radio buttons, dropdowns, GET and POST); file uploads don't.
Cookies last until you close the app.

## Building

The `.nro` is built by GitHub Actions in the `devkitpro/devkita64` container (`make`). The desktop
job builds the same code against desktop SDL2 and runs `tests/test_urlutil.cpp`. It also drives
the app offscreen through `tests/shots.txt` and uploads the screenshots as the `preview` artifact.

`tools/make_icon.py` redraws `icon.jpg`. `tools/patch_litehtml.py` re-applies BrowseNX's DOM patches
(marked "BrowseNX patch") if litehtml is re-vendored.
