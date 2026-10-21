#include "web/container.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <functional>

#include <litehtml/render_image.h>
#include <litehtml/url.h>

#include "ui/icons.h"
#include "ui/images.h"
#include "util/strings.h"

using namespace litehtml;

const char* const kUserCss = R"css(
input, textarea, select, button { font-family: sans-serif; font-size: 14px; }
input, textarea, select {
    padding: 4px 8px; border: 1px solid #8f8f9d; border-radius: 4px;
    background-color: #ffffff; color: #111111;
}
input[type="submit"], input[type="button"], input[type="reset"], input[type="image"], button {
    padding: 4px 12px; border: 1px solid #8f8f9d; border-radius: 4px;
    background-color: #e9e9ed; color: #111111;
}
input[type="checkbox"], input[type="radio"] { padding: 0; margin: 3px 4px; }
input[type="radio"] { border-radius: 50%; }
main, figure, figcaption, details, summary, dialog, search { display: block; }
)css";

namespace {

Color C(const web_color& c) { return Color(c.red, c.green, c.blue, c.alpha); }
Rect R(const position& p) { return Rect{p.x, p.y, p.width, p.height}; }

// data: URIs never touch the network: decode them straight into the image cache.
void PutDataImage(const std::string& url) {
    static std::set<std::string> tried;
    if (!tried.insert(url).second) return;
    size_t comma = url.find(',');
    if (comma == std::string::npos) return;
    std::string meta = url.substr(5, comma - 5);
    std::string payload = url.substr(comma + 1);
    std::string bytes = payload;
    if (Str::EndsWith(meta, ";base64")) {
        std::vector<uint8_t> raw = Str::Base64Decode(payload);
        bytes.assign(raw.begin(), raw.end());
    }
    DecodedImage img;
    if (!Images::Decode(bytes, 1024, 1, img) || img.frames.empty()) return;
    if (SDL_Texture* t = Images::MakeTexture(img.frames[0], img.w, img.h)) Images::Put(url, t, img.w, img.h);
}

Color GradientAt(const background_layer::gradient_base& g, float t) {
    const auto& pts = g.color_points;
    if (pts.empty()) return Color(0, 0, 0, 0);
    if (t <= pts.front().offset) return C(pts.front().color);
    for (size_t i = 1; i < pts.size(); i++) {
        if (t <= pts[i].offset) {
            float span = pts[i].offset - pts[i - 1].offset;
            float f = span > 0 ? (t - pts[i - 1].offset) / span : 1;
            return Mix(C(pts[i - 1].color), C(pts[i].color), f);
        }
    }
    return C(pts.back().color);
}

}  // namespace

// ---------------------------------------------------------------------------
// form controls
// ---------------------------------------------------------------------------

namespace {

class ElControl : public html_tag {
public:
    explicit ElControl(const std::shared_ptr<document>& doc) : html_tag(doc) {}

    bool is_replaced() const override { return true; }

    void get_content_size(size& sz, pixel_t maxWidth) override {
        auto* box = static_cast<WebContainer*>(get_document()->container());
        const font_metrics& fm = css().get_font_metrics();
        const pixel_t fs = css().get_font_size();
        const pixel_t ch = fm.ch_width > 0 ? fm.ch_width : fs * 0.55f;
        const std::string tag = Forms::Tag(shared_from_this());
        const std::string type = Forms::Type(shared_from_this());
        sz.height = fm.height;
        if (type == "checkbox" || type == "radio") {
            sz.width = sz.height = std::round(fs * 1.1f);
        } else if (tag == "select") {
            pixel_t w = 0;
            for (auto& o : Forms::Options(shared_from_this()))
                w = std::max(w, box->text_width(o.first.c_str(), css().get_font()));
            sz.width = w + fs * 1.4f;
        } else if (tag == "textarea") {
            sz.width = atoi(get_attr("cols", "20")) * ch;
            sz.height = std::max(1, atoi(get_attr("rows", "2"))) * fm.height;
        } else if (Forms::IsSubmit(shared_from_this()) || type == "button" || type == "reset" || type == "file") {
            sz.width = box->text_width(Label().c_str(), css().get_font());
        } else {
            sz.width = std::max(2, atoi(get_attr("size", "20"))) * ch;
        }
        if (maxWidth > 0 && sz.width > maxWidth) sz.width = maxWidth;
    }

