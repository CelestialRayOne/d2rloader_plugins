// ===========================================================================
// Bound Demon Scaling (celestialrayone.bound-demon-scaling)
// ===========================================================================
//
// Bound demons scale like normal summons. A bound demon keeps the monster it
// was bound from for its looks, AI, skills, level and affixes, but its stats
// come from one monstats.txt row named in the TOML, the pseudo row:
//   - Life: the pseudo row's MinHP..MaxHP for the difficulty (monlvl.txt at
//     the demon's level unless the row has noRatio). It replaces the original
//     life and every bonus on it (champion, unique, minion, player count).
//     Bind Demon's calc1 and passivestats are added on top, as for a summon.
//   - Resistances (ResDm..ResPo), AC and ToBlock: the original row's part is
//     swapped for the pseudo row's. What affixes and monprop added stays.
//   - Damage and attack rating of every attack, elemental attacks included:
//     A1/A2/S1 and El1-El3 of the pseudo row.
//   - Life regeneration: the pseudo row's DamageRegen (calc7 still decides
//     who regenerates).
//   - MonProp: the pseudo row's monprop is added to the original monster's.
//   - Hit recovery: a hit only stuns a bound demon when the pseudo row's
//     monstats2 row (MonStatsEx) has the get-hit mode (mGH), and its own
//     model has it too. Its life is summon sized now, so without this its
//     former pack's hits would keep it in hit recovery right after binding.
//   - Resist cap: published as the "stats-row" service, version 1. Summon
//     Resist Cap 3.4.0 and later decide a bound demon's cap from the pseudo
//     row (Align, extra_monster_ids, uncap pairs).
// Server side.
//
// ---------------------------------------------------------------------------
// 1. How a bound demon gets its stats (D2R 3.3.93847, D2RLoader image)
// ---------------------------------------------------------------------------
//   Bind conversion 51EF90 (game, owner, demon, skill, skill level, cast id,
//   newBind byte at [rsp+30h]). Every bind goes through it:
//     new bind  520CF0, call at 520F2D, newBind 1
//     restore   51E0F0 (called by the save restore 531790), call at 51E159,
//               newBind 0
//   It keeps the monster's own stat list. On a new bind it lowers the level
//   (stat 12) to the owner's (51F345..51F37A) and adds Bind Demon's
//   calc2-calc5 monster mods. Then the shared summon setup 55EF80 adds the
//   passivestats to the base stats, puts the aurastats on their own list and
//   sets max life = max life + calc1 percent. Then
//   D2RCore!ApplyBindDemonBoundStats (monpet.txt BoundStat1-5, empty in ESR)
//   and, at 51F453, life regeneration from the monster's own DamageRegen
//   (record +1F4h) through 542E70.
//
// ---------------------------------------------------------------------------
// 2. What the monster brought from its spawn (543E90)
// ---------------------------------------------------------------------------
//   Base stats set from its monstats record (r13) for difficulty d:
//     damageresist 36 = int16 [+196h + 2d]    magicresist 37 = [+19Ch + 2d]
//     fireresist   39 = [+1A2h + 2d]          lightresist 41 = [+1A8h + 2d]
//     coldresist   43 = [+1AEh + 2d]          poisonresist 45 = [+1B4h + 2d]
//     toblock      20 = byte [+0F5h + d]
//   3A26C0 (context, row, game type, d, level, 7, out[14], extra) gives
//   out[0] min life, out[1] max life, out[2] AC; life = min + seed roll,
//   plus the player count percent, held at 7FFFFFh, then << 8 into 7 and 6.
//   Monprop: record +4Eh (word) indexes DataTables +13E8h (count +13F0h,
//   0x138 a row); props at +4 + 16 * (6d + i), chances at +124h + 6d + i,
//   each through 3D40B0 (valid) and 3D4620 (unit, prop, flag 40h).
//   The whole 56 byte spawn info (d at +0, game type flags at +4 / +8,
//   level at +10h, extra at +14h) is copied to MonsterData +78h at 544732.
//   Champion, unique and minion life bonuses multiply life later (49BD50).
//   Affix bonuses to these stats are flat additions. Stock 38E430 (the
//   resistance affixes) adds flat resistances and has Stone Skin double the
//   defense; ESR's Monster Affixes plugin replaces that routine (only Magic
//   Resistant adds anything, and Stone Skin no longer doubles defense), so
//   the rebase keeps every affix bonus exactly by moving only the row's part.
//
// ---------------------------------------------------------------------------
// 3. Damage per attack (588D50, MONSTERMODE_UpdateModeAttackStats)
// ---------------------------------------------------------------------------
//   Not stored: every attack mode recomputes min/max damage and attack
//   rating through 3A26C0 with the class id from 349860 (called at 588F89,
//   589013, 58909D, 5892CA) and reads El1-El3 mode/type/chance from the
//   monstats record loaded at 588E0F (mov r13,[rax], MonsterData +0).
//
// ---------------------------------------------------------------------------
// 4. Save and restore
// ---------------------------------------------------------------------------
//   The save writes every base stat with itemstatcost Save Bits (D2RCore
//   WritePetFormatStats), maxhp, resistances, AC, toblock and hpregen
//   included. The restore 531790 spawns the class, rebinds it and only then
//   reads the saved stats back with a set (ReadPetStats -> 3D9B50). So a
//   restored demon has the stats it was saved with: this plugin's rebase on
//   restore is overwritten by the save, which itself came from a rebased
//   demon. A demon saved before this plugin keeps its saved numbers until a
//   new bind. Damage, attack rating and the cap are read live.
//
// ---------------------------------------------------------------------------
// 5. What this plugin patches
// ---------------------------------------------------------------------------
//   - The two calls to 51EF90 (520F2D, 51E159) call the bind relay. Before
//     the native bind runs, the relay rebases the demon: base life set to
//     the pseudo row's, AC / ToBlock / resistances moved by (pseudo - own),
//     the pseudo row's monprop applied. The native bind then lowers the
//     level, adds its mods and runs the summon setup on top, unchanged.
//     The level used for the pseudo row is the one the bind will leave
//     (51F345..51F37A, mirrored).
//   - 51F453 (mov edi,[rcx+1F4h], 6 bytes) calls a stub: while the relay is
//     binding that demon, edi is the pseudo row's DamageRegen.
//   - The four 349860 calls in 588D50 call a relay that returns the pseudo
//     row for a bound demon (state 207, bind_demon, the test the game's own
//     demon save uses) and the real class id for every other unit.
//   - 588E0F (mov r13,[rax] / test r13,r13, 6 bytes, no branch lands inside;
//     the whole function and its jump table were audited) calls a stub that
//     swaps r13 for the pseudo row's record for a bound demon.
//   - The class id call 44FCD4 in the hit recovery test 44FBA0 calls a relay.
//     44FBA0 skips hit recovery for a hit under maxlife / 8..64 (by hit
//     class, two rolled bands) and for a monster whose monstats2 row has no
//     get-hit mode (1A2D30 -> 0975B0, mode flags at +0F0h). For a bound demon
//     whose pseudo row has no get-hit mode the relay hands it the pseudo row,
//     so the test skips; otherwise it gets its own class id, unchanged.
//   - After every rebased bind the plugin publishes the plugin communication
//     event "demon-bound", version 1 (DemonBoundEvent below) on the game
//     thread, so a display can re-read the demon at once. Monster Display
//     1.0.14 and later refreshes its panel on it.
//   Every site is checked against the bytes below before anything is
//   written; a part whose bytes differ stays off and says so.
//
// ===========================================================================

#include <D2RLPlugin/api.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

namespace {

constexpr char PluginIdText[]  = "celestialrayone.bound-demon-scaling";
constexpr char PluginVersion[] = "1.0.3";

// PURE-BEGIN ------------------------------------------------------------------
// Config parsing. No game or Windows code, so the test harness builds it alone.

struct Config {
    bool        enabled{ true };
    std::string pseudoRow{ "bounddemon" };
    bool        logBinds{ true };
};

constexpr std::size_t MaxRowName = 63;

auto TrimView(std::string_view text) noexcept -> std::string_view {
    std::size_t start = 0;
    while (start < text.size() && (text[start] == ' ' || text[start] == '\t' || text[start] == '\r')) ++start;
    std::size_t end = text.size();
    while (end > start && (text[end - 1] == ' ' || text[end - 1] == '\t' || text[end - 1] == '\r')) --end;
    return text.substr(start, end - start);
}

// The part of a line before a # that is not inside a quoted string.
auto StripComment(std::string_view line) noexcept -> std::string_view {
    bool inString = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (inString) {
            if (c == '\\' && i + 1 < line.size()) {
                ++i;
            } else if (c == '"') {
                inString = false;
            }
        } else if (c == '"') {
            inString = true;
        } else if (c == '#') {
            return line.substr(0, i);
        }
    }
    return line;
}

auto ParseBoolValue(std::string_view value, bool& out) noexcept -> bool {
    if (value == "true") {
        out = true;
        return true;
    }
    if (value == "false") {
        out = false;
        return true;
    }
    return false;
}

auto ParseStringValue(std::string_view value, std::string& out) noexcept -> bool {
    if (value.size() < 2 || value.front() != '"' || value.back() != '"') return false;
    std::string result;
    for (std::size_t i = 1; i + 1 < value.size(); ++i) {
        char c = value[i];
        if (c == '\\') {
            if (i + 2 >= value.size()) return false;
            c = value[++i];
            if (c != '"' && c != '\\') return false;
        } else if (c == '"') {
            return false;
        }
        result.push_back(c);
    }
    out = result;
    return true;
}

// Reads the keys this plugin knows. Problems are reported through the
// callback, once per line, and leave the default in place.
template <typename Report>
void ParseConfigText(std::string_view text, Config& config, Report&& report) {
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF && static_cast<unsigned char>(text[1]) == 0xBB
            && static_cast<unsigned char>(text[2]) == 0xBF) {
        text.remove_prefix(3);
    }
    std::size_t lineNumber = 0;
    while (!text.empty()) {
        ++lineNumber;
        const std::size_t newline = text.find('\n');
        std::string_view line     = text.substr(0, newline);
        text.remove_prefix(newline == std::string_view::npos ? text.size() : newline + 1);

        line = TrimView(StripComment(line));
        if (line.empty()) continue;
        if (line.front() == '[') {
            report(lineNumber, "this plugin has no [sections]; its keys go at the top of the file");
            continue;
        }
        const std::size_t equals = line.find('=');
        if (equals == std::string_view::npos) {
            report(lineNumber, "expected key = value");
            continue;
        }
        const std::string_view key   = TrimView(line.substr(0, equals));
        const std::string_view value = TrimView(line.substr(equals + 1));
        if (key == "enabled") {
            if (!ParseBoolValue(value, config.enabled)) report(lineNumber, "enabled must be true or false");
        } else if (key == "log_binds") {
            if (!ParseBoolValue(value, config.logBinds)) report(lineNumber, "log_binds must be true or false");
        } else if (key == "pseudo_row") {
            std::string name;
            if (!ParseStringValue(value, name)) {
                report(lineNumber, "pseudo_row must be a quoted monstats.txt Id, e.g. \"bounddemon\"");
            } else if (name.empty() || name.size() > MaxRowName) {
                report(lineNumber, "pseudo_row must be 1 to 63 characters");
            } else {
                config.pseudoRow = name;
            }
        } else {
            report(lineNumber, "unknown key, ignored");
        }
    }
}

auto EqualsIgnoreCase(std::string_view a, std::string_view b) noexcept -> bool {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        char x = a[i];
        char y = b[i];
        if (x >= 'A' && x <= 'Z') x = static_cast<char>(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = static_cast<char>(y - 'A' + 'a');
        if (x != y) return false;
    }
    return true;
}

// The spawn's life roll (544179..5441CB): span = max - min, n = span + 1;
// the seed steps lo * 6AC690C5h + hi; the roll is new % n, or new & span
// when n is a power of two.
auto StepSeed(std::uint32_t seed[2]) noexcept -> std::uint32_t {
    const std::uint64_t next = static_cast<std::uint64_t>(seed[0]) * 0x6AC690C5ULL + seed[1];
    seed[0]                  = static_cast<std::uint32_t>(next);
    seed[1]                  = static_cast<std::uint32_t>(next >> 32);
    return seed[0];
}

