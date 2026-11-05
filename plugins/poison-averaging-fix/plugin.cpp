// Converted Poison
//
// Poison that a hit gets from converted weapon damage lasts the poison length
// of the skill that converted it, and nothing else: no averaging with the
// other poison sources on the hit and no minimum. Everything else about poison
// is untouched. With states.txt canstack 0 a target still holds one poison,
// and any poison with an equal or higher damage per frame replaces it.
//
// Built for D2R 3.3 on D2RLoader 1.3.1 (PluginSDK 0.3.0). Everything below was
// read out of the D2RLoader 1.3.1 dump (md5 8af7a47ce9077de0378c69af2930854d)
// and D2RCore.dll 1.3.1 (SHA-256 2a868d013d2e0830bd2d9e04b918b19e46a73cf726c8
// 33e70d089b948fdeb5a2). Every game address is guarded by a byte witness at
// load.
//
// ---------------------------------------------------------------------------
// How a hit's poison gets its length
// ---------------------------------------------------------------------------
//   A hit (D2Damage) carries poison as a rate per frame (+38h, 256ths of life)
//   and a length in frames (+3Ch). The damage commit SUNITDMG_ExecuteEvents
//   0x44CE80 applies it through the poison applier 0x4518C0. With canstack 0
//   that finds the target's one poison list (0x2F5940) and replaces it when
//   the new rate is equal or higher.
//
//   Missile hits. MISSILES_ServerHitResolution 0x4639A0 fills the hit from the
//   missile's stats (0x465420): rate from poisonmindam/poisonmaxdam, length =
//   poisonlength / poison_count when poison_count is above 1 (0x46571C ..
//   0x465739). The missile got those stats when it was made (0x3BF9B0 and
//   0x3C08A0): the skill's own poison length plus the owner's item poison
//   lengths, and only item poison properties add to poison_count. Then
//   0x4639A0 calls the missiles.txt pSrvDmgFunc (table 0x2391270, call at
//   0x463F1D). pSrvDmgFunc 1 (0x453490) moves DmgCalc1 percent (at most 100)
//   of the physical damage into the missile's element through 0x465160, which
//   for poison adds it to the rate (+38h) and leaves the length alone.
//
//   Melee hits. SUNITDMG_FillDamageValues 0x44C030 builds the poison the same
//   way, then runs the skills.txt calc4 conversion the skill put on the hit
//   (+149h element, +14Ch percent): ebx = the converted physical damage
//   (0x44CBBC .. 0x44CC8B), r10b = the element, after element 10 (random)
//   rolled 1..5 (0x44CC9B .. 0x44CCDC). Element 5 adds ebx / 8 to the rate
//   and raises the length to at least 50 frames (0x44CD2E .. 0x44CD4D).
//
//   So the converted damage keeps hitting for whatever length the other
//   poison sources produced: a 3-frame skill poison next to a 250-frame item
//   poison runs for 253 frames with one poison item and (3+250+250)/2 with
//   two.
//
// ---------------------------------------------------------------------------
// What the plugin does
// ---------------------------------------------------------------------------
//   Right after a conversion adds poison to a hit, the plugin sets the hit's
//   poison length to the converting skill's length from the game's own
//   SKILLS_GetElementalLength 0x337DA0 (context, unit, skill, level): ELen +
//   ELevLen1/2/3 by level + ELenSymPerCalc percent, the value the skill's own
//   poison gets. That happens before resistances, so poison length reduction
//   and everything after treat it like any other length. The hit still
//   carries one poison and is applied and replaced exactly as before. A hit
//   that carries converted poison runs all of its poison for that length.
//
//   Missiles: inline hook on pSrvDmgFunc 1 (0x453490). A higher rate after the
//   original means poison was converted. Skill, the way 0x3BF9B0 picks it:
//   when the missiles.txt row is skill driven (+25h & bit 7), the missile's
//   own skill (missile data +0Ch) if above 0, else the row's Skill column
//   (+1B6h); a row that is not skill driven and has no Skill gets no change.
//   Level: the missile's stat 12, which the game sets to the skill level when
//   it makes the missile. Synergies: the missile's owner (0x490300).
//
//   Melee: the call at 0x44CD88, right after the conversion switch, goes
//   through a 20-byte relay that packs r10b (element) and ebx (converted
//   physical damage) into the call's fourth argument. The original callee
//   0x5889E0 takes three. Checked against the stock switch and against the
//   damage-overflow-fix plugin's replacement of 0x44CCDF .. 0x44CD7F, which
//   reads both registers and writes neither. Skill: the one the attacker is
//   using ([[unit+100h]+18h]), level through the loader thunk 0x33D1E0.
//
//   canstack 1: the commit applies each poison item as its own poison and the
//   rest (the skill's poison and the converted damage) with length = hit
//   length minus the items' lengths (0x44D1CF .. 0x44D262). There the plugin
//   sets the hit length to the skill length plus the items' lengths
//   (+40h list, +48h count, 12-byte entries, length at +8), so the rest runs
//   for the skill length.

#include <D2RLPlugin/api.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>