    void draw(uint_ptr hdc, pixel_t x, pixel_t y, const position* clip, const std::shared_ptr<render_item>& ri) override {
        html_tag::draw(hdc, x, y, clip, ri);
        auto* box = static_cast<WebContainer*>(get_document()->container());
        position p = ri->pos();
        p.x += x;
        p.y += y;
        if (clip && !p.does_intersect(clip)) return;
        box->FlushText();
        const auto self = shared_from_this();
        const std::string tag = Forms::Tag(self);
        const std::string type = Forms::Type(self);
        const Color fg = C(css().get_color());
        const font_metrics& fm = css().get_font_metrics();
        const float midY = p.y + (p.height - fm.height) / 2;

        if (type == "checkbox" || type == "radio") {
            if (!Checked()) return;
            Rect r = R(p);
            if (type == "radio") {
                Gfx::FillCircle(r.x + r.w / 2, r.y + r.h / 2, r.w * 0.3f, Color::Hex(0x0B57D0));
            } else {
                Gfx::FillRound(r, 3, Color::Hex(0x0B57D0));
                Icon::Check(r.x + r.w / 2, r.y + r.h / 2, r.w * 0.8f, Color(255, 255, 255));
            }
        } else if (tag == "select") {
            auto opts = Forms::Options(self);
            int i = Forms::SelectedIndex(self);
            std::string label = i >= 0 && i < (int)opts.size() ? opts[i].first : "";
            box->DrawControlText(label, css().get_font(), fg, p.x, midY, p.width - fm.height);
            Icon::Chevron(p.right() - fm.height * 0.45f, p.y + p.height / 2, fm.height * 0.6f, fg, 1);
        } else if (Forms::IsSubmit(self) || type == "button" || type == "reset" || type == "file") {
            std::string label = Label();
            float w = box->text_width(label.c_str(), css().get_font());
            box->DrawControlText(label, css().get_font(), fg, p.x + std::max(0.f, (p.width - w) / 2), midY, p.width);
        } else {
            std::string v = Forms::Value(self);
            Color c = fg;
            if (v.empty()) {
                v = get_attr("placeholder", "");
                c = fg.Alpha(0.45f);
            } else if (type == "password") {
                v = std::string(Str::CpCount(v) * 3, '\0');
                for (size_t k = 0; k < v.size(); k += 3) memcpy(&v[k], "\xE2\x80\xA2", 3);  // bullets
            }
            if (tag == "textarea") {
                Gfx::PushClip(R(p));
                float ly = p.y;
                for (auto& line : Str::Split(v, '\n')) {
                    box->DrawControlText(line, css().get_font(), c, p.x, ly, p.width);
                    ly += fm.height;
                    if (ly > p.bottom()) break;
                }
                Gfx::PopClip();
            } else {
                box->DrawControlText(v, css().get_font(), c, p.x, midY, p.width);
            }
        }
    }

    std::shared_ptr<render_item> create_render_item(const std::shared_ptr<render_item>& parent_ri) override {
        auto ret = std::make_shared<render_item_image>(shared_from_this());
        ret->parent(parent_ri);
        return ret;
    }

private:
    bool Checked() const {
        if (const char* v = get_attr("nx-checked")) return strcmp(v, "1") == 0;
        return get_attr("checked") != nullptr;
    }

    std::string Label() const {
        std::string type = Forms::Type(std::const_pointer_cast<element>(shared_from_this()));
        if (type == "file") return "File uploads aren't supported";
        if (const char* v = get_attr("value")) return v;
        if (type == "reset") return "Reset";
        if (type == "button") return "";
        return "Submit";
    }
};

std::string Lower(const char* s) { return Str::Lower(s ? s : ""); }

void Walk(const element::ptr& el, const std::function<void(const element::ptr&)>& fn) {
    for (auto& c : el->children()) {
        fn(c);
        Walk(c, fn);
    }
}

std::string FormEncode(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        if (isalnum(c) || c == '*' || c == '-' || c == '.' || c == '_') out += (char)c;
        else if (c == ' ') out += '+';
        else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}

}  // namespace