// min + the roll, exactly as the spawn leaves edi at 5441CF (32 bits).
auto RollLifeRaw(std::int32_t minLife, std::int32_t maxLife, std::uint32_t seed[2]) noexcept -> std::int32_t {
    const std::uint32_t span = static_cast<std::uint32_t>(maxLife) - static_cast<std::uint32_t>(minLife);
    const std::uint32_t n    = span + 1U;
    std::uint32_t       roll = 0;
    if (static_cast<std::int32_t>(n) > 0) {
        const std::uint32_t next = StepSeed(seed);
        roll = (span & n) != 0 ? next % n : next & span;
    }
    return static_cast<std::int32_t>(roll + static_cast<std::uint32_t>(minLife));
}

// The spawn then holds life at 7FFFFFh before the << 8; a life below 1 would
// leave the demon dead, so it is held at 1 as well.
auto RollLife(std::int32_t minLife, std::int32_t maxLife, std::uint32_t seed[2]) noexcept -> std::int64_t {
    std::int64_t life = RollLifeRaw(minLife, maxLife, seed);
    if (life >= 0x800000) life = 0x7FFFFF;
    if (life < 1) life = 1;
    return life;
}

// A base stat moved from the original row's value to the pseudo row's,
// keeping what was added on top of it.
auto Rebased(std::int64_t base, std::int64_t own, std::int64_t pseudo, std::int64_t floor) noexcept -> std::int32_t {
    std::int64_t value = base - own + pseudo;
    if (value < floor) value = floor;
    if (value > 0x7FFFFFFFLL) value = 0x7FFFFFFFLL;
    if (value < -0x7FFFFFFFLL - 1) value = -0x7FFFFFFFLL - 1;
    return static_cast<std::int32_t>(value);
}

// PURE-END --------------------------------------------------------------------

constexpr char DefaultConfig[] = R"toml(# ============================================================================
# Bound Demon Scaling
# ============================================================================
#
# WHAT IT DOES
# Bound demons scale like a normal summon. Every bound demon takes its stats
# from one monstats.txt row (the "pseudo row" below) instead of from the
# monster it was before you bound it. It keeps that monster's looks, AI,
# skills, level and affixes.
#
# FROM THE PSEUDO ROW (the columns of the game's difficulty)
#   Life           MinHP..MaxHP, scaled by monlvl.txt at the demon's level
#                  unless the row has noRatio. It replaces the original life
#                  completely: champion, unique and player count bonuses do
#                  not carry over. Bind Demon's skills.txt calc1 and its
#                  passivestats are added on top, as for any summon.
#   Resistances    ResDm..ResPo, AC and ToBlock. The original row's values are
#   AC, ToBlock    swapped for the pseudo row's; whatever affixes and monprop
#                  added on top of them stays.
#   Damage and     A1MinD/A1MaxD/A1TH, A2..., S1..., and El1..El3, worked out
#   attack rating  on every attack at the demon's level (unless noRatio).
#   Regeneration   DamageRegen. Bind Demon's calc7 still decides who
#                  regenerates.
#   Hit recovery   A hit stuns a bound demon only when the pseudo row's
#                  MonStatsEx (monstats2.txt) row has mGH, and the demon's
#                  own model has it too. With mGH 0 there it never flinches,
#                  like your summons without mGH.
#   MonProp        The pseudo row's monprop is added to the original monster's
#                  own monprop, rolled like a spawn (chance columns). Put the
#                  Kill Tracker kill-skill here to count bound demon kills.
#   Align          With Summon Resist Cap 3.4.0 or later, a bound demon is
#                  capped as the pseudo row: its Align, extra_monster_ids and
#                  [uncap] pairs use the pseudo row's id.
#
# UNCHANGED
# Level (its own, lowered to yours when you bind it), skills, AI, appearance,
# affixes, and everything Bind Demon's skills.txt row adds.
#
# SAVING
# The game saves a bound demon's stats and puts them back after it binds the
# restored demon again, so a demon comes back with the stats it was saved
# with. One bound before this plugin was installed keeps its old life and
# resistances until you bind a new one. Damage, attack rating and the resist
# cap are worked out live and apply to every bound demon at once.
#
# This plugin only reads the pseudo row; it never spawns it.
#
# CONSOLE
# bounddemonscaling shows whether each part is installed, the row it found,
# its counters and the last demon it changed.
# ============================================================================

# Set to false to turn the plugin off.
enabled = true

# monstats.txt Id of the row every bound demon takes its stats from.
pseudo_row = "bounddemon"

# Write one line to the plugin log for every demon bound or restored.
log_binds = true
)toml";

// ---------------------------------------------------------------------------
// Game addresses (RVAs in the D2RLoader process image)
// ---------------------------------------------------------------------------

constexpr std::uint64_t BindConversionRva   = 0x51EF90;
constexpr std::uint64_t BindRestoreCallRva  = 0x51E159;  // in 51E0F0, newBind 0
constexpr std::uint64_t BindNewCallRva      = 0x520F2D;  // in 520CF0, newBind 1
constexpr std::uint64_t RegenReadRva        = 0x51F453;  // mov edi,[rcx+1F4h]
constexpr std::uint64_t AttackRecordReadRva = 0x588E0F;  // mov r13,[rax] / test r13,r13
constexpr std::array<std::uint64_t, 4> AttackClassIdCallRvas{ 0x588F89, 0x589013, 0x58909D, 0x5892CA };
constexpr std::uint64_t HitRecoveryClassIdCallRva = 0x44FCD4;  // in 44FBA0, before the get-hit mode test

constexpr std::uint64_t GetUnitTypeRva       = 0x34B9D0;  // (unit) -> type
constexpr std::uint64_t GetUnitClassIdRva    = 0x349860;  // (unit, file, line) -> class id
constexpr std::uint64_t GetDataContextRva    = 0x34A0E0;  // (unit) -> data context
constexpr std::uint64_t GetDataTablesRva     = 0x300A90;  // (context) -> DataTables
constexpr std::uint64_t GetMonStatsRva       = 0x0976E0;  // (context, row) -> monstats record
constexpr std::uint64_t SkillsRecordRva      = 0x097790;  // (context, skill) -> skills record
constexpr std::uint64_t LevelStatsRva        = 0x3A26C0;  // (context, row, type, d, level, flags, out, extra)
constexpr std::uint64_t DifficultyInfoRva    = 0x48FF40;  // (out, game, unit) -> out
constexpr std::uint64_t UnitSeedRva          = 0x34A1E0;  // (unit) -> uint32[2]
constexpr std::uint64_t ReadStatRva          = 0x2F5020;  // (unit, stat, layer) -> total, loader thunk
constexpr std::uint64_t ReadBaseStatRva      = 0x2F48C0;  // (unit, stat, layer) -> base, loader thunk
constexpr std::uint64_t SetStatRva           = 0x2F7D10;  // (unit, stat, value, layer)
constexpr std::uint64_t CheckStateRva        = 0x3351B0;  // (unit, state) -> nonzero when set
constexpr std::uint64_t PropValidRva         = 0x3D40B0;  // (prop) -> bool
constexpr std::uint64_t AssignPropertyRva    = 0x3D4620;  // (0, 0, unit, 0, 0, 0, prop, 0, flags, 0)
constexpr std::uint64_t LinkerIndexByNameRva = 0xA121F0;  // (table, name, warn) -> row or -1
constexpr std::uint64_t LinkerNameByIndexRva = 0xA12420;  // (table, row, mode) -> name
constexpr std::uint64_t MonStats2RecordRva   = 0x0975B0;  // (context, monstats row) -> monstats2 record, 1A2D39
constexpr std::uint64_t MonsterHasModeRva    = 0x1A2D30;  // (context, monstats row, mode) -> mode flag
constexpr std::uint64_t ModeGetHit           = 3;         // monstats2 mGH

constexpr std::size_t UnitClassIdOffset      = 0x04;
constexpr std::size_t UnitMonsterDataOffset  = 0x10;
constexpr std::size_t GameDataContextOffset  = 0x106;
constexpr std::size_t MonsterSpawnInfoOffset = 0x78;   // 56 bytes, 544732
constexpr std::size_t DataTablesMonStatsLinker = 0xF70;  // 39DBBE: lea r15,[rsi+0F70h], the Id column
constexpr std::size_t DataTablesMonStatsCount  = 0xF60;  // 2A56CD
constexpr std::size_t DataTablesMonPropRows    = 0x13E8; // 5445C3
constexpr std::size_t DataTablesMonPropCount   = 0x13F0; // 5445B6
constexpr std::size_t MonPropRowSize           = 0x138;
constexpr std::size_t MonPropPropsOffset       = 0x04;
constexpr std::size_t MonPropChanceOffset      = 0x124;
constexpr std::size_t MonPropPropSize          = 16;
constexpr std::size_t MonPropPropsPerDiff      = 6;
constexpr std::size_t MonStatsMonPropOffset    = 0x4E;
constexpr std::size_t MonStatsToBlockOffset    = 0xF5;
constexpr std::size_t MonStatsDamageRegen      = 0x1F4;
constexpr std::int32_t AssignPropertyMonsterFlag = 0x40;

constexpr std::uint32_t StatToBlock    = 20;
constexpr std::uint32_t StatArmorClass = 31;
constexpr std::uint32_t StatHitpoints  = 6;
constexpr std::uint32_t StatMaxHp      = 7;
constexpr std::uint32_t StatLevel      = 12;
constexpr std::int32_t  BindDemonState = 207;  // states.txt bind_demon; 533CE0's demon test
constexpr std::int32_t  UnitTypeMonster = 1;
constexpr std::int32_t  LevelStatsLife  = 1;
constexpr std::int32_t  LevelStatsAc    = 2;

struct ResistColumn {
    std::uint32_t stat;
    std::size_t   offset;
    const char*   label;
};
constexpr std::array<ResistColumn, 6> ResistColumns{ {
    { 36, 0x196, "ResDm" },
    { 37, 0x19C, "ResMa" },
    { 39, 0x1A2, "ResFi" },
    { 41, 0x1A8, "ResLi" },
    { 43, 0x1AE, "ResCo" },
    { 45, 0x1B4, "ResPo" },
} };

// ---------------------------------------------------------------------------
// Byte witnesses, read from the D2RLoader 1.3.1 image (D2R 3.3.93847)
// ---------------------------------------------------------------------------

struct Witness {
    std::uint64_t       rva;
    const std::uint8_t* bytes;
    std::uint32_t       size;
    const char*         name;
};

// UNITS_GetUnitType 34B9D0
constexpr std::uint8_t W00[]{
    0x48, 0x83, 0xEC, 0x28, 0x48, 0x85, 0xC9, 0x75, 0x1D, 0x88, 0x4C, 0x24, 0x30, 0x48, 0x8D, 0x4C,
    0x24, 0x30
};

// DATATBLS_GetMonStatsTxtRecord 0976E0
constexpr std::uint8_t W01[]{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x48,
    0x63, 0xF2, 0xE8, 0x99, 0x93, 0x26, 0x00
};

// DATATBLS_GetDataTables 300A90
constexpr std::uint8_t W02[]{
    0x48, 0x83, 0xEC, 0x28, 0x0F, 0xB6, 0xC1, 0x48, 0x89, 0x44, 0x24, 0x38, 0x48, 0x83, 0xF8, 0x04,
    0x72, 0x19
};

// DATATBLS_GetSkillsTxtRecord 097790
constexpr std::uint8_t W03[]{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x48,
    0x63, 0xF2, 0xE8, 0xE9, 0x92, 0x26, 0x00, 0x48, 0x8B, 0xF8, 0x48, 0x8B, 0xDE, 0x85, 0xF6, 0x78,
    0x09, 0x48, 0x3B, 0x98, 0xB8, 0x11, 0x00, 0x00, 0x72, 0x18
};

// STATES_CheckState 3351B0
constexpr std::uint8_t W04[]{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x8B,
    0xDA, 0x48, 0x8B, 0xF1, 0xE8, 0x07, 0x68, 0x01, 0x00
};

// linker name lookup A121F0
constexpr std::uint8_t W05[]{
    0x40, 0x55, 0x56, 0x57, 0x41, 0x54, 0x48, 0x8B, 0xEC, 0x48, 0x83, 0xEC, 0x78, 0x48, 0x8B, 0x05,
    0xC4, 0x90, 0xFB, 0x01, 0x48, 0x33, 0xC4, 0x48, 0x89, 0x45, 0xE8, 0x44, 0x89, 0x45, 0xAC
};

// linker row lookup A12420
constexpr std::uint8_t W06[]{
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x60, 0x48, 0x63, 0xDA, 0x48, 0x85, 0xC9, 0x75, 0x20, 0x88, 0x4C,
    0x24, 0x70, 0x48, 0x8D, 0x4C, 0x24, 0x70, 0xE8, 0xD4, 0xDC, 0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x01
};