namespace CelestialRayOne::ConvertedPoison {
namespace {

// ---------------------------------------------------------------------------
// Native anchors
// ---------------------------------------------------------------------------

constexpr std::uint64_t SrvDmg01Rva        = 0x453490;   // missiles.txt pSrvDmgFunc 1
constexpr std::uint64_t SrvDmgTableRva     = 0x2391270;  // pSrvDmgFunc table, 8-byte slots
constexpr std::uint64_t MeleeSwitchRva     = 0x44CBA3;   // conversion amount and element
constexpr std::uint64_t MeleeWindowRva     = 0x44CD7F;   // join point after the conversion switch
constexpr std::uint64_t MeleeCallRva       = 0x44CD88;   // call 0x5889E0, redirected to the relay
constexpr std::uint64_t AfterConversionRva = 0x5889E0;   // (attacker, defender, hit)
constexpr std::uint64_t GetUnitStatRva     = 0x2F5020;   // loader thunk, (unit, stat, layer)
constexpr std::uint64_t SkillLevelRva      = 0x33D1E0;   // loader thunk, (unit, skill, bonus, 0)
constexpr std::uint64_t MissileOwnerRva    = 0x490300;   // (game, missile), may return null
constexpr std::uint64_t MissileRecordRva   = 0x166040;   // (dataContext, missileId) -> missiles.txt row
constexpr std::uint64_t UnitClassIdRva     = 0x349860;   // (unit, file, line) -> class id
constexpr std::uint64_t ElementalLengthRva = 0x337DA0;   // (dataContext, unit, skill, level)
constexpr std::uint64_t DataContextRva     = 0x34A0E0;   // (unit) -> data context
constexpr std::uint64_t DataTablesRva      = 0x300A90;   // (dataContext) -> data tables
constexpr std::uint64_t BitMasksRva        = 0x1D996D0;  // 32 dwords, 1 << n

constexpr std::size_t CallSize = 5;
constexpr std::size_t MeleeCallOffset = MeleeCallRva - MeleeWindowRva;

// The hit (D2Damage)
constexpr std::size_t HitPoisonRateOffset     = 0x38;
constexpr std::size_t HitPoisonLengthOffset   = 0x3C;
constexpr std::size_t HitSourcesOffset        = 0x40;
constexpr std::size_t HitSourceCountOffset    = 0x48;
constexpr std::size_t HitSourceCapacityOffset = 0x50;
constexpr std::size_t SourceEntryBytes        = 12;
constexpr std::size_t SourceLengthOffset      = 8;
constexpr std::uint64_t SourceInlineFlag      = 0x8000'0000'0000'0000ULL;
constexpr std::uint64_t MaximumSources        = 4096;

// Units and records
constexpr std::size_t GameDataContextOffset  = 0x106;
constexpr std::size_t MissileDataOffset      = 0x10;
constexpr std::size_t MissileDataSkillOffset = 0x0C;
constexpr std::size_t MissileRowFlagsOffset  = 0x25;
constexpr std::size_t MissileRowSkillOffset  = 0x1B6;
constexpr std::size_t SkillDrivenBit         = 7;
constexpr std::size_t UnitSkillListOffset    = 0x100;
constexpr std::size_t SkillListUsedOffset    = 0x18;
constexpr std::int32_t LevelStat             = 12;
constexpr std::uint32_t PoisonElement        = 5;

// Data tables (states.txt)
constexpr std::size_t   StatesRowsOffset     = 0x290;
constexpr std::size_t   StatesCountOffset    = 0x298;
constexpr std::size_t   StatesRowSize        = 0x44;
constexpr std::size_t   StatesCanStackOffset = 0x3C;
constexpr std::uint64_t PoisonState          = 2;

constexpr std::size_t MaximumConfigBytes = 16'384;

// ---------------------------------------------------------------------------
// Byte witnesses (D2RLoader 1.3.1 dump)
// ---------------------------------------------------------------------------

// 453490: mov [rsp+18h],rbp / push rsi / push rdi / push r14 / sub rsp,30h /
// mov rsi,r9 / mov r14,r8 / mov rdi,rdx / mov rbp,rcx. No relative operand;
// the next instruction (test rdx,rdx at 4534A9) starts on the boundary.
constexpr std::uint8_t SrvDmg01Prologue[]{
    0x48, 0x89, 0x6C, 0x24, 0x18, 0x56, 0x57, 0x41, 0x56, 0x48, 0x83, 0xEC,
    0x30, 0x49, 0x8B, 0xF1, 0x4D, 0x8B, 0xF0, 0x48, 0x8B, 0xFA, 0x48, 0x8B,
    0xE9,
};

// 44CBA3 .. 44CCDE: movzx r10d,byte [rdi+149h] (element), the converted
// physical damage into ebx along all three overflow-safe branches, physical
// minus that, and the element 10 roll that leaves 1..5 in r10b. Ends right
// before 44CCDF, where the damage-overflow-fix plugin's window starts.
constexpr std::uint8_t MeleeSwitchWitness[]{
    0x44, 0x0F, 0xB6, 0x97, 0x49, 0x01, 0x00, 0x00, 0x4C, 0x8B, 0xBC, 0x24,
    0xE8, 0x04, 0x00, 0x00, 0x45, 0x84, 0xD2, 0x0F, 0x84, 0xC3, 0x01, 0x00,
    0x00, 0x4C, 0x63, 0x47, 0x18, 0x4C, 0x63, 0x8F, 0x4C, 0x01, 0x00, 0x00,
    0x41, 0x81, 0xF8, 0x00, 0x00, 0x10, 0x00, 0x7E, 0x50, 0x41, 0x8B, 0xC0,
    0x83, 0xE0, 0xF0, 0x3D, 0x40, 0x06, 0x00, 0x00, 0x7D, 0x2B, 0x49, 0x8B,
    0xC8, 0x48, 0xB8, 0x0B, 0xD7, 0xA3, 0x70, 0x3D, 0x0A, 0xD7, 0xA3, 0x49,
    0x0F, 0xAF, 0xC9, 0x48, 0xF7, 0xE9, 0x48, 0x8D, 0x1C, 0x11, 0x48, 0xC1,
    0xFB, 0x06, 0x48, 0x8B, 0xC3, 0x48, 0xC1, 0xE8, 0x3F, 0x48, 0x03, 0xD8,
    0xE9, 0x85, 0x00, 0x00, 0x00, 0xB8, 0x1F, 0x85, 0xEB, 0x51, 0x41, 0xF7,
    0xE8, 0xC1, 0xFA, 0x05, 0x8B, 0xDA, 0xC1, 0xEB, 0x1F, 0x03, 0xDA, 0x41,
    0x0F, 0xAF, 0xD9, 0xEB, 0x6D, 0x41, 0x81, 0xF9, 0x00, 0x00, 0x01, 0x00,
    0x7E, 0x4C, 0x41, 0x8B, 0xC1, 0x83, 0xE0, 0xF0, 0x3D, 0x40, 0x06, 0x00,
    0x00, 0x7D, 0x27, 0x49, 0x8B, 0xC8, 0x48, 0xB8, 0x0B, 0xD7, 0xA3, 0x70,
    0x3D, 0x0A, 0xD7, 0xA3, 0x49, 0x0F, 0xAF, 0xC9, 0x48, 0xF7, 0xE9, 0x48,
    0x03, 0xD1, 0x48, 0xC1, 0xFA, 0x06, 0x48, 0x8B, 0xDA, 0x48, 0xC1, 0xEB,
    0x3F, 0x48, 0x03, 0xDA, 0xEB, 0x30, 0xB8, 0x1F, 0x85, 0xEB, 0x51, 0x41,
    0xF7, 0xE9, 0xC1, 0xFA, 0x05, 0x8B, 0xDA, 0xC1, 0xEB, 0x1F, 0x03, 0xDA,
    0x41, 0x0F, 0xAF, 0xD8, 0xEB, 0x18, 0x41, 0x8B, 0xC8, 0xB8, 0x1F, 0x85,
    0xEB, 0x51, 0x41, 0x0F, 0xAF, 0xC9, 0xF7, 0xE9, 0xC1, 0xFA, 0x05, 0x8B,
    0xDA, 0xC1, 0xEB, 0x1F, 0x03, 0xDA, 0x44, 0x2B, 0xC3, 0x45, 0x85, 0xC0,
    0x45, 0x0F, 0x4E, 0xC6, 0x44, 0x89, 0x47, 0x18, 0x41, 0x80, 0xFA, 0x0A,
    0x75, 0x3E, 0x48, 0x8B, 0xCE, 0xE8, 0x37, 0xD5, 0xEF, 0xFF, 0x4C, 0x8B,
    0xC8, 0x8B, 0x08, 0x4C, 0x69, 0xC1, 0xC5, 0x90, 0xC6, 0x6A, 0x8B, 0x48,
    0x04, 0x4C, 0x03, 0xC1, 0x49, 0x8B, 0xC8, 0x45, 0x8B, 0xD0, 0x48, 0xC1,
    0xE9, 0x20, 0x89, 0x48, 0x04, 0xB8, 0xCD, 0xCC, 0xCC, 0xCC, 0x41, 0xF7,
    0xE0, 0x45, 0x89, 0x01, 0xC1, 0xEA, 0x02, 0x8D, 0x04, 0x92, 0x44, 0x2B,
    0xD0, 0x41, 0xFE, 0xC2,
};

// 44CD7F: mov r8,rdi / mov rdx,r12 / mov rcx,rsi / call 5889E0 /
// mov edx,[rsp+550h] / mov rcx,rsi / call 424490.
constexpr std::uint8_t MeleeWindow[]{
    0x4C, 0x8B, 0xC7, 0x49, 0x8B, 0xD4, 0x48, 0x8B, 0xCE, 0xE8, 0x53, 0xBC,
    0x13, 0x00, 0x8B, 0x94, 0x24, 0x50, 0x05, 0x00, 0x00, 0x48, 0x8B, 0xCE,
    0xE8, 0xF4, 0x76, 0xFD, 0xFF,
};

constexpr std::uint8_t LoaderThunkBytes[]{ 0xFF, 0x25 };
constexpr std::uint8_t MissileOwnerBytes[]{
    0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xFA, 0x48, 0x8B, 0xF1, 0x48, 0x85, 0xD2,
    0x75, 0x20 };
constexpr std::uint8_t MissileRecordBytes[]{
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x30, 0x48, 0x63, 0xDA, 0xE8, 0x42, 0xAA, 0x19, 0x00 };
constexpr std::uint8_t DataContextBytes[]{
    0x48, 0x83, 0xEC, 0x28, 0x48, 0x85, 0xC9, 0x75, 0x1A, 0x88, 0x4C, 0x24, 0x30, 0x48, 0x8D, 0x4C, 0x24, 0x30 };
constexpr std::uint8_t UnitClassIdBytes[]{
    0x48, 0x83, 0xEC, 0x28, 0x48, 0x85, 0xC9, 0x75, 0x1D, 0x88, 0x4C, 0x24,
    0x30, 0x48, 0x8D, 0x4C, 0x24, 0x30,
};
constexpr std::uint8_t ElementalLengthBytes[]{
    0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x48, 0x89,
    0x7C, 0x24, 0x20, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x30, 0x4C, 0x8B, 0xF2,
};
constexpr std::uint8_t DataTablesBytes[]{
    0x48, 0x83, 0xEC, 0x28, 0x0F, 0xB6, 0xC1, 0x48, 0x89, 0x44, 0x24, 0x38,
    0x48, 0x83, 0xF8, 0x04,
};

// 41 89 D9          mov   r9d, ebx      converted physical damage
// 41 0F B6 C2       movzx eax, r10b     element
// 48 C1 E0 20       shl   rax, 32
// 49 09 C1          or    r9, rax
// FF 25 00 00 00 00 jmp   [rip]         MeleeConversionHook, slot right after
// rcx, rdx and r8 still hold the original call's three arguments.
constexpr std::uint8_t MeleeRelayCode[]{
    0x41, 0x89, 0xD9, 0x41, 0x0F, 0xB6, 0xC2, 0x48, 0xC1, 0xE0, 0x20, 0x49, 0x09, 0xC1, 0xFF, 0x25, 0x00, 0x00, 0x00,
    0x00 };
constexpr std::size_t RelayPageBytes = 4'096;

// ---------------------------------------------------------------------------
// Native functions
// ---------------------------------------------------------------------------

using SrvDmgFn          = std::int64_t(__fastcall*)(void* game, void* missile, void* target, std::uint8_t* hit);
using AfterConversionFn = std::int64_t(__fastcall*)(void* attacker, void* defender, std::uint8_t* hit);
using GetUnitStatFn     = std::int32_t(__fastcall*)(void* unit, std::int32_t stat, std::int32_t layer);
using SkillLevelFn      = std::int32_t(__fastcall*)(void* unit, void* skill, std::uint64_t bonus, std::uint64_t unused);
using MissileOwnerFn    = void*(__fastcall*)(void* game, void* missile);
using MissileRecordFn   = const std::uint8_t*(__fastcall*)(std::uint8_t dataContext, std::int32_t missileId);
using UnitClassIdFn     = std::int32_t(__fastcall*)(void* unit, const char* file, std::int32_t line);
using ElementalLengthFn = std::int32_t(__fastcall*)(std::uint8_t dataContext, void* unit, std::int32_t skill, std::int32_t level);
using DataContextFn     = std::uint8_t(__fastcall*)(void* unit);
using DataTablesFn      = const std::uint8_t*(__fastcall*)(std::uint8_t dataContext);

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

struct Config {
    bool enabled = true;
    bool missiles = true;
    bool melee = true;
};

const D2RL::PluginContext* Context{};
std::uintptr_t Base{};
Config Settings{};
std::string ConfigProblems;

SrvDmgFn          OriginalSrvDmg01{};
AfterConversionFn AfterConversion{};
GetUnitStatFn     GetUnitStat{};
SkillLevelFn      SkillLevel{};
MissileOwnerFn    MissileOwner{};
MissileRecordFn   MissileRecord{};
UnitClassIdFn     UnitClassId{};
ElementalLengthFn ElementalLength{};
DataContextFn     DataContext{};
DataTablesFn      DataTables{};

std::uint8_t SkillDrivenMask{};
void*        RelayPage{};
bool         MissilesArmed{};
bool         MeleeArmed{};
std::uint8_t MeleeCallPatch[CallSize]{};

std::atomic<bool>          LogEachHit{false};
std::atomic<std::uint64_t> MissileHits{0};
std::atomic<std::uint64_t> MeleeHits{0};
std::atomic<std::uint64_t> NoSkill{0};
std::atomic<std::uint64_t> BadSourceLists{0};
std::atomic<std::int32_t>  LastKind{0};        // 0 none, 1 missile, 2 melee
std::atomic<std::int32_t>  LastSkill{-1};
std::atomic<std::int32_t>  LastLevel{0};
std::atomic<std::int32_t>  LastBefore{0};
std::atomic<std::int32_t>  LastAfter{0};
std::atomic<std::int32_t>  LastContext{-1};

// ---------------------------------------------------------------------------
// Memory helpers
// ---------------------------------------------------------------------------

template <typename T>
auto Read(const void* base, std::size_t offset) noexcept -> T {
    T value{};
    std::memcpy(&value, static_cast<const std::uint8_t*>(base) + offset, sizeof(T));
    return value;
}

template <typename T>
void Write(void* base, std::size_t offset, T value) noexcept {
    std::memcpy(static_cast<std::uint8_t*>(base) + offset, &value, sizeof(T));
}

template <typename T>
auto At(std::uint64_t rva) noexcept -> T {
    return reinterpret_cast<T>(Base + rva);
}

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

constexpr char DefaultConfigToml[] =
    "# Converted Poison (celestialrayone.converted-poison)\n"
    "#\n"
    "# Poison that a hit gets from converted weapon damage lasts the poison length\n"
    "# of the skill that converted it, and nothing else: no averaging with the other\n"
    "# poison on the hit and no minimum.\n"
    "#\n"
    "# Without the plugin the converted damage joins the hit's poison and lasts as\n"
    "# long as all of the hit's poison sources together: the skill's own length\n"
    "# plus every poison item's length, divided by the number of poison items when\n"
    "# there are two or more. A very short converted poison next to a long item\n"
    "# poison was stretched to the item poison's length and dealt many times its\n"
    "# intended damage.\n"
    "#\n"
    "# With the plugin, a hit that carries converted poison has the converting\n"
    "# skill's length: skills.txt ELen plus ELevLen1/2/3 for its level plus\n"
    "# ELenSymPerCalc, the same length the skill's own poison gets. The target's\n"
    "# poison length reduction still shortens it like any poison.\n"
    "#\n"
    "# Everything else works as before. The hit carries one poison whose damage per\n"
    "# frame is unchanged (the converted damage plus the other poison on the hit).\n"
    "# With states.txt canstack 0 the target keeps one poison, and any poison with\n"
    "# an equal or higher damage per frame replaces it. The other poison carried by\n"
    "# that same hit (poison items, the skill's own poison) runs for the same\n"
    "# length. With canstack 1 the poison items keep their own poison and length,\n"
    "# and the rest of the hit gets the skill's length.\n"
    "#\n"
    "# A skill whose length is 0 leaves the hit with no poison over time. A missile\n"
    "# that belongs to no skill (missiles.txt Skill empty and the row not driven by\n"
    "# a skill) is left to the game.\n"
    "#\n"
    "# Console:  convertedpoison      status and counters\n"
    "#           convertedpoison log  toggles a log line per retimed hit\n"
    "\n"
    "# Master switch.\n"
    "enabled = true\n"
    "\n"
    "# Missiles with missiles.txt pSrvDmgFunc 1 (DmgCalc1 percent of the physical\n"
    "# damage converted to the missile's element).\n"
    "missile_conversion = true\n"
    "\n"
    "# Melee skills with a skills.txt calc4 conversion and EType pois, or the\n"
    "# random element when it rolls poison.\n"
    "melee_conversion = true\n";

auto Trim(std::string_view text) noexcept -> std::string_view {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r')) text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) text.remove_suffix(1);
    return text;
}

void ApplyBool(std::string_view key, std::string_view value, bool& target) {
    if (value == "true") {
        target = true;
    } else if (value == "false") {
        target = false;
    } else {
        ConfigProblems += std::string(key) + " must be true or false; ";
    }
}

void ApplyConfigLine(std::string_view key, std::string_view value) {
    if (key == "enabled") {
        ApplyBool(key, value, Settings.enabled);
    } else if (key == "missile_conversion") {
        ApplyBool(key, value, Settings.missiles);
    } else if (key == "melee_conversion") {
        ApplyBool(key, value, Settings.melee);
    } else {
        ConfigProblems += "unknown setting " + std::string(key) + "; ";
    }
}

void ParseConfig(std::string_view text) {
    while (!text.empty()) {
        const std::size_t breakAt = text.find('\n');
        std::string_view line = text.substr(0, breakAt);
        text = breakAt == std::string_view::npos ? std::string_view{} : text.substr(breakAt + 1);
        const std::size_t comment = line.find('#');
        if (comment != std::string_view::npos) line = line.substr(0, comment);
        line = Trim(line);
        if (line.empty() || line.front() == '[') continue;
        const std::size_t equals = line.find('=');
        if (equals == std::string_view::npos) continue;
        ApplyConfigLine(Trim(line.substr(0, equals)), Trim(line.substr(equals + 1)));
    }
}

void ReadConfiguration() {
    if (!Context->EnsureConfig(DefaultConfigToml)) {
        Context->LogWarn("ConvertedPoison: the config file could not be created; using defaults.");
    } else {
        std::string buffer(MaximumConfigBytes, '\0');
        if (Context->ReadConfig(buffer.data(), static_cast<std::uint32_t>(buffer.size()), nullptr)) {
            buffer.resize(std::strlen(buffer.c_str()));
            ParseConfig(buffer);
        } else {
            Context->LogWarn("ConvertedPoison: the config file could not be read; using defaults.");
        }
    }
    if (!ConfigProblems.empty()) {
        D2RL::LogWarnF(Context, "ConvertedPoison: %s", ConfigProblems.c_str());
    }
}

// ---------------------------------------------------------------------------
// The new length
// ---------------------------------------------------------------------------

// The same test as the damage commit at 0x44D14D .. 0x44D1C9.
auto PoisonCanStack(std::uint8_t dataContext) noexcept -> bool {
    const std::uint8_t* tables = DataTables(dataContext);
    if (tables == nullptr || Read<std::uint64_t>(tables, StatesCountOffset) <= PoisonState) return false;
    const auto* rows = Read<const std::uint8_t*>(tables, StatesRowsOffset);
    return rows != nullptr && rows[PoisonState * StatesRowSize + StatesCanStackOffset] != 0;
}

// What the commit subtracts from the hit length before it applies the rest
// (0x44D1F5 .. 0x44D207): every entry's length, summed.
auto SourceLengths(const std::uint8_t* hit, std::int64_t& sum) noexcept -> bool {
    const auto* data = Read<const std::uint8_t*>(hit, HitSourcesOffset);
    const auto count = Read<std::uint64_t>(hit, HitSourceCountOffset);
    const auto capacity = Read<std::uint64_t>(hit, HitSourceCapacityOffset) & ~SourceInlineFlag;
    if (count > capacity || count > MaximumSources || (count != 0 && data == nullptr)) return false;
    sum = 0;
    for (std::uint64_t index = 0; index < count; ++index) {
        sum += Read<std::int32_t>(data, static_cast<std::size_t>(index) * SourceEntryBytes + SourceLengthOffset);
    }
    return true;
}

void SetConvertedLength(std::uint8_t* hit, std::uint8_t dataContext, std::int32_t skillLength, std::int32_t kind,
        std::int32_t skill, std::int32_t level) noexcept {
    const std::int32_t before = Read<std::int32_t>(hit, HitPoisonLengthOffset);
    std::int64_t length = std::max<std::int32_t>(skillLength, 0);
    if (PoisonCanStack(dataContext)) {
        std::int64_t sources = 0;
        if (!SourceLengths(hit, sources)) {
            BadSourceLists.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        length += sources;
    }
    length = std::clamp<std::int64_t>(length, 0, std::numeric_limits<std::int32_t>::max());
    Write<std::int32_t>(hit, HitPoisonLengthOffset, static_cast<std::int32_t>(length));

    (kind == 1 ? MissileHits : MeleeHits).fetch_add(1, std::memory_order_relaxed);
    LastKind.store(kind, std::memory_order_relaxed);
    LastSkill.store(skill, std::memory_order_relaxed);
    LastLevel.store(level, std::memory_order_relaxed);
    LastBefore.store(before, std::memory_order_relaxed);
    LastAfter.store(static_cast<std::int32_t>(length), std::memory_order_relaxed);
    LastContext.store(dataContext, std::memory_order_relaxed);
    if (LogEachHit.load(std::memory_order_relaxed)) {
        D2RL::LogInfoF(Context, "ConvertedPoison: %s skill %d level %d, rate %d, length %d -> %d frames.",
            kind == 1 ? "missile" : "melee", skill, level, Read<std::int32_t>(hit, HitPoisonRateOffset), before,
            static_cast<std::int32_t>(length));
    }
}

// The skill and level 0x3BF9B0 gives a missile's damage.
auto MissileSkill(void* game, void* missile, std::uint8_t dataContext, std::int32_t& skill, std::int32_t& level,
        void*& owner) noexcept -> bool {
    const std::int32_t missileId = UnitClassId(missile, "converted-poison", 0);
    const std::uint8_t* row = MissileRecord(dataContext, missileId);
    if (row == nullptr) return false;
    const std::int16_t rowSkill = Read<std::int16_t>(row, MissileRowSkillOffset);
    const bool skillDriven = (Read<std::uint8_t>(row, MissileRowFlagsOffset) & SkillDrivenMask) != 0;
    if (rowSkill <= 0 && !skillDriven) return false;
    std::int32_t id = rowSkill;
    if (skillDriven) {
        const auto* data = Read<const std::uint8_t*>(missile, MissileDataOffset);
        if (data != nullptr) {
            const std::int16_t own = Read<std::int16_t>(data, MissileDataSkillOffset);
            if (own > 0) id = own;
        }
    }
    if (id < 0) return false;
    owner = MissileOwner(game, missile);
    if (owner == nullptr) return false;
    skill = id;
    level = GetUnitStat(missile, LevelStat, 0);
    return true;
}

// The skill the attacker is using, read the way 0x350B40 reads it.
auto MeleeSkill(void* attacker, std::int32_t& skill, std::int32_t& level) noexcept -> bool {
    void* list = Read<void*>(attacker, UnitSkillListOffset);
    if (list == nullptr) return false;
    void* used = Read<void*>(list, SkillListUsedOffset);
    if (used == nullptr) return false;
    const auto* record = Read<const std::uint8_t*>(used, 0);
    if (record == nullptr) return false;
    skill = Read<std::uint16_t>(record, 0);
    level = SkillLevel(attacker, used, 1, 0);
    return true;
}

// ---------------------------------------------------------------------------
// Hooks
// ---------------------------------------------------------------------------

auto __fastcall HookedSrvDmg01(void* game, void* missile, void* target, std::uint8_t* hit) -> std::int64_t {
    const std::int32_t rateBefore = hit != nullptr ? Read<std::int32_t>(hit, HitPoisonRateOffset) : 0;
    const std::int64_t result = OriginalSrvDmg01(game, missile, target, hit);
    if (hit == nullptr || game == nullptr || missile == nullptr) return result;
    if (Read<std::int32_t>(hit, HitPoisonRateOffset) <= rateBefore) return result;

    const std::uint8_t dataContext = Read<std::uint8_t>(game, GameDataContextOffset);
    std::int32_t skill = -1;
    std::int32_t level = 0;
    void* owner = nullptr;
    if (!MissileSkill(game, missile, dataContext, skill, level, owner)) {
        NoSkill.fetch_add(1, std::memory_order_relaxed);
        return result;
    }
    SetConvertedLength(hit, dataContext, ElementalLength(dataContext, owner, skill, level), 1, skill, level);
    return result;
}

auto __fastcall MeleeConversionHook(void* attacker, void* defender, std::uint8_t* hit, std::uint64_t packed)
        -> std::int64_t {
    const auto element = static_cast<std::uint32_t>(packed >> 32) & 0xFFU;
    const auto converted = static_cast<std::int32_t>(static_cast<std::uint32_t>(packed));
    // The game added converted / 8 to the rate (0x44CD2E .. 0x44CD3E).
    if (element == PoisonElement && converted / 8 > 0 && hit != nullptr && attacker != nullptr) {
        std::int32_t skill = -1;
        std::int32_t level = 0;
        if (MeleeSkill(attacker, skill, level)) {
            const std::uint8_t dataContext = DataContext(attacker);
            SetConvertedLength(hit, dataContext, ElementalLength(dataContext, attacker, skill, level), 2, skill,
                level);
        } else {
            NoSkill.fetch_add(1, std::memory_order_relaxed);
        }
    }
    return AfterConversion(attacker, defender, hit);
}

// ---------------------------------------------------------------------------
// Installation
// ---------------------------------------------------------------------------

auto Verify(std::uint64_t rva, const std::uint8_t* bytes, std::size_t size, const char* what) -> bool {
    if (Context->CheckExpectedBytes(rva, bytes, static_cast<std::uint32_t>(size))) return true;
    D2RL::LogErrorF(Context, "ConvertedPoison: %s at 0x%llX does not match D2RLoader 1.3.1; wrong game build or "
        "another plugin patched it.", what, static_cast<unsigned long long>(rva));
    return false;
}

auto CanEncodeRel32(std::uintptr_t from, std::uintptr_t to) noexcept -> bool {
    const std::int64_t delta = static_cast<std::int64_t>(to) - static_cast<std::int64_t>(from + CallSize);
    return delta >= std::numeric_limits<std::int32_t>::min() && delta <= std::numeric_limits<std::int32_t>::max();
}

auto AllocateNear(std::uintptr_t site, std::size_t size) noexcept -> void* {
    SYSTEM_INFO systemInfo{};
    GetSystemInfo(&systemInfo);
    const auto granularity = static_cast<std::uintptr_t>(systemInfo.dwAllocationGranularity);
    const auto aligned = site & ~(granularity - 1U);
    for (std::uintptr_t delta = granularity; delta < 0x7000'0000ULL; delta += granularity) {
        const std::uintptr_t candidate = aligned + delta;
        if (!CanEncodeRel32(site, candidate + size)) break;
        if (void* memory = VirtualAlloc(reinterpret_cast<void*>(candidate), size, MEM_COMMIT | MEM_RESERVE,
                PAGE_READWRITE)) {
            return memory;
        }
    }
    return nullptr;
}

auto WriteRelaySlot(void* target) noexcept -> bool {
    auto* slot = static_cast<std::uint8_t*>(RelayPage) + sizeof(MeleeRelayCode);
    DWORD previous{};
    if (!VirtualProtect(RelayPage, RelayPageBytes, PAGE_EXECUTE_READWRITE, &previous)) return false;
    const auto address = reinterpret_cast<std::uintptr_t>(target);
    std::memcpy(slot, &address, sizeof(address));
    DWORD ignored{};
    VirtualProtect(RelayPage, RelayPageBytes, PAGE_EXECUTE_READ, &ignored);
    FlushInstructionCache(GetCurrentProcess(), RelayPage, RelayPageBytes);
    return true;
}

auto InstallMelee() -> bool {
    const bool helpers = Verify(DataContextRva, DataContextBytes, sizeof(DataContextBytes), "the unit data context")
        && Verify(SkillLevelRva, LoaderThunkBytes, sizeof(LoaderThunkBytes), "the skill level thunk")
        && Verify(MeleeSwitchRva, MeleeSwitchWitness, sizeof(MeleeSwitchWitness), "the melee conversion")
        && Verify(MeleeWindowRva, MeleeWindow, sizeof(MeleeWindow), "the call after the melee conversion");
    if (!helpers) return false;

    const std::uintptr_t site = Base + MeleeCallRva;
    RelayPage = AllocateNear(site, RelayPageBytes);
    if (RelayPage == nullptr) {
        Context->LogError("ConvertedPoison: no memory within reach of 0x44CD88 for the melee relay.");
        return false;
    }
    auto* page = static_cast<std::uint8_t*>(RelayPage);
    std::memset(page, 0xCC, RelayPageBytes);
    std::memcpy(page, MeleeRelayCode, sizeof(MeleeRelayCode));
    const auto hook = reinterpret_cast<std::uintptr_t>(&MeleeConversionHook);
    std::memcpy(page + sizeof(MeleeRelayCode), &hook, sizeof(hook));
    DWORD previous{};
    if (!VirtualProtect(page, RelayPageBytes, PAGE_EXECUTE_READ, &previous)) {
        Context->LogError("ConvertedPoison: the melee relay could not be made executable.");
        return false;
    }
    FlushInstructionCache(GetCurrentProcess(), page, RelayPageBytes);

    const auto relay = reinterpret_cast<std::uintptr_t>(page);
    if (!CanEncodeRel32(site, relay)) return false;
    MeleeCallPatch[0] = 0xE8;
    const auto displacement = static_cast<std::int32_t>(static_cast<std::int64_t>(relay)
        - static_cast<std::int64_t>(site + CallSize));
    std::memcpy(MeleeCallPatch + 1, &displacement, sizeof(displacement));
    return Context->PatchBytes(MeleeCallRva, MeleeWindow + MeleeCallOffset, CallSize, MeleeCallPatch, CallSize);
}

auto InstallMissiles() -> bool {
    const bool helpers = Verify(GetUnitStatRva, LoaderThunkBytes, sizeof(LoaderThunkBytes), "the unit stat thunk")
        && Verify(MissileOwnerRva, MissileOwnerBytes, sizeof(MissileOwnerBytes), "the missile owner getter")
        && Verify(MissileRecordRva, MissileRecordBytes, sizeof(MissileRecordBytes), "the missiles.txt row getter")
        && Verify(UnitClassIdRva, UnitClassIdBytes, sizeof(UnitClassIdBytes), "the unit class id getter")
        && Verify(SrvDmg01Rva, SrvDmg01Prologue, sizeof(SrvDmg01Prologue), "pSrvDmgFunc 1");
    if (!helpers) return false;

    const std::uint32_t mask = Read<std::uint32_t>(reinterpret_cast<const void*>(Base + BitMasksRva),
        SkillDrivenBit * sizeof(std::uint32_t));
    if (mask != (1U << SkillDrivenBit)) {
        D2RL::LogErrorF(Context, "ConvertedPoison: the bit mask table at 0x%llX holds 0x%X for bit %u.",
            static_cast<unsigned long long>(BitMasksRva), mask, static_cast<unsigned>(SkillDrivenBit));
        return false;
    }
    SkillDrivenMask = static_cast<std::uint8_t>(mask);

    const auto slot = Read<std::uintptr_t>(reinterpret_cast<const void*>(Base + SrvDmgTableRva), sizeof(std::uintptr_t));
    if (slot != Base + SrvDmg01Rva) {
        D2RL::LogWarnF(Context, "ConvertedPoison: pSrvDmgFunc slot 1 points to 0x%llX, not 0x%llX; missiles with "
            "pSrvDmgFunc 1 may not reach the hook.", static_cast<unsigned long long>(slot - Base),
            static_cast<unsigned long long>(SrvDmg01Rva));
    }
    return Context->InstallInlineHook(SrvDmg01Rva, SrvDmg01Prologue, sizeof(SrvDmg01Prologue), &HookedSrvDmg01,
        &OriginalSrvDmg01);
}

// ---------------------------------------------------------------------------
// Console command
// ---------------------------------------------------------------------------

auto __cdecl StatusCommand(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command, void*) noexcept
        -> D2RL::ConsoleCommandResult {
    if (command == nullptr || command->plugin == nullptr) return D2RL::ConsoleCommandResult::Failed;
    const std::string_view args = command->args != nullptr
        ? Trim(std::string_view(command->args, command->argsLength)) : std::string_view{};
    char message[512];
    if (args == "log") {
        const bool on = !LogEachHit.load(std::memory_order_relaxed);
        LogEachHit.store(on, std::memory_order_relaxed);
        std::snprintf(message, sizeof(message), "Converted Poison: per-hit log lines %s.", on ? "on" : "off");
        command->plugin->WriteConsoleMessage(message);
        return D2RL::ConsoleCommandResult::Handled;
    }
    const std::int32_t context = LastContext.load(std::memory_order_relaxed);
    const char* canStack = "unknown";
    if (context >= 0 && DataTables != nullptr) {
        canStack = PoisonCanStack(static_cast<std::uint8_t>(context)) ? "1" : "0";
    }
    const std::int32_t kind = LastKind.load(std::memory_order_relaxed);
    char last[160] = "none yet";
    if (kind != 0) {
        std::snprintf(last, sizeof(last), "%s skill %d level %d, length %d -> %d frames",
            kind == 1 ? "missile" : "melee", LastSkill.load(std::memory_order_relaxed),
            LastLevel.load(std::memory_order_relaxed), LastBefore.load(std::memory_order_relaxed),
            LastAfter.load(std::memory_order_relaxed));
    }
    std::snprintf(message, sizeof(message),
        "Converted Poison: %s | missiles %s, melee %s | poison canstack %s | retimed hits: missile %llu, melee %llu "
        "| no skill %llu | bad source lists %llu | last: %s",
        Settings.enabled ? "on" : "off", MissilesArmed ? "armed" : "off", MeleeArmed ? "armed" : "off", canStack,
        static_cast<unsigned long long>(MissileHits.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(MeleeHits.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(NoSkill.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(BadSourceLists.load(std::memory_order_relaxed)), last);
    command->plugin->WriteConsoleMessage(message);
    return D2RL::ConsoleCommandResult::Handled;
}

void RegisterStatusCommand() noexcept {
    if (!Context->RegisterConsoleCommand("convertedpoison", &StatusCommand,
            "Converted Poison status and counters. 'convertedpoison log' toggles a log line per retimed hit.")) {
        Context->LogWarn("ConvertedPoison: the console command was refused.");
    }
}

constexpr D2RL::PluginInfo Info{
    .infoSize    = D2RL::PluginInfoSize,
    .abiVersion  = D2RL_PLUGIN_ABI_VERSION,
    .id          = "celestialrayone.converted-poison",
    .name        = "Converted Poison",
    .version     = "1.0.0",
    .author      = "CelestialRayOne",
    .description = "Poison converted from weapon damage lasts the converting skill's poison length instead of "
                   "the hit's averaged poison length.",
    .flags       = D2RL::PluginFlags::Server | D2RL::PluginFlags::NativeHooks,
};

}  // namespace

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* {
    return &Info;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
    if (!D2RL::HasContext(context)) return false;
    Context = context;
    Base = context->exeBase;

    ReadConfiguration();
    if (!Settings.enabled || (!Settings.missiles && !Settings.melee)) {
        Context->LogInfo("ConvertedPoison: disabled by configuration; no hooks installed.");
        RegisterStatusCommand();
        return true;
    }

    if (!Verify(ElementalLengthRva, ElementalLengthBytes, sizeof(ElementalLengthBytes),
            "SKILLS_GetElementalLength")
        || !Verify(DataTablesRva, DataTablesBytes, sizeof(DataTablesBytes), "the data tables getter")) {
        return false;
    }

    AfterConversion = At<AfterConversionFn>(AfterConversionRva);
    GetUnitStat     = At<GetUnitStatFn>(GetUnitStatRva);
    SkillLevel      = At<SkillLevelFn>(SkillLevelRva);
    MissileOwner    = At<MissileOwnerFn>(MissileOwnerRva);
    MissileRecord   = At<MissileRecordFn>(MissileRecordRva);
    UnitClassId     = At<UnitClassIdFn>(UnitClassIdRva);
    ElementalLength = At<ElementalLengthFn>(ElementalLengthRva);
    DataContext     = At<DataContextFn>(DataContextRva);
    DataTables      = At<DataTablesFn>(DataTablesRva);

    if (Settings.missiles) {
        MissilesArmed = InstallMissiles();
        if (!MissilesArmed) Context->LogError("ConvertedPoison: missile conversion NOT armed, see above.");
    }
    if (Settings.melee) {
        MeleeArmed = InstallMelee();
        if (!MeleeArmed) Context->LogError("ConvertedPoison: melee conversion NOT armed, see above.");
    }
    if (!MissilesArmed && !MeleeArmed) return false;

    D2RL::LogInfoF(Context, "ConvertedPoison: active (missile conversion %s, melee conversion %s).",
        MissilesArmed ? "on" : "off", MeleeArmed ? "on" : "off");
    RegisterStatusCommand();
    return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
    if (Context == nullptr) return;
    if (MeleeArmed) {
        if (Context->PatchBytes(MeleeCallRva, MeleeCallPatch, CallSize, MeleeWindow + MeleeCallOffset, CallSize)) {
            MeleeArmed = false;
        }
        // The relay page stays: a thread may be inside it right now. It now
        // goes straight to the original callee.
        WriteRelaySlot(reinterpret_cast<void*>(Base + AfterConversionRva));
    }
}

}  // namespace CelestialRayOne::ConvertedPoison
