// =============================================================================
//  Aura Zero Results  -  celestialrayone.aura-zero-results
//
//  D2R 3.3 under D2RLoader 1.3.1. An aura formula that returns 0 removes its
//  stat instead of leaving the last nonzero value in place:
//
//    1. AURA STATS. Results written through the shared aura callback:
//       aurastat/aurastatcalc of srvdofunc 65 (caster and targets), and the
//       caster passivestats and target aurastats of srvdofunc 66, 81 and 82.
//    2. PASSIVE STATS. passivestat/passivecalc results srvdofunc 65 writes
//       onto the caster's aura state.
//
//  Every address below was read out of the dumped D2RLoader 1.3.1 image and
//  disassembled before being relied on. Byte witnesses guard every function
//  the plugin changes or calls, so a different build refuses cleanly.
//
// -----------------------------------------------------------------------------
//  WHY A 0 NEVER LANDS (server)
// -----------------------------------------------------------------------------
//  0x238EA00  srvdofunc table; slot 65 (0x238EC08) -> 0x563D90.
//  0x563D90   srvdofunc 65. Evaluates aurastatcalc 1-6 once per tick in the
//             caster's context (stat ids skill+0x74, calcs skill+0x88) into
//             its args block, then applies them to the caster's aurastate
//             (0x432AE0) and to every target's auratargetstate (0x4327D0),
//             both through the callback 0x5616F0.
//  0x5616F0   aura callback(context, target)
//             rsi = context ([rsi] = game), r12 = args ([rcx+18h]),
//             r14 = target. 0x433D20 returns the state's stat list in r13
//             (copied to [r12+88h]). It REUSES the existing list when state,
//             skill and level match, so a stat changes only when written.
//             args: [r12] state id, [r12+14h+4j] stat id j, [r12+4Ch+4j]
//             result j, [r12+84h] "the aura did something" count, which the
//             handler reads afterwards to drain mana.
//    slot loop 0x561AD2 .. 0x561CDC, 6 slots:
//    0x561AD6  test r15d,r15d / je 0x561CC8      gate 1: result 0 -> skipped
//    0x561AE3  stat id <= 0 or >= the itemstatcost count -> skipped
//    0x561B2F  stat 110 (item_poisonlengthresist) -> 0x561DE0, which scales
//              every curable state's remaining time by result/100.
//    0x561B5B  call 0x55BFB0, the resistance rule: a negative resist result on
//              a non-player, non-hireling unit with 100+ base resist is /5.
//              Everything else, 0 included, comes back unchanged.
//    0x561B62  test eax,eax / je 0x561CC4        gate 2: 0 after it -> skipped
//    0x561B6A  the write: record 0x2D9730; "direct" stats (record+4 & 8) are
//              added to the unit; every other stat: read the list (0x2F5DF0),
//              toggle the state's gfx bit (0x3355A0) when it changes, floor
//              67/68 at the target's cold effect (0x449F40), write the list
//              (0x2F7C00), copy 68 into 69, count it at [r12+84h].
//    0x5617F9 .. 0x561AB7  the game's own zeroing pass. It runs only when a
//              different caster refreshed a list it did not create, and writes
//              0 exactly like the write above, minus the count. This plugin
//              gives a caster's own 0 results that same write.
//  0x561AE5  test edi,edi / jle 0x561CC8   stat id check: ids <= 0 skipped,
//              so strength (stat 0) never applies. strength_aura_stats turns
//              it into jl. aura_stats works with either form.
//  0x563E40 .. 0x563E5C  srvdofunc 65 zeroes its args block, then collects
//              aurastat ids and values for slots before the first empty
//              column (0x563EC6, js on id < 0). The slots after it keep id 0
//              and value 0, and so do all six when the mana check at 0x563EAE
//              skips the collection.
//  0x563FD9 .. 0x5640A8  srvdofunc 65's passivestat loop, 14 slots (stat ids
//              skill+0xBA, calcs skill+0xD8). 0x56403A je 0x564096 skips a 0;
//              0x564091 writes the rest onto the caster's list.
//
// -----------------------------------------------------------------------------
//  WHAT THE PLUGIN CHANGES
// -----------------------------------------------------------------------------
//  aura_stats
//    0x561AD9  je 0x561CC8 -> 6 byte nop. A 0 result goes on to the stock
//              stat id checks.
//    0x561B37  call redirect, cleanse. A 0 returns 0 without cleansing. That
//              is what the game did before, since a 0 never got there.
//    0x561B5B  call redirect, resistance rule. The stock rule runs. When its
//              result is 0 the stat is written the way the zeroing pass writes
//              it, then the 0 goes back, so the stock je at 0x561B64 skips the
//              rest of the slot and the count stays untouched.
//    0x563E60  call redirect in srvdofunc 65, right after its args block is
//              zeroed: the six aurastat ids are set to -1 before the
//              collection, so a slot it does not fill carries no stat. A 0
//              result in a filled slot is now written, and without this an
//              unfilled slot (id 0, value 0) would be read as a strength 0
//              wherever stat id 0 is allowed, clearing a real strength value.
//              The stock call (0x34B6C0) still runs and its result is kept.
//  strength_aura_stats
//    0x561AE7  jle 0x561CC8 -> jl 0x561CC8 (0x561AE8, 8E -> 8C). Only ids < 0
//              (an empty column) are skipped, so stat id 0 (strength) applies
//              like any other aura stat. The same change as the 2.4 patch at
//              383FB2 and the basic-aura-stat-id-0.json memory patch, which
//              this replaces. When that memory patch already made the change
//              the plugin leaves the byte to it.
//  passive_stats
//    0x56403A  je 0x564096 -> 2 byte nop.
//    0x564091  call redirect, list write. Nonzero values go to the stock
//              write. A 0 is written only when the stat is on the list.
//
//  The redirected calls land on a page the plugin allocates within rel32
//  reach of the image; nothing inside D2R's own code is used as a cave.
//
//  Console command "aurazeroresults" shows what is installed and counters.
//  Settings: d2rloader/config/celestialrayone.aura-zero-results.toml
// =============================================================================

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

