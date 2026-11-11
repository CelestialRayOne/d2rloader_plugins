#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <D2RLPlugin/api.h>

#include <Windows.h>

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace {

// ---------------------------------------------------------------------------
// Native contract, D2RLoader 1.3.0 (D2R 3.3 image at base 0x140000000 plus
// D2RCore.dll). Re-derived from both binaries.
//
// Crushing Blow lives in D2RCore now. In 1.2.x the game image held the core
// (0x581BD0) and its two callers. In 1.3.0:
//
//   - 0x581BD0 is a thunk to D2RCore!ApplyWideCrushingBlow, and neither of
//     its game-image callers is reached any more: EventFunc16 at 0x583150 is
//     no longer registered, and 0x51D950 is dead code behind the Mirrored
//     Blades thunk at 0x51D7B0.
//     The event registrar 0x438230 is a thunk to D2RCore!RegisterWideSkillEffect,
//     which stores D2RCore's own handler for every event function (its table
//     entry 16 is D2RCore's Crushing Blow handler). The game image's handler
//     table is only read by dead code left behind the loader's thunks.
//
//   - The live paths are all inside D2RCore and all end in one internal
//     function, the Crushing Blow core:
//         D2RCore Crushing Blow event handler  (4 / 10 / 10, ranged = event 6)
//         D2RCore Mirrored Blades effect       (skill-calc divisors)
//         D2RCore!ApplyWideCrushingBlow export (the game-image thunk)
//     The first two call the core directly, so a hook on the game image
//     never sees them. This plugin therefore hooks the D2RCore core itself.
//     The loader's plugin hooks only address the game image, so this one hook
//     is installed by the plugin: the core is located through the export,
//     whose stub ends in `call core`, and both are byte-checked first.
//
// The core keeps the vanilla ABI and params block:
//     int32 __fastcall (context, pAttacker, pTarget, CrushingBlowParams*)
// and does, in order:
//     player                     -> divisor = params.playerDivisor
//     monster, hireling          -> divisor = params.hirelingDivisor
//     monster, monstats boss     -> divisor = params.monsterDivisor * 2
//        or MonTypeFlag 0x02
//     monster, anything else     -> divisor = params.monsterDivisor
//     monster (non hireling)     -> divisor += divisor * playersBonus / 100
//     params.rangedEvent         -> divisor *= 2
//     damage  = currentLife / divisor
//     damage -= damage * min(damageresist, 100) / 100
//     life    = max(life - damage, 0)
//
// This plugin only rewrites the params block before handing the call to the
// original. Every later step, including the boss doubling, the /players
// scaling, physical damage reduction, the kill flag and overlay 147, stays
// native. Those steps are multiplicative, so the result keeps exactly the
// configured ratio against vanilla.
//
// The classification helpers below are native game-image functions; the
// D2RCore core calls the same three monster queries.
//
// Crushing Blow effectiveness stat (1.2.0). [effectiveness] stat_id names an
// itemstatcost row. On every Crushing Blow the hook reads it from pAttacker
// with 0x2F5020, the game-image thunk to D2RCore!ReadWideUnitStat, called as
// (unit, stat, layer 0): the same total stat('x'.accr) reads. It takes the
// layer as a full 32-bit register and answers 0 for stat ids from 0x8000 up.
//
// The stat cannot go into the divisor: the core divides current life by an
// integer, so 8 * 100 / 125 = 6.4 would round to 6 and turn +25 % into +33 %
// against a monster at 50. The core therefore runs at the class divisor
// exactly as before, and the stat then adds its share of the life the core
// actually removed (after damage reduction), in 64 bits:
//     extra = removed * value / 100,  life = max(life - extra, 0)
// written with 0x2F7D10, the thunk to D2RCore!SetWideUnitStat
// (unit, stat, value, layer), on stat 6 hitpoints layer 0, the stat and
// writer the core itself uses. The core's own end steps are kept: life floors
// at 0, and the will-die bit (0x0002 in the D2DamageStrc result flags at
// +0x04, the word the critical-strike-damage plugin's verified 0x44C3C8 block
// also writes) is set when the stat's share kills, or put back to its state
// before the core when a negative value leaves alive a target the core had
// killed. Overlay 147 and the kill credit stay the core's.
//
// pAttacker is the unit that rolled the Crushing Blow chance. Both game-image
// paths D2RCore reimplements show it, read on the 1.3.1 image:
//   EventFunc16 0x583150: the chance is read at 0x583194 with
//     0x2F5C60(rdi, ...), and the core is called at 0x583257 with rdx = rdi.
//   Mirrored Blades 0x51D7B0: the chance is read at 0x51D846 with
//     0x2F5C60(rdi, ...), and the core is called at 0x51D950 with rdx = rdi.
// A missile hit fires the damage event on the missile's owner (D2MOO
// MissMode.cpp: SUNITDMG_ExecuteEvents(pGame, pOwner, ...)), so ranged
// Crushing Blow reads the shooter's value.
//
// Re-checked on D2RLoader 1.3.1: the four helpers, the 0x581BD0 thunk and
// the 0x2F5020 reader match the game image byte for byte. The D2RCore core is
// still checked at load, as before.
// ---------------------------------------------------------------------------

constexpr std::uintptr_t GetUnitTypeRva           = 0x34B9D0;
constexpr std::uintptr_t GetHirelingTypeIdRva     = 0x3AF240;
constexpr std::uintptr_t IsBossRva                = 0x3AEFF0;
constexpr std::uintptr_t CheckMonTypeFlagRva      = 0x38E870;
constexpr std::uintptr_t GetUnitStatRva           = 0x2F5020;
constexpr std::uintptr_t SetUnitStatRva           = 0x2F7D10;

constexpr wchar_t CoreModuleName[] = L"D2RCore.dll";
constexpr char    CoreExportName[] = "ApplyWideCrushingBlow";

// D2RCore!ApplyWideCrushingBlow, 64 bytes:
//   +00 sub rsp,38h / +04 mov rax,[rip+cookie] / +0B xor rax,rsp /
//   +0E mov [rsp+30h],rax / +13 null checks of rcx, r8, r9 / +2F jne +40 /
//   +31 mov [rsp+28h],rcx / +36 lea rcx,[rsp+28h] / +3B call core
// The cookie displacement (+07) and the call rel32 (+3C) move between loader
// builds and are not pinned; the call target is decoded from them.
constexpr std::array<std::uint8_t, 64> CoreExportStub{
    0x48, 0x83, 0xEC, 0x38, 0x48, 0x8B, 0x05, 0x00,
    0x00, 0x00, 0x00, 0x48, 0x31, 0xE0, 0x48, 0x89,
    0x44, 0x24, 0x30, 0x48, 0x85, 0xC9, 0x0F, 0x94,
    0xC0, 0x4D, 0x85, 0xC0, 0x41, 0x0F, 0x94, 0xC2,
    0x41, 0x08, 0xC2, 0x4D, 0x85, 0xC9, 0x41, 0x0F,
    0x94, 0xC3, 0x31, 0xC0, 0x45, 0x08, 0xD3, 0x75,
    0x0F, 0x48, 0x89, 0x4C, 0x24, 0x28, 0x48, 0x8D,
    0x4C, 0x24, 0x28, 0xE8, 0x00, 0x00, 0x00, 0x00,
};
constexpr std::size_t CoreExportCookieOffset = 0x07;
constexpr std::size_t CoreExportCallOffset   = 0x3B;
constexpr std::size_t CoreExportCallEnd      = 0x40;

// The core's first 22 bytes: push r15 / push r14 / push r13 / push r12 /
// push rsi / push rdi / push rbp / push rbx / sub rsp,68h / mov r14,r9 /
// mov rsi,r8. The first 16 are position independent and are the ones the
// hook replaces with jmp qword ptr [rip+0] and two nops.
constexpr std::array<std::uint8_t, 22> CorePrologue{
    0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54,
    0x56, 0x57, 0x55, 0x53, 0x48, 0x83, 0xEC, 0x68,
    0x4D, 0x89, 0xCE, 0x4C, 0x89, 0xC6,
};
constexpr std::size_t CorePatchSize = 16;

// The params block as the core reads it, each instruction byte-checked:
//   +025 mov ebx,[r9+8]                       monster divisor
//   +039 mov ebx,[r14+0Ch]                    player divisor
//   +052 mov ebx,[r14+10h]                    hireling divisor
//   +108 movzx ecx,byte [r14+14h] / shl ebx,cl ranged doubling
//   +1D7 mov rax,[r14]                        damage, receives the kill flag
struct CoreWitness {
    std::size_t                 offset;
    std::array<std::uint8_t, 7> bytes;
    std::size_t                 size;
};
constexpr std::array<CoreWitness, 5> CoreParamsWitnesses{{
    {0x025, {0x41, 0x8B, 0x59, 0x08}, 4},
    {0x039, {0x41, 0x8B, 0x5E, 0x0C}, 4},
    {0x052, {0x41, 0x8B, 0x5E, 0x10}, 4},
    {0x108, {0x41, 0x0F, 0xB6, 0x4E, 0x14, 0xD3, 0xE3}, 7},
    {0x1D7, {0x49, 0x8B, 0x06}, 3},
}};

constexpr std::size_t TrampolinePageBytes = 4'096;
constexpr std::array<std::uint8_t, 16> GetUnitTypeExpected{
    0x48, 0x83, 0xEC, 0x28, 0x48, 0x85, 0xC9, 0x75,
    0x1D, 0x88, 0x4C, 0x24, 0x30, 0x48, 0x8D, 0x4C,
};
constexpr std::array<std::uint8_t, 16> GetHirelingTypeIdExpected{
    0x40, 0x56, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B,
    0xF1, 0xE8, 0x82, 0xC7, 0xF9, 0xFF, 0x83, 0xF8,
};
constexpr std::array<std::uint8_t, 16> IsBossExpected{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83,
    0xEC, 0x20, 0x48, 0x8B, 0xFA, 0x48, 0x8B, 0xD9,
};
constexpr std::array<std::uint8_t, 16> CheckMonTypeFlagExpected{
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x83,
    0xEC, 0x20, 0x0F, 0xB7, 0xFA, 0x48, 0x8B, 0xD9,
};

// 0x2F5020 (unit in rcx, stat id in edx, layer in r8d), a loader import thunk:
//   2F5020  FF 25 xx xx xx xx   jmp [rip+disp32]
//   2F5026  90 90 90 90         nop x4
//   2F502A  48 89 74 24 20 ...  the untouched remainder of the old body
// disp32 locates the loader's import slot and moves between loader builds,
// so it is not pinned. Only checked while the stat is on.
constexpr std::uintptr_t GetUnitStatNopsRva = GetUnitStatRva + 6;
constexpr std::uintptr_t GetUnitStatBodyRva = GetUnitStatRva + 10;
constexpr std::array<std::uint8_t, 2> GetUnitStatThunkJump{0xFF, 0x25};
constexpr std::array<std::uint8_t, 4> GetUnitStatThunkNops{
    0x90, 0x90, 0x90, 0x90,
};
// mov [rsp+20h],rsi / push rdi / sub rsp,20h / movzx ebp,r8w / mov edi,edx /
// mov rbx,rcx / test rcx,rcx
constexpr std::array<std::uint8_t, 22> GetUnitStatBody{
    0x48, 0x89, 0x74, 0x24, 0x20, 0x57, 0x48, 0x83,
    0xEC, 0x20, 0x41, 0x0F, 0xB7, 0xE8, 0x8B, 0xFA,
    0x48, 0x8B, 0xD9, 0x48, 0x85, 0xC9,
};

// 0x2F7D10, STATLIST_SetUnitStat, the same 10-byte loader thunk shape
// (jmp [rip+disp32] / nop x4) into D2RCore!SetWideUnitStat. Only checked
// while the stat is on.
constexpr std::uintptr_t SetUnitStatNopsRva = SetUnitStatRva + 6;

constexpr std::int32_t UnitTypePlayer  = 0;
constexpr std::int32_t UnitTypeMonster = 1;

constexpr std::uint16_t MonTypeFlagSuperUnique = 0x0002;
constexpr std::uint16_t MonTypeFlagChampion    = 0x0004;
constexpr std::uint16_t MonTypeFlagUnique      = 0x0008;
constexpr std::uint16_t MonTypeFlagMinion      = 0x0010;

// Stat 6 hitpoints, and the D2DamageStrc result flags word with its will-die
// bit, which the core sets when Crushing Blow takes the last of the life.
constexpr std::int32_t  StatHitpoints           = 6;
constexpr std::size_t   DamageResultFlagsOffset = 0x04;
constexpr std::uint16_t DamageFlagWillDie       = 0x0002;

#pragma pack(push, 8)
struct CrushingBlowParams {
    void*        damage;          // +0x00 D2DamageStrc, receives the kill flag
    std::int32_t monsterDivisor;  // +0x08 vanilla 4
    std::int32_t playerDivisor;   // +0x0C vanilla 10
    std::int32_t hirelingDivisor; // +0x10 vanilla 10
    std::uint8_t rangedEvent;     // +0x14 set when the event is domissiledamage
};
#pragma pack(pop)

static_assert(offsetof(CrushingBlowParams, monsterDivisor) == 0x08);
static_assert(offsetof(CrushingBlowParams, playerDivisor) == 0x0C);
static_assert(offsetof(CrushingBlowParams, hirelingDivisor) == 0x10);
static_assert(offsetof(CrushingBlowParams, rangedEvent) == 0x14);

using ApplyCrushingBlowCoreFn =
    std::int32_t(__fastcall*)(void*, void*, void*, CrushingBlowParams*) noexcept;
using GetUnitTypeFn       = std::int32_t(__fastcall*)(void*) noexcept;
using GetHirelingTypeIdFn = std::int32_t(__fastcall*)(void*) noexcept;
using IsBossFn            = std::uint8_t(__fastcall*)(void*, void*) noexcept;
using CheckMonTypeFlagFn  = std::uint8_t(__fastcall*)(void*, std::uint16_t) noexcept;
// The layer is a full 32-bit argument: D2RCore's reader keys its lookup on r8d.
using GetUnitStatFn       =
    std::int32_t(__fastcall*)(void*, std::int32_t, std::uint32_t) noexcept;
using SetUnitStatFn       =
    bool(__fastcall*)(void*, std::uint32_t, std::int32_t, std::uint32_t) noexcept;

// ---------------------------------------------------------------------------
// Target classes
// ---------------------------------------------------------------------------

enum class TargetClass : std::size_t {
    Monster = 0,
    Champion,
    Unique,
    SuperUnique,
    ActBoss,
    Minion,
    Mercenary,
    Player,
    Count,
};

constexpr std::size_t TargetClassCount =
    static_cast<std::size_t>(TargetClass::Count);

constexpr std::array<const char*, TargetClassCount> TargetClassKeys{
    "monster", "champion", "unique", "super_unique",
    "act_boss", "minion", "mercenary", "player",
};

// Divisor the engine ends up using for each class in vanilla, melee hit, one
// player in the game. Used only for the console readout.
constexpr std::array<std::int32_t, TargetClassCount> VanillaDivisor{
    4, 4, 4, 8, 8, 4, 10, 10,
};

// Percentages are stored in hundredths of a percent: 100.00% == 10000.
constexpr std::int32_t PercentScale         = 10000;
constexpr std::int32_t PercentMaximum       = 1000 * 100;
constexpr std::int32_t MaximumDivisor       = 1 << 24;
constexpr std::int32_t MaximumBase          = 1000000;
constexpr std::int32_t VanillaRangedPercent = 5000;

// [effectiveness] stat_id. -1 is off; D2RCore's reader answers 0 from 0x8000 up.
constexpr std::int32_t StatIdOff     = -1;
constexpr std::int32_t MaximumStatId = 0x7FFF;

// The stat's effect in plain percent, 100 == unchanged. The bonus is capped so
// 100 + value fits in 32 bits and removed * value stays far inside 64 bits;
// any value that high already takes all current life.
constexpr std::int32_t EffectNeutral    = 100;
constexpr std::int32_t MaximumStatBonus = 1'000'000;

struct Config {
    bool         enabled = true;
    std::int32_t percent[TargetClassCount]{
        5000,  // monster
        5000,  // champion
        5000,  // unique
        1600,  // super_unique
        1600,  // act_boss
        5000,  // minion
        5000,  // mercenary
        5000,  // player
    };
    std::int32_t rangedPercent = VanillaRangedPercent;
    std::int32_t statId        = StatIdOff;
};

constexpr char DefaultConfig[] = R"toml(# Crushing Blow
# Crushing Blow removes a share of the target's CURRENT life on hit. This file
# scales that share per target class. Every value is a percentage of what the
# same hit would have removed in vanilla Diablo II, so 100 keeps a class
# exactly as the game shipped it and 25 makes Crushing Blow four times weaker
# against it.
#
# The classes do not stack. A monster is matched once, in this order:
#   mercenary -> act_boss -> super_unique -> unique -> champion -> minion
#   -> monster. Player characters are handled separately.
#
# Vanilla share of current life per class, melee hit, single player game:
#   monster       25 %      champion      25 %
#   unique        25 %      super_unique  12.5 %
#   act_boss      12.5 %    minion        25 %
#   mercenary     10 %      player        10 %

# File format version. Leave this value unchanged.
config_version = 1

# Turn the whole plugin off without removing the DLL.
enabled = true

[targets]
# Ordinary enemies with no rarity flag.
# 50 -> 12.5 % of current life.
monster = 50

# Blue champion monsters. Vanilla treats them as ordinary monsters.
# 50 -> 12.5 % of current life.
champion = 50

# Random yellow-name pack leaders. Vanilla treats them as ordinary monsters.
# 50 -> 12.5 % of current life.
unique = 50

# Named boss monsters such as Rakanishu, Pindleskin or The Countess.
# 16 -> 2 % of current life.
super_unique = 16

# Anything flagged boss in monstats.txt, which is the act bosses.
# 16 -> 2 % of current life.
act_boss = 16

# Minions that spawned alongside a unique or champion pack leader.
# 50 -> 12.5 % of current life.
minion = 50

# Hirelings, yours and anyone else's.
# 50 -> 5 % of current life.
mercenary = 50

# Player characters, which is what matters in PvP.
# 50 -> 5 % of current life.
player = 50

[attack]
# Crushing Blow rolled from a missile or thrown hit, as a percentage of the
# melee result against the same target. Vanilla halves it, which is 50. Set
# 100 to make ranged Crushing Blow as strong as melee.
ranged = 50

[effectiveness]
# An itemstatcost.txt stat that makes Crushing Blow stronger against every
# target class at once. Its value is a percent increase on top of [targets]
# and [attack]: at 25, every share above is multiplied by 1.25, so a monster
# at 50 goes from 12.5 % to 15.6 % of current life, and an act boss at 16
# goes from 2 % to 2.5 %. Negative values weaken it the same way, and at -100
# or lower the attacker's Crushing Blow does nothing.
#
# The value is read from the unit that landed the Crushing Blow, the way
# stat('...'.accr) reads it: gear, charms, skills and states all count. A
# summon or a hireling uses its own value, not its owner's. Ranged Crushing
# Blow counts too, because the game credits a missile's hit to its shooter.
#
# The itemstatcost.txt row of the stat, 0 to 32767. -1 leaves it off.
stat_id = -1

# Behaviour that is deliberately left native:
#   - The /players setting still divides Crushing Blow further against
#     monsters, exactly as it does in vanilla.
#   - Physical damage reduction on the target still reduces the result.
#
# Other notes:
#   - A value of 0 disables Crushing Blow against that class completely: no
#     life loss, no overlay, no kill credit.
#   - Values above 100 are allowed and make Crushing Blow stronger than
#     vanilla.
#   - One decimal place is accepted, for example 12.5.
#   - Type crushing-blow in the console to print the active table.

# Lets D2RLoader verify that multiplayer peers use matching settings.
[d2rl]
match = true
)toml";

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