// base stat thunk 2F48C0
constexpr std::uint8_t W07[]{
    0xFF, 0x25
};

// base stat thunk padding 2F48C6
constexpr std::uint8_t W08[]{
    0x90, 0x90, 0x90, 0x90
};

// STATLIST_GetUnitBaseStat body 2F48CA
constexpr std::uint8_t W09[]{
    0x48, 0x89, 0x74, 0x24, 0x20, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x41, 0x0F, 0xB7, 0xE8, 0x8B, 0xDA,
    0x48, 0x8B, 0xF9, 0x48, 0x85, 0xC9, 0x75, 0x2A
};

// stat thunk 2F5020
constexpr std::uint8_t W10[]{
    0xFF, 0x25
};

// stat thunk padding 2F5026
constexpr std::uint8_t W11[]{
    0x90, 0x90, 0x90, 0x90
};

// STATLIST_GetUnitStat body 2F502A
constexpr std::uint8_t W12[]{
    0x48, 0x89, 0x74, 0x24, 0x20, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x41, 0x0F, 0xB7, 0xE8, 0x8B, 0xFA,
    0x48, 0x8B, 0xD9, 0x48, 0x85, 0xC9
};

// spawn base resistances and ToBlock 54400A
constexpr std::uint8_t W13[]{
    0x45, 0x0F, 0xBF, 0x84, 0x75, 0x96, 0x01, 0x00, 0x00, 0x45, 0x33, 0xC9, 0x8B, 0xFE, 0x48, 0x8B,
    0xCB, 0x48, 0x89, 0x7D, 0xC7, 0x41, 0x8D, 0x51, 0x24, 0xE8, 0xE8, 0x3C, 0xDB, 0xFF, 0x45, 0x0F,
    0xBF, 0x84, 0x75, 0x9C, 0x01, 0x00, 0x00, 0x45, 0x33, 0xC9, 0x48, 0x8B, 0xCB, 0x41, 0x8D, 0x51,
    0x25, 0xE8, 0xD0, 0x3C, 0xDB, 0xFF, 0x45, 0x0F, 0xBF, 0x84, 0x75, 0xA2, 0x01, 0x00, 0x00, 0x45,
    0x33, 0xC9, 0x48, 0x8B, 0xCB, 0x41, 0x8D, 0x51, 0x27, 0xE8, 0xB8, 0x3C, 0xDB, 0xFF, 0x45, 0x0F,
    0xBF, 0x84, 0x75, 0xA8, 0x01, 0x00, 0x00, 0x45, 0x33, 0xC9, 0x48, 0x8B, 0xCB, 0x41, 0x8D, 0x51,
    0x29, 0xE8, 0xA0, 0x3C, 0xDB, 0xFF, 0x45, 0x0F, 0xBF, 0x84, 0x75, 0xAE, 0x01, 0x00, 0x00, 0x45,
    0x33, 0xC9, 0x48, 0x8B, 0xCB, 0x41, 0x8D, 0x51, 0x2B, 0xE8, 0x88, 0x3C, 0xDB, 0xFF, 0x45, 0x0F,
    0xBF, 0x84, 0x75, 0xB4, 0x01, 0x00, 0x00, 0x45, 0x33, 0xC9, 0x48, 0x8B, 0xCB, 0x41, 0x8D, 0x51,
    0x2D, 0xE8, 0x70, 0x3C, 0xDB, 0xFF, 0x46, 0x0F, 0xB6, 0x84, 0x2E, 0xF5, 0x00, 0x00, 0x00, 0x45,
    0x33, 0xC9, 0x48, 0x8B, 0xCB, 0x41, 0x8D, 0x51, 0x14, 0xE8, 0x58, 0x3C, 0xDB, 0xFF
};

// spawn level stats and life roll 544107
constexpr std::uint8_t W14[]{
    0x45, 0x38, 0x7E, 0x08, 0x75, 0x06, 0x45, 0x39, 0x7E, 0x04, 0x74, 0x06, 0x41, 0xBF, 0x01, 0x00,
    0x00, 0x00, 0x41, 0x8B, 0x7E, 0x14, 0x48, 0x8D, 0x15, 0x9C, 0x92, 0x7F, 0x01, 0x0F, 0x57, 0xC0,
    0x33, 0xC0, 0x41, 0xB8, 0xCE, 0x00, 0x00, 0x00, 0x48, 0x89, 0x45, 0x17, 0x48, 0x8B, 0xCB, 0x0F,
    0x11, 0x45, 0xE7, 0x0F, 0x11, 0x45, 0xF7, 0x0F, 0x11, 0x45, 0x07, 0xE8, 0x19, 0x57, 0xE0, 0xFF,
    0x48, 0x8D, 0x4D, 0xE7, 0x89, 0x7C, 0x24, 0x38, 0x48, 0x89, 0x4C, 0x24, 0x30, 0x44, 0x8B, 0xCE,
    0x8B, 0x4D, 0x9F, 0x45, 0x8B, 0xC7, 0xC7, 0x44, 0x24, 0x28, 0x07, 0x00, 0x00, 0x00, 0x8B, 0xD0,
    0x89, 0x4C, 0x24, 0x20, 0x41, 0x0F, 0xB6, 0x8C, 0x24, 0x06, 0x01, 0x00, 0x00, 0xE8, 0x47, 0xE5,
    0xE5, 0xFF, 0x8B, 0x7D, 0xEB, 0x48, 0x8B, 0xCB, 0x44, 0x8B, 0x7D, 0xE7, 0x41, 0x2B, 0xFF, 0x8D,
    0x77, 0x01, 0xE8, 0x52, 0x60, 0xE0, 0xFF, 0x4C, 0x8B, 0xC8, 0x85, 0xF6, 0x7F, 0x04, 0x33, 0xFF,
    0xEB, 0x32, 0x8B, 0x00, 0x8B, 0xD7, 0x41, 0x8B, 0x49, 0x04, 0x4C, 0x69, 0xC0, 0xC5, 0x90, 0xC6,
    0x6A, 0x4C, 0x03, 0xC1, 0x49, 0x8B, 0xC8, 0x48, 0xC1, 0xE9, 0x20, 0x41, 0x89, 0x49, 0x04, 0x45,
    0x89, 0x01, 0x23, 0xD6, 0x74, 0x0B, 0x33, 0xD2, 0x41, 0x8B, 0xC0, 0xF7, 0xF6, 0x8B, 0xFA, 0xEB,
    0x03, 0x41, 0x23, 0xF8
};

// spawn monprop loop 5445A3
constexpr std::uint8_t W15[]{
    0xE8, 0xE8, 0xC4, 0xDB, 0xFF, 0x48, 0x0F, 0xBF, 0x4E, 0x4E, 0x66, 0x85, 0xC9, 0x0F, 0x88, 0x0F,
    0x01, 0x00, 0x00, 0x48, 0x3B, 0x88, 0xF0, 0x13, 0x00, 0x00, 0x0F, 0x83, 0x02, 0x01, 0x00, 0x00,
    0x48, 0x8D, 0xB8, 0xE8, 0x13, 0x00, 0x00, 0x48, 0x89, 0x4D, 0xA7, 0x48, 0x3B, 0x4F, 0x08, 0x72,
    0x1A, 0x48, 0x8D, 0x45, 0xA7, 0x48, 0x89, 0x7D, 0xDF, 0x48, 0x8D, 0x4D, 0xD7, 0x48, 0x89, 0x45,
    0xD7, 0xE8, 0xF7, 0xDD, 0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x01, 0xCC, 0x4C, 0x69, 0x65, 0xA7, 0x38,
    0x01, 0x00, 0x00, 0x4C, 0x03, 0x27, 0x0F, 0x84, 0xC6, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x45, 0xC7,
    0x45, 0x33, 0xF6, 0x41, 0x8B, 0xFE, 0x48, 0x8D, 0x04, 0x40, 0x4C, 0x8D, 0x2C, 0x00, 0x49, 0x8D,
    0xB5, 0x24, 0x01, 0x00, 0x00, 0x49, 0x03, 0xF4, 0x0F, 0x1F, 0x44, 0x00, 0x00, 0x8B, 0xC7, 0x4D,
    0x8D, 0x7C, 0x24, 0x04, 0x49, 0x03, 0xC5, 0x48, 0xC1, 0xE0, 0x04, 0x4C, 0x03, 0xF8, 0x49, 0x8B,
    0xCF, 0xE8, 0x77, 0xFA, 0xE8, 0xFF, 0x84, 0xC0, 0x0F, 0x84, 0x80, 0x00, 0x00, 0x00, 0x44, 0x38,
    0x36, 0x76, 0x3D, 0x48, 0x8B, 0xCB, 0xE8, 0x92, 0x5B, 0xE0, 0xFF, 0x8B, 0x08, 0x4C, 0x69, 0xC1,
    0xC5, 0x90, 0xC6, 0x6A, 0x8B, 0x48, 0x04, 0x4C, 0x03, 0xC1, 0x44, 0x89, 0x00, 0x49, 0x8B, 0xC8,
    0x48, 0xC1, 0xE9, 0x20, 0x89, 0x48, 0x04, 0xB8, 0x1F, 0x85, 0xEB, 0x51, 0x41, 0xF7, 0xE0, 0xC1,
    0xEA, 0x05, 0x6B, 0xC2, 0x64, 0x44, 0x2B, 0xC0, 0x0F, 0xB6, 0x06, 0x44, 0x3B, 0xC0, 0x7D, 0x30,
    0x4C, 0x89, 0x74, 0x24, 0x48, 0x45, 0x33, 0xC9, 0xC7, 0x44, 0x24, 0x40, 0x40, 0x00, 0x00, 0x00,
    0x4C, 0x8B, 0xC3, 0x44, 0x89, 0x74, 0x24, 0x38, 0x33, 0xD2, 0x4C, 0x89, 0x7C, 0x24, 0x30, 0x33,
    0xC9, 0x44, 0x89, 0x74, 0x24, 0x28, 0x44, 0x89, 0x74, 0x24, 0x20, 0xE8, 0x6D, 0xFF, 0xE8, 0xFF,
    0xFF, 0xC7, 0x48, 0xFF, 0xC6, 0x83, 0xFF, 0x06
};

// spawn info copy 54471C
constexpr std::uint8_t W16[]{
    0x48, 0x8B, 0xCB, 0xE8, 0xAC, 0x72, 0xE0, 0xFF, 0x83, 0xF8, 0x01, 0x75, 0x37, 0x48, 0x8B, 0x43,
    0x10, 0x48, 0x85, 0xC0, 0x74, 0x2E, 0x41, 0x0F, 0x10, 0x06, 0x0F, 0x11, 0x40, 0x78, 0x41, 0x0F,
    0x10, 0x4E, 0x10, 0x0F, 0x11, 0x88, 0x88, 0x00, 0x00, 0x00, 0x41, 0x0F, 0x10, 0x46, 0x20, 0x0F,
    0x11, 0x80, 0x98, 0x00, 0x00, 0x00, 0xF2, 0x41, 0x0F, 0x10, 0x4E, 0x30, 0xF2, 0x0F, 0x11, 0x88,
    0xA8, 0x00, 0x00, 0x00
};

// get-hit mode test 1A2D30
constexpr std::uint8_t W17[]{
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x49, 0x63, 0xD8, 0xE8, 0x72, 0x48, 0xEF, 0xFF, 0x48, 0x8B,
    0xD0, 0x48, 0x85, 0xC0, 0x75, 0x1B, 0x48, 0x8D, 0x4C, 0x24, 0x48, 0x88, 0x44, 0x24, 0x48, 0xE8,
    0x9C, 0x68, 0xF5, 0xFF, 0x84, 0xC0, 0x74, 0x01, 0xCC, 0x33, 0xC0, 0x48, 0x83, 0xC4, 0x20, 0x5B,
    0xC3
};

// attack stats data context and difficulty 588E6B
constexpr std::uint8_t W18[]{
    0x48, 0x8B, 0xCE, 0x4C, 0x89, 0xBC, 0x24, 0xE0, 0x00, 0x00, 0x00, 0xE8, 0x65, 0x12, 0xDC, 0xFF,
    0x0F, 0xB6, 0xC8, 0x48, 0x8B, 0xD3, 0xE8, 0x0A, 0xA7, 0xD6, 0xFF, 0x48, 0x8B, 0xCE, 0xE8, 0x72,
    0x70, 0xF0, 0xFF, 0x4C, 0x8B, 0xC6, 0x48, 0x8D, 0x4D, 0xC7, 0x48, 0x8B, 0xD0, 0xE8, 0xA3, 0x70,
    0xF0, 0xFF, 0xF2, 0x0F, 0x10, 0x00, 0x8B, 0x40, 0x08, 0xF2, 0x0F, 0x11, 0x45, 0xA7, 0x44, 0x0F,
    0xB6, 0x65, 0xA7, 0x89, 0x45, 0xAF
};

