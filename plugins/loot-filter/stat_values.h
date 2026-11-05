#pragma once
//
// Tooltip numbers for stats.
//
// The game stores some stats in units its tooltip never shows: life, mana and
// stamina in 256ths, per-level stats in eighths per level. A filter reads every
// stat as the number the item tooltip prints, worked out exactly as the game's
// stat line formatter does it (D2RLoader 1.3.1: 0x2D97E0, then the descfunc
// cases of 0x2D6520), all in 32 bits:
//   op 2 to 5   value * (your character's op base stat >> that stat's
//               ValShift) >> op param
//   then        >> ValShift
//   descfunc 5 and 10   * 100 / 128, rounded toward 0
// Charges (Encode 3) give the skill level held in the param, the first number
// of their line. Everything else, including the sign-flipping descfunc 20 and
// 21 lines, descfunc 11 and the poison stats, stays the stored value.
//
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace lootfilter {

struct StatDisplay {
    std::uint8_t valShift{};
    std::uint8_t op{};
    std::uint8_t opParam{};
    std::uint8_t descFunc{};
    std::uint8_t encode{};
    int          opBase{ -1 };  // itemstatcost.txt row of the op base stat, -1 for none
};

struct StatDisplayTable {
    std::vector<StatDisplay> rows;           // by itemstatcost.txt row
    int                      skillShift{ 6 };  // row 0 "stuff": bits a skill param keeps for the level

    [[nodiscard]] auto Find(int stat) const noexcept -> const StatDisplay* {
        return stat >= 0 && static_cast<std::size_t>(stat) < rows.size() ? &rows[static_cast<std::size_t>(stat)] : nullptr;
    }
    // Encode 2 (chance to cast) and 3 (charges) keep a skill and its level in the param.
    [[nodiscard]] auto PacksSkillLevel(int stat) const noexcept -> bool {
        const StatDisplay* row = Find(stat);
        return row != nullptr && (row->encode == 2 || row->encode == 3);
    }
    [[nodiscard]] auto SkillOf(std::uint32_t param) const noexcept -> std::uint32_t { return param >> skillShift; }
    [[nodiscard]] auto LevelOf(std::uint32_t param) const noexcept -> std::uint32_t { return param & ((1U << skillShift) - 1U); }
    // Whether the tooltip number of `stat` depends on the character's op base stat.
    [[nodiscard]] auto UsesOpBase(int stat) const noexcept -> bool {
        const StatDisplay* row = Find(stat);
        return row != nullptr && row->op >= 2 && row->op <= 5 && Find(row->opBase) != nullptr;
    }
};

// Reads itemstatcost.txt. False when its header lacks one of the columns used.
auto ParseStatDisplayTable(std::string_view text, StatDisplayTable& table) -> bool;

// `baseValue`: the op base stat as stored on your character (only read for op 2 to 5).
auto TooltipNumber(const StatDisplayTable& table, int stat, std::uint32_t param, std::int32_t stored, std::int32_t baseValue) -> std::int32_t;

}  // namespace lootfilter
