// Misc Extra Stats  (celestialrayone.misc-extra-stats)
//
// Adds stat4/calc4, stat5/calc5 and stat6/calc6 to misc.txt, next to the stock
// stat1..3 / calc1..3, so a usable item's pSpell can apply up to six stats.
// Nothing in the save format changes.
//
// Target: D2RLoader 1.3.1 running D2R 3.3. Every RVA and byte below was read
// from the dumped D2RLoader.exe image (base 0x140000000) and decoded before
// being relied on. On any build whose bytes differ, the plugin installs
// nothing, logs why, and stays loaded so its console command can explain.
//
// ---------------------------------------------------------------------------
// 1. Why a plugin
// ---------------------------------------------------------------------------
//   The item record (448 bytes, shared by weapons, armor and misc) is fully
//   packed: stat1..3 are words at +0x9E/+0xA0/+0xA2, calc1..3 dwords at
//   +0xA4/+0xA8/+0xAC, len at +0xB0. The item loader 0x315FD0 hands a fixed
//   column list to the txt parser, which skips any other column, and D2RLoader
//   has no way to add columns to a stock table. The pSpell handlers hardcode a
//   loop of three.
//
// ---------------------------------------------------------------------------
// 2. The loops that read the pairs (all verified byte for byte)
// ---------------------------------------------------------------------------
//   pSpell 3  0x57ECB0  index rdi, record rbp, record slot [rsp+68h]
//   pSpell 4  0x57F280  index rax ([rsp+60h]), record rdi, slot [rsp+58h]
//   pSpell 5  0x57F790  index r12, record rsi, slot [rsp+50h]
//   pSpell 9  0x57FD80  rbx walks the stat words, r12 = end, r13 = record,
//                       calc index = (r15 + rbx) >> 1 with r15 = -0x9E - r13
//   can-use   0x580A80  rbx walks the stat words, rsi = current entry,
//                       rdi = record (shared pre-use check of pSpell 3/4/5)
//   pSpell 6 (0x5807B0) runs pSpell 9's code. pSpell 8, 10, 11, 12 and 15 do
//   not loop over the pairs. Inside each loop the record is read only for the
//   stat word [rec + i*2 + 0x9E] and the calc [rec + i*4 + 0xA4].
//
// ---------------------------------------------------------------------------
// 3. How six slots work
// ---------------------------------------------------------------------------
//   Each row with extra stats gets a 24-byte extension block E:
//     E+0, E+2, E+4    stat4..6 (word, -1 = empty)
//     E+12, E+16, E+20 calc4..6 (dword formula id, -1 = empty)
//   and a virtual record base B = E - 164. For loop index i = 3..5,
//   B + i*2 + 0x9E = E + (i-3)*2 and B + i*4 + 0xA4 = E + 12 + (i-3)*4, so the
//   game's own loop body reads stat4..6 / calc4..6 at index 3..5.
//
//   Each loop's advance-and-test is replaced by a jmp to a relay on a page
//   this plugin allocates within rel32 reach. The relay replays the stolen
//   instructions; when the stock loop would end at 3 it asks LookupVirtualBase
//   for the row, swaps the record register and its stack slot to B and keeps
//   counting 3, 4, 5. The index is never reset, so code the game runs only on
//   the first slot (the timed-state packet in pSpell 3/4) still runs once.
//   The walks (pSpell 9, can-use) are re-seeded onto E the same way.
//
//   The relay was assembled with keystone, decoded back with capstone, and run
//   under unicorn against a callee that clobbers every volatile register and
//   xmm0..5: every path, register, stack slot and the 16-byte call alignment
//   were checked.
//
// ---------------------------------------------------------------------------
// 4. calc4..6
// ---------------------------------------------------------------------------
//   Compiled with the game's own compiler, FORMULA_Compile 0xA24290, with the
//   exact resolvers the item compiler 0x3B47C0 passes (0x3B58A0, 0x3B57A0,
//   0x3B54E0), into buffers this plugin owns: the game's formula pool is never
//   touched. The record carries id 0x7E000000 | n. An inline hook on the item
//   formula evaluator 0x3B4880 runs ids with that tag through the game's
//   interpreter 0xA234B0 with exactly the context 0x3B4880 builds:
//     +00 vtable 0x1D0AEC8, +08..+38 the unit (item at +20), +40 dword 0,
//     +44 byte 1, +48 the item; token callback 0x79100, functions 0x1D09C50,
//     count 9.
//   Every other id goes to the stock evaluator untouched.
//
// ---------------------------------------------------------------------------
// 5. Reading the columns
// ---------------------------------------------------------------------------
//   On D2RLoader's tables-loaded event (game thread) the plugin reads the same
//   misc.txt the game loaded, from the mod folder: data\global\excel\ for the
//   RotW bank, data\global\excel\base\ for Classic and LoD. The bank of each
//   loaded Items table is found by matching the data-table service's Items
//   rows pointer. Rows are matched by item code against the game's compiled
//   records (code dword at +0x80), stat names are resolved through the game's
//   own itemstatcost registry (data tables +0x1270, lookup 0xA121F0).

#include <D2RLPlugin/api.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace CelestialRayOne::MiscExtraStats {
namespace {

constexpr char PluginVersion[] = "1.0.0";

constexpr const char DefaultConfigToml[] = R"TOML(# celestialrayone.misc-extra-stats
#
# Misc Extra Stats
#   Adds three more stat/formula pairs to misc.txt: stat4/calc4, stat5/calc5
#   and stat6/calc6, next to the stock stat1..3 / calc1..3. A usable item can
#   then apply up to six stats when it is used.
#
# The columns
#   Add the six columns anywhere in misc.txt with exactly these names:
#     stat4  calc4  stat5  calc5  stat6  calc6
#   Fill them exactly like stat1..3 / calc1..3:
#   - statN is an itemstatcost.txt Stat name.
#   - calcN is a formula. In it, stat('x'.accr) reads the player using the
#     item. These formulas have no "lvl" token; for the player's level use
#     stat('level'.accr).
#   The same rules as the stock pairs apply: timed state and len, the
#   itemstatcost maxstat clamp, and so on.
#
# Which pSpell values use them
#   The six pairs work wherever the game uses stat1..3:
#     pSpell 3, 4, 5   the list stops at the first empty stat column, exactly
#                      like stock, so fill stat1, stat2, stat3, stat4 ... in
#                      order. An empty stat3 means stat4..6 are never read.
#     pSpell 6, 9      empty stat columns are skipped.
#   The pre-use check of pSpell 3, 4 and 5 (the item can be used when at least
#   one pair gives a value) also looks at the new pairs.
#
# Where they are read from
#   The misc.txt in the mod folder that the game itself loads:
#   data\global\excel\misc.txt for RotW games and
#   data\global\excel\base\misc.txt for Classic and LoD games. Mistakes (an
#   unknown item code or stat name, a formula that does not compile) are
#   written to this plugin's log and that one value is left empty.
#
# Changes to misc.txt or this file are read at the next game start.
# Built for D2RLoader 1.3.1 (D2R 3.3). On any other build the byte checks fail
# and the plugin loads without changing the game; the console command
# "miscextrastats" says why.

# Master switch. false loads the plugin without changing the game.
enabled = true

# true writes every item that got extra stats to the plugin log when the tables
# load, and the first evaluations of the new formulas. Keep false for play.
diagnostics = false
)TOML";

// ---------------------------------------------------------------------------
// Native addresses and layout (D2RLoader 1.3.1 image, D2R 3.3)
// ---------------------------------------------------------------------------
constexpr std::uint64_t GetDataTablesRva    = 0x300A90;
constexpr std::uint64_t ItemEvalRva         = 0x3B4880;
constexpr std::uint64_t CompileRva          = 0xA24290;
constexpr std::uint64_t InterpretRva        = 0xA234B0;
constexpr std::uint64_t RegistryLookupRva   = 0xA121F0;
constexpr std::uint64_t TokenCallbackRva    = 0x079100;
constexpr std::uint64_t FunctionTableRva    = 0x1D09C50;
constexpr std::uint64_t ContextVtableRva    = 0x1D0AEC8;
constexpr std::uint64_t FnNameResolverRva   = 0x3B58A0;
constexpr std::uint64_t ArgCountCheckRva    = 0x3B57A0;
constexpr std::uint64_t ArgResolverRva      = 0x3B54E0;
constexpr std::uint64_t CompileCallSetupRva = 0x3B4807;
constexpr std::uint64_t ItemEvalBodyRva     = 0x3B489B;

constexpr std::size_t ItemsRowsOffset     = 0x15A0;  // data tables: Items rows (weapons, armor, misc)
constexpr std::size_t ItemsCountOffset    = 0x15A8;  // qword row count
constexpr std::size_t StatRegistryOffset  = 0x1270;  // itemstatcost name registry
constexpr std::size_t ItemRecordSize      = 448;
constexpr std::size_t ItemCodeOffset      = 0x80;
constexpr std::size_t VirtualBaseDistance = 164;     // B = E - 164

