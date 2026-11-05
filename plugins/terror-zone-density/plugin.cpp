// Terror Zone Density
//
// Terrorized levels spawn more monsters and more champion and unique packs.
// Settings: celestialrayone.terror-zone-density.toml. Console command: tzdensity.
//
// Everything below was read out of the D2RLoader 1.3.1 process dump (md5
// 8af7a47ce9077de0378c69af2930854d) with IDA, and out of D2RCore.dll 1.3.1
// (SHA-256 2a868d013d2e0830bd2d9e04b918b19e46a73cf726c833e70d089b948fdeb5a2).
// Every game address the plugin uses is guarded by a byte witness at load.
//
// ---------------------------------------------------------------------------
// When monsters are created
// ---------------------------------------------------------------------------
//   Not when the game starts. A room gets its monsters the first time it
//   activates, that is when a player comes near it:
//     0x4042A0  room activation (game, room): D2RCore PopulateRoomSpecialMonsters,
//               then, for a room not filled yet: preset monsters 0x48F190,
//               objects 0x503790, 0x510F00, random monsters 0x50ACF0, and
//               0x2F04E0 (room, 1) marks the room filled.
//   Single player and multiplayer work the same way: this runs where the game
//   is hosted, and a room activates when any player comes near it.
//
// ---------------------------------------------------------------------------
// Where the numbers come from
// ---------------------------------------------------------------------------
//   Every level has a monster region record: game+1D0h -> table[level id].
//   D2RCore's InitializeGameMonsterRegions allocates the table when the game
//   is created and the game fills each record from levels.txt for the game's
//   difficulty. The fields used here:
//     +2B8h int32  density (MonDen of the game's difficulty)
//     +2BCh uint8  minimum packs (MonUMin)
//     +2BDh uint8  maximum packs (MonUMax)
//   The random-monster pass 0x50ACF0 reads them every time a room fills:
//     0x50AD92  cmp [region+2B8h],0 / je end            density 0: no monsters
//     0x50AE10  density capped at 10000 and written back
//     0x50AF03  cmp roll % 100000, [region+2B8h]        one roll per 3x3 subtiles
//     0x50AF40  call 0x419D20 (region, room)             pack or normal group
//   0x419D20 compares +2BCh and +2BDh with the packs the level already spawned
//   (+2C8h). Terror zones do not touch these fields: 0x508F60 only sets the
//   region's terror flag (+2E8h), terror level (+2ECh) and terror config (+2F0h).
//
// ---------------------------------------------------------------------------
// What the plugin does
// ---------------------------------------------------------------------------
//   An inline hook on 0x50ACF0 runs before the random-monster pass of every
//   room. It reads the room's level (0x2EFC10, the call 0x50ACF0 makes first),
//   asks the game whether that level is terrorized right now (0x489B70, the
//   test the game uses for terror monster levels at 0x508FAA), and reads the
//   level's own values from levels.txt for the game's difficulty. Then:
//     terrorized      the record gets the boosted values
//     not terrorized  the record gets its levels.txt values back, but only if
//                     it still holds this plugin's boosted values
//   A record holding anything else (changed by something else) is left alone
//   and reported once in the log. Nothing is stored between rooms, so a new
//   game, another difficulty or a terror zone rotation needs no bookkeeping.
//
// ---------------------------------------------------------------------------
// levels.txt row (compiled), from the column table built at 0x32EAC0
// ---------------------------------------------------------------------------
//   rows at data tables+1460h, row count at +1468h, 18Ch bytes per row,
//   row index = level id (0x32C7ED reads it that way)
//     +22h int16    MonLvlEx(H)
//     +24h int32[3] MonDen, MonDen(N), MonDen(H)
//     +30h uint8[3] MonUMin, MonUMin(N), MonUMin(H)
//     +33h uint8[3] MonUMax, MonUMax(N), MonUMax(H)
//   data tables of a game: 0x300A90 (game+106h)
//   game difficulty: game+104h, uint8 (0x48FF59)
//
// ---------------------------------------------------------------------------
// ABIs (x64)
// ---------------------------------------------------------------------------
//   random-monster pass  0x50ACF0 (game, room) -> void
//   room level id        0x2EFC10 (room) -> int32, 0 for no room
//   level terrorized     0x489B70 (game, level id) -> bool
//   data tables          0x300A90 (data context byte, 0 to 3) -> tables