namespace Forms {

std::string Tag(const element::ptr& el) { return el ? Lower(el->get_tagName()) : ""; }

std::string Type(const element::ptr& el) {
    std::string tag = Tag(el);
    if (tag == "input") return Lower(el->get_attr("type", "text"));
    if (tag == "button") return Lower(el->get_attr("type", "submit"));
    return tag;
}

bool IsControl(const element::ptr& el) {
    std::string t = Tag(el);
    return t == "input" || t == "textarea" || t == "select" || t == "button";
}

bool IsTextField(const element::ptr& el) {
    std::string tag = Tag(el);
    if (tag == "textarea") return true;
    if (tag != "input") return false;
    static const char* kNot[] = {"hidden", "checkbox", "radio", "submit", "image", "button", "reset", "file"};
    std::string type = Type(el);
    for (const char* n : kNot)
        if (type == n) return false;
    return true;
}

bool IsSubmit(const element::ptr& el) {
    std::string tag = Tag(el), type = Type(el);
    return (tag == "input" && (type == "submit" || type == "image")) || (tag == "button" && type == "submit");
}

std::vector<std::pair<std::string, std::string>> Options(const element::ptr& select) {
    std::vector<std::pair<std::string, std::string>> out;
    Walk(select, [&](const element::ptr& c) {
        if (Tag(c) != "option") return;
        std::string label;
        if (const char* l = c->get_attr("label")) label = l;
        else c->get_text(label);
        label = Str::Trim(label);
        const char* v = c->get_attr("value");
        out.push_back({label, v ? v : label});
    });
    return out;
}

int SelectedIndex(const element::ptr& select) {
    if (const char* v = select->get_attr("nx-selected")) return atoi(v);
    int i = 0, found = 0;
    Walk(select, [&](const element::ptr& c) {
        if (Tag(c) != "option") return;
        if (c->get_attr("selected")) found = i;
        i++;
    });
    return found;
}

void Select(const element::ptr& select, int index) { select->set_attr("nx-selected", std::to_string(index).c_str()); }

std::string Value(const element::ptr& el) {
    std::string tag = Tag(el);
    if (tag == "select") {
        auto opts = Options(el);
        int i = SelectedIndex(el);
        return i >= 0 && i < (int)opts.size() ? opts[i].second : "";
    }
    if (const char* v = el->get_attr("value")) return v;
    if (tag == "textarea") {
        std::string t;
        el->get_text(t);
        return t;
    }
    return "";
}

void SetValue(const element::ptr& el, const std::string& v) { el->set_attr("value", v.c_str()); }

static bool IsChecked(const element::ptr& el) {
    if (const char* v = el->get_attr("nx-checked")) return strcmp(v, "1") == 0;
    return el->get_attr("checked") != nullptr;
}

element::ptr FormOf(const element::ptr& el) {
    for (auto p = el ? el->parent() : nullptr; p; p = p->parent())
        if (Tag(p) == "form") return p;
    return nullptr;
}

void Toggle(const element::ptr& el) {
    if (Type(el) == "radio") {
        std::string name = el->get_attr("name", "");
        auto scope = FormOf(el);
        if (!scope) scope = el->parent();
        if (scope && !name.empty())
            Walk(scope, [&](const element::ptr& c) {
                if (Type(c) == "radio" && name == c->get_attr("name", "")) c->set_attr("nx-checked", "0");
            });
        el->set_attr("nx-checked", "1");
    } else {
        el->set_attr("nx-checked", IsChecked(el) ? "0" : "1");
    }
}

int TextFieldCount(const element::ptr& form) {
    int n = 0;
    if (form) Walk(form, [&](const element::ptr& c) { n += IsTextField(c) ? 1 : 0; });
    return n;
}

Submission Build(const element::ptr& form, const element::ptr& submitter, const std::string& pageUrl) {
    std::string data;
    auto add = [&](const std::string& k, const std::string& v) {
        if (!data.empty()) data += '&';
        data += FormEncode(k) + "=" + FormEncode(v);
    };
    if (form) {
        Walk(form, [&](const element::ptr& c) {
            if (!IsControl(c) || c->get_attr("disabled")) return;
            std::string name = c->get_attr("name", "");
            if (name.empty()) return;
            std::string tag = Tag(c), type = Type(c);
            if (tag == "button" || type == "submit" || type == "image" || type == "reset" || type == "button") {
                if (c == submitter && type != "reset" && type != "button") add(name, Value(c));
            } else if (type == "checkbox" || type == "radio") {
                if (IsChecked(c)) add(name, c->get_attr("value", "on"));
            } else if (type != "file") {
                add(name, Value(c));
            }
        });
    }
    Submission s;
    std::string action = form ? form->get_attr("action", "") : "";
    if (submitter && submitter->get_attr("formaction")) action = submitter->get_attr("formaction");
    s.url = action.empty() ? pageUrl : resolve(url(pageUrl), url(action)).str();
    std::string method = Lower(form ? form->get_attr("method", "get") : "get");
    if (submitter && submitter->get_attr("formmethod")) method = Lower(submitter->get_attr("formmethod"));
    if (method == "post") {
        s.method = "POST";
        s.body = data;
    } else {
        s.url = s.url.substr(0, s.url.find('#'));
        s.url = s.url.substr(0, s.url.find('?')) + "?" + data;
    }
    return s;
}

}  // namespace Forms

