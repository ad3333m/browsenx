#pragma once

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <litehtml.h>

#include "ui/gfx.h"
#include "ui/text.h"

// litehtml's window onto the console: fonts come from Text, shapes from Gfx,
// pictures from Images. Everything runs on the main thread (glyphs live in GPU atlases).
class WebContainer : public litehtml::document_container {
public:
    struct Font {
        Face face;
        int size;
        int decoration;  // litehtml::text_decoration_line bits
        Text::Metrics m;
    };

    std::string baseUrl;
    float viewW = 1280, viewH = 632;
    float originX = 0, originY = 0;  // where the page's (0, 0) lands on screen, scroll included
    std::string title;

    // Stylesheets: import_css only reads this cache and lists what's missing; the page
    // fetches those on a worker and parses the document again.
    std::map<std::string, std::string> cssCache;
    std::vector<std::string> missingCss;
    // Images laid out before they arrived (0x0); the page re-renders once they land.
    std::set<std::string> waitingImages;

    // What the last click asked for. Read after document::on_lbutton_up returns,
    // because acting inside litehtml's callback would free the document under it.
    std::string clickedLink;
    litehtml::element::ptr clickedControl;
    std::string cursor;

    std::string Resolve(const std::string& href) const;
    void FlushText();

    // document_container
    litehtml::uint_ptr create_font(const litehtml::font_description& descr, const litehtml::document* doc,
                                   litehtml::font_metrics* fm) override;
    void delete_font(litehtml::uint_ptr hFont) override;
    litehtml::pixel_t text_width(const char* text, litehtml::uint_ptr hFont) override;
    void draw_text(litehtml::uint_ptr hdc, const char* text, litehtml::uint_ptr hFont, litehtml::web_color color,
                   const litehtml::position& pos) override;
    litehtml::pixel_t pt_to_px(float pt) const override { return pt * 96.0f / 72.0f; }
    litehtml::pixel_t get_default_font_size() const override { return 16; }
    const char* get_default_font_name() const override { return "sans-serif"; }
    void draw_list_marker(litehtml::uint_ptr hdc, const litehtml::list_marker& marker) override;
    void load_image(const char* src, const char* baseurl, bool redraw_on_ready) override;
    void get_image_size(const char* src, const char* baseurl, litehtml::size& sz) override;
    void draw_image(litehtml::uint_ptr hdc, const litehtml::background_layer& layer, const std::string& url,
                    const std::string& base_url) override;
    void draw_solid_fill(litehtml::uint_ptr hdc, const litehtml::background_layer& layer,
                         const litehtml::web_color& color) override;
    void draw_linear_gradient(litehtml::uint_ptr hdc, const litehtml::background_layer& layer,
                              const litehtml::background_layer::linear_gradient& gradient) override;
    void draw_radial_gradient(litehtml::uint_ptr hdc, const litehtml::background_layer& layer,
                              const litehtml::background_layer::radial_gradient& gradient) override;
    void draw_conic_gradient(litehtml::uint_ptr hdc, const litehtml::background_layer& layer,
                             const litehtml::background_layer::conic_gradient& gradient) override;
    void draw_borders(litehtml::uint_ptr hdc, const litehtml::borders& borders, const litehtml::position& draw_pos,
                      bool root) override;
    void set_caption(const char* caption) override { title = caption ? caption : ""; }
    void set_base_url(const char* base_url) override;
    void link(const std::shared_ptr<litehtml::document>&, const litehtml::element::ptr&) override {}
    void on_anchor_click(const char* url, const litehtml::element::ptr& el) override;
    bool on_element_click(const litehtml::element::ptr& el) override;
    void on_mouse_event(const litehtml::element::ptr&, litehtml::mouse_event) override {}
    void set_cursor(const char* c) override { cursor = c ? c : ""; }
    void transform_text(litehtml::string& text, litehtml::text_transform tt) override;
    void import_css(litehtml::string& text, const litehtml::string& url, litehtml::string& baseurl) override;
    void set_clip(const litehtml::position& pos, const litehtml::border_radiuses& bdr_radius) override;
    void del_clip() override;
    void get_viewport(litehtml::position& viewport) const override;
    litehtml::element::ptr create_element(const char* tag_name, const litehtml::string_map& attributes,
                                          const std::shared_ptr<litehtml::document>& doc) override;
    void get_media_features(litehtml::media_features& media) const override;
    void get_language(litehtml::string& language, litehtml::string& culture) const override {
        language = "en";
        culture = "";
    }

    // Used by form controls to draw their value.
    void DrawControlText(const std::string& s, litehtml::uint_ptr hFont, Color c, float x, float y, float maxW);

private:
    std::vector<std::unique_ptr<Font>> fonts_;
    int clipDepth_ = 0;
    bool textPending_ = false;
    std::string ImageUrl(const char* src, const char* baseurl) const;
};

// Form controls are drawn by us; litehtml only lays out a box of the right size.
namespace Forms {
bool IsControl(const litehtml::element::ptr& el);
std::string Tag(const litehtml::element::ptr& el);
std::string Type(const litehtml::element::ptr& el);  // lower-case input type ("text" by default)
bool IsTextField(const litehtml::element::ptr& el);
bool IsSubmit(const litehtml::element::ptr& el);
std::string Value(const litehtml::element::ptr& el);
void SetValue(const litehtml::element::ptr& el, const std::string& v);
// Options of a <select>: {label, value}; selected index via SelectedIndex.
std::vector<std::pair<std::string, std::string>> Options(const litehtml::element::ptr& select);
int SelectedIndex(const litehtml::element::ptr& select);
void Select(const litehtml::element::ptr& select, int index);
void Toggle(const litehtml::element::ptr& el);  // checkbox / radio
litehtml::element::ptr FormOf(const litehtml::element::ptr& el);
// GET: url with ?query. POST: body set, url is the action.
struct Submission {
    std::string url, method = "GET", body;
};
Submission Build(const litehtml::element::ptr& form, const litehtml::element::ptr& submitter, const std::string& pageUrl);
// Fields the user types into, for the "press Enter submits" rule.
int TextFieldCount(const litehtml::element::ptr& form);
}  // namespace Forms

// Extra CSS on top of litehtml's master stylesheet: control looks and a few
// things the master sheet leaves to the browser.
extern const char* const kUserCss;