#include <D2RLPlugin/api.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace CelestialRayOne::TerrorZoneDensity {
namespace {

// ---- rules begin: plain C++, no game access ---------------------------------

constexpr std::uint32_t MaximumLevelId  = 0xFFFF;   // D2RCore's region table holds 65536 levels
constexpr std::int32_t  GameDensityCap  = 10000;    // 0x50AE16: mov eax, 2710h / cmovg
constexpr std::uint32_t MaximumPacks    = 255;      // uint8 fields
constexpr std::int32_t  MaximumDensity  = 10000;
constexpr std::uint32_t MaximumPermille = 10000;    // packs_multiplier up to 10

enum class Group : std::uint8_t { Untouched, Campaign, Endgame };
enum class Action : std::uint8_t { Nothing, Boost, Restore, Foreign };

struct GroupRule {
    std::int32_t  densityAdd    = 0;
    std::uint32_t packsPermille = 1000;   // packs_multiplier * 1000
};

struct Values {
    std::int32_t  density  = 0;
    std::uint32_t packsMin = 0;
    std::uint32_t packsMax = 0;
};

constexpr auto operator==(const Values& a, const Values& b) noexcept -> bool {
    return a.density == b.density && a.packsMin == b.packsMin && a.packsMax == b.packsMax;
}

class LevelSet {
public:
    void Add(std::uint32_t id) noexcept {
        if (id > MaximumLevelId) return;
        std::uint64_t& word = bits_[id / 64];
        const std::uint64_t bit = std::uint64_t{1} << (id % 64);
        if ((word & bit) == 0) {
            word |= bit;
            ++count_;
        }
    }
    void Clear() noexcept {
        bits_.fill(0);
        count_ = 0;
    }
    auto Has(std::uint32_t id) const noexcept -> bool {
        return id <= MaximumLevelId && (bits_[id / 64] & (std::uint64_t{1} << (id % 64))) != 0;
    }
    auto Count() const noexcept -> std::uint32_t { return count_; }

private:
    std::array<std::uint64_t, (MaximumLevelId + 1) / 64> bits_{};
    std::uint32_t                                         count_ = 0;
};

struct Settings {
    bool         enabled             = true;
    bool         logChanges          = true;
    std::int32_t densityCap          = 3000;
    std::int32_t endgameMonsterLevel = 92;
    GroupRule    campaign{ 1500, 3000 };
    GroupRule    endgame{ 500, 1500 };
    LevelSet     endgameLevels;
    LevelSet     mapLevels;
};

void SetDefaultLists(Settings& settings) noexcept {
    settings.endgameLevels.Clear();
    settings.endgameLevels.Add(115);   // Naraku Level 1
    settings.endgameLevels.Add(214);   // Naraku Level 2
    settings.mapLevels.Clear();
    for (std::uint32_t id = 139; id <= 212; ++id) settings.mapLevels.Add(id);
}

auto ScalePacks(std::uint32_t value, std::uint32_t permille) noexcept -> std::uint32_t {
    const std::uint64_t scaled = (static_cast<std::uint64_t>(value) * permille + 500) / 1000;
    return scaled > MaximumPacks ? MaximumPacks : static_cast<std::uint32_t>(scaled);
}

auto BoostDensity(std::int32_t value, std::int32_t add, std::int32_t cap) noexcept -> std::int32_t {
    if (value <= 0 || value >= cap) return value;
    const std::int64_t raised = static_cast<std::int64_t>(value) + add;
    return raised > cap ? cap : static_cast<std::int32_t>(raised);
}

auto Boost(const Values& base, const GroupRule& rule, std::int32_t cap) noexcept -> Values {
    return Values{ BoostDensity(base.density, rule.densityAdd, cap), ScalePacks(base.packsMin, rule.packsPermille),
        ScalePacks(base.packsMax, rule.packsPermille) };
}

auto Classify(const Settings& settings, std::uint32_t levelId, std::int32_t monLvlExHell) noexcept -> Group {
    if (settings.mapLevels.Has(levelId)) return Group::Untouched;
    if (settings.endgameLevels.Has(levelId) || monLvlExHell >= settings.endgameMonsterLevel) return Group::Endgame;
    return Group::Campaign;
}

// The game writes the density back capped at 10000 every time a room fills.
auto AsStored(Values values) noexcept -> Values {
    if (values.density > GameDensityCap) values.density = GameDensityCap;
    return values;
}

auto Decide(const Values& base, const Values& boosted, const Values& current, bool terrorized) noexcept -> Action {
    const Values storedBase    = AsStored(base);
    const Values storedBoosted = AsStored(boosted);
    if (storedBoosted == storedBase) return Action::Nothing;
    if (current == (terrorized ? storedBoosted : storedBase)) return Action::Nothing;
    if (terrorized && current == storedBase) return Action::Boost;
    if (!terrorized && current == storedBoosted) return Action::Restore;
    return Action::Foreign;
}

// Small TOML subset: [section], key = value, # comments, true/false, whole
// numbers, decimals, and lists of ids or [first, last] ranges (may span lines).

struct ConfigProblem {
    std::size_t line = 0;
    std::string text;
};

auto Trim(std::string_view value) noexcept -> std::string_view {
    std::size_t first = 0;
    while (first < value.size() && (value[first] == ' ' || value[first] == '\t' || value[first] == '\r')) ++first;
    std::size_t last = value.size();
    while (last > first && (value[last - 1] == ' ' || value[last - 1] == '\t' || value[last - 1] == '\r')) --last;
    return value.substr(first, last - first);
}

auto ParseWhole(std::string_view text, std::int64_t& out) noexcept -> bool {
    if (text.empty()) return false;
    std::size_t index = 0;
    bool negative = false;
    if (text[0] == '-' || text[0] == '+') {
        negative = text[0] == '-';
        index = 1;
    }
    if (index >= text.size()) return false;
    std::int64_t value = 0;
    for (; index < text.size(); ++index) {
        const char c = text[index];
        if (c < '0' || c > '9') return false;
        value = value * 10 + (c - '0');
        if (value > 1'000'000'000) return false;
    }
    out = negative ? -value : value;
    return true;
}

// "3", "3.0", "1.5", "0.125" -> thousandths. At most three decimals.
auto ParseThousandths(std::string_view text, std::uint32_t& out) noexcept -> bool {
    if (text.empty()) return false;
    std::uint64_t whole = 0;
    std::size_t   index = 0;
    bool          digits = false;
    for (; index < text.size() && text[index] >= '0' && text[index] <= '9'; ++index) {
        whole = whole * 10 + static_cast<std::uint64_t>(text[index] - '0');
        digits = true;
        if (whole > 1'000'000) return false;
    }
    std::uint64_t fraction = 0;
    std::size_t   decimals = 0;
    if (index < text.size() && text[index] == '.') {
        ++index;
        for (; index < text.size() && text[index] >= '0' && text[index] <= '9'; ++index) {
            if (++decimals > 3) return false;
            fraction = fraction * 10 + static_cast<std::uint64_t>(text[index] - '0');
            digits = true;
        }
    }
    if (!digits || index != text.size()) return false;
    while (decimals < 3) {
        fraction *= 10;
        ++decimals;
    }
    const std::uint64_t value = whole * 1000 + fraction;
    if (value > 0xFFFFFFFFull) return false;
    out = static_cast<std::uint32_t>(value);
    return true;
}

// [115, 214] or [[139, 212], 300]: every id must be 1 to 65535.
auto ParseLevelList(std::string_view text, LevelSet& out, std::string& error) noexcept -> bool {
    LevelSet result;
    std::size_t index = 0;
    auto skip = [&]() {
        while (index < text.size() && (text[index] == ' ' || text[index] == '\t' || text[index] == '\r' || text[index] == '\n'))
            ++index;
    };
    auto number = [&](std::uint32_t& value) -> bool {
        skip();
        const std::size_t start = index;
        while (index < text.size() && text[index] >= '0' && text[index] <= '9') ++index;
        std::int64_t parsed = 0;
        if (!ParseWhole(text.substr(start, index - start), parsed) || parsed < 1 || parsed > MaximumLevelId) {
            error = "level ids must be whole numbers from 1 to 65535";
            return false;
        }
        value = static_cast<std::uint32_t>(parsed);
        return true;
    };
    skip();
    if (index >= text.size() || text[index] != '[') {
        error = "a list must start with [";
        return false;
    }
    ++index;
    for (;;) {
        skip();
        if (index < text.size() && text[index] == ']') {
            ++index;
            break;
        }
        if (index < text.size() && text[index] == '[') {
            ++index;
            std::uint32_t first = 0;
            std::uint32_t last  = 0;
            if (!number(first)) return false;
            skip();
            if (index >= text.size() || text[index] != ',') {
                error = "a range is written [first, last]";
                return false;
            }
            ++index;
            if (!number(last)) return false;
            skip();
            if (index >= text.size() || text[index] != ']') {
                error = "a range is written [first, last]";
                return false;
            }
            ++index;
            if (last < first) {
                error = "a range must not end before it starts";
                return false;
            }
            for (std::uint32_t id = first; id <= last; ++id) result.Add(id);
        } else {
            std::uint32_t id = 0;
            if (!number(id)) return false;
            result.Add(id);
        }
        skip();
        if (index < text.size() && text[index] == ',') {
            ++index;
            continue;
        }
        skip();
        if (index < text.size() && text[index] == ']') {
            ++index;
            break;
        }
        error = "expected , or ] in the list";
        return false;
    }
    skip();
    if (index != text.size()) {
        error = "unexpected text after the list";
        return false;
    }
    out = result;
    return true;
}

auto StripComment(std::string_view line) noexcept -> std::string_view {
    const std::size_t hash = line.find('#');
    return Trim(hash == std::string_view::npos ? line : line.substr(0, hash));
}

auto BracketBalance(std::string_view text) noexcept -> int {
    int depth = 0;
    for (const char c : text) {
        if (c == '[') ++depth;
        if (c == ']') --depth;
    }
    return depth;
}

void ParseSettings(std::string_view text, Settings& settings, std::vector<ConfigProblem>& problems) noexcept {
    settings = Settings{};
    SetDefaultLists(settings);

    std::size_t cursor = 0;
    std::size_t lineNumber = 0;
    bool inSection = false;
    bool sawSection = false;
    while (cursor < text.size()) {
        std::size_t end = text.find('\n', cursor);
        if (end == std::string_view::npos) end = text.size();
        std::string_view line = StripComment(text.substr(cursor, end - cursor));
        cursor = end + 1;
        ++lineNumber;
        const std::size_t startLine = lineNumber;
        if (line.empty()) continue;

        if (line.front() == '[' && line.find('=') == std::string_view::npos) {
            inSection = line == "[terror_zone_density]";
            sawSection = sawSection || inSection;
            if (!inSection) problems.push_back({ startLine, "unknown section " + std::string(line) + ", its settings are ignored" });
            continue;
        }
        const std::size_t equals = line.find('=');
        if (equals == std::string_view::npos) {
            problems.push_back({ startLine, "expected key = value" });
            continue;
        }
        const std::string_view key = Trim(line.substr(0, equals));
        std::string value(Trim(line.substr(equals + 1)));
        // A list may continue on the next lines until its brackets close.
        while (BracketBalance(value) > 0 && cursor < text.size()) {
            std::size_t next = text.find('\n', cursor);
            if (next == std::string_view::npos) next = text.size();
            value += ' ';
            value += StripComment(text.substr(cursor, next - cursor));
            cursor = next + 1;
            ++lineNumber;
        }
        if (!inSection) {
            problems.push_back({ startLine, std::string(key) + " is outside [terror_zone_density] and is ignored" });
            continue;
        }

        auto boolean = [&](bool& target) {
            if (value == "true") target = true;
            else if (value == "false") target = false;
            else problems.push_back({ startLine, std::string(key) + " must be true or false, keeping the default" });
        };
        auto whole = [&](std::int32_t& target, std::int64_t minimum, std::int64_t maximum) {
            std::int64_t parsed = 0;
            if (ParseWhole(value, parsed) && parsed >= minimum && parsed <= maximum) target = static_cast<std::int32_t>(parsed);
            else problems.push_back({ startLine, std::string(key) + " must be a whole number from " + std::to_string(minimum) + " to " + std::to_string(maximum) + ", keeping the default" });
        };
        auto multiplier = [&](std::uint32_t& target) {
            std::uint32_t parsed = 0;
            if (ParseThousandths(value, parsed) && parsed <= MaximumPermille) target = parsed;
            else problems.push_back({ startLine, std::string(key) + " must be a number from 0 to 10 with at most three decimals, keeping the default" });
        };
        auto list = [&](LevelSet& target) {
            std::string error;
            if (!ParseLevelList(value, target, error)) problems.push_back({ startLine, std::string(key) + ": " + error + ", keeping the default" });
        };

        if (key == "enabled") boolean(settings.enabled);
        else if (key == "log_changes") boolean(settings.logChanges);
        else if (key == "density_cap") whole(settings.densityCap, 0, MaximumDensity);
        else if (key == "endgame_monster_level") whole(settings.endgameMonsterLevel, 0, 32767);
        else if (key == "campaign_density_add") whole(settings.campaign.densityAdd, 0, MaximumDensity);
        else if (key == "campaign_packs_multiplier") multiplier(settings.campaign.packsPermille);
        else if (key == "endgame_density_add") whole(settings.endgame.densityAdd, 0, MaximumDensity);
        else if (key == "endgame_packs_multiplier") multiplier(settings.endgame.packsPermille);
        else if (key == "endgame_levels") list(settings.endgameLevels);
        else if (key == "map_levels") list(settings.mapLevels);
        else problems.push_back({ startLine, "unknown setting " + std::string(key) + " is ignored" });
    }
    if (!sawSection) problems.push_back({ 0, "no [terror_zone_density] section, using the defaults" });
}

// ---- rules end ----------------------------------------------------------------

// ---------------------------------------------------------------------------
// Native contract
// ---------------------------------------------------------------------------

constexpr std::uint64_t PopulateRoomRva       = 0x50ACF0;
constexpr std::uint64_t PopulateLevelSiteRva  = 0x50AD0E;
constexpr std::uint64_t PopulateRegionSiteRva = 0x50AD5E;
constexpr std::uint64_t PopulateRollSiteRva   = 0x50AF03;
constexpr std::uint64_t PackTypeRva           = 0x419D20;
constexpr std::uint64_t IsTerrorizedRva       = 0x489B70;
constexpr std::uint64_t RoomLevelIdRva        = 0x2EFC10;
constexpr std::uint64_t DrlgRoomLevelIdRva    = 0x360FC0;
constexpr std::uint64_t DataTablesRva         = 0x300A90;
constexpr std::uint64_t LevelRowSiteRva       = 0x32C7ED;
constexpr std::uint64_t LevelNamesSiteRva     = 0x32ED42;
constexpr std::uint64_t LevelColumnsSiteRva   = 0x32EE29;
constexpr std::uint64_t DifficultySiteRva     = 0x48FF59;

constexpr std::size_t GameDifficultyOffset   = 0x104;   // uint8
constexpr std::size_t GameDataContextOffset  = 0x106;   // uint8
constexpr std::size_t GameRegionTableOffset  = 0x1D0;   // MonsterRegion* table, indexed by level id
constexpr std::size_t RegionDensityOffset    = 0x2B8;   // int32
constexpr std::size_t RegionPacksMinOffset   = 0x2BC;   // uint8
constexpr std::size_t RegionPacksMaxOffset   = 0x2BD;   // uint8
constexpr std::size_t TablesLevelRowsOffset  = 0x1460;  // levels.txt rows
constexpr std::size_t TablesLevelCountOffset = 0x1468;  // levels.txt row count, uint64
constexpr std::size_t LevelRowSize           = 0x18C;
constexpr std::size_t LevelMonLvlExHell      = 0x22;    // int16
constexpr std::size_t LevelMonDen            = 0x24;    // int32[3]
constexpr std::size_t LevelMonUMin           = 0x30;    // uint8[3]
constexpr std::size_t LevelMonUMax           = 0x33;    // uint8[3]

// ---------------------------------------------------------------------------
// Byte witnesses (D2RLoader 1.3.1 dump)
// ---------------------------------------------------------------------------

// 0x50ACF0, hook prologue: mov r11,rsp / push rbx / push rsi / sub rsp,0B8h.
// Twelve bytes, whole instructions, nothing relative; the next instruction
// is the security cookie load.
constexpr std::uint8_t PopulateRoomPrologue[]{
    0x4C, 0x8B, 0xDC, 0x53, 0x56, 0x48, 0x81, 0xEC, 0xB8, 0x00, 0x00, 0x00,
};  // 12 bytes

// 0x50AD0E: cmp [monsters on],0 / je end / room level 0x2EFC10 (room) /
// mov rcx,[game+1D0h] / call 0x508740 (table, level) / test / je end / argument
// setup of the D2RCore call at 0x50AD59 (not included).
constexpr std::uint8_t PopulateLevelSite[]{
    0x83, 0x3D, 0xA3, 0x28, 0xE8, 0x01, 0x00, 0x48, 0x8B, 0xF2, 0x48, 0x8B,
    0xD9, 0x0F, 0x84, 0x73, 0x04, 0x00, 0x00, 0x49, 0x89, 0x7B, 0xE8, 0x48,
    0x8B, 0xCA, 0x4D, 0x89, 0x73, 0xD0, 0xE8, 0xDF, 0x4E, 0xDE, 0xFF, 0x8B,
    0xD0, 0x48, 0x8B, 0x8B, 0xD0, 0x01, 0x00, 0x00, 0xE8, 0x01, 0xDA, 0xFF,
    0xFF, 0x48, 0x8B, 0xF8, 0x48, 0x85, 0xC0, 0x0F, 0x84, 0x39, 0x04, 0x00,
    0x00, 0x48, 0x8B, 0xCB, 0x48, 0x8B, 0xD6, 0x48, 0x89, 0xAC, 0x24, 0xE0,
    0x00, 0x00, 0x00,
};  // 75 bytes

// 0x50AD5E: after the D2RCore call. Region +0Ch room count, cmp [region+2B8h],0 /
// je end, the Chaos Sanctum check, the room box list, then the populated
// level 0x2EFD00, the second region lookup and the density read capped at
// 10000 and written back to [region+2B8h].
constexpr std::uint8_t PopulateRegionSite[]{
    0x8B, 0xE8, 0x85, 0xC0, 0x0F, 0x84, 0x14, 0x04, 0x00, 0x00, 0x83, 0x7F,
    0x0C, 0x00, 0x7D, 0x24, 0x0F, 0xB6, 0x17, 0x48, 0x8D, 0x8B, 0x90, 0x01,
    0x00, 0x00, 0xE8, 0x13, 0x7E, 0xF7, 0xFF, 0x0F, 0xB6, 0x8B, 0x06, 0x01,
    0x00, 0x00, 0x44, 0x8B, 0xC5, 0x48, 0x8B, 0x10, 0xE8, 0x91, 0x4C, 0xDE,
    0xFF, 0x89, 0x47, 0x0C, 0x83, 0xBF, 0xB8, 0x02, 0x00, 0x00, 0x00, 0x0F,
    0x84, 0xDD, 0x03, 0x00, 0x00, 0x83, 0x7F, 0x0C, 0x00, 0x0F, 0x84, 0xD3,
    0x03, 0x00, 0x00, 0x48, 0x8B, 0xCE, 0xE8, 0x5F, 0x4E, 0xDE, 0xFF, 0x83,
    0xF8, 0x6C, 0x75, 0x10, 0x48, 0x8B, 0xCB, 0xE8, 0x32, 0x9B, 0x0D, 0x00,
    0x85, 0xC0, 0x0F, 0x85, 0xB6, 0x03, 0x00, 0x00, 0x48, 0x8B, 0xCE, 0x4C,
    0x89, 0xAC, 0x24, 0xA0, 0x00, 0x00, 0x00, 0xE8, 0xFA, 0x4C, 0xDE, 0xFF,
    0x4C, 0x8B, 0xE8, 0x48, 0x85, 0xC0, 0x0F, 0x84, 0x92, 0x03, 0x00, 0x00,
    0x4C, 0x89, 0xA4, 0x24, 0xA8, 0x00, 0x00, 0x00, 0x48, 0x8B, 0xCE, 0x4C,
    0x89, 0xBC, 0x24, 0x90, 0x00, 0x00, 0x00, 0xE8, 0x06, 0x4F, 0xDE, 0xFF,
    0x8B, 0xD0, 0x48, 0x8B, 0x8B, 0xD0, 0x01, 0x00, 0x00, 0xE8, 0x38, 0xD9,
    0xFF, 0xFF, 0x48, 0x8B, 0xE8, 0x48, 0x89, 0x44, 0x24, 0x68, 0x8B, 0x88,
    0xB8, 0x02, 0x00, 0x00, 0xB8, 0x10, 0x27, 0x00, 0x00, 0x3B, 0xC8, 0x0F,
    0x4F, 0xC8, 0x45, 0x33, 0xF6, 0x41, 0x8B, 0xC6, 0x89, 0x8D, 0xB8, 0x02,
    0x00, 0x00, 0x89, 0x44, 0x24, 0x50,
};  // 210 bytes

// 0x50AF03: cmp roll, [region+2B8h] / jg next / pick a monster 0x419E80 /
// call 0x419D20 (region, room) / cmp eax,1 / je normal group.
constexpr std::uint8_t PopulateRollSite[]{
    0x3B, 0x8D, 0xB8, 0x02, 0x00, 0x00, 0x0F, 0x8F, 0x1C, 0x02, 0x00, 0x00,
    0x44, 0x89, 0x74, 0x24, 0x28, 0x4C, 0x8D, 0x4C, 0x24, 0x60, 0x4C, 0x8B,
    0xC6, 0xC6, 0x44, 0x24, 0x20, 0x14, 0x48, 0x8B, 0xD5, 0x48, 0x8B, 0xCB,
    0xE8, 0x54, 0xEF, 0xF0, 0xFF, 0x48, 0x83, 0x7C, 0x24, 0x60, 0x00, 0x8B,
    0xF8, 0x0F, 0x84, 0x2A, 0x02, 0x00, 0x00, 0x48, 0x8B, 0xD6, 0x48, 0x8B,
    0xCD, 0xE8, 0xDB, 0xED, 0xF0, 0xFF, 0x83, 0xF8, 0x01, 0x0F, 0x84, 0x05,
    0x01, 0x00, 0x00,
};  // 75 bytes

// 0x419D20, pack choice (region, room): movzx edi,[region+2C8h] packs spawned /
// movzx r14d,[region+2BDh] maximum / cmp dil,[region+2BCh] minimum / room
// counts +0Ch and +04h.
constexpr std::uint8_t PackTypeSite[]{
    0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x48, 0x89,
    0x7C, 0x24, 0x20, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x20, 0x0F, 0xB6, 0xB9,
    0xC8, 0x02, 0x00, 0x00, 0x48, 0x8B, 0xEA, 0x44, 0x0F, 0xB6, 0xB1, 0xBD,
    0x02, 0x00, 0x00, 0x40, 0x3A, 0xB9, 0xBC, 0x02, 0x00, 0x00, 0x73, 0x57,
    0x8B, 0x71, 0x0C, 0x85, 0xF6, 0x74, 0x50, 0x48, 0x89, 0x5C, 0x24, 0x30,
    0x8B, 0x59, 0x04,
};  // 63 bytes

// 0x489B70, level terrorized (game, level id): region lookup, the game's list
// of terrorized levels (game+5820h, 90h per entry), cross-checked against
// the region's own flag (0x509580).
constexpr std::uint8_t IsTerrorizedSite[]{
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B,
    0xF9, 0x8B, 0xDA, 0x48, 0x8B, 0x89, 0xD0, 0x01, 0x00, 0x00, 0xE8, 0xB5,
    0xEB, 0x07, 0x00, 0x48, 0x85, 0xC0, 0x74, 0x53, 0x4C, 0x8B, 0x87, 0x20,
    0x58, 0x00, 0x00, 0x48, 0x8B, 0x8F, 0x28, 0x58, 0x00, 0x00, 0x4C, 0x8D,
    0x0C, 0xC9, 0x49, 0xC1, 0xE1, 0x04, 0x4D, 0x03, 0xC8, 0x4D, 0x3B, 0xC1,
    0x74, 0x13, 0x66, 0x90, 0x41, 0x39, 0x18, 0x74, 0x3B, 0x49, 0x81, 0xC0,
    0x90, 0x00, 0x00, 0x00, 0x4D, 0x3B, 0xC1, 0x75, 0xEF, 0x32, 0xDB, 0x48,
    0x8B, 0xC8, 0xE8, 0xB5, 0xF9, 0x07, 0x00, 0x3A, 0xD8, 0x74, 0x25, 0x48,
    0x8D, 0x4C, 0x24, 0x30, 0xC6, 0x44, 0x24, 0x30, 0x00, 0xE8, 0x72, 0xDC,
    0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x01, 0xCC, 0x32, 0xC0, 0x48, 0x8B, 0x5C,
    0x24, 0x38, 0x48, 0x83, 0xC4, 0x20, 0x5F, 0xC3, 0xB3, 0x01, 0xEB, 0xCF,
    0x0F, 0xB6, 0xC3, 0x48, 0x8B, 0x5C, 0x24, 0x38, 0x48, 0x83, 0xC4, 0x20,
    0x5F, 0xC3,
};  // 146 bytes

// 0x2EFC10, room level id: test rcx,rcx / jne / xor eax,eax / ret /
// mov rcx,[rcx+18h] / jmp 0x360FC0.
constexpr std::uint8_t RoomLevelIdSite[]{
    0x48, 0x85, 0xC9, 0x75, 0x03, 0x33, 0xC0, 0xC3, 0x48, 0x8B, 0x49, 0x18,
    0xE9, 0x9F, 0x13, 0x07, 0x00,
};  // 17 bytes

// 0x360FC0: mov rax,[rcx+90h] / mov eax,[rax+1F8h] / ret.
constexpr std::uint8_t DrlgRoomLevelIdSite[]{
    0x48, 0x8B, 0x81, 0x90, 0x00, 0x00, 0x00, 0x8B, 0x80, 0xF8, 0x01, 0x00,
    0x00, 0xC3,
};  // 14 bytes

// 0x300A90, data tables (context): context below 4, then
// mov rax,[0x2A9A580 + context * 16].
constexpr std::uint8_t DataTablesSite[]{
    0x48, 0x83, 0xEC, 0x28, 0x0F, 0xB6, 0xC1, 0x48, 0x89, 0x44, 0x24, 0x38,
    0x48, 0x83, 0xF8, 0x04, 0x72, 0x19, 0x48, 0x8D, 0x44, 0x24, 0x38, 0x48,
    0x8D, 0x4C, 0x24, 0x40, 0x48, 0x89, 0x44, 0x24, 0x40, 0xE8, 0x7A, 0xD3,
    0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x01, 0xCC, 0x48, 0x8B, 0x44, 0x24, 0x38,
    0x48, 0x8D, 0x0D, 0xB9, 0x9A, 0x79, 0x02, 0x48, 0x03, 0xC0, 0x48, 0x8B,
    0x04, 0xC1, 0x48, 0x83, 0xC4, 0x28, 0xC3,
};  // 67 bytes

// 0x32C7ED, monster level lookup: lea rdi,[tables+1460h] / cmp level,[rdi+8] /
// imul rax,level,18Ch / add rax,[rdi] / MonLvlEx at +1Eh, MonLvl at +18h.
constexpr std::uint8_t LevelRowSite[]{
    0x49, 0x8D, 0xBE, 0x60, 0x14, 0x00, 0x00, 0x48, 0x89, 0x5C, 0x24, 0x20,
    0x48, 0x3B, 0x5F, 0x08, 0x72, 0x1E, 0x48, 0x8D, 0x44, 0x24, 0x20, 0x48,
    0x89, 0x7C, 0x24, 0x30, 0x48, 0x8D, 0x4C, 0x24, 0x28, 0x48, 0x89, 0x44,
    0x24, 0x28, 0xE8, 0x38, 0xD2, 0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x01, 0xCC,
    0x48, 0x69, 0x44, 0x24, 0x20, 0x8C, 0x01, 0x00, 0x00, 0x48, 0x03, 0x07,
    0x0F, 0x84, 0x10, 0xFF, 0xFF, 0xFF, 0x45, 0x85, 0xFF, 0x74, 0x0A, 0x0F,
    0xBF, 0x44, 0x68, 0x1E, 0xE9, 0x06, 0xFF, 0xFF, 0xFF, 0x0F, 0xBF, 0x44,
    0x68, 0x18,
};  // 86 bytes

// 0x32ED42, levels.txt column table, names: MonLvlEx(H) store, then lea of
// "MonDen" "MonDen(N)" "MonDen(H)" "MonUMin" "MonUMax" "MonUMin(N)" "MonUMax(N)"
// "MonUMin(H)" "MonUMax(H)" into their column entries.
constexpr std::uint8_t LevelNamesSite[]{
    0x48, 0x89, 0x85, 0x80, 0x01, 0x00, 0x00, 0x48, 0x8D, 0x05, 0x94, 0xF9,
    0x9C, 0x01, 0x48, 0x89, 0x85, 0xA0, 0x01, 0x00, 0x00, 0x48, 0x8D, 0x05,
    0x92, 0xF9, 0x9C, 0x01, 0x48, 0x89, 0x85, 0xC0, 0x01, 0x00, 0x00, 0x48,
    0x8D, 0x05, 0x94, 0xF9, 0x9C, 0x01, 0x48, 0x89, 0x85, 0xE0, 0x01, 0x00,
    0x00, 0x48, 0x8D, 0x05, 0x96, 0xF9, 0x9C, 0x01, 0x48, 0x89, 0x85, 0x00,
    0x02, 0x00, 0x00, 0x48, 0x8D, 0x05, 0x90, 0xF9, 0x9C, 0x01, 0x48, 0x89,
    0x85, 0x20, 0x02, 0x00, 0x00, 0x48, 0x8D, 0x05, 0x8A, 0xF9, 0x9C, 0x01,
    0x48, 0x89, 0x85, 0x40, 0x02, 0x00, 0x00, 0x48, 0x8D, 0x05, 0x8C, 0xF9,
    0x9C, 0x01, 0x48, 0x89, 0x85, 0x60, 0x02, 0x00, 0x00, 0x48, 0x8D, 0x05,
    0x8E, 0xF9, 0x9C, 0x01, 0x48, 0x89, 0x85, 0x80, 0x02, 0x00, 0x00, 0x48,
    0x8D, 0x05, 0x90, 0xF9, 0x9C, 0x01, 0x48, 0x89, 0x85, 0xA0, 0x02, 0x00,
    0x00,
};  // 133 bytes

// 0x32EE29, the same column entries, type and offset: MonLvlEx(H) word +22h,
// MonDen dword +24h +28h +2Ch, MonUMin byte +30h +31h +32h, MonUMax byte +33h
// +34h +35h.
constexpr std::uint8_t LevelColumnsSite[]{
    0x48, 0xC7, 0x85, 0x88, 0x01, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x48,
    0xC7, 0x85, 0x90, 0x01, 0x00, 0x00, 0x22, 0x00, 0x00, 0x00, 0x4C, 0x89,
    0xB5, 0x98, 0x01, 0x00, 0x00, 0x48, 0xC7, 0x85, 0xA8, 0x01, 0x00, 0x00,
    0x02, 0x00, 0x00, 0x00, 0x48, 0xC7, 0x85, 0xB0, 0x01, 0x00, 0x00, 0x24,
    0x00, 0x00, 0x00, 0x4C, 0x89, 0xB5, 0xB8, 0x01, 0x00, 0x00, 0x48, 0xC7,
    0x85, 0xC8, 0x01, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x48, 0xC7, 0x85,
    0xD0, 0x01, 0x00, 0x00, 0x28, 0x00, 0x00, 0x00, 0x4C, 0x89, 0xB5, 0xD8,
    0x01, 0x00, 0x00, 0x48, 0xC7, 0x85, 0xE8, 0x01, 0x00, 0x00, 0x02, 0x00,
    0x00, 0x00, 0x48, 0xC7, 0x85, 0xF0, 0x01, 0x00, 0x00, 0x2C, 0x00, 0x00,
    0x00, 0x4C, 0x89, 0xB5, 0xF8, 0x01, 0x00, 0x00, 0x48, 0xC7, 0x85, 0x08,
    0x02, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x48, 0xC7, 0x85, 0x10, 0x02,
    0x00, 0x00, 0x30, 0x00, 0x00, 0x00, 0x4C, 0x89, 0xB5, 0x18, 0x02, 0x00,
    0x00, 0x48, 0xC7, 0x85, 0x28, 0x02, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00,
    0x48, 0xC7, 0x85, 0x30, 0x02, 0x00, 0x00, 0x33, 0x00, 0x00, 0x00, 0x4C,
    0x89, 0xB5, 0x38, 0x02, 0x00, 0x00, 0x48, 0xC7, 0x85, 0x48, 0x02, 0x00,
    0x00, 0x04, 0x00, 0x00, 0x00, 0x48, 0xC7, 0x85, 0x50, 0x02, 0x00, 0x00,
    0x31, 0x00, 0x00, 0x00, 0x4C, 0x89, 0xB5, 0x58, 0x02, 0x00, 0x00, 0x48,
    0xC7, 0x85, 0x68, 0x02, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x48, 0xC7,
    0x85, 0x70, 0x02, 0x00, 0x00, 0x34, 0x00, 0x00, 0x00, 0x4C, 0x89, 0xB5,
    0x78, 0x02, 0x00, 0x00, 0x48, 0xC7, 0x85, 0x88, 0x02, 0x00, 0x00, 0x04,
    0x00, 0x00, 0x00, 0x48, 0xC7, 0x85, 0x90, 0x02, 0x00, 0x00, 0x32, 0x00,
    0x00, 0x00, 0x4C, 0x89, 0xB5, 0x98, 0x02, 0x00, 0x00, 0x48, 0xC7, 0x85,
    0xA8, 0x02, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x48, 0xC7, 0x85, 0xB0,
    0x02, 0x00, 0x00, 0x35, 0x00, 0x00, 0x00,
};  // 283 bytes

// 0x48FF59: movzx eax, byte ptr [rdx+104h], the game's difficulty byte.
constexpr std::uint8_t DifficultySite[]{
    0x0F, 0xB6, 0x82, 0x04, 0x01, 0x00, 0x00,
};  // 7 bytes


using PopulateRoomFn = void(__fastcall*)(void* game, void* room) noexcept;
using RoomLevelIdFn  = std::int32_t(__fastcall*)(void* room) noexcept;
using IsTerrorizedFn = bool(__fastcall*)(void* game, std::int32_t levelId) noexcept;
using DataTablesFn   = std::uint8_t*(__fastcall*)(std::uint8_t context) noexcept;

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

constexpr char DefaultConfigToml[] =
    "# Terror Zone Density\n"
    "#\n"
    "# Terrorized levels spawn more monsters and more champion and unique packs.\n"
    "#\n"
    "# How it works:\n"
    "# - The game does not create monsters when a game starts. A room gets its\n"
    "#   monsters the first time a player comes near it, and the game reads the\n"
    "#   level's monster density (levels.txt MonDen) and pack counts (MonUMin,\n"
    "#   MonUMax) at that moment.\n"
    "# - Right before a room gets its monsters, this plugin checks whether that\n"
    "#   room's level is terrorized. If it is, the level uses the boosted values\n"
    "#   below. When the terror zone moves on, the level goes back to its\n"
    "#   levels.txt values.\n"
    "# - Rooms that already have their monsters keep them. Only rooms nobody has\n"
    "#   reached yet are affected.\n"
    "# - Each difficulty uses its own columns: MonDen, MonDen(N), MonDen(H), and\n"
    "#   the matching MonUMin and MonUMax columns.\n"
    "# - Multiplayer: monsters are created where the game is hosted, so only the\n"
    "#   host needs this plugin. Players who join need nothing.\n"
    "#\n"
    "# Level groups:\n"
    "# - Endgame maps (map_levels) are never changed.\n"
    "# - Endgame levels: every other level whose MonLvlEx(H) is at least\n"
    "#   endgame_monster_level, plus the levels listed in endgame_levels.\n"
    "# - Campaign levels: everything else.\n"
    "#\n"
    "# Rules:\n"
    "# - Density: the levels.txt value plus density_add, never above density_cap.\n"
    "#   A level already above density_cap keeps its own value. A level with\n"
    "#   density 0 (no random monsters) stays at 0.\n"
    "# - Packs: MonUMin and MonUMax are multiplied by packs_multiplier and\n"
    "#   rounded, .5 rounds up (9 x 1.5 = 14). The game stores pack counts in one\n"
    "#   byte, so 255 is the most a level can have.\n"
    "# - More monsters also means more herald rolls: every kill rolls for one.\n"
    "#\n"
    "# Console command: tzdensity (state, settings, counters, recent changes).\n"
    "\n"
    "[terror_zone_density]\n"
    "\n"
    "# Master switch.\n"
    "enabled = true\n"
    "\n"
    "# Write one log line for every level the plugin boosts or puts back.\n"
    "log_changes = true\n"
    "\n"
    "# No boosted density goes above this, 0 to 10000. The game itself stops at 10000.\n"
    "density_cap = 3000\n"
    "\n"
    "# Levels whose MonLvlEx(H) is at least this count as endgame levels.\n"
    "endgame_monster_level = 92\n"
    "\n"
    "# Campaign levels. density_add 0 to 10000, packs_multiplier 0 to 10.\n"
    "campaign_density_add = 1500\n"
    "campaign_packs_multiplier = 3.0\n"
    "\n"
    "# Endgame levels. density_add 0 to 10000, packs_multiplier 0 to 10.\n"
    "endgame_density_add = 500\n"
    "endgame_packs_multiplier = 1.5\n"
    "\n"
    "# More endgame levels, whatever their MonLvlEx(H). levels.txt Id.\n"
    "# 115 Naraku Level 1, 214 Naraku Level 2.\n"
    "endgame_levels = [115, 214]\n"
    "\n"
    "# Endgame maps, never changed. levels.txt Id: single ids or [first, last] ranges.\n"
    "# 139 to 212: Chasm of Horrors through The Altar of Doomed Salvation and Infernal Nexus.\n"
    "map_levels = [[139, 212]]\n";

constexpr std::uint32_t MaximumConfigBytes = 65'536;

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

struct Change {
    std::int32_t level      = 0;
    std::uint8_t difficulty = 0;
    Group        group      = Group::Untouched;
    bool         boosted    = false;
    Values       from;
    Values       to;
};

const D2RL::PluginContext* Context{};
std::uintptr_t             Base{};
Settings                   Config{};

RoomLevelIdFn  RoomLevelId{};
IsTerrorizedFn IsTerrorized{};
DataTablesFn   DataTables{};
std::atomic<PopulateRoomFn> OriginalPopulateRoom{};

std::atomic<bool>          Armed{ false };
std::atomic<bool>          Installed{ false };
std::atomic<std::uint64_t> Boosts{ 0 };
std::atomic<std::uint64_t> Restores{ 0 };
std::atomic<std::uint64_t> Foreign{ 0 };
std::array<std::atomic<std::uint64_t>, (MaximumLevelId + 1) / 64> ForeignReported{};

constexpr std::size_t   ChangeSlots = 16;
SRWLOCK                 ChangesLock = SRWLOCK_INIT;
std::array<Change, ChangeSlots> Changes{};
std::size_t             ChangeCount = 0;
std::size_t             ChangeNext  = 0;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

template <typename T>
auto At(std::uint64_t rva) noexcept -> T {
    return reinterpret_cast<T>(Base + rva);
}

template <typename T>
auto Read(const void* base, std::size_t offset) noexcept -> T {
    T value{};
    std::memcpy(&value, static_cast<const std::uint8_t*>(base) + offset, sizeof(T));
    return value;
}

auto DifficultyName(std::uint8_t difficulty) noexcept -> const char* {
    switch (difficulty) {
    case 0: return "Normal";
    case 1: return "Nightmare";
    case 2: return "Hell";
    }
    return "?";
}

auto GroupName(Group group) noexcept -> const char* {
    switch (group) {
    case Group::Campaign: return "campaign";
    case Group::Endgame:  return "endgame";
    case Group::Untouched: return "map";
    }
    return "?";
}

void RecordChange(const Change& change) noexcept {
    AcquireSRWLockExclusive(&ChangesLock);
    Changes[ChangeNext] = change;
    ChangeNext = (ChangeNext + 1) % ChangeSlots;
    if (ChangeCount < ChangeSlots) ++ChangeCount;
    ReleaseSRWLockExclusive(&ChangesLock);

    if (Config.logChanges) {
        D2RL::LogInfoF(Context,
            "TerrorZoneDensity: level %d (%s, %s) %s: MonDen %d -> %d, MonUMin %u -> %u, MonUMax %u -> %u.",
            change.level, GroupName(change.group), DifficultyName(change.difficulty),
            change.boosted ? "terrorized, boosted" : "no longer terrorized, put back",
            change.from.density, change.to.density, change.from.packsMin, change.to.packsMin, change.from.packsMax,
            change.to.packsMax);
    }
}

void ReportForeign(std::int32_t level, std::uint8_t difficulty, const Values& current, const Values& base,
    const Values& boosted) noexcept {
    Foreign.fetch_add(1, std::memory_order_relaxed);
    const auto id = static_cast<std::uint32_t>(level);
    const std::uint64_t bit = std::uint64_t{1} << (id % 64);
    if ((ForeignReported[id / 64].fetch_or(bit, std::memory_order_relaxed) & bit) != 0) return;
    D2RL::LogWarnF(Context,
        "TerrorZoneDensity: level %d (%s) left alone. Its record holds MonDen %d, MonUMin %u, MonUMax %u, which is "
        "neither its levels.txt values (%d, %u, %u) nor the boosted values (%d, %u, %u). Something else changed it. "
        "This is reported once per level.",
        level, DifficultyName(difficulty), current.density, current.packsMin, current.packsMax, base.density,
        base.packsMin, base.packsMax, boosted.density, boosted.packsMin, boosted.packsMax);
}

// ---------------------------------------------------------------------------
// The hook
// ---------------------------------------------------------------------------

void SyncRoomLevel(void* game, void* room) noexcept {
    const std::int32_t level = RoomLevelId(room);
    if (level <= 0 || static_cast<std::uint32_t>(level) > MaximumLevelId) return;

    const std::uint8_t difficulty = Read<std::uint8_t>(game, GameDifficultyOffset);
    const std::uint8_t context    = Read<std::uint8_t>(game, GameDataContextOffset);
    if (difficulty > 2 || context > 3) return;

    auto* const* table = Read<std::uint8_t* const*>(game, GameRegionTableOffset);
    if (table == nullptr) return;
    const std::uint8_t* tables = DataTables(context);
    if (tables == nullptr) return;
    const auto* rows = Read<const std::uint8_t*>(tables, TablesLevelRowsOffset);
    const auto  count = Read<std::uint64_t>(tables, TablesLevelCountOffset);
    if (rows == nullptr || static_cast<std::uint64_t>(level) >= count) return;
    std::uint8_t* region = table[level];
    if (region == nullptr) return;

    const std::uint8_t* row = rows + static_cast<std::size_t>(level) * LevelRowSize;
    const Group group = Classify(Config, static_cast<std::uint32_t>(level), Read<std::int16_t>(row, LevelMonLvlExHell));
    if (group == Group::Untouched) return;

    const Values base{ Read<std::int32_t>(row, LevelMonDen + 4U * difficulty), row[LevelMonUMin + difficulty],
        row[LevelMonUMax + difficulty] };
    const Values boosted = Boost(base, group == Group::Endgame ? Config.endgame : Config.campaign, Config.densityCap);
    if (AsStored(boosted) == AsStored(base)) return;

    const bool terrorized = IsTerrorized(game, level);
    const Values current{ Read<std::int32_t>(region, RegionDensityOffset), region[RegionPacksMinOffset],
        region[RegionPacksMaxOffset] };

    switch (Decide(base, boosted, current, terrorized)) {
    case Action::Nothing:
        return;
    case Action::Foreign:
        ReportForeign(level, difficulty, current, base, boosted);
        return;
    case Action::Boost:
    case Action::Restore:
        break;
    }

    const Values target = AsStored(terrorized ? boosted : base);
    std::memcpy(region + RegionDensityOffset, &target.density, sizeof(target.density));
    region[RegionPacksMinOffset] = static_cast<std::uint8_t>(target.packsMin);
    region[RegionPacksMaxOffset] = static_cast<std::uint8_t>(target.packsMax);
    (terrorized ? Boosts : Restores).fetch_add(1, std::memory_order_relaxed);
    RecordChange(Change{ level, difficulty, group, terrorized, current, target });
}

void __fastcall PopulateRoomHook(void* game, void* room) noexcept {
    if (Armed.load(std::memory_order_relaxed) && game != nullptr && room != nullptr) {
        SyncRoomLevel(game, room);
    }
    const PopulateRoomFn original = OriginalPopulateRoom.load(std::memory_order_acquire);
    if (original != nullptr) original(game, room);
}

// ---------------------------------------------------------------------------
// Install
// ---------------------------------------------------------------------------

auto CheckWitnesses() noexcept -> bool {
    struct Witness {
        std::uint64_t       rva;
        const std::uint8_t* bytes;
        std::uint32_t       size;
        const char*         name;
    };
    const Witness witnesses[]{
        { PopulateLevelSiteRva, PopulateLevelSite, sizeof(PopulateLevelSite), "random-monster pass, level and region lookup" },
        { PopulateRegionSiteRva, PopulateRegionSite, sizeof(PopulateRegionSite), "random-monster pass, density reads" },
        { PopulateRollSiteRva, PopulateRollSite, sizeof(PopulateRollSite), "random-monster pass, density roll and pack choice" },
        { PackTypeRva, PackTypeSite, sizeof(PackTypeSite), "pack choice, minimum and maximum packs" },
        { IsTerrorizedRva, IsTerrorizedSite, sizeof(IsTerrorizedSite), "level terrorized test" },
        { RoomLevelIdRva, RoomLevelIdSite, sizeof(RoomLevelIdSite), "room level id" },
        { DrlgRoomLevelIdRva, DrlgRoomLevelIdSite, sizeof(DrlgRoomLevelIdSite), "room level id, level read" },
        { DataTablesRva, DataTablesSite, sizeof(DataTablesSite), "data tables lookup" },
        { LevelRowSiteRva, LevelRowSite, sizeof(LevelRowSite), "levels.txt rows and row size" },
        { LevelNamesSiteRva, LevelNamesSite, sizeof(LevelNamesSite), "levels.txt MonDen, MonUMin, MonUMax column names" },
        { LevelColumnsSiteRva, LevelColumnsSite, sizeof(LevelColumnsSite), "levels.txt MonLvlEx(H), MonDen, MonUMin, MonUMax column offsets" },
        { DifficultySiteRva, DifficultySite, sizeof(DifficultySite), "game difficulty byte" },
    };
    bool ok = true;
    for (const Witness& witness : witnesses) {
        if (!Context->CheckExpectedBytes(witness.rva, witness.bytes, witness.size)) {
            D2RL::LogErrorF(Context,
                "TerrorZoneDensity: the %s at 0x%llX does not match D2RLoader 1.3.1; wrong game build or another "
                "plugin patched it.",
                witness.name, static_cast<unsigned long long>(witness.rva));
            ok = false;
        }
    }
    return ok;
}

auto InstallHook() noexcept -> bool {
    RoomLevelId  = At<RoomLevelIdFn>(RoomLevelIdRva);
    IsTerrorized = At<IsTerrorizedFn>(IsTerrorizedRva);
    DataTables   = At<DataTablesFn>(DataTablesRva);

    PopulateRoomFn original = nullptr;
    if (!Context->InstallInlineHook(PopulateRoomRva, PopulateRoomPrologue, sizeof(PopulateRoomPrologue),
            &PopulateRoomHook, &original)
        || original == nullptr) {
        D2RL::LogError(Context,
            "TerrorZoneDensity: NOT installed. The hook on the random-monster pass (0x50ACF0) was refused; another "
            "plugin may hook the same function.");
        return false;
    }
    OriginalPopulateRoom.store(original, std::memory_order_release);
    return true;
}

// ---------------------------------------------------------------------------
// Config file
// ---------------------------------------------------------------------------

void ReadConfiguration() noexcept {
    Config = Settings{};
    SetDefaultLists(Config);
    if (!Context->EnsureConfig(DefaultConfigToml)) {
        D2RL::LogWarn(Context, "TerrorZoneDensity: the config file could not be created; using the defaults.");
        return;
    }
    std::string buffer(MaximumConfigBytes, '\0');
    std::uint32_t required = 0;
    if (!Context->ReadConfig(buffer.data(), static_cast<std::uint32_t>(buffer.size()), &required)) {
        D2RL::LogWarn(Context, "TerrorZoneDensity: the config file could not be read; using the defaults.");
        return;
    }
    buffer.resize(std::strlen(buffer.c_str()));
    std::vector<ConfigProblem> problems;
    ParseSettings(buffer, Config, problems);
    for (const ConfigProblem& problem : problems) {
        D2RL::LogWarnF(Context, "TerrorZoneDensity: config line %u: %s.", static_cast<unsigned>(problem.line),
            problem.text.c_str());
    }
}

// ---------------------------------------------------------------------------
// Console command
// ---------------------------------------------------------------------------

auto StateName() noexcept -> const char* {
    if (!Config.enabled) return "disabled in the config file";
    if (!Installed.load(std::memory_order_relaxed)) return "NOT installed, see the log";
    return "active";
}

// 3000 -> "3.0", 1500 -> "1.5", 125 -> "0.125"
auto Thousandths(std::uint32_t value, char (&out)[24]) noexcept -> const char* {
    const std::uint32_t fraction = value % 1000;
    if (fraction == 0) std::snprintf(out, sizeof(out), "%u.0", value / 1000);
    else if (fraction % 100 == 0) std::snprintf(out, sizeof(out), "%u.%u", value / 1000, fraction / 100);
    else if (fraction % 10 == 0) std::snprintf(out, sizeof(out), "%u.%02u", value / 1000, fraction / 10);
    else std::snprintf(out, sizeof(out), "%u.%03u", value / 1000, fraction);
    return out;
}

auto __cdecl StatusCommand(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command, void*) noexcept
    -> D2RL::ConsoleCommandResult {
    if (command == nullptr || command->plugin == nullptr) return D2RL::ConsoleCommandResult::Failed;
    char campaign[24];
    char endgame[24];
    char line[640];
    std::snprintf(line, sizeof(line),
        "Terror Zone Density: %s | campaign MonDen +%d, packs x%s | endgame (MonLvlEx(H) %d+, %u listed levels) MonDen "
        "+%d, packs x%s | density cap %d | %u map levels untouched",
        StateName(), Config.campaign.densityAdd, Thousandths(Config.campaign.packsPermille, campaign),
        Config.endgameMonsterLevel, Config.endgameLevels.Count(), Config.endgame.densityAdd,
        Thousandths(Config.endgame.packsPermille, endgame), Config.densityCap, Config.mapLevels.Count());
    command->plugin->WriteConsoleMessage(line);
    std::snprintf(line, sizeof(line), "Levels boosted %llu, put back %llu, left alone (changed by something else) %llu",
        static_cast<unsigned long long>(Boosts.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(Restores.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(Foreign.load(std::memory_order_relaxed)));
    command->plugin->WriteConsoleMessage(line);

    std::array<Change, ChangeSlots> recent{};
    std::size_t count = 0;
    std::size_t next  = 0;
    AcquireSRWLockShared(&ChangesLock);
    recent = Changes;
    count  = ChangeCount;
    next   = ChangeNext;
    ReleaseSRWLockShared(&ChangesLock);
    for (std::size_t i = 0; i < count; ++i) {
        const Change& change = recent[(next + ChangeSlots - count + i) % ChangeSlots];
        std::snprintf(line, sizeof(line), "  level %d %s %s: MonDen %d -> %d, MonUMin %u -> %u, MonUMax %u -> %u (%s)",
            change.level, DifficultyName(change.difficulty), GroupName(change.group), change.from.density,
            change.to.density, change.from.packsMin, change.to.packsMin, change.from.packsMax, change.to.packsMax,
            change.boosted ? "boosted" : "put back");
        command->plugin->WriteConsoleMessage(line);
    }
    if (count == 0) command->plugin->WriteConsoleMessage("  no level changed yet in this session");
    return D2RL::ConsoleCommandResult::Handled;
}

void RegisterStatusCommand() noexcept {
    if (!Context->RegisterConsoleCommand("tzdensity", &StatusCommand,
            "Reports Terror Zone Density: state, settings, counters and the latest level changes.")) {
        D2RL::LogWarn(Context, "TerrorZoneDensity: the tzdensity console command was refused.");
    }
}

constexpr D2RL::PluginInfo Info{
    .infoSize    = D2RL::PluginInfoSize,
    .abiVersion  = D2RL_PLUGIN_ABI_VERSION,
    .id          = "celestialrayone.terror-zone-density",
    .name        = "Terror Zone Density",
    .version     = "1.0.0",
    .author      = "CelestialRayOne",
    .description = "Terrorized levels spawn more monsters and more champion and unique packs.",
    .flags       = D2RL::PluginFlags::Server | D2RL::PluginFlags::NativeHooks,
};

static_assert(D2RL::HasValidPluginRole(Info.flags), "Exactly one execution role must be set.");
static_assert(D2RL::HasOnlyKnownPluginFlags(Info.flags), "Unknown plugin flag set.");

}  // namespace

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* {
    return &Info;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
    if (!D2RL::HasContext(context)) return false;
    Context = context;
    Base    = context->exeBase;
    if (Base == 0) {
        D2RL::LogError(Context, "TerrorZoneDensity: the game image base is unknown.");
        return false;
    }

    ReadConfiguration();
    RegisterStatusCommand();
    if (!Config.enabled) {
        D2RL::LogInfo(Context, "TerrorZoneDensity: disabled in the config file.");
        return true;
    }
    if (!CheckWitnesses()) return false;
    if (!InstallHook()) return false;

    Installed.store(true, std::memory_order_relaxed);
    Armed.store(true, std::memory_order_release);
    char campaign[24];
    char endgame[24];
    D2RL::LogInfoF(Context,
        "TerrorZoneDensity: installed. Campaign MonDen +%d, packs x%s. Endgame (MonLvlEx(H) %d+ and %u listed "
        "levels) MonDen +%d, packs x%s. Density cap %d. %u map levels untouched.",
        Config.campaign.densityAdd, Thousandths(Config.campaign.packsPermille, campaign), Config.endgameMonsterLevel,
        Config.endgameLevels.Count(), Config.endgame.densityAdd, Thousandths(Config.endgame.packsPermille, endgame),
        Config.densityCap, Config.mapLevels.Count());
    return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
    Armed.store(false, std::memory_order_release);
}

}  // namespace CelestialRayOne::TerrorZoneDensity
