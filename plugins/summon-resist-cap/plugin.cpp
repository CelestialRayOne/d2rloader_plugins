// Summon Resist Cap
//
// Allied monsters resolve their resistances through the same capped branch the
// game already uses for players: base 75, raised by the per-element max-resist
// stat, hard-ceilinged at 95, floored at -100. Physical (damageresist) keeps
// the engine's own 50 cap. The nightmare/hell difficulty penalty is not applied
// to them.
//
// Which monsters get capped: every monster whose monstats.txt Align byte
// (record + 0x87) is 1, plus the rows listed in [allies] extra_monster_ids.
// Every other monster keeps the engine's uncapped path.
//
// Uncap pairs (3.3.0): [uncap] pairs lists [monster id, stat id] pairs. A
// capped monster of that monstats row is NOT capped while the player who owns
// it has a non-zero total of that stat. It then takes the engine's uncapped
// path exactly like an ordinary monster, so it can pass 100 and become immune.
// Owner and stat are read at every resolution, so the exception follows the
// stat as it comes and goes.
//
// 3.3.1:
//   - [uncap] pairs also takes a single pair written flat, pairs = [423, 511].
//     3.3.0 only read the nested form [[423, 511]] and dropped the flat one
//     with nothing but a log warning, so the pair never existed and the
//     summon stayed capped.
//   - A UTF-8 byte order mark at the start of the TOML is skipped, and a key
//     the plugin does not know, or one written under the wrong [section], is
//     named in the log instead of being ignored silently.
//   - The log lists the pairs read at load, and for every paired summon it
//     writes one line whenever its result changes: the owner found through
//     the owner link, the value of each paired stat on that owner and the
//     decision. summon-resist-cap shows the latest of these lines.
//
// 3.4.0, bound demons:
//   - With Bound Demon Scaling (celestialrayone.bound-demon-scaling) loaded, a
//     bound demon is decided as its pseudo row: that row's Align byte, the
//     extra_monster_ids and the [uncap] pairs all use the pseudo row's id,
//     through the same decision every other monster takes. The row comes from
//     that plugin's "stats-row" service, version 1, asked on every decision.
//   - Every other unit, and every bound demon while that plugin is missing, off
//     or has no pseudo row, is decided by its own monstats row exactly as
//     before. The service is looked up on first use and again every 5 seconds
//     while it is missing; the log says once which of the two it is.
//
// Cap decision service (3.3.0): the gate's decision is also published through
// D2RLoader's plugin communication service as "cap-decision", version 1 (see
// CapDecisionService below). Monster Display asks it for every monster it
// shows, so its numbers follow the Align column, extra_monster_ids and the
// uncap pairs exactly, with no second copy of these settings to keep in sync.
//
// Verified sites, D2RLoader image (3.2.92777 / 3.3.93847 common space):
//   0x4523E0  SUNITDMG_ApplyResistancesAndAbsorb(ctx, resistanceRecord, dontAbsorb)
//   0x452468  difficulty-penalty gate, reads ctx+0x24
//   0x45249C  cap gate: 39 6E 24 (cmp [rsi+24h], ebp) + 75 57 (jnz 0x4524F8)
//   0x4524A1  first instruction of the capping block
//   0x4524F8  first instruction after the capping block
//   0x34B9D0  UNITS_GetUnitType(unit)
//   0x349860  UNITS_GetUnitClassId(unit) -> [unit+4], the monstats row
//   0x34A0E0  UNITS_GetUnitDataContext(unit)
//   0x0976E0  DATATBLS_GetMonStatsTxtRecordForContext(context, classId),
//             returns records + 508 * classId
//   0x4A53C0  D2GAME_GetMinionOwner(unit) -> owner unit, or null
//   0x2F5020  STATLIST_GetUnitStat(unit, statId, layer), the loader's thunk to
//             D2RCore!ReadWideUnitStat
//
// The resolver reads ctx+0x24 exactly twice and nowhere else. A non-zero value
// skips the difficulty penalty at 0x452468 and then skips the entire capping
// block at 0x45249C. That single flag is why an ally can pass 100 and become
// immune. ctx+0x18 is the defender: the resolver reads the resistance itself
// from it (ReadWideUnitStat(ctx[3], resist stat) at 0x452436).
//
// This plugin replaces the five bytes of the cap gate ONLY, with a jmp rel32
// into a relay page, and leaves the function entry pristine so plugins that
// hook the entry (monsterdisplay, for one) keep working. Patching the gate
// rather than the entry also means the penalty gate has already run and already
// skipped the penalty by the time the relay is reached, so nothing has to be
// done about the difficulty penalty at all.
//
// D2RLoader 1.3.1: the loader now accepts a rel32 jump only when its target is
// inside D2R.exe, and the relay page is not. The gate therefore jumps to a
// 5-byte "jmp relay" trampoline in the int3 padding after the resolver
// (0x4526B6; the resolver's ret is at 0x4526B5, the next function starts at
// 0x4526C0), and the trampoline jumps on to the relay. A jmp changes no
// register, flag or stack slot, so the relay sees exactly what the gate saw.
// Re-verified against the 1.3.1 image: rsi = ctx (mov rsi,rcx at 0x4523FD)
// and ebp = 0 (xor ebp,ebp at 0x4523F5) with no later write to either before
// the gate; the gate, both continuations (0x4524A1 mov edx,[r14+0Ch],
// 0x4524F8 cmp dword [r14+8],24h, neither reads the flags) and the four game
// functions the callback calls are unchanged.
//
// Owner lookup, 0x4A53C0, read from the 1.3.1 image for 3.3.0:
//   Returns null unless UNITS_GetUnitType says monster (1). Otherwise it
//   follows MonsterData [unit+10h] to the AI control block [+30h] and returns
//   SUNIT_GetServerUnit (0x48FE80) for the game at [ai+60h], the owner type at
//   [ai+6Ch] and the owner id at [ai+68h]; null when a link is missing or the
//   owner has left. AIGENERAL_SetOwnerData (0x4A5800) writes those fields. The
//   summon spawn helper 0x4350E0 calls it at 0x435373 with the caster's own
//   type and id, right after adding the summon to the caster's pet list
//   (0x4FEB00), and Revive does the same at 0x55EAF1, so a summon's owner is
//   the player who cast it. The game uses this same lookup to credit an
//   owner's magic find for a minion's kill (0x44237B). A minion owned by
//   another minion is followed up the chain, at most MaxOwnerDepth links, until
//   a player is reached; a loop or any other unit type ends with no owner.
//
// Stat read, 0x2F5020: D2RLoader 1.3.x replaces its first 10 bytes with an
//   import thunk into D2RCore's wide stat store:
//     2F5020  FF 25 xx xx xx xx   jmp [rip+disp32]   ; D2RCore!ReadWideUnitStat
//     2F5026  90 90 90 90         nop x4
//     2F502A  48 89 74 24 20 ...  untouched remainder of the old body
//   ReadWideUnitStat (D2RCore 1.3.1 export +0x831DE0, body +0x3D8BD0) returns
//   the unit's total for (statId, layer), and 0 for a null unit, a unit with
//   no stat list, or a stat id of 0x8000 or more. It builds its lookup key from
//   the full 32-bit r8d, so the layer is passed as a 32-bit 0. disp32 locates
//   the loader's import slot, which moves between loader builds, so the check
//   pins only the thunk shape and the old body. The owner lookup and the stat
//   read are checked, and used, only when pairs are configured.

#include <D2RLPlugin/api.h>

// Windows.h defines min and max as macros unless this is set, which breaks any
// later std::numeric_limits<...>::min() / max() call.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