namespace CelestialRayOne::AuraZeroResults {
namespace {

constexpr char PluginVersion[] = "1.1.0";

// ---------------------------------------------------------------------------
//  Default configuration. EnsureConfig writes this text on first load, and it
//  is the plugin's documentation as shipped.
// ---------------------------------------------------------------------------
constexpr const char DefaultConfigToml[] = R"TOML(# celestialrayone.aura-zero-results
#
# Aura Zero Results
#   An aura formula that returns 0 now removes its stat. Without this plugin a
#   0 is never written, so the stat keeps the last nonzero value it had for as
#   long as the aura runs.
#
# Changes are read when the plugin loads. Restart the game after editing.
# Built for D2RLoader 1.3.1 (Diablo II: Resurrected 3.3). On any other build
# the byte checks fail and the plugin loads without changing the game.
#
# Console command "aurazeroresults" shows what is installed and counts what
# the plugin did.
#
# Compatibility
#   Another plugin or memory patch that changes the aura callback (RVA
#   0x5616F0) or the srvdofunc 65 passive stat loop makes the byte checks for
#   that part fail. That part then stays off, and the plugin log and the
#   console command name the byte and who changed it.
#   ESR's passivestat-zero-mana-gate.json memory patch changes srvdofunc 65
#   bytes this plugin does not check, so the two work together.

# Master switch. false loads the plugin without touching the game.
enabled = true

# -----------------------------------------------------------------------------
# aura_stats
#
# The problem
#   An aura keeps one stat list per state on each unit and reuses it every
#   tick. The game writes a result into that list only when the result is not
#   0. With
#       aurastatcalc1 = (stat('somestat'.accr)>0)?100:0
#   the stat gets 100 the first time the condition holds, then stays at 100
#   until the aura ends, even after the condition stops holding. A result that
#   the immune-monster resistance rule (negative resist divided by 5) rounds
#   down to 0 sticks the same way.
#
# What changes
#   A 0 result is written the way the game writes any other change: the stat
#   is removed from the state's list, the unit's totals update right away and
#   the client is told about the state. A slot that is already 0 costs one
#   lookup and writes nothing.
#
# Skills covered
#   Everything the game applies through its shared aura callback:
#     srvdofunc 65         aurastat/aurastatcalc, on the caster and on targets
#     srvdofunc 66 and 81  passivestat/passivecalc on the caster,
#                          aurastat/aurastatcalc on targets
#     srvdofunc 82         passivestat/passivecalc on the caster
#
# Left as they are
#   item_poisonlengthresist (Cleansing style). A 0 still does nothing there.
#     Passing it on would end every poison and curse on the unit at once.
#   Stats with "direct" set in itemstatcost.txt (life, mana and stamina style
#     heals). They are added to the unit each tick rather than kept on the
#     state, so a 0 has nothing to remove.
#   Mana. A 0 result does not count as the aura doing something, so an aura
#     whose results are all 0 still costs no mana that tick.
#   srvdofunc 65 when a player cannot pay the aura's mana cost: the game skips
#     the whole stat block then, and the last values stay, as before.
#
# Behavior notes
#   A 0 now removes the stat instead of holding the last value. A formula that
#     relied on holding, on purpose or through integer division reaching 0,
#     behaves differently.
#   srvdofunc 66, 81 and 82 fill their caster stat ids even when a player
#     cannot pay the mana cost, and leave the results at 0. Those caster stats
#     are now removed while the player is out of mana instead of keeping their
#     old values. Only matters for such rows with a mana cost.
#   Two casters of the same aura at the same level share one stat list on a
#     target, and whichever ticks last sets the value. A caster whose formula
#     gives 0 now sets 0 there, like any other value.
# -----------------------------------------------------------------------------
aura_stats = true

# -----------------------------------------------------------------------------
# strength_aura_stats
#
# The problem
#   The aura callback skips every stat id of 0 or below, so strength
#   (itemstatcost id 0) never applies as an aurastat, even though the skill
#   row collects it.
#
# What changes
#   Only ids below 0 (an empty column) are skipped, so strength applies like
#   any other aura stat. Same 1 byte change as the 2.4 patch at 383FB2.
#
# Notes
#   Replaces the basic-aura-stat-id-0.json memory patch: delete that file.
#   While it is still in the patches folder the plugin finds the byte already
#   changed and leaves it to that patch.
#   Works with or without aura_stats.
# -----------------------------------------------------------------------------
strength_aura_stats = true

# -----------------------------------------------------------------------------
# passive_stats
#
# The problem
#   srvdofunc 65 writes passivestat/passivecalc results onto the caster's aura
#   state with the same rule: a 0 is never written, so the stat keeps its last
#   nonzero value.
#
# What changes
#   A 0 result removes the stat from the caster's aura state. A stat that is
#   not on the state is left alone.
#
# Left as they are
#   The game runs this block only while the caster has more mana than the
#   aura costs and the skill has no passivestate. Outside that nothing is
#   written at all, and the last values stay, as before.
# -----------------------------------------------------------------------------
passive_stats = true

# Writes the first 64 changes (stat, old and new value, state, unit) to the
# plugin log. Off for normal play.
diagnostics = false
)TOML";

// ---------------------------------------------------------------------------
//  Native anchors
// ---------------------------------------------------------------------------
constexpr std::uint64_t SrvDoTableRva          = 0x238EA00;
constexpr std::size_t   BasicAuraSrvDoSlot     = 65;
constexpr std::uint64_t BasicAuraHandlerRva    = 0x563D90;

constexpr std::uint64_t AuraCallbackRva        = 0x5616F0;
constexpr std::uint64_t AuraLoopHeadRva        = 0x561AC0;
constexpr std::uint64_t StatIdGateRva          = 0x561AE7;  // jle 0x561CC8 after test edi,edi
constexpr std::uint64_t StatIdGateOpcodeRva    = 0x561AE8;  // jle (8E) stock, jl (8C) with stat id 0 allowed
constexpr std::uint64_t AuraLoopRestRva        = 0x561AE9;
constexpr std::uint64_t ZeroGateRva            = 0x561AD9;
constexpr std::uint64_t CleanseCallRva         = 0x561B37;
constexpr std::uint64_t ResistanceCallRva      = 0x561B5B;

constexpr std::uint64_t ArgsSetupRva           = 0x563E21;
constexpr std::uint64_t ArgsCallRva            = 0x563E60;
constexpr std::uint64_t AuraCollectRva         = 0x563EB0;
constexpr std::uint64_t PassiveSelfApplyRva    = 0x563FC0;
constexpr std::uint64_t PassiveLoopRva         = 0x563FFC;
constexpr std::uint64_t PassiveZeroGateRva     = 0x56403A;
constexpr std::uint64_t PassiveWriteCallRva    = 0x564091;

constexpr std::uint64_t ResistanceRuleRva      = 0x55BFB0;
constexpr std::uint64_t CleanseRva             = 0x561DE0;
constexpr std::uint64_t StatRecordRva          = 0x2D9730;
constexpr std::uint64_t ReadListStatRva        = 0x2F5DF0;  // loader thunk, D2RCore!ReadWideListStat
constexpr std::uint64_t WriteListStatRva       = 0x2F7C00;  // loader thunk, D2RCore!SetWideListStat
constexpr std::uint64_t ToggleGfxStateRva      = 0x3355A0;
constexpr std::uint64_t GetGameRva             = 0x48FF00;
constexpr std::uint64_t ColdEffectRva          = 0x449F40;
constexpr std::uint64_t BitMasksRva            = 0x1D996D0;  // the game's bit table; [3] = 8
constexpr std::uint64_t CastFrameRva           = 0x34B6C0;   // unit -> [unit+12Ch], -1 without a unit

constexpr std::size_t   GameDataContextOffset  = 0x106;  // byte, game+106h
constexpr std::size_t   StatRecordFlagsOffset  = 0x04;   // byte, tested at 0x561B8B
constexpr std::uint8_t  DirectFlagMask         = 0x08;   // bit table [3], the "direct" column
constexpr std::size_t   ArgsStateOffset        = 0x00;   // [r12], copied to [rbp+0Bh] at 0x56174B
constexpr std::size_t   UnitIdOffset           = 0x08;
constexpr std::size_t   ArgsStatIdsOffset      = 0x14;   // six aurastat ids, [rbp-6Ch] in srvdofunc 65
constexpr std::size_t   ArgsAuraSlots          = 6;      // the callback walks six slots (0x561AC5)

constexpr std::int32_t  VelocityPercentStat    = 67;
constexpr std::int32_t  AttackRateStat         = 68;
constexpr std::int32_t  OtherAnimRateStat      = 69;

// ---------------------------------------------------------------------------
//  Byte witnesses
// ---------------------------------------------------------------------------
// 0x5616F0 .. 0x5617A7: callback entry through mov [r12+88h],r13. Proves
// rsi = context, r12 = args, r14 = target, the state id at [r12] and the state
// list from 0x433D20 in r13.
constexpr auto AuraCallbackEntry = std::to_array<std::uint8_t>({
    0x40, 0x55, 0x56, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8D, 0x6C, 0x24, 0xD1,
    0x48, 0x81, 0xEC, 0x88, 0x00, 0x00, 0x00, 0x4C, 0x8B, 0x61, 0x18, 0x45, 0x33, 0xFF, 0x4C, 0x89,
    0x7D, 0x0F, 0x48, 0x8B, 0xF1, 0x48, 0x8B, 0x49, 0x08, 0x4C, 0x8B, 0xF2, 0x48, 0x89, 0x4D, 0xE7,
    0x48, 0x89, 0x55, 0xEF, 0x41, 0x8B, 0x44, 0x24, 0x04, 0x89, 0x45, 0xF7, 0x41, 0x8B, 0x44, 0x24,
    0x08, 0x89, 0x45, 0xFB, 0x41, 0x8B, 0x44, 0x24, 0x10, 0x89, 0x45, 0xFF, 0xC7, 0x45, 0x03, 0xFF,
    0xFF, 0xFF, 0xFF, 0x44, 0x89, 0x7D, 0x07, 0x41, 0x8B, 0x04, 0x24, 0x89, 0x45, 0x0B, 0x49, 0x8B,
    0x84, 0x24, 0x98, 0x00, 0x00, 0x00, 0x4C, 0x89, 0x65, 0x7F, 0x48, 0x85, 0xC0, 0x75, 0x11, 0x49,
    0x3B, 0xCE, 0x48, 0x8D, 0x15, 0x07, 0x4A, 0x00, 0x00, 0x41, 0x8B, 0xC7, 0x48, 0x0F, 0x44, 0xC2,
    0x48, 0x8D, 0x4D, 0xE7, 0x48, 0x89, 0x45, 0x0F, 0xE8, 0xA3, 0x25, 0xED, 0xFF, 0x4C, 0x8B, 0xE8,
    0x48, 0x85, 0xC0, 0x75, 0x12, 0x48, 0x81, 0xC4, 0x88, 0x00, 0x00, 0x00, 0x41, 0x5F, 0x41, 0x5E,
    0x41, 0x5D, 0x41, 0x5C, 0x5E, 0x5D, 0xC3, 0x45, 0x8B, 0x44, 0x24, 0x0C, 0x49, 0x8B, 0xD5, 0x4D,
    0x89, 0xAC, 0x24, 0x88, 0x00, 0x00, 0x00,
});

// 0x561AC0 .. 0x561CE2: the whole slot loop. Proves every register, offset and
// call the redirects and the zero write rely on: both gates, the stat 110
// branch, the resistance rule call, and the stock write the zero write
// mirrors (record lookup, direct test, list read, gfx toggle, cold floor,
// list write, 68 -> 69, the count at [r12+84h]). [rsp+20h] is only ever
// written there (fifth argument of the list write) and [rsp+28h] is never
// used, which is what lets the resistance stub pass r13 and r12 on. It is
// checked in two parts around the stat id check's opcode at 0x561AE8, which
// may be 8E (stock jle) or 8C (jl, ESR's basic-aura-stat-id-0 patch).
constexpr auto AuraLoopHead = std::to_array<std::uint8_t>({
    0x4D, 0x8D, 0x7C, 0x24, 0x14, 0xB9, 0x06, 0x00, 0x00, 0x00, 0x4C, 0x89, 0x7D, 0xD7, 0x48, 0x89,
    0x4D, 0x77, 0x45, 0x8B, 0x7F, 0x38, 0x45, 0x85, 0xFF, 0x0F, 0x84, 0xE9, 0x01, 0x00, 0x00, 0x48,
    0x8B, 0x45, 0xD7, 0x8B, 0x38, 0x85, 0xFF, 0x0F,
});
constexpr auto AuraLoopRest = std::to_array<std::uint8_t>({
    0xDB, 0x01, 0x00, 0x00, 0x48, 0x8B, 0x06, 0x0F, 0xB6, 0x88, 0x06, 0x01, 0x00, 0x00, 0xE8, 0x94,
    0xEF, 0xD9, 0xFF, 0x48, 0x8B, 0x98, 0x60, 0x12, 0x00, 0x00, 0x48, 0x63, 0xC3, 0x48, 0x3B, 0xC3,
    0x75, 0x04, 0x85, 0xDB, 0x79, 0x12, 0x48, 0x8D, 0x4D, 0x67, 0xC6, 0x45, 0x67, 0x00, 0xE8, 0x44,
    0x4E, 0xB2, 0xFF, 0x84, 0xC0, 0x74, 0x01, 0xCC, 0x3B, 0xFB, 0x0F, 0x8D, 0x9B, 0x01, 0x00, 0x00,
    0x48, 0x8B, 0x0E, 0x49, 0x8B, 0xD6, 0x83, 0xFF, 0x6E, 0x75, 0x21, 0x45, 0x8B, 0xC7, 0xE8, 0xA4,
    0x02, 0x00, 0x00, 0x48, 0x8B, 0x4D, 0x77, 0x85, 0xC0, 0x0F, 0x84, 0x80, 0x01, 0x00, 0x00, 0x41,
    0xFF, 0x84, 0x24, 0x84, 0x00, 0x00, 0x00, 0xE9, 0x73, 0x01, 0x00, 0x00, 0x45, 0x8B, 0xCF, 0x44,
    0x8B, 0xC7, 0xE8, 0x50, 0xA4, 0xFF, 0xFF, 0x8B, 0xD8, 0x85, 0xC0, 0x0F, 0x84, 0x5A, 0x01, 0x00,
    0x00, 0x48, 0x8B, 0x0E, 0x8B, 0xD7, 0x0F, 0xB6, 0x89, 0x06, 0x01, 0x00, 0x00, 0xE8, 0xB5, 0x7B,
    0xD7, 0xFF, 0x4C, 0x8B, 0xF8, 0x48, 0x85, 0xC0, 0x0F, 0x84, 0x3D, 0x01, 0x00, 0x00, 0x0F, 0xB6,
    0x48, 0x04, 0x84, 0x0D, 0x4B, 0x7B, 0x83, 0x01, 0x0F, 0x85, 0xA3, 0x00, 0x00, 0x00, 0x48, 0x8B,
    0x06, 0x45, 0x33, 0xC9, 0x44, 0x8B, 0xC7, 0x49, 0x8B, 0xD5, 0x0F, 0xB6, 0x88, 0x06, 0x01, 0x00,
    0x00, 0xE8, 0x41, 0x42, 0xD9, 0xFF, 0x3B, 0xC3, 0x74, 0x11, 0x8B, 0x55, 0x0B, 0x41, 0xB8, 0x01,
    0x00, 0x00, 0x00, 0x49, 0x8B, 0xCE, 0xE8, 0xDC, 0x39, 0xDD, 0xFF, 0x8B, 0xCF, 0x83, 0xE9, 0x43,
    0x74, 0x05, 0x83, 0xF9, 0x01, 0x75, 0x18, 0x49, 0x8B, 0xCE, 0xE8, 0x28, 0xE3, 0xF2, 0xFF, 0x48,
    0x8B, 0xC8, 0x49, 0x8B, 0xD6, 0xE8, 0x5D, 0x83, 0xEE, 0xFF, 0x3B, 0xD8, 0x0F, 0x4C, 0xD8, 0x48,
    0x8B, 0x06, 0x45, 0x33, 0xFF, 0x44, 0x8B, 0xCB, 0x90, 0x44, 0x89, 0x7C, 0x24, 0x20, 0x44, 0x8B,
    0xC7, 0x49, 0x8B, 0xD5, 0x0F, 0xB6, 0x88, 0x06, 0x01, 0x00, 0x00, 0xE8, 0xF7, 0x5F, 0xD9, 0xFF,
    0x83, 0xFF, 0x44, 0x75, 0x1F, 0x48, 0x8B, 0x06, 0x45, 0x8D, 0x47, 0x45, 0x44, 0x8B, 0xCB, 0x90,
    0x44, 0x89, 0x7C, 0x24, 0x20, 0x49, 0x8B, 0xD5, 0x0F, 0xB6, 0x88, 0x06, 0x01, 0x00, 0x00, 0xE8,
    0xD3, 0x5F, 0xD9, 0xFF, 0x41, 0xFF, 0x84, 0x24, 0x84, 0x00, 0x00, 0x00, 0xE9, 0x8A, 0x00, 0x00,
    0x00, 0x45, 0x33, 0xC0, 0x8B, 0xD7, 0x49, 0x8B, 0xCE, 0xE8, 0xD9, 0x33, 0xD9, 0xFF, 0x48, 0x8B,
    0x0E, 0x03, 0xD8, 0x89, 0x45, 0x6F, 0x0F, 0xB6, 0x89, 0x06, 0x01, 0x00, 0x00, 0xE8, 0x35, 0xEE,
    0xD9, 0xFF, 0x4C, 0x8B, 0xA0, 0x60, 0x12, 0x00, 0x00, 0x49, 0x63, 0xCC, 0x49, 0x3B, 0xCC, 0x75,
    0x05, 0x45, 0x85, 0xE4, 0x79, 0x12, 0x48, 0x8D, 0x4D, 0x67, 0xC6, 0x45, 0x67, 0x00, 0xE8, 0xE4,
    0x4C, 0xB2, 0xFF, 0x84, 0xC0, 0x74, 0x01, 0xCC, 0x41, 0x0F, 0xB7, 0x57, 0x2E, 0x41, 0x3B, 0xD4,
    0x7D, 0x12, 0x45, 0x33, 0xC0, 0x49, 0x8B, 0xCE, 0xE8, 0x8A, 0x33, 0xD9, 0xFF, 0x3B, 0xD8, 0x0F,
    0x4C, 0xC3, 0x8B, 0xD8, 0x3B, 0x5D, 0x6F, 0x74, 0x1E, 0x45, 0x33, 0xC9, 0x44, 0x8B, 0xC3, 0x8B,
    0xD7, 0x49, 0x8B, 0xCE, 0xE8, 0x5E, 0x60, 0xD9, 0xFF, 0x4C, 0x8B, 0x65, 0x7F, 0x41, 0xFF, 0x84,
    0x24, 0x84, 0x00, 0x00, 0x00, 0xEB, 0x04, 0x4C, 0x8B, 0x65, 0x7F, 0x48, 0x8B, 0x4D, 0x77, 0x4C,
    0x8B, 0x7D, 0xD7, 0x49, 0x83, 0xC7, 0x04, 0x48, 0x83, 0xE9, 0x01, 0x4C, 0x89, 0x7D, 0xD7, 0x48,
    0x89, 0x4D, 0x77, 0x0F, 0x85, 0xF0, 0xFD, 0xFF, 0xFF,
});
constexpr std::uint8_t StatIdGateStock   = 0x8E;  // jle: ids <= 0 skipped
constexpr std::uint8_t StatIdGateAllowed = 0x8C;  // jl: id 0 (strength) applied
constexpr auto StatIdGateJle = std::to_array<std::uint8_t>({0x0F, 0x8E, 0xDB, 0x01, 0x00, 0x00});  // jle 0x561CC8
constexpr auto StatIdGateJl  = std::to_array<std::uint8_t>({0x0F, 0x8C, 0xDB, 0x01, 0x00, 0x00});  // jl 0x561CC8

// 0x563E21 .. 0x563E6B: srvdofunc 65 zeroes the args block at rbp-80h (ids at
// rbp-6Ch .. rbp-58h), calls 0x34B6C0 with the unit and keeps eax at rbp-74h.
// rdx carries nothing into that call, which is what lets the stub hand the
// args block over in it.
constexpr auto ArgsSetup = std::to_array<std::uint8_t>({
    0x0F, 0x57, 0xC0, 0x89, 0x44, 0x24, 0x68, 0x8B, 0xF0, 0x44, 0x89, 0x65, 0x84, 0x33, 0xC0, 0x89,
    0x5D, 0x88, 0x49, 0x8B, 0xCD, 0x89, 0x45, 0x80, 0x48, 0x89, 0x45, 0x14, 0x89, 0x45, 0x1C, 0x0F,
    0x11, 0x45, 0x94, 0x0F, 0x11, 0x45, 0xA4, 0x0F, 0x11, 0x45, 0xB4, 0x0F, 0x11, 0x45, 0xC4, 0x0F,
    0x11, 0x45, 0xD4, 0x0F, 0x11, 0x45, 0xE4, 0x0F, 0x11, 0x45, 0xF4, 0x0F, 0x11, 0x45, 0x04, 0xE8,
    0x5B, 0x78, 0xDE, 0xFF, 0x45, 0x8B, 0xCC, 0x89, 0x45, 0x8C,
});

// 0x563EB0 .. 0x563F02: the aurastat collection. Ids from skill+74h, stops at
// the first id < 0, stores ids at rbp-6Ch+4*slot and values at rbp-34h+4*slot.
constexpr auto AuraCollect = std::to_array<std::uint8_t>({
    0x4D, 0x8B, 0xFE, 0x48, 0x8D, 0x45, 0x94, 0x49, 0x8D, 0x76, 0x74, 0x4C, 0x2B, 0xF8, 0x44, 0x8B,
    0x74, 0x24, 0x64, 0x48, 0x8B, 0xD9, 0x0F, 0xBF, 0x06, 0x85, 0xC0, 0x78, 0x35, 0x0F, 0xB6, 0x8F,
    0x06, 0x01, 0x00, 0x00, 0x45, 0x8B, 0xCC, 0x89, 0x44, 0x9D, 0x94, 0x49, 0x8B, 0xD5, 0x49, 0x8D,
    0x04, 0x9F, 0x44, 0x89, 0x74, 0x24, 0x20, 0x44, 0x8B, 0x44, 0x05, 0x1C, 0xE8, 0x6F, 0x12, 0xE5,
    0xFF, 0x89, 0x44, 0x9D, 0xCC, 0x48, 0x83, 0xC6, 0x02, 0x48, 0xFF, 0xC3, 0x48, 0x83, 0xFB, 0x06,
    0x7C, 0xC4,
});

// 0x563FC0 .. 0x563FD9: srvdofunc 65 hands 0x5616F0 to the self applier 0x432AE0.
constexpr auto PassiveSelfApply = std::to_array<std::uint8_t>({
    0x4C, 0x8D, 0x4D, 0x80, 0x89, 0x45, 0x80, 0x4C, 0x8D, 0x05, 0x22, 0xD7, 0xFF, 0xFF, 0x49, 0x8B,
    0xD5, 0x48, 0x8B, 0xCF, 0xE8, 0x07, 0xEB, 0xEC, 0xFF,
});

// 0x563FFC .. 0x5640B2: the passivestat loop: the 0 gate at 0x56403A and the list
// write at 0x564091 (context, [rbp+8] caster list, stat, value, 0). The block
// gates in between (0x563FD9 .. 0x563FFC: caster list present, mana against
// cost, passivestate) are not checked: they only decide whether the loop runs,
// which this plugin does not touch, and the mana compare at 0x563FE7 is the
// spot ESR's 2.4 mana gate fix (384502, jle -> jl) changes.
constexpr auto PassiveLoop = std::to_array<std::uint8_t>({
    0x4D, 0x8D, 0xBE, 0xD8, 0x00, 0x00, 0x00, 0x4D, 0x8D, 0xA6, 0xBA, 0x00, 0x00, 0x00, 0x41, 0xBE,
    0x0E, 0x00, 0x00, 0x00, 0x8B, 0x44, 0x24, 0x64, 0x49, 0x8B, 0xD5, 0x44, 0x8B, 0x4C, 0x24, 0x6C,
    0x45, 0x8B, 0x07, 0x0F, 0xB6, 0x8F, 0x06, 0x01, 0x00, 0x00, 0x41, 0x0F, 0xBF, 0x34, 0x24, 0x89,
    0x44, 0x24, 0x20, 0xE8, 0x2C, 0x11, 0xE5, 0xFF, 0x89, 0x44, 0x24, 0x68, 0x85, 0xC0, 0x74, 0x5A,
    0x85, 0xF6, 0x7E, 0x56, 0x0F, 0xB6, 0x8F, 0x06, 0x01, 0x00, 0x00, 0xE8, 0x44, 0xCA, 0xD9, 0xFF,
    0x48, 0x8B, 0x98, 0x60, 0x12, 0x00, 0x00, 0x48, 0x63, 0xCB, 0x48, 0x3B, 0xCB, 0x75, 0x04, 0x85,
    0xDB, 0x79, 0x14, 0x48, 0x8D, 0x4C, 0x24, 0x60, 0xC6, 0x44, 0x24, 0x60, 0x00, 0xE8, 0xF2, 0x28,
    0xB2, 0xFF, 0x84, 0xC0, 0x74, 0x01, 0xCC, 0x3B, 0xF3, 0x7D, 0x1F, 0x44, 0x8B, 0x4C, 0x24, 0x68,
    0x33, 0xC0, 0x48, 0x8B, 0x55, 0x08, 0x44, 0x8B, 0xC6, 0x0F, 0xB6, 0x8F, 0x06, 0x01, 0x00, 0x00,
    0x90, 0x89, 0x44, 0x24, 0x20, 0xE8, 0x6A, 0x3B, 0xD9, 0xFF, 0x49, 0x83, 0xC4, 0x02, 0x49, 0x83,
    0xC7, 0x04, 0x49, 0x83, 0xEE, 0x01, 0x0F, 0x85, 0x68, 0xFF, 0xFF, 0xFF, 0x4C, 0x8B, 0x74, 0x24,
    0x78, 0x44, 0x8B, 0x64, 0x24, 0x6C,
});

constexpr auto ZeroGateStock        = std::to_array<std::uint8_t>({0x0F, 0x84, 0xE9, 0x01, 0x00, 0x00});  // je 0x561CC8
constexpr auto ZeroGateNop          = std::to_array<std::uint8_t>({0x66, 0x0F, 0x1F, 0x44, 0x00, 0x00});  // nop word [rax+rax]
constexpr auto CleanseCall          = std::to_array<std::uint8_t>({0xE8, 0xA4, 0x02, 0x00, 0x00});        // call 0x561DE0
constexpr auto ResistanceCall       = std::to_array<std::uint8_t>({0xE8, 0x50, 0xA4, 0xFF, 0xFF});        // call 0x55BFB0
constexpr auto PassiveZeroGateStock = std::to_array<std::uint8_t>({0x74, 0x5A});                          // je 0x564096
constexpr auto PassiveZeroGateNop   = std::to_array<std::uint8_t>({0x66, 0x90});                          // 2 byte nop
constexpr auto PassiveWriteCall     = std::to_array<std::uint8_t>({0xE8, 0x6A, 0x3B, 0xD9, 0xFF});        // call 0x2F7C00
constexpr auto ArgsCall             = std::to_array<std::uint8_t>({0xE8, 0x5B, 0x78, 0xDE, 0xFF});        // call 0x34B6C0

// Entries of the functions the bridges call. A loader-tracked inline hook at
// one of them is accepted: the call then goes through that hook.
constexpr auto ResistanceRuleEntry = std::to_array<std::uint8_t>({
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x41,
});
constexpr auto CleanseEntry = std::to_array<std::uint8_t>({
    0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83,
});
constexpr auto StatRecordEntry = std::to_array<std::uint8_t>({
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x48,
});
constexpr auto ToggleGfxStateEntry = std::to_array<std::uint8_t>({
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x55, 0x56, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x30, 0x41, 0x8B, 0xE8,
});
constexpr auto GetGameEntry = std::to_array<std::uint8_t>({
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0x48, 0x85, 0xC9, 0x75, 0x20, 0x88, 0x4C,
});
constexpr auto ColdEffectEntry = std::to_array<std::uint8_t>({
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0x48, 0x8B, 0xFA,
});
// D2RLoader 1.3.x turns both stat list routines into jmp [rip+disp32] thunks
// into D2RCore; only the opcode is fixed, the displacement depends on the
// loader build.
constexpr auto LoaderThunkPrefix = std::to_array<std::uint8_t>({0xFF, 0x25});
constexpr auto CastFrameEntry = std::to_array<std::uint8_t>({
    0xB8, 0xFF, 0xFF, 0xFF, 0xFF, 0x48, 0x85, 0xC9, 0x74, 0x06, 0x8B, 0x81, 0x2C, 0x01, 0x00, 0x00, 0xC3,
});
constexpr auto BitMasksWitness = std::to_array<std::uint8_t>({
    0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00,
});

template <std::size_t WindowSize, std::size_t Size>
constexpr auto WindowHolds(const std::array<std::uint8_t, WindowSize>& window, std::uint64_t offset,
                           const std::array<std::uint8_t, Size>& bytes) noexcept -> bool {
    if (offset + Size > WindowSize) {
        return false;
    }
    for (std::size_t index = 0; index < Size; ++index) {
        if (window[static_cast<std::size_t>(offset) + index] != bytes[index]) {
            return false;
        }
    }
    return true;
}

constexpr auto CallTarget(std::uint64_t rva, const std::array<std::uint8_t, 5>& call) noexcept -> std::uint64_t {
    const std::uint32_t raw = static_cast<std::uint32_t>(call[1]) | (static_cast<std::uint32_t>(call[2]) << 8)
                            | (static_cast<std::uint32_t>(call[3]) << 16) | (static_cast<std::uint32_t>(call[4]) << 24);
    const auto rel = static_cast<std::int64_t>(static_cast<std::int32_t>(raw));
    return static_cast<std::uint64_t>(static_cast<std::int64_t>(rva) + 5 + rel);
}

static_assert(AuraLoopHeadRva + std::tuple_size_v<decltype(AuraLoopHead)> == StatIdGateOpcodeRva);
static_assert(StatIdGateOpcodeRva + 1 == AuraLoopRestRva);
static_assert(StatIdGateRva + 1 == StatIdGateOpcodeRva);
static_assert(AuraLoopHead[StatIdGateRva - AuraLoopHeadRva] == StatIdGateJle[0]);
static_assert(StatIdGateJle[1] == StatIdGateStock && StatIdGateJl[1] == StatIdGateAllowed);
static_assert(AuraLoopRest[0] == StatIdGateJle[2] && AuraLoopRest[1] == StatIdGateJle[3]
              && AuraLoopRest[2] == StatIdGateJle[4] && AuraLoopRest[3] == StatIdGateJle[5]);
static_assert(WindowHolds(AuraLoopHead, ZeroGateRva - AuraLoopHeadRva, ZeroGateStock));
static_assert(WindowHolds(AuraLoopRest, CleanseCallRva - AuraLoopRestRva, CleanseCall));
static_assert(WindowHolds(AuraLoopRest, ResistanceCallRva - AuraLoopRestRva, ResistanceCall));
static_assert(WindowHolds(ArgsSetup, ArgsCallRva - ArgsSetupRva, ArgsCall));
static_assert(CallTarget(ArgsCallRva, ArgsCall) == CastFrameRva);
static_assert(WindowHolds(PassiveLoop, PassiveZeroGateRva - PassiveLoopRva, PassiveZeroGateStock));
static_assert(WindowHolds(PassiveLoop, PassiveWriteCallRva - PassiveLoopRva, PassiveWriteCall));
static_assert(CallTarget(CleanseCallRva, CleanseCall) == CleanseRva);
static_assert(CallTarget(ResistanceCallRva, ResistanceCall) == ResistanceRuleRva);
static_assert(CallTarget(PassiveWriteCallRva, PassiveWriteCall) == WriteListStatRva);
static_assert(ZeroGateNop.size() == ZeroGateStock.size() && PassiveZeroGateNop.size() == PassiveZeroGateStock.size());
static_assert(BitMasksWitness[12] == DirectFlagMask);

// ---------------------------------------------------------------------------
//  Stub page. One page the plugin allocates within rel32 reach of the image.
//
//      +0x000  UNWIND_INFO, no frame (no stub touches rsp)
//      +0x010  RUNTIME_FUNCTION table, 4 entries
//      +0x100  mov rax,<bridge> / jmp rax                     cleanse
//      +0x120  mov [rsp+28h],r13 / mov [rsp+30h],r12 /
//              mov rax,<bridge> / jmp rax                     resistance rule
//      +0x140  mov rax,<bridge> / jmp rax                     passive list write
//      +0x160  lea rdx,[rbp-80h] /
//              mov rax,<bridge> / jmp rax                     srvdofunc 65 args
//
//  A stub is reached by the retargeted call, so the return address is on the
//  stack and the stock arguments are in place: the bridge is entered as if
//  the game had called it. The resistance stub also stores the state list
//  (r13) and the args (r12) into the fifth and sixth argument slots. Those are
//  the caller's [rsp+20h] (only ever written by the caller, right before each
//  five-argument call) and [rsp+28h] (never used by the caller). The args stub
//  passes srvdofunc 65's args block (rbp-80h, its frame) as the second
//  argument of a call that takes one.
// ---------------------------------------------------------------------------
constexpr std::size_t PageBytes           = 0x1000;
constexpr std::size_t UnwindOffset        = 0x000;
constexpr std::size_t FunctionTableOffset = 0x010;
constexpr std::size_t StubCount           = 4;
enum StubIndex : std::size_t {
    StubCleanse      = 0,
    StubResistance   = 1,
    StubPassiveWrite = 2,
    StubArgs         = 3,
};
constexpr std::array<std::size_t, StubCount> StubOffsets{0x100, 0x120, 0x140, 0x160};
constexpr std::array<std::size_t, StubCount> StubSizes{12, 22, 12, 16};
constexpr auto ResistanceStubPrefix = std::to_array<std::uint8_t>({
    0x4C, 0x89, 0x6C, 0x24, 0x28,  // mov [rsp+28h], r13
    0x4C, 0x89, 0x64, 0x24, 0x30,  // mov [rsp+30h], r12
});
static_assert(ResistanceStubPrefix.size() + 12 == 22);
constexpr auto ArgsStubPrefix = std::to_array<std::uint8_t>({
    0x48, 0x8D, 0x55, 0x80,  // lea rdx, [rbp-80h]
});
static_assert(ArgsStubPrefix.size() + 12 == 16);

// ---------------------------------------------------------------------------
//  Native signatures
// ---------------------------------------------------------------------------
using ResistanceRuleFn = std::int32_t(__fastcall*)(void* game, void* unit, std::uint32_t statId, std::int32_t value) noexcept;
using CleanseFn        = std::int32_t(__fastcall*)(void* game, void* unit, std::int32_t percent) noexcept;
using StatRecordFn     = const std::uint8_t*(__fastcall*)(std::uint32_t dataContext, std::int32_t statId) noexcept;
using ReadListStatFn   = std::int32_t(__fastcall*)(std::uint32_t dataContext, void* statList, std::int32_t statId,
                                                   std::int32_t layer) noexcept;
using WriteListStatFn  = void(__fastcall*)(std::uint32_t dataContext, void* statList, std::int32_t statId,
                                           std::int32_t value, std::uint32_t layer) noexcept;
using ToggleGfxStateFn = void(__fastcall*)(void* unit, std::int32_t state, std::int32_t enable) noexcept;
using GetGameFn        = void*(__fastcall*)(void* unit) noexcept;
using ColdEffectFn     = std::int32_t(__fastcall*)(void* game, void* unit) noexcept;
using CastFrameFn      = std::uint32_t(__fastcall*)(void* unit) noexcept;

// ---------------------------------------------------------------------------
//  State
// ---------------------------------------------------------------------------
struct Settings {
    bool enabled{true};
    bool auraStats{true};
    bool strengthAuraStats{true};
    bool passiveStats{true};
    bool diagnostics{false};
};

struct InstallState {
    bool resistanceRedirect{false};
    bool cleanseRedirect{false};
    bool zeroGate{false};
    bool passiveWriteRedirect{false};
    bool passiveZeroGate{false};
    bool argsRedirect{false};
    bool statIdZeroAllowed{false};
    bool statIdZeroOwned{false};
};

const D2RL::PluginContext* Context{};
std::uintptr_t             Base{};
Settings                   Config{};
InstallState               Installed{};
char                       ConfigSource[48]{"built-in defaults"};
std::atomic<bool>          AuraActive{false};
std::atomic<bool>          PassiveActive{false};

ResistanceRuleFn ResistanceRule{};
CleanseFn        Cleanse{};
StatRecordFn     GetStatRecord{};
ReadListStatFn   ReadListStat{};
WriteListStatFn  WriteListStat{};
ToggleGfxStateFn ToggleGfxState{};
GetGameFn        GetGame{};
ColdEffectFn     ColdEffect{};
CastFrameFn      CastFrame{};

std::uint8_t* Page{};
bool          PageUnwindRegistered{};

struct CallSitePatch {
    std::uint64_t               rva;
    const std::uint8_t*         stock;
    std::size_t                 stub;
    std::array<std::uint8_t, 5> written;
    bool                        applied;
};
std::array<CallSitePatch, StubCount> CallSites{{
    {CleanseCallRva, CleanseCall.data(), StubCleanse, {}, false},
    {ResistanceCallRva, ResistanceCall.data(), StubResistance, {}, false},
    {PassiveWriteCallRva, PassiveWriteCall.data(), StubPassiveWrite, {}, false},
    {ArgsCallRva, ArgsCall.data(), StubArgs, {}, false},
}};

struct GatePatch {
    std::uint64_t       rva;
    const std::uint8_t* stock;
    const std::uint8_t* nop;
    std::uint32_t       size;
    bool                applied;
};
GatePatch ZeroGate{ZeroGateRva, ZeroGateStock.data(), ZeroGateNop.data(),
                   static_cast<std::uint32_t>(ZeroGateStock.size()), false};
GatePatch PassiveZeroGate{PassiveZeroGateRva, PassiveZeroGateStock.data(), PassiveZeroGateNop.data(),
                          static_cast<std::uint32_t>(PassiveZeroGateStock.size()), false};
GatePatch StatIdGate{StatIdGateRva, StatIdGateJle.data(), StatIdGateJl.data(),
                     static_cast<std::uint32_t>(StatIdGateJle.size()), false};

std::atomic<std::uint64_t> AuraZeroResults{};
std::atomic<std::uint64_t> AuraStatsCleared{};
std::atomic<std::uint64_t> AuraDirectLeft{};
std::atomic<std::uint64_t> CleanseZerosKept{};
std::atomic<std::uint64_t> PassiveZeroResults{};
std::atomic<std::uint64_t> PassiveStatsCleared{};
std::atomic<std::uint64_t> ReadBackMismatches{};
std::atomic<bool>          MismatchReported{false};
char                       LastFailure[320]{};
char                       AuraReason[320]{"off in the config"};
char                       PassiveReason[320]{"off in the config"};
char                       StrengthReason[320]{"off in the config"};
std::atomic<std::uint32_t> DiagnosticsBudget{};

template <typename T>
auto At(std::uint64_t rva) noexcept -> T {
    return reinterpret_cast<T>(Base + static_cast<std::uintptr_t>(rva));
}

auto ReadU8(const void* base, std::size_t offset) noexcept -> std::uint8_t {
    return static_cast<const std::uint8_t*>(base)[offset];
}

auto ReadU32(const void* base, std::size_t offset) noexcept -> std::uint32_t {
    std::uint32_t value = 0;
    std::memcpy(&value, static_cast<const std::uint8_t*>(base) + offset, sizeof(value));
    return value;
}

auto ReadI32(const void* base, std::size_t offset) noexcept -> std::int32_t {
    std::int32_t value = 0;
    std::memcpy(&value, static_cast<const std::uint8_t*>(base) + offset, sizeof(value));
    return value;
}

auto PutU32(std::uint8_t* at, std::uint32_t value) noexcept -> void {
    std::memcpy(at, &value, sizeof(value));
}

auto PutU64(std::uint8_t* at, std::uint64_t value) noexcept -> void {
    std::memcpy(at, &value, sizeof(value));
}

auto UnitIdOf(const void* unit) noexcept -> std::uint32_t {
    return unit != nullptr ? ReadU32(unit, UnitIdOffset) : 0xFFFFFFFFU;
}

// ---------------------------------------------------------------------------
//  Logging
// ---------------------------------------------------------------------------
void LogInfo(const char* format, ...) noexcept {
    char message[512]{};
    va_list args;
    va_start(args, format);
    std::vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    Context->LogInfo(message);
}

void LogWarn(const char* format, ...) noexcept {
    char message[512]{};
    va_list args;
    va_start(args, format);
    std::vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    Context->LogWarn(message);
}

void LogError(const char* format, ...) noexcept {
    char message[512]{};
    va_list args;
    va_start(args, format);
    std::vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    Context->LogError(message);
}

auto TakeDiagnosticsLine() noexcept -> bool {
    if (!Config.diagnostics) {
        return false;
    }
    auto budget = DiagnosticsBudget.load(std::memory_order_relaxed);
    while (budget != 0) {
        if (DiagnosticsBudget.compare_exchange_weak(budget, budget - 1, std::memory_order_relaxed)) {
            return true;
        }
    }
    return false;
}

// A 0 that did not stick means D2RCore's list write treats it differently from
// the stock routine. Said once, whatever diagnostics is set to; the console
// command keeps the count.
void ReportMismatch(const char* part, std::int32_t statId, std::int32_t wanted, std::int32_t stored) noexcept {
    ReadBackMismatches.fetch_add(1, std::memory_order_relaxed);
    bool expected = false;
    if (MismatchReported.compare_exchange_strong(expected, true, std::memory_order_relaxed)) {
        LogWarn("AuraZeroResults: %s stat %d was written as %d but reads back %d. Further mismatches are only "
                "counted (console command aurazeroresults).",
                part, statId, wanted, stored);
    }
}

// Why a part is off: logged, and kept for the console command.
void NoteFailure(const char* format, ...) noexcept {
    va_list args;
    va_start(args, format);
    std::vsnprintf(LastFailure, sizeof(LastFailure), format, args);
    va_end(args);
    LogError("AuraZeroResults: %s", LastFailure);
}

// Who changed a byte, as the loader's diagnostics service sees it.
void DescribeOwner(std::uint64_t rva, const std::uint8_t* expected, char* out, std::size_t outSize) noexcept {
    std::snprintf(out, outSize, "%s", "the loader's diagnostics service is not available");
    const D2RL::DiagnosticsService* diagnostics = nullptr;
    if (Context->QueryService(&diagnostics) != D2RL::ServiceQueryResult::Success
        || !D2RL::HasDiagnosticsServiceField(diagnostics, D2RL::DiagnosticsServiceRequiredSize)
        || diagnostics->queryHookStatus == nullptr) {
        return;
    }
    D2RL::Diagnostics::HookQuery query{
        .structSize   = D2RL::Diagnostics::HookQuerySize,
        .flags        = 0,
        .rva          = rva,
        .expected     = expected,
        .expectedSize = 1,
        .reserved     = 0,
    };
    D2RL::Diagnostics::HookStatus status{
        .structSize = D2RL::Diagnostics::HookStatusSize,
    };
    if (diagnostics->queryHookStatus(Context, &query, &status) != D2RL::Diagnostics::Result::Success) {
        std::snprintf(out, outSize, "%s", "the loader's diagnostics service gave no answer");
        return;
    }
    const char* kind = status.kind == D2RL::Diagnostics::ModificationKind::BytePatch    ? "byte patch"
                     : status.kind == D2RL::Diagnostics::ModificationKind::InlineHook   ? "inline hook"
                     : status.kind == D2RL::Diagnostics::ModificationKind::Multiple     ? "several changes"
                                                                                         : "change";
    if (status.state == D2RL::Diagnostics::ModificationState::Tracked) {
        if (status.ownerPluginId[0] != '\0') {
            std::snprintf(out, outSize, "%s from plugin %.63s", kind, status.ownerPluginId);
        } else {
            std::snprintf(out, outSize, "%s from %u plugins", kind, status.ownerCount);
        }
    } else if (status.state == D2RL::Diagnostics::ModificationState::Untracked) {
        std::snprintf(out, outSize, "%s",
                      "changed with no plugin owner: a patches-folder memory patch, the loader itself, or a different "
                      "game or loader build");
    } else {
        std::snprintf(out, outSize, "%s", "the loader reports these bytes unchanged");
    }
}

// The first byte that differs from the witness, and who changed it.
void NoteMismatch(std::uint64_t rva, const std::uint8_t* bytes, std::size_t size, const char* name) noexcept {
    const auto* live = reinterpret_cast<const std::uint8_t*>(Base + static_cast<std::uintptr_t>(rva));
    std::size_t at = 0;
    while (at < size && live[at] == bytes[at]) {
        ++at;
    }
    if (at == size) {
        NoteFailure("%s at RVA 0x%llX: the loader refused the byte check although the bytes match.", name,
                    static_cast<unsigned long long>(rva));
        return;
    }
    char owner[200]{};
    DescribeOwner(rva + at, bytes + at, owner, sizeof(owner));
    NoteFailure("%s: RVA 0x%llX holds %02X where %02X was expected (%s).", name,
                static_cast<unsigned long long>(rva + at), live[at], bytes[at], owner);
}

// ---------------------------------------------------------------------------
//  Configuration
// ---------------------------------------------------------------------------
auto Trim(std::string_view value) noexcept -> std::string_view {
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t' || value.front() == '\r')) {
        value.remove_prefix(1);
    }
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t' || value.back() == '\r')) {
        value.remove_suffix(1);
    }
    return value;
}

