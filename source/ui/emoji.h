#pragma once

#include <cstddef>
#include <string>

// Stub Twemoji API: emoji detection is a no-op here because the GitHub UI only
// renders plain repo/release text. Keeping the API shape so text.cpp compiles
// unchanged from the SwitchCord drop.
namespace EmojiText {

inline size_t Match(const char*, const char*, std::string*) { return 0; }
inline std::string TwemojiUrl(const std::string&) { return {}; }
inline bool OnlyEmoji(const std::string&, int* count) {
    if (count) *count = 0;
    return false;
}

}  // namespace EmojiText
