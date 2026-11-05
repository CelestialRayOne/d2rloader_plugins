#pragma once
//
// Extra Curses: the decision the plugin hands back to the timed-state builder.
//
// No Windows or SDK dependencies, so the same code is unit-tested against
// fake stat-list chains. plugin.cpp proves every offset below against the
// game and D2RCore images before it uses this.
//
// What the builder does with the answer (D2RCore 1.3.1 +0x3E77B9 onward):
//   nullptr            create the new curse, remove nothing
//   same state         stock refresh, or stock refusal of a lower-level recast
//   another curse      remove that curse the stock way, then create the new one
//
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace extra_curses {

// Stat list layout, D2R 3.3 under D2RLoader 1.3.1.
inline constexpr std::size_t StatListFlagsOffset = 0x1C;    // int32, bit 31 = extended list
inline constexpr std::size_t StatListStateOffset = 0x20;    // int32 state id
inline constexpr std::size_t StatListExpireOffset = 0x24;   // float expire frame
inline constexpr std::size_t StatListNextOffset = 0x68;     // next list in the chain
inline constexpr std::size_t StatListExChainOffset = 0x90;  // chain FindStatListByFlags walks for 0x20

inline constexpr std::uint32_t StatListTimedFlag = 0x2;
inline constexpr std::uint32_t StatListCurseFlag = 0x20;

// A chain longer than this is treated as damaged and not walked further.
inline constexpr std::size_t MaxChainWalk = 4096;

enum class ReplacePolicy : std::uint8_t {
    ShortestRemaining = 0,  // the curse with the least time left
    Oldest = 1,             // the curse applied first
};

enum class Outcome : std::uint8_t {
    NoStatListEx,  // no extended stat list: the stock lookup finds nothing either
    SameState,     // the target has this curse already
    Added,         // room left, nothing removed
    Replaced,      // full, one curse handed to the builder for removal
    Unremovable,   // full, but no curse the builder would remove
};

struct Decision {
    const std::uint8_t* compare{};    // what the lookup returns to the builder
    Outcome outcome{Outcome::NoStatListEx};
    std::uint32_t otherCurses{};      // curses on the target with a different state
    std::int32_t replacedState{-1};
    bool replacedTimed{};
    float replacedExpire{};
    bool chainTruncated{};
};

template <typename T>
inline auto Load(const std::uint8_t* base, std::size_t offset) noexcept -> T {
    T value{};
    std::memcpy(&value, base + offset, sizeof(T));
    return value;
}

// cap = 1 + the caster's points, never below 1.
inline auto CurseCap(std::int32_t points) noexcept -> std::int64_t {
    return 1 + (points > 0 ? static_cast<std::int64_t>(points) : 0);
}

// statListEx is the target's extended stat list (unit + 0x88). The walk is the
// stock FindStatListByFlags(target, 0x20) walk: chain at +0x90, next at +0x68,
// a list matches when its flags have 0x20.
//
// The chain is newest first (PostStatList inserts at the head), so the walk
// meets the oldest curse last.
inline auto Decide(
        const std::uint8_t* statListEx,
        std::int32_t newState,
        std::int64_t cap,
        ReplacePolicy policy) noexcept -> Decision {
    Decision decision{};
    if (statListEx == nullptr
            || Load<std::int32_t>(statListEx, StatListFlagsOffset) >= 0) {
        return decision;
    }

    const std::uint8_t* sameState = nullptr;
    const std::uint8_t* firstCurse = nullptr;
    const std::uint8_t* victim = nullptr;
    bool victimTimed = false;
    float victimExpire = 0.0F;

    std::size_t walked = 0U;
    for (const auto* list = Load<const std::uint8_t*>(statListEx, StatListExChainOffset);
            list != nullptr;
            list = Load<const std::uint8_t*>(list, StatListNextOffset)) {
        if (++walked > MaxChainWalk) {
            decision.chainTruncated = true;
            break;
        }
        const auto flags = Load<std::uint32_t>(list, StatListFlagsOffset);
        if ((flags & StatListCurseFlag) == 0U) continue;

        if (Load<std::int32_t>(list, StatListStateOffset) == newState) {
            if (sameState == nullptr) sameState = list;
            continue;
        }
        if (firstCurse == nullptr) firstCurse = list;
        ++decision.otherCurses;

        // The builder never removes an extended list (+0x3E781C: js skips it).
        if (static_cast<std::int32_t>(flags) < 0) continue;

        const bool timed = (flags & StatListTimedFlag) != 0U;
        const float expire = Load<float>(list, StatListExpireOffset);
        bool take = false;
        if (victim == nullptr || policy == ReplacePolicy::Oldest) {
            // Oldest: every later candidate is older, so the last one wins.
            take = true;
        } else if (timed != victimTimed) {
            // A timed curse runs out before one with no timer.
            take = timed;
        } else if (!timed) {
            take = true;  // two curses with no timer: the older one
        } else {
            take = expire <= victimExpire;  // equal time left: the older one
        }
        if (take) {
            victim = list;
            victimTimed = timed;
            victimExpire = expire;
        }
    }

    if (sameState != nullptr) {
        decision.compare = sameState;
        decision.outcome = Outcome::SameState;
        return decision;
    }
    if (static_cast<std::int64_t>(decision.otherCurses) < cap) {
        decision.outcome = Outcome::Added;
        return decision;
    }
    if (victim != nullptr) {
        decision.compare = victim;
        decision.outcome = Outcome::Replaced;
        decision.replacedState = Load<std::int32_t>(victim, StatListStateOffset);
        decision.replacedTimed = victimTimed;
        decision.replacedExpire = victimExpire;
        return decision;
    }
    // Only lists the builder keeps: answer like the stock lookup would.
    decision.compare = firstCurse;
    decision.outcome = Outcome::Unremovable;
    return decision;
}

}  // namespace extra_curses
