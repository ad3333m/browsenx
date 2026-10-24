#include "js/engine.h"

#include <chrono>
#include <cstring>
#include <map>

#include <litehtml/el_text.h>
#include <litehtml/url.h>

#include "net/async.h"
#include "net/http.h"
#include "platform/platform.h"
#include "quickjs.h"
#include "util/log.h"
#include "util/strings.h"
#include "web/container.h"
#include "web/urlutil.h"

using litehtml::element;

namespace {

const char* const kPrelude =
#include "js/prelude.js.inc"
    ;

double WallMs() {
    using namespace std::chrono;
    return (double)duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count() / 1000.0;
}

JsEngine* E(JSContext* c) { return static_cast<JsEngine*>(JS_GetContextOpaque(c)); }

std::string S(JSContext* c, JSValueConst v) {
    size_t n = 0;
    const char* s = JS_ToCStringLen(c, &n, v);
    std::string r = s ? std::string(s, n) : "";
    JS_FreeCString(c, s);
    return r;
}

int I(JSContext* c, JSValueConst v) {
    int32_t i = 0;
    JS_ToInt32(c, &i, v);
    return i;
}

JSValue JStr(JSContext* c, const std::string& s) { return JS_NewStringLen(c, s.data(), s.size()); }

std::string Resolve(const std::string& base, const std::string& href) {
    return litehtml::resolve(litehtml::url(base), litehtml::url(href)).str();
}

bool Attached(const element::ptr& el, const element::ptr& root) {
    element::ptr p = el;
    while (p && p != root) p = p->parent();
    return p == root;
}

// innerHTML / outerHTML
const char* const kVoid[] = {"area", "base", "br", "col", "embed", "hr", "img", "input",
                             "link", "meta", "source", "track", "wbr"};

void Serialize(const element::ptr& el, std::string& out) {
    if (el->is_text()) {
        std::string t;
        el->get_text(t);
        out += UrlUtil::HtmlEscape(t);
        return;
    }
    if (el->is_comment()) return;
    const std::string tag = Forms::Tag(el);
    if (tag.empty()) {  // anonymous boxes (::before and friends)
        for (auto& c : el->children()) Serialize(c, out);
        return;
    }
    out += "<" + tag;
    if (const litehtml::string_map* a = el->attrs())
        for (auto& kv : *a)
            if (kv.first.rfind("nx-", 0) != 0) out += " " + kv.first + "=\"" + UrlUtil::HtmlEscape(kv.second) + "\"";
    out += ">";
    for (const char* v : kVoid)
        if (tag == v) return;
    if (tag == "script" || tag == "style") {
        std::string t;
        el->get_text(t);
        out += t;
    } else {
        for (auto& c : el->children()) Serialize(c, out);
    }
    out += "</" + tag + ">";
}

// localStorage, per origin, for the whole session.
// ponytail: kept in memory only; write it to DataDir if sites need it to survive a restart.
std::map<std::string, std::map<std::string, std::string>> g_storage;

std::string Origin(const std::string& url) {
    size_t a = url.find("://");
    if (a == std::string::npos) return url;
    size_t b = url.find('/', a + 3);
    return url.substr(0, b);
}

// ---------------------------------------------------------------------------
// natives: everything the prelude needs from the C++ side, keyed by node id
// ---------------------------------------------------------------------------

#define NATIVE(name) JSValue name(JSContext* c, JSValueConst, int argc, JSValueConst* argv)
#define EL(i)                                                  \
    element::ptr el = argc > (i) ? E(c)->El(I(c, argv[i])) : nullptr; \
    if (!el) return JS_NULL

NATIVE(n_root) {
    (void)argc, (void)argv;
    return JS_NewInt32(c, E(c)->Id(E(c)->doc->root()));
}

NATIVE(n_kind) {
    EL(0);
    return JS_NewInt32(c, el->is_text() ? 3 : el->is_comment() ? 8 : 1);
}

NATIVE(n_tag) {
    EL(0);
    return JStr(c, Forms::Tag(el));
}

NATIVE(n_parent) {
    EL(0);
    auto p = el->parent();
    return JS_NewInt32(c, p ? E(c)->Id(p) : 0);
}

NATIVE(n_kids) {
    EL(0);
    JSValue arr = JS_NewArray(c);
    uint32_t i = 0;
    for (auto& k : el->children())
        if (!Forms::Tag(k).empty() || k->is_text() || k->is_comment())
            JS_SetPropertyUint32(c, arr, i++, JS_NewInt32(c, E(c)->Id(k)));
    return arr;
}

NATIVE(n_attr) {
    EL(0);
    const char* v = el->get_attr(S(c, argv[1]).c_str());
    return v ? JStr(c, v) : JS_NULL;
}

NATIVE(n_setAttr) {
    EL(0);
    std::string name = Str::Lower(S(c, argv[1]));
    el->set_attr(name.c_str(), S(c, argv[2]).c_str());
    if (name != "value" && name.rfind("nx-", 0) != 0) E(c)->MarkDirty();
    return JS_UNDEFINED;
}

NATIVE(n_delAttr) {
    EL(0);
    el->remove_attr(Str::Lower(S(c, argv[1])).c_str());
    E(c)->MarkDirty();
    return JS_UNDEFINED;
}

NATIVE(n_attrNames) {
    EL(0);
    JSValue arr = JS_NewArray(c);
    uint32_t i = 0;
    if (const litehtml::string_map* a = el->attrs())
        for (auto& kv : *a)
            if (kv.first.rfind("nx-", 0) != 0) JS_SetPropertyUint32(c, arr, i++, JStr(c, kv.first));
    return arr;
}

NATIVE(n_text) {
    EL(0);
    std::string t;
    el->get_text(t);
    return JStr(c, t);
}

// Replaces el's children (or, for a text node, el itself) with parsed text.
NATIVE(n_setText) {
    EL(0);
    JsEngine* e = E(c);
    const std::string text = S(c, argv[1]);
    const std::string tag = Forms::Tag(el);
    if (el->is_text()) {
        auto parent = el->parent();
        if (!parent) return JS_UNDEFINED;
        for (auto& n : e->doc->parse_fragment(UrlUtil::HtmlEscape(text).c_str())) parent->insertBefore(n, el);
        parent->removeChild(el);
    } else if (tag == "script" || tag == "style" || tag == "textarea" || tag == "title") {
        el->clearRecursive();
        el->appendChild(std::make_shared<litehtml::el_text>(text.c_str(), e->doc));
        if (tag == "textarea") Forms::SetValue(el, text);
    } else {
        el->clearRecursive();
        for (auto& n : e->doc->parse_fragment(UrlUtil::HtmlEscape(text).c_str())) el->appendChild(n);
    }
    e->MarkDirty();
    return JS_UNDEFINED;
}

NATIVE(n_html) {
    EL(0);
    std::string out;
    for (auto& k : el->children()) Serialize(k, out);
    return JStr(c, out);
}

NATIVE(n_outer) {
    EL(0);
    std::string out;
    Serialize(el, out);
    return JStr(c, out);
}

NATIVE(n_setHTML) {
    EL(0);
    JsEngine* e = E(c);
    el->clearRecursive();
    for (auto& n : e->doc->parse_fragment(S(c, argv[1]).c_str())) el->appendChild(n);
    e->MarkDirty();  // scripts inside innerHTML don't run, as in real browsers
    return JS_UNDEFINED;
}

NATIVE(n_create) {
    (void)argc;
    JsEngine* e = E(c);
    std::string tag = Str::Lower(S(c, argv[0]));
    auto el = e->doc->create_element(tag.c_str(), {});
    return el ? JS_NewInt32(c, e->Id(el)) : JS_NULL;
}

NATIVE(n_createText) {
    (void)argc;
    JsEngine* e = E(c);
    return JS_NewInt32(c, e->Id(std::make_shared<litehtml::el_text>(S(c, argv[0]).c_str(), e->doc)));
}

NATIVE(n_insert) {
    EL(0);
    JsEngine* e = E(c);
    element::ptr child = e->El(I(c, argv[1]));
    element::ptr before = argc > 2 ? e->El(I(c, argv[2])) : nullptr;
    if (!child || child == el) return JS_FALSE;
    if (auto old = child->parent()) old->removeChild(child);
    bool ok = before ? el->insertBefore(child, before) : el->appendChild(child);
    if (!ok) return JS_FALSE;
    if (Attached(child, e->doc->root())) e->QueueScripts(child);
    e->MarkDirty();
    return JS_TRUE;
}

NATIVE(n_remove) {
    EL(0);
    element::ptr child = E(c)->El(I(c, argv[1]));
    if (child) el->removeChild(child);
    E(c)->MarkDirty();
    return JS_UNDEFINED;
}

NATIVE(n_select) {
    EL(0);
    JSValue arr = JS_NewArray(c);
    uint32_t i = 0;
    try {
        for (auto& m : el->select_all(S(c, argv[1]))) JS_SetPropertyUint32(c, arr, i++, JS_NewInt32(c, E(c)->Id(m)));
    } catch (...) {
    }
    return arr;
}

NATIVE(n_matches) {
    EL(0);
    try {
        return JS_NewBool(c, el->select(S(c, argv[1])) != litehtml::select_no_match);
    } catch (...) {
        return JS_FALSE;
    }
}

NATIVE(n_rect) {
    EL(0);
    litehtml::position p = el->get_placement();
    JSValue arr = JS_NewArray(c);
    JS_SetPropertyUint32(c, arr, 0, JS_NewFloat64(c, p.x));
    JS_SetPropertyUint32(c, arr, 1, JS_NewFloat64(c, p.y));
    JS_SetPropertyUint32(c, arr, 2, JS_NewFloat64(c, p.width));
    JS_SetPropertyUint32(c, arr, 3, JS_NewFloat64(c, p.height));
    return arr;
}

NATIVE(n_display) {
    EL(0);
    static const std::vector<std::string> kNames = Str::Split(style_display_strings, ';');
    int d = (int)el->css().get_display();
    return JStr(c, d >= 0 && d < (int)kNames.size() ? kNames[d] : "block");
}

NATIVE(n_viewport) {
    (void)argc, (void)argv;
    JsEngine* e = E(c);
    litehtml::position v;
    e->doc->container()->get_viewport(v);
    JSValue arr = JS_NewArray(c);
    JS_SetPropertyUint32(c, arr, 0, JS_NewFloat64(c, v.width));
    JS_SetPropertyUint32(c, arr, 1, JS_NewFloat64(c, v.height));
    JS_SetPropertyUint32(c, arr, 2, JS_NewFloat64(c, e->host.scrollY ? e->host.scrollY() : 0));
    JS_SetPropertyUint32(c, arr, 3, JS_NewFloat64(c, e->doc->height()));
    return arr;
}

NATIVE(n_scrollTo) {
    (void)argc;
    double y = 0;
    JS_ToFloat64(c, &y, argv[0]);
    if (E(c)->host.scrollTo) E(c)->host.scrollTo((float)y);
    return JS_UNDEFINED;
}

NATIVE(n_navigate) {
    JsEngine* e = E(c);
    std::string target = Resolve(e->url, S(c, argv[0]));
    std::string method = argc > 1 && JS_IsString(argv[1]) ? S(c, argv[1]) : "GET";
    std::string body = argc > 2 && JS_IsString(argv[2]) ? S(c, argv[2]) : "";
    bool replace = argc > 3 && JS_ToBool(c, argv[3]) > 0;
    if (e->host.navigate) e->host.navigate(target, method, body, replace);
    return JS_UNDEFINED;
}

NATIVE(n_submit) {
    EL(0);
    JsEngine* e = E(c);
    auto s = Forms::Build(el, argc > 1 ? e->El(I(c, argv[1])) : nullptr, e->url);
    if (e->host.navigate) e->host.navigate(s.url, s.method, s.body, false);
    return JS_UNDEFINED;
}

NATIVE(n_setUrl) {
    (void)argc;
    JsEngine* e = E(c);
    e->url = Resolve(e->url, S(c, argv[0]));
    if (e->host.setUrl) e->host.setUrl(e->url);
    return JS_UNDEFINED;
}

NATIVE(n_url) {
    (void)argc, (void)argv;
    return JStr(c, E(c)->url);
}

NATIVE(n_resolve) {
    (void)argc;
    return JStr(c, Resolve(S(c, argv[0]), S(c, argv[1])));
}

NATIVE(n_log) {
    std::string line = S(c, argv[0]) + ":";
    for (int i = 1; i < argc; i++) line += " " + S(c, argv[i]);
    E(c)->Log(line);
    return JS_UNDEFINED;
}

NATIVE(n_alert) {
    (void)argc;
    if (E(c)->host.toast) E(c)->host.toast(S(c, argv[0]));
    return JS_UNDEFINED;
}

NATIVE(n_prompt) {
    std::string out;
    if (!Platform::Keyboard(S(c, argv[0]), argc > 1 ? S(c, argv[1]) : "", out, 1000)) return JS_NULL;
    return JStr(c, out);
}

NATIVE(n_storage) {
    auto& m = g_storage[Origin(E(c)->url)];
    std::string op = S(c, argv[0]);
    if (op == "get") {
        auto it = m.find(S(c, argv[1]));
        return it == m.end() ? JS_NULL : JStr(c, it->second);
    }
    if (op == "set") m[S(c, argv[1])] = S(c, argv[2]);
    else if (op == "del") m.erase(S(c, argv[1]));
    else if (op == "clear") m.clear();
    else if (op == "keys") {
        JSValue arr = JS_NewArray(c);
        uint32_t i = 0;
        for (auto& kv : m) JS_SetPropertyUint32(c, arr, i++, JStr(c, kv.first));
        return arr;
    }
    (void)argc;
    return JS_UNDEFINED;
}

NATIVE(n_cookie) {
    if (argc > 0 && JS_IsString(argv[0])) {
        Http::SetCookie(E(c)->url, S(c, argv[0]));
        return JS_UNDEFINED;
    }
    return JStr(c, Http::CookieHeader(E(c)->url));
}

NATIVE(n_fetch) {
    JsEngine* e = E(c);
    std::vector<std::string> headers;
    if (argc > 3 && JS_IsArray(argv[3])) {
        int64_t n = 0;
        JS_GetLength(c, argv[3], &n);
        for (int64_t i = 0; i < n; i++) {
            JSValue h = JS_GetPropertyUint32(c, argv[3], (uint32_t)i);
            headers.push_back(S(c, h));
            JS_FreeValue(c, h);
        }
    }
    e->Fetch(I(c, argv[0]), S(c, argv[1]), Resolve(e->url, S(c, argv[2])), headers,
             argc > 4 && JS_IsString(argv[4]) ? S(c, argv[4]) : "");
    return JS_UNDEFINED;
}

NATIVE(n_write) {
    (void)argc;
    E(c)->Write(S(c, argv[0]));
    return JS_UNDEFINED;
}

NATIVE(n_over) {
    (void)argc, (void)argv;
    JsEngine* e = E(c);
    auto el = e->host.over ? e->host.over() : nullptr;
    return JS_NewInt32(c, el ? e->Id(el) : 0);
}

NATIVE(n_now) {
    (void)argc, (void)argv;
    static const double start = WallMs();
    return JS_NewFloat64(c, WallMs() - start);
}

NATIVE(n_dirty) {
    (void)argc, (void)argv;
    E(c)->MarkDirty();
    return JS_UNDEFINED;
}

const struct {
    const char* name;
    JSCFunction* fn;
    int args;
} kNatives[] = {
    {"root", n_root, 0},       {"kind", n_kind, 1},         {"tag", n_tag, 1},
    {"parent", n_parent, 1},   {"kids", n_kids, 1},         {"attr", n_attr, 2},
    {"setAttr", n_setAttr, 3}, {"delAttr", n_delAttr, 2},   {"attrNames", n_attrNames, 1},
    {"text", n_text, 1},       {"setText", n_setText, 2},   {"html", n_html, 1},
    {"outer", n_outer, 1},     {"setHTML", n_setHTML, 2},   {"create", n_create, 1},
    {"createText", n_createText, 1}, {"insert", n_insert, 3}, {"remove", n_remove, 2},
    {"select", n_select, 2},   {"matches", n_matches, 2},   {"rect", n_rect, 1},
    {"display", n_display, 1}, {"viewport", n_viewport, 0}, {"scrollTo", n_scrollTo, 1},
    {"navigate", n_navigate, 4}, {"submit", n_submit, 2},   {"setUrl", n_setUrl, 1},
    {"url", n_url, 0},         {"resolve", n_resolve, 2},   {"log", n_log, 2},
    {"alert", n_alert, 1},     {"prompt", n_prompt, 2},     {"storage", n_storage, 3},
    {"cookie", n_cookie, 1},   {"fetch", n_fetch, 5},       {"write", n_write, 1},
    {"over", n_over, 0},       {"dirty", n_dirty, 0},       {"now", n_now, 0},
};

// Modules: imports are fetched synchronously while the importing script runs.
// ponytail: blocking fetch on the main thread; fine for the handful of modules small sites use.
JSModuleDef* LoadModule(JSContext* c, const char* name, void*) {
    Http::Request req;
    req.url = name;
    req.timeoutMs = 20000;
    Http::Response r = Http::Perform(req);
    if (!r.Ok()) {
        JS_ThrowReferenceError(c, "could not load module '%s'", name);
        return nullptr;
    }
    JSValue fn = JS_Eval(c, r.body.data(), r.body.size(), name, JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
    if (JS_IsException(fn)) return nullptr;
    auto* m = static_cast<JSModuleDef*>(JS_VALUE_GET_PTR(fn));
    JS_FreeValue(c, fn);
    return m;
}

char* NormalizeModule(JSContext* c, const char* base, const char* name, void*) {
    std::string full = Resolve(base, name);
    char* out = static_cast<char*>(js_malloc(c, full.size() + 1));
    if (out) memcpy(out, full.c_str(), full.size() + 1);
    return out;
}

}  // namespace

// ---------------------------------------------------------------------------
// JsEngine
// ---------------------------------------------------------------------------

JsEngine::JsEngine(std::shared_ptr<litehtml::document> d, const std::string& u, Host h)
    : doc(std::move(d)), url(u), host(std::move(h)) {
    rt_ = JS_NewRuntime();
    JS_SetMemoryLimit(rt_, Platform::AppletMode() ? (96u << 20) : (256u << 20));
    JS_SetMaxStackSize(rt_, 256 * 1024);
    JS_SetInterruptHandler(rt_, Interrupt, this);
    JS_SetModuleLoaderFunc(rt_, NormalizeModule, LoadModule, nullptr);
    ctx_ = JS_NewContext(rt_);
    JS_SetContextOpaque(ctx_, this);
    Install();
}

JsEngine::~JsEngine() {
    *alive_ = false;
    if (ctx_) JS_FreeContext(ctx_);
    if (rt_) JS_FreeRuntime(rt_);
}

int JsEngine::Interrupt(JSRuntime*, void* opaque) {
    auto* e = static_cast<JsEngine*>(opaque);
    return e->deadline_ > 0 && WallMs() > e->deadline_;
}

void JsEngine::Arm(double ms) { deadline_ = WallMs() + ms; }

void JsEngine::Install() {
    JSValue global = JS_GetGlobalObject(ctx_);
    JSValue nx = JS_NewObject(ctx_);
    for (auto& n : kNatives) JS_SetPropertyStr(ctx_, nx, n.name, JS_NewCFunction(ctx_, n.fn, n.name, n.args));
    JS_SetPropertyStr(ctx_, global, "__nx", nx);
    JS_FreeValue(ctx_, global);
    Arm(5000);
    Eval(kPrelude, "<browsenx>");
    Call("__nxInit", "[" + std::string(Platform::AppletMode() ? "true" : "false") + "]");
}

void JsEngine::Log(const std::string& line) {
    LOG("js %s", line.c_str());
    console_.push_back(line);
    if (console_.size() > 300) console_.pop_front();
}

void JsEngine::Eval(const std::string& code, const std::string& name, bool module) {
    JSValue r = JS_Eval(ctx_, code.c_str(), code.size(), name.c_str(),
                        module ? JS_EVAL_TYPE_MODULE : JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(r)) {
        JSValue ex = JS_GetException(ctx_);
        std::string msg = S(ctx_, ex);
        JSValue stack = JS_GetPropertyStr(ctx_, ex, "stack");
        if (JS_IsString(stack)) msg += "\n" + S(ctx_, stack);
        JS_FreeValue(ctx_, stack);
        JS_FreeValue(ctx_, ex);
        Log("error: " + msg + " (" + name + ")");
    }
    JS_FreeValue(ctx_, r);
    PumpJobs();
}

// Calls a global function with JSON arguments; the prelude's entry points all start with __nx.
void JsEngine::Call(const char* fn, const std::string& argsJson) { Eval(std::string(fn) + ".apply(null," + argsJson + ")", fn); }

void JsEngine::PumpJobs() {
    JSContext* c = nullptr;
    for (int i = 0; i < 10000; i++) {
        int r = JS_ExecutePendingJob(rt_, &c);
        if (r == 0) break;
        if (r < 0) {
            JSValue ex = JS_GetException(c);
            Log("error: " + S(c, ex) + " (promise job)");
            JS_FreeValue(c, ex);
        }
    }
}

int JsEngine::Id(const element::ptr& el) {
    if (!el) return 0;
    auto it = ids_.find(el.get());
    if (it != ids_.end()) return it->second;
    nodes_.push_back(el);
    int id = (int)nodes_.size();
    ids_[el.get()] = id;
    return id;
}

element::ptr JsEngine::El(int id) const { return id > 0 && id <= (int)nodes_.size() ? nodes_[id - 1] : nullptr; }

bool JsEngine::TakeDirty() {
    bool d = dirty_;
    dirty_ = false;
    return d;
}

void JsEngine::Start() {
    started_ = true;
    QueueScripts(doc->root());
    RunReadyScripts();
}

void JsEngine::QueueScripts(const element::ptr& subtree) {
    std::vector<element::ptr> found;
    std::function<void(const element::ptr&)> walk = [&](const element::ptr& el) {
        if (Forms::Tag(el) == "script") found.push_back(el);
        for (auto& k : el->children()) walk(k);
    };
    walk(subtree);
    for (auto& el : found) {
        if (!seenScripts_.insert(el.get()).second) continue;
        const std::string type = Str::Lower(Str::Trim(el->get_attr("type", "")));
        const bool module = type == "module";
        static const char* kJs[] = {"", "text/javascript", "application/javascript", "module", "text/ecmascript",
                                    "application/ecmascript", "application/x-javascript"};
        bool js = false;
        for (const char* t : kJs) js = js || type == t;
        if (!js || el->get_attr("nomodule")) continue;
        auto s = std::make_shared<Script>();
        s->el = el;
        s->module = module;
        const char* src = el->get_attr("src");
        if (src && *src) {
            s->src = Resolve(url, src);
            pendingScripts_++;
            std::weak_ptr<bool> alive = alive_;
            Http::Request req;
            req.url = s->src;
            req.timeoutMs = 30000;
            req.maxBytes = 16u << 20;
            Async::Run(Async::Pool::Api, [this, alive, s, req] {
                auto r = std::make_shared<Http::Response>(Http::Perform(req));
                Async::Post([this, alive, s, r] {
                    auto a = alive.lock();
                    if (!a || !*a) return;
                    s->code = std::move(r->body);
                    s->failed = !r->Ok();
                    s->ready = true;
                    pendingScripts_--;
                    RunReadyScripts();
                });
            });
        } else {
            el->get_text(s->code);
            s->ready = true;
        }
        scripts_.push_back(s);
    }
}

void JsEngine::RunReadyScripts() {
    while (!scripts_.empty() && scripts_.front()->ready) {
        auto s = scripts_.front();
        scripts_.pop_front();
        if (s->failed) {
            Log("error: couldn't load script " + s->src);
            Dispatch(s->el, "error");
            continue;
        }
        current_ = s->el;
        Call("__nxSetCurrentScript", "[" + std::to_string(Id(s->el)) + "]");
        Arm(8000);
        Eval(s->code, s->src.empty() ? url + " (inline script)" : s->src, s->module);
        current_.reset();
        Call("__nxSetCurrentScript", "[0]");
        if (!s->src.empty()) Dispatch(s->el, "load");
    }
    if (scripts_.empty() && started_ && !loadFired_) {
        loadFired_ = true;
        Arm(5000);
        Call("__nxLoaded", "[]");
    }
}

void JsEngine::Write(const std::string& html) {
    auto nodes = doc->parse_fragment(html.c_str());
    element::ptr parent = current_ ? current_->parent() : nullptr;
    element::ptr next;
    if (parent) {
        bool after = false;
        for (auto& k : parent->children()) {
            if (after) {
                next = k;
                break;
            }
            after = k == current_;
        }
    } else {
        auto bodies = doc->root()->select_all("body");
        parent = bodies.empty() ? doc->root() : bodies.front();
    }
    for (auto& n : nodes) {
        if (next) parent->insertBefore(n, next);
        else parent->appendChild(n);
        QueueScripts(n);
    }
    MarkDirty();
}

void JsEngine::Fetch(int requestId, const std::string& method, const std::string& target,
                     const std::vector<std::string>& headers, const std::string& body) {
    Http::Request req;
    req.method = method.empty() ? "GET" : Str::Lower(method) == "get" ? "GET" : method;
    req.url = target;
    req.headers = headers;
    req.body = body;
    req.timeoutMs = 30000;
    req.maxBytes = 24u << 20;
    if (!url.empty()) req.headers.push_back("Referer: " + url);
    std::weak_ptr<bool> alive = alive_;
    Async::Run(Async::Pool::Api, [this, alive, req, requestId] {
        auto r = std::make_shared<Http::Response>(Http::Perform(req));
        Async::Post([this, alive, r, requestId] {
            auto a = alive.lock();
            if (!a || !*a) return;
            JSValue args[6] = {JS_NewInt32(ctx_, requestId), JS_NewInt32(ctx_, r->status == 0 && r->Ok() ? 200 : (int)r->status), JStr(ctx_, r->body),
                               JStr(ctx_, r->headers), JStr(ctx_, r->finalUrl), JStr(ctx_, r->error)};
            JSValue global = JS_GetGlobalObject(ctx_);
            JSValue fn = JS_GetPropertyStr(ctx_, global, "__nxFetchDone");
            Arm(5000);
            JSValue ret = JS_Call(ctx_, fn, JS_UNDEFINED, 6, args);
            if (JS_IsException(ret)) {
                JSValue ex = JS_GetException(ctx_);
                Log("error: " + S(ctx_, ex) + " (fetch callback)");
                JS_FreeValue(ctx_, ex);
            }
            JS_FreeValue(ctx_, ret);
            JS_FreeValue(ctx_, fn);
            JS_FreeValue(ctx_, global);
            for (auto& v : args) JS_FreeValue(ctx_, v);
            PumpJobs();
        });
    });
}

bool JsEngine::Dispatch(const element::ptr& el, const char* type) {
    if (!el) return false;
    JSValue args[2] = {JS_NewInt32(ctx_, Id(el)), JS_NewString(ctx_, type)};
    JSValue global = JS_GetGlobalObject(ctx_);
    JSValue fn = JS_GetPropertyStr(ctx_, global, "__nxDispatch");
    Arm(5000);
    JSValue ret = JS_Call(ctx_, fn, JS_UNDEFINED, 2, args);
    bool prevented = false;
    if (JS_IsException(ret)) {
        JSValue ex = JS_GetException(ctx_);
        Log("error: " + S(ctx_, ex) + " (" + type + " handler)");
        JS_FreeValue(ctx_, ex);
    } else {
        prevented = JS_ToBool(ctx_, ret) > 0;
    }
    JS_FreeValue(ctx_, ret);
    JS_FreeValue(ctx_, fn);
    JS_FreeValue(ctx_, global);
    for (auto& v : args) JS_FreeValue(ctx_, v);
    PumpJobs();
    return prevented;
}

void JsEngine::RunUrl(const std::string& code) {
    Arm(5000);
    Eval(litehtml::url::decode(code), "javascript: URL");
}

bool JsEngine::Tick(double nowMs, bool scrolled) {
    Arm(3000);
    Call("__nxTick", "[" + std::to_string(nowMs) + "," + (scrolled ? "true" : "false") + "]");
    return TakeDirty();
}