// ---------------------------------------------------------------------------
// WebContainer
// ---------------------------------------------------------------------------

std::string WebContainer::Resolve(const std::string& href) const {
    if (href.empty()) return baseUrl;
    if (Str::StartsWith(href, "data:") || Str::StartsWith(href, "javascript:") || Str::StartsWith(href, "mailto:"))
        return href;
    return resolve(url(baseUrl), url(href)).str();
}

std::string WebContainer::ImageUrl(const char* src, const char* baseurl) const {
    std::string s = src ? Str::Trim(src) : "";
    if (s.empty()) return "";
    if (Str::StartsWith(s, "data:")) return s;
    std::string full = baseurl && *baseurl ? resolve(url(Resolve(baseurl)), url(s)).str() : Resolve(s);
    return full.substr(0, full.find('#'));  // '#' means "first frame only" to Images
}

void WebContainer::FlushText() {
    if (!textPending_) return;
    Text::Flush();
    textPending_ = false;
}

uint_ptr WebContainer::create_font(const font_description& d, const document*, font_metrics* fm) {
    const std::string fam = Str::Lower(d.family);
    Face face = Face::Regular;
    if (fam.find("mono") != std::string::npos || fam.find("courier") != std::string::npos ||
        fam.find("consolas") != std::string::npos || fam.find("menlo") != std::string::npos)
        face = Face::Mono;
    else if (d.style == font_style_italic) face = Face::Italic;
    else if (d.weight >= 700) face = Face::Bold;
    else if (d.weight >= 600) face = Face::SemiBold;
    else if (d.weight >= 500) face = Face::Medium;
    const int size = std::clamp((int)std::lround(d.size), 6, 96);

    auto f = std::make_unique<Font>();
    f->face = face;
    f->size = size;
    f->decoration = d.decoration_line;
    f->m = Text::GetMetrics(face, size);
    if (fm) {
        fm->font_size = d.size;
        fm->ascent = std::round(f->m.ascent);
        fm->descent = std::round(f->m.descent);
        fm->height = fm->ascent + fm->descent;
        fm->x_height = std::round(f->m.ascent * 0.55f);
        fm->ch_width = Text::Advance('0', face, size);
        fm->draw_spaces = d.decoration_line != text_decoration_line_none;
        fm->sub_shift = std::round(size * 0.2f);
        fm->super_shift = std::round(size * 0.35f);
    }
    fonts_.push_back(std::move(f));
    return (uint_ptr)fonts_.back().get();
}

