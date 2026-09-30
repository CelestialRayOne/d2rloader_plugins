// Ally Ignore List
//
// Allied units never pick the listed monster types (monstats.txt hcIdx) as a
// target when they look for something to attack: hirelings, necromancer
// summons, druid summons, assassin traps and shadows, Warlock demons
// (GenericPet), converted monsters and every other unit on the player's side.
// Nothing else about those monsters changes. Missiles still collide with them
// and hit them, the player can still attack them, and their corpses stay usable.
//
// Written for D2R 3.3 on D2RLoader 1.3.1. Every address and byte below was read
// out of the live D2RLoader.exe process image (md5 8af7a47ce9077de0378c69af2930854d)
// and disassembled before being relied on.
//
// ---------------------------------------------------------------------------
// How an AI looks for a target
// ---------------------------------------------------------------------------
//   Every AI candidate search goes through the AI unit enumerator 0x596340. It
//   walks the units of the nearby rooms and asks a per-candidate test whether
//   the unit is a valid target. The tests an allied AI can reach are:
//
//     0x597840  valid-target test of the wide search (enumerator table
//               entries 4 and 9). The main target pick 0x595750 runs it for
//               every non-evil unit: the category 1, 2, 3, 5 and 6 AI tick
//               target, Shadow Warrior, Raven, Hydra, Druid spirits, wolves
//               and bear, converted monsters, and the necro pet "assist my
//               owner" pick.
//                 cmp type > 1, IsDead 0x34C2C0, CanBeAttacked 0x34F5A0,
//                 then IsHostile 0x48E460
//     0x597710  valid-target test of the nearest-enemy search 0x595F80
//               (entries 5 and 6): hirelings, necro pets, assassin traps,
//               Poison Creeper. Also called straight by 0x5956D0, the Raven's
//               check of the target its player is attacking.
//     0x5D1360  Shadow Master's own search callback (enumerator entry 1).
//     0x5D4F30  GenericPet's own search callback (enumerator entry 1). The
//               Warlock demons pick every target through it, including the
//               target they remember between ticks.
//
//   Each test is hooked at its entry. A living monster whose class id is in
//   the list gets "not a target" back, exactly as if it were out of reach, so
//   the search moves on to the next candidate.
//
// ---------------------------------------------------------------------------
// What is deliberately left alone
// ---------------------------------------------------------------------------
//   Missile collision. The missile's own unit test 0x467AA0 calls IsValidTarget
//   0x34FD10, CanBeAttacked 0x34F5A0 and IsHostile 0x48E460 itself. None of
//   those is hooked, so missiles collide with and hit a listed monster exactly
//   as before. Damage, auras and the player's own attacks never pass through
//   the four tests either.
//
//   Corpses. Only living units are skipped. The GenericPet test also collects
//   corpses for corpse skills, and the wolf and vine corpse finder 0x584CE0 is
//   a separate path, so corpse use stays vanilla.
//
//   Copying the player's target. Shadow Masters read their player's current
//   target at 0x5CDBA8, and the skill that warps pets and sends them onto the
//   caster's target (skill function 0x521EC0, pet callback 0x520930) writes
//   that target into each pet's forced-target slot. Both start from a unit the
//   player targeted himself, so a monster the player cannot select never
//   reaches them. Shadow Masters also switch from a normal (non boss) minion
//   to the minion's owner at 0x5CDD9E, which only matters for a listed monster
//   that owns minions.
//
// ---------------------------------------------------------------------------
// Unit fields read here
// ---------------------------------------------------------------------------
//   +0x00 unit type, 1 = monster. 0x3501E0 compares [unit] with 1.
//   +0x04 class id, the monstats.txt hcIdx. 0x349860 returns [rcx+4].
//   Dead: the game's own IsDead 0x34C2C0 (flag 0x10000 at +0x124 and more).

#include <D2RLPlugin/api.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