void ParseConfig(std::string_view text) noexcept {
    std::size_t lineNumber = 0;
    for (std::size_t start = 0; start <= text.size();) {
        ++lineNumber;
        const auto end = text.find('\n', start);
        auto line = text.substr(start, end == std::string_view::npos ? text.size() - start : end - start);
        start = end == std::string_view::npos ? text.size() + 1 : end + 1;

        const auto comment = line.find('#');
        if (comment != std::string_view::npos) {
            line = line.substr(0, comment);
        }
        line = Trim(line);
        if (line.empty()) {
            continue;
        }

        const auto equals = line.find('=');
        if (equals == std::string_view::npos) {
            LogWarn("AuraZeroResults: config line %zu ignored, expected key = value.", lineNumber);
            continue;
        }
        const auto key   = Trim(line.substr(0, equals));
        const auto value = Trim(line.substr(equals + 1));

        bool* target = nullptr;
        if (key == "enabled") {
            target = &Config.enabled;
        } else if (key == "aura_stats") {
            target = &Config.auraStats;
        } else if (key == "strength_aura_stats") {
            target = &Config.strengthAuraStats;
        } else if (key == "passive_stats") {
            target = &Config.passiveStats;
        } else if (key == "diagnostics") {
            target = &Config.diagnostics;
        } else {
            LogWarn("AuraZeroResults: config line %zu has an unknown key.", lineNumber);
            continue;
        }

        if (value == "true") {
            *target = true;
        } else if (value == "false") {
            *target = false;
        } else {
            LogWarn("AuraZeroResults: config line %zu ignored, the value must be true or false.", lineNumber);
        }
    }
}

