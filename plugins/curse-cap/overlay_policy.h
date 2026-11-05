#pragma once
//
// Extra Curses, client side: when a new overlay command would delete another
// state's cast overlay before it finished, that cast hands over to its own
// follow-up overlays instead of vanishing.
//
// No Windows or SDK dependencies, so it is unit-tested on its own. plugin.cpp
// proves every offset below against the game image before using it.
//
// Overlay node (0xB8 bytes, built by the dispatcher at 0x150423..0x150514):
//   +00h mode, +04h overlay id, +08h follow-up 1, +0Ch follow-up 2,
//   +10h state (modes 8 and 9), +B0h next node.
//
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace extra_curses {

inline constexpr std::size_t OverlayNodeModeOffset = 0x00;
inline constexpr std::size_t OverlayNodeIdOffset = 0x04;
inline constexpr std::size_t OverlayNodeFollowUp1Offset = 0x08;
inline constexpr std::size_t OverlayNodeFollowUp2Offset = 0x0C;
inline constexpr std::size_t OverlayNodeStateOffset = 0x10;

inline constexpr std::int32_t OverlayModeLoop = 3;       // looping overlay
inline constexpr std::int32_t OverlayModeCast = 4;       // cast overlay, then follow-ups as mode 3
inline constexpr std::int32_t OverlayModeStateLoop = 8;  // looping overlay tied to a state
inline constexpr std::int32_t OverlayModeStateCast = 9;  // cast overlay, then follow-ups as mode 8
inline constexpr std::int32_t NoOverlay = -1;

struct OverlayCommand {
    std::int32_t id{NoOverlay};
    std::int32_t mode{};
    std::int32_t followUp1{NoOverlay};
    std::int32_t followUp2{NoOverlay};
};

template <typename T>
inline auto LoadField(const std::uint8_t* base, std::size_t offset) noexcept -> T {
    T value{};
    std::memcpy(&value, base + offset, sizeof(T));
    return value;
}

inline auto IsCastMode(std::int32_t mode) noexcept -> bool {
    return mode == OverlayModeCast || mode == OverlayModeStateCast;
}

// True when the dispatcher is about to delete `node` only because the new
// command uses the same cast overlay, while the node is still playing that
// cast for a DIFFERENT state's visuals (different follow-ups). Two states that
// share a cast overlay, every curse with the stock data, hit this whenever the
// second is applied before the first one's cast animation ended.
//
// Every other match keeps the stock behaviour:
//   - the same follow-ups: the same visuals again, the old one is replaced;
//   - a match through the node's follow-ups: the very overlay is re-added;
//   - a new command that is not a cast, or a node with nothing to hand over.
inline auto IsSupersededForeignCast(
        const std::uint8_t* node,
        const OverlayCommand& command) noexcept -> bool {
    if (node == nullptr || !IsCastMode(command.mode)) return false;
    if (!IsCastMode(LoadField<std::int32_t>(node, OverlayNodeModeOffset))) return false;
    if (LoadField<std::int32_t>(node, OverlayNodeIdOffset) != command.id) return false;
    const auto followUp1 = LoadField<std::int32_t>(node, OverlayNodeFollowUp1Offset);
    const auto followUp2 = LoadField<std::int32_t>(node, OverlayNodeFollowUp2Offset);
    if (followUp1 == NoOverlay && followUp2 == NoOverlay) return false;
    return followUp1 != command.followUp1 || followUp2 != command.followUp2;
}

}  // namespace extra_curses