// restore bind call 51E131
constexpr std::uint8_t W19[]{
    0x0F, 0xB6, 0x84, 0x24, 0x80, 0x00, 0x00, 0x00, 0x44, 0x8B, 0xCB, 0x88, 0x44, 0x24, 0x30, 0x4C,
    0x8B, 0xC7, 0x8B, 0x44, 0x24, 0x78, 0x48, 0x8B, 0xD6, 0x89, 0x44, 0x24, 0x28, 0x48, 0x8B, 0xCD,
    0x8B, 0x44, 0x24, 0x70, 0x89, 0x44, 0x24, 0x20, 0xE8, 0x32, 0x0E, 0x00, 0x00
};

// new bind call 520F13
constexpr std::uint8_t W20[]{
    0xC6, 0x44, 0x24, 0x30, 0x01, 0x45, 0x8B, 0xCF, 0x89, 0x44, 0x24, 0x28, 0x4C, 0x8B, 0xC7, 0x48,
    0x8B, 0xD5, 0x44, 0x89, 0x64, 0x24, 0x20, 0x48, 0x8B, 0xCE, 0xE8, 0x5E, 0xE0, 0xFF, 0xFF
};

// bind conversion entry 51EF90
constexpr std::uint8_t W21[]{
    0x40, 0x53, 0x56, 0x41, 0x54, 0x41, 0x57, 0x48, 0x81, 0xEC, 0xD8, 0x02, 0x00, 0x00, 0x48, 0x8B,
    0x05, 0x23, 0xC3, 0x4A, 0x02, 0x48, 0x33, 0xC4, 0x48, 0x89, 0x84, 0x24, 0xA0, 0x02, 0x00, 0x00
};

// bind level clamp 51F345
constexpr std::uint8_t W22[]{
    0x45, 0x33, 0xC0, 0x48, 0x8B, 0xCB, 0x41, 0x8D, 0x50, 0x0C, 0xE8, 0xCC, 0x5C, 0xDD, 0xFF, 0x45,
    0x33, 0xC0, 0x49, 0x8B, 0xCF, 0x8B, 0xF8, 0x41, 0x8D, 0x50, 0x0C, 0xE8, 0xBB, 0x5C, 0xDD, 0xFF,
    0x85, 0xFF, 0x74, 0x16, 0x3B, 0xC7, 0x7D, 0x12, 0x45, 0x33, 0xC9, 0x44, 0x8B, 0xC0, 0x48, 0x8B,
    0xCB, 0x41, 0x8D, 0x51, 0x0C, 0xE8, 0x91, 0x89, 0xDD, 0xFF
};

// bind regeneration 51F442
constexpr std::uint8_t W23[]{
    0x48, 0x8B, 0x43, 0x10, 0x48, 0x85, 0xC0, 0x74, 0x31, 0x48, 0x8B, 0x08, 0x48, 0x85, 0xC9, 0x74,
    0x29, 0x8B, 0xB9, 0xF4, 0x01, 0x00, 0x00, 0x48, 0x8B, 0xCB, 0xE8, 0xBF, 0x58, 0xDD, 0xFF, 0x8B,
    0xD7, 0x8B, 0xC8, 0xE8, 0x06, 0x3A, 0x02, 0x00, 0x45, 0x33, 0xC9, 0x44, 0x8B, 0xC0, 0x48, 0x8B,
    0xCB, 0x41, 0x8D, 0x51, 0x4A, 0xE8, 0x94, 0x88, 0xDD, 0xFF
};

// hit recovery life threshold 44FBE2
constexpr std::uint8_t W24[]{
    0x81, 0xBF, 0x34, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x0F, 0x8C, 0x09, 0x01, 0x00, 0x00,
    0x48, 0x8B, 0xCE, 0xE8, 0x26, 0x51, 0xEA, 0xFF, 0x8D, 0x4B, 0xFF, 0x8B, 0xE8, 0x83, 0xF9, 0x0B,
    0x77, 0x2B, 0x48, 0x8D, 0x05, 0xF5, 0x03, 0xBB, 0xFF, 0x48, 0x63, 0xC9, 0x8B, 0x94, 0x88, 0x18,
    0xFD, 0x44, 0x00, 0x48, 0x03, 0xD0, 0xFF, 0xE2, 0xBB, 0x08, 0x00, 0x00, 0x00, 0xEB, 0x13, 0xBB,
    0x20, 0x00, 0x00, 0x00, 0xEB, 0x0C, 0xBB, 0x40, 0x00, 0x00, 0x00, 0xEB, 0x05, 0xBB, 0x10, 0x00,
    0x00, 0x00, 0x44, 0x8B, 0x87, 0x34, 0x01, 0x00, 0x00, 0x8B, 0xC5, 0x99, 0xF7, 0xFB, 0x44, 0x3B,
    0xC0, 0x0F, 0x8C, 0xB2, 0x00, 0x00, 0x00
};

// hit recovery get-hit mode test 44FCB7
constexpr std::uint8_t W25[]{
    0x48, 0x8B, 0xCE, 0xE8, 0x11, 0xBD, 0xEF, 0xFF, 0x83, 0xF8, 0x01, 0x75, 0x33, 0x41, 0xB8, 0x47,
    0x09, 0x00, 0x00, 0x48, 0x8D, 0x15, 0x5F, 0xAC, 0x8C, 0x01, 0x48, 0x8B, 0xCE, 0xE8, 0x87, 0x9B,
    0xEF, 0xFF, 0x48, 0x8B, 0xCE, 0x8B, 0xD8, 0xE8, 0xFD, 0xA3, 0xEF, 0xFF, 0x41, 0xB8, 0x03, 0x00,
    0x00, 0x00, 0x8B, 0xD3, 0x0F, 0xB6, 0xC8, 0xE8, 0x3D, 0x30, 0xD5, 0xFF
};

// attack stats record read 588DFA
constexpr std::uint8_t W26[]{
    0x48, 0x8B, 0x46, 0x10, 0x48, 0x85, 0xC0, 0x0F, 0x84, 0x62, 0x08, 0x00, 0x00, 0x4C, 0x89, 0xAC,
    0x24, 0xF0, 0x00, 0x00, 0x00, 0x4C, 0x8B, 0x28, 0x4D, 0x85, 0xED, 0x0F, 0x84, 0x46, 0x08, 0x00,
    0x00
};

// attack stats S1 588F72
constexpr std::uint8_t W27[]{
    0xE8, 0xA9, 0xC0, 0xD6, 0xFF, 0x41, 0xB8, 0xA7, 0x00, 0x00, 0x00, 0x48, 0x8D, 0x15, 0x7C, 0x5D,
    0x7B, 0x01, 0x48, 0x8B, 0xCE, 0x8B, 0xF8, 0xE8, 0xD2, 0x08, 0xDC, 0xFF, 0x48, 0x8B, 0xCE, 0x8B,
    0xD8, 0xE8, 0x48, 0x11, 0xDC, 0xFF, 0x48, 0x8D, 0x4D, 0xE7, 0xC7, 0x44, 0x24, 0x38, 0x00, 0x00,
    0x00, 0x00, 0x48, 0x89, 0x4C, 0x24, 0x30, 0x45, 0x8B, 0xCC, 0xC7, 0x44, 0x24, 0x28, 0x20, 0x00,
    0x00, 0x00, 0x0F, 0xB6, 0xC8, 0x45, 0x8B, 0xC6, 0x89, 0x7C, 0x24, 0x20, 0x8B, 0xD3, 0xE8, 0xFB,
    0x96, 0xE1, 0xFF
};

// attack stats A2 588FFC
constexpr std::uint8_t W28[]{
    0xE8, 0x1F, 0xC0, 0xD6, 0xFF, 0x41, 0xB8, 0x9B, 0x00, 0x00, 0x00, 0x48, 0x8D, 0x15, 0xF2, 0x5C,
    0x7B, 0x01, 0x48, 0x8B, 0xCE, 0x8B, 0xF8, 0xE8, 0x48, 0x08, 0xDC, 0xFF, 0x48, 0x8B, 0xCE, 0x8B,
    0xD8, 0xE8, 0xBE, 0x10, 0xDC, 0xFF, 0x48, 0x8D, 0x4D, 0xE7, 0xC7, 0x44, 0x24, 0x38, 0x00, 0x00,
    0x00, 0x00, 0x48, 0x89, 0x4C, 0x24, 0x30, 0x45, 0x8B, 0xCC, 0xC7, 0x44, 0x24, 0x28, 0x10, 0x00,
    0x00, 0x00, 0x0F, 0xB6, 0xC8, 0x45, 0x8B, 0xC6, 0x89, 0x7C, 0x24, 0x20, 0x8B, 0xD3, 0xE8, 0x71,
    0x96, 0xE1, 0xFF
};

// attack stats A1 589086
constexpr std::uint8_t W29[]{
    0xE8, 0x95, 0xBF, 0xD6, 0xFF, 0x41, 0xB8, 0x90, 0x00, 0x00, 0x00, 0x48, 0x8D, 0x15, 0x68, 0x5C,
    0x7B, 0x01, 0x48, 0x8B, 0xCE, 0x8B, 0xF8, 0xE8, 0xBE, 0x07, 0xDC, 0xFF, 0x48, 0x8B, 0xCE, 0x8B,
    0xD8, 0xE8, 0x34, 0x10, 0xDC, 0xFF, 0x48, 0x8D, 0x4D, 0xE7, 0xC7, 0x44, 0x24, 0x38, 0x00, 0x00,
    0x00, 0x00, 0x48, 0x89, 0x4C, 0x24, 0x30, 0x45, 0x8B, 0xCC, 0xC7, 0x44, 0x24, 0x28, 0x08, 0x00,
    0x00, 0x00, 0x0F, 0xB6, 0xC8, 0x45, 0x8B, 0xC6, 0x89, 0x7C, 0x24, 0x20, 0x8B, 0xD3, 0xE8, 0xE7,
    0x95, 0xE1, 0xFF
};

// attack stats Align read 5890FC
constexpr std::uint8_t W30[]{
    0x41, 0x80, 0xBD, 0x87, 0x00, 0x00, 0x00, 0x01
};

// attack stats El records 5891F5
constexpr std::uint8_t W31[]{
    0x4C, 0x63, 0x65, 0x8B, 0x49, 0x8D, 0x9D, 0x53, 0x01, 0x00, 0x00, 0x48, 0x89, 0x5D, 0x9F, 0x41,
    0xBE, 0x40, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x45, 0x9F, 0x44, 0x89, 0x75, 0x97, 0x49, 0x8D, 0x9C,
    0x24, 0x56, 0x01, 0x00, 0x00, 0x48, 0xC7, 0x45, 0xC7, 0x03, 0x00, 0x00, 0x00, 0x49, 0x03, 0xDD,
    0x48, 0x89, 0x5D, 0xBF
};

// attack stats El damage 5892B3
constexpr std::uint8_t W32[]{
    0xE8, 0x68, 0xBD, 0xD6, 0xFF, 0x41, 0xB8, 0xD0, 0x00, 0x00, 0x00, 0x48, 0x8D, 0x15, 0x3B, 0x5A,
    0x7B, 0x01, 0x48, 0x8B, 0xCE, 0x8B, 0xF8, 0xE8, 0x91, 0x05, 0xDC, 0xFF, 0x48, 0x8B, 0xCE, 0x8B,
    0xD8, 0xE8, 0x07, 0x0E, 0xDC, 0xFF, 0x44, 0x8B, 0x45, 0x9B, 0x33, 0xC9, 0x89, 0x4C, 0x24, 0x38,
    0x45, 0x8B, 0xCC, 0x48, 0x8D, 0x4D, 0xE7, 0x8B, 0xD3, 0x48, 0x89, 0x4C, 0x24, 0x30, 0x0F, 0xB6,
    0xC8, 0x44, 0x89, 0x74, 0x24, 0x28, 0x89, 0x7C, 0x24, 0x20, 0xE8, 0xBE, 0x93, 0xE1, 0xFF
};

