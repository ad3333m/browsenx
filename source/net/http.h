#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Http {

struct Request {
    std::string method = "GET";
    std::string url;
    std::vector<std::string> headers;  // "Name: value"
    std::string body;
    long timeoutMs = 20000;
    size_t maxBytes = 32u << 20;  // images and history pages are far smaller
    // Page loads: stop as soon as the headers say this is a file to download
    // rather than something to show, and report it through Response::isFile.
    bool stopOnFile = false;
};

struct Response {
    long status = 0;
    std::string body;
    std::string error;       // transport error, empty on an HTTP response
    double retryAfter = 0;   // seconds, from a 429
    std::string contentType;   // of the final response, lower-case, e.g. "text/html; charset=utf-8"
    std::string disposition;   // Content-Disposition, as sent
    std::string finalUrl;      // after redirects
    uint64_t contentLength = 0;
    bool isFile = false;       // stopOnFile tripped: body is empty, nothing was read
    bool Ok() const { return status >= 200 && status < 300; }
};

void GlobalInit();
void GlobalCleanup();
// Blocking. Each thread keeps its own handle so connections are reused.
Response Perform(const Request& req);
// Frees the calling thread's handle (call before a worker thread exits).
void ThreadCleanup();
// Turns off certificate checks (a hidden setting for consoles with stale CA stores).
void SetInsecure(bool insecure);
// True when a response with these headers is a download, not a page.
bool IsDownload(const std::string& contentType, const std::string& disposition);
// The browser's User-Agent, shared by pages, images and downloads.
const char* UserAgent();
// Joins every handle to one cookie jar so a login on a page carries over to its downloads.
void ShareCookies(void* curlHandle);
// Makes every transfer in flight fail fast (used when quitting).
void AbortAll();

}  // namespace Http
