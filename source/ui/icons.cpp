#include "ui/icons.h"

#include <algorithm>
#include <cctype>
#include <cmath>

#include <cstdio>

#include "platform/platform.h"
#include "ui/images.h"
#include "ui/text.h"

namespace Icon {
namespace {

constexpr float kPi = 3.14159265f;

struct Pen {
    float cx, cy, s;
    SDL_FPoint P(float x, float y) const { return SDL_FPoint{cx + x * s, cy + y * s}; }
    void L(float x0, float y0, float x1, float y1, float w, Color c) const {
        Gfx::Line(cx + x0 * s, cy + y0 * s, cx + x1 * s, cy + y1 * s, std::max(1.2f, w * s), c);
    }
    void Poly(std::initializer_list<std::pair<float, float>> pts, Color c) const {
        std::vector<SDL_FPoint> v;
        for (auto& p : pts) v.push_back(P(p.first, p.second));
        Gfx::FillPolygon(v, c);
    }
    void Stroke(std::initializer_list<std::pair<float, float>> pts, float w, Color c, bool closed = false) const {
        std::vector<SDL_FPoint> v;
        for (auto& p : pts) v.push_back(P(p.first, p.second));
        Gfx::Polyline(v, std::max(1.2f, w * s), c, closed);
    }
};

}  // namespace

void Hash(float cx, float cy, float s, Color c) {
    Pen p{cx, cy, s};
    const float w = 0.095f;
    p.L(-0.08f, -0.38f, -0.2f, 0.38f, w, c);
    p.L(0.2f, -0.38f, 0.08f, 0.38f, w, c);
    p.L(-0.34f, -0.13f, 0.38f, -0.13f, w, c);
    p.L(-0.38f, 0.14f, 0.34f, 0.14f, w, c);
}

void Speaker(float cx, float cy, float s, Color c) {
    Pen p{cx, cy, s};
    p.Poly({{-0.38f, -0.13f}, {-0.2f, -0.13f}, {0.02f, -0.34f}, {0.02f, 0.34f}, {-0.2f, 0.13f}, {-0.38f, 0.13f}}, c);
    Gfx::Arc(p.P(0.04f, 0).x, cy, s * 0.2f, -0.8f, 0.8f, std::max(1.4f, s * 0.08f), c);
    Gfx::Arc(p.P(0.04f, 0).x, cy, s * 0.36f, -0.8f, 0.8f, std::max(1.4f, s * 0.08f), c);
}

void Megaphone(float cx, float cy, float s, Color c) {
    Pen p{cx, cy, s};
    p.Poly({{-0.36f, -0.1f}, {0.28f, -0.36f}, {0.28f, 0.28f}, {-0.36f, 0.12f}}, c);
    p.L(-0.18f, 0.12f, -0.1f, 0.36f, 0.1f, c);
    p.L(0.36f, -0.14f, 0.36f, 0.08f, 0.08f, c);
}

void Forum(float cx, float cy, float s, Color c) {
    Pen p{cx, cy, s};
    Gfx::StrokeRound(Rect{cx - s * 0.4f, cy - s * 0.34f, s * 0.56f, s * 0.42f}, s * 0.12f, std::max(1.4f, s * 0.08f), c);
    Gfx::FillRound(Rect{cx - s * 0.1f, cy - s * 0.04f, s * 0.5f, s * 0.38f}, s * 0.12f, c);
    p.Poly({{0.2f, 0.3f}, {0.34f, 0.3f}, {0.34f, 0.42f}}, c);
}

void Thread(float cx, float cy, float s, Color c) {
    Hash(cx - s * 0.08f, cy - s * 0.05f, s * 0.8f, c);
    Pen p{cx, cy, s};
    p.Stroke({{0.1f, 0.42f}, {0.42f, 0.42f}, {0.42f, 0.18f}}, 0.08f, c);
}

void Gear(float cx, float cy, float s, Color c, Color hole) {
    const float w = s * 0.14f;
    for (int i = 0; i < 8; i++) {
        float a = i * kPi / 4;
        Gfx::Line(cx + std::cos(a) * s * 0.26f, cy + std::sin(a) * s * 0.26f, cx + std::cos(a) * s * 0.42f,
                  cy + std::sin(a) * s * 0.42f, w, c);
    }
    Gfx::FillCircle(cx, cy, s * 0.31f, c);
    Gfx::FillCircle(cx, cy, s * 0.13f, hole);
}

void People(float cx, float cy, float s, Color c) {
    Gfx::FillCircle(cx - s * 0.12f, cy - s * 0.16f, s * 0.14f, c);
    Gfx::FillRoundCorners(Rect{cx - s * 0.38f, cy + s * 0.04f, s * 0.52f, s * 0.32f}, s * 0.22f, s * 0.22f, s * 0.05f,
                          s * 0.05f, c);
    Gfx::FillCircle(cx + s * 0.2f, cy - s * 0.2f, s * 0.11f, c.Alpha(0.7f));
    Gfx::FillRoundCorners(Rect{cx + s * 0.02f, cy - s * 0.03f, s * 0.38f, s * 0.28f}, s * 0.18f, s * 0.18f, s * 0.05f,
                          s * 0.05f, c.Alpha(0.7f));
}

void Plus(float cx, float cy, float s, Color c) {
    Pen p{cx, cy, s};
    p.L(0, -0.34f, 0, 0.34f, 0.1f, c);
    p.L(-0.34f, 0, 0.34f, 0, 0.1f, c);
}

void Close(float cx, float cy, float s, Color c) {
    Pen p{cx, cy, s};
    p.L(-0.28f, -0.28f, 0.28f, 0.28f, 0.1f, c);
    p.L(0.28f, -0.28f, -0.28f, 0.28f, 0.1f, c);
}

void Check(float cx, float cy, float s, Color c) {
    Pen p{cx, cy, s};
    p.Stroke({{-0.32f, 0.02f}, {-0.1f, 0.26f}, {0.34f, -0.24f}}, 0.11f, c);
}

void Chevron(float cx, float cy, float s, Color c, int dir) {
    Pen p{cx, cy, s};
    switch (dir) {
        case 0: p.Stroke({{-0.12f, -0.28f}, {0.16f, 0}, {-0.12f, 0.28f}}, 0.1f, c); break;
        case 1: p.Stroke({{-0.28f, -0.12f}, {0, 0.16f}, {0.28f, -0.12f}}, 0.1f, c); break;
        case 2: p.Stroke({{0.12f, -0.28f}, {-0.16f, 0}, {0.12f, 0.28f}}, 0.1f, c); break;
        default: p.Stroke({{-0.28f, 0.12f}, {0, -0.16f}, {0.28f, 0.12f}}, 0.1f, c); break;
    }
}

void Reply(float cx, float cy, float s, Color c) {
    Pen p{cx, cy, s};
    p.Stroke({{-0.08f, -0.34f}, {-0.36f, -0.08f}, {-0.08f, 0.18f}}, 0.1f, c);
    p.Stroke({{-0.34f, -0.08f}, {0.1f, -0.08f}, {0.26f, 0.0f}, {0.34f, 0.14f}, {0.34f, 0.34f}}, 0.1f, c);
}

void Smile(float cx, float cy, float s, Color c) {
    const float w = std::max(1.4f, s * 0.08f);
    Gfx::StrokeCircle(cx, cy, s * 0.42f, w, c);
    Gfx::FillCircle(cx - s * 0.14f, cy - s * 0.1f, s * 0.06f, c);
    Gfx::FillCircle(cx + s * 0.14f, cy - s * 0.1f, s * 0.06f, c);
    Gfx::Arc(cx, cy + s * 0.02f, s * 0.2f, 0.35f, kPi - 0.35f, w, c);
}

void Pencil(float cx, float cy, float s, Color c) {
    Pen p{cx, cy, s};
    p.Poly({{0.18f, -0.38f}, {0.36f, -0.2f}, {-0.2f, 0.36f}, {-0.38f, 0.18f}}, c);
    p.Poly({{-0.38f, 0.22f}, {-0.24f, 0.36f}, {-0.42f, 0.42f}}, c);
}

void Trash(float cx, float cy, float s, Color c) {
    Pen p{cx, cy, s};
    p.L(-0.36f, -0.26f, 0.36f, -0.26f, 0.09f, c);
    p.L(-0.1f, -0.36f, 0.1f, -0.36f, 0.09f, c);
    Gfx::FillRoundCorners(Rect{cx - s * 0.27f, cy - s * 0.18f, s * 0.54f, s * 0.58f}, 0, 0, s * 0.1f, s * 0.1f, c);
}

void Image(float cx, float cy, float s, Color c) {
    const float w = std::max(1.4f, s * 0.08f);
    Gfx::StrokeRound(Rect{cx - s * 0.42f, cy - s * 0.34f, s * 0.84f, s * 0.68f}, s * 0.1f, w, c);
    Pen p{cx, cy, s};
    p.Poly({{-0.3f, 0.24f}, {-0.08f, -0.04f}, {0.06f, 0.1f}, {0.14f, 0.02f}, {0.3f, 0.24f}}, c);
    Gfx::FillCircle(cx + s * 0.16f, cy - s * 0.14f, s * 0.07f, c);
}

void File(float cx, float cy, float s, Color c) {
    Pen p{cx, cy, s};
    p.Stroke({{0.08f, -0.42f}, {-0.3f, -0.42f}, {-0.3f, 0.42f}, {0.3f, 0.42f}, {0.3f, -0.2f}, {0.08f, -0.42f}}, 0.08f, c, true);
    p.Stroke({{0.08f, -0.42f}, {0.08f, -0.2f}, {0.3f, -0.2f}}, 0.08f, c);
}

void Send(float cx, float cy, float s, Color c) {
    Pen p{cx, cy, s};
    p.Poly({{-0.4f, -0.36f}, {0.42f, 0.0f}, {-0.4f, 0.36f}, {-0.26f, 0.0f}}, c);
}

void Lock(float cx, float cy, float s, Color c) {
    Gfx::FillRound(Rect{cx - s * 0.3f, cy - s * 0.05f, s * 0.6f, s * 0.45f}, s * 0.08f, c);
    Gfx::Arc(cx, cy - s * 0.08f, s * 0.2f, kPi, 2 * kPi, std::max(1.4f, s * 0.1f), c);
    Pen p{cx, cy, s};
    p.L(-0.2f, -0.08f, -0.2f, 0.0f, 0.1f, c);
    p.L(0.2f, -0.08f, 0.2f, 0.0f, 0.1f, c);
}

void Palette(float cx, float cy, float s, Color c, Color bg) {
    Gfx::FillCircle(cx, cy, s * 0.42f, c);
    Gfx::FillCircle(cx + s * 0.14f, cy + s * 0.18f, s * 0.1f, bg);
    Gfx::FillCircle(cx - s * 0.18f, cy - s * 0.12f, s * 0.07f, bg);
    Gfx::FillCircle(cx + s * 0.02f, cy - s * 0.22f, s * 0.07f, bg);
    Gfx::FillCircle(cx + s * 0.2f, cy - s * 0.06f, s * 0.07f, bg);
}

void Info(float cx, float cy, float s, Color c) {
    Gfx::StrokeCircle(cx, cy, s * 0.42f, std::max(1.4f, s * 0.08f), c);
    Gfx::FillCircle(cx, cy - s * 0.18f, s * 0.06f, c);
    Pen p{cx, cy, s};
    p.L(0, -0.02f, 0, 0.22f, 0.09f, c);
}

void Power(float cx, float cy, float s, Color c) {
    Gfx::Arc(cx, cy + s * 0.04f, s * 0.34f, -kPi / 2 + 0.7f, 1.5f * kPi - 0.7f, std::max(1.4f, s * 0.09f), c);
    Pen p{cx, cy, s};
    p.L(0, -0.42f, 0, -0.04f, 0.09f, c);
}

void Eye(float cx, float cy, float s, Color c) {
    const float w = std::max(1.4f, s * 0.08f);
    Gfx::Arc(cx, cy + s * 0.32f, s * 0.5f, -kPi + 0.62f, -0.62f, w, c);
    Gfx::Arc(cx, cy - s * 0.32f, s * 0.5f, 0.62f, kPi - 0.62f, w, c);
    Gfx::FillCircle(cx, cy, s * 0.12f, c);
}

void Spinner(float cx, float cy, float r, Color c, double t) {
    float a = (float)std::fmod(t * 5.0, 2 * kPi);
    Gfx::Arc(cx, cy, r, a, a + kPi * 1.4f, std::max(2.0f, r * 0.22f), c);
}

void Logo(float cx, float cy, float s, float alpha) {
    // BrowseNX: a compass, blue dial with a white and red needle.
    const float r = s * 0.5f;
    Gfx::FillCircle(cx, cy, r, Color(0x3D, 0x7B, 0xFF).Alpha(alpha));
    Gfx::StrokeCircle(cx, cy, r * 0.82f, std::max(1.0f, r * 0.06f), Color(255, 255, 255).Alpha(alpha * 0.55f));
    const float c = 0.70710678f, n = r * 0.62f, w = r * 0.16f;  // needle points north-east
    SDL_FPoint tip{cx + n * c, cy - n * c}, tail{cx - n * c, cy + n * c};
    SDL_FPoint left{cx - w * c, cy - w * c}, right{cx + w * c, cy + w * c};
    Gfx::FillPolygon({tip, right, left}, Color(0xFF, 0x4D, 0x4D).Alpha(alpha));
    Gfx::FillPolygon({tail, left, right}, Color(255, 255, 255).Alpha(alpha));
}

void Status(float cx, float cy, float r, const std::string& status, Color ring) {
    Gfx::FillCircle(cx, cy, r + 3, ring);
    if (status == "online") {
        Gfx::FillCircle(cx, cy, r, Color(0x23, 0xA5, 0x5A));
    } else if (status == "idle") {
        Gfx::FillCircle(cx, cy, r, Color(0xF0, 0xB2, 0x32));
        Gfx::FillCircle(cx - r * 0.45f, cy - r * 0.45f, r * 0.62f, ring);
    } else if (status == "dnd") {
        Gfx::FillCircle(cx, cy, r, Color(0xF2, 0x3F, 0x43));
        Gfx::FillRound(Rect{cx - r * 0.6f, cy - r * 0.18f, r * 1.2f, r * 0.36f}, r * 0.18f, ring);
    } else {
        Gfx::FillCircle(cx, cy, r, Color(0x80, 0x84, 0x8E));
        Gfx::FillCircle(cx, cy, r * 0.45f, ring);
    }
}

float ButtonWidth(const std::string& label) {
    if (label.size() == 1 && isalpha((unsigned char)label[0])) return 24;
    return std::max(24.0f, Text::Width(label, Face::Bold, 12) + 14);
}

float Button(float x, float cy, const std::string& label, Color fg, Color bg) {
    float w = ButtonWidth(label);
    if (w <= 24 && label.size() == 1 && isalpha((unsigned char)label[0])) {
        Gfx::FillCircle(x + 12, cy, 12, fg);
        Text::DrawCentered(label, x + 12, cy - Text::GetMetrics(Face::Bold, 13).lineHeight * 0.5f, Face::Bold, 13, bg);
    } else {
        Gfx::FillRound(Rect{x, cy - 11, w, 22}, 11, fg);
        Text::DrawCentered(label, x + w * 0.5f, cy - Text::GetMetrics(Face::Bold, 12).lineHeight * 0.5f, Face::Bold, 12, bg);
    }
    return w;
}

float HintsWidth(const std::vector<std::pair<std::string, std::string>>& items) {
    float w = 0;
    for (size_t i = 0; i < items.size(); i++) {
        if (i) w += 18;
        w += ButtonWidth(items[i].first) + 7 + Text::Width(items[i].second, Face::Medium, 14);
    }
    return w;
}

void Hints(float right, float cy, const std::vector<std::pair<std::string, std::string>>& items, Color text,
           Color glyphBg, Color glyphFg) {
    float x = right - HintsWidth(items);
    const float th = Text::GetMetrics(Face::Medium, 14).lineHeight;
    for (size_t i = 0; i < items.size(); i++) {
        if (i) x += 18;
        x += Button(x, cy, items[i].first, glyphBg, glyphFg) + 7;
        x += Text::Draw(items[i].second, x, cy - th * 0.5f, Face::Medium, 14, text);
    }
}

}  // namespace Icon