void WebContainer::delete_font(uint_ptr hFont) {
    fonts_.erase(std::remove_if(fonts_.begin(), fonts_.end(),
                                [&](const std::unique_ptr<Font>& f) { return (uint_ptr)f.get() == hFont; }),
                 fonts_.end());
}

pixel_t WebContainer::text_width(const char* text, uint_ptr hFont) {
    const Font* f = (const Font*)hFont;
    if (!f || !text) return 0;
    float w = 0;
    const char* p = text;
    const char* end = text + strlen(text);
    while (p < end) w += Text::Advance(Str::NextCp(p, end), f->face, f->size);
    return w;
}

void WebContainer::draw_text(uint_ptr, const char* text, uint_ptr hFont, web_color color, const position& pos) {
    const Font* f = (const Font*)hFont;
    if (!f || !text) return;
    const Rect clip = Gfx::CurrentClip();
    if (pos.bottom() < clip.y || pos.y > clip.Bottom()) return;  // off screen: skip the glyph work
    const Color c = C(color);
    const float ascent = std::round(f->m.ascent);
    const float baseline = std::round(pos.y + ascent);
    float pen = pos.x;
    const char* p = text;
    const char* end = text + strlen(text);
    while (p < end) {
        uint32_t cp = Str::NextCp(p, end);
        Text::Glyph(cp, pen, baseline, f->face, f->size, c);
        pen += Text::Advance(cp, f->face, f->size);
    }
    textPending_ = true;
    if (f->decoration) {
        const float t = std::max(1.0f, std::round(f->size / 15.0f));
        if (f->decoration & text_decoration_line_underline)
            Gfx::FillRect(Rect{pos.x, baseline + t + 1, pen - pos.x, t}, c);
        if (f->decoration & text_decoration_line_line_through)
            Gfx::FillRect(Rect{pos.x, std::round(baseline - ascent * 0.3f), pen - pos.x, t}, c);
        if (f->decoration & text_decoration_line_overline) Gfx::FillRect(Rect{pos.x, pos.y, pen - pos.x, t}, c);
    }
}

void WebContainer::DrawControlText(const std::string& s, uint_ptr hFont, Color c, float x, float y, float maxW) {
    if (s.empty() || maxW <= 0) return;
    const Font* f = (const Font*)hFont;
    if (!f) return;
    Gfx::PushClip(Rect{x, y - 2, maxW, f->m.ascent + f->m.descent + 4});
    draw_text(0, s.c_str(), hFont, web_color(c.r, c.g, c.b, c.a), position(x, y, maxW, 0));
    FlushText();
    Gfx::PopClip();
}

void WebContainer::draw_list_marker(uint_ptr, const list_marker& m) {
    FlushText();
    if (!m.image.empty()) {
        if (const Images::Img* img = Images::Get(ImageUrl(m.image.c_str(), m.baseurl)); img && img->tex) {
            Gfx::Image(img->tex, R(m.pos));
            return;
        }
    }
    const Color c = C(m.color);
    const float r = std::min(m.pos.width, m.pos.height) / 2;
    const float cx = m.pos.x + m.pos.width / 2, cy = m.pos.y + m.pos.height / 2;
    switch (m.marker_type) {
        case list_style_type_circle: Gfx::StrokeCircle(cx, cy, r, 1, c); break;
        case list_style_type_disc: Gfx::FillCircle(cx, cy, r, c); break;
        case list_style_type_square: Gfx::FillRect(R(m.pos), c); break;
        default: break;
    }
}

void WebContainer::load_image(const char* src, const char* baseurl, bool) {
    std::string u = ImageUrl(src, baseurl);
    if (u.empty()) return;
    if (Str::StartsWith(u, "data:")) {
        PutDataImage(u);
        return;
    }
    const Images::Img* img = Images::Get(u);
    if (!img) waitingImages.insert(u);
}

void WebContainer::get_image_size(const char* src, const char* baseurl, size& sz) {
    sz = size(0, 0);
    std::string u = ImageUrl(src, baseurl);
    if (u.empty()) return;
    if (Str::StartsWith(u, "data:")) PutDataImage(u);
    const Images::Img* img = Images::Get(u);
    if (img && img->tex) {
        sz = size((pixel_t)img->w, (pixel_t)img->h);
    } else if (!img && !Str::StartsWith(u, "data:")) {
        waitingImages.insert(u);
    }
}

