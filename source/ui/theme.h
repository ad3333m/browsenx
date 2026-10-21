#pragma once

#include "ui/gfx.h"

// BrowseNX's dark chrome. All UI draws with it through T(); pages bring their own colours.
struct Theme {
    Color bg;          // page background
    Color surface;     // cards, lists
    Color surface2;    // raised rows, menus
    Color surface3;    // hover
    Color border;      // card and list separators
    Color header;      // top bar
    Color input;       // search field
    Color text;        // body text
    Color textStrong;  // headings, repo names
    Color textMuted;   // metadata, timestamps
    Color textFaint;   // placeholders
    Color link;        // clickable text
    Color accent;      // primary buttons, pointer, progress
    Color accentText;
    Color warn;        // amber notices
    Color danger;
    Color scrim;       // dialog backdrop
    Color codeBg;
};

const Theme& T();
void InitTheme();
