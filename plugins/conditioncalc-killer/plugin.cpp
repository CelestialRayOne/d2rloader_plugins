// ConditionCalc Killer  (celestialrayone.conditioncalc-killer)
//
// treasureclassex.txt ConditionCalc reads the player who got the kill:
// stat(), skill() and sklvl() read that player (the owner of a summon or
// hireling), cond() keeps testing the monster. Nothing in the save format
// changes.
//
// Target: D2RLoader 1.3.1 running D2R 3.3. Every RVA and byte below was read
// from the dumped D2RLoader.exe image (base 0x140000000) and decoded before
// being relied on. On any build whose bytes differ, the plugin installs
// nothing, logs why, and stays loaded so its console command can explain.
//
// ---------------------------------------------------------------------------
// 1. Stock behaviour
// ---------------------------------------------------------------------------
//   The drop roll 0x4404F0 (game, monster, killer, treasure class, ...) homes
//   the killer once, at 0x44055B mov [rbp-80h],r8, and rbp is its frame base
//   for the whole body (the only writes to rbp are the prologue lea and the
//   epilogue pop; [rbp-80h] is written nowhere else). It checks a treasure
//   class's conditions through 0x444920 (game, tc, monster, expansion flag):
//     0x440643  the treasure class being rolled
//     0x440903  a conditional treasure-class entry inside it
//   0x444920 tests the ladder flag (0x403FE0), the quest flag (0x514FD0 with
//   states 0, 15, 13) and, for flag bit 4, ConditionCalc at tc+20h through
//   0x3B5380, which puts the monster in every one of the seven context slots.
//   Who reads which slot (the nine callbacks at 0x1D09C50):
//     +08 rand   +10 stat   +18 skill, sklvl   +20 and +38 cond
//     +28 sksrc  +30 miss
//
// ---------------------------------------------------------------------------
// 2. The change
// ---------------------------------------------------------------------------
//   Both 5-byte calls to 0x444920 become calls to a relay on a page within
//   rel32 reach:
//     mov  rax,[rbp-80h]        ; the killer, from the roll's own frame
//     sub  rsp,38h
//     mov  [rsp+20h],rax        ; fifth argument
//     call [rip+slot]           ; ConditionCheckWithKiller
//     add  rsp,38h
//     ret
//   rcx, rdx, r8 and r9 pass through untouched. Assembled with keystone,
//   decoded with capstone, and run under unicorn (arguments, the fifth
//   argument, 16-byte call alignment, return value, stack).
//
//   ConditionCheckWithKiller resolves the credited player (a player killer, or
//   the player owner of a minion killer via 0x4A53C0). With none it calls the
//   stock 0x444920 unchanged. With one it runs 0x444920's three tests in the
//   same order through the same calls, and evaluates ConditionCalc exactly as
//   0x3B5380 does (pool +0D90h/+0D98h of the data tables, vtable 0x1D0AEC8,
//   +40 the monster's level from stat 12, +44 = 0, interpreter 0xA234B0 with
//   0x79100, 0x1D09C50, 9) except that slots +10 and +18 hold the player.

#include <D2RLPlugin/api.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <array>
#include <atomic>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