constexpr std::uint32_t FormulaTag     = 0x7E000000U;
constexpr std::uint32_t FormulaTagMask = 0xFF000000U;
constexpr std::uint32_t NoFormula      = 0xFFFFFFFFU;
constexpr std::int32_t  FormulaBufferBytes = 1024;    // what 0x3B47C0 gives the compiler
constexpr int ExtraSlots = 3;

struct LoopSite {
    std::uint64_t rva;
    std::uint64_t head;
    std::uint64_t done;
    std::size_t   relayEntry;
    std::size_t   headSlot;
    std::size_t   doneSlot;
    const char*   name;
};

// relay layout (fixed by the build): entries and the 8-byte pointer slots
constexpr std::size_t RelaySlotLookup = 0x208;
constexpr LoopSite Sites[] = {
    {0x57F1CF, 0x57EDF0, 0x57F1F4, 0x074, 0x210, 0x218, "pSpell 3"},
    {0x57F6E9, 0x57F3C0, 0x57F714, 0x0B1, 0x220, 0x228, "pSpell 4"},
    {0x57FA35, 0x57F8D0, 0x57FA42, 0x0F3, 0x230, 0x238, "pSpell 5"},
    {0x58004E, 0x57FF80, 0x58005B, 0x133, 0x240, 0x248, "pSpell 9"},
    {0x580BD5, 0x580B90, 0x580BE5, 0x181, 0x250, 0x258, "can-use check"},
};

constexpr auto RelayBlob = std::to_array<std::uint8_t>({
    0x53, 0x52, 0x41, 0x50, 0x41, 0x51, 0x41, 0x52, 0x41, 0x53, 0x48, 0x89,
    0xE3, 0x48, 0x83, 0xE4, 0xF0, 0x48, 0x81, 0xEC, 0x80, 0x00, 0x00, 0x00,
    0xF3, 0x0F, 0x7F, 0x44, 0x24, 0x20, 0xF3, 0x0F, 0x7F, 0x4C, 0x24, 0x30,
    0xF3, 0x0F, 0x7F, 0x54, 0x24, 0x40, 0xF3, 0x0F, 0x7F, 0x5C, 0x24, 0x50,
    0xF3, 0x0F, 0x7F, 0x64, 0x24, 0x60, 0xF3, 0x0F, 0x7F, 0x6C, 0x24, 0x70,
    0xFF, 0x15, 0xC6, 0x01, 0x00, 0x00, 0xF3, 0x0F, 0x6F, 0x44, 0x24, 0x20,
    0xF3, 0x0F, 0x6F, 0x4C, 0x24, 0x30, 0xF3, 0x0F, 0x6F, 0x54, 0x24, 0x40,
    0xF3, 0x0F, 0x6F, 0x5C, 0x24, 0x50, 0xF3, 0x0F, 0x6F, 0x64, 0x24, 0x60,
    0xF3, 0x0F, 0x6F, 0x6C, 0x24, 0x70, 0x48, 0x89, 0xDC, 0x41, 0x5B, 0x41,
    0x5A, 0x41, 0x59, 0x41, 0x58, 0x5A, 0x5B, 0xC3, 0x48, 0x83, 0xFF, 0x03,
    0x0F, 0x8C, 0x4C, 0x01, 0x00, 0x00, 0x0F, 0x85, 0x1E, 0x00, 0x00, 0x00,
    0x48, 0x89, 0xE9, 0xE8, 0x74, 0xFF, 0xFF, 0xFF, 0x48, 0x85, 0xC0, 0x0F,
    0x84, 0x3B, 0x01, 0x00, 0x00, 0x48, 0x89, 0xC5, 0x48, 0x89, 0x44, 0x24,
    0x68, 0xE9, 0x28, 0x01, 0x00, 0x00, 0x48, 0x83, 0xFF, 0x06, 0x0F, 0x8C,
    0x1E, 0x01, 0x00, 0x00, 0xE9, 0x1F, 0x01, 0x00, 0x00, 0x48, 0x83, 0xF8,
    0x03, 0x0F, 0x8C, 0x1B, 0x01, 0x00, 0x00, 0x0F, 0x85, 0x23, 0x00, 0x00,
    0x00, 0x48, 0x89, 0xF9, 0xE8, 0x37, 0xFF, 0xFF, 0xFF, 0x48, 0x85, 0xC0,
    0x0F, 0x84, 0x0A, 0x01, 0x00, 0x00, 0x48, 0x89, 0xC7, 0x48, 0x89, 0x44,
    0x24, 0x58, 0x48, 0x8B, 0x44, 0x24, 0x60, 0xE9, 0xF2, 0x00, 0x00, 0x00,
    0x48, 0x83, 0xF8, 0x06, 0x0F, 0x8C, 0xE8, 0x00, 0x00, 0x00, 0xE9, 0xE9,
    0x00, 0x00, 0x00, 0x49, 0xFF, 0xC4, 0x49, 0x83, 0xFC, 0x03, 0x0F, 0x8C,
    0xE2, 0x00, 0x00, 0x00, 0x0F, 0x85, 0x1E, 0x00, 0x00, 0x00, 0x48, 0x89,
    0xF1, 0xE8, 0xF2, 0xFE, 0xFF, 0xFF, 0x48, 0x85, 0xC0, 0x0F, 0x84, 0xD1,
    0x00, 0x00, 0x00, 0x48, 0x89, 0xC6, 0x48, 0x89, 0x44, 0x24, 0x50, 0xE9,
    0xBE, 0x00, 0x00, 0x00, 0x49, 0x83, 0xFC, 0x06, 0x0F, 0x8C, 0xB4, 0x00,
    0x00, 0x00, 0xE9, 0xB5, 0x00, 0x00, 0x00, 0x48, 0x83, 0xC3, 0x02, 0x4C,
    0x39, 0xE3, 0x0F, 0x85, 0xAE, 0x00, 0x00, 0x00, 0x49, 0x8D, 0x85, 0xA4,
    0x00, 0x00, 0x00, 0x49, 0x39, 0xC4, 0x0F, 0x85, 0xA4, 0x00, 0x00, 0x00,
    0x4C, 0x89, 0xE9, 0xE8, 0xA8, 0xFE, 0xFF, 0xFF, 0x48, 0x85, 0xC0, 0x0F,
    0x84, 0x93, 0x00, 0x00, 0x00, 0x49, 0x89, 0xC5, 0x48, 0x8D, 0x98, 0xA4,
    0x00, 0x00, 0x00, 0x4C, 0x8D, 0xA0, 0xAA, 0x00, 0x00, 0x00, 0x49, 0xC7,
    0xC7, 0x62, 0xFF, 0xFF, 0xFF, 0x49, 0x29, 0xC7, 0xE9, 0x6D, 0x00, 0x00,
    0x00, 0x48, 0x83, 0xC3, 0x02, 0x48, 0x8D, 0x87, 0xA2, 0x00, 0x00, 0x00,
    0x48, 0x39, 0xC6, 0x0F, 0x84, 0x15, 0x00, 0x00, 0x00, 0x48, 0x8D, 0x87,
    0xA8, 0x00, 0x00, 0x00, 0x48, 0x39, 0xC6, 0x0F, 0x84, 0x5B, 0x00, 0x00,
    0x00, 0xE9, 0x50, 0x00, 0x00, 0x00, 0x48, 0x89, 0xF9, 0xE8, 0x4E, 0xFE,
    0xFF, 0xFF, 0x48, 0x85, 0xC0, 0x0F, 0x84, 0x45, 0x00, 0x00, 0x00, 0x48,
    0x89, 0xC7, 0x48, 0x8D, 0x98, 0xA4, 0x00, 0x00, 0x00, 0xE9, 0x30, 0x00,
    0x00, 0x00, 0xFF, 0x25, 0x40, 0x00, 0x00, 0x00, 0xFF, 0x25, 0x42, 0x00,
    0x00, 0x00, 0xFF, 0x25, 0x44, 0x00, 0x00, 0x00, 0xFF, 0x25, 0x46, 0x00,
    0x00, 0x00, 0xFF, 0x25, 0x48, 0x00, 0x00, 0x00, 0xFF, 0x25, 0x4A, 0x00,
    0x00, 0x00, 0xFF, 0x25, 0x4C, 0x00, 0x00, 0x00, 0xFF, 0x25, 0x4E, 0x00,
    0x00, 0x00, 0xFF, 0x25, 0x50, 0x00, 0x00, 0x00, 0xFF, 0x25, 0x52, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
});