constexpr char DefaultConfig[] = R"toml(# ============================================================================
# Summon Resist Cap
# ============================================================================
#
# WHAT IT DOES
# Allied monsters resolve their resistances through the exact same capped
# branch the engine already runs for players:
#
#   cap        = 75 + the unit's per-element max-resist stat
#   hard cap   = 95            (never exceeded, same ceiling players have)
#   physical   = 50            (damageresist, when no max-resist stat applies)
#   floor      = -100
#   final      = min(resistance, cap)
#
# Without this plugin they skip that whole block, so a summon can pass 100%
# and become outright invincible to that damage type.
#
# The nightmare/hell difficulty penalty is never applied to them. Players keep
# it. This is not a setting, it falls out of where the patch sits.
#
# The 75 base, the 95 ceiling and the 50 physical cap are the game's own
# immediates and are shared with players by design, so they are not settings
# here. Changing them would change them for players too.
#
# WHICH MONSTERS GET CAPPED
# Every monster whose monstats.txt Align column is 1 (summons, Valkyrie,
# golems, Revives, hirelings), plus the rows in extra_monster_ids. Every other
# monster is left alone.
#
# Bound demons: with the Bound Demon Scaling plugin installed, a bound demon
# counts as that plugin's pseudo_row here, for the Align column,
# extra_monster_ids and [uncap] pairs alike.
#
# UNCAPPED SUMMONS
# [uncap] pairs lists [monster id, stat id] pairs. While the player who owns a
# capped monster of that monstats row has the stat, that monster is not
# capped: it resolves its resistances like an ordinary monster, so it can pass
# 100% and become immune to that damage type.
#
#   monster id  the monstats.txt row id (the *hcIdx column), the same number
#               summon-resist-probe prints.
#   stat id     the itemstatcost.txt row id, 0 to 32767. Any non-zero value of
#               the stat on the player counts.
#
# Writing pairs:
#   pairs = [423, 511]                 one pair, short form
#   pairs = [[423, 511]]               one pair
#   pairs = [[423, 511], [357, 511]]   two pairs
# Anything else (three numbers in one pair, loose numbers next to pairs) is
# named in the log and skipped.
#
#   - The owner is the player who summoned or hired the monster. A minion owned
#     by another of that player's minions belongs to the same player.
#   - It is checked on every hit, so the monster is capped again as soon as the
#     player loses the stat.
#   - List a monster more than once to accept several stats. Any one of them
#     is enough.
#
# Example: Valkyries (row 357) pass the cap while their owner has stat 511.
#   pairs = [[357, 511]]
#
# LOG (<scope>\d2rloader\logs\celestialrayone.summon-resist-cap.log)
#   - At load: the pairs read from this file, or "none".
#   - A key this plugin does not know, or a key under the wrong [section],
#     is named with its section. Nothing is ignored silently.
#   - For a paired summon, one line each time its result changes, e.g.
#       row 423, unit 5: owner player unit 1, stat 511 = 1 -> uncapped
#       row 423, unit 5: owner player unit 1, stat 511 = 0 -> capped
#       row 423, unit 5: no player owner (owner link: type 1, id 7) -> capped
#     summon-resist-cap shows the latest of these lines too.
#
# HOW IT WORKS
# Inside SUNITDMG_ApplyResistancesAndAbsorb (RVA 0x4523E0) a single flag at
# ctx+0x24 decides whether the capping block at 0x45249C runs. The plugin
# replaces those five bytes with a jump, through the int3 padding at 0x4526B6,
# into a relay that asks the plugin whether to cap. Owners are found with the
# game's own minion owner lookup (0x4A53C0) and stats are read with the game's
# stat reader (0x2F5020). The function entry is left untouched, so plugins
# hooking it keep working.
#
# The same decision is published to other plugins as the "cap-decision"
# service. Monster Display asks it, so it shows exactly the values the game
# uses, uncapped summons included, with nothing to copy into its own config.
#
# CONSOLE
#   summon-resist-cap       settings, patch state and counters
#   summon-resist-probe     monstats rows seen at the resistance resolver
#
# ============================================================================

# File format version. Leave this value unchanged.
config_version = 6

# Master switch. false loads without patching anything.
enabled = true


[allies]
# Cap every monster whose monstats.txt Align column is 1.
use_align_column = true

# Extra monstats rows to cap regardless of their Align value. Normally empty.
# Any separators: [357, 358] and 357 358 both parse.
extra_monster_ids = []


[uncap]
# [monster id, stat id] pairs, see UNCAPPED SUMMONS above. Up to 256 pairs,
# all on one line or one pair per line:
#   pairs = [
#       [357, 511],
#       [423, 511],
#   ]
# A single pair can also be written flat: pairs = [423, 511]
pairs = []


# Lets D2RLoader verify that multiplayer peers use matching settings.
[d2rl]
match = true
)toml";

// ---------------------------------------------------------------- native map

constexpr std::uintptr_t CapGateRva = 0x45249C;
constexpr std::uintptr_t CapBlockRva = 0x4524A1;      // gate falls through here
constexpr std::uintptr_t SkipCapRva = 0x4524F8;       // original jnz target
constexpr std::uintptr_t GetUnitTypeRva = 0x34B9D0;
constexpr std::uintptr_t GetUnitClassIdRva = 0x349860;
constexpr std::uintptr_t GetUnitDataContextRva = 0x34A0E0;
constexpr std::uintptr_t MonStatsGetRecordRva = 0x0976E0;
// Uncap pairs only.
constexpr std::uintptr_t GetMinionOwnerRva = 0x4A53C0;
constexpr std::uintptr_t GetUnitStatRva = 0x2F5020;
constexpr std::uintptr_t GetUnitStatThunkNopsRva = GetUnitStatRva + 6U;
constexpr std::uintptr_t GetUnitStatBodyRva = GetUnitStatRva + 10U;

constexpr std::uint32_t CapGatePatchSize = 5U;

// cmp dword ptr [rsi+24h], ebp   /   jnz 0x4524F8
constexpr std::array<std::uint8_t, 5> CapGateExpected{
    0x39, 0x6E, 0x24, 0x75, 0x57,
};
// In-image trampoline (D2RLoader 1.3.1), 10 x int3 after the resolver.
constexpr std::uintptr_t TrampolineRva = 0x4526B6;
constexpr std::array<std::uint8_t, 10> TrampolineRunExpected{
    0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC,
};
constexpr std::array<std::uint8_t, 5> TrampolineSlotExpected{
    0xCC, 0xCC, 0xCC, 0xCC, 0xCC,
};
constexpr std::array<std::uint8_t, 18> GetUnitTypeExpected{
    0x48, 0x83, 0xEC, 0x28, 0x48, 0x85, 0xC9, 0x75, 0x1D,
    0x88, 0x4C, 0x24, 0x30, 0x48, 0x8D, 0x4C, 0x24, 0x30,
};
// D2GAME_GetMinionOwner entry, 36 bytes from the 1.3.1 image:
//   4A53C0  40 53 / 48 83 EC 20      push rbx / sub rsp,20h
//   4A53C6  48 8B D9                 mov rbx,rcx
//   4A53C9  E8 02 66 EA FF           call 0x34B9D0       ; UNITS_GetUnitType
//   4A53CE  83 F8 01                 cmp eax,1           ; a monster?
//   4A53D1  0F 85 9E 00 00 00        jne 0x4A5475        ; no: return null
//   4A53D7  48 85 DB / 74 0D         test rbx,rbx / je 0x4A53E9
//   4A53DC  48 8B CB                 mov rcx,rbx
//   4A53DF  E8 EC 65 EA FF           call 0x34B9D0
constexpr std::array<std::uint8_t, 36> GetMinionOwnerExpected{
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9,
    0xE8, 0x02, 0x66, 0xEA, 0xFF, 0x83, 0xF8, 0x01, 0x0F,
    0x85, 0x9E, 0x00, 0x00, 0x00, 0x48, 0x85, 0xDB, 0x74,
    0x0D, 0x48, 0x8B, 0xCB, 0xE8, 0xEC, 0x65, 0xEA, 0xFF,
};
// STATLIST_GetUnitStat as D2RLoader 1.3.x leaves it: the thunk's
// jmp [rip+disp32] opcode, its four NOPs, and the untouched start of the old
// body (mov [rsp+20h],rsi / push rdi / sub rsp,20h / movzx ebp,r8w /
// mov edi,edx / mov rbx,rcx / test rcx,rcx).
constexpr std::array<std::uint8_t, 2> GetUnitStatThunkJump{0xFF, 0x25};
constexpr std::array<std::uint8_t, 4> GetUnitStatThunkNops{
    0x90, 0x90, 0x90, 0x90,
};
constexpr std::array<std::uint8_t, 22> GetUnitStatBody{
    0x48, 0x89, 0x74, 0x24, 0x20, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x41,
    0x0F, 0xB7, 0xE8, 0x8B, 0xFA, 0x48, 0x8B, 0xD9, 0x48, 0x85, 0xC9,
};

// Layout of the resolver's first argument, read straight off 0x4523E0.
constexpr std::size_t DefenderOffset = 0x18;      // ctx[3], stats are read from it
constexpr std::size_t UncappedFlagOffset = 0x24;  // gates penalty + cap

// Owner link, read only for the log. D2GAME_GetMinionOwner (0x4A53C0) reads
// MonsterData [unit+10h] -> AI control [+30h] -> game [+60h], owner id
// [+68h], owner type [+6Ch]; SUNIT_GetServerUnit (0x48FE80) matches the unit
// id at [unit+8].
constexpr std::size_t UnitIdOffset = 0x08;
constexpr std::size_t MonsterDataOffset = 0x10;
constexpr std::size_t AiControlOffset = 0x30;
constexpr std::size_t OwnerIdOffset = 0x68;
constexpr std::size_t OwnerTypeOffset = 0x6C;

constexpr std::size_t MonStatsRecordSize = 508U;  // from the getter itself
constexpr std::size_t MonStatsAiOffset = 0x52;    // signed word, <= 0x9A
// monstats.txt Align. Located in the column parser at sub_14039DB30, where the
// "Align" descriptor sits at rbp+0x340 and its record-offset store is
// mov qword [rbp+0x350], 0x87, and CONFIRMED in game: an allied row reads 1
// here while ordinary monsters read 0. 0 = enemy, 1 = ally, 2 = neutral.
constexpr std::size_t MonStatsAlignOffset = 0x87;
constexpr std::uint8_t AlignAlly = 1U;

constexpr std::int32_t UnitTypePlayer = 0;
constexpr std::int32_t UnitTypeMonster = 1;
constexpr std::size_t MaxAllyIds = 256U;
constexpr std::size_t MaxObservedRows = 24U;
constexpr std::size_t MaxUncapPairs = 256U;
constexpr std::size_t MaxOwnerDepth = 8U;
constexpr std::int64_t MaxPairMonsterId = 0xFFFF;
// ReadWideUnitStat answers 0 for any stat id of 0x8000 or more.
constexpr std::int64_t MaxPairStatId = 0x7FFF;

constexpr std::size_t RelaySize = 81U;
constexpr std::size_t RelayCallbackSlot = 29U;   // imm64 of mov rax, callback
constexpr std::size_t RelayCapJumpSlot = 60U;    // rel32 of jmp CapBlockRva
constexpr std::size_t RelayCapJumpEnd = 64U;
constexpr std::size_t RelaySkipJumpSlot = 77U;   // rel32 of jmp SkipCapRva
constexpr std::size_t RelaySkipJumpEnd = 81U;