void LoadConfig() noexcept {
    Config = Settings{};
    std::snprintf(ConfigSource, sizeof(ConfigSource), "%s", "built-in defaults");
    if (!Context->EnsureConfig(DefaultConfigToml)) {
        LogWarn("AuraZeroResults: could not create the default config, using built-in defaults.");
    }

    std::string buffer(16384, '\0');
    std::uint32_t required = 0;
    if (!Context->ReadConfig(buffer.data(), static_cast<std::uint32_t>(buffer.size()), &required)) {
        if (required <= buffer.size()) {
            LogWarn("AuraZeroResults: could not read the config, using built-in defaults.");
            return;
        }
        buffer.assign(required, '\0');
        if (!Context->ReadConfig(buffer.data(), static_cast<std::uint32_t>(buffer.size()), &required)) {
            LogWarn("AuraZeroResults: could not read the config, using built-in defaults.");
            return;
        }
    }
    ParseConfig(std::string_view(buffer.c_str()));
    std::snprintf(ConfigSource, sizeof(ConfigSource), "%s", "config file");
}

// ---------------------------------------------------------------------------
//  Image checks
// ---------------------------------------------------------------------------
auto CheckExact(std::uint64_t rva, const std::uint8_t* bytes, std::size_t size, const char* name) noexcept -> bool {
    if (Context->CheckExpectedBytes(rva, bytes, static_cast<std::uint32_t>(size))) {
        return true;
    }
    NoteMismatch(rva, bytes, size, name);
    return false;
}