constexpr auto PSpell3Head = std::to_array<std::uint8_t>({
    0x0F, 0xBF, 0xAC, 0x7D, 0x9E, 0x00, 0x00, 0x00, 0x85, 0xED, 0x0F, 0x88,
    0xDB, 0x03, 0x00, 0x00, 0x41, 0x0F, 0xB6, 0x8F, 0x06, 0x01, 0x00, 0x00,
    0xE8, 0x83, 0x1C, 0xD8, 0xFF, 0x48, 0x8B, 0x98, 0x60, 0x12, 0x00, 0x00,
    0x48, 0x63, 0xC3, 0x48, 0x3B, 0xC3, 0x75, 0x04, 0x85, 0xDB, 0x79, 0x14,
    0x48, 0x8D, 0x4C, 0x24, 0x40, 0xC6, 0x44, 0x24, 0x40, 0x00, 0xE8, 0x31,
    0x7B, 0xB0, 0xFF, 0x84, 0xC0, 0x74, 0x01, 0xCC, 0x3B, 0xEB, 0x0F, 0x8D,
    0x9F, 0x03, 0x00, 0x00, 0x41, 0x0F, 0xB6, 0x8F, 0x06, 0x01, 0x00, 0x00,
    0x8B, 0xD5, 0xE8, 0xE5, 0xA8, 0xD5, 0xFF, 0x4C, 0x8B, 0xE8, 0x48, 0x85,
    0xC0, 0x0F, 0x84, 0x5E, 0x03, 0x00, 0x00, 0x48, 0x8B, 0x44, 0x24, 0x68,
    0x48, 0x8B, 0xD6, 0x4C, 0x8B, 0x84, 0x24, 0xD0, 0x00, 0x00, 0x00, 0x41,
    0x0F, 0xB6, 0x8F, 0x06, 0x01, 0x00, 0x00, 0x44, 0x8B, 0x8C, 0xB8, 0xA4,
    0x00, 0x00, 0x00, 0xE8,
});

constexpr auto PSpell3Tail = std::to_array<std::uint8_t>({
    0x48, 0x8B, 0x7C, 0x24, 0x48, 0x44, 0x8B, 0x6C, 0x24, 0x5C, 0x48, 0xFF,
    0xC7, 0x48, 0x8B, 0x6C, 0x24, 0x68, 0x41, 0xFF, 0xC5, 0x44, 0x89, 0x6C,
    0x24, 0x5C, 0x48, 0x89, 0x7C, 0x24, 0x48,
});

constexpr auto PSpell4Head = std::to_array<std::uint8_t>({
    0x0F, 0xBF, 0xAC, 0x47, 0x9E, 0x00, 0x00, 0x00, 0x85, 0xED, 0x0F, 0x88,
    0x25, 0x03, 0x00, 0x00, 0x41, 0x0F, 0xB6, 0x8F, 0x06, 0x01, 0x00, 0x00,
    0xE8, 0xB3, 0x16, 0xD8, 0xFF, 0x48, 0x8B, 0x98, 0x60, 0x12, 0x00, 0x00,
    0x48, 0x63, 0xC3, 0x48, 0x3B, 0xC3, 0x75, 0x04, 0x85, 0xDB, 0x79, 0x1A,
    0x48, 0x8D, 0x8C, 0x24, 0xB0, 0x00, 0x00, 0x00, 0xC6, 0x84, 0x24, 0xB0,
    0x00, 0x00, 0x00, 0x00, 0xE8, 0x5B, 0x75, 0xB0, 0xFF, 0x84, 0xC0, 0x74,
    0x01, 0xCC, 0x3B, 0xEB, 0x0F, 0x8D, 0xE3, 0x02, 0x00, 0x00, 0x41, 0x0F,
    0xB6, 0x8F, 0x06, 0x01, 0x00, 0x00, 0x8B, 0xD5, 0xE8, 0x0F, 0xA3, 0xD5,
    0xFF, 0x4C, 0x8B, 0xE8, 0x48, 0x85, 0xC0, 0x0F, 0x84, 0xA2, 0x02, 0x00,
    0x00, 0x48, 0x8B, 0x44, 0x24, 0x60, 0x48, 0x8B, 0xD6, 0x4C, 0x8B, 0x84,
    0x24, 0xC0, 0x00, 0x00, 0x00, 0x41, 0x0F, 0xB6, 0x8F, 0x06, 0x01, 0x00,
    0x00, 0x44, 0x8B, 0x8C, 0x87, 0xA4, 0x00, 0x00, 0x00, 0xE8, 0x2E, 0x54,
});

constexpr auto PSpell4Tail = std::to_array<std::uint8_t>({
    0x48, 0x8B, 0x7C, 0x24, 0x58, 0x44, 0x8B, 0x6C, 0x24, 0x48, 0x48, 0x8B,
    0x44, 0x24, 0x60, 0x41, 0xFF, 0xC5, 0x48, 0xFF, 0xC0, 0x44, 0x89, 0x6C,
    0x24, 0x48, 0x48, 0x89, 0x44, 0x24, 0x60,
});

constexpr auto PSpell5Head = std::to_array<std::uint8_t>({
    0x46, 0x0F, 0xBF, 0xBC, 0x66, 0x9E, 0x00, 0x00, 0x00, 0x45, 0x85, 0xFF,
    0x0F, 0x88, 0x6B, 0x04,
});

constexpr auto PSpell5Tail = std::to_array<std::uint8_t>({
    0xB7, 0x46, 0x2E, 0x3B, 0xC3, 0x7D, 0x51, 0x48, 0x8B, 0x44, 0x24, 0x50,
    0x48, 0x8B, 0xD5, 0x4C, 0x8B, 0x84, 0x24, 0xB0, 0x00, 0x00, 0x00, 0x41,
    0x0F, 0xB6, 0x8E, 0x06, 0x01, 0x00, 0x00, 0x46, 0x8B, 0x8C, 0xA0, 0xA4,
    0x00, 0x00, 0x00, 0xE8, 0x7C, 0x4E, 0xE3, 0xFF, 0x48, 0x63, 0xF8, 0x85,
    0xC0, 0x0F, 0x8F, 0x8F, 0x00, 0x00, 0x00, 0x48, 0x8D, 0x8C, 0x24, 0xA0,
    0x00, 0x00, 0x00, 0xC6, 0x84, 0x24, 0xA0, 0x00, 0x00, 0x00, 0x00, 0xE8,
    0xCC, 0x1B, 0x00, 0x00, 0x84, 0xC0, 0x74, 0x01, 0xCC, 0x8B, 0xBC, 0x24,
    0xB8, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x74, 0x24, 0x50,
});

constexpr auto PSpell9Body = std::to_array<std::uint8_t>({
    0x49, 0x8D, 0x9D, 0x9E, 0x00, 0x00, 0x00, 0x4D, 0x8D, 0xA5, 0xA4, 0x00,
    0x00, 0x00, 0x49, 0x3B, 0xDC, 0x0F, 0x84, 0xE6, 0x00, 0x00, 0x00, 0x49,
    0xC7, 0xC7, 0x62, 0xFF, 0xFF, 0xFF, 0x4D, 0x2B, 0xFD, 0x90, 0x0F, 0xBF,
    0x03, 0x66, 0x85, 0xC0, 0x0F, 0x88, 0xC2, 0x00, 0x00, 0x00, 0x0F, 0xB6,
    0x8D, 0x06, 0x01, 0x00, 0x00, 0x8B, 0xD0, 0xE8, 0x96, 0x97, 0xD5, 0xFF,
    0x48, 0x85, 0xC0, 0x75, 0x22, 0x48, 0x8D, 0x8C, 0x24, 0x80, 0x00, 0x00,
    0x00, 0x88, 0x84, 0x24, 0x80, 0x00, 0x00, 0x00, 0xE8, 0x9D, 0x11, 0x00,
    0x00, 0x84, 0xC0, 0x0F, 0x84, 0x93, 0x00, 0x00, 0x00, 0xCC, 0xE9, 0x8D,
    0x00, 0x00, 0x00, 0x4C, 0x8B, 0x84, 0x24, 0x90, 0x00, 0x00, 0x00, 0x49,
    0x8D, 0x04, 0x1F, 0x0F, 0xB6, 0x8D, 0x06, 0x01, 0x00, 0x00, 0x48, 0x8B,
    0xD6, 0x48, 0xD1, 0xF8, 0x45, 0x8B, 0x8C, 0x85, 0xA4, 0x00, 0x00, 0x00,
    0xE8, 0x99, 0x48, 0xE3, 0xFF, 0x8B, 0xF8, 0x85, 0xC0, 0x74, 0x61, 0x44,
    0x0F, 0xBF, 0x03, 0x33, 0xC0, 0x0F, 0xB6, 0x8D, 0x06, 0x01, 0x00, 0x00,
    0x44, 0x8B, 0xCF, 0x49, 0x8B, 0xD6, 0x90, 0x89, 0x44, 0x24, 0x20, 0xE8,
    0xF6, 0x7B, 0xD7, 0xFF, 0x66, 0x83, 0x3B, 0x1C, 0x75, 0x3E, 0x85, 0xFF,
    0x7E, 0x3A, 0x45, 0x33, 0xC0, 0x48, 0x8B, 0xCE, 0x41, 0x8D, 0x50, 0x0B,
    0xE8, 0xFD, 0x4F, 0xD7, 0xFF, 0x45, 0x33, 0xC0, 0x48, 0x8B, 0xCE, 0x8B,
    0xF8, 0x41, 0x8D, 0x50, 0x0A, 0xE8, 0xEC, 0x4F, 0xD7, 0xFF, 0x3B, 0xC7,
    0x7D, 0x16, 0x45, 0x33, 0xC9, 0x44, 0x8D, 0x87, 0x00, 0x01, 0x00, 0x00,
    0x48, 0x8B, 0xCE, 0x41, 0x8D, 0x51, 0x0A, 0xE8, 0xC2, 0x7C, 0xD7, 0xFF,
});