constexpr std::uint32_t ConfigReadBufferBytes = 16384U;

using GetUnitTypeFn = std::int32_t(__fastcall*)(void*) noexcept;
using GetUnitClassIdFn = std::int32_t(__fastcall*)(void*) noexcept;
using GetUnitDataContextFn = std::uint8_t(__fastcall*)(void*) noexcept;
using MonStatsGetRecordFn =
    const std::uint8_t*(__fastcall*)(std::uint8_t, std::int32_t) noexcept;
using GetMinionOwnerFn = void*(__fastcall*)(void*) noexcept;
using GetUnitStatFn =
    std::int32_t(__fastcall*)(void*, std::int32_t, std::uint32_t) noexcept;

struct UncapPair {
    std::int32_t monsterId;
    std::int32_t statId;
};

struct Config {
    bool enabled{true};
    bool useAlignColumn{true};
    std::int32_t allyIds[MaxAllyIds]{};
    std::size_t allyIdCount{};
    UncapPair uncapPairs[MaxUncapPairs]{};
    std::size_t uncapPairCount{};
    bool sawObsoleteUncapStat{};
};

struct ObservedRow {
    std::int32_t classId;
    std::int32_t ai;
    std::uint32_t alignByte;
    std::uint64_t hits;
    std::uint64_t pairUncaps;
};

const D2RL::PluginContext* Context{};
std::uint8_t* Base{};
Config Settings{};
std::string LoadedConfigPath{"embedded defaults"};
std::string RuntimeBuild{"<unavailable>"};
void* RelayPage{};

GetUnitTypeFn GetUnitType{};
GetUnitClassIdFn GetUnitClassId{};
GetUnitDataContextFn GetUnitDataContext{};
MonStatsGetRecordFn MonStatsGetRecord{};
GetMinionOwnerFn GetMinionOwner{};
GetUnitStatFn GetUnitStat{};

std::atomic_bool Operational{};
std::atomic<std::uint64_t> GateCalls{};
std::atomic<std::uint64_t> PlayerGates{};
std::atomic<std::uint64_t> AllyCaps{};
std::atomic<std::uint64_t> ListCaps{};
std::atomic<std::uint64_t> MonsterSkips{};
std::atomic<std::uint64_t> PairChecks{};
std::atomic<std::uint64_t> PairNoOwner{};
std::atomic<std::uint64_t> PairUncaps{};
std::atomic<std::uint64_t> ServiceQueries{};
std::atomic_bool ServicePublished{};

ObservedRow Observed[MaxObservedRows]{};
std::atomic<std::size_t> ObservedCount{};

constexpr D2RL::PluginInfo Info{
    .infoSize = D2RL::PluginInfoSize,
    .abiVersion = D2RL_PLUGIN_ABI_VERSION,
    .id = "celestialrayone.summon-resist-cap",
    .name = "Summon Resist Cap",
    .version = "3.4.0",
    .author = "CelestialRayOne",
    .description =
        "Caps allied monsters' resistances the way player resistances are "
        "capped; owner stats can lift the cap per monster.",
    .flags = D2RL::PluginFlags::Server | D2RL::PluginFlags::NativeHooks,
};

template <typename T>
auto At(std::uintptr_t rva) noexcept -> T {
    return reinterpret_cast<T>(Base + rva);
}

auto VerifyBytes(
        std::uintptr_t rva,
        const std::uint8_t* expected,
        std::size_t size,
        const char* what) noexcept -> bool {
    if (Context->CheckExpectedBytes(
            rva, expected, static_cast<std::uint32_t>(size))) {
        return true;
    }
    char message[192]{};
    std::snprintf(
        message,
        sizeof(message),
        "SummonResistCap: %s at 0x%llX does not match the expected bytes; "
        "plugin refused.",
        what,
        static_cast<unsigned long long>(rva));
    Context->LogError(message);
    return false;
}

void Warn(const char* message) noexcept {
    if (Context != nullptr) Context->LogWarn(message);
}

// ------------------------------------------------------------------ config

auto Trim(std::string value) noexcept -> std::string {
    const auto isSpace = [](char c) {
        return std::isspace(static_cast<unsigned char>(c)) != 0;
    };
    while (!value.empty() && isSpace(value.front())) value.erase(value.begin());
    while (!value.empty() && isSpace(value.back())) value.pop_back();
    return value;
}

auto ParseBool(const std::string& value, bool fallback) noexcept -> bool {
    if (value == "true" || value == "1") return true;
    if (value == "false" || value == "0") return false;
    return fallback;
}

// Accepts "[357, 358]", "357,358" or "357 358". Anything that is not a digit
// separates entries, so brackets and commas need no special handling.
void ParseIdList(const std::string& value) noexcept {
    Settings.allyIdCount = 0U;
    std::int32_t current = 0;
    bool have = false;
    for (const char c : value) {
        if (c >= '0' && c <= '9') {
            current = current * 10 + (c - '0');
            have = true;
            continue;
        }
        if (have && Settings.allyIdCount < MaxAllyIds) {
            Settings.allyIds[Settings.allyIdCount++] = current;
        }
        current = 0;
        have = false;
    }
    if (have && Settings.allyIdCount < MaxAllyIds) {
        Settings.allyIds[Settings.allyIdCount++] = current;
    }
}

// One [monster id, stat id] entry of [uncap] pairs. An entry that cannot be a
// pair is reported and skipped; the rest of the list still loads.
void AddUncapPair(
        const std::int64_t* numbers,
        std::size_t count,
        const std::string& text) noexcept {
    char message[256]{};
    const char* problem = nullptr;
    if (count != 2U) {
        problem = "a pair is exactly [monster id, stat id]";
    } else if (numbers[0] < 0 || numbers[0] > MaxPairMonsterId) {
        problem = "the monster id must be a monstats.txt row from 0 to 65535";
    } else if (numbers[1] < 0 || numbers[1] > MaxPairStatId) {
        problem = "the stat id must be an itemstatcost.txt row from 0 to 32767";
    } else if (Settings.uncapPairCount >= MaxUncapPairs) {
        problem = "only the first 256 pairs are read";
    }
    if (problem != nullptr) {
        std::snprintf(
            message,
            sizeof(message),
            "SummonResistCap: [uncap] pairs entry %.64s ignored: %s.",
            text.c_str(),
            problem);
        Warn(message);
        return;
    }
    const UncapPair pair{
        static_cast<std::int32_t>(numbers[0]),
        static_cast<std::int32_t>(numbers[1]),
    };
    for (std::size_t i = 0U; i < Settings.uncapPairCount; ++i) {
        if (Settings.uncapPairs[i].monsterId == pair.monsterId
                && Settings.uncapPairs[i].statId == pair.statId) {
            return;  // listed twice; once is enough
        }
    }
    Settings.uncapPairs[Settings.uncapPairCount++] = pair;
}

// Reads [uncap] pairs: an array of [monster id, stat id] arrays, e.g.
// [[357, 511], [358, 512]], or one pair written flat, [357, 511]. Brackets
// give the structure; commas and spaces only separate numbers. 3.3.0 read
// only the nested form and dropped a flat pair with a log warning, which is
// how pairs = [422, 511] ended up as no pair at all.
void ParsePairList(const std::string& value) noexcept {
    Settings.uncapPairCount = 0U;
    std::int64_t numbers[3]{};
    std::size_t count = 0U;
    std::size_t entryStart = 0U;
    std::size_t depth = 0U;
    std::int64_t current = 0;
    bool have = false;
    bool negative = false;
    // Numbers straight inside the outer brackets: a flat pair.
    std::int64_t flat[3]{};
    std::size_t flatCount = 0U;
    std::size_t nestedEntries = 0U;
    bool misplaced = false;

    const auto finishNumber = [&]() noexcept {
        if (have) {
            const std::int64_t number = negative ? -current : current;
            if (depth == 2U) {
                if (count < 3U) numbers[count] = number;
                ++count;
            } else if (depth == 1U) {
                if (flatCount < 3U) flat[flatCount] = number;
                ++flatCount;
            } else {
                misplaced = true;
            }
        }
        current = 0;
        have = false;
        negative = false;
    };

    for (std::size_t i = 0U; i < value.size(); ++i) {
        const char c = value[i];
        if (c >= '0' && c <= '9') {
            if (current < 100000000) current = current * 10 + (c - '0');
            have = true;
            continue;
        }
        finishNumber();
        if (c == '-') {
            negative = true;
        } else if (c == '[') {
            ++depth;
            if (depth == 2U) {
                count = 0U;
                entryStart = i;
            }
        } else if (c == ']') {
            if (depth == 2U) {
                ++nestedEntries;
                AddUncapPair(
                    numbers, count, value.substr(entryStart, i + 1U - entryStart));
            }
            if (depth > 0U) --depth;
        }
    }
    finishNumber();

    if (flatCount > 0U) {
        if (nestedEntries == 0U && flatCount == 2U) {
            AddUncapPair(flat, flatCount, value);
        } else {
            misplaced = true;
        }
    }
    if (misplaced) {
        Warn("SummonResistCap: [uncap] pairs has numbers that are not one "
             "[monster id, stat id] pair; they were ignored. Write one pair as "
             "pairs = [423, 511] or pairs = [[423, 511]], several as "
             "pairs = [[423, 511], [357, 511]].");
    }
}