const D2RL::PluginContext* Context{};
std::uint8_t*              Base{};
Config                     Settings{};
std::string                ConfigOrigin{"embedded defaults"};

ApplyCrushingBlowCoreFn OriginalCore{};
GetUnitTypeFn           GetUnitType{};
GetHirelingTypeIdFn     GetHirelingTypeId{};
IsBossFn                IsBoss{};
CheckMonTypeFlagFn      CheckMonTypeFlag{};
GetUnitStatFn           GetUnitStat{};
SetUnitStatFn           SetUnitStat{};

std::atomic_bool           Operational{};
std::uint8_t*              CoreFunction{};
void*                      TrampolinePage{};
std::array<std::uint8_t, CorePatchSize> CoreOriginalBytes{};
std::array<std::uint8_t, CorePatchSize> CorePatchBytes{};
std::atomic<std::uint64_t> Applications[TargetClassCount]{};
std::atomic<std::uint64_t> Suppressed{};
std::atomic<std::uint64_t> StatApplied{};
std::atomic<std::int32_t>  LastStatValue{};

constexpr D2RL::PluginInfo Info{
    .infoSize    = D2RL::PluginInfoSize,
    .abiVersion  = D2RL_PLUGIN_ABI_VERSION,
    .id          = "celestialrayone.crushing-blow",
    .name        = "Crushing Blow",
    .version     = "1.2.0",
    .author      = "CelestialRayOne",
    .description = "Scales Crushing Blow per target class from a TOML file, "
                   "plus an effectiveness stat.",
    .flags       = D2RL::PluginFlags::Server | D2RL::PluginFlags::NativeHooks,
};

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