// A function the plugin only calls may already carry another plugin's inline
// hook at its entry. The loader tracks those, and calling through one is fine.
auto CheckCallable(std::uint64_t rva, const std::uint8_t* bytes, std::size_t size, const char* name) noexcept -> bool {
    if (Context->CheckExpectedBytes(rva, bytes, static_cast<std::uint32_t>(size))) {
        return true;
    }

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
        D2RL::Diagnostics::HookStatus status{
            .structSize = D2RL::Diagnostics::HookStatusSize,
        };
        if (diagnostics->queryHookStatus(Context, &query, &status) == D2RL::Diagnostics::Result::Success
            && status.state == D2RL::Diagnostics::ModificationState::Tracked
            && status.kind == D2RL::Diagnostics::ModificationKind::InlineHook) {
            LogInfo("AuraZeroResults: %s at RVA 0x%llX is hooked by another plugin, calling through it.", name,
                    static_cast<unsigned long long>(rva));
            return true;
        }
    }

    NoteMismatch(rva, bytes, size, name);
    return false;
}

auto ValidateStatHelpers() noexcept -> bool {
    return CheckCallable(ReadListStatRva, LoaderThunkPrefix.data(), LoaderThunkPrefix.size(), "stat list read thunk")
        && CheckCallable(WriteListStatRva, LoaderThunkPrefix.data(), LoaderThunkPrefix.size(), "stat list write thunk");
}

