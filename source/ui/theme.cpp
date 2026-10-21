#include "ui/theme.h"

namespace {

Theme g_theme;

Color H(uint32_t rgb, uint8_t a = 255) { return Color::Hex(rgb, a); }

}  // namespace

const Theme& T() { return g_theme; }

void InitTheme() {
    Theme& t = g_theme;
    t.bg = H(0x0E1116);
    t.surface = H(0x171B22);
    t.surface2 = H(0x1F242D);
    t.surface3 = H(0x2A303B);
    t.border = H(0x30363D);
    t.header = H(0x0A0D12);
    t.input = H(0x0E1116);
    t.text = H(0xC9D1D9);
    t.textStrong = H(0xF0F6FC);
    t.textMuted = H(0x8B949E);
    t.textFaint = H(0x6E7681);
    t.link = H(0x58A6FF);
    t.accent = H(0x3D7BFF);
    t.accentText = H(0xFFFFFF);
    t.warn = H(0xD29922);
    t.danger = H(0xF85149);
    t.scrim = H(0x05070A, 190);
    t.codeBg = H(0x0A0D12);
}
