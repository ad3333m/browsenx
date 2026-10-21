// Built and run by the desktop CI job: g++ -I source tests/test_urlutil.cpp source/web/urlutil.cpp
#include <cassert>
#include <cstdio>
#include <string>

#include "web/urlutil.h"

using namespace UrlUtil;

int main() {
    assert(ToUrl("example.com") == "https://example.com");
    assert(ToUrl("  http://x.org/a  ") == "http://x.org/a");
    assert(ToUrl("switch homebrew") == "https://html.duckduckgo.com/html/?q=switch+homebrew");
    assert(ToUrl("a&b") == "https://html.duckduckgo.com/html/?q=a%26b");
    assert(ToUrl("about:home") == "about:home");

    assert(FileName("https://x.org/d/My%20Game%20%5Bv1%5D.nsp?token=1", "") == "My Game [v1].nsp");
    assert(FileName("https://x.org/dl?id=3", "attachment; filename=\"Cool App.nro\"") == "Cool App.nro");
    assert(FileName("https://x.org/dl", "attachment; filename*=UTF-8''G%C3%A4me.nsz") == "G\xC3\xA4me.nsz");
    assert(FileName("https://x.org/", "") == "download");
    assert(FileName("https://x.org/a:b?.xci", "") == "a_b");

    assert(Guess("Game.NSP") == Kind::Nsp);
    assert(Guess("g.xcz") == Kind::Xci);
    assert(Guess("app.nro") == Kind::Nro);
    assert(Guess("readme.txt") == Kind::Other);
    assert(std::string(Folder(Kind::Nro)) == "switch/");
    assert(std::string(Folder(Kind::Nsz)) == "games/nsz/");

    assert(FixExtension("download.bin", Kind::Nsp) == "download.nsp");
    assert(FixExtension("Game v1.2 build", Kind::Xci) == "Game v1.2 build.xci");
    assert(FixExtension("Game.xcz", Kind::Xci) == "Game.xcz");
    assert(FixExtension("notes.txt", Kind::Other) == "notes.txt");

    assert(LooksLikeFile("https://x.org/a/b.NSZ"));
    assert(LooksLikeFile("https://x.org/b.nro?dl=1"));
    assert(!LooksLikeFile("https://x.org/b.html"));
    assert(!LooksLikeFile("https://x.org/v1.2/page"));

    assert(HtmlEscape("<a&\"") == "&lt;a&amp;&quot;");
    puts("urlutil: all tests passed");
    return 0;
}