// The stat id check's opcode: stock jle, or jl from ESR's basic-aura-stat-id-0
// patch. Anything else is refused.
auto CheckStatIdGate() noexcept -> bool {
    if (Context->CheckExpectedBytes(StatIdGateOpcodeRva, &StatIdGateStock, 1)) {
        Installed.statIdZeroAllowed = false;
        return true;
    }
    if (Context->CheckExpectedBytes(StatIdGateOpcodeRva, &StatIdGateAllowed, 1)) {
        Installed.statIdZeroAllowed = true;
        return true;
    }
    NoteMismatch(StatIdGateOpcodeRva, &StatIdGateStock, 1, "aura callback stat id check");
    return false;
}

auto ValidateAura() noexcept -> bool {
    return CheckExact(AuraCallbackRva, AuraCallbackEntry.data(), AuraCallbackEntry.size(), "aura callback entry")
        && CheckExact(AuraLoopHeadRva, AuraLoopHead.data(), AuraLoopHead.size(), "aura callback slot loop")
        && CheckStatIdGate()
        && CheckExact(AuraLoopRestRva, AuraLoopRest.data(), AuraLoopRest.size(), "aura callback slot loop")
        && CheckExact(ArgsSetupRva, ArgsSetup.data(), ArgsSetup.size(), "srvdofunc 65 args setup")
        && CheckExact(AuraCollectRva, AuraCollect.data(), AuraCollect.size(), "srvdofunc 65 aurastat collection")
        && CheckCallable(CastFrameRva, CastFrameEntry.data(), CastFrameEntry.size(), "unit cast frame getter")
        && CheckCallable(ResistanceRuleRva, ResistanceRuleEntry.data(), ResistanceRuleEntry.size(), "resistance rule")
        && CheckCallable(CleanseRva, CleanseEntry.data(), CleanseEntry.size(), "cleanse helper")
        && CheckCallable(StatRecordRva, StatRecordEntry.data(), StatRecordEntry.size(), "itemstatcost record lookup")
        && CheckCallable(ToggleGfxStateRva, ToggleGfxStateEntry.data(), ToggleGfxStateEntry.size(),
                         "state gfx toggle")
        && CheckCallable(GetGameRva, GetGameEntry.data(), GetGameEntry.size(), "unit game getter")
        && CheckCallable(ColdEffectRva, ColdEffectEntry.data(), ColdEffectEntry.size(), "cold effect")
        && CheckExact(BitMasksRva, BitMasksWitness.data(), BitMasksWitness.size(), "bit table")
        && ValidateStatHelpers();
}

auto ValidatePassive() noexcept -> bool {
    if (!CheckExact(PassiveSelfApplyRva, PassiveSelfApply.data(), PassiveSelfApply.size(), "srvdofunc 65 self apply")
        || !CheckExact(PassiveLoopRva, PassiveLoop.data(), PassiveLoop.size(), "srvdofunc 65 passive stat loop")
        || !ValidateStatHelpers()) {
        return false;
    }
    std::uintptr_t handler = 0;
    std::memcpy(&handler,
                reinterpret_cast<const void*>(Base + static_cast<std::uintptr_t>(SrvDoTableRva)
                                              + BasicAuraSrvDoSlot * sizeof(std::uintptr_t)),
                sizeof(handler));
    if (handler != Base + static_cast<std::uintptr_t>(BasicAuraHandlerRva)) {
        NoteFailure("srvdofunc 65 no longer points at the stock handler (RVA 0x%llX), something replaced it.",
                    static_cast<unsigned long long>(BasicAuraHandlerRva));
        return false;
    }
    return true;
}

void ResolveNativeFunctions() noexcept {
    ResistanceRule = At<ResistanceRuleFn>(ResistanceRuleRva);
    Cleanse        = At<CleanseFn>(CleanseRva);
    GetStatRecord  = At<StatRecordFn>(StatRecordRva);
    ReadListStat   = At<ReadListStatFn>(ReadListStatRva);
    WriteListStat  = At<WriteListStatFn>(WriteListStatRva);
    ToggleGfxState = At<ToggleGfxStateFn>(ToggleGfxStateRva);
    GetGame        = At<GetGameFn>(GetGameRva);
    ColdEffect     = At<ColdEffectFn>(ColdEffectRva);
    CastFrame      = At<CastFrameFn>(CastFrameRva);
}