constexpr auto CanUseBody = std::to_array<std::uint8_t>({
    0x48, 0x8D, 0x47, 0x06, 0x48, 0x8D, 0x9F, 0x9E, 0x00, 0x00, 0x00, 0x48,
    0x3B, 0xF8, 0x74, 0x57, 0x66, 0x90, 0x0F, 0xBF, 0x13, 0x48, 0x8B, 0xF3,
    0x0F, 0xB6, 0x8D, 0x06, 0x01, 0x00, 0x00, 0xE8, 0x8E, 0x8B, 0xD5, 0xFF,
    0x48, 0x8B, 0xCB, 0x48, 0x2B, 0xCF, 0x48, 0x81, 0xE9, 0x9E, 0x00, 0x00,
    0x00, 0x48, 0xD1, 0xF9, 0x48, 0x85, 0xC0, 0x74, 0x1E, 0x44, 0x8B, 0x8C,
    0x8F, 0xA4, 0x00, 0x00, 0x00, 0x4D, 0x8B, 0xC7, 0x0F, 0xB6, 0x8D, 0x06,
    0x01, 0x00, 0x00, 0x49, 0x8B, 0xD4, 0xE8, 0xAF, 0x3C, 0xE3, 0xFF, 0x85,
    0xC0, 0x75, 0x29,
});

constexpr auto PSpell3Window = std::to_array<std::uint8_t>({
    0x48, 0x83, 0xFF, 0x03, 0x0F, 0x8C, 0x17, 0xFC, 0xFF, 0xFF, 0xEB, 0x19,
});

constexpr auto PSpell4Window = std::to_array<std::uint8_t>({
    0x48, 0x83, 0xF8, 0x03, 0x0F, 0x8C, 0xCD, 0xFC, 0xFF, 0xFF, 0xEB, 0x1F,
});

constexpr auto PSpell5Window = std::to_array<std::uint8_t>({
    0x49, 0xFF, 0xC4, 0x49, 0x83, 0xFC, 0x03, 0x0F, 0x8C, 0x8E, 0xFE, 0xFF,
    0xFF,
});

constexpr auto PSpell9Window = std::to_array<std::uint8_t>({
    0x48, 0x83, 0xC3, 0x02, 0x49, 0x3B, 0xDC, 0x0F, 0x85, 0x25, 0xFF, 0xFF,
    0xFF,
});

constexpr auto CanUseWindow = std::to_array<std::uint8_t>({
    0x48, 0x83, 0xC3, 0x02, 0x48, 0x8D, 0x87, 0xA2, 0x00, 0x00, 0x00, 0x48,
    0x3B, 0xF0, 0x75, 0xAB,
});

constexpr auto ItemEvalPrologue = std::to_array<std::uint8_t>({
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89,
    0x74, 0x24, 0x18, 0x57, 0x41, 0x56, 0x41, 0x57, 0x48, 0x81, 0xEC, 0xB0,
    0x00, 0x00, 0x00,
});

constexpr auto ItemEvalBody = std::to_array<std::uint8_t>({
    0x41, 0x8B, 0xD9, 0x4D, 0x8B, 0xF0, 0x48, 0x8B, 0xFA, 0x44, 0x0F, 0xB6,
    0xF9, 0xE8, 0xE3, 0xC1, 0xF4, 0xFF, 0x48, 0x8B, 0xE8, 0x8B, 0xF3, 0x48,
    0x3B, 0x98, 0x30, 0x02, 0x00, 0x00, 0x72, 0x07, 0x33, 0xC0, 0xE9, 0xEF,
    0x00, 0x00, 0x00, 0x48, 0x8D, 0x05, 0xFF, 0x65, 0x95, 0x01, 0x4C, 0x89,
    0xB4, 0x24, 0xA8, 0x00, 0x00, 0x00, 0x48, 0x89, 0x44, 0x24, 0x60, 0xC6,
    0x84, 0x24, 0xA4, 0x00, 0x00, 0x00, 0x01, 0x48, 0x89, 0x7C, 0x24, 0x68,
    0x48, 0x89, 0x7C, 0x24, 0x78, 0x48, 0x89, 0xBC, 0x24, 0x88, 0x00, 0x00,
    0x00, 0x48, 0x89, 0xBC, 0x24, 0x90, 0x00, 0x00, 0x00, 0x48, 0x89, 0x7C,
    0x24, 0x70, 0x4C, 0x89, 0xB4, 0x24, 0x80, 0x00, 0x00, 0x00, 0x48, 0x89,
    0xBC, 0x24, 0x98, 0x00, 0x00, 0x00, 0xC7, 0x84, 0x24, 0xA0, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x9D, 0x30, 0x02, 0x00, 0x00,
    0x48, 0x2B, 0xDE, 0x48, 0x63, 0xC3, 0x48, 0x3B, 0xC3, 0x75, 0x04, 0x85,
    0xDB, 0x79, 0x1A, 0x48, 0x8D, 0x8C, 0x24, 0xE8, 0x00, 0x00, 0x00, 0xC6,
    0x84, 0x24, 0xE8, 0x00, 0x00, 0x00, 0x00, 0xE8, 0x1D, 0x20, 0xCD, 0xFF,
    0x84, 0xC0, 0x74, 0x01, 0xCC, 0x48, 0x8D, 0xBD, 0x28, 0x02, 0x00, 0x00,
    0x48, 0x89, 0x74, 0x24, 0x40, 0x48, 0x3B, 0x77, 0x08, 0x72, 0x1E, 0x48,
    0x8D, 0x44, 0x24, 0x40, 0x48, 0x89, 0x7C, 0x24, 0x50, 0x48, 0x8D, 0x4C,
    0x24, 0x48, 0x48, 0x89, 0x44, 0x24, 0x48, 0xE8, 0xCD, 0xF9, 0xFF, 0xFF,
    0x84, 0xC0, 0x74, 0x01, 0xCC, 0x48, 0x8B, 0x54, 0x24, 0x40, 0x48, 0x8D,
    0x44, 0x24, 0x60, 0x48, 0x03, 0x17, 0x4C, 0x8D, 0x0D, 0x74, 0x47, 0xCC,
    0xFF, 0x48, 0x89, 0x44, 0x24, 0x30, 0x44, 0x8B, 0xC3, 0x48, 0x8D, 0x05,
    0xB5, 0x52, 0x95, 0x01, 0xC7, 0x44, 0x24, 0x28, 0x09, 0x00, 0x00, 0x00,
    0x41, 0x0F, 0xB6, 0xCF, 0x48, 0x89, 0x44, 0x24, 0x20, 0xE8, 0xFF, 0xEA,
    0x66, 0x00,
});

constexpr auto CompileCallSetup = std::to_array<std::uint8_t>({
    0x48, 0x8D, 0x05, 0xD2, 0x0C, 0x00, 0x00, 0x41, 0xB9, 0x00, 0x04, 0x00,
    0x00, 0x48, 0x89, 0x44, 0x24, 0x30, 0x4C, 0x8D, 0x44, 0x24, 0x40, 0x48,
    0x8D, 0x05, 0x7B, 0x0F, 0x00, 0x00, 0x48, 0x8B, 0xD7, 0x48, 0x89, 0x44,
    0x24, 0x28, 0x40, 0x0F, 0xB6, 0xCD, 0x48, 0x8D, 0x05, 0x68, 0x10, 0x00,
    0x00, 0x48, 0x89, 0x44, 0x24, 0x20, 0xE8, 0x4E, 0xFA, 0x66, 0x00,
});

