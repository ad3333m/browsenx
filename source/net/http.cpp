#include "net/http.h"

#include <atomic>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <strings.h>

#include <curl/curl.h>

#include "util/log.h"

namespace Http {
namespace {

std::atomic<bool> g_insecure{false};
std::atomic<bool> g_abort{false};
thread_local CURL* t_curl = nullptr;

CURLSH* g_share = nullptr;
std::mutex g_shareMu[CURL_LOCK_DATA_LAST];

void ShareLock(CURL*, curl_lock_data d, curl_lock_access, void*) { g_shareMu[d].lock(); }
void ShareUnlock(CURL*, curl_lock_data d, void*) { g_shareMu[d].unlock(); }

struct Sink {
    std::string* body;
    size_t max;
    bool overflow = false;
    Response* res = nullptr;
    bool stopOnFile = false;
    bool checked = false;
};

std::string HeaderValue(const char* buf, size_t n, size_t nameLen) {
    std::string v(buf + nameLen, n - nameLen);
    while (!v.empty() && (v.back() == '\r' || v.back() == '\n' || v.back() == ' ')) v.pop_back();
    size_t i = 0;
    while (i < v.size() && v[i] == ' ') i++;
    return v.substr(i);
}

size_t OnBody(char* ptr, size_t size, size_t nmemb, void* user) {
    Sink* s = static_cast<Sink*>(user);
    size_t n = size * nmemb;
    if (s->stopOnFile && !s->checked) {
        s->checked = true;
        if (IsDownload(s->res->contentType, s->res->disposition)) {
            s->res->isFile = true;
            return 0;  // aborts the transfer; the download starts over on its own handle
        }
    }
    if (s->body->size() + n > s->max) {
        s->overflow = true;
        return 0;
    }
    s->body->append(ptr, n);
    return n;
}

size_t OnHeader(char* buf, size_t size, size_t nitems, void* user) {
    size_t n = size * nitems;
    Response* r = static_cast<Response*>(user);
    static const char kRetry[] = "retry-after:";
    static const char kType[] = "content-type:";
    static const char kDisp[] = "content-disposition:";
    static const char kLen[] = "content-length:";
    if (n > 5 && strncmp(buf, "HTTP/", 5) == 0) {  // a new response (after a redirect)
        r->contentType.clear();
        r->disposition.clear();
        r->contentLength = 0;
        r->headers.clear();
        return n;
    }
    if (n > 2) r->headers.append(buf, n);
    if (n > sizeof(kRetry) - 1 && strncasecmp(buf, kRetry, sizeof(kRetry) - 1) == 0) {
        r->retryAfter = atof(buf + sizeof(kRetry) - 1);
    } else if (n > sizeof(kType) - 1 && strncasecmp(buf, kType, sizeof(kType) - 1) == 0) {
        r->contentType = HeaderValue(buf, n, sizeof(kType) - 1);
        for (char& ch : r->contentType) ch = (char)tolower((unsigned char)ch);
    } else if (n > sizeof(kDisp) - 1 && strncasecmp(buf, kDisp, sizeof(kDisp) - 1) == 0) {
        r->disposition = HeaderValue(buf, n, sizeof(kDisp) - 1);
    } else if (n > sizeof(kLen) - 1 && strncasecmp(buf, kLen, sizeof(kLen) - 1) == 0) {
        r->contentLength = strtoull(buf + sizeof(kLen) - 1, nullptr, 10);
    }
    return n;
}

int OnProgress(void*, curl_off_t, curl_off_t, curl_off_t, curl_off_t) { return g_abort.load() ? 1 : 0; }

}  // namespace

void AbortAll() { g_abort = true; }

void GlobalInit() {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    // ponytail: cookies live for the session only; add CURLOPT_COOKIEJAR if logins should survive a restart.
    g_share = curl_share_init();
    curl_share_setopt(g_share, CURLSHOPT_SHARE, CURL_LOCK_DATA_COOKIE);
    curl_share_setopt(g_share, CURLSHOPT_SHARE, CURL_LOCK_DATA_DNS);
    curl_share_setopt(g_share, CURLSHOPT_LOCKFUNC, ShareLock);
    curl_share_setopt(g_share, CURLSHOPT_UNLOCKFUNC, ShareUnlock);
}
void GlobalCleanup() {
    if (g_share) curl_share_cleanup(g_share);
    g_share = nullptr;
    curl_global_cleanup();
}

void ShareCookies(void* handle) {
    CURL* c = static_cast<CURL*>(handle);
    if (g_share) curl_easy_setopt(c, CURLOPT_SHARE, g_share);
    curl_easy_setopt(c, CURLOPT_COOKIEFILE, "");  // turns the cookie engine on
}

namespace {

std::string HostOf(const std::string& url) {
    size_t a = url.find("://");
    a = a == std::string::npos ? 0 : a + 3;
    size_t b = url.find_first_of("/:?#", a);
    std::string h = url.substr(a, b == std::string::npos ? std::string::npos : b - a);
    for (char& ch : h) ch = (char)tolower((unsigned char)ch);
    return h;
}

}  // namespace

std::string CookieHeader(const std::string& url) {
    CURL* c = curl_easy_init();
    if (!c) return "";
    ShareCookies(c);
    struct curl_slist* list = nullptr;
    curl_easy_getinfo(c, CURLINFO_COOKIELIST, &list);
    const std::string host = HostOf(url);
    std::string out;
    for (struct curl_slist* l = list; l; l = l->next) {
        // Netscape format: domain, subdomains, path, secure, expiry, name, value (tab separated).
        std::vector<std::string> f;
        std::string cur;
        for (const char* p = l->data; *p; p++) {
            if (*p == '\t') {
                f.push_back(cur);
                cur.clear();
            } else {
                cur += *p;
            }
        }
        f.push_back(cur);
        if (f.size() < 7 || f[0].rfind("#HttpOnly_", 0) == 0) continue;  // HttpOnly: hidden from scripts
        std::string d = f[0][0] == '.' ? f[0].substr(1) : f[0];
        bool match = host == d || (host.size() > d.size() && host.compare(host.size() - d.size() - 1, std::string::npos,
                                                                          "." + d) == 0);
        if (match) out += (out.empty() ? "" : "; ") + f[5] + "=" + f[6];
    }
    curl_slist_free_all(list);
    curl_easy_cleanup(c);
    return out;
}

void SetCookie(const std::string& url, const std::string& cookie) {
    CURL* c = curl_easy_init();
    if (!c) return;
    ShareCookies(c);
    std::string nameValue = cookie.substr(0, cookie.find(';'));
    size_t eq = nameValue.find('=');
    if (eq == std::string::npos) eq = nameValue.size();
    std::string name = nameValue.substr(0, eq), value = eq < nameValue.size() ? nameValue.substr(eq + 1) : "";
    std::string domain, path = "/";
    long long expiry = 0;  // session cookie
    for (size_t p = cookie.find(';'); p != std::string::npos; p = cookie.find(';', p + 1)) {
        std::string attr = cookie.substr(p + 1, cookie.find(';', p + 1) - p - 1);
        while (!attr.empty() && attr[0] == ' ') attr.erase(0, 1);
        std::string low = attr;
        for (char& ch : low) ch = (char)tolower((unsigned char)ch);
        if (low.rfind("domain=", 0) == 0) domain = attr.substr(7);
        else if (low.rfind("path=", 0) == 0) path = attr.substr(5);
        else if (low.rfind("max-age=", 0) == 0 && atoll(attr.c_str() + 8) <= 0) expiry = 1;  // delete
    }
    while (!domain.empty() && domain[0] == '.') domain.erase(0, 1);
    std::string line = (domain.empty() ? HostOf(url) : "." + domain) + "\t" + (domain.empty() ? "FALSE" : "TRUE") +
                       "\t" + path + "\tFALSE\t" + std::to_string(expiry) + "\t" + name + "\t" + value;
    curl_easy_setopt(c, CURLOPT_COOKIELIST, line.c_str());
    curl_easy_cleanup(c);
}

const char* UserAgent() {
    return "Mozilla/5.0 (X11; Linux aarch64; rv:128.0) Gecko/20100101 Firefox/128.0 BrowseNX/1.0";
}

bool IsDownload(const std::string& type, const std::string& disposition) {
    if (strncasecmp(disposition.c_str(), "attachment", 10) == 0) return true;
    if (type.empty()) return false;  // unlabelled: try to show it
    static const char* kShown[] = {"text/", "application/xhtml", "application/xml", "image/"};
    for (const char* p : kShown)
        if (type.compare(0, strlen(p), p) == 0) return false;
    return true;
}
void SetInsecure(bool insecure) { g_insecure = insecure; }

void ThreadCleanup() {
    if (t_curl) {
        curl_easy_cleanup(t_curl);
        t_curl = nullptr;
    }
}

Response Perform(const Request& req) {
    Response res;
    if (!t_curl) t_curl = curl_easy_init();
    CURL* c = t_curl;
    if (!c) {
        res.error = "curl init failed";
        return res;
    }
    curl_easy_reset(c);

    Sink sink{&res.body, req.maxBytes};
    sink.res = &res;
    sink.stopOnFile = req.stopOnFile;
    struct curl_slist* hdrs = nullptr;
    for (const auto& h : req.headers) hdrs = curl_slist_append(hdrs, h.c_str());

    curl_easy_setopt(c, CURLOPT_URL, req.url.c_str());
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_MAXREDIRS, 10L);
    curl_easy_setopt(c, CURLOPT_USERAGENT, UserAgent());
    ShareCookies(c);
    curl_easy_setopt(c, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT_MS, 10000L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT_MS, req.timeoutMs);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, OnBody);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &sink);
    curl_easy_setopt(c, CURLOPT_HEADERFUNCTION, OnHeader);
    curl_easy_setopt(c, CURLOPT_HEADERDATA, &res);
    curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, OnProgress);
    if (hdrs) curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdrs);
    if (g_insecure) {
        curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 0L);
    }

    if (req.method == "GET") {
        curl_easy_setopt(c, CURLOPT_HTTPGET, 1L);
    } else {
        curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, req.method.c_str());
        if (!req.body.empty() || req.method == "POST" || req.method == "PUT" || req.method == "PATCH") {
            curl_easy_setopt(c, CURLOPT_POSTFIELDS, req.body.c_str());
            curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, (long)req.body.size());
        }
    }

    CURLcode rc = curl_easy_perform(c);
    if (res.isFile) {
        rc = CURLE_OK;
    } else if (rc != CURLE_OK && !sink.overflow) {
        res.error = curl_easy_strerror(rc);
        LOG("http %s %s failed: %s", req.method.c_str(), req.url.c_str(), res.error.c_str());
    } else if (sink.overflow) {
        res.error = "response too large";
    }
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &res.status);
    char* eff = nullptr;
    if (curl_easy_getinfo(c, CURLINFO_EFFECTIVE_URL, &eff) == CURLE_OK && eff) res.finalUrl = eff;
    else res.finalUrl = req.url;
    if (hdrs) curl_slist_free_all(hdrs);
    return res;
}

}  // namespace Http