// Returns false for a key this plugin does not read in that section.
auto ApplyConfigLine(
        const std::string& section,
        const std::string& key,
        const std::string& value) noexcept -> bool {
    if (section.empty()) {
        if (key == "enabled") {
            Settings.enabled = ParseBool(value, Settings.enabled);
            return true;
        }
        return key == "config_version";
    }
    if (section == "allies") {
        if (key == "use_align_column") {
            Settings.useAlignColumn = ParseBool(value, Settings.useAlignColumn);
            return true;
        }
        if (key == "extra_monster_ids" || key == "monster_ids") {
            ParseIdList(value);
            return true;
        }
        if (key == "uncap_stat_id") {
            Settings.sawObsoleteUncapStat = true;
            return true;
        }
        return false;
    }
    if (section == "uncap") {
        if (key == "pairs") {
            ParsePairList(value);
            return true;
        }
        return false;
    }
    // [d2rl] is read by D2RLoader itself.
    return section == "d2rl";
}

void WarnUnknownKey(const std::string& section, const std::string& key) noexcept {
    char message[256]{};
    if (key == "pairs") {
        std::snprintf(
            message,
            sizeof(message),
            "SummonResistCap: pairs is under [%.32s] and was ignored; it belongs "
            "under [uncap].",
            section.empty() ? "no section" : section.c_str());
    } else {
        std::snprintf(
            message,
            sizeof(message),
            "SummonResistCap: unknown setting %.48s under [%.32s]; it was ignored.",
            key.c_str(),
            section.empty() ? "no section" : section.c_str());
    }
    Warn(message);
}

auto StripComment(const std::string& line) noexcept -> std::string {
    const auto comment = line.find('#');
    return Trim(comment == std::string::npos ? line : line.substr(0U, comment));
}

// [name] on a line of its own. "[357]" or "[357, 511]" is not a section.
auto IsSectionHeader(const std::string& line) noexcept -> bool {
    if (line.size() < 3U || line.front() != '[' || line.back() != ']') {
        return false;
    }
    const std::string name = Trim(line.substr(1U, line.size() - 2U));
    if (name.empty() || std::isdigit(static_cast<unsigned char>(name.front())) != 0) {
        return false;
    }
    for (const char c : name) {
        if (std::isalnum(static_cast<unsigned char>(c)) == 0
                && c != '_' && c != '-' && c != '.') {
            return false;
        }
    }
    return true;
}

// Brackets still open at the end of the text.
auto OpenBrackets(const std::string& text) noexcept -> std::int32_t {
    std::int32_t depth = 0;
    for (const char c : text) {
        if (c == '[') {
            ++depth;
        } else if (c == ']') {
            --depth;
        }
    }
    return depth;
}

void ParseConfig(const std::string& text) noexcept {
    std::string section;
    // A UTF-8 byte order mark would otherwise stick to the first line.
    std::size_t begin = text.rfind("\xEF\xBB\xBF", 0) == 0 ? 3U : 0U;
    std::string pending;
    bool havePending = false;
    // Hands out lines with comments removed; a line read ahead and not used is
    // handed out again next time.
    const auto nextLine = [&](std::string& line) noexcept -> bool {
        if (havePending) {
            line = pending;
            havePending = false;
            return true;
        }
        if (begin > text.size()) return false;
        std::size_t end = text.find('\n', begin);
        if (end == std::string::npos) end = text.size();
        line = StripComment(text.substr(begin, end - begin));
        begin = end + 1U;
        return true;
    };

    std::string line;
    while (nextLine(line)) {
        if (line.empty()) continue;

        if (IsSectionHeader(line)) {
            section = Trim(line.substr(1U, line.size() - 2U));
            continue;
        }
        const auto equals = line.find('=');
        if (equals == std::string::npos) continue;
        const std::string key = Trim(line.substr(0U, equals));
        std::string value = Trim(line.substr(equals + 1U));

        // An array may run over several lines. It ends where its brackets
        // close; a section header or another setting ends it early.
        std::string more;
        while (!value.empty() && value.front() == '['
                && OpenBrackets(value) > 0 && nextLine(more)) {
            if (IsSectionHeader(more) || more.find('=') != std::string::npos) {
                pending = more;
                havePending = true;
                char message[192]{};
                std::snprintf(
                    message,
                    sizeof(message),
                    "SummonResistCap: %.32s is missing a closing ]; it was read "
                    "up to the next line that is not part of it.",
                    key.c_str());
                Warn(message);
                break;
            }
            value += ' ';
            value += more;
        }
        if (!ApplyConfigLine(section, key, value)) WarnUnknownKey(section, key);
    }
}

auto NarrowPath(const wchar_t* wide) noexcept -> std::string {
    if (wide == nullptr || wide[0] == L'\0') return {};
    const int needed =
        ::WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (needed <= 1) return {};
    std::string narrow(static_cast<std::size_t>(needed - 1), '\0');
    ::WideCharToMultiByte(
        CP_UTF8, 0, wide, -1, narrow.data(), needed, nullptr, nullptr);
    return narrow;
}

auto ConfigPathForDisplay() noexcept -> std::string {
    constexpr std::size_t needed =
        offsetof(D2RL::PluginContext, pluginConfigPath) + sizeof(const wchar_t*);
    if (Context->contextSize < needed) return {};
    return NarrowPath(Context->pluginConfigPath);
}

auto LoadConfig() noexcept -> bool {
    Settings = Config{};
    LoadedConfigPath = "embedded defaults";

    if (!Context->EnsureConfig(DefaultConfig)) {
        Context->LogWarn(
            "SummonResistCap: the TOML could not be created; embedded defaults "
            "are in use.");
        return true;
    }

    std::string text(ConfigReadBufferBytes, '\0');
    std::uint32_t required = 0U;
    if (!Context->ReadConfig(
            text.data(), static_cast<std::uint32_t>(text.size()), &required)) {
        if (required > text.size()) {
            text.assign(required, '\0');
            if (!Context->ReadConfig(text.data(), required, &required)) {
                Context->LogWarn(
                    "SummonResistCap: the TOML could not be read; embedded "
                    "defaults are in use.");
                return true;
            }
        } else {
            Context->LogWarn(
                "SummonResistCap: the TOML could not be read; embedded "
                "defaults are in use.");
            return true;
        }
    }

    text.resize(std::strlen(text.c_str()));
    ParseConfig(text);
    if (Settings.sawObsoleteUncapStat) {
        Warn("SummonResistCap: [allies] uncap_stat_id (3.2.0) is no longer read. "
             "Use [uncap] pairs = [[monster id, stat id]] instead.");
    }

    const std::string path = ConfigPathForDisplay();
    LoadedConfigPath = path.empty() ? std::string{"<plugin config>"} : path;
    return true;
}

// ---------------------------------------------------------------- diagnostics

auto IsPlausiblePointer(const void* pointer) noexcept -> bool {
    const auto value = reinterpret_cast<std::uintptr_t>(pointer);
    return value >= 0x10000ULL && (value & 7U) == 0U;
}

// monstats records are 508 bytes apart, which is not a multiple of 8, so every
// odd row sits on a 4-byte boundary. Checking them for 8-byte alignment throws
// away half the table.
auto IsPlausibleRecord(const void* pointer) noexcept -> bool {
    const auto value = reinterpret_cast<std::uintptr_t>(pointer);
    return value >= 0x10000ULL && (value & 3U) == 0U;
}

auto MonStatsRecordFor(void* unit, std::int32_t classId) noexcept
        -> const std::uint8_t* {
    if (MonStatsGetRecord == nullptr || GetUnitDataContext == nullptr) {
        return nullptr;
    }
    const std::uint8_t* record =
        MonStatsGetRecord(GetUnitDataContext(unit), classId);
    if (!IsPlausibleRecord(record)) return nullptr;
    // The AI column proves this is a monstats record and not something else.
    const std::uint16_t ai =
        *reinterpret_cast<const std::uint16_t*>(record + MonStatsAiOffset);
    return ai <= 0x9AU ? record : nullptr;
}

// Records every monstats row that reaches the gate, with the row's AI id and
// its Align byte. Purely informational: summon-resist-probe prints it.
auto NoteRow(void* unit, std::int32_t classId) noexcept -> ObservedRow* {
    const std::size_t count = ObservedCount.load(std::memory_order_acquire);
    for (std::size_t i = 0U; i < count && i < MaxObservedRows; ++i) {
        if (Observed[i].classId == classId) {
            ++Observed[i].hits;
            return &Observed[i];
        }
    }
    if (count >= MaxObservedRows) return nullptr;

    ObservedRow row{classId, -1, 0xFFFFFFFFU, 1U, 0U};
    const std::uint8_t* record = MonStatsRecordFor(unit, classId);
    if (record != nullptr) {
        row.ai =
            *reinterpret_cast<const std::int16_t*>(record + MonStatsAiOffset);
        row.alignByte = record[MonStatsAlignOffset];
    }
    Observed[count] = row;
    ObservedCount.store(count + 1U, std::memory_order_release);
    return &Observed[count];
}

// ------------------------------------------------------------ gate callback

auto IsListedAlly(std::int32_t classId) noexcept -> bool {
    for (std::size_t i = 0U; i < Settings.allyIdCount; ++i) {
        if (Settings.allyIds[i] == classId) return true;
    }
    return false;
}

auto HasUncapPair(std::int32_t classId) noexcept -> bool {
    for (std::size_t i = 0U; i < Settings.uncapPairCount; ++i) {
        if (Settings.uncapPairs[i].monsterId == classId) return true;
    }
    return false;
}