namespace CelestialRayOne::AllyIgnoreList {
namespace {

// ---------------------------------------------------------------------------
// Addresses (RVAs into D2RLoader.exe)
// ---------------------------------------------------------------------------

constexpr std::uint64_t WideSearchTestRva     = 0x597840;
constexpr std::uint64_t NearestEnemyTestRva   = 0x597710;
constexpr std::uint64_t ShadowMasterSearchRva = 0x5D1360;
constexpr std::uint64_t GenericPetSearchRva   = 0x5D4F30;
constexpr std::uint64_t UnitIsDeadRva         = 0x34C2C0;

// Each hook steals only whole instructions: no branch and no RIP-relative
// operand inside any of these prologues.
//
// 0x597840  mov [rsp+8],rbx / mov [rsp+10h],rsi / push rdi / sub rsp,20h /
//           mov rsi,rcx / mov rbx,r8 / mov rcx,r8 / mov rdi,rdx
constexpr std::uint8_t WideSearchTestEntry[]{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC,
    0x20, 0x48, 0x8B, 0xF1, 0x49, 0x8B, 0xD8, 0x49, 0x8B, 0xC8, 0x48, 0x8B, 0xFA };

// 0x597710  mov [rsp+8],rbx / mov [rsp+10h],rsi / push rdi / sub rsp,20h /
//           mov rsi,rcx / mov rbx,r8 / mov rcx,rdx / mov rdi,rdx
constexpr std::uint8_t NearestEnemyTestEntry[]{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC,
    0x20, 0x48, 0x8B, 0xF1, 0x49, 0x8B, 0xD8, 0x48, 0x8B, 0xCA, 0x48, 0x8B, 0xFA };

// 0x5D1360  push rbx / push rdi / push r13 / push r15 / sub rsp,28h /
//           mov rbx,r9 / mov rdi,r8 / mov r15,rdx / mov r13,rcx
constexpr std::uint8_t ShadowMasterSearchEntry[]{
    0x40, 0x53, 0x57, 0x41, 0x55, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x28, 0x49,
    0x8B, 0xD9, 0x49, 0x8B, 0xF8, 0x4C, 0x8B, 0xFA, 0x4C, 0x8B, 0xE9 };

// 0x5D4F30  mov [rsp+8],rcx / push rbp / push rsi / push rdi / push r13 /
//           push r14 / sub rsp,80h / mov r13,[r9+18h]
constexpr std::uint8_t GenericPetSearchEntry[]{
    0x48, 0x89, 0x4C, 0x24, 0x08, 0x55, 0x56, 0x57, 0x41, 0x55, 0x41, 0x56,
    0x48, 0x81, 0xEC, 0x80, 0x00, 0x00, 0x00, 0x4D, 0x8B, 0x69, 0x18 };

// 0x34C2C0  sub rsp,28h / test rcx,rcx / jnz +1Dh / (null unit: assert, dead)
// Only verified, never patched: the hooks call it to tell corpses apart.
constexpr std::uint8_t UnitIsDeadEntry[]{
    0x48, 0x83, 0xEC, 0x28, 0x48, 0x85, 0xC9, 0x75, 0x1D,
    0x88, 0x4C, 0x24, 0x30, 0x48, 0x8D, 0x4C, 0x24, 0x30 };

// Unit layout, see the header.
constexpr std::size_t   UnitTypeOffset    = 0x00;
constexpr std::size_t   UnitClassIdOffset = 0x04;
constexpr std::uint32_t UnitTypeMonster   = 1;

// ---------------------------------------------------------------------------
// Native signatures (Windows x64 ABI)
// ---------------------------------------------------------------------------

// (game, searcher, candidate) -> nonzero when the candidate is a valid target
using TargetTestFn = std::int64_t (*)(void* game, void* searcher, void* candidate) noexcept;
// (game, searcher, candidate, search context) -> nonzero stops the enumeration
using SearchCallbackFn = std::int64_t (*)(void* game, void* searcher, void* candidate, void* context) noexcept;
// (unit) -> nonzero when dead
using UnitIsDeadFn = std::uint32_t (*)(void* unit) noexcept;

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

constexpr char         ConfigSection[]    = "ally_ignore_list";
constexpr std::int32_t MinimumMonsterId   = 0;
constexpr std::int32_t MaximumMonsterId   = 0x7FFF;
constexpr std::size_t  InitialConfigBytes = 16'384;
constexpr std::size_t  MaximumConfigBytes = 1'048'576;
constexpr std::size_t  RejectedSampleSize = 5;
constexpr std::size_t  BitmapWords        = (static_cast<std::size_t>(MaximumMonsterId) + 1) / 64;

constexpr char DefaultConfigToml[] =
    "# Ally Ignore List\n"
    "#\n"
    "# Allied units never pick the monster types listed below as a target:\n"
    "# hirelings, necromancer summons, druid summons, assassin traps and\n"
    "# shadows, Warlock demons (GenericPet), converted monsters and every other\n"
    "# unit on the player's side.\n"
    "#\n"
    "# Only target picking changes. A listed monster is still a solid unit:\n"
    "#   - missiles still collide with it and hit it\n"
    "#   - whatever reaches it still deals its damage and effects\n"
    "#   - the player can still attack it\n"
    "#   - its corpse stays usable for corpse eaters and corpse skills\n"
    "#   - evil monsters are unaffected; a Confused monster will not pick it either\n"
    "#\n"
    "# How it works: when an AI looks for something to attack, the game tests\n"
    "# every nearby unit with one of four \"is this a valid target?\" checks. This\n"
    "# plugin answers \"no\" for a living monster from the list, so the ally moves\n"
    "# on to the next candidate. The four checks:\n"
    "#   - the wide search behind every allied AI's main target\n"
    "#   - the nearest-enemy search (hirelings, necro pets, traps, Poison Creeper)\n"
    "#   - the Shadow Master's own search\n"
    "#   - the GenericPet search (Warlock demons)\n"
    "#\n"
    "# Not covered: an ally copying the target the player attacks. Shadow Masters\n"
    "# copy their player's current target, and the skill that warps pets onto the\n"
    "# caster's target orders them onto it. Both need the player to target the\n"
    "# monster first, so a monster the player cannot select never gets there.\n"
    "# Shadow Masters also switch from a normal minion to the monster that owns\n"
    "# it, which only matters if a listed monster has minions of its own.\n"
    "#\n"
    "# Server side only. In single player the game hosts its own server, so it\n"
    "# applies there too.\n"
    "#\n"
    "# Console command: allyignore (status, ignored ids and skip counters)\n"
    "\n"
    "[ally_ignore_list]\n"
    "\n"
    "# Master switch. false installs nothing at all.\n"
    "enabled = true\n"
    "\n"
    "# The monsters allies must never target, as monstats.txt hcIdx values,\n"
    "# 0 to 32767. An empty list, [], ignores nothing and installs no hook.\n"
    "# The list may be written on one line or spread over several, and may\n"
    "# carry # comments:\n"
    "#\n"
    "#   ignored_monster_ids = [1110, 1111]\n"
    "#\n"
    "ignored_monster_ids = [\n"
    "    1110,   # stormobelisk\n"
    "]\n";

struct Settings {
    bool enabled = true;
};

struct ListReport {
    bool         found        = false;
    bool         malformed    = false;
    bool         unterminated = false;
    std::int32_t rejected     = 0;
    std::string  rejectedSample;
};

// ---------------------------------------------------------------------------
// Hooks
// ---------------------------------------------------------------------------

enum class HookId : std::size_t {
    WideSearch,
    NearestEnemy,
    ShadowMaster,
    GenericPet,
    Count,
};

constexpr std::size_t HookCount = static_cast<std::size_t>(HookId::Count);

constexpr std::array<const char*, HookCount> HookNames{
    "wide search",
    "nearest-enemy search",
    "Shadow Master search",
    "GenericPet search",
};

enum class InstallState : std::uint8_t {
    NotAttempted,
    DisabledByConfig,
    NoIgnoredIds,
    ConfigError,
    UnsupportedBuild,
    InstallFailed,
    Partial,
    Installed,
};

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

const D2RL::PluginContext*                          Context{};
std::uintptr_t                                      Base{};
Settings                                            Config{};
std::array<std::uint64_t, BitmapWords>              IgnoredBits{};
std::int32_t                                        IgnoredCount{};
bool                                                ConfigFileWasRead{};
std::atomic<InstallState>                           State{ InstallState::NotAttempted };
std::array<bool, HookCount>                         HookInstalled{};
std::array<std::atomic<std::uint64_t>, HookCount>   Skips{};
UnitIsDeadFn                                        UnitIsDead{};

TargetTestFn     OriginalWideSearchTest{};
TargetTestFn     OriginalNearestEnemyTest{};
SearchCallbackFn OriginalShadowMasterSearch{};
SearchCallbackFn OriginalGenericPetSearch{};

auto IsIgnoredId(std::uint32_t monsterId) noexcept -> bool {
    if (monsterId > static_cast<std::uint32_t>(MaximumMonsterId)) return false;
    return ((IgnoredBits[monsterId >> 6] >> (monsterId & 63U)) & 1U) != 0;
}

void MarkIgnored(std::int32_t monsterId) noexcept {
    const auto id = static_cast<std::uint32_t>(monsterId);
    IgnoredBits[id >> 6] |= std::uint64_t{ 1 } << (id & 63U);
}

void ClearIgnored() noexcept {
    IgnoredBits.fill(0);
    IgnoredCount = 0;
}

// A living monster from the list. The type and class id are plain reads; the
// dead test is only reached for a listed class, so every other unit costs two
// compares.
auto IsIgnoredLivingMonster(void* unit) noexcept -> bool {
    if (unit == nullptr) return false;
    const auto* bytes = static_cast<const std::uint8_t*>(unit);
    std::uint32_t type = 0;
    std::memcpy(&type, bytes + UnitTypeOffset, sizeof(type));
    if (type != UnitTypeMonster) return false;
    std::uint32_t classId = 0;
    std::memcpy(&classId, bytes + UnitClassIdOffset, sizeof(classId));
    if (!IsIgnoredId(classId)) return false;
    return UnitIsDead(unit) == 0;
}

void CountSkip(HookId hook) noexcept {
    Skips[static_cast<std::size_t>(hook)].fetch_add(1, std::memory_order_relaxed);
}

auto HookWideSearchTest(void* game, void* searcher, void* candidate) noexcept -> std::int64_t {
    if (IsIgnoredLivingMonster(candidate)) {
        CountSkip(HookId::WideSearch);
        return 0;
    }
    const TargetTestFn original = OriginalWideSearchTest;
    return original != nullptr ? original(game, searcher, candidate) : 0;
}

auto HookNearestEnemyTest(void* game, void* searcher, void* candidate) noexcept -> std::int64_t {
    if (IsIgnoredLivingMonster(candidate)) {
        CountSkip(HookId::NearestEnemy);
        return 0;
    }
    const TargetTestFn original = OriginalNearestEnemyTest;
    return original != nullptr ? original(game, searcher, candidate) : 0;
}

// Returning 0 is the callback's own "keep looking" answer; it never records
// the candidate in any of its slots or counters.
auto HookShadowMasterSearch(void* game, void* searcher, void* candidate, void* context) noexcept
        -> std::int64_t {
    if (IsIgnoredLivingMonster(candidate)) {
        CountSkip(HookId::ShadowMaster);
        return 0;
    }
    const SearchCallbackFn original = OriginalShadowMasterSearch;
    return original != nullptr ? original(game, searcher, candidate, context) : 0;
}

auto HookGenericPetSearch(void* game, void* searcher, void* candidate, void* context) noexcept
        -> std::int64_t {
    if (IsIgnoredLivingMonster(candidate)) {
        CountSkip(HookId::GenericPet);
        return 0;
    }
    const SearchCallbackFn original = OriginalGenericPetSearch;
    return original != nullptr ? original(game, searcher, candidate, context) : 0;
}

// ---------------------------------------------------------------------------
// Config parsing (small TOML subset, no external dependency)
// ---------------------------------------------------------------------------

auto Trim(std::string_view value) noexcept -> std::string_view {
    std::size_t first = 0;
    while (first < value.size()
            && (value[first] == ' ' || value[first] == '\t'
                || value[first] == '\r' || value[first] == '\n')) {
        ++first;
    }
    std::size_t last = value.size();
    while (last > first
            && (value[last - 1] == ' ' || value[last - 1] == '\t'
                || value[last - 1] == '\r' || value[last - 1] == '\n')) {
        --last;
    }
    return value.substr(first, last - first);
}

auto StripComment(std::string_view line) noexcept -> std::string_view {
    const std::size_t hash = line.find('#');
    return hash == std::string_view::npos ? line : line.substr(0, hash);
}

auto ParseBool(std::string_view value, bool& out) noexcept -> bool {
    if (value == "true") { out = true; return true; }
    if (value == "false") { out = false; return true; }
    return false;
}

// Decimal, optional leading '+', '_' separators allowed as in TOML.
auto ParseMonsterId(std::string_view text, std::int32_t& out) noexcept -> bool {
    std::size_t index = 0;
    if (!text.empty() && text[0] == '+') index = 1;
    std::int32_t value = 0;
    bool anyDigit = false;
    for (; index < text.size(); ++index) {
        const char character = text[index];
        if (character == '_') continue;
        if (character < '0' || character > '9') return false;
        anyDigit = true;
        value = value * 10 + (character - '0');
        if (value > MaximumMonsterId) return false;
    }
    if (!anyDigit) return false;
    out = value;
    return true;
}

// The text between the brackets, one or more lines already joined with
// commas. Empty elements (a trailing comma, a blank line) are skipped.
void ApplyIdList(std::string_view text, ListReport& report) noexcept {
    std::size_t cursor = 0;
    while (cursor <= text.size()) {
        const std::size_t comma = text.find(',', cursor);
        const std::size_t end = comma == std::string_view::npos ? text.size() : comma;
        const std::string_view element = Trim(text.substr(cursor, end - cursor));
        if (!element.empty()) {
            std::int32_t monsterId = 0;
            if (ParseMonsterId(element, monsterId)) {
                if (!IsIgnoredId(static_cast<std::uint32_t>(monsterId))) {
                    MarkIgnored(monsterId);
                    ++IgnoredCount;
                }
            } else {
                ++report.rejected;
                if (report.rejected <= static_cast<std::int32_t>(RejectedSampleSize)) {
                    if (!report.rejectedSample.empty()) report.rejectedSample += ", ";
                    report.rejectedSample.append(element.substr(0, 24));
                }
            }
        }
        if (comma == std::string_view::npos) break;
        cursor = comma + 1;
    }
}

void ParseConfig(std::string_view text, ListReport& report) noexcept {
    std::string_view section;
    std::string      arrayText;
    bool             capturing = false;

    std::size_t cursor = 0;
    while (cursor < text.size()) {
        std::size_t end = text.find('\n', cursor);
        if (end == std::string_view::npos) end = text.size();
        const std::string_view line = Trim(StripComment(text.substr(cursor, end - cursor)));
        cursor = end + 1;

        if (capturing) {
            if (!line.empty() && line.front() == '[') {
                // A table header before the closing bracket: the list was
                // never closed. Drop it and read the header normally.
                capturing = false;
                report.unterminated = true;
                ClearIgnored();
            } else {
                const std::size_t close = line.find(']');
                arrayText.append(line.substr(0, close));
                arrayText.push_back(',');
                if (close != std::string_view::npos) {
                    capturing = false;
                    ApplyIdList(arrayText, report);
                }
                continue;
            }
        }

        if (line.empty()) continue;

        if (line.front() == '[') {
            const std::size_t close = line.find(']');
            section = Trim(line.substr(1, close == std::string_view::npos ? close : close - 1));
            continue;
        }

        const std::size_t equals = line.find('=');
        if (equals == std::string_view::npos || section != ConfigSection) continue;
        const std::string_view key = Trim(line.substr(0, equals));
        const std::string_view value = Trim(line.substr(equals + 1));

        if (key == "enabled") {
            if (!ParseBool(value, Config.enabled)) {
                D2RL::LogWarnF(Context,
                    "AllyIgnoreList: enabled must be true or false; keeping %s.",
                    Config.enabled ? "true" : "false");
            }
        } else if (key == "ignored_monster_ids") {
            // The last assignment wins, as with every other key.
            ClearIgnored();
            report.found = true;
            report.malformed = false;
            report.unterminated = false;
            report.rejected = 0;
            report.rejectedSample.clear();
            if (value.empty() || value.front() != '[') {
                report.malformed = true;
                continue;
            }
            const std::size_t close = value.find(']');
            if (close == std::string_view::npos) {
                arrayText.assign(value.substr(1));
                arrayText.push_back(',');
                capturing = true;
            } else {
                arrayText.assign(value.substr(1, close - 1));
                ApplyIdList(arrayText, report);
            }
        }
    }

    if (capturing) {
        report.unterminated = true;
        ClearIgnored();
    }
}

auto ReadConfigText(std::string& out) noexcept -> bool {
    std::string buffer(InitialConfigBytes, '\0');
    std::uint32_t required = 0;
    if (!Context->ReadConfig(buffer.data(), static_cast<std::uint32_t>(buffer.size()), &required)) {
        if (required < buffer.size() || required >= MaximumConfigBytes) return false;
        buffer.assign(static_cast<std::size_t>(required) + 1, '\0');
        if (!Context->ReadConfig(buffer.data(), static_cast<std::uint32_t>(buffer.size()), &required)) {
            return false;
        }
    }
    buffer.resize(std::strlen(buffer.c_str()));
    out = std::move(buffer);
    return true;
}

auto ReadConfiguration() noexcept -> bool {
    std::string text;
    if (!Context->EnsureConfig(DefaultConfigToml)) {
        Context->LogWarn("AllyIgnoreList: the config file could not be created; using the "
                         "embedded defaults.");
        text = DefaultConfigToml;
    } else if (!ReadConfigText(text)) {
        Context->LogWarn("AllyIgnoreList: the config file could not be read; using the "
                         "embedded defaults.");
        text = DefaultConfigToml;
    } else {
        ConfigFileWasRead = true;
    }

    ListReport report{};
    ParseConfig(text, report);

    if (!report.found) {
        Context->LogWarn("AllyIgnoreList: ignored_monster_ids is missing from "
                         "[ally_ignore_list], so nothing is ignored.");
    } else if (report.malformed) {
        Context->LogError("AllyIgnoreList: ignored_monster_ids must be a list in square "
                          "brackets, for example [1110, 1111]. Nothing is ignored.");
        return false;
    } else if (report.unterminated) {
        Context->LogError("AllyIgnoreList: ignored_monster_ids is missing its closing ']'. "
                          "Nothing is ignored.");
        return false;
    }

    if (report.rejected > 0) {
        D2RL::LogWarnF(Context,
            "AllyIgnoreList: %d entr%s in ignored_monster_ids ignored (%s%s). Every entry "
            "must be a whole number from %d to %d.",
            report.rejected, report.rejected == 1 ? "y was" : "ies were",
            report.rejectedSample.c_str(),
            report.rejected > static_cast<std::int32_t>(RejectedSampleSize) ? ", ..." : "",
            MinimumMonsterId, MaximumMonsterId);
    }
    return true;
}

// Appends " id id id" in ascending order, stopping before `limit` characters.
void AppendIgnoredIds(std::string& out, std::size_t limit) noexcept {
    char number[16]{};
    for (std::int32_t monsterId = MinimumMonsterId; monsterId <= MaximumMonsterId; ++monsterId) {
        if (!IsIgnoredId(static_cast<std::uint32_t>(monsterId))) continue;
        const int length = std::snprintf(number, sizeof(number), " %d", monsterId);
        if (length <= 0) continue;
        if (out.size() + static_cast<std::size_t>(length) + 4 > limit) {
            out += " ...";
            return;
        }
        out.append(number, static_cast<std::size_t>(length));
    }
}

// ---------------------------------------------------------------------------
// Installation
// ---------------------------------------------------------------------------

struct HookSite {
    HookId               id;
    std::uint64_t        rva;
    const std::uint8_t*  entry;
    std::uint32_t        entrySize;
};

constexpr std::array<HookSite, HookCount> Sites{{
    { HookId::WideSearch,   WideSearchTestRva,     WideSearchTestEntry,
      static_cast<std::uint32_t>(sizeof(WideSearchTestEntry)) },
    { HookId::NearestEnemy, NearestEnemyTestRva,   NearestEnemyTestEntry,
      static_cast<std::uint32_t>(sizeof(NearestEnemyTestEntry)) },
    { HookId::ShadowMaster, ShadowMasterSearchRva, ShadowMasterSearchEntry,
      static_cast<std::uint32_t>(sizeof(ShadowMasterSearchEntry)) },
    { HookId::GenericPet,   GenericPetSearchRva,   GenericPetSearchEntry,
      static_cast<std::uint32_t>(sizeof(GenericPetSearchEntry)) },
}};

// Every byte is checked before anything is written, so an unknown build
// leaves the game completely stock.
auto VerifyImage() noexcept -> bool {
    bool verified = true;
    for (const HookSite& site : Sites) {
        if (!Context->CheckExpectedBytes(site.rva, site.entry, site.entrySize)) {
            D2RL::LogErrorF(Context,
                "AllyIgnoreList: the %s test at 0x%llX does not match the verified D2R image, "
                "or another plugin already hooks it.",
                HookNames[static_cast<std::size_t>(site.id)],
                static_cast<unsigned long long>(site.rva));
            verified = false;
        }
    }
    if (!Context->CheckExpectedBytes(UnitIsDeadRva, UnitIsDeadEntry,
            static_cast<std::uint32_t>(sizeof(UnitIsDeadEntry)))) {
        Context->LogError("AllyIgnoreList: IsDead at 0x34C2C0 does not match the verified "
                          "D2R image.");
        verified = false;
    }
    return verified;
}

auto InstallOne(const HookSite& site) noexcept -> bool {
    switch (site.id) {
    case HookId::WideSearch:
        return Context->InstallInlineHook(site.rva, site.entry, site.entrySize,
                   &HookWideSearchTest, &OriginalWideSearchTest)
            && OriginalWideSearchTest != nullptr;
    case HookId::NearestEnemy:
        return Context->InstallInlineHook(site.rva, site.entry, site.entrySize,
                   &HookNearestEnemyTest, &OriginalNearestEnemyTest)
            && OriginalNearestEnemyTest != nullptr;
    case HookId::ShadowMaster:
        return Context->InstallInlineHook(site.rva, site.entry, site.entrySize,
                   &HookShadowMasterSearch, &OriginalShadowMasterSearch)
            && OriginalShadowMasterSearch != nullptr;
    case HookId::GenericPet:
        return Context->InstallInlineHook(site.rva, site.entry, site.entrySize,
                   &HookGenericPetSearch, &OriginalGenericPetSearch)
            && OriginalGenericPetSearch != nullptr;
    case HookId::Count:
    default:
        return false;
    }
}

void InstallHooks(bool configUsable) noexcept {
    if (!configUsable) {
        State.store(InstallState::ConfigError, std::memory_order_release);
        return;
    }
    if (!Config.enabled) {
        State.store(InstallState::DisabledByConfig, std::memory_order_release);
        Context->LogInfo("AllyIgnoreList: disabled in the config file; nothing installed.");
        return;
    }
    if (IgnoredCount == 0) {
        State.store(InstallState::NoIgnoredIds, std::memory_order_release);
        Context->LogInfo("AllyIgnoreList: ignored_monster_ids is empty; nothing installed.");
        return;
    }
    if (!VerifyImage()) {
        State.store(InstallState::UnsupportedBuild, std::memory_order_release);
        Context->LogError("AllyIgnoreList: nothing installed; the game runs stock.");
        return;
    }

    // Resolved before any hook goes live: every hook calls it.
    UnitIsDead = reinterpret_cast<UnitIsDeadFn>(Base + UnitIsDeadRva);

    std::size_t installed = 0;
    for (const HookSite& site : Sites) {
        const auto index = static_cast<std::size_t>(site.id);
        if (InstallOne(site)) {
            HookInstalled[index] = true;
            ++installed;
        } else {
            D2RL::LogErrorF(Context,
                "AllyIgnoreList: the %s test at 0x%llX could not be hooked; that search still "
                "picks listed monsters.",
                HookNames[index], static_cast<unsigned long long>(site.rva));
        }
    }

    if (installed == HookCount) {
        State.store(InstallState::Installed, std::memory_order_release);
        D2RL::LogInfoF(Context,
            "AllyIgnoreList: active, %d monster id%s ignored by allied target searches.",
            IgnoredCount, IgnoredCount == 1 ? "" : "s");
    } else if (installed > 0) {
        State.store(InstallState::Partial, std::memory_order_release);
    } else {
        State.store(InstallState::InstallFailed, std::memory_order_release);
    }
}

// ---------------------------------------------------------------------------
// Console
// ---------------------------------------------------------------------------

auto StateText(InstallState state) noexcept -> const char* {
    switch (state) {
    case InstallState::Installed:        return "active";
    case InstallState::Partial:          return "PARTLY ACTIVE, a hook failed, see the log";
    case InstallState::DisabledByConfig: return "off in the config file";
    case InstallState::NoIgnoredIds:     return "off, ignored_monster_ids is empty";
    case InstallState::ConfigError:      return "NOT ACTIVE, ignored_monster_ids could not be read, see the log";
    case InstallState::UnsupportedBuild: return "NOT ACTIVE, unrecognised game build or already hooked, see the log";
    case InstallState::InstallFailed:    return "NOT ACTIVE, the hooks failed, see the log";
    case InstallState::NotAttempted:
    default:                             return "NOT ACTIVE, never attempted";
    }
}

auto __cdecl StatusCommand(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command,
        void*) noexcept -> D2RL::ConsoleCommandResult {
    if (command == nullptr || command->plugin == nullptr) return D2RL::ConsoleCommandResult::Failed;
    const D2RL::PluginContext* plugin = command->plugin;

    char header[256]{};
    std::snprintf(header, sizeof(header),
        "Ally Ignore List: %s | config %s | %d id%s ignored",
        StateText(State.load(std::memory_order_acquire)),
        ConfigFileWasRead ? "loaded" : "NOT READ (embedded defaults)",
        IgnoredCount, IgnoredCount == 1 ? "" : "s");
    plugin->WriteConsoleMessage(header);

    std::string ids = "ids:";
    AppendIgnoredIds(ids, 380);
    plugin->WriteConsoleMessage(IgnoredCount == 0 ? "ids: none" : ids.c_str());

    for (std::size_t index = 0; index < HookCount; ++index) {
        char line[160]{};
        std::snprintf(line, sizeof(line), "%s: %s, %llu candidate%s skipped",
            HookNames[index],
            HookInstalled[index] ? "hooked" : "not hooked",
            static_cast<unsigned long long>(Skips[index].load(std::memory_order_relaxed)),
            Skips[index].load(std::memory_order_relaxed) == 1 ? "" : "s");
        plugin->WriteConsoleMessage(line);
    }
    return D2RL::ConsoleCommandResult::Handled;
}

constexpr D2RL::PluginInfo Info{
    .infoSize    = D2RL::PluginInfoSize,
    .abiVersion  = D2RL_PLUGIN_ABI_VERSION,
    .id          = "celestialrayone.ally-ignore-list",
    .name        = "Ally Ignore List",
    .version     = "1.0.0",
    .author      = "CelestialRayOne",
    .description = "Allied units never pick the listed monster types as a target; missiles "
                   "still collide with them.",
    // Server: monster AI only runs on the server.
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
    Base = context->exeBase;

    // Registered first, and the load always succeeds: returning false would
    // unload the DLL and take the command with it, leaving no in-game way to
    // see why nothing is ignored.
    if (!Context->RegisterConsoleCommand("allyignore", &StatusCommand,
            "Reports Ally Ignore List status, ignored ids and skip counters.")) {
        Context->LogWarn("AllyIgnoreList: the status console command was refused.");
    }

    const bool configUsable = ReadConfiguration();
    InstallHooks(configUsable);
    return true;
}

// The loader removes its inline hooks when the plugin unloads; nothing here
// writes game memory by itself.
D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {}

}  // namespace CelestialRayOne::AllyIgnoreList