constexpr auto GetDataTablesEntry = std::to_array<std::uint8_t>({
    0x48, 0x83, 0xEC, 0x28, 0x0F, 0xB6, 0xC1, 0x48, 0x89, 0x44, 0x24, 0x38, 0x48, 0x83, 0xF8, 0x04,
});
constexpr auto CompileEntry = std::to_array<std::uint8_t>({
    0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8D, 0xAC,
    0x24, 0x58, 0xFB, 0xFF, 0xFF, 0x48, 0x81, 0xEC,
});
constexpr auto InterpretEntry = std::to_array<std::uint8_t>({
    0x48, 0x89, 0x5C, 0x24, 0x18, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57,
    0x48, 0x8D, 0x6C, 0x24, 0x80, 0x48, 0x81, 0xEC,
});
constexpr auto RegistryLookupEntry = std::to_array<std::uint8_t>({
    0x40, 0x55, 0x56, 0x57, 0x41, 0x54, 0x48, 0x8B, 0xEC, 0x48, 0x83, 0xEC, 0x78, 0x48, 0x8B, 0x05,
    0xC4, 0x90, 0xFB, 0x01, 0x48, 0x33, 0xC4, 0x48,
});
constexpr auto TokenCallbackBody = std::to_array<std::uint8_t>({0x33, 0xC0, 0xC3});

static_assert(RelayBlob.size() == RelaySlotLookup + 8 * 11, "Relay layout changed.");

using GetDataTablesFn  = std::uint8_t* (*)(std::uint8_t bank) noexcept;
using ItemEvalFn       = std::int32_t (*)(std::uint8_t bank, void* unit, void* item, std::uint32_t formula) noexcept;
using CompileFn        = std::int32_t (*)(std::uint8_t bank, const char* text, std::uint8_t* out, std::int32_t capacity,
                                          const void* names, const void* argCount, const void* args) noexcept;
using InterpretFn      = std::int32_t (*)(std::uint8_t bank, const std::uint8_t* code, std::int32_t length,
                                          const void* tokens, const void* functions, std::int32_t functionCount,
                                          void* context) noexcept;
using RegistryLookupFn = std::uint32_t (*)(const void* registry, const char* name, std::int32_t report) noexcept;

// The context 0x3B4880 builds for an item formula.
struct alignas(16) ItemFormulaContext {
    const void*  vtable;    // +00
    void*        slot[7];   // +08 .. +38
    std::int32_t level;     // +40
    std::uint8_t hasItem;   // +44
    std::uint8_t pad[3];
    void*        item;      // +48
    std::uint8_t spare[16];
};
static_assert(offsetof(ItemFormulaContext, slot) == 0x08);
static_assert(offsetof(ItemFormulaContext, level) == 0x40);
static_assert(offsetof(ItemFormulaContext, hasItem) == 0x44);
static_assert(offsetof(ItemFormulaContext, item) == 0x48);

// ---------------------------------------------------------------------------
// Data built on every tables load
// ---------------------------------------------------------------------------
struct ExtensionBlock {
    std::int16_t  stat[ExtraSlots];
    std::uint8_t  pad[6];
    std::uint32_t calc[ExtraSlots];
};
static_assert(sizeof(ExtensionBlock) == 24);
static_assert(offsetof(ExtensionBlock, calc) == 12);

struct Formula {
    std::vector<std::uint8_t> code;   // compiled bytes plus zero padding
    std::int32_t              length; // compiled length + padding handed to the interpreter
};

struct BankRows {
    std::uintptr_t            rows{};
    std::uint64_t             count{};
    std::vector<std::int32_t> blockOfRow;   // -1 = no extra stats
};

struct Snapshot {
    std::vector<BankRows>       banks;
    std::vector<ExtensionBlock> blocks;      // never resized after publish
    std::vector<Formula>        formulas;
    std::uint32_t               rowsWithExtras{};
    std::string                 summary;
};

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
const D2RL::PluginContext*      Context{};
std::uintptr_t                  Base{};
const D2RL::DataTableService*   DataTableApi{};
const D2RL::LifecycleService*   LifecycleApi{};
D2RL::Lifecycle::ListenerHandle TablesListener{D2RL::Lifecycle::InvalidHandle};

struct Settings {
    bool enabled{true};
    bool diagnostics{false};
} Config;

bool Installed{false};
char InactiveReason[256]{};
bool ConfigFileRead{false};

GetDataTablesFn  GetDataTables{};
ItemEvalFn       OriginalItemEval{};
CompileFn        Compile{};
InterpretFn      Interpret{};
RegistryLookupFn RegistryLookup{};

std::uint8_t*                       RelayPage{};
std::array<bool, std::size(Sites)>  SitePatched{};
std::array<std::array<std::uint8_t, 16>, std::size(Sites)> SiteWritten{};

std::atomic<const Snapshot*> Current{nullptr};
std::vector<const Snapshot*> Retired;   // never freed: a relay may still be reading one

std::atomic<std::uint64_t> LoopsExtended{};
std::atomic<std::uint64_t> FormulasEvaluated{};
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
auto TakeDiagnosticLine() noexcept -> bool {
    if (!Config.diagnostics) return false;
    return DiagnosticLines.fetch_add(1, std::memory_order_relaxed) < 40;
}

// ---------------------------------------------------------------------------
// Called by the relay at the end of a stock loop: the real item record in,
// the virtual record base B for its extension block out, or null.
// ---------------------------------------------------------------------------
auto MiscExtraStatsLookup(const std::uint8_t* record) noexcept -> const std::uint8_t* {
    const Snapshot* snapshot = Current.load(std::memory_order_acquire);
    if (snapshot == nullptr || record == nullptr) return nullptr;
    const auto address = reinterpret_cast<std::uintptr_t>(record);
    for (const BankRows& bank : snapshot->banks) {
        if (address < bank.rows) continue;
        const std::uintptr_t offset = address - bank.rows;
        if (offset % ItemRecordSize != 0) continue;
        const std::uint64_t row = offset / ItemRecordSize;
        if (row >= bank.count) continue;
        const std::int32_t block = bank.blockOfRow[static_cast<std::size_t>(row)];
        if (block < 0) return nullptr;
        LoopsExtended.fetch_add(1, std::memory_order_relaxed);
        const auto* extension = reinterpret_cast<const std::uint8_t*>(&snapshot->blocks[static_cast<std::size_t>(block)]);
        return extension - VirtualBaseDistance;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// The item formula evaluator hook: plugin formulas in, everything else stock.
// ---------------------------------------------------------------------------
auto HookItemEval(std::uint8_t bank, void* unit, void* item, std::uint32_t formula) noexcept -> std::int32_t {
    if ((formula & FormulaTagMask) != FormulaTag) {
        return OriginalItemEval(bank, unit, item, formula);
    }
    const Snapshot* snapshot = Current.load(std::memory_order_acquire);
    const std::uint32_t index = formula & ~FormulaTagMask;
    if (snapshot == nullptr || index >= snapshot->formulas.size()) return 0;
    const Formula& compiled = snapshot->formulas[index];

    ItemFormulaContext context{};
    context.vtable  = At<const void*>(ContextVtableRva);
    for (void*& slot : context.slot) slot = unit;
    context.slot[3] = item;            // +20
    context.level   = 0;
    context.hasItem = 1;
    context.item    = item;

    FormulasEvaluated.fetch_add(1, std::memory_order_relaxed);
    const std::int32_t value = Interpret(bank, compiled.code.data(), compiled.length,
        At<const void*>(TokenCallbackRva), At<const void*>(FunctionTableRva), 9, &context);
    if (TakeDiagnosticLine()) {
        LogInfo("MiscExtraStats: extra calc %u evaluated to %d.", index, value);
    }
    return value;
}

// ===========================================================================
// misc.txt reading
// ===========================================================================
auto Trim(std::string_view text) noexcept -> std::string_view {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r')) text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) text.remove_suffix(1);
    return text;
}
auto EqualsNoCase(std::string_view a, std::string_view b) noexcept -> bool {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z') x = static_cast<char>(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = static_cast<char>(y - 'A' + 'a');
        if (x != y) return false;
    }
    return true;
}
// Excel quotes a cell that holds a comma or a quote: "..." with "" for ".
auto CellText(std::string_view cell) -> std::string {
    cell = Trim(cell);
    if (cell.size() >= 2 && cell.front() == '"' && cell.back() == '"') {
        cell = cell.substr(1, cell.size() - 2);
        std::string out;
        out.reserve(cell.size());
        for (std::size_t i = 0; i < cell.size(); ++i) {
            out.push_back(cell[i]);
            if (cell[i] == '"' && i + 1 < cell.size() && cell[i + 1] == '"') ++i;
        }
        return out;
    }
    return std::string(cell);
}
void SplitTabs(std::string_view line, std::vector<std::string_view>& cells) {
    cells.clear();
    std::size_t start = 0;
    for (;;) {
        const std::size_t tab = line.find('\t', start);
        if (tab == std::string_view::npos) { cells.push_back(line.substr(start)); return; }
        cells.push_back(line.substr(start, tab - start));
        start = tab + 1;
    }
}

struct ExtraRow {
    std::string code;
    std::array<std::string, ExtraSlots> stat;
    std::array<std::string, ExtraSlots> calc;
    std::uint32_t line{};
};
struct MiscFile {
    std::string           path;
    bool                  found{false};
    bool                  hasColumns{false};
    std::vector<ExtraRow> rows;   // only rows with at least one extra stat
};

void ParseMisc(std::string_view text, MiscFile& file) {
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF
        && static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF) {
        text.remove_prefix(3);
    }
    std::vector<std::string_view> cells;
    int codeColumn = -1;
    std::array<int, ExtraSlots> statColumn{-1, -1, -1};
    std::array<int, ExtraSlots> calcColumn{-1, -1, -1};
    std::size_t cursor = 0;
    std::uint32_t line = 0;
    bool header = true;
    while (cursor <= text.size()) {
        const std::size_t newline = text.find('\n', cursor);
        const std::size_t end = newline == std::string_view::npos ? text.size() : newline;
        std::string_view current = text.substr(cursor, end - cursor);
        cursor = end + 1;
        ++line;
        if (!current.empty() && current.back() == '\r') current.remove_suffix(1);
        if (header) {
            header = false;
            SplitTabs(current, cells);
            for (std::size_t c = 0; c < cells.size(); ++c) {
                const std::string_view name = Trim(cells[c]);
                if (EqualsNoCase(name, "code")) codeColumn = static_cast<int>(c);
                for (int s = 0; s < ExtraSlots; ++s) {
                    char column[8]{};
                    std::snprintf(column, sizeof(column), "stat%d", s + 4);
                    if (EqualsNoCase(name, column)) statColumn[s] = static_cast<int>(c);
                    std::snprintf(column, sizeof(column), "calc%d", s + 4);
                    if (EqualsNoCase(name, column)) calcColumn[s] = static_cast<int>(c);
                }
            }
            file.hasColumns = statColumn[0] >= 0 || statColumn[1] >= 0 || statColumn[2] >= 0;
            if (codeColumn < 0 || !file.hasColumns) return;
            continue;
        }
        if (Trim(current).empty()) { if (newline == std::string_view::npos) break; continue; }
        SplitTabs(current, cells);
        auto cell = [&](int column) -> std::string {
            return column >= 0 && static_cast<std::size_t>(column) < cells.size() ? CellText(cells[static_cast<std::size_t>(column)]) : std::string{};
        };
        ExtraRow row;
        row.line = line;
        bool any = false;
        for (int s = 0; s < ExtraSlots; ++s) {
            row.stat[s] = cell(statColumn[s]);
            row.calc[s] = cell(calcColumn[s]);
            any = any || !row.stat[s].empty();
        }
        if (any) {
            row.code = cell(codeColumn);
            file.rows.push_back(std::move(row));
        }
        if (newline == std::string_view::npos) break;
    }
}