// Follows the minion owner chain from a monster up to the player at its root.
// Null when there is no live owner, a link is neither a player nor a monster,
// or the chain loops or runs longer than MaxOwnerDepth links.
auto OwningPlayer(void* monster) noexcept -> void* {
    std::array<void*, MaxOwnerDepth> visited{};
    void* current = monster;
    for (std::size_t depth = 0U; depth < MaxOwnerDepth; ++depth) {
        for (std::size_t i = 0U; i < depth; ++i) {
            if (visited[i] == current) return nullptr;
        }
        visited[depth] = current;
        void* owner = GetMinionOwner(current);
        if (!IsPlausiblePointer(owner)) return nullptr;
        const std::int32_t ownerType = GetUnitType(owner);
        if (ownerType == UnitTypePlayer) return owner;
        if (ownerType != UnitTypeMonster) return nullptr;
        current = owner;
    }
    return nullptr;
}

// ------------------------------------------------------------- pair trace
// One log line per paired summon each time its result changes; the latest
// one is also shown by summon-resist-cap.

enum class PairOutcome : std::uint8_t { NoOwner, StatZero, Uncapped };

constexpr std::size_t TraceStats = 4U;
constexpr std::size_t TraceSlots = 64U;

struct PairTrace {
    const void* unit{};
    std::uint32_t unitId{};
    std::int32_t classId{-1};
    PairOutcome outcome{PairOutcome::NoOwner};
    std::uint32_t ownerId{};   // the owning player's unit id
    bool haveLink{};           // NoOwner: the raw owner link could be read
    std::int32_t linkType{};
    std::uint32_t linkId{};
    std::int32_t statIds[TraceStats]{};
    std::int32_t statValues[TraceStats]{};
    std::size_t statCount{};
};

PairTrace Traces[TraceSlots]{};
std::size_t TraceNext{};
PairTrace LastTrace{};
bool HaveLastTrace{};
SRWLOCK TraceLock = SRWLOCK_INIT;

template <typename T>
auto ReadField(const void* base, std::size_t offset) noexcept -> T {
    T value{};
    std::memcpy(&value, static_cast<const std::uint8_t*>(base) + offset, sizeof(T));
    return value;
}

// The owner link GetMinionOwner follows, for the log when it finds no player.
auto ReadOwnerLink(
        const void* monster, std::int32_t& type, std::uint32_t& id) noexcept -> bool {
    const void* data = ReadField<const void*>(monster, MonsterDataOffset);
    if (!IsPlausiblePointer(data)) return false;
    const void* ai = ReadField<const void*>(data, AiControlOffset);
    if (!IsPlausiblePointer(ai)) return false;
    type = ReadField<std::int32_t>(ai, OwnerTypeOffset);
    id = ReadField<std::uint32_t>(ai, OwnerIdOffset);
    return true;
}

auto SameResult(const PairTrace& a, const PairTrace& b) noexcept -> bool {
    if (a.outcome != b.outcome || a.ownerId != b.ownerId
            || a.statCount != b.statCount || a.haveLink != b.haveLink
            || a.linkType != b.linkType || a.linkId != b.linkId) {
        return false;
    }
    for (std::size_t i = 0U; i < a.statCount; ++i) {
        if (a.statIds[i] != b.statIds[i] || a.statValues[i] != b.statValues[i]) {
            return false;
        }
    }
    return true;
}

void FormatTrace(const PairTrace& trace, char* out, std::size_t size) noexcept {
    int used = std::snprintf(
        out, size, "row %d, unit %u: ", trace.classId, trace.unitId);
    if (used < 0 || static_cast<std::size_t>(used) >= size) return;
    std::size_t at = static_cast<std::size_t>(used);
    const auto add = [&](const char* format, auto... args) noexcept {
        if (at + 1U >= size) return;
        const int written = std::snprintf(out + at, size - at, format, args...);
        if (written > 0) at = std::min(size - 1U, at + static_cast<std::size_t>(written));
    };
    if (trace.outcome == PairOutcome::NoOwner) {
        if (trace.haveLink) {
            add("no player owner (owner link: type %d, id %u) -> capped",
                trace.linkType, trace.linkId);
        } else {
            add("%s", "no player owner (no owner link on the monster) -> capped");
        }
        return;
    }
    add("owner player unit %u", trace.ownerId);
    for (std::size_t i = 0U; i < trace.statCount; ++i) {
        add(", stat %d = %d", trace.statIds[i], trace.statValues[i]);
    }
    add("%s", trace.outcome == PairOutcome::Uncapped ? " -> uncapped" : " -> capped");
}

void RecordTrace(const PairTrace& trace) noexcept {
    bool changed = true;
    AcquireSRWLockExclusive(&TraceLock);
    PairTrace* slot = nullptr;
    for (auto& known : Traces) {
        if (known.unit == trace.unit && known.unitId == trace.unitId
                && known.classId == trace.classId) {
            slot = &known;
            break;
        }
    }
    if (slot != nullptr) {
        changed = !SameResult(*slot, trace);
        *slot = trace;
    } else {
        Traces[TraceNext] = trace;
        TraceNext = (TraceNext + 1U) % TraceSlots;
    }
    if (changed) {
        LastTrace = trace;
        HaveLastTrace = true;
    }
    ReleaseSRWLockExclusive(&TraceLock);
    if (changed && Context != nullptr) {
        char line[256]{};
        FormatTrace(trace, line, sizeof(line));
        char message[320]{};
        std::snprintf(message, sizeof(message), "SummonResistCap: %s.", line);
        Context->LogInfo(message);
    }
}

// True when a pair names this monstats row and the player who owns the monster
// has a non-zero total of that pair's stat. counted feeds the console counters
// (the gate's calls); the service's calls leave them alone. Every decision for
// a paired row goes to RecordTrace.
auto OwnerStatUncaps(
        void* monster, std::int32_t classId, bool counted) noexcept -> bool {
    if (GetMinionOwner == nullptr || GetUnitStat == nullptr) return false;
    PairTrace trace{};
    void* player = nullptr;
    bool checked = false;
    bool uncapped = false;
    for (std::size_t i = 0U; i < Settings.uncapPairCount; ++i) {
        const UncapPair& pair = Settings.uncapPairs[i];
        if (pair.monsterId != classId) continue;
        if (!checked) {
            checked = true;
            trace.unit = monster;
            trace.unitId = ReadField<std::uint32_t>(monster, UnitIdOffset);
            trace.classId = classId;
            if (counted) PairChecks.fetch_add(1U, std::memory_order_relaxed);
            player = OwningPlayer(monster);
            if (player == nullptr) {
                if (counted) PairNoOwner.fetch_add(1U, std::memory_order_relaxed);
                trace.outcome = PairOutcome::NoOwner;
                trace.haveLink = ReadOwnerLink(monster, trace.linkType, trace.linkId);
                RecordTrace(trace);
                return false;
            }
            trace.ownerId = ReadField<std::uint32_t>(player, UnitIdOffset);
        }
        const std::int32_t value = GetUnitStat(player, pair.statId, 0U);
        if (trace.statCount < TraceStats) {
            trace.statIds[trace.statCount] = pair.statId;
            trace.statValues[trace.statCount] = value;
            ++trace.statCount;
        }
        if (value != 0) {
            uncapped = true;
            break;
        }
    }
    if (!checked) return false;
    trace.outcome = uncapped ? PairOutcome::Uncapped : PairOutcome::StatZero;
    RecordTrace(trace);
    return uncapped;
}

// ------------------------------------------------- bound demon stats row
// Bound Demon Scaling's "stats-row" service, in the layout its provider
// publishes (celestialrayone.bound-demon-scaling 1.0.0 and later).
struct StatsRowService {
    using StatsRowFn = std::int32_t(__cdecl*)(void* unit) noexcept;

    std::uint32_t serviceSize;
    std::uint32_t serviceVersion;
    // Game thread, server unit: the monstats row a bound demon takes its stats
    // from, -1 for every other unit.
    StatsRowFn statsRow;
};
static_assert(sizeof(StatsRowService) == 16U);
static_assert(offsetof(StatsRowService, statsRow) == 8U);

constexpr char StatsRowProviderId[] = "celestialrayone.bound-demon-scaling";
constexpr char StatsRowServiceName[] = "stats-row";
constexpr std::uint32_t StatsRowServiceVersion = 1U;
constexpr std::uint64_t StatsRowRetryMs = 5000U;

// Game thread only, apart from the atomics the console reads.
D2RL::PluginCommunication::ServiceLease<StatsRowService> StatsRowLease;
std::uint64_t StatsRowLastAttempt{};
std::atomic<std::uint32_t> StatsRowLastResult{0xFFFFFFFFU};
std::atomic_bool StatsRowAcquired{};
std::atomic_bool StatsRowNoted{};
std::atomic<std::uint64_t> BoundDemonRows{};