void WebContainer::draw_image(uint_ptr, const background_layer& layer, const std::string& src,
                              const std::string& base) {
    FlushText();
    const Images::Img* img = Images::Get(ImageUrl(src.c_str(), base.c_str()));
    if (!img || !img->tex) return;
    const position& o = layer.origin_box;
    if (o.width < 1 || o.height < 1) return;
    const Rect clip = R(layer.clip_box);
    if (!Gfx::Visible(clip)) return;
    const float radius = layer.border_radius.top_left_x;
    if (layer.repeat == background_repeat_no_repeat) {
        Gfx::PushClip(clip);
        Gfx::Image(img->tex, R(o), radius);
        Gfx::PopClip();
        return;
    }
    float x = o.x, y = o.y;
    const bool rx = layer.repeat == background_repeat_repeat || layer.repeat == background_repeat_repeat_x;
    const bool ry = layer.repeat == background_repeat_repeat || layer.repeat == background_repeat_repeat_y;
    if (rx) while (x > clip.x) x -= o.width;
    if (ry) while (y > clip.y) y -= o.height;
    if ((rx ? clip.w / o.width : 1) * (ry ? clip.h / o.height : 1) > 2000) return;  // tiny tiles: not worth it
    Gfx::PushClip(clip);
    for (float tx = x; tx < (rx ? clip.Right() : x + 1); tx += o.width)
        for (float ty = y; ty < (ry ? clip.Bottom() : y + 1); ty += o.height)
            Gfx::Image(img->tex, Rect{tx, ty, o.width, o.height});
    Gfx::PopClip();
}

void WebContainer::draw_solid_fill(uint_ptr, const background_layer& layer, const web_color& color) {
    FlushText();
    if (color.alpha == 0) return;
    if (layer.is_root) {
        Gfx::FillRect(Gfx::CurrentClip(), C(color));
        return;
    }
    const border_radiuses& br = layer.border_radius;
    const Rect r = R(layer.clip_box);
    if (br.top_left_x > 0 || br.top_right_x > 0 || br.bottom_right_x > 0 || br.bottom_left_x > 0)
        Gfx::FillRoundCorners(r, br.top_left_x, br.top_right_x, br.bottom_right_x, br.bottom_left_x, C(color));
    else
        Gfx::FillRect(r, C(color));
}

void WebContainer::draw_linear_gradient(uint_ptr, const background_layer& layer,
                                        const background_layer::linear_gradient& g) {
    FlushText();
    // Strips across the box, each coloured by its centre projected onto the gradient line.
    const Rect r = R(layer.clip_box);
    if (!Gfx::Visible(r)) return;
    const float dx = g.end.x - g.start.x, dy = g.end.y - g.start.y;
    const float len2 = dx * dx + dy * dy;
    if (len2 <= 0) return;
    const bool vertical = std::fabs(dy) >= std::fabs(dx);
    const int n = std::clamp((int)((vertical ? r.h : r.w) / 4), 1, 64);
    for (int i = 0; i < n; i++) {
        Rect s = vertical ? Rect{r.x, r.y + r.h * i / n, r.w, r.h / n + 1} : Rect{r.x + r.w * i / n, r.y, r.w / n + 1, r.h};
        float cx = s.x + s.w / 2, cy = s.y + s.h / 2;
        float t = ((cx - g.start.x) * dx + (cy - g.start.y) * dy) / len2;
        Gfx::FillRect(s, GradientAt(g, t));
    }
}

// ponytail: radial and conic gradients are drawn as their middle colour; real shading if a site needs it.
void WebContainer::draw_radial_gradient(uint_ptr, const background_layer& layer,
                                        const background_layer::radial_gradient& g) {
    FlushText();
    Gfx::FillRect(R(layer.clip_box), GradientAt(g, 0.5f));
}

void WebContainer::draw_conic_gradient(uint_ptr, const background_layer& layer,
                                       const background_layer::conic_gradient& g) {
    FlushText();
    Gfx::FillRect(R(layer.clip_box), GradientAt(g, 0.5f));
}