auto Utf8ToWide(const char* text) -> std::wstring {
    if (text == nullptr || *text == '\0') return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
    if (size <= 1) return {};
    std::wstring out(static_cast<std::size_t>(size - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text, -1, out.data(), size);
    return out;
}
auto WideToUtf8(const std::wstring& text) -> std::string {
    if (text.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string out(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), out.data(), size, nullptr, nullptr);
    return out;
}
auto ReadWholeFile(const std::wstring& path, std::string& out) -> bool {
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    bool ok = GetFileSizeEx(file, &size) != 0 && size.QuadPart >= 0 && size.QuadPart < (64LL << 20);
    if (ok) {
        out.assign(static_cast<std::size_t>(size.QuadPart), '\0');
        DWORD read = 0;
        ok = out.empty() || (ReadFile(file, out.data(), static_cast<DWORD>(out.size()), &read, nullptr) != 0 && read == out.size());
    }
    CloseHandle(file);
    return ok;
}
void AddRoot(std::vector<std::wstring>& roots, std::wstring root) {
    while (!root.empty() && (root.back() == L'\\' || root.back() == L'/')) root.pop_back();
    if (root.empty()) return;
    for (const std::wstring& existing : roots) {
        if (CompareStringOrdinal(existing.c_str(), -1, root.c_str(), -1, TRUE) == CSTR_EQUAL) return;
    }
    roots.push_back(std::move(root));
}
// The misc.txt the game loads for one bank folder ("excel" or "excel\\base").
void LoadMisc(const wchar_t* folder, MiscFile& file) {
    std::vector<std::wstring> roots;
    const std::wstring mod = Utf8ToWide(Context->activeMod);
    for (const wchar_t* base : {Context->modDirectory, Context->scopeRootDirectory}) {
        if (base == nullptr || *base == L'\0') continue;
        AddRoot(roots, base);
        if (!mod.empty()) AddRoot(roots, std::wstring(base) + L"\\" + mod + L".mpq");
    }
    for (const std::wstring& root : roots) {
        const std::wstring path = root + L"\\data\\global\\" + folder + L"\\misc.txt";
        std::string text;
        if (!ReadWholeFile(path, text)) continue;
        file.path = WideToUtf8(path);
        file.found = true;
        ParseMisc(text, file);
        return;
    }
}

// ===========================================================================
// Building the extension data
// ===========================================================================
auto PackCode(std::string_view code, char pad) noexcept -> std::uint32_t {
    std::uint32_t packed = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        const char c = i < code.size() ? code[i] : pad;
        packed |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(c)) << (8U * i);
    }
    return packed;
}

auto BankOfRows(std::uintptr_t rows) noexcept -> D2RL::DataTables::Bank {
    if (DataTableApi == nullptr) return static_cast<D2RL::DataTables::Bank>(0);
    for (const auto bank : {D2RL::DataTables::Bank::Classic, D2RL::DataTables::Bank::Lod, D2RL::DataTables::Bank::Rotw}) {
        D2RL::DataTables::TableView view{};
        view.structSize = D2RL::DataTables::TableViewSize;
        if (DataTableApi->getTable(Context, bank, D2RL::DataTables::TableId::Items, &view) == D2RL::DataTables::Result::Success
            && D2RL::DataTables::HasTableViewField(&view, D2RL::DataTables::TableViewRequiredSize)
            && reinterpret_cast<std::uintptr_t>(view.rows) == rows) {
            return bank;
        }
    }
    return static_cast<D2RL::DataTables::Bank>(0);
}

auto BankName(D2RL::DataTables::Bank bank) noexcept -> const char* {
    switch (bank) {
    case D2RL::DataTables::Bank::Classic: return "Classic";
    case D2RL::DataTables::Bank::Lod:     return "LoD";
    case D2RL::DataTables::Bank::Rotw:    return "RotW";
    default:                              return "unknown";
    }
}