const std::array<Witness, 19> NativeWitnesses{ {
    { 0x34B9D0, W00, sizeof(W00), "UNITS_GetUnitType 34B9D0" },
    { 0x0976E0, W01, sizeof(W01), "DATATBLS_GetMonStatsTxtRecord 0976E0" },
    { 0x300A90, W02, sizeof(W02), "DATATBLS_GetDataTables 300A90" },
    { 0x097790, W03, sizeof(W03), "DATATBLS_GetSkillsTxtRecord 097790" },
    { 0x3351B0, W04, sizeof(W04), "STATES_CheckState 3351B0" },
    { 0xA121F0, W05, sizeof(W05), "linker name lookup A121F0" },
    { 0xA12420, W06, sizeof(W06), "linker row lookup A12420" },
    { 0x2F48C0, W07, sizeof(W07), "base stat thunk 2F48C0" },
    { 0x2F48C6, W08, sizeof(W08), "base stat thunk padding 2F48C6" },
    { 0x2F48CA, W09, sizeof(W09), "STATLIST_GetUnitBaseStat body 2F48CA" },
    { 0x2F5020, W10, sizeof(W10), "stat thunk 2F5020" },
    { 0x2F5026, W11, sizeof(W11), "stat thunk padding 2F5026" },
    { 0x2F502A, W12, sizeof(W12), "STATLIST_GetUnitStat body 2F502A" },
    { 0x54400A, W13, sizeof(W13), "spawn base resistances and ToBlock 54400A" },
    { 0x544107, W14, sizeof(W14), "spawn level stats and life roll 544107" },
    { 0x5445A3, W15, sizeof(W15), "spawn monprop loop 5445A3" },
    { 0x54471C, W16, sizeof(W16), "spawn info copy 54471C" },
    { 0x1A2D30, W17, sizeof(W17), "get-hit mode test 1A2D30" },
    { 0x588E6B, W18, sizeof(W18), "attack stats data context and difficulty 588E6B" },
} };

const std::array<Witness, 5> BindWitnesses{ {
    { 0x51E131, W19, sizeof(W19), "restore bind call 51E131" },
    { 0x520F13, W20, sizeof(W20), "new bind call 520F13" },
    { 0x51EF90, W21, sizeof(W21), "bind conversion entry 51EF90" },
    { 0x51F345, W22, sizeof(W22), "bind level clamp 51F345" },
    { 0x51F442, W23, sizeof(W23), "bind regeneration 51F442" },
} };

const std::array<Witness, 7> AttackWitnesses{ {
    { 0x588DFA, W26, sizeof(W26), "attack stats record read 588DFA" },
    { 0x588F72, W27, sizeof(W27), "attack stats S1 588F72" },
    { 0x588FFC, W28, sizeof(W28), "attack stats A2 588FFC" },
    { 0x589086, W29, sizeof(W29), "attack stats A1 589086" },
    { 0x5890FC, W30, sizeof(W30), "attack stats Align read 5890FC" },
    { 0x5891F5, W31, sizeof(W31), "attack stats El records 5891F5" },
    { 0x5892B3, W32, sizeof(W32), "attack stats El damage 5892B3" },
} };

const std::array<Witness, 2> HitWitnesses{ {
    { 0x44FBE2, W24, sizeof(W24), "hit recovery life threshold 44FBE2" },
    { 0x44FCB7, W25, sizeof(W25), "hit recovery get-hit mode test 44FCB7" },
} };

// Record stub (588E0F): mov r13,[rax] / sub rsp,28h / mov rcx,rsi / mov rdx,r13 /
// mov rax,RecordFor / call rax / add rsp,28h / mov r13,rax / test r13,r13 / ret
constexpr std::array<std::uint8_t, 36> RecordStub{ {
    0x4C, 0x8B, 0x28, 0x48, 0x83, 0xEC, 0x28, 0x48, 0x89, 0xF1, 0x4C, 0x89, 0xEA, 0x48, 0xB8, 0x88,
    0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, 0xFF, 0xD0, 0x48, 0x83, 0xC4, 0x28, 0x49, 0x89, 0xC5,
    0x4D, 0x85, 0xED, 0xC3
} };
constexpr std::size_t RecordStubTargetSlot = 15;

// Regeneration stub (51F453): mov edi,[rcx+1F4h] / sub rsp,28h / mov rcx,rbx / mov edx,edi /
// mov rax,RegenFor / call rax / add rsp,28h / mov edi,eax / ret
constexpr std::array<std::uint8_t, 34> RegenStub{ {
    0x8B, 0xB9, 0xF4, 0x01, 0x00, 0x00, 0x48, 0x83, 0xEC, 0x28, 0x48, 0x89, 0xD9, 0x89, 0xFA, 0x48,
    0xB8, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, 0xFF, 0xD0, 0x48, 0x83, 0xC4, 0x28, 0x89,
    0xC7, 0xC3
} };
constexpr std::size_t RegenStubTargetSlot = 17;

// ---------------------------------------------------------------------------
// Natives
// ---------------------------------------------------------------------------

using BindFn           = bool (*)(void* game, void* owner, void* demon, std::uint32_t skill, std::int32_t level,
                                  std::int32_t castId, std::uint8_t newBind) noexcept;
using GetUnitTypeFn    = std::int32_t (*)(void* unit) noexcept;
using GetUnitClassIdFn = std::int32_t (*)(void* unit, const char* file, std::int32_t line) noexcept;
using GetDataContextFn = std::uint8_t (*)(void* unit) noexcept;
using GetDataTablesFn  = std::uint8_t* (*)(std::uint64_t context) noexcept;
using GetMonStatsFn    = const std::uint8_t* (*)(std::uint64_t context, std::uint64_t row) noexcept;
using SkillsRecordFn   = const std::uint8_t* (*)(std::uint64_t context, std::uint64_t skill) noexcept;
using LevelStatsFn     = std::uint64_t (*)(std::uint64_t context, std::uint64_t row, std::uint64_t gameType,
                                           std::uint64_t difficulty, std::int32_t level, std::int32_t flags,
                                           std::int32_t* out, std::int32_t extra) noexcept;
using DifficultyInfoFn = std::uint8_t* (*)(std::uint8_t* out, void* game, void* unit) noexcept;
using UnitSeedFn       = std::uint32_t* (*)(void* unit) noexcept;
using ReadStatFn       = std::int32_t (*)(void* unit, std::uint64_t stat, std::uint64_t layer) noexcept;
using SetStatFn        = void (*)(void* unit, std::uint64_t stat, std::int64_t value, std::uint64_t layer) noexcept;
using CheckStateFn     = std::int32_t (*)(void* unit, std::uint64_t state) noexcept;
using PropValidFn      = bool (*)(const void* prop) noexcept;
using AssignPropertyFn = std::int64_t (*)(void* a, void* b, void* unit, void* d, std::int32_t e, std::int32_t f,
                                          const void* prop, std::int32_t h, std::int32_t flags, void* j) noexcept;
using LinkerByNameFn   = std::int32_t (*)(void* table, const char* name, std::int32_t warn) noexcept;
using LinkerByIndexFn  = const char* (*)(void* table, std::int64_t row, std::int32_t mode) noexcept;
using MonStats2Fn      = const std::uint8_t* (*)(std::uint64_t context, std::uint64_t row) noexcept;
using HasModeFn        = std::uint32_t (*)(std::uint64_t context, std::uint64_t row, std::uint64_t mode) noexcept;

const D2RL::PluginContext* Context{};
std::uintptr_t             Base{};
Config                     Settings{};
std::string                LoadedConfigPath{ "embedded defaults" };

BindFn           OriginalBind{};
GetUnitTypeFn    GetUnitType{};
GetUnitClassIdFn GetUnitClassId{};
GetDataContextFn GetDataContext{};
GetDataTablesFn  GetDataTables{};
GetMonStatsFn    GetMonStats{};
SkillsRecordFn   SkillsRecord{};
LevelStatsFn     LevelStats{};
DifficultyInfoFn DifficultyInfo{};
UnitSeedFn       UnitSeed{};
ReadStatFn       ReadStat{};
ReadStatFn       ReadBaseStat{};
SetStatFn        SetStat{};
CheckStateFn     CheckState{};
PropValidFn      PropValid{};
AssignPropertyFn AssignProperty{};
LinkerByNameFn   LinkerIndexByName{};
LinkerByIndexFn  LinkerNameByIndex{};
MonStats2Fn      MonStats2Record{};
HasModeFn        MonsterHasMode{};

std::atomic<bool> NativesReady{};
std::atomic<bool> BindArmed{};
std::atomic<bool> AttackArmed{};
std::atomic<bool> HitArmed{};
std::atomic<bool> ServicePublished{};

std::atomic<std::uint64_t> NewBindsRebased{};
std::atomic<std::uint64_t> RestoresRebased{};
std::atomic<std::uint64_t> BindsSkipped{};
std::atomic<std::uint64_t> RegenSwaps{};
std::atomic<std::uint64_t> AttackClassSwaps{};
std::atomic<std::uint64_t> AttackRecordSwaps{};
std::atomic<std::uint64_t> ServiceQueries{};
std::atomic<std::uint64_t> HitRecoverySkips{};

std::string BindStatus{ "not installed" };
std::string AttackStatus{ "not installed" };
std::string HitStatus{ "not installed" };

template <typename T>
auto At(std::uint64_t rva) noexcept -> T {
    return reinterpret_cast<T>(Base + static_cast<std::uintptr_t>(rva));
}

template <typename T>
auto Read(const void* base, std::size_t offset) noexcept -> T {
    T value{};
    std::memcpy(&value, static_cast<const std::uint8_t*>(base) + offset, sizeof(T));
    return value;
}

// ---------------------------------------------------------------------------
// The pseudo row, per data context
// ---------------------------------------------------------------------------

constexpr std::uint32_t DataContextCount = 4;  // cmp rax,4 in 300A90

struct RowSlot {
    std::atomic<const void*>  tables{};
    std::atomic<const void*>  linker{};
    std::atomic<std::int32_t> row{ -1 };
};
std::array<RowSlot, DataContextCount> Rows{};

SRWLOCK ReportLock = SRWLOCK_INIT;
std::string RowReport{ "not looked up yet" };

void SetRowReport(const std::string& text) noexcept {
    AcquireSRWLockExclusive(&ReportLock);
    RowReport = text;
    ReleaseSRWLockExclusive(&ReportLock);
}

// The monstats.txt row named by pseudo_row, -1 when there is none. Looked up
// through the table's Id linker the first time and again whenever the data
// tables move; the reverse lookup must give the same name back.
auto PseudoRow(std::uint32_t context) noexcept -> std::int32_t {
    if (!NativesReady.load(std::memory_order_acquire) || context >= DataContextCount) return -1;
    std::uint8_t* tables = GetDataTables(context);
    if (tables == nullptr) return -1;
    void* linker = Read<void*>(tables, DataTablesMonStatsLinker);
    if (linker == nullptr) return -1;
    RowSlot& slot = Rows[context];
    if (slot.tables.load(std::memory_order_acquire) == tables && slot.linker.load(std::memory_order_acquire) == linker) {
        return slot.row.load(std::memory_order_acquire);
    }

    std::int32_t       row  = LinkerIndexByName(linker, Settings.pseudoRow.c_str(), 0);
    const char*        back = row >= 0 ? LinkerNameByIndex(linker, row, 0) : nullptr;
    const std::uint64_t count = Read<std::uint64_t>(tables, DataTablesMonStatsCount);
    const bool found = row >= 0 && static_cast<std::uint64_t>(row) < count && back != nullptr
        && EqualsIgnoreCase(back, Settings.pseudoRow) && GetMonStats(context, static_cast<std::uint64_t>(row)) != nullptr;
    if (!found) row = -1;

    slot.row.store(row, std::memory_order_release);
    slot.linker.store(linker, std::memory_order_release);
    slot.tables.store(tables, std::memory_order_release);

    char text[256]{};
    if (found) {
        std::snprintf(text, sizeof(text), "\"%s\" is monstats.txt row %d (data context %u)", Settings.pseudoRow.c_str(), row,
            context);
        D2RL::LogInfoF(Context, "BoundDemonScaling: pseudo row %s.", text);
    } else {
        std::snprintf(text, sizeof(text), "\"%s\" is not a monstats.txt Id (data context %u); bound demons keep their own stats",
            Settings.pseudoRow.c_str(), context);
        D2RL::LogWarnF(Context, "BoundDemonScaling: pseudo row %s.", text);
    }
    SetRowReport(text);
    return row;
}

// Whether a monstats row's monstats2 row has the get-hit mode. A row without
// a monstats2 row reads as having it, so the native test stays in charge.
auto RowHasGetHit(std::uint32_t context, std::int32_t row) noexcept -> bool {
    if (MonStats2Record(context, static_cast<std::uint64_t>(row)) == nullptr) return true;
    return MonsterHasMode(context, static_cast<std::uint64_t>(row), ModeGetHit) != 0;
}