template <class Function>
auto At(std::uintptr_t rva) noexcept -> Function {
    return reinterpret_cast<Function>(Base + rva);
}

template <std::size_t Size>
auto Matches(std::uintptr_t rva,
             const std::array<std::uint8_t, Size>& expected) noexcept -> bool {
    return Base != nullptr
        && std::memcmp(Base + rva, expected.data(), expected.size()) == 0;
}

void FormatPercent(std::int32_t hundredths, char* buffer,
                   std::size_t size) noexcept {
    const std::int32_t whole    = hundredths / 100;
    const std::int32_t fraction = hundredths % 100;
    if (fraction == 0) {
        std::snprintf(buffer, size, "%d", whole);
    } else if (fraction % 10 == 0) {
        std::snprintf(buffer, size, "%d.%d", whole, fraction / 10);
    } else {
        std::snprintf(buffer, size, "%d.%02d", whole, fraction);
    }
}

// ---------------------------------------------------------------------------
// TOML reading. Deliberately tiny: sections, key = value, # comments.
// Unknown keys are reported and skipped so a hand edited file never refuses to
// load over a typo, while a malformed value is a hard error.
// ---------------------------------------------------------------------------

auto Trim(std::string_view text) noexcept -> std::string_view {
    while (!text.empty()
           && (text.front() == ' ' || text.front() == '\t'
               || text.front() == '\r' || text.front() == '\n')) {
        text.remove_prefix(1);
    }
    while (!text.empty()
           && (text.back() == ' ' || text.back() == '\t'
               || text.back() == '\r' || text.back() == '\n')) {
        text.remove_suffix(1);
    }
    return text;
}

