#pragma once
//
// Stance Dance rules, kept apart from the game code so they can be tested on
// their own.
//
//   total       the stance stat as the game reads it, everything added up
//   base        the character's own value of that stat: the plugin's part
//   ascendancy  total - base: what the ascendancy item (and anything else
//               that is not the character's own value) gives
//
// The ascendancy gives 1 and the plugin's part is 0, 1 or 2, so the total is
// 1, 2 or 3. A key press moves it 1 -> 2 -> 3 -> 1.

#include <algorithm>
#include <cstdint>

namespace stance {

inline constexpr std::int64_t First = 1;
inline constexpr std::int64_t Last  = 3;

struct Reading {
    std::int64_t total{};
    std::int64_t base{};
};

constexpr auto Ascendancy(const Reading& reading) noexcept -> std::int64_t {
    return reading.total - reading.base;
}

// The key works only while the ascendancy gives at least First.
constexpr auto Unlocked(const Reading& reading) noexcept -> bool {
    return Ascendancy(reading) >= First;
}

// The largest part the plugin may add without the total going above Last.
constexpr auto MaxPart(std::int64_t ascendancy) noexcept -> std::int64_t {
    return ascendancy >= Last ? 0 : Last - ascendancy;
}

// The part the character should have right now: none without the
// ascendancy, otherwise the current part kept within 0 .. MaxPart.
constexpr auto AllowedPart(const Reading& reading) noexcept -> std::int64_t {
    if (!Unlocked(reading)) return 0;
    return std::clamp<std::int64_t>(reading.base, 0, MaxPart(Ascendancy(reading)));
}

// The part after one key press: the next stance, back to First after Last.
constexpr auto NextPart(const Reading& reading) noexcept -> std::int64_t {
    if (!Unlocked(reading)) return 0;
    const std::int64_t ascendancy = Ascendancy(reading);
    const std::int64_t current    = std::clamp<std::int64_t>(ascendancy + AllowedPart(reading), First, Last);
    const std::int64_t next       = current >= Last ? First : current + 1;
    return std::clamp<std::int64_t>(next - ascendancy, 0, MaxPart(ascendancy));
}

// The cycle with the ascendancy's 1, checked when the plugin compiles.
static_assert(NextPart({ 1, 0 }) == 1 && NextPart({ 2, 1 }) == 2 && NextPart({ 3, 2 }) == 0);
// No ascendancy: no part, whatever the character had.
static_assert(NextPart({ 0, 0 }) == 0 && AllowedPart({ 2, 2 }) == 0);

}  // namespace stance