auto BuildSnapshot() -> std::unique_ptr<Snapshot> {
    auto snapshot = std::make_unique<Snapshot>();
    MiscFile rotwFile;
    MiscFile baseFile;
    LoadMisc(L"excel", rotwFile);
    LoadMisc(L"excel\\base", baseFile);

    // Reserve generously so block and formula storage never moves once built.
    std::size_t maxRows = rotwFile.rows.size() + baseFile.rows.size();
    snapshot->blocks.reserve(maxRows * 3 + 1);
    snapshot->formulas.reserve(maxRows * ExtraSlots * 3 + 1);

    std::vector<std::uintptr_t> seen;
    for (std::uint8_t dataBank = 0; dataBank < 4; ++dataBank) {
        std::uint8_t* tables = GetDataTables(dataBank);
        if (tables == nullptr) continue;
        const auto rows  = *reinterpret_cast<const std::uintptr_t*>(tables + ItemsRowsOffset);
        const auto count = *reinterpret_cast<const std::uint64_t*>(tables + ItemsCountOffset);
        if (rows == 0 || count == 0 || count > 0x100000) continue;
        if (std::find(seen.begin(), seen.end(), rows) != seen.end()) continue;
        seen.push_back(rows);

        const D2RL::DataTables::Bank bank = BankOfRows(rows);
        const MiscFile* file = nullptr;
        if (bank == D2RL::DataTables::Bank::Rotw) file = &rotwFile;
        else if (bank == D2RL::DataTables::Bank::Lod || bank == D2RL::DataTables::Bank::Classic) file = &baseFile;

        BankRows bankRows;
        bankRows.rows  = rows;
        bankRows.count = count;
        bankRows.blockOfRow.assign(static_cast<std::size_t>(count), -1);

        char line[512]{};
        if (file == nullptr) {
            std::snprintf(line, sizeof(line), "items table %u: bank not identified, no extra stats", dataBank);
            snapshot->summary += (snapshot->summary.empty() ? "" : " | ") + std::string(line);
            snapshot->banks.push_back(std::move(bankRows));
            continue;
        }
        if (!file->found || !file->hasColumns || file->rows.empty()) {
            std::snprintf(line, sizeof(line), "%s: %s", BankName(bank),
                          !file->found ? "misc.txt not found in the mod folder"
                          : !file->hasColumns ? "no stat4..6 columns" : "no row fills stat4..6");
            snapshot->summary += (snapshot->summary.empty() ? "" : " | ") + std::string(line);
            snapshot->banks.push_back(std::move(bankRows));
            continue;
        }

        // The game's own compiled records, by code.
        std::unordered_map<std::uint32_t, std::uint32_t> rowOfCode;
        rowOfCode.reserve(static_cast<std::size_t>(count) * 2);
        for (std::uint64_t r = 0; r < count; ++r) {
            std::uint32_t code = 0;
            std::memcpy(&code, reinterpret_cast<const std::uint8_t*>(rows) + r * ItemRecordSize + ItemCodeOffset, 4);
            rowOfCode.emplace(code, static_cast<std::uint32_t>(r));
        }
        const void* registry = *reinterpret_cast<void* const*>(tables + StatRegistryOffset);

        std::uint32_t applied = 0, badCodes = 0, badStats = 0, badCalcs = 0;
        for (const ExtraRow& row : file->rows) {
            auto found = rowOfCode.find(PackCode(row.code, ' '));
            if (found == rowOfCode.end()) found = rowOfCode.find(PackCode(row.code, '\0'));
            if (row.code.empty() || row.code.size() > 4 || found == rowOfCode.end()) {
                ++badCodes;
                LogWarn("MiscExtraStats: %s misc.txt line %u: item code '%s' is not a loaded item; its extra stats are skipped.",
                        BankName(bank), row.line, row.code.c_str());
                continue;
            }
            ExtensionBlock block{};
            bool anyStat = false;
            for (int s = 0; s < ExtraSlots; ++s) {
                block.stat[s] = -1;
                block.calc[s] = NoFormula;
                if (!row.stat[s].empty()) {
                    const std::uint32_t id = registry != nullptr ? RegistryLookup(registry, row.stat[s].c_str(), 0) : 0xFFFFFFFFU;
                    if (id > 0x7FFF) {
                        ++badStats;
                        LogWarn("MiscExtraStats: %s misc.txt line %u (%s): stat%d '%s' is not an itemstatcost.txt stat.",
                                BankName(bank), row.line, row.code.c_str(), s + 4, row.stat[s].c_str());
                    } else {
                        block.stat[s] = static_cast<std::int16_t>(id);
                        anyStat = true;
                    }
                }
                if (!row.calc[s].empty()) {
                    std::array<std::uint8_t, FormulaBufferBytes> buffer{};
                    const std::int32_t length = Compile(static_cast<std::uint8_t>(dataBank), row.calc[s].c_str(), buffer.data(),
                        FormulaBufferBytes, At<const void*>(FnNameResolverRva), At<const void*>(ArgCountCheckRva),
                        At<const void*>(ArgResolverRva));
                    if (length <= 0 || length > FormulaBufferBytes) {
                        ++badCalcs;
                        LogWarn("MiscExtraStats: %s misc.txt line %u (%s): calc%d does not compile: %s",
                                BankName(bank), row.line, row.code.c_str(), s + 4, row.calc[s].c_str());
                    } else if (snapshot->formulas.size() < 0x00FFFFFF) {
                        Formula formula;
                        formula.code.assign(buffer.begin(), buffer.begin() + length);
                        formula.code.resize(static_cast<std::size_t>(length) + 16, 0);
                        formula.length = length + 16;
                        block.calc[s] = FormulaTag | static_cast<std::uint32_t>(snapshot->formulas.size());
                        snapshot->formulas.push_back(std::move(formula));
                    }
                }
            }
            if (!anyStat) continue;
            bankRows.blockOfRow[found->second] = static_cast<std::int32_t>(snapshot->blocks.size());
            snapshot->blocks.push_back(block);
            ++applied;
            if (Config.diagnostics) {
                LogInfo("MiscExtraStats: %s %s: stat4..6 = %d, %d, %d; calc ids %08X %08X %08X.", BankName(bank),
                        row.code.c_str(), block.stat[0], block.stat[1], block.stat[2], block.calc[0], block.calc[1], block.calc[2]);
            }
        }
        snapshot->rowsWithExtras += applied;
        std::snprintf(line, sizeof(line), "%s: %u item%s with extra stats from %s%s", BankName(bank), applied,
                      applied == 1 ? "" : "s", file->path.c_str(),
                      (badCodes + badStats + badCalcs) != 0 ? " (problems are in the log)" : "");
        snapshot->summary += (snapshot->summary.empty() ? "" : " | ") + std::string(line);
        snapshot->banks.push_back(std::move(bankRows));
    }
    return snapshot;
}

void __cdecl OnTablesLoaded(const D2RL::PluginContext*, const D2RL::Lifecycle::DataTablesLoadedEvent*, void*) noexcept {
    if (!Installed) return;
    try {
        std::unique_ptr<Snapshot> built = BuildSnapshot();
        const Snapshot* previous = Current.exchange(built.release(), std::memory_order_acq_rel);
        if (previous != nullptr) Retired.push_back(previous);
        const Snapshot* now = Current.load(std::memory_order_acquire);
        LogInfo("MiscExtraStats: %s.", now->summary.empty() ? "no items table found" : now->summary.c_str());
    } catch (...) {
        LogError("MiscExtraStats: building the extra stats failed; the previous set stays active.");
    }
}

// ===========================================================================
// Install
// ===========================================================================
auto CheckExact(std::uint64_t rva, const std::uint8_t* bytes, std::size_t size, const char* what) noexcept -> bool {
    if (Context->CheckExpectedBytes(rva, bytes, static_cast<std::uint32_t>(size))) return true;
    SetInactive("%s at RVA 0x%llX does not match D2RLoader 1.3.1", what, static_cast<unsigned long long>(rva));
    return false;
}
// A function we only call may carry another plugin's inline hook at its entry.
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
            LogInfo("MiscExtraStats: %s at RVA 0x%llX is hooked by another plugin; calling through it.", what,
                    static_cast<unsigned long long>(rva));
            return true;
        }
    }
    SetInactive("%s at RVA 0x%llX does not match D2RLoader 1.3.1", what, static_cast<unsigned long long>(rva));
    return false;
}