// The pseudo row a unit takes its stats from: a monster with the bind_demon
// state while a pseudo row exists. -1 for every other unit.
auto ScaledRow(void* unit) noexcept -> std::int32_t {
    if (unit == nullptr || !NativesReady.load(std::memory_order_acquire) || !Settings.enabled) return -1;
    if (GetUnitType(unit) != UnitTypeMonster) return -1;
    if (CheckState(unit, BindDemonState) == 0) return -1;
    return PseudoRow(GetDataContext(unit));
}

// ---------------------------------------------------------------------------
// The rebase, run by the bind relay before the native bind
// ---------------------------------------------------------------------------

struct BindReport {
    bool         valid{};
    bool         newBind{};
    std::int32_t classId{};
    std::int32_t row{};
    std::int32_t level{};
    std::int32_t difficulty{};
    std::int64_t lifeBefore{};
    std::int64_t lifeAfter{};
    std::int32_t acBefore{}, acAfter{};
    std::int32_t blockBefore{}, blockAfter{};
    std::array<std::int32_t, 6> resistBefore{};
    std::array<std::int32_t, 6> resistAfter{};
    std::int32_t monPropRow{ -1 };
    std::int32_t monPropApplied{};
    bool         pseudoGetHit{};
    bool         noPseudoLife{};
    bool         bound{};
    std::int64_t lifeAfterBind{};
    std::int64_t maxAfterBind{};
};

BindReport LastBind{};
SRWLOCK    LastBindLock = SRWLOCK_INIT;

thread_local void*               BindingDemon{};
thread_local const std::uint8_t* BindingPseudo{};

void FormatBind(const BindReport& r, char* out, std::size_t size) noexcept {
    std::snprintf(out, size,
        "%s, monstats row %d -> pseudo row %d, level %d, difficulty %d: life %lld -> %lld, AC %d -> %d, "
        "ToBlock %d -> %d, resistances %d/%d/%d/%d/%d/%d -> %d/%d/%d/%d/%d/%d (dmg/mag/fire/ltng/cold/pois), "
        "monprop row %d: %d entries applied, hit recovery %s; %s life %lld / %lld%s",
        r.newBind ? "new bind" : "restore", r.classId, r.row, r.level, r.difficulty, static_cast<long long>(r.lifeBefore),
        static_cast<long long>(r.lifeAfter), r.acBefore, r.acAfter, r.blockBefore, r.blockAfter, r.resistBefore[0],
        r.resistBefore[1], r.resistBefore[2], r.resistBefore[3], r.resistBefore[4], r.resistBefore[5], r.resistAfter[0],
        r.resistAfter[1], r.resistAfter[2], r.resistAfter[3], r.resistAfter[4], r.resistAfter[5], r.monPropRow,
        r.monPropApplied, r.pseudoGetHit ? "as its own model (the pseudo row has a get-hit mode)" : "never (the pseudo row has no get-hit mode)",
        !r.bound ? "the game refused the bind, kept" : r.newBind ? "after the bind" : "after the bind (the saved stats are put back next)",
        static_cast<long long>(r.lifeAfterBind), static_cast<long long>(r.maxAfterBind),
        r.noPseudoLife ? "; WARNING: the pseudo row has no MinHP/MaxHP for this difficulty, so it got 1 life" : "");
}

// The pseudo row's monprop, as the spawn applies a monster's own (5445A8..
// 5446BB): stop at the first invalid property; a non-zero chance is rolled
// on the unit's seed (% 100).
auto ApplyMonProp(void* demon, std::uint32_t context, const std::uint8_t* pseudo, std::uint32_t difficulty,
        BindReport& report) noexcept -> void {
    const auto index = Read<std::int16_t>(pseudo, MonStatsMonPropOffset);
    report.monPropRow = index;
    if (index < 0) return;
    std::uint8_t* tables = GetDataTables(context);
    if (tables == nullptr) return;
    const auto  count = Read<std::uint64_t>(tables, DataTablesMonPropCount);
    const auto* rows  = Read<const std::uint8_t*>(tables, DataTablesMonPropRows);
    if (rows == nullptr || static_cast<std::uint64_t>(index) >= count) return;
    const std::uint8_t* row = rows + static_cast<std::size_t>(index) * MonPropRowSize;
    for (std::uint32_t i = 0; i < MonPropPropsPerDiff; ++i) {
        const std::size_t slot = static_cast<std::size_t>(difficulty) * MonPropPropsPerDiff + i;
        const std::uint8_t* prop = row + MonPropPropsOffset + slot * MonPropPropSize;
        if (!PropValid(prop)) break;
        const std::uint8_t chance = row[MonPropChanceOffset + slot];
        if (chance != 0) {
            std::uint32_t* seed = UnitSeed(demon);
            if (seed == nullptr) continue;
            if (StepSeed(seed) % 100U >= chance) continue;
        }
        AssignProperty(nullptr, nullptr, demon, nullptr, 0, 0, prop, 0, AssignPropertyMonsterFlag, nullptr);
        ++report.monPropApplied;
    }
}

auto Rebase(void* game, void* owner, void* demon, std::uint8_t newBind, BindReport& report) noexcept -> const std::uint8_t* {
    auto* monsterData = Read<std::uint8_t*>(demon, UnitMonsterDataOffset);
    if (monsterData == nullptr) return nullptr;
    const auto* own = Read<const std::uint8_t*>(monsterData, 0);
    if (own == nullptr) return nullptr;
    const std::uint32_t context = Read<std::uint8_t>(game, GameDataContextOffset);
    const std::int32_t  row     = PseudoRow(context);
    if (row < 0) return nullptr;
    const std::uint8_t* pseudo = GetMonStats(context, static_cast<std::uint64_t>(row));
    if (pseudo == nullptr) return nullptr;

    // What the spawn used for the demon's own row (MonsterData +78h).
    const std::uint8_t* spawn      = monsterData + MonsterSpawnInfoOffset;
    const std::uint32_t spawnDiff  = std::min<std::uint32_t>(spawn[0], 2U);
    const std::uint64_t spawnType  = (spawn[8] != 0 || Read<std::uint32_t>(spawn, 4) != 0) ? 1U : 0U;
    const std::int32_t  spawnLevel = spawn[0x10];
    const std::int32_t  spawnExtra = Read<std::int32_t>(spawn, 0x14);
    const std::int32_t  ownClass   = Read<std::int32_t>(demon, UnitClassIdOffset);

    // The game now, as 588D50 reads it for every attack.
    std::array<std::uint8_t, 32> difficultyInfo{};
    DifficultyInfo(difficultyInfo.data(), game, demon);
    const std::uint32_t difficulty = std::min<std::uint32_t>(difficultyInfo[0], 2U);
    const std::uint64_t gameType   = (difficultyInfo[8] != 0 || Read<std::uint32_t>(difficultyInfo.data(), 4) != 0) ? 1U : 0U;

    // The level the bind will leave (51F345..51F37A).
    std::int32_t level = ReadStat(demon, StatLevel, 0);
    if (newBind != 0) {
        const std::int32_t ownerLevel = ReadStat(owner, StatLevel, 0);
        if (level != 0 && ownerLevel < level) level = ownerLevel;
    }

    std::array<std::int32_t, 14> pseudoOut{};
    std::array<std::int32_t, 14> ownOut{};
    LevelStats(context, static_cast<std::uint64_t>(row), gameType, difficulty, level, LevelStatsLife | LevelStatsAc,
        pseudoOut.data(), 0);
    LevelStats(context, static_cast<std::uint64_t>(static_cast<std::uint32_t>(ownClass)), spawnType, spawnDiff, spawnLevel,
        LevelStatsAc, ownOut.data(), spawnExtra);

    report.valid      = true;
    report.newBind    = newBind != 0;
    report.classId    = ownClass;
    report.row        = row;
    report.level      = level;
    report.difficulty = static_cast<std::int32_t>(difficulty);

    // Life: the pseudo row's, rolled as the spawn rolls it.
    report.lifeBefore   = static_cast<std::int64_t>(ReadBaseStat(demon, StatMaxHp, 0)) >> 8;
    report.noPseudoLife = pseudoOut[0] <= 0 && pseudoOut[1] <= 0;
    std::int64_t life = 1;
    if (std::uint32_t* seed = UnitSeed(demon); seed != nullptr) {
        life = RollLife(pseudoOut[0], pseudoOut[1], seed);
    } else {
        life = std::clamp<std::int64_t>(pseudoOut[0], 1, 0x7FFFFF);
    }
    report.lifeAfter = life;
    SetStat(demon, StatMaxHp, life << 8, 0);
    SetStat(demon, StatHitpoints, life << 8, 0);

    // AC and ToBlock: the own row's part swapped for the pseudo row's.
    report.acBefore = ReadBaseStat(demon, StatArmorClass, 0);
    report.acAfter  = Rebased(report.acBefore, ownOut[2], pseudoOut[2], 0);
    SetStat(demon, StatArmorClass, report.acAfter, 0);

    report.blockBefore = ReadBaseStat(demon, StatToBlock, 0);
    report.blockAfter  = Rebased(report.blockBefore, own[MonStatsToBlockOffset + spawnDiff], pseudo[MonStatsToBlockOffset + difficulty], 0);
    SetStat(demon, StatToBlock, report.blockAfter, 0);

    for (std::size_t i = 0; i < ResistColumns.size(); ++i) {
        const ResistColumn& column = ResistColumns[i];
        const auto ownValue    = Read<std::int16_t>(own, column.offset + 2U * spawnDiff);
        const auto pseudoValue = Read<std::int16_t>(pseudo, column.offset + 2U * difficulty);
        report.resistBefore[i] = ReadBaseStat(demon, column.stat, 0);
        report.resistAfter[i]  = Rebased(report.resistBefore[i], ownValue, pseudoValue, -0x7FFFFFFFLL - 1);
        SetStat(demon, column.stat, report.resistAfter[i], 0);
    }

    ApplyMonProp(demon, context, pseudo, difficulty, report);
    report.pseudoGetHit = RowHasGetHit(context, row);
    return pseudo;
}

// ---------------------------------------------------------------------------
// "demon-bound" event
// ---------------------------------------------------------------------------
// Published as "demon-bound", version 1, by celestialrayone.bound-demon-scaling,
// on the game thread right after a rebased demon was bound or restored. The
// layout is the contract; a later version may only add fields at the end.
struct DemonBoundEvent {
    std::uint32_t eventSize;
    std::uint32_t unitId;   // the demon's server unit id (monster)
    std::uint32_t newBind;  // 1 a new bind, 0 a restore (the saved stats follow)
    std::uint32_t reserved;
};
static_assert(sizeof(DemonBoundEvent) == 16);

constexpr char          DemonBoundEventName[]  = "demon-bound";
constexpr std::uint32_t DemonBoundEventVersion = 1;
constexpr std::size_t   UnitIdOffset           = 0x08;

const D2RL::PluginCommunicationService* Communication{};
std::atomic<std::uint64_t>              EventsPublished{};

void PublishDemonBound(void* demon, bool newBind) noexcept {
    if (Communication == nullptr) return;
    const DemonBoundEvent event{ sizeof(DemonBoundEvent), Read<std::uint32_t>(demon, UnitIdOffset), newBind ? 1U : 0U, 0 };
    const D2RL::PluginCommunication::PublishEventRequest request{
        .structSize   = D2RL::PluginCommunication::PublishEventRequestSize,
        .flags        = 0U,
        .name         = DemonBoundEventName,
        .eventVersion = DemonBoundEventVersion,
        .dataSize     = static_cast<std::uint32_t>(sizeof(event)),
        .data         = &event,
    };
    if (Communication->publishEvent(Context, &request) == D2RL::PluginCommunication::Result::Success) {
        EventsPublished.fetch_add(1, std::memory_order_relaxed);
    }
}

// ---------------------------------------------------------------------------
// Relays
// ---------------------------------------------------------------------------

struct BindScope {
    ~BindScope() {
        BindingDemon  = nullptr;
        BindingPseudo = nullptr;
    }
};

