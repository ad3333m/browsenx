#pragma once

#include <cstdint>
#include <string>

// Pure string helpers (no SDL, no curl) so tests/test_urlutil.cpp can check them on desktop.
namespace UrlUtil {

// Address-bar text -> URL: full URLs stay, bare domains get https://, anything else
// is a DuckDuckGo search (its no-JavaScript HTML version).
std::string ToUrl(const std::string& typed);

// "https://a.b/c/My%20Game.nsp?x=1" -> "My Game.nsp"; Content-Disposition's filename wins.
// The result is safe for the SD card's file system.
std::string FileName(const std::string& url, const std::string& disposition);

// Where a download goes, picked by the user in the "What type of file is this?" prompt.
enum class Kind { Nro, Nsp, Nsz, Xci, Other, Count };
Kind Guess(const std::string& fileName);        // from the extension
const char* Label(Kind k);                      // "NRO app"
const char* Folder(Kind k);                     // relative to the SD root: "switch/", "games/nsp/" ...
// Makes the name end in the kind's extension, so a "download.bin" saved as an NSP becomes "download.nsp".
std::string FixExtension(const std::string& fileName, Kind k);

// True for links that are obviously files, so they skip the page request and go
// straight to the prompt.
bool LooksLikeFile(const std::string& url);

std::string HtmlEscape(const std::string& s);

}  // namespace UrlUtil