void WebContainer::draw_borders(uint_ptr, const borders& b, const position& pos, bool) {
    FlushText();
    auto shown = [](const border& e) {
        return e.width > 0 && e.color.alpha > 0 && e.style != border_style_none && e.style != border_style_hidden;
    };
    const Rect r = R(pos);
    if (!Gfx::Visible(Rect{r.x - 2, r.y - 2, r.w + 4, r.h + 4})) return;
    const float radius = b.radius.top_left_x;
    const bool same = b.top.width == b.left.width && b.top.width == b.right.width && b.top.width == b.bottom.width &&
                      b.top.color == b.left.color && b.top.color == b.right.color && b.top.color == b.bottom.color &&
                      shown(b.top) && shown(b.left) && shown(b.right) && shown(b.bottom);
    if (same && radius > 0) {
        const float w = b.top.width;
        Gfx::StrokeRound(Rect{r.x + w / 2, r.y + w / 2, r.w - w, r.h - w}, std::max(0.f, radius - w / 2), w, C(b.top.color));
        return;
    }
    if (shown(b.top)) Gfx::FillRect(Rect{r.x, r.y, r.w, b.top.width}, C(b.top.color));
    if (shown(b.bottom)) Gfx::FillRect(Rect{r.x, r.Bottom() - b.bottom.width, r.w, b.bottom.width}, C(b.bottom.color));
    if (shown(b.left)) Gfx::FillRect(Rect{r.x, r.y, b.left.width, r.h}, C(b.left.color));
    if (shown(b.right)) Gfx::FillRect(Rect{r.Right() - b.right.width, r.y, b.right.width, r.h}, C(b.right.color));
}

void WebContainer::set_base_url(const char* base) {
    if (base && *base) baseUrl = Resolve(base);
}

void WebContainer::on_anchor_click(const char* u, const element::ptr&) {
    if (u) clickedLink = Resolve(u);
}

bool WebContainer::on_element_click(const element::ptr& el) {
    if (!Forms::IsControl(el)) return false;
    clickedControl = el;
    return true;
}

void WebContainer::transform_text(string& text, text_transform tt) {
    if (tt == text_transform_uppercase) {
        for (char& c : text) c = (char)toupper((unsigned char)c);
    } else if (tt == text_transform_lowercase) {
        for (char& c : text) c = (char)tolower((unsigned char)c);
    } else if (tt == text_transform_capitalize && !text.empty()) {
        text[0] = (char)toupper((unsigned char)text[0]);
    }
}

void WebContainer::import_css(string& text, const string& href, string& baseurl) {
    const std::string full = baseurl.empty() ? Resolve(href) : resolve(url(Resolve(baseurl)), url(href)).str();
    auto it = cssCache.find(full);
    if (it != cssCache.end()) {
        text = it->second;
        baseurl = full;
        return;
    }
    if (std::find(missingCss.begin(), missingCss.end(), full) == missingCss.end()) missingCss.push_back(full);
}

void WebContainer::set_clip(const position& pos, const border_radiuses&) {
    FlushText();
    Gfx::PushClip(R(pos));
    clipDepth_++;
}

void WebContainer::del_clip() {
    FlushText();
    if (clipDepth_ > 0) {
        Gfx::PopClip();
        clipDepth_--;
    }
}

void WebContainer::get_viewport(position& v) const { v = position(0, 0, viewW, viewH); }

element::ptr WebContainer::create_element(const char* tag, const string_map&, const std::shared_ptr<document>& doc) {
    if (!strcmp(tag, "input") || !strcmp(tag, "textarea") || !strcmp(tag, "select"))
        return std::make_shared<ElControl>(doc);
    return nullptr;
}

void WebContainer::get_media_features(media_features& m) const {
    m.type = media_type_screen;
    m.width = viewW;
    m.height = viewH;
    m.device_width = 1280;
    m.device_height = 720;
    m.color = 8;
    m.monochrome = 0;
    m.color_index = 0;
    m.resolution = 96;
}