// Called in place of 51EF90 from 520F2D and 51E159, with its arguments.
auto RelayBind(void* game, void* owner, void* demon, std::uint32_t skill, std::int32_t level, std::int32_t castId,
        std::uint8_t newBind) noexcept -> bool {
    BindScope  scope;
    BindReport report{};
    bool       rebased = false;
    // 51EF90 gives up before touching anything without an owner, a demon or
    // the skill's record; the rebase does the same. The rebase runs first, so
    // the game's own bind code (owner, AI, alignment, bind_demon state, calc1
    // life) never sees the monster's original stats.
    if (BindArmed.load(std::memory_order_acquire) && Settings.enabled && game != nullptr && owner != nullptr
            && demon != nullptr && GetUnitType(demon) == UnitTypeMonster
            && SkillsRecord(Read<std::uint8_t>(game, GameDataContextOffset), skill) != nullptr) {
        if (const std::uint8_t* pseudo = Rebase(game, owner, demon, newBind, report); pseudo != nullptr) {
            BindingDemon  = demon;
            BindingPseudo = pseudo;
            rebased       = true;
        } else {
            BindsSkipped.fetch_add(1, std::memory_order_relaxed);
        }
    }
    const bool bound = OriginalBind(game, owner, demon, skill, level, castId, newBind);
    if (!rebased) return bound;

    // The life it has the moment the game's bind code is done, in the same
    // server frame.
    report.bound         = bound;
    report.lifeAfterBind = static_cast<std::int64_t>(ReadStat(demon, StatHitpoints, 0)) >> 8;
    report.maxAfterBind  = static_cast<std::int64_t>(ReadStat(demon, StatMaxHp, 0)) >> 8;
    (newBind != 0 ? NewBindsRebased : RestoresRebased).fetch_add(1, std::memory_order_relaxed);
    AcquireSRWLockExclusive(&LastBindLock);
    LastBind = report;
    ReleaseSRWLockExclusive(&LastBindLock);
    if (Settings.logBinds || report.noPseudoLife) {
        char text[900]{};
        FormatBind(report, text, sizeof(text));
        if (report.noPseudoLife) {
            D2RL::LogWarnF(Context, "BoundDemonScaling: %s.", text);
        } else {
            D2RL::LogInfoF(Context, "BoundDemonScaling: %s.", text);
        }
    }
    if (bound) PublishDemonBound(demon, newBind != 0);
    return bound;
}

// The regeneration stub at 51F453: rcx = demon, edx = its own DamageRegen.
auto RegenFor(void* demon, std::uint32_t native) noexcept -> std::uint32_t {
    if (demon == nullptr || demon != BindingDemon || BindingPseudo == nullptr) return native;
    RegenSwaps.fetch_add(1, std::memory_order_relaxed);
    return Read<std::uint32_t>(BindingPseudo, MonStatsDamageRegen);
}

// The four class id reads of 588D50.
auto RelayClassId(void* unit, const char* file, std::int32_t line) noexcept -> std::int32_t {
    if (AttackArmed.load(std::memory_order_acquire)) {
        if (const std::int32_t row = ScaledRow(unit); row >= 0) {
            AttackClassSwaps.fetch_add(1, std::memory_order_relaxed);
            return row;
        }
    }
    return GetUnitClassId(unit, file, line);
}

// The record stub at 588E0F: rcx = unit, rdx = its own monstats record.
auto RecordFor(void* unit, const std::uint8_t* native) noexcept -> const std::uint8_t* {
    if (!AttackArmed.load(std::memory_order_acquire)) return native;
    const std::int32_t row = ScaledRow(unit);
    if (row < 0) return native;
    const std::uint8_t* record = GetMonStats(GetDataContext(unit), static_cast<std::uint64_t>(row));
    if (record == nullptr) return native;
    AttackRecordSwaps.fetch_add(1, std::memory_order_relaxed);
    return record;
}

// The class id 44FBA0 gives its get-hit mode test (44FCD4). A bound demon
// whose pseudo row has no get-hit mode gets the pseudo row, so the test finds
// no get-hit mode and skips hit recovery; every other unit, and a bound demon
// whose pseudo row has one, gets its own class id and the native test.
auto RelayHitRecoveryClassId(void* unit, const char* file, std::int32_t line) noexcept -> std::int32_t {
    const std::int32_t own = GetUnitClassId(unit, file, line);
    if (!HitArmed.load(std::memory_order_acquire)) return own;
    const std::int32_t row = ScaledRow(unit);
    if (row < 0 || RowHasGetHit(GetDataContext(unit), row)) return own;
    HitRecoverySkips.fetch_add(1, std::memory_order_relaxed);
    return row;
}

// ---------------------------------------------------------------------------
// "stats-row" service
// ---------------------------------------------------------------------------
// Published as "stats-row", version 1, by celestialrayone.bound-demon-scaling.
// The layout is the contract: a consumer declares the same struct. A later
// version may only add fields at the end.
struct StatsRowService {
    using StatsRowFn = std::int32_t(__cdecl*)(void* unit) noexcept;

    std::uint32_t serviceSize;
    std::uint32_t serviceVersion;
    // Game thread, server unit: the monstats.txt row the unit takes its stats
    // from when it is a bound demon this plugin scales, -1 for every other
    // unit (and while the plugin is off or no pseudo row exists).
    StatsRowFn statsRow;
};
static_assert(sizeof(StatsRowService) == 16);
static_assert(offsetof(StatsRowService, statsRow) == 8);

constexpr char          StatsRowServiceName[]  = "stats-row";
constexpr std::uint32_t StatsRowServiceVersion = 1;

auto __cdecl StatsRow(void* unit) noexcept -> std::int32_t {
    ServiceQueries.fetch_add(1, std::memory_order_relaxed);
    return ScaledRow(unit);
}

const StatsRowService StatsRowTable{
    sizeof(StatsRowService),
    StatsRowServiceVersion,
    &StatsRow,
};

auto PublishStatsRow() noexcept -> bool {
    const D2RL::PluginCommunicationService* communication = nullptr;
    if (Context->QueryService(&communication) != D2RL::ServiceQueryResult::Success
            || !D2RL::HasPluginCommunicationServiceField(communication, D2RL::PluginCommunicationServiceRequiredSize)
            || communication->publishService == nullptr) {
        return false;
    }
    const D2RL::PluginCommunication::PublishServiceRequest request{
        .structSize     = D2RL::PluginCommunication::PublishServiceRequestSize,
        .flags          = 0U,
        .name           = StatsRowServiceName,
        .serviceVersion = StatsRowServiceVersion,
        .tableSize      = static_cast<std::uint32_t>(sizeof(StatsRowTable)),
        .table          = &StatsRowTable,
    };
    return communication->publishService(Context, &request) == D2RL::PluginCommunication::Result::Success;
}

// ---------------------------------------------------------------------------
// Relay page and patches
// ---------------------------------------------------------------------------
// One page within rel32 reach of every site, never freed (a thread may be
// inside a stub at any time):
//   +000h  jmp [RelayBind]        (520F2D, 51E159)
//   +040h  jmp [RelayClassId]     (588F89, 589013, 58909D, 5892CA)
//   +080h  record stub            (588E0F)
//   +0C0h  regen stub             (51F453)
//   +100h  jmp [RelayHitRecoveryClassId] (44FCD4)

constexpr std::size_t RelayPageBytes   = 0x1000;
constexpr std::size_t BindStubOffset   = 0x00;
constexpr std::size_t ClassStubOffset  = 0x40;
constexpr std::size_t RecordStubOffset = 0x80;
constexpr std::size_t RegenStubOffset  = 0xC0;
constexpr std::size_t HitStubOffset    = 0x100;

std::uint8_t* RelayPage{};

auto CanEncodeRel32(std::uintptr_t from, std::uintptr_t to) noexcept -> bool {
    const auto delta = static_cast<std::int64_t>(to) - static_cast<std::int64_t>(from);
    return delta >= static_cast<std::int64_t>(INT32_MIN) && delta <= static_cast<std::int64_t>(INT32_MAX);
}

auto AllocateNear(std::uintptr_t hint, std::size_t size) noexcept -> std::uint8_t* {
    SYSTEM_INFO systemInfo{};
    GetSystemInfo(&systemInfo);
    const auto granularity = static_cast<std::uintptr_t>(systemInfo.dwAllocationGranularity != 0 ? systemInfo.dwAllocationGranularity : 0x10000U);
    const auto aligned = hint & ~(granularity - 1U);
    for (std::uintptr_t delta = granularity; delta < 0x70000000ULL; delta += granularity) {
        const auto candidate = aligned + delta;
        if (!CanEncodeRel32(hint, candidate + size)) break;
        if (auto* memory = VirtualAlloc(reinterpret_cast<void*>(candidate), size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)) {
            return static_cast<std::uint8_t*>(memory);
        }
    }
    return nullptr;
}

void WriteJumpAbsolute(std::uint8_t* at, const void* target) noexcept {
    static constexpr std::uint8_t JumpIndirect[6]{ 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00 };  // jmp [rip+0]
    std::memcpy(at, JumpIndirect, sizeof(JumpIndirect));
    const auto address = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(target));
    std::memcpy(at + sizeof(JumpIndirect), &address, sizeof(address));
}

void WriteStub(std::uint8_t* at, const std::uint8_t* code, std::size_t size, std::size_t slot, const void* target) noexcept {
    std::memcpy(at, code, size);
    const auto address = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(target));
    std::memcpy(at + slot, &address, sizeof(address));
}

auto BuildRelayPage() noexcept -> bool {
    RelayPage = AllocateNear(Base + static_cast<std::uintptr_t>(BindRestoreCallRva), RelayPageBytes);
    if (RelayPage == nullptr) return false;
    std::memset(RelayPage, 0xCC, RelayPageBytes);
    WriteJumpAbsolute(RelayPage + BindStubOffset, reinterpret_cast<const void*>(&RelayBind));
    WriteJumpAbsolute(RelayPage + ClassStubOffset, reinterpret_cast<const void*>(&RelayClassId));
    WriteStub(RelayPage + RecordStubOffset, RecordStub.data(), RecordStub.size(), RecordStubTargetSlot,
        reinterpret_cast<const void*>(&RecordFor));
    WriteStub(RelayPage + RegenStubOffset, RegenStub.data(), RegenStub.size(), RegenStubTargetSlot,
        reinterpret_cast<const void*>(&RegenFor));
    WriteJumpAbsolute(RelayPage + HitStubOffset, reinterpret_cast<const void*>(&RelayHitRecoveryClassId));
    DWORD previous = 0;
    if (!VirtualProtect(RelayPage, RelayPageBytes, PAGE_EXECUTE_READ, &previous)) return false;
    FlushInstructionCache(GetCurrentProcess(), RelayPage, RelayPageBytes);
    return true;
}

struct Site {
    const char*                 name;
    std::uint64_t               rva;
    std::array<std::uint8_t, 6> stock;
    std::uint32_t               size;
    std::size_t                 stubOffset;
    std::array<std::uint8_t, 6> written{};
    bool                        applied{};
};

auto MakeCall(Site& site) noexcept -> bool {
    const auto from = Base + static_cast<std::uintptr_t>(site.rva) + 5U;
    const auto to   = reinterpret_cast<std::uintptr_t>(RelayPage) + site.stubOffset;
    if (!CanEncodeRel32(from, to)) return false;
    const auto rel = static_cast<std::int32_t>(static_cast<std::int64_t>(to) - static_cast<std::int64_t>(from));
    site.written.fill(0x90);
    site.written[0] = 0xE8;
    std::memcpy(site.written.data() + 1, &rel, sizeof(rel));
    return true;
}

void Restore(Site* sites, std::size_t count) noexcept {
    for (std::size_t i = 0; i < count; ++i) {
        Site& site = sites[i];
        if (site.applied && Context->PatchBytes(site.rva, site.written.data(), site.size, site.stock.data(), site.size)) {
            site.applied = false;
        }
    }
}

auto Apply(Site* sites, std::size_t count, std::string& status) noexcept -> bool {
    for (std::size_t i = 0; i < count; ++i) {
        Site& site = sites[i];
        if (!MakeCall(site)) {
            status = std::string("the relay page is out of reach of ") + site.name;
            Restore(sites, count);
            return false;
        }
        if (!Context->PatchBytes(site.rva, site.stock.data(), site.size, site.written.data(), site.size)) {
            status = std::string("D2RLoader refused ") + site.name + " (different bytes, or another plugin owns them)";
            Restore(sites, count);
            return false;
        }
        site.applied = true;
        if (std::memcmp(reinterpret_cast<const void*>(Base + static_cast<std::uintptr_t>(site.rva)), site.written.data(), site.size) != 0) {
            status = std::string(site.name) + " does not hold the written bytes";
            Restore(sites, count);
            return false;
        }
    }
    return true;
}