// ---------------------------------------------------------------------------
//  The zero write
// ---------------------------------------------------------------------------
// One slot whose result is 0, given the same way the stock write at 0x561B6A
// gives a nonzero one, without the count at [r12+84h]:
//   - direct stats are left alone: the stock write adds them to the unit, and
//     adding 0 changes nothing.
//   - the gfx bit is toggled when the list held a value (stock: when the list
//     value differs from the result, which is 0 here).
//   - 67/68 take the target's cold effect when it is above 0 (stock floor:
//     the result never goes below the cold effect), and 68 is copied into 69.
//   - nothing is written when the list already holds that value.
void ApplyAuraZero(void* game, void* target, std::int32_t statId, void* statList, const std::uint8_t* args) noexcept {
    AuraZeroResults.fetch_add(1, std::memory_order_relaxed);
    if (game == nullptr || target == nullptr || statList == nullptr || args == nullptr) {
        return;
    }
    const auto  context = static_cast<std::uint32_t>(ReadU8(game, GameDataContextOffset));
    const auto* record  = GetStatRecord(context, statId);
    if (record == nullptr) {
        return;
    }
    if ((ReadU8(record, StatRecordFlagsOffset) & DirectFlagMask) != 0) {
        AuraDirectLeft.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    const auto previous = ReadListStat(context, statList, statId, 0);
    std::int32_t value = 0;
    if (statId == VelocityPercentStat || statId == AttackRateStat) {
        const auto cold = ColdEffect(GetGame(target), target);
        if (cold > 0) {
            value = cold;
        }
    }
    const auto state = ReadI32(args, ArgsStateOffset);
    if (previous != 0) {
        ToggleGfxState(target, state, 1);
    }
    if (previous == value) {
        return;
    }

    WriteListStat(context, statList, statId, value, 0);
    if (statId == AttackRateStat) {
        WriteListStat(context, statList, OtherAnimRateStat, value, 0);
    }
    const auto stored = ReadListStat(context, statList, statId, 0);
    if (stored != value) {
        ReportMismatch("aura", statId, value, stored);
        return;
    }
    AuraStatsCleared.fetch_add(1, std::memory_order_relaxed);
    if (TakeDiagnosticsLine()) {
        LogInfo("AuraZeroResults: stat %d on state %d of unit %u went from %d to %d.", statId, state,
                UnitIdOf(target), previous, value);
    }
}

// ---------------------------------------------------------------------------
//  Bridges
// ---------------------------------------------------------------------------
// 0x561B37. Before the gate nop no 0 reached the cleanse helper, so returning
// 0 for it, without cleansing, is the stock behavior in every state the
// plugin can be in.
auto BridgeCleanse(void* game, void* target, std::int32_t percent) noexcept -> std::int32_t {
    if (percent == 0) {
        CleanseZerosKept.fetch_add(1, std::memory_order_relaxed);
        return 0;
    }
    return Cleanse(game, target, percent);
}

// 0x561B5B. rcx = game, rdx = target, r8d = stat id, r9d = result; the stub
// adds r13 = the state's stat list and r12 = the args block.
auto BridgeResistanceRule(void* game, void* target, std::int32_t statId, std::int32_t value, void* statList,
                          const std::uint8_t* args) noexcept -> std::int32_t {
    const auto result = ResistanceRule(game, target, static_cast<std::uint32_t>(statId), value);
    if (result == 0 && AuraActive.load(std::memory_order_relaxed)) {
        ApplyAuraZero(game, target, statId, statList, args);
    }
    return result;
}

// 0x563E60, srvdofunc 65. rcx = the unit (the stock call's only argument), rdx
// = the args block the stub adds. The six aurastat ids get -1 before the
// collection fills the slots it reaches; the stock call's result goes back for
// the store at 0x563E68. This runs whatever the gate state is: -1 is skipped
// by the stat id check in both its stock and its stat-id-0 form.
auto BridgeAuraArgs(void* unit, std::uint8_t* args) noexcept -> std::uint32_t {
    for (std::size_t slot = 0; slot < ArgsAuraSlots; ++slot) {
        PutU32(args + ArgsStatIdsOffset + slot * sizeof(std::uint32_t), 0xFFFFFFFFU);
    }
    return CastFrame(unit);
}

// 0x564091. ecx = data context, rdx = the caster's aura state list, r8d = stat
// id, r9d = result, fifth argument = layer 0.
void BridgePassiveWrite(std::uint32_t dataContext, void* statList, std::int32_t statId, std::int32_t value,
                        std::uint32_t layer) noexcept {
    if (value != 0) {
        WriteListStat(dataContext, statList, statId, value, layer);
        return;
    }
    if (!PassiveActive.load(std::memory_order_relaxed) || statList == nullptr) {
        return;
    }
    PassiveZeroResults.fetch_add(1, std::memory_order_relaxed);
    const auto previous = ReadListStat(dataContext, statList, statId, static_cast<std::int32_t>(layer));
    if (previous == 0) {
        return;
    }
    WriteListStat(dataContext, statList, statId, 0, layer);
    const auto stored = ReadListStat(dataContext, statList, statId, static_cast<std::int32_t>(layer));
    if (stored != 0) {
        ReportMismatch("passive", statId, 0, stored);
        return;
    }
    PassiveStatsCleared.fetch_add(1, std::memory_order_relaxed);
    if (TakeDiagnosticsLine()) {
        LogInfo("AuraZeroResults: passive stat %d on the caster's aura state went from %d to 0.", statId, previous);
    }
}

// ---------------------------------------------------------------------------
//  Stub page and patches
// ---------------------------------------------------------------------------
auto AllocatePage() noexcept -> std::uint8_t* {
    const auto* image     = reinterpret_cast<const std::uint8_t*>(Base);
    const auto  ntOffset  = static_cast<std::size_t>(ReadU32(image, 0x3C));
    const auto  imageSize = ReadU32(image, ntOffset + 0x50);

    SYSTEM_INFO systemInfo{};
    GetSystemInfo(&systemInfo);
    const std::uintptr_t granularity =
        systemInfo.dwAllocationGranularity != 0 ? systemInfo.dwAllocationGranularity : 0x10000;
    const auto alignUp = [granularity](std::uintptr_t value) noexcept {
        return (value + granularity - 1) & ~(granularity - 1);
    };

    // The page sits above the image, so the lowest call site is the farthest.
    const std::uintptr_t limit = Base + static_cast<std::uintptr_t>(CleanseCallRva) + 0x7FF00000ULL;
    std::uintptr_t address = alignUp(Base + imageSize);
    while (address + PageBytes < limit) {
        MEMORY_BASIC_INFORMATION info{};
        if (VirtualQuery(reinterpret_cast<LPCVOID>(address), &info, sizeof(info)) == 0) {
            break;
        }
        const auto regionEnd = reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize;
        if (info.State == MEM_FREE && address + PageBytes <= regionEnd) {
            if (auto* page = VirtualAlloc(reinterpret_cast<LPVOID>(address), PageBytes, MEM_RESERVE | MEM_COMMIT,
                                          PAGE_READWRITE)) {
                return static_cast<std::uint8_t*>(page);
            }
        }
        const auto next = alignUp(regionEnd);
        if (next <= address) {
            break;
        }
        address = next;
    }
    return nullptr;
}

void EmitPage(std::uint8_t* page) noexcept {
    const std::uint8_t noFrameUnwind[4]{0x01, 0x00, 0x00, 0x00};
    std::memcpy(page + UnwindOffset, noFrameUnwind, sizeof(noFrameUnwind));

    const std::array<std::uint64_t, StubCount> bridges{
        static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(&BridgeCleanse)),
        static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(&BridgeResistanceRule)),
        static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(&BridgePassiveWrite)),
        static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(&BridgeAuraArgs)),
    };

    for (std::size_t index = 0; index < StubCount; ++index) {
        auto* stub = page + StubOffsets[index];
        std::size_t at = 0;
        if (index == StubResistance) {
            std::memcpy(stub, ResistanceStubPrefix.data(), ResistanceStubPrefix.size());
            at = ResistanceStubPrefix.size();
        } else if (index == StubArgs) {
            std::memcpy(stub, ArgsStubPrefix.data(), ArgsStubPrefix.size());
            at = ArgsStubPrefix.size();
        }
        stub[at]     = 0x48;  // mov rax,imm64
        stub[at + 1] = 0xB8;
        PutU64(stub + at + 2, bridges[index]);
        stub[at + 10] = 0xFF;  // jmp rax
        stub[at + 11] = 0xE0;

        auto* function = page + FunctionTableOffset + index * 12;
        PutU32(function, static_cast<std::uint32_t>(StubOffsets[index]));
        PutU32(function + 4, static_cast<std::uint32_t>(StubOffsets[index] + StubSizes[index]));
        PutU32(function + 8, static_cast<std::uint32_t>(UnwindOffset));
    }
}

auto EnsurePage() noexcept -> bool {
    if (Page != nullptr) {
        return true;
    }
    auto* page = AllocatePage();
    if (page == nullptr) {
        NoteFailure("no free page within call range of the game image.");
        return false;
    }
    std::memset(page, 0xCC, PageBytes);
    std::memset(page, 0, StubOffsets[0]);
    EmitPage(page);

    DWORD previous = 0;
    if (!VirtualProtect(page, PageBytes, PAGE_EXECUTE_READ, &previous)) {
        NoteFailure("could not make the stub page executable (error %lu).", GetLastError());
        VirtualFree(page, 0, MEM_RELEASE);
        return false;
    }
    FlushInstructionCache(GetCurrentProcess(), page, PageBytes);

    PageUnwindRegistered = RtlAddFunctionTable(reinterpret_cast<PRUNTIME_FUNCTION>(page + FunctionTableOffset),
                                               static_cast<DWORD>(StubCount),
                                               static_cast<DWORD64>(reinterpret_cast<std::uintptr_t>(page)))
                        != FALSE;
    if (!PageUnwindRegistered) {
        LogWarn("AuraZeroResults: unwind data for the stub page was not registered; stack walks stop there.");
    }
    Page = page;
    return true;
}

auto RestoreCallSite(CallSitePatch& site) noexcept -> bool {
    if (!site.applied) {
        return true;
    }
    if (!Context->PatchBytes(site.rva, site.written.data(), 5, site.stock, 5)) {
        LogError("AuraZeroResults: could not restore the call at RVA 0x%llX.", static_cast<unsigned long long>(site.rva));
        return false;
    }
    site.applied = false;
    return true;
}

auto PatchCallSite(CallSitePatch& site) noexcept -> bool {
    const auto from = Base + static_cast<std::uintptr_t>(site.rva) + 5;
    const auto to   = reinterpret_cast<std::uintptr_t>(Page) + StubOffsets[site.stub];
    const auto rel  = static_cast<std::int64_t>(to) - static_cast<std::int64_t>(from);
    if (rel < INT32_MIN || rel > INT32_MAX) {
        NoteFailure("the stub page is out of reach of RVA 0x%llX.", static_cast<unsigned long long>(site.rva));
        return false;
    }
    site.written[0] = 0xE8;
    const auto rel32 = static_cast<std::int32_t>(rel);
    std::memcpy(site.written.data() + 1, &rel32, sizeof(rel32));

    if (!Context->PatchBytes(site.rva, site.stock, 5, site.written.data(), 5)) {
        NoteFailure("the loader refused the call redirect at RVA 0x%llX.", static_cast<unsigned long long>(site.rva));
        return false;
    }
    site.applied = true;
    if (std::memcmp(reinterpret_cast<const std::uint8_t*>(Base + static_cast<std::uintptr_t>(site.rva)),
                    site.written.data(), 5) != 0) {
        NoteFailure("RVA 0x%llX does not hold the bytes that were written.", static_cast<unsigned long long>(site.rva));
        RestoreCallSite(site);
        return false;
    }
    return true;
}

auto RestoreGate(GatePatch& gate) noexcept -> bool {
    if (!gate.applied) {
        return true;
    }
    if (!Context->PatchBytes(gate.rva, gate.nop, gate.size, gate.stock, gate.size)) {
        LogError("AuraZeroResults: could not restore the gate at RVA 0x%llX.", static_cast<unsigned long long>(gate.rva));
        return false;
    }
    gate.applied = false;
    return true;
}

auto PatchGate(GatePatch& gate) noexcept -> bool {
    if (!Context->PatchBytes(gate.rva, gate.stock, gate.size, gate.nop, gate.size)) {
        NoteFailure("the loader refused the gate at RVA 0x%llX.", static_cast<unsigned long long>(gate.rva));
        return false;
    }
    gate.applied = true;
    if (std::memcmp(reinterpret_cast<const std::uint8_t*>(Base + static_cast<std::uintptr_t>(gate.rva)), gate.nop,
                    gate.size) != 0) {
        NoteFailure("RVA 0x%llX does not hold the bytes that were written.", static_cast<unsigned long long>(gate.rva));
        RestoreGate(gate);
        return false;
    }
    return true;
}

// The gate opens last: until then no 0 reaches either redirect, and both
// redirects behave exactly like the calls they replace. The args redirect is
// in before it opens, so no unfilled slot can pass as a strength 0.
void InstallAura() noexcept {
    if (!ValidateAura() || !EnsurePage()) {
        std::snprintf(AuraReason, sizeof(AuraReason), "off, %s", LastFailure);
        LogError("AuraZeroResults: aura_stats is off.");
        return;
    }
    auto& resistanceSite = CallSites[StubResistance];
    auto& cleanseSite    = CallSites[StubCleanse];
    auto& argsSite       = CallSites[StubArgs];

    Installed.resistanceRedirect = PatchCallSite(resistanceSite);
    Installed.cleanseRedirect    = Installed.resistanceRedirect && PatchCallSite(cleanseSite);
    Installed.argsRedirect       = Installed.cleanseRedirect && PatchCallSite(argsSite);
    if (Installed.argsRedirect) {
        AuraActive.store(true, std::memory_order_relaxed);
        Installed.zeroGate = PatchGate(ZeroGate);
    }
    if (!Installed.zeroGate) {
        AuraActive.store(false, std::memory_order_relaxed);
        // With the gate still open the redirects stay: the cleanse bridge keeps
        // a 0 away from the stock helper, unfilled slots keep id -1, and the
        // resistance bridge passes through.
        if (!ZeroGate.applied) {
            RestoreCallSite(argsSite);
            RestoreCallSite(cleanseSite);
            RestoreCallSite(resistanceSite);
            Installed.resistanceRedirect = Installed.cleanseRedirect = Installed.argsRedirect = false;
        }
        std::snprintf(AuraReason, sizeof(AuraReason), "off, %s", LastFailure);
        LogError("AuraZeroResults: aura_stats is off, a site could not be changed.");
        return;
    }
    std::snprintf(AuraReason, sizeof(AuraReason), "%s", "installed");
}

