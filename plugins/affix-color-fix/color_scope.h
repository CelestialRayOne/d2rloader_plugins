#pragma once
//
// Tooltip Color Scope: text logic.
//
// Plain byte-buffer code with no game or Windows dependency. plugin.cpp
// calls it from the hooks; the same header is what the tests exercise.
//
// Color codes as the game sees them
//   The string files write the escape as "y-umlaut c" (bytes C3 BF 63).
//   D2RCore rewrites every one of them in the loaded string tables into the
//   private-use character U+E07E (EE 81 BE), which is the escape the D2R
//   text renderer understands. A color code is the escape plus one color
//   character. Both forms are recognised here, so text that did not pass
//   through the string tables is covered too.
//
// Markers
//   A marker is the escape U+E07E plus 0x01 (end of a stat) or 0x02 (start
//   of a stat). Real color characters are printable, so a marker can never be
//   mistaken for a color code, and the end marker has exactly the size of
//   the color code that later replaces it.
//
//   Markers only exist while one of the game's stat block builders runs.
//   Before the outermost builder returns, every end marker is replaced by
//   the block's own color code and every start marker is deleted, so no
//   marker ever leaves the plugin's hooks.
//
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace CelestialRayOne::TooltipColorScope::Text {

inline constexpr std::size_t   CodeSize    = 4;  // escape (3 bytes) + color character
inline constexpr std::size_t   MarkerSize  = 4;
inline constexpr unsigned char EscapeByte0 = 0xEE;
inline constexpr unsigned char EscapeByte1 = 0x81;
inline constexpr unsigned char EscapeByte2 = 0xBE;
inline constexpr unsigned char LegacyByte0 = 0xC3;  // legacy escape, C3 BF 63
inline constexpr unsigned char LegacyByte1 = 0xBF;
inline constexpr unsigned char LegacyByte2 = 0x63;
inline constexpr unsigned char EndTag      = 0x01;
inline constexpr unsigned char StartTag    = 0x02;

inline constexpr char EndMarker[MarkerSize]{
    static_cast<char>(EscapeByte0), static_cast<char>(EscapeByte1),
    static_cast<char>(EscapeByte2), static_cast<char>(EndTag)};
inline constexpr char StartMarker[MarkerSize]{
    static_cast<char>(EscapeByte0), static_cast<char>(EscapeByte1),
    static_cast<char>(EscapeByte2), static_cast<char>(StartTag)};

enum class Token { None, Code, Start, End };

// Length of a NUL-terminated string inside a buffer of `capacity` bytes.
// Returns `capacity` when there is no terminator inside the buffer.
inline auto BoundedLength(const char* text, std::size_t capacity) noexcept -> std::size_t {
    std::size_t length = 0;
    while (length < capacity && text[length] != '\0') ++length;
    return length;
}

// What starts at text[i]. A code or marker needs all four of its bytes; an
// escape cut off at the end of the text is not a code.
inline auto TokenAt(const char* text, std::size_t length, std::size_t i) noexcept -> Token {
    if (i + CodeSize > length) return Token::None;
    const auto* bytes = reinterpret_cast<const unsigned char*>(text + i);
    if (bytes[0] == EscapeByte0 && bytes[1] == EscapeByte1 && bytes[2] == EscapeByte2) {
        if (bytes[3] == EndTag) return Token::End;
        if (bytes[3] == StartTag) return Token::Start;
        return Token::Code;
    }
    if (bytes[0] == LegacyByte0 && bytes[1] == LegacyByte1 && bytes[2] == LegacyByte2) {
        return Token::Code;
    }
    return Token::None;
}

inline auto ContainsCode(const char* text, std::size_t length) noexcept -> bool {
    for (std::size_t i = 0; i + CodeSize <= length; ++i) {
        if (TokenAt(text, length, i) == Token::Code) return true;
    }
    return false;
}

inline auto ContainsMarker(const char* text, std::size_t length) noexcept -> bool {
    for (std::size_t i = 0; i + MarkerSize <= length; ++i) {
        const Token token = TokenAt(text, length, i);
        if (token == Token::Start || token == Token::End) return true;
    }
    return false;
}

// Inserts `size` bytes at `at` into a NUL-terminated string of `length` bytes
// held in a buffer of `capacity` bytes. Refuses when the result would not fit.
inline auto InsertAt(char* text, std::size_t& length, std::size_t capacity, std::size_t at,
        const char* bytes, std::size_t size) noexcept -> bool {
    if (at > length || length + size + 1 > capacity) return false;
    std::memmove(text + at + size, text + at, length - at + 1);
    std::memcpy(text + at, bytes, size);
    length += size;
    return true;
}

inline void EraseAt(char* text, std::size_t& length, std::size_t at, std::size_t size) noexcept {
    std::memmove(text + at, text + at + size, length - at - size + 1);
    length -= size;
}

// ---------------------------------------------------------------------------
// Step 1, per stat: the text one stat-line writer produced
// ---------------------------------------------------------------------------
// A stat whose text contains a color code is wrapped as start marker + text +
// end marker. The whole stat is wrapped, including any line breaks inside
// it, so a stat spanning several lines keeps its color across its own lines.
// `reserve` bytes stay free for the newline the renderer appends afterwards.

enum class WrapResult { Untouched, Wrapped, NoRoom };

inline auto WrapStat(char* line, std::size_t capacity, std::size_t reserve) noexcept -> WrapResult {
    const std::size_t length = BoundedLength(line, capacity);
    if (length >= capacity) return WrapResult::Untouched;
    if (!ContainsCode(line, length) || ContainsMarker(line, length)) return WrapResult::Untouched;
    if (length + 2 * MarkerSize + reserve + 1 > capacity) return WrapResult::NoRoom;
    std::memmove(line + MarkerSize, line, length + 1);
    std::memcpy(line, StartMarker, MarkerSize);
    std::memcpy(line + MarkerSize + length, EndMarker, MarkerSize);
    line[length + 2 * MarkerSize] = '\0';
    return WrapResult::Wrapped;
}

// ---------------------------------------------------------------------------
// Step 2, per block builder: text that did not come from the stat writers
// ---------------------------------------------------------------------------
// The damage-range lines (stats 17 to 59) are written by the game's composer
// straight into the block, and so are lines other plugins compose there (the
// bleed line). Those lines are single lines. Every such line, or part of a
// line before a wrapped stat, that contains a color code gets an end marker
// where it ends. Wrapped stats are skipped. Running this twice over the same
// text adds nothing the second time.

struct CloseResult {
    std::uint32_t closed = 0;
    std::uint32_t noRoom = 0;
};

inline auto CloseLines(char* text, std::size_t capacity, std::size_t start) noexcept -> CloseResult {
    CloseResult result{};
    std::size_t length = BoundedLength(text, capacity);
    if (length >= capacity || start >= length) return result;

    bool insideStat = false;  // between a start marker and its end marker
    bool open       = false;  // a color code outside wrapped stats is still active
    auto close = [&](std::size_t at) noexcept -> bool {
        open = false;
        if (InsertAt(text, length, capacity, at, EndMarker, MarkerSize)) {
            ++result.closed;
            return true;
        }
        ++result.noRoom;
        return false;
    };

    std::size_t i = start;
    while (i < length) {
        switch (TokenAt(text, length, i)) {
        case Token::Start:
            if (!insideStat && open && close(i)) i += MarkerSize;
            insideStat = true;
            i += MarkerSize;
            continue;
        case Token::End:
            if (insideStat) insideStat = false;
            else open = false;
            i += MarkerSize;
            continue;
        case Token::Code:
            if (!insideStat) open = true;
            i += CodeSize;
            continue;
        case Token::None:
            break;
        }
        if (!insideStat && open && text[i] == '\n' && close(i)) i += MarkerSize;
        ++i;
    }
    if (!insideStat && open) close(length);
    return result;
}

// ---------------------------------------------------------------------------
// Step 3, when the outermost block builder returns
// ---------------------------------------------------------------------------
// The game's colorizer is asked to color a one-character probe with the
// block's color, which yields exactly the code it will later put in front of
// the block, followed by the probe character. Returns the length of that
// code, or 0 when the output does not start with a recognised color code.

inline auto ExtractBlockCode(const char* produced, std::size_t capacity, char probe,
        std::size_t maxCode) noexcept -> std::size_t {
    const std::size_t length = BoundedLength(produced, capacity);
    if (length < 2 || length >= capacity || produced[length - 1] != probe) return 0;
    const std::size_t codeLength = length - 1;
    if (codeLength < CodeSize || codeLength > maxCode) return 0;
    if (TokenAt(produced, length, 0) != Token::Code) return 0;
    return codeLength;
}

struct ResolveResult {
    std::uint32_t restored = 0;  // end markers replaced by the block's code
    std::uint32_t removed  = 0;  // start markers deleted
    std::uint32_t dropped  = 0;  // end markers deleted without a code
};

// Replaces every end marker from `start` on with `code` and deletes every
// start marker. With codeLength 0, every marker is deleted and the text is left
// as the game wrote it. A marker cut short by a full buffer (a lone U+E07E or
// the first bytes of one at the very end) is removed as well.
inline auto ResolveMarkers(char* text, std::size_t capacity, std::size_t start, const char* code,
        std::size_t codeLength) noexcept -> ResolveResult {
    ResolveResult result{};
    std::size_t length = BoundedLength(text, capacity);
    if (length >= capacity || start > length) return result;

    std::size_t i = start;
    while (i < length) {
        const Token token = TokenAt(text, length, i);
        if (token == Token::Start) {
            EraseAt(text, length, i, MarkerSize);
            ++result.removed;
            continue;
        }
        if (token == Token::End) {
            if (codeLength == MarkerSize) {
                std::memcpy(text + i, code, MarkerSize);
                ++result.restored;
                i += MarkerSize;
                continue;
            }
            EraseAt(text, length, i, MarkerSize);
            if (codeLength != 0 && InsertAt(text, length, capacity, i, code, codeLength)) {
                ++result.restored;
                i += codeLength;
            } else {
                ++result.dropped;
            }
            continue;
        }
        i += token == Token::Code ? CodeSize : 1;
    }

    // A cut-off marker: EE, EE 81 or EE 81 BE as the last bytes of the text.
    const auto* bytes = reinterpret_cast<const unsigned char*>(text);
    for (std::size_t cut = 3; cut >= 1; --cut) {
        if (length - start < cut) continue;
        const std::size_t at = length - cut;
        const bool prefix = bytes[at] == EscapeByte0
            && (cut < 2 || bytes[at + 1] == EscapeByte1)
            && (cut < 3 || bytes[at + 2] == EscapeByte2);
        if (prefix) {
            text[at] = '\0';
            ++result.dropped;
            break;
        }
    }
    return result;
}

}  // namespace CelestialRayOne::TooltipColorScope::Text
