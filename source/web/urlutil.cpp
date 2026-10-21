#include "web/urlutil.h"

#include <cctype>
#include <cstring>

namespace UrlUtil {
namespace {

std::string Lower(std::string s) {
    for (char& c : s) c = (char)tolower((unsigned char)c);
    return s;
}

bool EndsWith(const std::string& s, const char* suffix) {
    size_t n = strlen(suffix);
    return s.size() >= n && Lower(s.substr(s.size() - n)) == suffix;
}

std::string PercentDecode(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '%' && i + 2 < s.size() && isxdigit((unsigned char)s[i + 1]) && isxdigit((unsigned char)s[i + 2])) {
            out += (char)strtol(s.substr(i + 1, 2).c_str(), nullptr, 16);
            i += 2;
        } else {
            out += s[i];
        }
    }
    return out;
}

// Extension of the URL's path (query and fragment ignored), lower-case, with the dot.
std::string PathExtension(const std::string& url) {
    std::string path = url.substr(0, url.find_first_of("?#"));
    size_t slash = path.find_last_of('/');
    size_t dot = path.find_last_of('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return "";
    return Lower(path.substr(dot));
}

const char* const kExt[] = {".nro", ".nsp", ".nsz", ".xci", ""};

}  // namespace

std::string ToUrl(const std::string& typed) {
    size_t a = typed.find_first_not_of(' ');
    if (a == std::string::npos) return "";
    std::string in = typed.substr(a, typed.find_last_not_of(' ') - a + 1);
    if (in.find("://") != std::string::npos || in.compare(0, 6, "about:") == 0) return in;
    if (in.find(' ') == std::string::npos && in.find('.') != std::string::npos) return "https://" + in;
    std::string out = "https://html.duckduckgo.com/html/?q=";
    static const char* hex = "0123456789ABCDEF";
    for (unsigned char c : in) {
        if (isalnum(c) || strchr("-_.~", c)) out += (char)c;
        else if (c == ' ') out += '+';
        else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}

std::string FileName(const std::string& url, const std::string& disposition) {
    std::string name;
    std::string low = Lower(disposition);
    size_t p;
    if ((p = low.find("filename*=")) != std::string::npos) {  // RFC 5987: UTF-8''name%20here
        std::string v = disposition.substr(p + 10);
        v = v.substr(0, v.find(';'));
        size_t q = v.find("''");
        name = PercentDecode(q == std::string::npos ? v : v.substr(q + 2));
    } else if ((p = low.find("filename=")) != std::string::npos) {
        std::string v = disposition.substr(p + 9);
        if (!v.empty() && v[0] == '"') v = v.substr(1, v.find('"', 1) - 1);
        else v = v.substr(0, v.find(';'));
        name = v;
    }
    if (name.empty()) {
        std::string path = url.substr(0, url.find_first_of("?#"));
        name = PercentDecode(path.substr(path.find_last_of('/') + 1));
    }
    std::string out;
    for (char c : name) out += strchr("\\/:*?\"<>|", c) || (unsigned char)c < 32 ? '_' : c;
    while (!out.empty() && (out.back() == ' ' || out.back() == '.')) out.pop_back();
    while (!out.empty() && out[0] == ' ') out.erase(0, 1);
    return out.empty() ? "download" : out;
}

Kind Guess(const std::string& name) {
    if (EndsWith(name, ".nro")) return Kind::Nro;
    if (EndsWith(name, ".nsp")) return Kind::Nsp;
    if (EndsWith(name, ".nsz")) return Kind::Nsz;
    if (EndsWith(name, ".xci") || EndsWith(name, ".xcz")) return Kind::Xci;
    return Kind::Other;
}

const char* Label(Kind k) {
    switch (k) {
        case Kind::Nro: return "NRO app";
        case Kind::Nsp: return "NSP game";
        case Kind::Nsz: return "NSZ game";
        case Kind::Xci: return "XCI / XCZ game";
        default: return "Something else";
    }
}

const char* Folder(Kind k) {
    switch (k) {
        case Kind::Nro: return "switch/";
        case Kind::Nsp: return "games/nsp/";
        case Kind::Nsz: return "games/nsz/";
        case Kind::Xci: return "games/xci/";
        default: return "downloads/";
    }
}

std::string FixExtension(const std::string& name, Kind k) {
    if (k == Kind::Other || Guess(name) == k) return name;
    std::string base = name;
    size_t dot = base.find_last_of('.');
    // Swap a short extension ("game.bin"), keep dots that are part of the name ("v1.2 build").
    if (dot != std::string::npos && dot > 0 && base.size() - dot <= 5 && base.find(' ', dot) == std::string::npos)
        base = base.substr(0, dot);
    return base + kExt[(int)k];
}

bool LooksLikeFile(const std::string& url) {
    static const char* kFiles[] = {".nro", ".nsp", ".nsz", ".xci", ".xcz", ".zip", ".7z",  ".rar", ".tar",
                                   ".gz",  ".bin", ".iso", ".apk", ".ipa", ".exe", ".msi", ".dmg", ".nca"};
    std::string ext = PathExtension(url);
    for (const char* f : kFiles)
        if (ext == f) return true;
    return false;
}

std::string HtmlEscape(const std::string& s) {
    std::string out;
    for (char c : s) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default: out += c;
        }
    }
    return out;
}

}  // namespace UrlUtil