// Acquires the service on first use and retries every few seconds while it is
// missing. Held until unload.
auto StatsRowProvider() noexcept -> const StatsRowService* {
    if (StatsRowLease) return StatsRowLease.Get();
    const std::uint64_t now = ::GetTickCount64();
    if (StatsRowLastAttempt != 0U && now - StatsRowLastAttempt < StatsRowRetryMs) {
        return nullptr;
    }
    StatsRowLastAttempt = now;
    const D2RL::PluginCommunicationService* communication = nullptr;
    if (Context->QueryService(&communication) != D2RL::ServiceQueryResult::Success
            || !D2RL::HasPluginCommunicationServiceField(
                communication, D2RL::PluginCommunicationServiceRequiredSize)) {
        StatsRowLastResult.store(static_cast<std::uint32_t>(
            D2RL::PluginCommunication::Result::Unavailable));
        return nullptr;
    }
    const auto result = D2RL::PluginCommunication::Acquire(Context, communication,
        StatsRowProviderId, StatsRowServiceName, StatsRowServiceVersion,
        static_cast<std::uint32_t>(sizeof(StatsRowService)), &StatsRowLease);
    StatsRowLastResult.store(static_cast<std::uint32_t>(result));
    if (result != D2RL::PluginCommunication::Result::Success || !StatsRowLease
            || StatsRowLease->statsRow == nullptr) {
        StatsRowLease.Reset();
        if (!StatsRowNoted.exchange(true)) {
            char text[256]{};
            std::snprintf(text, sizeof(text),
                "SummonResistCap: Bound Demon Scaling's stats-row service is not "
                "available (result %u); bound demons are decided by their own "
                "monstats row.",
                static_cast<unsigned>(result));
            Context->LogInfo(text);
        }
        return nullptr;
    }
    StatsRowAcquired.store(true);
    Context->LogInfo(
        "SummonResistCap: using Bound Demon Scaling's stats-row service; bound "
        "demons are decided as its pseudo row.");
    return StatsRowLease.Get();
}

// The monstats row a unit's cap is decided by: a bound demon's pseudo row
// when Bound Demon Scaling names one, otherwise the unit's own class id.
auto StatsRowFor(void* unit) noexcept -> std::int32_t {
    if (const StatsRowService* service = StatsRowProvider(); service != nullptr) {
        const std::int32_t row = service->statsRow(unit);
        if (row >= 0) {
            BoundDemonRows.fetch_add(1U, std::memory_order_relaxed);
            return row;
        }
    }
    return GetUnitClassId(unit);
}

enum class MonsterCap : std::uint8_t { Skip, CapAlign, CapList, UncapPair };

// The cap decision for a monster defender: the one place it is made, for the
// gate and for the cap-decision service alike.
auto DecideMonster(
        void* monster, std::int32_t classId, bool counted) noexcept -> MonsterCap {
    bool byAlign = false;
    if (Settings.useAlignColumn) {
        const std::uint8_t* record = MonStatsRecordFor(monster, classId);
        byAlign = record != nullptr && record[MonStatsAlignOffset] == AlignAlly;
    }
    if (!byAlign && !IsListedAlly(classId)) return MonsterCap::Skip;
    // An uncap pair sends a capped monster to the skip target instead, the
    // same path every ordinary monster takes.
    if (OwnerStatUncaps(monster, classId, counted)) return MonsterCap::UncapPair;
    return byAlign ? MonsterCap::CapAlign : MonsterCap::CapList;
}

// Returns non-zero to run the engine's capping block, zero to skip it exactly
// as the original jnz did. Called from the relay with RCX = ctx.
auto EvaluateCapGate(void* ctx) noexcept -> std::int32_t {
    if (ctx == nullptr) return 0;

    auto* raw = reinterpret_cast<std::uint8_t*>(ctx);
    const std::uint32_t uncapped =
        *reinterpret_cast<const std::uint32_t*>(raw + UncappedFlagOffset);

    GateCalls.fetch_add(1U, std::memory_order_relaxed);

    // Vanilla capped path: a player. Nothing to decide.
    if (uncapped == 0U) {
        PlayerGates.fetch_add(1U, std::memory_order_relaxed);
        return 1;
    }
    if (!Operational.load(std::memory_order_acquire)
            || GetUnitType == nullptr || GetUnitClassId == nullptr) {
        return 0;
    }

    void* defender = *reinterpret_cast<void**>(raw + DefenderOffset);
    if (!IsPlausiblePointer(defender)) return 0;
    if (GetUnitType(defender) != UnitTypeMonster) return 0;

    const std::int32_t classId = StatsRowFor(defender);
    ObservedRow* row = NoteRow(defender, classId);

    switch (DecideMonster(defender, classId, true)) {
    case MonsterCap::CapAlign:
        AllyCaps.fetch_add(1U, std::memory_order_relaxed);
        return 1;
    case MonsterCap::CapList:
        ListCaps.fetch_add(1U, std::memory_order_relaxed);
        return 1;
    case MonsterCap::UncapPair:
        PairUncaps.fetch_add(1U, std::memory_order_relaxed);
        if (row != nullptr) ++row->pairUncaps;
        return 0;
    case MonsterCap::Skip:
        break;
    }
    MonsterSkips.fetch_add(1U, std::memory_order_relaxed);
    return 0;
}

// --------------------------------------------------- cap decision service
// Published as "cap-decision", version 1, by celestialrayone.summon-resist-cap.
// The layout is the contract: a consumer declares the same struct. A later
// version may only add fields at the end; a breaking change takes a new name.
struct CapDecisionService {
    using MonsterCapStateFn = std::int32_t(__cdecl*)(void* monster) noexcept;

    std::uint32_t serviceSize;
    std::uint32_t serviceVersion;
    // Game thread only: it reads server units, the owner chain and stats.
    // For a server monster unit, the decision the cap gate makes for it:
    //    1  its resistances are capped (an ally with no active uncap pair)
    //    0  they are not (an ordinary monster, or an ally whose owner has a
    //       paired stat)
    //   -1  this plugin is not deciding: disabled, not armed, or not a monster
    MonsterCapStateFn monsterCapState;
};
static_assert(sizeof(CapDecisionService) == 16U);
static_assert(offsetof(CapDecisionService, monsterCapState) == 8U);

constexpr char CapDecisionServiceName[] = "cap-decision";
constexpr std::uint32_t CapDecisionServiceVersion = 1U;

auto __cdecl MonsterCapState(void* monster) noexcept -> std::int32_t {
    if (!Operational.load(std::memory_order_acquire) || GetUnitType == nullptr
            || GetUnitClassId == nullptr || !IsPlausiblePointer(monster)
            || GetUnitType(monster) != UnitTypeMonster) {
        return -1;
    }
    ServiceQueries.fetch_add(1U, std::memory_order_relaxed);
    switch (DecideMonster(monster, StatsRowFor(monster), false)) {
    case MonsterCap::CapAlign:
    case MonsterCap::CapList:
        return 1;
    case MonsterCap::UncapPair:
    case MonsterCap::Skip:
        break;
    }
    return 0;
}

const CapDecisionService CapDecisionTable{
    sizeof(CapDecisionService),
    CapDecisionServiceVersion,
    &MonsterCapState,
};

auto PublishCapDecision() noexcept -> bool {
    const D2RL::PluginCommunicationService* communication = nullptr;
    if (Context->QueryService(&communication) != D2RL::ServiceQueryResult::Success
            || !D2RL::HasPluginCommunicationServiceField(
                communication, D2RL::PluginCommunicationServiceRequiredSize)
            || communication->publishService == nullptr) {
        return false;
    }
    const D2RL::PluginCommunication::PublishServiceRequest request{
        .structSize = D2RL::PluginCommunication::PublishServiceRequestSize,
        .flags = 0U,
        .name = CapDecisionServiceName,
        .serviceVersion = CapDecisionServiceVersion,
        .tableSize = static_cast<std::uint32_t>(sizeof(CapDecisionTable)),
        .table = &CapDecisionTable,
    };
    return communication->publishService(Context, &request)
        == D2RL::PluginCommunication::Result::Success;
}

// ------------------------------------------------------------------- relay

auto CanEncodeRel32(std::uintptr_t from, std::uintptr_t to) noexcept -> bool {
    const auto delta =
        static_cast<std::int64_t>(to) - static_cast<std::int64_t>(from);
    return delta >= static_cast<std::int64_t>(INT32_MIN)
        && delta <= static_cast<std::int64_t>(INT32_MAX);
}

auto AllocateNear(std::uintptr_t hint, std::size_t size) noexcept -> void* {
    SYSTEM_INFO systemInfo{};
    ::GetSystemInfo(&systemInfo);
    const auto granularity =
        static_cast<std::uintptr_t>(systemInfo.dwAllocationGranularity);
    if (granularity == 0U) return nullptr;

    const auto aligned = hint & ~(granularity - 1U);
    for (std::uintptr_t delta = granularity;
            delta < 0x70000000ULL; delta += granularity) {
        if (aligned > UINTPTR_MAX - delta) break;
        const auto candidate = aligned + delta;
        if (!CanEncodeRel32(hint, candidate)) break;
        if (auto* memory = ::VirtualAlloc(
                reinterpret_cast<void*>(candidate),
                size,
                MEM_COMMIT | MEM_RESERVE,
                PAGE_READWRITE)) {
            return memory;
        }
    }
    return nullptr;
}

void WriteRel32(
        std::uint8_t* relay,
        std::size_t slot,
        std::size_t nextInstruction,
        std::uintptr_t target) noexcept {
    const auto from =
        reinterpret_cast<std::uintptr_t>(relay) + nextInstruction;
    const auto displacement =
        static_cast<std::int32_t>(static_cast<std::int64_t>(target)
            - static_cast<std::int64_t>(from));
    std::memcpy(relay + slot, &displacement, sizeof(displacement));
}