auto ParseBool(std::string_view value, bool& out) noexcept -> bool {
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

// Accepts 0, 50, 12.5, 16.25. Returns hundredths of a percent.
auto ParsePercent(std::string_view value, std::int32_t& out) noexcept -> bool {
    if (value.empty()) return false;
    std::size_t  index    = 0;
    std::int64_t whole    = 0;
    bool         sawDigit = false;
    while (index < value.size() && value[index] >= '0' && value[index] <= '9') {
        whole = whole * 10 + (value[index] - '0');
        if (whole > PercentMaximum) return false;
        sawDigit = true;
        ++index;
    }
    if (!sawDigit) return false;
    std::int64_t fraction = 0;
    if (index < value.size() && value[index] == '.') {
        ++index;
        int  digits           = 0;
        bool sawFractionDigit = false;
        while (index < value.size()
               && value[index] >= '0' && value[index] <= '9') {
            if (digits < 2) fraction = fraction * 10 + (value[index] - '0');
            ++digits;
            sawFractionDigit = true;
            ++index;
        }
        if (!sawFractionDigit) return false;
        if (digits == 1) fraction *= 10;
    }
    if (index != value.size()) return false;
    const std::int64_t hundredths = whole * 100 + fraction;
    if (hundredths > PercentMaximum) return false;
    out = static_cast<std::int32_t>(hundredths);
    return true;
}

// Accepts -1 (off) or 0 to 32767.
auto ParseStatId(std::string_view value, std::int32_t& out) noexcept -> bool {
    if (value == "-1") {
        out = StatIdOff;
        return true;
    }
    if (value.empty() || value.size() > 5) return false;
    std::int32_t parsed = 0;
    for (const char digit : value) {
        if (digit < '0' || digit > '9') return false;
        parsed = parsed * 10 + (digit - '0');
    }
    if (parsed > MaximumStatId) return false;
    out = parsed;
    return true;
}

void WarnUnknown(std::string_view section, std::string_view key) noexcept {
    if (!Context) return;
    char message[256]{};
    std::snprintf(message, sizeof(message),
                  "CrushingBlow: ignoring unknown setting '%.*s%s%.*s'.",
                  static_cast<int>(section.size()), section.data(),
                  section.empty() ? "" : ".",
                  static_cast<int>(key.size()), key.data());
    Context->LogWarn(message);
}

auto ParseToml(std::string_view text, Config& out) noexcept -> bool {
    Config      parsed{};
    std::string section;
    std::size_t cursor = 0;
    while (cursor <= text.size()) {
        const std::size_t newline = text.find('\n', cursor);
        const std::size_t end =
            newline == std::string_view::npos ? text.size() : newline;
        std::string_view  line = text.substr(cursor, end - cursor);
        cursor = end + 1;

        const std::size_t comment = line.find('#');
        if (comment != std::string_view::npos) line = line.substr(0, comment);
        line = Trim(line);
        if (line.empty()) continue;

        if (line.front() == '[' && line.back() == ']') {
            section = std::string(Trim(line.substr(1, line.size() - 2)));
            continue;
        }

        const std::size_t equals = line.find('=');
        if (equals == std::string_view::npos) continue;
        const std::string_view key   = Trim(line.substr(0, equals));
        const std::string_view value = Trim(line.substr(equals + 1));
        if (key.empty() || value.empty()) continue;

        if (section.empty()) {
            if (key == "enabled") {
                if (!ParseBool(value, parsed.enabled)) return false;
            } else if (key != "config_version") {
                WarnUnknown(section, key);
            }
        } else if (section == "targets") {
            bool matched = false;
            for (std::size_t index = 0; index < TargetClassCount; ++index) {
                if (key != TargetClassKeys[index]) continue;
                if (!ParsePercent(value, parsed.percent[index])) return false;
                matched = true;
                break;
            }
            if (!matched) WarnUnknown(section, key);
        } else if (section == "attack") {
            if (key == "ranged") {
                if (!ParsePercent(value, parsed.rangedPercent)) return false;
            } else {
                WarnUnknown(section, key);
            }
        } else if (section == "effectiveness") {
            if (key == "stat_id") {
                if (!ParseStatId(value, parsed.statId)) return false;
            } else {
                WarnUnknown(section, key);
            }
        } else if (section != "d2rl") {
            WarnUnknown(section, key);
        }
    }
    out = parsed;
    return true;
}

auto LoadConfig() noexcept -> bool {
    Settings     = Config{};
    ConfigOrigin = "embedded defaults";
    if (!Context) return true;

    if (!Context->EnsureConfig(DefaultConfig)) {
        Context->LogWarn(
            "CrushingBlow: the configuration file could not be created; "
            "embedded defaults are active.");
        return true;
    }

    std::vector<char> buffer(8192, '\0');
    std::uint32_t     required = 0;
    bool              read     = Context->ReadConfig(
        buffer.data(), static_cast<std::uint32_t>(buffer.size()), &required);
    if (!read && required > buffer.size() && required <= (1u << 20)) {
        buffer.assign(static_cast<std::size_t>(required) + 1, '\0');
        read = Context->ReadConfig(
            buffer.data(), static_cast<std::uint32_t>(buffer.size()), nullptr);
    }
    if (!read) {
        Context->LogWarn(
            "CrushingBlow: the configuration file could not be read; "
            "embedded defaults are active.");
        return true;
    }

    buffer.back() = '\0';
    Config parsed{};
    if (!ParseToml(std::string_view(buffer.data()), parsed)) {
        Context->LogError(
            "CrushingBlow: the configuration file has an invalid value; "
            "plugin refused. Fix or delete the TOML and start again.");
        return false;
    }
    Settings     = parsed;
    ConfigOrigin = "configuration file";
    return true;
}

// ---------------------------------------------------------------------------
// Classification and scaling
// ---------------------------------------------------------------------------

auto Classify(void* target) noexcept -> TargetClass {
    const std::int32_t unitType = GetUnitType(target);
    if (unitType == UnitTypePlayer) return TargetClass::Player;
    if (unitType != UnitTypeMonster) return TargetClass::Monster;
    if (GetHirelingTypeId(target) != 0) return TargetClass::Mercenary;
    if (IsBoss(nullptr, target) != 0) return TargetClass::ActBoss;
    if (CheckMonTypeFlag(target, MonTypeFlagSuperUnique) != 0) {
        return TargetClass::SuperUnique;
    }
    if (CheckMonTypeFlag(target, MonTypeFlagUnique) != 0) {
        return TargetClass::Unique;
    }
    if (CheckMonTypeFlag(target, MonTypeFlagChampion) != 0) {
        return TargetClass::Champion;
    }
    if (CheckMonTypeFlag(target, MonTypeFlagMinion) != 0) {
        return TargetClass::Minion;
    }
    return TargetClass::Monster;
}

// The attacker's effectiveness in plain percent: 100 while the stat is off,
// absent on the attacker or the attacker is unknown. 0 or less means no
// Crushing Blow at all.
auto EffectPercent(void* attacker) noexcept -> std::int32_t {
    if (GetUnitStat == nullptr || attacker == nullptr) return EffectNeutral;
    const std::int32_t value = GetUnitStat(attacker, Settings.statId, 0);
    if (value == 0) return EffectNeutral;
    StatApplied.fetch_add(1, std::memory_order_relaxed);
    LastStatValue.store(value, std::memory_order_relaxed);
    return EffectNeutral + std::clamp(value, -EffectNeutral, MaximumStatBonus);
}

// base is the divisor the engine would have started from. The returned value
// leaves every later native multiplier intact, so the final ratio against
// vanilla is exactly targetPercent * rangedPercent.
auto ScaleDivisor(std::int32_t base,
                  std::int32_t targetPercent,
                  std::int32_t rangedPercent) noexcept -> std::int32_t {
    const std::int64_t denominator =
        static_cast<std::int64_t>(targetPercent) * rangedPercent;
    if (denominator <= 0) return MaximumDivisor;
    const std::int64_t numerator =
        static_cast<std::int64_t>(base) * PercentScale * PercentScale;
    std::int64_t scaled = (numerator + denominator / 2) / denominator;
    if (scaled < 1) scaled = 1;
    if (scaled > MaximumDivisor) scaled = MaximumDivisor;
    return static_cast<std::int32_t>(scaled);
}

// Runs the core at the class divisor, then adds the stat's share of the life
// the core actually removed, exactly. See the header for the end steps it
// mirrors.
auto CoreWithEffect(void*               game,
                    void*               attacker,
                    void*               target,
                    CrushingBlowParams* params,
                    std::int32_t        effectPercent) noexcept -> std::int32_t {
    auto* flags = params->damage == nullptr
        ? nullptr
        : reinterpret_cast<std::uint16_t*>(
              static_cast<std::uint8_t*>(params->damage) + DamageResultFlagsOffset);
    const std::uint16_t flagsBefore = flags == nullptr ? std::uint16_t{0} : *flags;

    const std::int32_t lifeBefore = GetUnitStat(target, StatHitpoints, 0);
    const std::int32_t result     = OriginalCore(game, attacker, target, params);
    const std::int32_t lifeAfter  = GetUnitStat(target, StatHitpoints, 0);

    const std::int64_t removed = static_cast<std::int64_t>(lifeBefore) - lifeAfter;
    if (removed <= 0) return result;
    const std::int64_t extra =
        removed * (effectPercent - EffectNeutral) / EffectNeutral;
    if (extra == 0) return result;

    std::int64_t lifeFinal = static_cast<std::int64_t>(lifeAfter) - extra;
    if (lifeFinal < 0) lifeFinal = 0;
    SetUnitStat(target, StatHitpoints, static_cast<std::int32_t>(lifeFinal), 0);

    if (flags != nullptr) {
        if (lifeFinal == 0) {
            *flags = static_cast<std::uint16_t>(*flags | DamageFlagWillDie);
        } else if (lifeAfter <= 0) {
            *flags = static_cast<std::uint16_t>(
                (*flags & ~DamageFlagWillDie) | (flagsBefore & DamageFlagWillDie));
        }
    }
    return result;
}

std::int32_t __fastcall HookApplyCrushingBlowCore(
        void*               game,
        void*               attacker,
        void*               target,
        CrushingBlowParams* params) noexcept {
    if (!OriginalCore) return 0;
    if (!Operational.load(std::memory_order_acquire)
            || target == nullptr || params == nullptr) {
        return OriginalCore(game, attacker, target, params);
    }

    const TargetClass  targetClass   = Classify(target);
    const std::size_t  index         = static_cast<std::size_t>(targetClass);
    const std::int32_t targetPercent = Settings.percent[index];

    if (targetPercent <= 0) {
        Suppressed.fetch_add(1, std::memory_order_relaxed);
        return 0;
    }

    std::int32_t base = params->monsterDivisor;
    if (targetClass == TargetClass::Player) {
        base = params->playerDivisor;
    } else if (targetClass == TargetClass::Mercenary) {
        base = params->hirelingDivisor;
    }
    if (base <= 0 || base > MaximumBase) {
        return OriginalCore(game, attacker, target, params);
    }

    // The native ranged doubling is left alone while ranged sits at its
    // vanilla value, and only folded into the divisor once it is changed.
    const bool foldRanged = Settings.rangedPercent != VanillaRangedPercent
        && params->rangedEvent != 0;
    if (foldRanged && Settings.rangedPercent <= 0) {
        Suppressed.fetch_add(1, std::memory_order_relaxed);
        return 0;
    }
    const std::int32_t rangedPercent =
        foldRanged ? Settings.rangedPercent : PercentScale;

    // Read last, so a Crushing Blow another setting already suppressed never
    // counts as one the stat applied to. Nothing in params changes before it.
    const std::int32_t effectPercent = EffectPercent(attacker);
    if (effectPercent <= 0) {
        Suppressed.fetch_add(1, std::memory_order_relaxed);
        return 0;
    }
    if (foldRanged) params->rangedEvent = 0;

    const std::int32_t scaled =
        ScaleDivisor(base, targetPercent, rangedPercent);
    params->monsterDivisor  = scaled;
    params->playerDivisor   = scaled;
    params->hirelingDivisor = scaled;

    Applications[index].fetch_add(1, std::memory_order_relaxed);
    if (effectPercent == EffectNeutral) {
        return OriginalCore(game, attacker, target, params);
    }
    return CoreWithEffect(game, attacker, target, params, effectPercent);
}

// ---------------------------------------------------------------------------
// Validation, console, lifecycle
// ---------------------------------------------------------------------------

auto ValidateNativeContract() noexcept -> bool {
    struct Check {
        bool        matched;
        const char* label;
    };
    const Check checks[]{
        {Matches(GetUnitTypeRva, GetUnitTypeExpected), "UNITS_GetUnitType"},
        {Matches(GetHirelingTypeIdRva, GetHirelingTypeIdExpected),
         "MONSTERS_GetHirelingTypeId"},
        {Matches(IsBossRva, IsBossExpected), "MONSTERS_IsBoss"},
        {Matches(CheckMonTypeFlagRva, CheckMonTypeFlagExpected),
         "MONSTERUNIQUE_CheckMonTypeFlag"},
        {Settings.statId == StatIdOff
             || (Matches(GetUnitStatRva, GetUnitStatThunkJump)
                 && Matches(GetUnitStatNopsRva, GetUnitStatThunkNops)
                 && Matches(GetUnitStatBodyRva, GetUnitStatBody)),
         "STATLIST_GetUnitStat (ReadWideUnitStat thunk)"},
        {Settings.statId == StatIdOff
             || (Matches(SetUnitStatRva, GetUnitStatThunkJump)
                 && Matches(SetUnitStatNopsRva, GetUnitStatThunkNops)),
         "STATLIST_SetUnitStat (SetWideUnitStat thunk)"},
    };
    for (const auto& check : checks) {
        if (check.matched) continue;
        char message[256]{};
        std::snprintf(message, sizeof(message),
                      "CrushingBlow: %s signature mismatch; plugin refused.",
                      check.label);
        Context->LogError(message);
        return false;
    }
    return true;
}

auto IsCommittedIn(const std::uint8_t* address, std::size_t size,
                   const void* allocationBase, bool executable) noexcept -> bool {
    MEMORY_BASIC_INFORMATION info{};
    if (VirtualQuery(address, &info, sizeof(info)) != sizeof(info)) return false;
    if (info.State != MEM_COMMIT || info.AllocationBase != allocationBase) return false;
    const auto regionEnd = static_cast<const std::uint8_t*>(info.BaseAddress) + info.RegionSize;
    if (address + size > regionEnd) return false;
    if (!executable) return true;
    const DWORD execute = PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE
        | PAGE_EXECUTE_WRITECOPY;
    return (info.Protect & execute) != 0 && (info.Protect & PAGE_GUARD) == 0;
}

// Finds the D2RCore Crushing Blow core through its export and checks every
// byte the hook relies on. Logs and returns null on any mismatch.
auto LocateCore() noexcept -> std::uint8_t* {
    const HMODULE core = GetModuleHandleW(CoreModuleName);
    if (core == nullptr) {
        Context->LogError("CrushingBlow: D2RCore.dll is not loaded; plugin refused.");
        return nullptr;
    }
    const auto* stub =
        reinterpret_cast<const std::uint8_t*>(GetProcAddress(core, CoreExportName));
    if (stub == nullptr
            || !IsCommittedIn(stub, CoreExportStub.size(), core, true)) {
        Context->LogError(
            "CrushingBlow: D2RCore does not export ApplyWideCrushingBlow; plugin refused.");
        return nullptr;
    }
    for (std::size_t index = 0; index < CoreExportStub.size(); ++index) {
        const bool cookie = index >= CoreExportCookieOffset
            && index < CoreExportCookieOffset + sizeof(std::int32_t);
        const bool rel32 = index > CoreExportCallOffset && index < CoreExportCallEnd;
        if (cookie || rel32) continue;
        if (stub[index] != CoreExportStub[index]) {
            Context->LogError(
                "CrushingBlow: D2RCore!ApplyWideCrushingBlow is not the D2RLoader 1.3.0 "
                "stub; plugin refused.");
            return nullptr;
        }
    }
    std::int32_t displacement = 0;
    std::memcpy(&displacement, stub + CoreExportCallOffset + 1, sizeof(displacement));
    auto* function = const_cast<std::uint8_t*>(stub + CoreExportCallEnd + displacement);

    std::size_t span = CorePrologue.size();
    for (const auto& witness : CoreParamsWitnesses) {
        span = std::max(span, witness.offset + witness.size);
    }
    if (!IsCommittedIn(function, span, core, true)
            || std::memcmp(function, CorePrologue.data(), CorePrologue.size()) != 0) {
        Context->LogError(
            "CrushingBlow: the D2RCore Crushing Blow core prologue does not match, or "
            "another hook already owns it; plugin refused.");
        return nullptr;
    }
    for (const auto& witness : CoreParamsWitnesses) {
        if (std::memcmp(function + witness.offset, witness.bytes.data(), witness.size) != 0) {
            char message[192]{};
            std::snprintf(message, sizeof(message),
                "CrushingBlow: the D2RCore Crushing Blow core no longer reads the params "
                "block at +0x%zX; plugin refused.", witness.offset);
            Context->LogError(message);
            return nullptr;
        }
    }
    return function;
}

auto WriteCode(std::uint8_t* address, const std::uint8_t* bytes, std::size_t size) noexcept
        -> bool {
    DWORD previous = 0;
    if (!VirtualProtect(address, size, PAGE_EXECUTE_READWRITE, &previous)) return false;
    std::memcpy(address, bytes, size);
    DWORD ignored = 0;
    VirtualProtect(address, size, previous, &ignored);
    FlushInstructionCache(GetCurrentProcess(), address, size);
    return true;
}

// jmp qword ptr [rip+0] followed by its 64-bit target.
void EncodeAbsoluteJump(std::uint8_t* out, std::uint64_t target) noexcept {
    static constexpr std::uint8_t JumpQwordRip[]{0xFF, 0x25, 0x00, 0x00, 0x00, 0x00};
    std::memcpy(out, JumpQwordRip, sizeof(JumpQwordRip));
    std::memcpy(out + sizeof(JumpQwordRip), &target, sizeof(target));
}

// Core entry -> HookApplyCrushingBlowCore. The trampoline replays the 16
// replaced prologue bytes and jumps to core + 16, so OriginalCore is the
// untouched core.
auto InstallCoreHook(std::uint8_t* function) noexcept -> bool {
    TrampolinePage = VirtualAlloc(nullptr, TrampolinePageBytes,
        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (TrampolinePage == nullptr) {
        Context->LogError("CrushingBlow: no trampoline page could be allocated.");
        return false;
    }
    auto* trampoline = static_cast<std::uint8_t*>(TrampolinePage);
    std::memset(trampoline, 0xCC, TrampolinePageBytes);
    std::memcpy(trampoline, function, CorePatchSize);
    EncodeAbsoluteJump(trampoline + CorePatchSize,
        reinterpret_cast<std::uint64_t>(function) + CorePatchSize);
    DWORD previous = 0;
    if (!VirtualProtect(trampoline, TrampolinePageBytes, PAGE_EXECUTE_READ, &previous)) {
        Context->LogError("CrushingBlow: trampoline protection could not be finalized.");
        VirtualFree(TrampolinePage, 0, MEM_RELEASE);
        TrampolinePage = nullptr;
        return false;
    }
    FlushInstructionCache(GetCurrentProcess(), trampoline, TrampolinePageBytes);
    OriginalCore = reinterpret_cast<ApplyCrushingBlowCoreFn>(trampoline);

    std::memcpy(CoreOriginalBytes.data(), function, CorePatchSize);
    CorePatchBytes.fill(0x90);
    EncodeAbsoluteJump(CorePatchBytes.data(),
        reinterpret_cast<std::uint64_t>(&HookApplyCrushingBlowCore));
    if (!WriteCode(function, CorePatchBytes.data(), CorePatchSize)) {
        Context->LogError("CrushingBlow: the D2RCore Crushing Blow core could not be patched.");
        OriginalCore = nullptr;
        VirtualFree(TrampolinePage, 0, MEM_RELEASE);
        TrampolinePage = nullptr;
        return false;
    }
    CoreFunction = function;
    return true;
}

// Puts the original prologue back if the hook is still the one this plugin wrote.
void RemoveCoreHook() noexcept {
    if (CoreFunction == nullptr) return;
    if (std::memcmp(CoreFunction, CorePatchBytes.data(), CorePatchSize) == 0) {
        WriteCode(CoreFunction, CoreOriginalBytes.data(), CorePatchSize);
    }
    CoreFunction = nullptr;
}

auto Status(D2R::Game::Client*,
            const D2RL::ConsoleCommandContext* command,
            void*) noexcept -> D2RL::ConsoleCommandResult {
    if (!command || !command->plugin) {
        return D2RL::ConsoleCommandResult::Failed;
    }

    char        settings[640]{};
    std::size_t written = 0;
    for (std::size_t index = 0; index < TargetClassCount; ++index) {
        char percent[16]{};
        char share[16]{};
        FormatPercent(Settings.percent[index], percent, sizeof(percent));
        const std::int32_t vanillaShare = PercentScale / VanillaDivisor[index];
        FormatPercent(
            static_cast<std::int32_t>(static_cast<std::int64_t>(vanillaShare)
                                      * Settings.percent[index] / PercentScale),
            share, sizeof(share));
        const int length = std::snprintf(
            settings + written, sizeof(settings) - written,
            "%s%s=%s%% (%s%% life)", written == 0 ? "" : "; ",
            TargetClassKeys[index], percent, share);
        if (length <= 0) break;
        written += static_cast<std::size_t>(length);
        if (written >= sizeof(settings) - 1) break;
    }

    char counters[320]{};
    written = 0;
    for (std::size_t index = 0; index < TargetClassCount; ++index) {
        const int length = std::snprintf(
            counters + written, sizeof(counters) - written, "%s%s=%llu",
            written == 0 ? "" : "/", TargetClassKeys[index],
            static_cast<unsigned long long>(
                Applications[index].load(std::memory_order_relaxed)));
        if (length <= 0) break;
        written += static_cast<std::size_t>(length);
        if (written >= sizeof(counters) - 1) break;
    }

    char ranged[16]{};
    FormatPercent(Settings.rangedPercent, ranged, sizeof(ranged));

    char stat[96]{};
    if (Settings.statId == StatIdOff) {
        std::snprintf(stat, sizeof(stat), "off");
    } else {
        std::snprintf(stat, sizeof(stat), "%d (applied=%llu, last=%+d%%)",
            Settings.statId,
            static_cast<unsigned long long>(
                StatApplied.load(std::memory_order_relaxed)),
            LastStatValue.load(std::memory_order_relaxed));
    }

    char message[1500]{};
    std::snprintf(
        message, sizeof(message),
        "Crushing Blow 1.2.0: active=%s; source=%s; ranged=%s%%; "
        "effectiveness stat=%s; %s; applied %s; suppressed=%llu.",
        Operational.load(std::memory_order_acquire) ? "true" : "false",
        ConfigOrigin.c_str(), ranged, stat, settings, counters,
        static_cast<unsigned long long>(
            Suppressed.load(std::memory_order_relaxed)));
    command->plugin->WriteConsoleMessage(message);
    return D2RL::ConsoleCommandResult::Handled;
}

void ResetState() noexcept {
    Operational.store(false, std::memory_order_release);
    for (auto& counter : Applications) {
        counter.store(0, std::memory_order_relaxed);
    }
    Suppressed.store(0, std::memory_order_relaxed);
    StatApplied.store(0, std::memory_order_relaxed);
    LastStatValue.store(0, std::memory_order_relaxed);
}

} // namespace

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
    Base    = reinterpret_cast<std::uint8_t*>(context->exeBase);
    ResetState();

    if (!Base) {
        context->LogError(
            "CrushingBlow: the D2R executable base is unavailable.");
        return false;
    }
    if (!LoadConfig()) return false;

    if (!context->RegisterConsoleCommand(
            "crushing-blow", Status,
            "Show the active Crushing Blow percentages and usage counters.")) {
        context->LogWarn(
            "CrushingBlow: optional status command was not registered.");
    }

    if (!Settings.enabled) {
        context->LogInfo(
            "Crushing Blow 1.2.0 loaded disabled; no hook was installed.");
        return true;
    }
    if (!ValidateNativeContract()) return false;

    GetUnitType       = At<GetUnitTypeFn>(GetUnitTypeRva);
    GetHirelingTypeId = At<GetHirelingTypeIdFn>(GetHirelingTypeIdRva);
    IsBoss            = At<IsBossFn>(IsBossRva);
    CheckMonTypeFlag  = At<CheckMonTypeFlagFn>(CheckMonTypeFlagRva);
    if (Settings.statId != StatIdOff) {
        GetUnitStat = At<GetUnitStatFn>(GetUnitStatRva);
        SetUnitStat = At<SetUnitStatFn>(SetUnitStatRva);
    }

    const std::uint8_t* core = LocateCore();
    if (core == nullptr) return false;
    if (!InstallCoreHook(const_cast<std::uint8_t*>(core))) return false;

    Operational.store(true, std::memory_order_release);

    char monster[16]{};
    char boss[16]{};
    char player[16]{};
    FormatPercent(
        Settings.percent[static_cast<std::size_t>(TargetClass::Monster)],
        monster, sizeof(monster));
    FormatPercent(
        Settings.percent[static_cast<std::size_t>(TargetClass::ActBoss)],
        boss, sizeof(boss));
    FormatPercent(
        Settings.percent[static_cast<std::size_t>(TargetClass::Player)],
        player, sizeof(player));
    char stat[16]{};
    if (Settings.statId == StatIdOff) {
        std::snprintf(stat, sizeof(stat), "off");
    } else {
        std::snprintf(stat, sizeof(stat), "%d", Settings.statId);
    }
    char message[512]{};
    std::snprintf(
        message, sizeof(message),
        "Crushing Blow 1.2.0 by CelestialRayOne active; monster=%s%%; "
        "act_boss=%s%%; player=%s%%; effectiveness stat=%s; source=%s; "
        "installation=%s. Type crushing-blow in the console for the full table.",
        monster, boss, player, stat, ConfigOrigin.c_str(),
        context->loadScope == D2RL::LoadScope::Mod ? "mod-local" : "global");
    context->LogInfo(message);
    return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
    Operational.store(false, std::memory_order_release);
    RemoveCoreHook();
    // The trampoline page is deliberately kept: a thread may still be inside it.
    GetUnitType       = nullptr;
    GetHirelingTypeId = nullptr;
    IsBoss            = nullptr;
    CheckMonTypeFlag  = nullptr;
    GetUnitStat       = nullptr;
    SetUnitStat       = nullptr;
    OriginalCore      = nullptr;
    Context           = nullptr;
    Base              = nullptr;
}