// strength_aura_stats. Independent of aura_stats: it needs only the slot loop
// to be stock around the check, and is safe with the 0 gate open or closed
// (the args redirect keeps unfilled slots at id -1, which jl still skips).
void InstallStrength() noexcept {
    if (!CheckExact(AuraLoopHeadRva, AuraLoopHead.data(), AuraLoopHead.size(), "aura callback slot loop")
        || !CheckExact(AuraLoopRestRva, AuraLoopRest.data(), AuraLoopRest.size(), "aura callback slot loop")) {
        std::snprintf(StrengthReason, sizeof(StrengthReason), "off, %s", LastFailure);
        LogError("AuraZeroResults: strength_aura_stats is off.");
        return;
    }
    if (Context->CheckExpectedBytes(StatIdGateRva, StatIdGateJl.data(), static_cast<std::uint32_t>(StatIdGateJl.size()))) {
        Installed.statIdZeroAllowed = true;
        std::snprintf(StrengthReason, sizeof(StrengthReason), "%s",
                      "already made by a patches-folder memory patch, left to it (basic-aura-stat-id-0.json can go)");
        LogInfo("AuraZeroResults: stat id 0 is already allowed by a memory patch; basic-aura-stat-id-0.json can be "
                "deleted.");
        return;
    }
    if (!CheckExact(StatIdGateRva, StatIdGateJle.data(), StatIdGateJle.size(), "aura callback stat id check")
        || !PatchGate(StatIdGate)) {
        std::snprintf(StrengthReason, sizeof(StrengthReason), "off, %s", LastFailure);
        LogError("AuraZeroResults: strength_aura_stats is off.");
        return;
    }
    Installed.statIdZeroAllowed = true;
    Installed.statIdZeroOwned   = true;
    std::snprintf(StrengthReason, sizeof(StrengthReason), "%s", "installed");
}

void InstallPassive() noexcept {
    if (!ValidatePassive() || !EnsurePage()) {
        std::snprintf(PassiveReason, sizeof(PassiveReason), "off, %s", LastFailure);
        LogError("AuraZeroResults: passive_stats is off.");
        return;
    }
    auto& writeSite = CallSites[StubPassiveWrite];

    Installed.passiveWriteRedirect = PatchCallSite(writeSite);
    if (Installed.passiveWriteRedirect) {
        PassiveActive.store(true, std::memory_order_relaxed);
        Installed.passiveZeroGate = PatchGate(PassiveZeroGate);
    }
    if (!Installed.passiveZeroGate) {
        PassiveActive.store(false, std::memory_order_relaxed);
        if (!PassiveZeroGate.applied) {
            RestoreCallSite(writeSite);
            Installed.passiveWriteRedirect = false;
        }
        std::snprintf(PassiveReason, sizeof(PassiveReason), "off, %s", LastFailure);
        LogError("AuraZeroResults: passive_stats is off, a site could not be changed.");
        return;
    }
    std::snprintf(PassiveReason, sizeof(PassiveReason), "%s", "installed");
}

// ---------------------------------------------------------------------------
//  Console
// ---------------------------------------------------------------------------
auto OnOff(bool value) noexcept -> const char* {
    return value ? "on" : "off";
}

auto Count(const std::atomic<std::uint64_t>& counter) noexcept -> unsigned long long {
    return static_cast<unsigned long long>(counter.load(std::memory_order_relaxed));
}

auto StatusCommand(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command, void*) noexcept
    -> D2RL::ConsoleCommandResult {
    if (command == nullptr || command->plugin == nullptr) {
        return D2RL::ConsoleCommandResult::Failed;
    }

    char line[512]{};
    std::snprintf(line, sizeof(line),
                  "Aura Zero Results %s (%s): aura stats %s [gate %s, resistance rule %s, cleanse guard %s, "
                  "unfilled slots %s; stat id 0 %s]; passive stats %s [gate %s, list write %s]; diagnostics %s.",
                  PluginVersion, ConfigSource, OnOff(AuraActive.load(std::memory_order_relaxed)),
                  OnOff(Installed.zeroGate), OnOff(Installed.resistanceRedirect), OnOff(Installed.cleanseRedirect),
                  OnOff(Installed.argsRedirect),
                  Installed.statIdZeroOwned     ? "allowed by this plugin"
                  : Installed.statIdZeroAllowed ? "allowed by a memory patch"
                                                : "skipped",
                  OnOff(PassiveActive.load(std::memory_order_relaxed)), OnOff(Installed.passiveZeroGate),
                  OnOff(Installed.passiveWriteRedirect), OnOff(Config.diagnostics));
    command->plugin->WriteConsoleMessage(line);

    std::snprintf(line, sizeof(line), "Aura stats: %s", AuraReason);
    command->plugin->WriteConsoleMessage(line);
    std::snprintf(line, sizeof(line), "Strength aura stats: %s", StrengthReason);
    command->plugin->WriteConsoleMessage(line);
    std::snprintf(line, sizeof(line), "Passive stats: %s", PassiveReason);
    command->plugin->WriteConsoleMessage(line);

    std::snprintf(line, sizeof(line),
                  "Aura counters: 0 results %llu, stats cleared %llu, direct stats left alone %llu, Cleansing zeros "
                  "kept away %llu.",
                  Count(AuraZeroResults), Count(AuraStatsCleared), Count(AuraDirectLeft), Count(CleanseZerosKept));
    command->plugin->WriteConsoleMessage(line);

    std::snprintf(line, sizeof(line), "Passive counters: 0 results %llu, stats cleared %llu.", Count(PassiveZeroResults),
                  Count(PassiveStatsCleared));
    command->plugin->WriteConsoleMessage(line);

    std::snprintf(line, sizeof(line), "Read-back mismatches: %llu (0 expected).", Count(ReadBackMismatches));
    command->plugin->WriteConsoleMessage(line);
    return D2RL::ConsoleCommandResult::Handled;
}

void ResetState() noexcept {
    Config    = Settings{};
    Installed = InstallState{};
    AuraActive.store(false, std::memory_order_relaxed);
    PassiveActive.store(false, std::memory_order_relaxed);
    for (auto* counter : {&AuraZeroResults, &AuraStatsCleared, &AuraDirectLeft, &CleanseZerosKept,
                          &PassiveZeroResults, &PassiveStatsCleared, &ReadBackMismatches}) {
        counter->store(0, std::memory_order_relaxed);
    }
    MismatchReported.store(false, std::memory_order_relaxed);
    LastFailure[0] = '\0';
    std::snprintf(AuraReason, sizeof(AuraReason), "%s", "off in the config");
    std::snprintf(PassiveReason, sizeof(PassiveReason), "%s", "off in the config");
    std::snprintf(StrengthReason, sizeof(StrengthReason), "%s", "off in the config");
    DiagnosticsBudget.store(64, std::memory_order_relaxed);
}

}  // namespace
}  // namespace CelestialRayOne::AuraZeroResults

namespace {

constexpr D2RL::PluginInfo Info{
    .infoSize    = D2RL::PluginInfoSize,
    .abiVersion  = D2RL_PLUGIN_ABI_VERSION,
    .id          = "celestialrayone.aura-zero-results",
    .name        = "Aura Zero Results",
    .version     = "1.1.0",
    .author      = "CelestialRayOne",
    .description = "Aura formulas that return 0 remove their stat instead of keeping the last value.",
    .flags       = D2RL::PluginFlags::Server | D2RL::PluginFlags::NativeHooks,
};

}  // namespace

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* {
    return &Info;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
    using namespace CelestialRayOne::AuraZeroResults;

    if (!D2RL::HasContext(context) || context->exeBase == 0) {
        return false;
    }
    Context = context;
    Base    = context->exeBase;
    ResetState();
    LoadConfig();

    if (!Config.enabled) {
        LogInfo("Aura Zero Results %s loaded disabled by config; the game runs stock.", PluginVersion);
        return true;
    }

    ResolveNativeFunctions();
    if (Config.strengthAuraStats) {
        InstallStrength();
    }
    if (Config.auraStats) {
        InstallAura();
    }
    if (Config.passiveStats) {
        InstallPassive();
    }

    if (!context->RegisterConsoleCommand("aurazeroresults", StatusCommand,
                                         "Show Aura Zero Results status and counters.")) {
        LogWarn("AuraZeroResults: the aurazeroresults console command could not be registered.");
    }

    LogInfo("Aura Zero Results %s by CelestialRayOne: aura stats %s, passive stats %s, build %s.", PluginVersion,
            OnOff(AuraActive.load(std::memory_order_relaxed)), OnOff(PassiveActive.load(std::memory_order_relaxed)),
            context->buildName != nullptr ? context->buildName : "unknown");
    return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
    using namespace CelestialRayOne::AuraZeroResults;
    if (Context == nullptr) {
        return;
    }
    // The bridges go back to pass-through first, then the gates close. A
    // redirect is only removed once its gate is closed again, so a 0 never
    // reaches a stock callee (the cleanse helper would end every curable state).
    AuraActive.store(false, std::memory_order_relaxed);
    PassiveActive.store(false, std::memory_order_relaxed);

    const bool auraGateClosed    = RestoreGate(ZeroGate);
    const bool passiveGateClosed = RestoreGate(PassiveZeroGate);
    bool       allRestored       = auraGateClosed && passiveGateClosed;
    if (auraGateClosed) {
        allRestored = RestoreCallSite(CallSites[StubArgs]) && allRestored;
        allRestored = RestoreCallSite(CallSites[StubCleanse]) && allRestored;
        allRestored = RestoreCallSite(CallSites[StubResistance]) && allRestored;
    }
    if (passiveGateClosed) {
        allRestored = RestoreCallSite(CallSites[StubPassiveWrite]) && allRestored;
    }
    // jle again skips ids 0 and -1 alike, whatever state the rest is in.
    RestoreGate(StatIdGate);
    if (allRestored && PageUnwindRegistered && Page != nullptr) {
        RtlDeleteFunctionTable(reinterpret_cast<PRUNTIME_FUNCTION>(Page + FunctionTableOffset));
        PageUnwindRegistered = false;
    }
}