auto InstallRelay() noexcept -> bool {
    const auto gateAddress = reinterpret_cast<std::uintptr_t>(Base) + CapGateRva;
    RelayPage = AllocateNear(gateAddress, RelaySize);
    if (RelayPage == nullptr) {
        Context->LogError(
            "SummonResistCap: no relay page was available within rel32 reach.");
        return false;
    }

    // The stack pointer is held in RBP across the callback. RBP is callee-saved
    // in the x64 calling convention, so EvaluateCapGate and every game function
    // it calls must hand it back unchanged. Up to 3.1.0 the relay held it in
    // R11, which is volatile: any callee may overwrite R11, and whenever one did,
    // the relay loaded that garbage into RSP and the next pop faulted with an
    // unusable stack, which Windows answers by ending the process with no crash
    // report. RBP itself is pushed first and popped before the branch, so the
    // host function gets its own RBP back on both exits.
    static constexpr std::uint8_t Template[RelaySize] = {
        0x9C,                                // pushfq
        0x50, 0x51, 0x52,                    // push rax, rcx, rdx
        0x41, 0x50, 0x41, 0x51,              // push r8, r9
        0x41, 0x52, 0x41, 0x53,              // push r10, r11
        0x55,                                // push rbp
        0x48, 0x89, 0xE5,                    // mov rbp, rsp
        0x48, 0x83, 0xE4, 0xF0,              // and rsp, -16
        0x48, 0x83, 0xEC, 0x20,              // sub rsp, 32
        0x48, 0x8B, 0xCE,                    // mov rcx, rsi
        0x48, 0xB8, 0, 0, 0, 0, 0, 0, 0, 0,  // mov rax, EvaluateCapGate
        0xFF, 0xD0,                          // call rax
        0x48, 0x89, 0xEC,                    // mov rsp, rbp
        0x5D,                                // pop rbp
        0x85, 0xC0,                          // test eax, eax
        0x74, 0x11,                          // jz  +17 -> skip epilogue
        0x41, 0x5B, 0x41, 0x5A, 0x41, 0x59,  // pop r11, r10, r9
        0x41, 0x58, 0x5A, 0x59, 0x58, 0x9D,  // pop r8, rdx, rcx, rax, popfq
        0xE9, 0, 0, 0, 0,                    // jmp CapBlockRva
        0x41, 0x5B, 0x41, 0x5A, 0x41, 0x59,  // pop r11, r10, r9
        0x41, 0x58, 0x5A, 0x59, 0x58, 0x9D,  // pop r8, rdx, rcx, rax, popfq
        0xE9, 0, 0, 0, 0,                    // jmp SkipCapRva
    };
    // Layout checks, so the slot constants can never drift from the bytes.
    static_assert(Template[RelayCallbackSlot - 2U] == 0x48
        && Template[RelayCallbackSlot - 1U] == 0xB8, "callback slot");
    static_assert(Template[RelayCapJumpSlot - 1U] == 0xE9
        && RelayCapJumpEnd == RelayCapJumpSlot + 4U, "cap jump slot");
    static_assert(Template[RelaySkipJumpSlot - 1U] == 0xE9
        && RelaySkipJumpEnd == RelaySkipJumpSlot + 4U
        && RelaySkipJumpEnd == RelaySize, "skip jump slot");
    static_assert(Template[RelayCapJumpSlot - 15U] == 0x74
        && Template[RelayCapJumpSlot - 14U] == RelayCapJumpEnd - (RelayCapJumpSlot - 13U),
        "jz lands on the skip epilogue");

    auto* relay = static_cast<std::uint8_t*>(RelayPage);
    std::memcpy(relay, Template, RelaySize);

    const auto callback = reinterpret_cast<std::uintptr_t>(&EvaluateCapGate);
    std::memcpy(relay + RelayCallbackSlot, &callback, sizeof(callback));
    WriteRel32(
        relay,
        RelayCapJumpSlot,
        RelayCapJumpEnd,
        reinterpret_cast<std::uintptr_t>(Base) + CapBlockRva);
    WriteRel32(
        relay,
        RelaySkipJumpSlot,
        RelaySkipJumpEnd,
        reinterpret_cast<std::uintptr_t>(Base) + SkipCapRva);

    DWORD previousProtection{};
    if (!::VirtualProtect(
            relay, RelaySize, PAGE_EXECUTE_READ, &previousProtection)) {
        Context->LogError(
            "SummonResistCap: relay page protection could not be finalized.");
        return false;
    }
    ::FlushInstructionCache(::GetCurrentProcess(), relay, RelaySize);

    const auto relayAddress = reinterpret_cast<std::uintptr_t>(relay);
    const auto baseAddress = reinterpret_cast<std::uintptr_t>(Base);
    const auto trampolineAddress = baseAddress + TrampolineRva;
    const auto trampolineDisplacement = static_cast<std::int64_t>(relayAddress)
        - static_cast<std::int64_t>(trampolineAddress + CapGatePatchSize);
    if (relayAddress < baseAddress || !CanEncodeRel32(gateAddress, relayAddress)
            || trampolineDisplacement < static_cast<std::int64_t>(INT32_MIN)
            || trampolineDisplacement > static_cast<std::int64_t>(INT32_MAX)) {
        Context->LogError(
            "SummonResistCap: relay displacement validation failed.");
        return false;
    }

    // "jmp relay" into the padding first. Nothing reaches it until the gate
    // below is aimed at it.
    std::array<std::uint8_t, CapGatePatchSize> trampoline{0xE9};
    const auto trampolineRel32 = static_cast<std::int32_t>(trampolineDisplacement);
    std::memcpy(trampoline.data() + 1, &trampolineRel32, sizeof(trampolineRel32));
    if (!Context->PatchBytes(
            TrampolineRva,
            TrampolineSlotExpected.data(),
            CapGatePatchSize,
            trampoline.data(),
            CapGatePatchSize)) {
        Context->LogError(
            "SummonResistCap: the trampoline at 0x4526B6 could not be written; "
            "plugin refused.");
        return false;
    }

    if (!Context->PatchJmpRel32(
            CapGateRva,
            CapGateExpected.data(),
            CapGatePatchSize,
            TrampolineRva,
            CapGatePatchSize)) {
        Context->LogError(
            "SummonResistCap: the cap gate at 0x45249C is already owned or does "
            "not match; plugin refused.");
        return false;
    }
    return true;
}

// ----------------------------------------------------------------- console

void DescribeGatePatch(char* out, std::size_t size) noexcept {
    const auto* gate = reinterpret_cast<const std::uint8_t*>(Base + CapGateRva);
    const char* state = "FOREIGN";
    if (gate[0] == 0x39 && gate[1] == 0x6E && gate[2] == 0x24
            && gate[3] == 0x75 && gate[4] == 0x57) {
        state = "VANILLA (our patch is NOT installed)";
    } else if (gate[0] == 0xE9) {
        std::int32_t displacement{};
        std::memcpy(&displacement, gate + 1, sizeof(displacement));
        const auto target =
            reinterpret_cast<std::uintptr_t>(gate + 5) + displacement;
        const auto* trampoline = Base + TrampolineRva;
        state = "FOREIGN (jmp elsewhere)";
        if (target == reinterpret_cast<std::uintptr_t>(trampoline)
                && trampoline[0] == 0xE9) {
            std::int32_t hop{};
            std::memcpy(&hop, trampoline + 1, sizeof(hop));
            const auto relay =
                reinterpret_cast<std::uintptr_t>(trampoline + 5) + hop;
            if (relay == reinterpret_cast<std::uintptr_t>(RelayPage)) state = "LIVE";
        }
    }
    std::snprintf(
        out,
        size,
        "%02X %02X %02X %02X %02X = %s",
        gate[0], gate[1], gate[2], gate[3], gate[4], state);
}

// Appends to a fixed console buffer and never runs past its end.
template <typename... Args>
void Append(
        char* buffer,
        std::size_t size,
        std::size_t& used,
        const char* format,
        Args... args) noexcept {
    if (used + 1U >= size) return;
    const int written =
        std::snprintf(buffer + used, size - used, format, args...);
    if (written <= 0) return;
    used += static_cast<std::size_t>(written);
    if (used >= size) used = size - 1U;
}