std::array<Site, 3> BindSites{ {
    { "the restore's bind call 51E159", BindRestoreCallRva, { 0xE8, 0x32, 0x0E, 0x00, 0x00 }, 5, BindStubOffset },
    { "the new bind's call 520F2D", BindNewCallRva, { 0xE8, 0x5E, 0xE0, 0xFF, 0xFF }, 5, BindStubOffset },
    { "the regeneration read 51F453", RegenReadRva, { 0x8B, 0xB9, 0xF4, 0x01, 0x00, 0x00 }, 6, RegenStubOffset },
} };

std::array<Site, 1> HitSites{ {
    { "the hit recovery class id call 44FCD4", HitRecoveryClassIdCallRva, { 0xE8, 0x87, 0x9B, 0xEF, 0xFF }, 5, HitStubOffset },
} };

std::array<Site, 5> AttackSites{ {
    { "the record read 588E0F", AttackRecordReadRva, { 0x4C, 0x8B, 0x28, 0x4D, 0x85, 0xED }, 6, RecordStubOffset },
    { "the S1 class id call 588F89", AttackClassIdCallRvas[0], { 0xE8, 0xD2, 0x08, 0xDC, 0xFF }, 5, ClassStubOffset },
    { "the A2 class id call 589013", AttackClassIdCallRvas[1], { 0xE8, 0x48, 0x08, 0xDC, 0xFF }, 5, ClassStubOffset },
    { "the A1 class id call 58909D", AttackClassIdCallRvas[2], { 0xE8, 0xBE, 0x07, 0xDC, 0xFF }, 5, ClassStubOffset },
    { "the elemental class id call 5892CA", AttackClassIdCallRvas[3], { 0xE8, 0x91, 0x05, 0xDC, 0xFF }, 5, ClassStubOffset },
} };

auto VerifyAll(const Witness* witnesses, std::size_t count, std::string& status) noexcept -> bool {
    for (std::size_t i = 0; i < count; ++i) {
        const Witness& witness = witnesses[i];
        if (!Context->CheckExpectedBytes(witness.rva, witness.bytes, witness.size)) {
            char text[256]{};
            std::snprintf(text, sizeof(text), "%s at RVA 0x%llX does not hold the expected bytes", witness.name,
                static_cast<unsigned long long>(witness.rva));
            status = text;
            return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Config
// ---------------------------------------------------------------------------

auto LoadConfig() noexcept -> void {
    Settings = Config{};
    if (!Context->EnsureConfig(DefaultConfig)) {
        Context->LogWarn("BoundDemonScaling: the TOML could not be created; embedded defaults are in use.");
        return;
    }
    std::string   text(16384, '\0');
    std::uint32_t required = 0;
    if (!Context->ReadConfig(text.data(), static_cast<std::uint32_t>(text.size()), &required)) {
        if (required <= text.size()) {
            Context->LogWarn("BoundDemonScaling: the TOML could not be read; embedded defaults are in use.");
            return;
        }
        text.assign(required, '\0');
        if (!Context->ReadConfig(text.data(), required, &required)) {
            Context->LogWarn("BoundDemonScaling: the TOML could not be read; embedded defaults are in use.");
            return;
        }
    }
    text.resize(std::strlen(text.c_str()));
    ParseConfigText(text, Settings, [](std::size_t line, const char* problem) {
        D2RL::LogWarnF(Context, "BoundDemonScaling: TOML line %zu: %s.", line, problem);
    });
    LoadedConfigPath = "<plugin config>";
}

// ---------------------------------------------------------------------------
// Console
// ---------------------------------------------------------------------------

auto Status(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command, void*) noexcept -> D2RL::ConsoleCommandResult {
    if (command == nullptr || command->plugin == nullptr) return D2RL::ConsoleCommandResult::Failed;
    std::string rows;
    AcquireSRWLockShared(&ReportLock);
    rows = RowReport;
    ReleaseSRWLockShared(&ReportLock);

    char text[2048]{};
    std::snprintf(text, sizeof(text),
        "Bound Demon Scaling %s: %s; pseudo_row \"%s\": %s. Bind rebase: %s. Attacks: %s. Hit recovery: %s. "
        "stats-row service: %s; demon-bound events %llu. Rebased: %llu new binds, %llu restores (%llu skipped); "
        "regeneration swaps %llu; attack class id swaps %llu, record swaps %llu; hit recovery skipped %llu times; "
        "service queries %llu.",
        PluginVersion, Settings.enabled ? "enabled" : "disabled", Settings.pseudoRow.c_str(), rows.c_str(),
        BindStatus.c_str(), AttackStatus.c_str(), HitStatus.c_str(), ServicePublished.load() ? "published" : "not published",
        static_cast<unsigned long long>(EventsPublished.load()),
        static_cast<unsigned long long>(NewBindsRebased.load()), static_cast<unsigned long long>(RestoresRebased.load()),
        static_cast<unsigned long long>(BindsSkipped.load()), static_cast<unsigned long long>(RegenSwaps.load()),
        static_cast<unsigned long long>(AttackClassSwaps.load()), static_cast<unsigned long long>(AttackRecordSwaps.load()),
        static_cast<unsigned long long>(HitRecoverySkips.load()), static_cast<unsigned long long>(ServiceQueries.load()));
    command->plugin->WriteConsoleMessage(text);

    BindReport last{};
    AcquireSRWLockShared(&LastBindLock);
    last = LastBind;
    ReleaseSRWLockShared(&LastBindLock);
    if (last.valid) {
        char line[900]{};
        FormatBind(last, line, sizeof(line));
        char message[960]{};
        std::snprintf(message, sizeof(message), "Last demon: %s.", line);
        command->plugin->WriteConsoleMessage(message);
    } else {
        command->plugin->WriteConsoleMessage("Last demon: none bound or restored since the game started.");
    }
    return D2RL::ConsoleCommandResult::Handled;
}

// ---------------------------------------------------------------------------
// Load
// ---------------------------------------------------------------------------

void BindNatives() noexcept {
    OriginalBind      = At<BindFn>(BindConversionRva);
    GetUnitType       = At<GetUnitTypeFn>(GetUnitTypeRva);
    GetUnitClassId    = At<GetUnitClassIdFn>(GetUnitClassIdRva);
    GetDataContext    = At<GetDataContextFn>(GetDataContextRva);
    GetDataTables     = At<GetDataTablesFn>(GetDataTablesRva);
    GetMonStats       = At<GetMonStatsFn>(GetMonStatsRva);
    SkillsRecord      = At<SkillsRecordFn>(SkillsRecordRva);
    LevelStats        = At<LevelStatsFn>(LevelStatsRva);
    DifficultyInfo    = At<DifficultyInfoFn>(DifficultyInfoRva);
    UnitSeed          = At<UnitSeedFn>(UnitSeedRva);
    ReadStat          = At<ReadStatFn>(ReadStatRva);
    ReadBaseStat      = At<ReadStatFn>(ReadBaseStatRva);
    SetStat           = At<SetStatFn>(SetStatRva);
    CheckState        = At<CheckStateFn>(CheckStateRva);
    PropValid         = At<PropValidFn>(PropValidRva);
    AssignProperty    = At<AssignPropertyFn>(AssignPropertyRva);
    LinkerIndexByName = At<LinkerByNameFn>(LinkerIndexByNameRva);
    LinkerNameByIndex = At<LinkerByIndexFn>(LinkerNameByIndexRva);
    MonStats2Record   = At<MonStats2Fn>(MonStats2RecordRva);
    MonsterHasMode    = At<HasModeFn>(MonsterHasModeRva);
}

constexpr D2RL::PluginInfo Info{
    .infoSize    = D2RL::PluginInfoSize,
    .abiVersion  = D2RL_PLUGIN_ABI_VERSION,
    .id          = PluginIdText,
    .name        = "Bound Demon Scaling",
    .version     = PluginVersion,
    .author      = "CelestialRayOne",
    .description = "Bound demons take their life, resistances, defense, block, damage, regeneration and monprop from one "
                   "monstats.txt row, like a summon.",
    .flags       = D2RL::PluginFlags::Server | D2RL::PluginFlags::NativeHooks,
    .reserved    = {},
};

}  // namespace

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* {
    return &Info;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
    if (!D2RL::HasContext(context) || context->abiVersion != D2RL_PLUGIN_ABI_VERSION) return false;
    Context = context;
    Base    = context->exeBase;
    if (Base == 0) return false;
    LoadConfig();

    if (!context->RegisterConsoleCommand("bounddemonscaling", Status,
            "Show Bound Demon Scaling: installed parts, the pseudo row, counters and the last demon it changed.")) {
        context->LogWarn("BoundDemonScaling: the bounddemonscaling console command was not registered.");
    }
    if (!Settings.enabled) {
        BindStatus   = "off (enabled = false)";
        AttackStatus = "off (enabled = false)";
        HitStatus    = "off (enabled = false)";
        context->LogInfo("BoundDemonScaling: loaded disabled; nothing was patched.");
        return true;
    }

    std::string status;
    if (!VerifyAll(NativeWitnesses.data(), NativeWitnesses.size(), status)) {
        BindStatus   = "off: " + status;
        AttackStatus = BindStatus;
        HitStatus    = BindStatus;
        SetRowReport("not looked up, the plugin is off (see Bind rebase)");
        D2RL::LogErrorF(context, "BoundDemonScaling: %s; nothing was patched.", status.c_str());
        return true;
    }
    BindNatives();
    NativesReady.store(true, std::memory_order_release);

    if (!BuildRelayPage()) {
        BindStatus   = "off: no memory for the relay page near the game image";
        AttackStatus = BindStatus;
        HitStatus    = BindStatus;
        context->LogError("BoundDemonScaling: no memory for the relay page near the game image; nothing was patched.");
        return true;
    }

    // Armed before the bytes change: a relay can be entered at once.
    if (VerifyAll(BindWitnesses.data(), BindWitnesses.size(), status)) {
        BindArmed.store(true, std::memory_order_release);
        if (Apply(BindSites.data(), BindSites.size(), status)) {
            BindStatus = "installed (bind calls 520F2D and 51E159, regeneration 51F453)";
        } else {
            BindArmed.store(false, std::memory_order_release);
            BindStatus = "off: " + status;
        }
    } else {
        BindStatus = "off: " + status;
    }
    if (VerifyAll(AttackWitnesses.data(), AttackWitnesses.size(), status)) {
        AttackArmed.store(true, std::memory_order_release);
        if (Apply(AttackSites.data(), AttackSites.size(), status)) {
            AttackStatus = "installed (588D50: four class id calls and the record read)";
        } else {
            AttackArmed.store(false, std::memory_order_release);
            AttackStatus = "off: " + status;
        }
    } else {
        AttackStatus = "off: " + status;
    }
    if (VerifyAll(HitWitnesses.data(), HitWitnesses.size(), status)) {
        HitArmed.store(true, std::memory_order_release);
        if (Apply(HitSites.data(), HitSites.size(), status)) {
            HitStatus = "installed (44FBA0's get-hit mode test, 44FCD4)";
        } else {
            HitArmed.store(false, std::memory_order_release);
            HitStatus = "off: " + status;
        }
    } else {
        HitStatus = "off: " + status;
    }
    ServicePublished.store(PublishStatsRow(), std::memory_order_release);
    {
        const D2RL::PluginCommunicationService* communication = nullptr;
        constexpr auto PublishEventEnd = static_cast<std::uint32_t>(
            offsetof(D2RL::PluginCommunicationService, publishEvent) + sizeof(D2RL::PluginCommunication::PublishEventFn));
        if (context->QueryService(&communication) == D2RL::ServiceQueryResult::Success
                && D2RL::HasPluginCommunicationServiceField(communication, PublishEventEnd)
                && communication->publishEvent != nullptr) {
            Communication = communication;
        }
    }

    D2RL::LogInfoF(context,
        "BoundDemonScaling %s: pseudo_row \"%s\"; bind rebase %s; attacks %s; hit recovery %s; stats-row service %s; "
        "log_binds %s; config %s.",
        PluginVersion, Settings.pseudoRow.c_str(), BindStatus.c_str(), AttackStatus.c_str(), HitStatus.c_str(),
        ServicePublished.load() ? "published" : "not published (Summon Resist Cap cannot see bound demons)",
        Settings.logBinds ? "on" : "off", LoadedConfigPath.c_str());
    return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
    // The relay page stays mapped: a thread may still be inside a stub.
    // Disarmed, every relay and stub reproduces the native behaviour.
    BindArmed.store(false, std::memory_order_release);
    AttackArmed.store(false, std::memory_order_release);
    HitArmed.store(false, std::memory_order_release);
    if (Context != nullptr) {
        Restore(BindSites.data(), BindSites.size());
        Restore(AttackSites.data(), AttackSites.size());
        Restore(HitSites.data(), HitSites.size());
    }
}