namespace CelestialRayOne::ConditionCalcKiller {
namespace {

constexpr char PluginVersion[] = "1.0.0";

constexpr const char DefaultConfigToml[] = R"TOML(# celestialrayone.conditioncalc-killer
#
# ConditionCalc Killer
#   Lets treasureclassex.txt ConditionCalc read the player who got the kill.
#
# What changes in a ConditionCalc formula
#   stat('x'.accr)            reads the player who got the kill
#   skill('Name'.lvl), sklvl  read the player who got the kill
#   cond(...)                 still tests the monster and the area, as stock
#                             (MonsterTestElite, Desecrated and the rest)
#   everything else           as stock
#
#   Examples (the treasure class only drops when the formula is not 0):
#     (stat('item_magicbonus'.accr)>=100)
#     (stat('level'.accr)>=90)
#     (skill('Some Skill'.lvl)>0)
#
# Who counts as the player who got the kill
#   - A player's own kill: that player.
#   - A summon's or hireling's kill: its owner, when credit_minion_owner is on.
#   - No player at all (a monster killed by a trap or another monster, or a
#     minion whose owner is not a player): the formula reads the monster
#     exactly like the stock game.
#
# Scope
#   Both places treasureclassex.txt ConditionCalc is checked: the monster's own
#   treasure class, and conditional treasure-class entries inside other
#   treasure classes. DropConditionCalc in weapons.txt, armor.txt and misc.txt
#   is a different column and is not changed.
#
# Built for D2RLoader 1.3.1 (D2R 3.3). On any other build the byte checks fail
# and the plugin loads without changing the game; the console command
# "conditioncalc" says why.

# Master switch. false loads the plugin without changing the game.
enabled = true

# true: a summon's or hireling's kill reads its owner's stats.
# false: such kills read the monster, like stock.
credit_minion_owner = true

# true writes the first evaluations to the plugin log (who was read and the
# result). Keep false for play.
diagnostics = false
)TOML";

// ---------------------------------------------------------------------------
// Native addresses (D2RLoader 1.3.1 image, D2R 3.3)
// ---------------------------------------------------------------------------
constexpr std::uint64_t KillerHomeRva      = 0x440547;
constexpr std::uint64_t TopSiteRva         = 0x440643;
constexpr std::uint64_t EntrySiteRva       = 0x440903;
constexpr std::uint64_t ConditionCheckRva  = 0x444920;
constexpr std::uint64_t ConditionEvalRva   = 0x3B5380;
constexpr std::uint64_t LadderCheckRva     = 0x403FE0;
constexpr std::uint64_t QuestCheckRva      = 0x514FD0;
constexpr std::uint64_t GetDataTablesRva   = 0x300A90;
constexpr std::uint64_t InterpretRva       = 0xA234B0;
constexpr std::uint64_t TokenCallbackRva   = 0x079100;
constexpr std::uint64_t FunctionTableRva   = 0x1D09C50;
constexpr std::uint64_t ContextVtableRva   = 0x1D0AEC8;
constexpr std::uint64_t ReadUnitStatRva    = 0x2F5020;
constexpr std::uint64_t GetUnitTypeRva     = 0x34B9D0;
constexpr std::uint64_t GetOwnerRva        = 0x4A53C0;

constexpr std::size_t ConditionPoolBase  = 0xD90;   // data tables: treasure-class formula pool
constexpr std::size_t ConditionPoolSize  = 0xD98;
constexpr std::size_t GameDataBankOffset = 0x106;   // game: data tables bank byte
constexpr std::uint32_t LevelStat        = 12;

constexpr std::size_t RelaySlot = 24;

constexpr auto RelayBlob = std::to_array<std::uint8_t>({
    0x48, 0x8B, 0x45, 0x80, 0x48, 0x83, 0xEC, 0x38, 0x48, 0x89, 0x44, 0x24,
    0x20, 0xFF, 0x15, 0x05, 0x00, 0x00, 0x00, 0x48, 0x83, 0xC4, 0x38, 0xC3,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
});

constexpr auto KillerHome = std::to_array<std::uint8_t>({
    0x48, 0x89, 0x54, 0x24, 0x58, 0x48, 0x8D, 0x95, 0x98, 0x00, 0x00, 0x00,
    0x4C, 0x89, 0x75, 0xA0, 0x4D, 0x89, 0x7B, 0xC0, 0x4C, 0x89, 0x45, 0x80,
    0x48, 0x89, 0x4C, 0x24, 0x70,
});

constexpr auto TopSiteBefore = std::to_array<std::uint8_t>({
    0x41, 0x80, 0xFF, 0x01, 0x4C, 0x8B, 0xC6, 0x48, 0x8B, 0xD3, 0x49, 0x8B,
    0xCC, 0x41, 0x0F, 0x95, 0xC1,
});

constexpr auto TopSiteCall = std::to_array<std::uint8_t>({
    0xE8, 0xD8, 0x42, 0x00, 0x00,
});

constexpr auto TopSiteAfter = std::to_array<std::uint8_t>({
    0x84, 0xC0, 0x0F, 0x84, 0x33, 0x0C, 0x00, 0x00,
});

constexpr auto EntrySiteBefore = std::to_array<std::uint8_t>({
    0x4C, 0x8B, 0x44, 0x24, 0x58, 0x45, 0x0F, 0xB6, 0xCC, 0x48, 0x8B, 0x4C,
    0x24, 0x70, 0x48, 0x8B, 0xD0,
});

constexpr auto EntrySiteCall = std::to_array<std::uint8_t>({
    0xE8, 0x18, 0x40, 0x00, 0x00,
});

constexpr auto EntrySiteAfter = std::to_array<std::uint8_t>({
    0x84, 0xC0, 0xEB, 0x43,
});

constexpr auto ConditionCheckBody = std::to_array<std::uint8_t>({
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89,
    0x74, 0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x20, 0xF6, 0x42, 0x25, 0x01,
    0x41, 0x0F, 0xB6, 0xF9, 0x49, 0x8B, 0xE8, 0x48, 0x8B, 0xDA, 0x48, 0x8B,
    0xF1, 0x74, 0x12, 0x44, 0x0F, 0xB7, 0x42, 0x06, 0x0F, 0xB7, 0x52, 0x04,
    0xE8, 0x8B, 0xF6, 0xFB, 0xFF, 0x84, 0xC0, 0x74, 0x6A, 0xF6, 0x43, 0x25,
    0x02, 0x74, 0x47, 0x40, 0x84, 0xFF, 0x74, 0x05, 0x8B, 0x7B, 0x0C, 0xEB,
    0x03, 0x8B, 0x7B, 0x08, 0x83, 0xFF, 0x29, 0x77, 0x35, 0x45, 0x33, 0xC0,
    0x8B, 0xD7, 0x48, 0x8B, 0xCE, 0xE8, 0x52, 0x06, 0x0D, 0x00, 0x85, 0xC0,
    0x75, 0x24, 0x44, 0x8D, 0x40, 0x0F, 0x8B, 0xD7, 0x48, 0x8B, 0xCE, 0xE8,
    0x40, 0x06, 0x0D, 0x00, 0x85, 0xC0, 0x75, 0x12, 0x44, 0x8D, 0x40, 0x0D,
    0x8B, 0xD7, 0x48, 0x8B, 0xCE, 0xE8, 0x2E, 0x06, 0x0D, 0x00, 0x85, 0xC0,
    0x74, 0x1D, 0xF6, 0x43, 0x25, 0x04, 0x74, 0x1B, 0x44, 0x8B, 0x43, 0x20,
    0x48, 0x8B, 0xD5, 0x0F, 0xB6, 0x8E, 0x06, 0x01, 0x00, 0x00, 0xE8, 0xC1,
    0x09, 0xF7, 0xFF, 0x85, 0xC0, 0x75, 0x04, 0x32, 0xC0, 0xEB, 0x02, 0xB0,
    0x01,
});

constexpr auto ConditionEvalBody = std::to_array<std::uint8_t>({
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x56, 0x57,
    0x41, 0x56, 0x48, 0x81, 0xEC, 0xA0, 0x00, 0x00, 0x00, 0x41, 0x8B, 0xD8,
    0x48, 0x8B, 0xFA, 0x44, 0x0F, 0xB6, 0xF1, 0xE8, 0xEC, 0xB6, 0xF4, 0xFF,
    0x48, 0x8B, 0xE8, 0x8B, 0xF3, 0x48, 0x3B, 0x98, 0x98, 0x0D, 0x00, 0x00,
    0x72, 0x07, 0x33, 0xC0, 0xE9, 0x07, 0x01, 0x00, 0x00, 0xC6, 0x84, 0x24,
    0x94, 0x00, 0x00, 0x00, 0x00, 0x48, 0x8D, 0x05, 0x00, 0x5B, 0x95, 0x01,
    0x48, 0x89, 0x44, 0x24, 0x50, 0x48, 0x89, 0x7C, 0x24, 0x58, 0x48, 0x89,
    0x7C, 0x24, 0x68, 0x48, 0x89, 0x7C, 0x24, 0x78, 0x48, 0x89, 0xBC, 0x24,
    0x80, 0x00, 0x00, 0x00, 0x48, 0x89, 0x7C, 0x24, 0x60, 0x48, 0x89, 0x7C,
    0x24, 0x70, 0x48, 0x89, 0xBC, 0x24, 0x88, 0x00, 0x00, 0x00, 0x48, 0x85,
    0xFF, 0x74, 0x18, 0x45, 0x33, 0xC0, 0x48, 0x8B, 0xCF, 0x41, 0x8D, 0x50,
    0x0C, 0xE8, 0x16, 0xFC, 0xF3, 0xFF, 0x89, 0x84, 0x24, 0x90, 0x00, 0x00,
    0x00, 0xEB, 0x0B, 0xC7, 0x84, 0x24, 0x90, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x48, 0x8B, 0x9D, 0x98, 0x0D, 0x00, 0x00, 0x48, 0x2B, 0xDE,
    0x48, 0x63, 0xC3, 0x48, 0x3B, 0xC3, 0x75, 0x04, 0x85, 0xDB, 0x79, 0x1A,
    0x48, 0x8D, 0x8C, 0x24, 0xD0, 0x00, 0x00, 0x00, 0xC6, 0x84, 0x24, 0xD0,
    0x00, 0x00, 0x00, 0x00, 0xE8, 0x17, 0x15, 0xCD, 0xFF, 0x84, 0xC0, 0x74,
    0x01, 0xCC, 0x48, 0x8D, 0xBD, 0x90, 0x0D, 0x00, 0x00, 0x48, 0x89, 0xB4,
    0x24, 0xD8, 0x00, 0x00, 0x00, 0x48, 0x3B, 0x77, 0x08, 0x72, 0x21, 0x48,
    0x8D, 0x84, 0x24, 0xD8, 0x00, 0x00, 0x00, 0x48, 0x89, 0x7C, 0x24, 0x48,
    0x48, 0x8D, 0x4C, 0x24, 0x40, 0x48, 0x89, 0x44, 0x24, 0x40, 0xE8, 0xC1,
    0xEE, 0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x01, 0xCC, 0x48, 0x8B, 0x94, 0x24,
    0xD8, 0x00, 0x00, 0x00, 0x48, 0x8D, 0x44, 0x24, 0x50, 0x48, 0x03, 0x17,
    0x4C, 0x8D, 0x0D, 0x65, 0x3C, 0xCC, 0xFF, 0x48, 0x89, 0x44, 0x24, 0x30,
    0x44, 0x8B, 0xC3, 0x48, 0x8D, 0x05, 0xA6, 0x47, 0x95, 0x01, 0xC7, 0x44,
    0x24, 0x28, 0x09, 0x00, 0x00, 0x00, 0x41, 0x0F, 0xB6, 0xCE, 0x48, 0x89,
    0x44, 0x24, 0x20, 0xE8, 0xF0, 0xDF, 0x66, 0x00,
});

constexpr auto LadderCheckEntry = std::to_array<std::uint8_t>({
    0x80, 0xB9, 0x01, 0x01, 0x00, 0x00, 0x00, 0x75, 0x1C, 0x66, 0x85, 0xD2, 0x74, 0x17, 0x66, 0x45,
    0x85, 0xC0, 0x78, 0x11, 0x83, 0xB9, 0x08, 0x01,
});
constexpr auto QuestCheckEntry = std::to_array<std::uint8_t>({
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48,
    0x8B, 0x99, 0xD8, 0x21, 0x00, 0x00, 0x41, 0x8B,
});
constexpr auto GetDataTablesEntry = std::to_array<std::uint8_t>({
    0x48, 0x83, 0xEC, 0x28, 0x0F, 0xB6, 0xC1, 0x48, 0x89, 0x44, 0x24, 0x38, 0x48, 0x83, 0xF8, 0x04,
});
constexpr auto InterpretEntry = std::to_array<std::uint8_t>({
    0x48, 0x89, 0x5C, 0x24, 0x18, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57,
    0x48, 0x8D, 0x6C, 0x24, 0x80, 0x48, 0x81, 0xEC,
});
constexpr auto TokenCallbackBody = std::to_array<std::uint8_t>({0x33, 0xC0, 0xC3});
constexpr auto ReadUnitStatThunk = std::to_array<std::uint8_t>({0xFF, 0x25, 0xF2, 0x51, 0xB3, 0x03});
constexpr auto GetUnitTypeBody = std::to_array<std::uint8_t>({
    0x48, 0x83, 0xEC, 0x28, 0x48, 0x85, 0xC9, 0x75, 0x1D, 0x88, 0x4C, 0x24,
    0x30, 0x48, 0x8D, 0x4C, 0x24, 0x30, 0xE8, 0x39, 0x9E, 0xFF, 0xFF, 0x84,
    0xC0, 0x74, 0x01, 0xCC, 0xB8, 0x06, 0x00, 0x00, 0x00, 0x48, 0x83, 0xC4,
    0x28, 0xC3, 0x8B, 0x01, 0x48, 0x83, 0xC4, 0x28, 0xC3,
});
constexpr auto GetOwnerEntry = std::to_array<std::uint8_t>({
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0xE8, 0x02, 0x66, 0xEA, 0xFF, 0x83, 0xF8,
    0x01, 0x0F, 0x85, 0x9E, 0x00, 0x00, 0x00, 0x48, 0x85, 0xDB, 0x74, 0x0D, 0x48, 0x8B, 0xCB, 0xE8,
});

static_assert(RelayBlob.size() == RelaySlot + 8, "Relay layout changed.");

using ConditionCheckFn = bool (*)(void* game, const std::uint8_t* tc, void* unit, std::uint8_t expansion) noexcept;
using LadderCheckFn    = bool (*)(void* game, std::uint32_t first, std::uint32_t last) noexcept;
using QuestCheckFn     = std::int32_t (*)(void* game, std::uint32_t quest, std::uint32_t state) noexcept;
using GetDataTablesFn  = std::uint8_t* (*)(std::uint8_t bank) noexcept;
using InterpretFn      = std::int32_t (*)(std::uint8_t bank, const std::uint8_t* code, std::int32_t length,
                                          const void* tokens, const void* functions, std::int32_t functionCount,
                                          void* context) noexcept;
using ReadUnitStatFn   = std::int32_t (*)(void* unit, std::uint32_t stat, std::uint32_t layer) noexcept;
using GetUnitTypeFn    = std::uint32_t (*)(const void* unit) noexcept;
using GetOwnerFn       = void* (*)(void* unit) noexcept;

// The context 0x3B5380 builds for a treasure-class formula.
struct alignas(16) ConditionContext {
    const void*  vtable;    // +00
    void*        slot[7];   // +08 .. +38
    std::int32_t level;     // +40
    std::uint8_t hasItem;   // +44
    std::uint8_t pad[3];
    void*        item;      // +48 (unused here)
    std::uint8_t spare[16];
};
static_assert(offsetof(ConditionContext, slot) == 0x08);
static_assert(offsetof(ConditionContext, level) == 0x40);
static_assert(offsetof(ConditionContext, hasItem) == 0x44);

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
const D2RL::PluginContext* Context{};
std::uintptr_t             Base{};

struct Settings {
    bool enabled{true};
    bool creditMinionOwner{true};
    bool diagnostics{false};
} Config;

bool Installed{false};
bool ConfigFileRead{false};
char InactiveReason[256]{};

ConditionCheckFn StockConditionCheck{};
LadderCheckFn    LadderCheck{};
QuestCheckFn     QuestCheck{};
GetDataTablesFn  GetDataTables{};
InterpretFn      Interpret{};
ReadUnitStatFn   ReadUnitStat{};
GetUnitTypeFn    GetUnitType{};
GetOwnerFn       GetOwner{};

std::uint8_t* RelayPage{};
bool          TopPatched{false};
bool          EntryPatched{false};
std::array<std::uint8_t, 5> TopWritten{};
std::array<std::uint8_t, 5> EntryWritten{};

std::atomic<std::uint64_t> ChecksWithPlayer{};
std::atomic<std::uint64_t> ChecksStock{};
std::atomic<std::uint32_t> DiagnosticLines{};

template <typename T>
auto At(std::uint64_t rva) noexcept -> T {
    return reinterpret_cast<T>(Base + static_cast<std::uintptr_t>(rva));
}

void Log(void (D2RL::PluginContext::*sink)(const char*) const noexcept, const char* format, va_list args) noexcept {
    char text[1024]{};
    std::vsnprintf(text, sizeof(text), format, args);
    (Context->*sink)(text);
}
void LogInfo(const char* format, ...) noexcept {
    va_list args; va_start(args, format); Log(&D2RL::PluginContext::LogInfo, format, args); va_end(args);
}
void LogWarn(const char* format, ...) noexcept {
    va_list args; va_start(args, format); Log(&D2RL::PluginContext::LogWarn, format, args); va_end(args);
}
void LogError(const char* format, ...) noexcept {
    va_list args; va_start(args, format); Log(&D2RL::PluginContext::LogError, format, args); va_end(args);
}
void SetInactive(const char* format, ...) noexcept {
    va_list args; va_start(args, format);
    std::vsnprintf(InactiveReason, sizeof(InactiveReason), format, args);
    va_end(args);
}

// ---------------------------------------------------------------------------
// The player whose stats ConditionCalc reads, or null for stock behaviour.
// ---------------------------------------------------------------------------
auto CreditedPlayer(void* killer, bool& viaOwner) noexcept -> void* {
    viaOwner = false;
    if (killer == nullptr) return nullptr;
    const std::uint32_t type = GetUnitType(killer);
    if (type == 0) return killer;
    if (type == 1 && Config.creditMinionOwner) {
        void* owner = GetOwner(killer);
        if (owner != nullptr && GetUnitType(owner) == 0) {
            viaOwner = true;
            return owner;
        }
    }
    return nullptr;
}

// 0x3B5380 with the player in the stat and skill slots.
auto EvaluateForPlayer(std::uint8_t bank, void* monster, void* player, std::uint32_t formula) noexcept -> std::int32_t {
    std::uint8_t* tables = GetDataTables(bank);
    if (tables == nullptr) return 0;
    const std::uint64_t size = *reinterpret_cast<const std::uint64_t*>(tables + ConditionPoolSize);
    if (formula >= size) return 0;
    const std::uint64_t length = size - formula;
    if (length > 0x7FFFFFFFULL) return 0;
    const std::uint8_t* code = *reinterpret_cast<const std::uint8_t* const*>(tables + ConditionPoolBase) + formula;

    ConditionContext context{};
    context.vtable  = At<const void*>(ContextVtableRva);
    context.slot[0] = monster;   // +08 rand
    context.slot[1] = player;    // +10 stat
    context.slot[2] = player;    // +18 skill, sklvl
    context.slot[3] = monster;   // +20 cond
    context.slot[4] = monster;   // +28 sksrc
    context.slot[5] = monster;   // +30 miss
    context.slot[6] = monster;   // +38 cond
    context.level   = monster != nullptr ? ReadUnitStat(monster, LevelStat, 0) : 0;
    context.hasItem = 0;
    return Interpret(bank, code, static_cast<std::int32_t>(length), At<const void*>(TokenCallbackRva),
                     At<const void*>(FunctionTableRva), 9, &context);
}

// Called by the relay in place of 0x444920, with the roll's killer added.
auto ConditionCheckWithKiller(void* game, const std::uint8_t* tc, void* monster, std::uint8_t expansion,
                                         void* killer) noexcept -> bool {
    bool viaOwner = false;
    void* player = CreditedPlayer(killer, viaOwner);
    if (player == nullptr || game == nullptr || tc == nullptr) {
        ChecksStock.fetch_add(1, std::memory_order_relaxed);
        return StockConditionCheck(game, tc, monster, expansion);
    }

    const std::uint8_t flags = tc[0x25];
    if ((flags & 1) != 0) {
        std::uint16_t first = 0, last = 0;
        std::memcpy(&first, tc + 4, 2);
        std::memcpy(&last, tc + 6, 2);
        if (!LadderCheck(game, first, last)) return false;
    }
    if ((flags & 2) != 0) {
        std::uint32_t quest = 0;
        std::memcpy(&quest, tc + (expansion != 0 ? 0x0C : 0x08), 4);
        if (quest <= 0x29 && QuestCheck(game, quest, 0) == 0 && QuestCheck(game, quest, 15) == 0
            && QuestCheck(game, quest, 13) == 0) {
            return false;
        }
    }
    if ((flags & 4) != 0) {
        std::uint32_t formula = 0;
        std::memcpy(&formula, tc + 0x20, 4);
        const std::uint8_t bank = *(static_cast<const std::uint8_t*>(game) + GameDataBankOffset);
        ChecksWithPlayer.fetch_add(1, std::memory_order_relaxed);
        const std::int32_t value = EvaluateForPlayer(bank, monster, player, formula);
        if (Config.diagnostics && DiagnosticLines.fetch_add(1, std::memory_order_relaxed) < 40) {
            LogInfo("ConditionCalcKiller: ConditionCalc read the %s, result %d (%s).",
                    viaOwner ? "owner of the minion that got the kill" : "player who got the kill", value,
                    value != 0 ? "treasure class allowed" : "treasure class skipped");
        }
        if (value == 0) return false;
    }
    return true;
}

// ===========================================================================
// Install
// ===========================================================================
auto CheckExact(std::uint64_t rva, const std::uint8_t* bytes, std::size_t size, const char* what) noexcept -> bool {
    if (Context->CheckExpectedBytes(rva, bytes, static_cast<std::uint32_t>(size))) return true;
    SetInactive("%s at RVA 0x%llX does not match D2RLoader 1.3.1", what, static_cast<unsigned long long>(rva));
    return false;
}
auto CheckCallable(std::uint64_t rva, const std::uint8_t* bytes, std::size_t size, const char* what) noexcept -> bool {
    if (Context->CheckExpectedBytes(rva, bytes, static_cast<std::uint32_t>(size))) return true;
    const D2RL::DiagnosticsService* diagnostics = nullptr;
    if (Context->QueryService(&diagnostics) == D2RL::ServiceQueryResult::Success
        && D2RL::HasDiagnosticsServiceField(diagnostics, D2RL::DiagnosticsServiceRequiredSize)
        && diagnostics->queryHookStatus != nullptr) {
        D2RL::Diagnostics::HookQuery query{
            .structSize   = D2RL::Diagnostics::HookQuerySize,
            .flags        = 0,
            .rva          = rva,
            .expected     = bytes,
            .expectedSize = static_cast<std::uint32_t>(size),
            .reserved     = 0,
        };
        D2RL::Diagnostics::HookStatus status{.structSize = D2RL::Diagnostics::HookStatusSize};
        if (diagnostics->queryHookStatus(Context, &query, &status) == D2RL::Diagnostics::Result::Success
            && status.state == D2RL::Diagnostics::ModificationState::Tracked
            && status.kind == D2RL::Diagnostics::ModificationKind::InlineHook) {
            LogInfo("ConditionCalcKiller: %s at RVA 0x%llX is hooked by another plugin; calling through it.", what,
                    static_cast<unsigned long long>(rva));
            return true;
        }
    }
    SetInactive("%s at RVA 0x%llX does not match D2RLoader 1.3.1", what, static_cast<unsigned long long>(rva));
    return false;
}

auto Verify() noexcept -> bool {
    return CheckExact(KillerHomeRva, KillerHome.data(), KillerHome.size(), "the drop roll's killer store")
        && CheckExact(TopSiteRva - TopSiteBefore.size(), TopSiteBefore.data(), TopSiteBefore.size(), "the treasure-class condition call")
        && CheckExact(TopSiteRva + 5, TopSiteAfter.data(), TopSiteAfter.size(), "the treasure-class condition call")
        && CheckExact(EntrySiteRva - EntrySiteBefore.size(), EntrySiteBefore.data(), EntrySiteBefore.size(), "the entry condition call")
        && CheckExact(EntrySiteRva + 5, EntrySiteAfter.data(), EntrySiteAfter.size(), "the entry condition call")
        && CheckExact(ConditionCheckRva, ConditionCheckBody.data(), ConditionCheckBody.size(), "the condition check")
        && CheckExact(ConditionEvalRva, ConditionEvalBody.data(), ConditionEvalBody.size(), "the ConditionCalc evaluator")
        && CheckCallable(LadderCheckRva, LadderCheckEntry.data(), LadderCheckEntry.size(), "the ladder check")
        && CheckCallable(QuestCheckRva, QuestCheckEntry.data(), QuestCheckEntry.size(), "the quest check")
        && CheckCallable(GetDataTablesRva, GetDataTablesEntry.data(), GetDataTablesEntry.size(), "GetDataTables")
        && CheckCallable(InterpretRva, InterpretEntry.data(), InterpretEntry.size(), "the formula interpreter")
        && CheckExact(TokenCallbackRva, TokenCallbackBody.data(), TokenCallbackBody.size(), "the formula token callback")
        && CheckExact(ReadUnitStatRva, ReadUnitStatThunk.data(), ReadUnitStatThunk.size(), "the stat read")
        && CheckCallable(GetUnitTypeRva, GetUnitTypeBody.data(), GetUnitTypeBody.size(), "UNITS_GetUnitType")
        && CheckCallable(GetOwnerRva, GetOwnerEntry.data(), GetOwnerEntry.size(), "the minion owner lookup");
}

auto CanReach(std::uintptr_t from, std::uintptr_t to) noexcept -> bool {
    const auto delta = static_cast<std::int64_t>(to) - static_cast<std::int64_t>(from);
    return delta >= INT32_MIN && delta <= INT32_MAX;
}
auto AllocateNear(std::uintptr_t hint, std::size_t size) noexcept -> std::uint8_t* {
    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    const auto step = static_cast<std::uintptr_t>(info.dwAllocationGranularity);
    if (step == 0) return nullptr;
    const std::uintptr_t aligned = hint & ~(step - 1);
    for (std::uintptr_t delta = step; delta < 0x7FF00000ULL; delta += step) {
        for (const std::uintptr_t candidate : {aligned + delta, aligned - delta}) {
            if (candidate < 0x10000 || !CanReach(hint, candidate) || !CanReach(hint, candidate + size)) continue;
            if (void* memory = VirtualAlloc(reinterpret_cast<void*>(candidate), size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)) {
                return static_cast<std::uint8_t*>(memory);
            }
        }
    }
    return nullptr;
}

auto CallBytes(std::uint64_t site, std::array<std::uint8_t, 5>& out) noexcept -> bool {
    const auto from = Base + site + 5;
    const auto to = reinterpret_cast<std::uintptr_t>(RelayPage);
    if (!CanReach(from, to)) return false;
    const auto rel = static_cast<std::int32_t>(static_cast<std::int64_t>(to) - static_cast<std::int64_t>(from));
    out[0] = 0xE8;
    std::memcpy(out.data() + 1, &rel, 4);
    return true;
}

void Restore() noexcept {
    if (TopPatched && Context->PatchBytes(TopSiteRva, TopWritten.data(), 5, TopSiteCall.data(), 5)) TopPatched = false;
    if (EntryPatched && Context->PatchBytes(EntrySiteRva, EntryWritten.data(), 5, EntrySiteCall.data(), 5)) EntryPatched = false;
}

auto Install() noexcept -> bool {
    if (!Verify()) return false;
    StockConditionCheck = At<ConditionCheckFn>(ConditionCheckRva);
    LadderCheck         = At<LadderCheckFn>(LadderCheckRva);
    QuestCheck          = At<QuestCheckFn>(QuestCheckRva);
    GetDataTables       = At<GetDataTablesFn>(GetDataTablesRva);
    Interpret           = At<InterpretFn>(InterpretRva);
    ReadUnitStat        = At<ReadUnitStatFn>(ReadUnitStatRva);
    GetUnitType         = At<GetUnitTypeFn>(GetUnitTypeRva);
    GetOwner            = At<GetOwnerFn>(GetOwnerRva);

    RelayPage = AllocateNear(Base + TopSiteRva, 0x1000);
    if (RelayPage == nullptr) {
        SetInactive("no memory page is free within reach of the drop roll");
        return false;
    }
    std::memcpy(RelayPage, RelayBlob.data(), RelayBlob.size());
    const auto target = reinterpret_cast<std::uintptr_t>(&ConditionCheckWithKiller);
    std::memcpy(RelayPage + RelaySlot, &target, 8);
    DWORD old = 0;
    if (!VirtualProtect(RelayPage, 0x1000, PAGE_EXECUTE_READ, &old)) {
        SetInactive("the relay page could not be made executable");
        return false;
    }
    FlushInstructionCache(GetCurrentProcess(), RelayPage, 0x1000);

    if (!CallBytes(TopSiteRva, TopWritten) || !CallBytes(EntrySiteRva, EntryWritten)) {
        SetInactive("the relay page is out of reach of the drop roll");
        return false;
    }
    TopPatched = Context->PatchBytes(TopSiteRva, TopSiteCall.data(), 5, TopWritten.data(), 5);
    EntryPatched = TopPatched && Context->PatchBytes(EntrySiteRva, EntrySiteCall.data(), 5, EntryWritten.data(), 5);
    if (!TopPatched || !EntryPatched) {
        Restore();
        SetInactive("the condition calls in the drop roll could not be patched");
        return false;
    }
    Installed = true;
    return true;
}

// ===========================================================================
// Config
// ===========================================================================
auto Trim(std::string_view text) noexcept -> std::string_view {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r')) text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) text.remove_suffix(1);
    return text;
}
auto ParseBool(std::string_view value, bool& out) noexcept -> bool {
    if (value == "true") { out = true; return true; }
    if (value == "false") { out = false; return true; }
    return false;
}
void ReadConfiguration() noexcept {
    std::string text = DefaultConfigToml;
    if (Context->EnsureConfig(DefaultConfigToml)) {
        std::string buffer(8192, '\0');
        std::uint32_t required = 0;
        bool ok = Context->ReadConfig(buffer.data(), static_cast<std::uint32_t>(buffer.size()), &required);
        if (!ok && required >= buffer.size() && required < (1U << 20)) {
            buffer.assign(static_cast<std::size_t>(required) + 1, '\0');
            ok = Context->ReadConfig(buffer.data(), static_cast<std::uint32_t>(buffer.size()), &required);
        }
        if (ok) {
            buffer.resize(std::strlen(buffer.c_str()));
            text = std::move(buffer);
            ConfigFileRead = true;
        }
    }
    std::size_t cursor = 0;
    while (cursor < text.size()) {
        const std::size_t newline = text.find('\n', cursor);
        const std::size_t end = newline == std::string::npos ? text.size() : newline;
        std::string_view line(text.data() + cursor, end - cursor);
        cursor = end + 1;
        if (const std::size_t hash = line.find('#'); hash != std::string_view::npos) line = line.substr(0, hash);
        line = Trim(line);
        if (line.empty() || line.front() == '[') continue;
        const std::size_t equals = line.find('=');
        if (equals == std::string_view::npos) continue;
        const std::string_view key = Trim(line.substr(0, equals));
        const std::string_view value = Trim(line.substr(equals + 1));
        bool parsed = true;
        if (key == "enabled") parsed = ParseBool(value, Config.enabled);
        else if (key == "credit_minion_owner") parsed = ParseBool(value, Config.creditMinionOwner);
        else if (key == "diagnostics") parsed = ParseBool(value, Config.diagnostics);
        else parsed = false;
        if (!parsed) LogWarn("ConditionCalcKiller: config line ignored: %.*s", static_cast<int>(line.size()), line.data());
    }
}

// ===========================================================================
// Console
// ===========================================================================
auto StatusCommand(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command, void*) noexcept -> D2RL::ConsoleCommandResult {
    char text[768]{};
    if (!Config.enabled) {
        std::snprintf(text, sizeof(text), "ConditionCalc Killer %s: off in the config file.", PluginVersion);
    } else if (!Installed) {
        std::snprintf(text, sizeof(text), "ConditionCalc Killer %s: NOT ACTIVE, %s. Nothing was changed.", PluginVersion,
                      InactiveReason[0] != '\0' ? InactiveReason : "install failed");
    } else {
        std::snprintf(text, sizeof(text),
                      "ConditionCalc Killer %s: active. Treasure-class checks with a player read: %llu, stock (no player): %llu. "
                      "Minion kills credit the owner: %s. Config %s.",
                      PluginVersion, static_cast<unsigned long long>(ChecksWithPlayer.load(std::memory_order_relaxed)),
                      static_cast<unsigned long long>(ChecksStock.load(std::memory_order_relaxed)),
                      Config.creditMinionOwner ? "yes" : "no", ConfigFileRead ? "loaded" : "NOT READ (defaults)");
    }
    command->plugin->WriteConsoleMessage(text, D2RL::ConsoleMessageKind::Output);
    return D2RL::ConsoleCommandResult::Handled;
}

constexpr D2RL::PluginInfo Info{
    .infoSize    = D2RL::PluginInfoSize,
    .abiVersion  = D2RL_PLUGIN_ABI_VERSION,
    .id          = "celestialrayone.conditioncalc-killer",
    .name        = "ConditionCalc Killer",
    .version     = PluginVersion,
    .author      = "CelestialRayOne",
    .description = "treasureclassex.txt ConditionCalc reads the player who got the kill.",
    .flags       = D2RL::PluginFlags::Server | D2RL::PluginFlags::NativeHooks,
};
static_assert(D2RL::HasFlag(Info.flags, D2RL::PluginFlags::NativeHooks), "Code patches require the NativeHooks flag.");

}  // namespace
}  // namespace CelestialRayOne::ConditionCalcKiller

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* {
    return &CelestialRayOne::ConditionCalcKiller::Info;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
    using namespace CelestialRayOne::ConditionCalcKiller;
    if (!D2RL::HasContext(context)) return false;
    Context = context;
    Base = context->exeBase;
    if (!Context->RegisterConsoleCommand("conditioncalc", &StatusCommand,
                                         "ConditionCalc Killer status and how often a player was read.")) {
        Context->LogWarn("ConditionCalcKiller: the console command was refused.");
    }
    ReadConfiguration();
    if (!Config.enabled) {
        Context->LogInfo("ConditionCalcKiller: off in the config file; nothing was changed.");
        return true;
    }
    if (!Install()) {
        LogError("ConditionCalcKiller: NOT ACTIVE, %s. Nothing was changed.", InactiveReason[0] != '\0' ? InactiveReason : "install failed");
        return true;
    }
    LogInfo("ConditionCalcKiller %s: active (both treasure-class condition checks in the drop roll).", PluginVersion);
    return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
    using namespace CelestialRayOne::ConditionCalcKiller;
    if (Context == nullptr) return;
    Restore();
}