auto Verify() noexcept -> bool {
    return CheckExact(0x57EDF0, PSpell3Head.data(), PSpell3Head.size(), "the pSpell 3 loop")
        && CheckExact(0x57F1B0, PSpell3Tail.data(), PSpell3Tail.size(), "the pSpell 3 loop end")
        && CheckExact(0x57F3C0, PSpell4Head.data(), PSpell4Head.size(), "the pSpell 4 loop")
        && CheckExact(0x57F6CA, PSpell4Tail.data(), PSpell4Tail.size(), "the pSpell 4 loop end")
        && CheckExact(0x57F8D0, PSpell5Head.data(), PSpell5Head.size(), "the pSpell 5 loop")
        && CheckExact(0x57F9D8, PSpell5Tail.data(), PSpell5Tail.size(), "the pSpell 5 loop end")
        && CheckExact(0x57FF5E, PSpell9Body.data(), PSpell9Body.size(), "the pSpell 9 walk")
        && CheckExact(0x580B7E, CanUseBody.data(), CanUseBody.size(), "the pSpell 3/4/5 use check")
        && CheckExact(ItemEvalBodyRva, ItemEvalBody.data(), ItemEvalBody.size(), "the item formula evaluator body")
        && CheckExact(CompileCallSetupRva, CompileCallSetup.data(), CompileCallSetup.size(), "the item formula compiler call")
        && CheckCallable(GetDataTablesRva, GetDataTablesEntry.data(), GetDataTablesEntry.size(), "GetDataTables")
        && CheckCallable(CompileRva, CompileEntry.data(), CompileEntry.size(), "FORMULA_Compile")
        && CheckCallable(InterpretRva, InterpretEntry.data(), InterpretEntry.size(), "the formula interpreter")
        && CheckCallable(RegistryLookupRva, RegistryLookupEntry.data(), RegistryLookupEntry.size(), "the name registry lookup")
        && CheckExact(TokenCallbackRva, TokenCallbackBody.data(), TokenCallbackBody.size(), "the formula token callback");
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

auto BuildRelay() noexcept -> bool {
    RelayPage = AllocateNear(Base + Sites[0].rva, 0x1000);
    if (RelayPage == nullptr) {
        SetInactive("no memory page is free within reach of the pSpell handlers");
        return false;
    }
    for (const LoopSite& site : Sites) {
        if (!CanReach(Base + site.rva + 5, reinterpret_cast<std::uintptr_t>(RelayPage) + site.relayEntry)) {
            SetInactive("the relay page is out of reach of the %s loop", site.name);
            return false;
        }
    }
    std::memcpy(RelayPage, RelayBlob.data(), RelayBlob.size());
    const auto put = [](std::size_t offset, std::uintptr_t value) noexcept { std::memcpy(RelayPage + offset, &value, 8); };
    put(RelaySlotLookup, reinterpret_cast<std::uintptr_t>(&MiscExtraStatsLookup));
    for (const LoopSite& site : Sites) {
        put(site.headSlot, Base + site.head);
        put(site.doneSlot, Base + site.done);
    }
    DWORD old = 0;
    if (!VirtualProtect(RelayPage, 0x1000, PAGE_EXECUTE_READ, &old)) {
        SetInactive("the relay page could not be made executable");
        return false;
    }
    FlushInstructionCache(GetCurrentProcess(), RelayPage, 0x1000);
    return true;
}

auto SiteWindow(std::size_t index) noexcept -> std::pair<const std::uint8_t*, std::size_t> {
    switch (index) {
    case 0:  return {PSpell3Window.data(), PSpell3Window.size()};
    case 1:  return {PSpell4Window.data(), PSpell4Window.size()};
    case 2:  return {PSpell5Window.data(), PSpell5Window.size()};
    case 3:  return {PSpell9Window.data(), PSpell9Window.size()};
    default: return {CanUseWindow.data(), CanUseWindow.size()};
    }
}

void RestoreSites() noexcept {
    for (std::size_t i = 0; i < std::size(Sites); ++i) {
        if (!SitePatched[i]) continue;
        const auto [stock, size] = SiteWindow(i);
        if (Context->PatchBytes(Sites[i].rva, SiteWritten[i].data(), static_cast<std::uint32_t>(size), stock,
                                static_cast<std::uint32_t>(size))) {
            SitePatched[i] = false;
        }
    }
}

auto PatchSites() noexcept -> bool {
    for (std::size_t i = 0; i < std::size(Sites); ++i) {
        const auto [stock, size] = SiteWindow(i);
        std::array<std::uint8_t, 16>& bytes = SiteWritten[i];
        bytes.fill(0x90);
        const auto target = reinterpret_cast<std::uintptr_t>(RelayPage) + Sites[i].relayEntry;
        const auto rel = static_cast<std::int32_t>(static_cast<std::int64_t>(target) - static_cast<std::int64_t>(Base + Sites[i].rva + 5));
        bytes[0] = 0xE9;
        std::memcpy(bytes.data() + 1, &rel, 4);
        if (!Context->PatchBytes(Sites[i].rva, stock, static_cast<std::uint32_t>(size), bytes.data(), static_cast<std::uint32_t>(size))) {
            SetInactive("the %s loop could not be patched", Sites[i].name);
            RestoreSites();
            return false;
        }
        SitePatched[i] = true;
    }
    return true;
}

auto Install() noexcept -> bool {
    if (Context->QueryService(&DataTableApi) != D2RL::ServiceQueryResult::Success
        || !D2RL::HasDataTableServiceField(DataTableApi, D2RL::DataTableServiceRequiredSize)) {
        DataTableApi = nullptr;
    }
    if (Context->QueryService(&LifecycleApi) != D2RL::ServiceQueryResult::Success
        || !D2RL::HasLifecycleServiceField(LifecycleApi, D2RL::LifecycleServiceRequiredSize)) {
        LifecycleApi = nullptr;
    }
    if (DataTableApi == nullptr || LifecycleApi == nullptr) {
        SetInactive("the data table or lifecycle service is unavailable");
        return false;
    }
    if (!Verify()) return false;

    GetDataTables  = At<GetDataTablesFn>(GetDataTablesRva);
    Compile        = At<CompileFn>(CompileRva);
    Interpret      = At<InterpretFn>(InterpretRva);
    RegistryLookup = At<RegistryLookupFn>(RegistryLookupRva);

    if (!BuildRelay()) return false;

    // The evaluator hook goes in first: it only answers tagged ids, which do
    // not exist anywhere until a loop has been extended.
    if (!Context->InstallInlineHook(ItemEvalRva, ItemEvalPrologue.data(), static_cast<std::uint32_t>(ItemEvalPrologue.size()),
                                    reinterpret_cast<void*>(&HookItemEval), reinterpret_cast<void**>(&OriginalItemEval))
        || OriginalItemEval == nullptr) {
        SetInactive("the item formula evaluator hook at 0x3B4880 could not be installed");
        return false;
    }
    if (!PatchSites()) return false;

    const D2RL::Lifecycle::DataTablesLoadedListener listener{
        .structSize = D2RL::Lifecycle::DataTablesLoadedListenerSize,
        .callback   = &OnTablesLoaded,
    };
    if (LifecycleApi->registerDataTablesLoadedListener(Context, &listener, &TablesListener) != D2RL::Lifecycle::Result::Success) {
        RestoreSites();
        SetInactive("the tables-loaded listener was refused");
        return false;
    }
    Installed = true;
    return true;
}

// ===========================================================================
// Config
// ===========================================================================
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
        else if (key == "diagnostics") parsed = ParseBool(value, Config.diagnostics);
        else parsed = false;
        if (!parsed) LogWarn("MiscExtraStats: config line ignored: %.*s", static_cast<int>(line.size()), line.data());
    }
}

// ===========================================================================
// Console
// ===========================================================================
auto StatusCommand(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command, void*) noexcept -> D2RL::ConsoleCommandResult {
    char text[1024]{};
    if (!Config.enabled) {
        std::snprintf(text, sizeof(text), "Misc Extra Stats %s: off in the config file.", PluginVersion);
    } else if (!Installed) {
        std::snprintf(text, sizeof(text), "Misc Extra Stats %s: NOT ACTIVE, %s. Nothing was changed.", PluginVersion,
                      InactiveReason[0] != '\0' ? InactiveReason : "install failed");
    } else {
        const Snapshot* snapshot = Current.load(std::memory_order_acquire);
        std::snprintf(text, sizeof(text),
                      "Misc Extra Stats %s: active. %s. Loops extended %llu times, extra calcs evaluated %llu times. Config %s.",
                      PluginVersion, snapshot == nullptr ? "Waiting for the tables to load" : snapshot->summary.c_str(),
                      static_cast<unsigned long long>(LoopsExtended.load(std::memory_order_relaxed)),
                      static_cast<unsigned long long>(FormulasEvaluated.load(std::memory_order_relaxed)),
                      ConfigFileRead ? "loaded" : "NOT READ (defaults)");
    }
    command->plugin->WriteConsoleMessage(text, D2RL::ConsoleMessageKind::Output);
    return D2RL::ConsoleCommandResult::Handled;
}

constexpr D2RL::PluginInfo Info{
    .infoSize    = D2RL::PluginInfoSize,
    .abiVersion  = D2RL_PLUGIN_ABI_VERSION,
    .id          = "celestialrayone.misc-extra-stats",
    .name        = "Misc Extra Stats",
    .version     = PluginVersion,
    .author      = "CelestialRayOne",
    .description = "stat4/calc4 .. stat6/calc6 columns for misc.txt.",
    .flags       = D2RL::PluginFlags::Server | D2RL::PluginFlags::NativeHooks,
};
static_assert(D2RL::HasFlag(Info.flags, D2RL::PluginFlags::NativeHooks), "Inline hooks require the NativeHooks flag.");

}  // namespace
}  // namespace CelestialRayOne::MiscExtraStats

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* {
    return &CelestialRayOne::MiscExtraStats::Info;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
    using namespace CelestialRayOne::MiscExtraStats;
    if (!D2RL::HasContext(context)) return false;
    Context = context;
    Base = context->exeBase;
    if (!Context->RegisterConsoleCommand("miscextrastats", &StatusCommand,
                                         "Misc Extra Stats status: what was read per bank and how often it ran.")) {
        Context->LogWarn("MiscExtraStats: the console command was refused.");
    }
    ReadConfiguration();
    if (!Config.enabled) {
        Context->LogInfo("MiscExtraStats: off in the config file; nothing was changed.");
        return true;
    }
    if (!Install()) {
        LogError("MiscExtraStats: NOT ACTIVE, %s. Nothing was changed.", InactiveReason[0] != '\0' ? InactiveReason : "install failed");
        return true;
    }
    LogInfo("MiscExtraStats %s: active (5 loops extended, item formula evaluator hooked).", PluginVersion);
    return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
    using namespace CelestialRayOne::MiscExtraStats;
    if (Context == nullptr) return;
    if (LifecycleApi != nullptr && TablesListener != D2RL::Lifecycle::InvalidHandle) {
        LifecycleApi->unregisterDataTablesLoadedListener(Context, TablesListener);
        TablesListener = D2RL::Lifecycle::InvalidHandle;
    }
    RestoreSites();
}