auto Status(
        D2R::Game::Client*,
        const D2RL::ConsoleCommandContext* command,
        void*) noexcept -> D2RL::ConsoleCommandResult {
    if (command == nullptr || command->plugin == nullptr) {
        return D2RL::ConsoleCommandResult::Failed;
    }
    char gate[128]{};
    DescribeGatePatch(gate, sizeof(gate));
    char message[2048]{};
    std::size_t used = 0U;
    Append(
        message,
        sizeof(message),
        used,
        "Summon Resist Cap 3.4.0: active=%s; build=%s; gate 0x45249C: %s; "
        "gate calls=%llu; players=%llu; capped=%llu by Align / %llu by list; "
        "skipped=%llu; uncap pairs: checked=%llu, no player owner=%llu, "
        "uncapped=%llu; cap-decision service: %s, %llu queries; Align "
        "column=%s; extra rows=%zu:",
        Operational.load(std::memory_order_acquire) ? "true" : "false",
        RuntimeBuild.c_str(),
        gate,
        static_cast<unsigned long long>(GateCalls.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(PlayerGates.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(AllyCaps.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(ListCaps.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(MonsterSkips.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(PairChecks.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(PairNoOwner.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(PairUncaps.load(std::memory_order_relaxed)),
        ServicePublished.load(std::memory_order_acquire) ? "published"
                                                         : "not published",
        static_cast<unsigned long long>(ServiceQueries.load(std::memory_order_relaxed)),
        Settings.useAlignColumn ? "on" : "off",
        Settings.allyIdCount);
    if (Settings.allyIdCount == 0U) {
        Append(message, sizeof(message), used, "%s", " none");
    }
    for (std::size_t i = 0U; i < Settings.allyIdCount && i < 24U; ++i) {
        Append(message, sizeof(message), used, " %d", Settings.allyIds[i]);
    }
    Append(
        message, sizeof(message), used, "; pairs=%zu:", Settings.uncapPairCount);
    if (Settings.uncapPairCount == 0U) {
        Append(message, sizeof(message), used, "%s", " none");
    }
    for (std::size_t i = 0U; i < Settings.uncapPairCount && i < 32U; ++i) {
        Append(
            message,
            sizeof(message),
            used,
            " [%d, %d]",
            Settings.uncapPairs[i].monsterId,
            Settings.uncapPairs[i].statId);
    }
    if (Settings.uncapPairCount > 32U) {
        Append(message, sizeof(message), used, "%s", " ...");
    }
    {
        PairTrace last{};
        bool have = false;
        AcquireSRWLockShared(&TraceLock);
        last = LastTrace;
        have = HaveLastTrace;
        ReleaseSRWLockShared(&TraceLock);
        if (have) {
            char line[256]{};
            FormatTrace(last, line, sizeof(line));
            Append(message, sizeof(message), used, "; last pair check: %s", line);
        } else if (Settings.uncapPairCount > 0U) {
            Append(message, sizeof(message), used, "%s",
                "; last pair check: none yet (no paired summon checked)");
        }
    }
    Append(
        message, sizeof(message), used, "; config=%s.", LoadedConfigPath.c_str());
    Append(
        message,
        sizeof(message),
        used,
        "; bound demons: stats-row service %s (last result %u), %llu decisions "
        "made as the pseudo row",
        StatsRowAcquired.load(std::memory_order_acquire) ? "in use" : "not available",
        StatsRowLastResult.load(std::memory_order_relaxed),
        static_cast<unsigned long long>(BoundDemonRows.load(std::memory_order_relaxed)));
    command->plugin->WriteConsoleMessage(message);
    return D2RL::ConsoleCommandResult::Handled;
}

auto Probe(
        D2R::Game::Client*,
        const D2RL::ConsoleCommandContext* command,
        void*) noexcept -> D2RL::ConsoleCommandResult {
    if (command == nullptr || command->plugin == nullptr) {
        return D2RL::ConsoleCommandResult::Failed;
    }
    const std::size_t count = ObservedCount.load(std::memory_order_acquire);
    char message[2400]{};
    std::size_t used = 0U;
    Append(
        message,
        sizeof(message),
        used,
        "monstats rows seen at the resistance resolver: %zu",
        count);
    if (count == 0U) {
        Append(
            message,
            sizeof(message),
            used,
            "%s",
            ". Nothing has resolved damage yet, so no rows are known.");
        command->plugin->WriteConsoleMessage(message);
        return D2RL::ConsoleCommandResult::Handled;
    }
    for (std::size_t i = 0U; i < count && i < MaxObservedRows; ++i) {
        const ObservedRow& row = Observed[i];
        const bool capped =
            (row.alignByte == AlignAlly && Settings.useAlignColumn)
            || IsListedAlly(row.classId);
        Append(
            message,
            sizeof(message),
            used,
            " | row %d ai=%d byte0x87=%u hits=%llu%s",
            row.classId,
            row.ai,
            row.alignByte,
            static_cast<unsigned long long>(row.hits),
            capped ? " CAPPED" : "");
        if (HasUncapPair(row.classId)) {
            Append(
                message,
                sizeof(message),
                used,
                " (paired, uncapped %llu times)",
                static_cast<unsigned long long>(row.pairUncaps));
        }
    }
    command->plugin->WriteConsoleMessage(message);
    return D2RL::ConsoleCommandResult::Handled;
}

}  // namespace

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept
        -> const D2RL::PluginInfo* {
    return &Info;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(
        const D2RL::PluginContext* context) noexcept -> bool {
    if (!D2RL::HasContext(context)
            || context->abiVersion != D2RL_PLUGIN_ABI_VERSION) {
        return false;
    }
    Context = context;
    Base = reinterpret_cast<std::uint8_t*>(context->exeBase);
    if (Base == nullptr || !LoadConfig()) return false;

    const auto* runtimeBuild = D2RL::GetBuildName(context);
    if (runtimeBuild != nullptr && runtimeBuild[0] != '\0') {
        RuntimeBuild = runtimeBuild;
    }

    if (!context->RegisterConsoleCommand(
            "summon-resist-cap",
            Status,
            "Show Summon Resist Cap settings and diagnostic counters.")) {
        context->LogWarn(
            "SummonResistCap: optional status command was not registered.");
    }
    if (!context->RegisterConsoleCommand(
            "summon-resist-probe",
            Probe,
            "List the monstats rows that reached the resistance resolver.")) {
        context->LogWarn(
            "SummonResistCap: optional probe command was not registered.");
    }

    if (!Settings.enabled) {
        context->LogInfo(
            "SummonResistCap: loaded disabled; no patch was installed.");
        return true;
    }

    if (!VerifyBytes(
            GetUnitTypeRva,
            GetUnitTypeExpected.data(),
            GetUnitTypeExpected.size(),
            "UNITS_GetUnitType")
        || !VerifyBytes(
            CapGateRva,
            CapGateExpected.data(),
            CapGateExpected.size(),
            "the resistance cap gate")
        || !VerifyBytes(
            TrampolineRva,
            TrampolineRunExpected.data(),
            TrampolineRunExpected.size(),
            "the int3 padding after the resolver")) {
        return false;
    }
    GetUnitType = At<GetUnitTypeFn>(GetUnitTypeRva);
    GetUnitClassId = At<GetUnitClassIdFn>(GetUnitClassIdRva);
    GetUnitDataContext = At<GetUnitDataContextFn>(GetUnitDataContextRva);
    MonStatsGetRecord = At<MonStatsGetRecordFn>(MonStatsGetRecordRva);

    // The owner lookup and the stat read run only for uncap pairs, so they are
    // required only when pairs are configured.
    if (Settings.uncapPairCount > 0U) {
        if (!VerifyBytes(
                GetMinionOwnerRva,
                GetMinionOwnerExpected.data(),
                GetMinionOwnerExpected.size(),
                "D2GAME_GetMinionOwner")
            || !VerifyBytes(
                GetUnitStatRva,
                GetUnitStatThunkJump.data(),
                GetUnitStatThunkJump.size(),
                "the loader's stat thunk")
            || !VerifyBytes(
                GetUnitStatThunkNopsRva,
                GetUnitStatThunkNops.data(),
                GetUnitStatThunkNops.size(),
                "the loader's stat thunk padding")
            || !VerifyBytes(
                GetUnitStatBodyRva,
                GetUnitStatBody.data(),
                GetUnitStatBody.size(),
                "STATLIST_GetUnitStat")) {
            return false;
        }
        GetMinionOwner = At<GetMinionOwnerFn>(GetMinionOwnerRva);
        GetUnitStat = At<GetUnitStatFn>(GetUnitStatRva);
    }

    // Armed before the patch lands: the relay can be entered the instant the
    // bytes change, and an unarmed callback would report vanilla behaviour.
    Operational.store(true, std::memory_order_release);
    if (!InstallRelay()) {
        Operational.store(false, std::memory_order_release);
        return false;
    }
    // The cap works without it; only plugins asking for the decision (Monster
    // Display) lose it.
    ServicePublished.store(PublishCapDecision(), std::memory_order_release);
    if (!ServicePublished.load(std::memory_order_acquire)) {
        context->LogWarn(
            "SummonResistCap: the cap-decision service could not be published; "
            "Monster Display cannot tell which monsters are capped.");
    }

    {
        char pairs[1024]{};
        std::size_t used = 0U;
        Append(pairs, sizeof(pairs), used, "SummonResistCap: uncap pairs read from "
            "the TOML: %zu", Settings.uncapPairCount);
        if (Settings.uncapPairCount == 0U) Append(pairs, sizeof(pairs), used, "%s", " (none)");
        for (std::size_t i = 0U; i < Settings.uncapPairCount && i < 48U; ++i) {
            Append(pairs, sizeof(pairs), used, " [%d, %d]",
                Settings.uncapPairs[i].monsterId, Settings.uncapPairs[i].statId);
        }
        Append(pairs, sizeof(pairs), used, "%s", ".");
        context->LogInfo(pairs);
    }

    char message[512]{};
    std::snprintf(
        message,
        sizeof(message),
        "Summon Resist Cap 3.4.0 active for observed D2R %s; Align column=%s, "
        "%zu extra rows, %zu uncap pairs; cap gate 0x45249C relayed; function "
        "entry 0x4523E0 left untouched; cap-decision service %s; config=%s.",
        RuntimeBuild.c_str(),
        Settings.useAlignColumn ? "on" : "off",
        Settings.allyIdCount,
        Settings.uncapPairCount,
        ServicePublished.load(std::memory_order_acquire) ? "published"
                                                         : "not published",
        LoadedConfigPath.c_str());
    context->LogInfo(message);
    return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
    // The relay stays mapped and the gate keeps calling it, so the callback
    // must remain valid. Disarmed, it reproduces vanilla behaviour exactly,
    // and the cap-decision service answers -1.
    Operational.store(false, std::memory_order_release);
}
