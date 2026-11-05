// Aura Percent Life Damage (celestialrayone.aura-percent-life)
//
// Every skill whose skills.txt srvdofunc is 66 (Holy Fire, Holy Shock in
// vanilla) or 81 (Holy Freeze in vanilla) can deal damage based on the current
// life of each target it pulses over, the way Static Field (srvdofunc 20) does.
// A calc column of the row (calc4 by default) holds the percent. On every
// pulse, for every target, the aura's element damage becomes the higher of its
// own EMin..EMax roll and Static Field's percent amount for that target. A row
// whose column is blank or evaluates to 0 or less stays vanilla.
//
// Everything below was read out of the live D2RLoader 1.3.1 image (D2R 3.3,
// base 0x140000000). Every byte the plugin relies on is checked at load and
// nothing is installed when one differs.
//
// ---------------------------------------------------------------------------
// Static Field, the rules copied here
// ---------------------------------------------------------------------------
//   srvdofunc table 0x238EA00: slot 20 -> 0x5546B0 Static Field, slot 66 ->
//   0x5630E0 Holy Fire / Holy Shock, slot 81 -> 0x563720 Holy Freeze. No
//   other slot points at the 66 or 81 body.
//
//   Body 0x5546B0 (checked bytes 0x554709..0x5547DF):
//     0x554704  call 48FF40(&ctx, game, caster)   difficulty context
//     0x55471F  call 300830(game+106h, ctx[0])    difficultylevels.txt row
//     0x554730  calc2 (+194h) -> flat minimum, 0x554762 calc1 (+190h) ->
//               percent, both through the evaluator 0x3B5160
//     0x554773  cmp byte [ctx+1],1 / je: StaticFieldMin (+68h) is used
//               unless ctx[1] is 1
//     0x5547BE  EType (+284h) -> element
//   Per-target callback 0x5556A0:
//     life = stat 6 >> 8; a target below 1 is skipped.
//     With a StaticFieldMin, a target at or below maxlife (0x2F4D20) >> 8
//     x StaticFieldMin / 100 takes nothing.
//     percent = life x calc1 / 100, capped at life - 1, so the percent part
//     alone never kills; damage = max(calc2, percent << 8).
//     A negative resist for the element (stat id from AddElementalDamage
//     0x437980) divides the damage by (100 - resist) / 100, so negative
//     resistance never amplifies it.
//     Every x / y goes through the game's overflow-safe percentage routine
//     (MONSTERUNIQUE_CalculatePercentage), copied as Percentage() below.
//
// ---------------------------------------------------------------------------
// Where 66 and 81 take it
// ---------------------------------------------------------------------------
//   Both bodies roll EMin..EMax once per pulse (0x437D00, into the EType slot
//   of a damage packet). Their per-target callbacks, 0x561280 (66) and
//   0x561480 (81), copy that packet for each target (0x4494B0) and then run
//   the same block, checked at load:
//     mov r9d,[rbx+4]        skill id
//     lea rax,[rsp+50h|60h] / mov [rsp+28h],rax      &packet copy
//     mov r8,rdi             target
//     mov eax,[rbx+8] / mov rdx,r14 / mov rcx,rsi / mov [rsp+20h],eax
//     call 560F60            (game, caster, target, skill id, level, &copy)
//     lea rax,[same copy] / mov r9d,1 / ... / call 44CE80  ExecuteEvents
//   at 0x56139E (66) and 0x5615F4 (81). 0x560F60 is the vanilla close-range
//   aura bonus, limited to vanilla skill ids 102, 114, 118, 119, 365, 368 and
//   369 and a no-op for every other row; those two calls are its only code
//   references. Its return value is never read.
//
//   So one inline hook on 0x560F60 sees every target of both srvdofuncs with
//   the caster, the skill id and level, and the target's own packet copy, right
//   before the damage is resolved. The hook calls the original first, then
//   raises the element slot of that copy to the percent amount when it is
//   higher than the roll. Nothing else in the packet changes, so the aura keeps
//   its own ResultFlags, HitFlags, HitClass and length.
//
//   Not touched, and no byte overlap with: holy-freeze-boss-damage (0x56150E,
//   0x561675), soft-hits (0x56357C, 0x563C0B), aura-zero-branch (0x5616F0),
//   ExecuteEvents hooks at 0x44CE80 (they run after this plugin's change).
//
// ---------------------------------------------------------------------------
// The element
// ---------------------------------------------------------------------------
//   EType (+284h) picks the slot and the resist stat, exactly as
//   AddElementalDamage 0x437980 maps them (jump table 0x437B1C, case bodies
//   0x437A37..0x437AF7, both checked):
//     1 fire  +20h  stat 39     2 ltng +2Ch stat 41    3 mag  +30h stat 37
//     4 cold  +34h  stat 43     5 pois +38h stat 45    11 burn +24h no resist
//     12 frze +34h  stat 43     0 and above 12: physical +18h, no resist
//     6, 7, 8, 9 (life, mana, stamina, stun): no life damage, skipped
//   EType 10 (random) was already resolved by the pulse's roll to one of the
//   four elements in 0x1D195F8 (fire, ltng, cold, pois, checked); the plugin
//   uses whichever of those four slots holds the roll.
//   With dynamic-etype loaded, the row holds the cast's decided element while
//   the do-handler runs, which is when this hook is reached.

#include <D2RLPlugin/api.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

namespace CelestialRayOne::AuraPercentLife {
namespace {

// ---------------------------------------------------------------------------
// Native anchors
// ---------------------------------------------------------------------------

constexpr std::uint64_t AuraBonusRva             = 0x560F60;   // hooked
constexpr std::uint64_t FireShockCallWindowRva   = 0x561380;   // in 0x561280
constexpr std::uint64_t FreezeCallWindowRva      = 0x5615D6;   // in 0x561480
constexpr std::uint64_t StaticFieldBodyRva       = 0x554709;   // in 0x5546B0
constexpr std::uint64_t ElementSwitchRva         = 0x437A37;   // in 0x437980
constexpr std::uint64_t ElementJumpTableRva      = 0x437B1C;
constexpr std::uint64_t RandomElementTableRva    = 0x1D195F8;

constexpr std::uint64_t GetSkillsTxtRecordRva    = 0x097790;   // (dataContext, skillId)
constexpr std::uint64_t EvaluateFormulaRva       = 0x3B5160;   // (dataContext, unit, calc, skillId, level)
constexpr std::uint64_t GetUnitStatRva           = 0x2F5020;   // (unit, stat, layer), loader thunk
constexpr std::uint64_t GetMaxLifeRva            = 0x2F4D20;   // (unit), loader thunk
constexpr std::uint64_t GetDifficultyContextRva  = 0x48FF40;   // (out, game, unit)
constexpr std::uint64_t GetDifficultyLevelsRva   = 0x300830;   // (dataContext, difficulty)

// ---------------------------------------------------------------------------
// Layouts
// ---------------------------------------------------------------------------

constexpr std::size_t   GameDataContextOffset    = 0x106;
constexpr std::size_t   SkillCalc1Offset         = 0x190;      // calc1..calc10 = +190h..+1B4h
constexpr std::size_t   SkillCalcStride          = 4;
constexpr std::size_t   SkillETypeOffset         = 0x284;
constexpr std::size_t   StaticFieldMinOffset     = 0x68;       // difficultylevels.txt row
constexpr std::uint32_t BlankCalc                = 0xFFFFFFFFu;
constexpr std::int32_t  StatHitpoints            = 6;
constexpr std::uint8_t  ContextWithoutFloor      = 1;          // ctx[1], see 0x554773
constexpr std::size_t   DifficultyContextBytes   = 16;         // 0x48FF40 writes 12
constexpr int           FirstCalcColumn          = 1;
constexpr int           LastCalcColumn           = 10;
constexpr std::uint32_t MaximumConfigBytes       = 32'768;

struct ElementSlot {
    std::uint32_t damageOffset;
    std::int32_t  resistStat;      // -1: no resist compensation
};

constexpr ElementSlot PhysicalSlot { 0x18, -1 };
constexpr ElementSlot FireSlot     { 0x20, 39 };
constexpr ElementSlot BurnSlot     { 0x24, -1 };
constexpr ElementSlot LightningSlot{ 0x2C, 41 };
constexpr ElementSlot MagicSlot    { 0x30, 37 };
constexpr ElementSlot ColdSlot     { 0x34, 43 };
constexpr ElementSlot PoisonSlot   { 0x38, 45 };

constexpr std::uint8_t ETypeRandom = 10;
constexpr std::uint8_t RandomElements[]{ 1, 2, 4, 5 };   // 0x1D195F8

// ---------------------------------------------------------------------------
// Witnesses, read out of the image and checked at load
// ---------------------------------------------------------------------------
//   AuraBonusEntry            0x560F60 prologue, the hook's expected bytes:
//                             mov rax,rsp / mov [rax+8],rcx / push rbx, rsi,
//                             rdi, r13 / sub rsp,58h. No relative operand.
//   FireShockCallWindow       0x561380..0x5613C1, the call block in 66's
//                             callback quoted in the header.
//   FreezeCallWindow          0x5615D6..0x561617, the same block in 81's.
//   StaticFieldBody           0x554709..0x5547DF, Static Field's argument
//                             setup: calc1/calc2/EType offsets and the floor.
//   ElementSwitch             0x437A37..0x437AFA, AddElementalDamage's cases.
//   ElementJumpTable          0x437B1C, its twelve case RVAs (EType 1..12).
//   RandomElementTable        0x1D195F8, EType 10's four elements.
//   The rest are the entries of the functions this plugin calls, the same
//   bytes the other ESR plugins check for them.

// AuraBonusEntry: 16 bytes at 0x560F60
constexpr std::uint8_t AuraBonusEntry[]{
    0x48, 0x8B, 0xC4, 0x48, 0x89, 0x48, 0x08, 0x53, 0x56, 0x57, 0x41, 0x55,
    0x48, 0x83, 0xEC, 0x58,
};

// FireShockCallWindow: 65 bytes at 0x561380
constexpr std::uint8_t FireShockCallWindow[]{
    0x44, 0x8B, 0x4B, 0x04, 0x48, 0x8D, 0x44, 0x24, 0x50, 0x48, 0x89, 0x44,
    0x24, 0x28, 0x4C, 0x8B, 0xC7, 0x8B, 0x43, 0x08, 0x49, 0x8B, 0xD6, 0x48,
    0x8B, 0xCE, 0x89, 0x44, 0x24, 0x20, 0xE8, 0xBD, 0xFB, 0xFF, 0xFF, 0x48,
    0x8D, 0x44, 0x24, 0x50, 0x41, 0xB9, 0x01, 0x00, 0x00, 0x00, 0x4C, 0x8B,
    0xC7, 0x48, 0x89, 0x44, 0x24, 0x20, 0x49, 0x8B, 0xD6, 0x48, 0x8B, 0xCE,
    0xE8, 0xBF, 0xBA, 0xEE, 0xFF,
};

// FreezeCallWindow: 65 bytes at 0x5615D6
constexpr std::uint8_t FreezeCallWindow[]{
    0x44, 0x8B, 0x4B, 0x04, 0x48, 0x8D, 0x44, 0x24, 0x60, 0x48, 0x89, 0x44,
    0x24, 0x28, 0x4C, 0x8B, 0xC7, 0x8B, 0x43, 0x08, 0x49, 0x8B, 0xD6, 0x48,
    0x8B, 0xCE, 0x89, 0x44, 0x24, 0x20, 0xE8, 0x67, 0xF9, 0xFF, 0xFF, 0x48,
    0x8D, 0x44, 0x24, 0x60, 0x41, 0xB9, 0x01, 0x00, 0x00, 0x00, 0x4C, 0x8B,
    0xC7, 0x48, 0x89, 0x44, 0x24, 0x20, 0x49, 0x8B, 0xD6, 0x48, 0x8B, 0xCE,
    0xE8, 0x69, 0xB8, 0xEE, 0xFF,
};

// GetSkillsTxtRecordEntry: 24 bytes at 0x97790
constexpr std::uint8_t GetSkillsTxtRecordEntry[]{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57, 0x48,
    0x83, 0xEC, 0x30, 0x48, 0x63, 0xF2, 0xE8, 0xE9, 0x92, 0x26, 0x00, 0x48,
};

// EvaluateFormulaEntry: 32 bytes at 0x3B5160
constexpr std::uint8_t EvaluateFormulaEntry[]{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89,
    0x74, 0x24, 0x20, 0x57, 0x41, 0x56, 0x41, 0x57, 0x48, 0x81, 0xEC, 0xB0,
    0x00, 0x00, 0x00, 0x45, 0x8B, 0xF1, 0x41, 0x8B,
};

// GetUnitStatThunk: 10 bytes at 0x2F5020
constexpr std::uint8_t GetUnitStatThunk[]{
    0xFF, 0x25, 0xF2, 0x51, 0xB3, 0x03, 0x90, 0x90, 0x90, 0x90,
};

// GetMaxLifeThunk: 6 bytes at 0x2F4D20
constexpr std::uint8_t GetMaxLifeThunk[]{
    0xFF, 0x25, 0xBA, 0x55, 0xB3, 0x03,
};

// DifficultyContextEntry: 60 bytes at 0x48FF40
constexpr std::uint8_t DifficultyContextEntry[]{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x60, 0x48, 0x8B,
    0x05, 0x77, 0xB3, 0x53, 0x02, 0x48, 0x33, 0xC4, 0x48, 0x89, 0x44, 0x24,
    0x58, 0x0F, 0xB6, 0x82, 0x04, 0x01, 0x00, 0x00, 0x48, 0x8B, 0xD9, 0x88,
    0x01, 0x49, 0x8B, 0xF8, 0x0F, 0xB6, 0x82, 0x01, 0x01, 0x00, 0x00, 0x88,
    0x41, 0x08, 0x0F, 0xB6, 0x82, 0x06, 0x01, 0x00, 0x00, 0x88, 0x41, 0x01,
};

// DifficultyLevelsEntry: 24 bytes at 0x300830
constexpr std::uint8_t DifficultyLevelsEntry[]{
    0x40, 0x53, 0x56, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x0F, 0xB6, 0xC1, 0x8B,
    0xF2, 0x48, 0x89, 0x44, 0x24, 0x60, 0x48, 0x83, 0xF8, 0x04, 0x72, 0x19,
};

// StaticFieldBody: 214 bytes at 0x554709
constexpr std::uint8_t StaticFieldBody[]{
    0x0F, 0xB6, 0x8B, 0x06, 0x01, 0x00, 0x00, 0xF2, 0x0F, 0x10, 0x00, 0xF2,
    0x0F, 0x11, 0x44, 0x24, 0x60, 0x0F, 0xB6, 0x54, 0x24, 0x60, 0xE8, 0x0C,
    0xC1, 0xDA, 0xFF, 0x4C, 0x8B, 0xF8, 0x48, 0x85, 0xC0, 0x0F, 0x84, 0x2B,
    0x01, 0x00, 0x00, 0x44, 0x8B, 0x86, 0x94, 0x01, 0x00, 0x00, 0x44, 0x8B,
    0xCD, 0x0F, 0xB6, 0x8B, 0x06, 0x01, 0x00, 0x00, 0x48, 0x8B, 0xD7, 0x44,
    0x89, 0x74, 0x24, 0x20, 0xE8, 0x12, 0x0A, 0xE6, 0xFF, 0x0F, 0xB6, 0x8B,
    0x06, 0x01, 0x00, 0x00, 0x44, 0x8B, 0xCD, 0x89, 0x84, 0x24, 0x80, 0x00,
    0x00, 0x00, 0x48, 0x8B, 0xD7, 0x44, 0x8B, 0x86, 0x90, 0x01, 0x00, 0x00,
    0x44, 0x89, 0x74, 0x24, 0x20, 0xE8, 0xED, 0x09, 0xE6, 0xFF, 0x80, 0x7C,
    0x24, 0x61, 0x01, 0x89, 0x84, 0x24, 0x84, 0x00, 0x00, 0x00, 0xC7, 0x84,
    0x24, 0x88, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x74, 0x0B, 0x41,
    0x8B, 0x47, 0x68, 0x89, 0x84, 0x24, 0x88, 0x00, 0x00, 0x00, 0x0F, 0xB6,
    0x8B, 0x06, 0x01, 0x00, 0x00, 0x45, 0x8B, 0xCE, 0x44, 0x8B, 0xC5, 0xC7,
    0x44, 0x24, 0x20, 0x01, 0x00, 0x00, 0x00, 0x48, 0x8B, 0xD7, 0xE8, 0xEC,
    0x35, 0xDE, 0xFF, 0x89, 0x84, 0x24, 0x8C, 0x00, 0x00, 0x00, 0x44, 0x8B,
    0xCD, 0x0F, 0xB6, 0x86, 0x84, 0x02, 0x00, 0x00, 0x4C, 0x8B, 0xC6, 0x48,
    0x8B, 0xD7, 0x89, 0x84, 0x24, 0x90, 0x00, 0x00, 0x00, 0x48, 0x8B, 0xCB,
    0x44, 0x89, 0x74, 0x24, 0x20, 0xE8, 0x61, 0xE4, 0xED, 0xFF,
};

// ElementSwitch: 195 bytes at 0x437A37
constexpr std::uint8_t ElementSwitch[]{
    0x01, 0x7B, 0x20, 0xBA, 0x27, 0x00, 0x00, 0x00, 0xC7, 0x83, 0x44, 0x01,
    0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0xE9, 0xAC, 0x00, 0x00, 0x00, 0x01,
    0x7B, 0x2C, 0xBA, 0x29, 0x00, 0x00, 0x00, 0xC7, 0x83, 0x44, 0x01, 0x00,
    0x00, 0x40, 0x00, 0x00, 0x00, 0xE9, 0x95, 0x00, 0x00, 0x00, 0x01, 0x7B,
    0x30, 0xBA, 0x25, 0x00, 0x00, 0x00, 0xE9, 0x88, 0x00, 0x00, 0x00, 0x01,
    0x7B, 0x34, 0xBA, 0x2B, 0x00, 0x00, 0x00, 0x44, 0x89, 0x83, 0x18, 0x01,
    0x00, 0x00, 0xC7, 0x83, 0x44, 0x01, 0x00, 0x00, 0x30, 0x00, 0x00, 0x00,
    0xEB, 0x6D, 0x01, 0x7B, 0x38, 0xBA, 0x2D, 0x00, 0x00, 0x00, 0x44, 0x89,
    0x43, 0x3C, 0xC7, 0x83, 0x44, 0x01, 0x00, 0x00, 0x50, 0x00, 0x00, 0x00,
    0xEB, 0x55, 0x01, 0xBB, 0x20, 0x01, 0x00, 0x00, 0xEB, 0x4D, 0x01, 0xBB,
    0x24, 0x01, 0x00, 0x00, 0xEB, 0x45, 0x01, 0xBB, 0x28, 0x01, 0x00, 0x00,
    0xEB, 0x3D, 0x42, 0x8D, 0x04, 0x07, 0xC7, 0x83, 0x44, 0x01, 0x00, 0x00,
    0x60, 0x00, 0x00, 0x00, 0x01, 0x83, 0x2C, 0x01, 0x00, 0x00, 0xEB, 0x27,
    0x01, 0x7B, 0x24, 0x44, 0x89, 0x43, 0x28, 0xEB, 0x1E, 0x01, 0x7B, 0x34,
    0xBA, 0x2B, 0x00, 0x00, 0x00, 0x44, 0x89, 0x83, 0x1C, 0x01, 0x00, 0x00,
    0xC7, 0x83, 0x44, 0x01, 0x00, 0x00, 0x30, 0x00, 0x00, 0x00, 0xEB, 0x03,
    0x01, 0x7B, 0x18,
};

// ElementJumpTable: 48 bytes at 0x437B1C
constexpr std::uint8_t ElementJumpTable[]{
    0x37, 0x7A, 0x43, 0x00, 0x4E, 0x7A, 0x43, 0x00, 0x65, 0x7A, 0x43, 0x00,
    0x72, 0x7A, 0x43, 0x00, 0x8D, 0x7A, 0x43, 0x00, 0xA5, 0x7A, 0x43, 0x00,
    0xAD, 0x7A, 0x43, 0x00, 0xB5, 0x7A, 0x43, 0x00, 0xBD, 0x7A, 0x43, 0x00,
    0xF7, 0x7A, 0x43, 0x00, 0xD3, 0x7A, 0x43, 0x00, 0xDC, 0x7A, 0x43, 0x00,
};

// RandomElementTable: 16 bytes at 0x1D195F8
constexpr std::uint8_t RandomElementTable[]{
    0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00,
    0x05, 0x00, 0x00, 0x00,
};

static_assert(sizeof(AuraBonusEntry) == 16);
static_assert(sizeof(FireShockCallWindow) == 65 && sizeof(FreezeCallWindow) == 65);
// Both windows call 0x560F60 at +1Eh and ExecuteEvents at +3Ch.
static_assert(FireShockCallWindow[0x1E] == 0xE8 && FireShockCallWindow[0x3C] == 0xE8);
static_assert(FreezeCallWindow[0x1E] == 0xE8 && FreezeCallWindow[0x3C] == 0xE8);
static_assert(sizeof(ElementJumpTable) == 12 * 4);
static_assert(sizeof(RandomElementTable) == 4 * 4);

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

struct Config {
    bool enabled        = true;
    int  calcColumn     = 4;
    bool staticFieldMin = true;
};

constexpr char DefaultConfigToml[] = R"toml(# Aura Percent Life Damage
#
# Every skill whose skills.txt srvdofunc is 66 (Holy Fire, Holy Shock in
# vanilla) or 81 (Holy Freeze in vanilla) can deal damage based on the current
# life of each target it pulses over, the way Static Field (srvdofunc 20) does.
#
# The percent comes from one calc column of the skill's row (calc_column
# below). On every pulse, for every target, the aura's element damage becomes
# the higher of:
#   - its own EMin..EMax roll, with everything the game already adds to it
#   - that percent of the target's current life, worked out like Static
#     Field's own percent part:
#       the percent part never takes the target's last point of life,
#       a target at or below difficultylevels.txt StaticFieldMin percent of its
#       max life gets no percent part (static_field_min below),
#       a negative resistance to the element does not amplify it.
# A blank column, or one that evaluates to 0 or less, leaves the row vanilla.
#
# The calc is evaluated for the caster, like every aura calc, once per target
# per pulse. Values above 100 act like 100 because of the last-point rule.
# The percent lands in the row's EType slot, the same one the roll went to, so
# resistances, immunities, absorbs and the difficultylevels.txt damage
# percents treat it exactly like the roll. EType 6 to 9 (life, mana, stamina,
# stun) rows get no percent part.
#
# Console command: aurapercent (status, counters and the last percent hit)

[aura_percent_life]

# Master switch. False leaves both srvdofuncs completely vanilla.
enabled = true

# skills.txt calc column that holds the percent, 1 to 10 (calc1 to calc10).
# The two aura functions read none of these columns themselves; this plugin is
# the only reader. Keep the column free of anything else on every srvdofunc 66
# and 81 row, because any value in it is taken as a percent.
calc_column = 4

# Apply difficultylevels.txt StaticFieldMin to the percent part, the same way
# Static Field applies it. The aura's own roll is never affected by it.
static_field_min = true
)toml";

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

enum class HookState : std::uint8_t {
    NotLoaded,
    DisabledByConfig,
    Armed,
};

using AuraBonusFn = std::int64_t(__fastcall*)(void* game, void* caster, void* target,
    std::int32_t skillId, std::int32_t skillLevel, std::uint8_t* packet) noexcept;
using GetSkillsTxtRecordFn = const std::uint8_t*(__fastcall*)(std::uint32_t dataContext,
    std::int32_t skillId) noexcept;
using EvaluateFormulaFn = std::int32_t(__fastcall*)(std::uint32_t dataContext, void* unit,
    std::uint32_t poolOffset, std::int32_t skillId, std::int32_t skillLevel) noexcept;
using GetUnitStatFn = std::int32_t(__fastcall*)(void* unit, std::int32_t statId,
    std::int32_t layer) noexcept;
using GetMaxLifeFn = std::int32_t(__fastcall*)(void* unit) noexcept;
using GetDifficultyContextFn = std::uint8_t*(__fastcall*)(std::uint8_t* out, void* game,
    void* unit) noexcept;
using GetDifficultyLevelsFn = const std::uint8_t*(__fastcall*)(std::uint32_t dataContext,
    std::int32_t difficulty) noexcept;

const D2RL::PluginContext* Context{};
std::uintptr_t             Base{};
Config                     Settings{};
HookState                  State{ HookState::NotLoaded };
std::atomic<bool>          Active{};

AuraBonusFn            OriginalAuraBonus{};
GetSkillsTxtRecordFn   GetSkillsTxtRecord{};
EvaluateFormulaFn      EvaluateFormula{};
GetUnitStatFn          GetUnitStat{};
GetMaxLifeFn           GetMaxLife{};
GetDifficultyContextFn GetDifficultyContext{};
GetDifficultyLevelsFn  GetDifficultyLevels{};

std::atomic<std::uint64_t> TargetsChecked{};
std::atomic<std::uint64_t> PercentWon{};
std::atomic<std::uint64_t> RollKept{};
std::atomic<std::uint64_t> FloorSkipped{};
std::atomic<std::int32_t>  LastSkill{ -1 };
std::atomic<std::int32_t>  LastLife{};
std::atomic<std::int32_t>  LastPercent{};
std::atomic<std::int32_t>  LastRoll{};
std::atomic<std::int32_t>  LastDamage{};
std::atomic<bool>          ReportedFirst{};
std::atomic<std::uint32_t> ReportedETypes{};

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

void ParseBool(std::string_view value, bool& out) noexcept {
    if (value == "true" || value == "1") {
        out = true;
    } else if (value == "false" || value == "0") {
        out = false;
    }
}

auto ParseInt(std::string_view value, int& out) noexcept -> bool {
    if (value.empty()) return false;
    std::size_t index = 0;
    bool negative = false;
    if (value[0] == '-' || value[0] == '+') {
        negative = value[0] == '-';
        index = 1;
    }
    if (index >= value.size()) return false;
    long long result = 0;
    for (; index < value.size(); ++index) {
        const char digit = value[index];
        if (digit < '0' || digit > '9') return false;
        result = result * 10 + (digit - '0');
        if (result > 1'000'000) return false;
    }
    out = static_cast<int>(negative ? -result : result);
    return true;
}

void ApplyConfigLine(std::string_view key, std::string_view value) noexcept {
    if (key == "enabled") {
        ParseBool(value, Settings.enabled);
    } else if (key == "static_field_min") {
        ParseBool(value, Settings.staticFieldMin);
    } else if (key == "calc_column") {
        int column = 0;
        if (ParseInt(value, column) && column >= FirstCalcColumn && column <= LastCalcColumn) {
            Settings.calcColumn = column;
        } else {
            Context->LogWarn("AuraPercentLife: calc_column must be 1 to 10; using calc4.");
        }
    }
}

void ParseConfig(std::string_view text) noexcept {
    std::size_t cursor = 0;
    while (cursor <= text.size()) {
        const std::size_t breakAt = text.find('\n', cursor);
        const std::size_t end = breakAt == std::string_view::npos ? text.size() : breakAt;
        std::string_view line = Trim(text.substr(cursor, end - cursor));
        cursor = end + 1;
        if (breakAt == std::string_view::npos && line.empty()) break;

        const std::size_t comment = line.find('#');
        if (comment != std::string_view::npos) line = Trim(line.substr(0, comment));
        if (!line.empty() && line.front() != '[') {
            const std::size_t equals = line.find('=');
            if (equals != std::string_view::npos) {
                ApplyConfigLine(Trim(line.substr(0, equals)), Trim(line.substr(equals + 1)));
            }
        }
        if (breakAt == std::string_view::npos) break;
    }
}

void ReadConfiguration() noexcept {
    if (!Context->EnsureConfig(DefaultConfigToml)) {
        Context->LogWarn("AuraPercentLife: config file could not be created; using defaults.");
        return;
    }
    std::string buffer(MaximumConfigBytes, '\0');
    if (!Context->ReadConfig(buffer.data(), static_cast<std::uint32_t>(buffer.size()), nullptr)) {
        Context->LogWarn("AuraPercentLife: config file could not be read; using defaults.");
        return;
    }
    buffer.resize(std::strlen(buffer.c_str()));
    ParseConfig(buffer);
}

// ---------------------------------------------------------------------------
// Static Field's arithmetic
// ---------------------------------------------------------------------------

// MONSTERUNIQUE_CalculatePercentage, value x multiplier / divisor with the
// game's own overflow handling. Static Field's callback inlines it three times
// (0x55572A floor, 0x5557D5 percent, 0x555974 resist), always with these
// branches and thresholds.
auto Percentage(std::int32_t value, std::int32_t multiplier, std::int32_t divisor) noexcept
        -> std::int32_t {
    if (divisor == 0) return 0;
    if (value <= 0x100000) {
        if (multiplier <= 0x10000) {
            return static_cast<std::int32_t>(
                static_cast<std::uint32_t>(multiplier) * static_cast<std::uint32_t>(value))
                / divisor;
        }
        if (divisor <= (multiplier >> 4)) {
            return static_cast<std::int32_t>(
                static_cast<std::uint32_t>(value) * static_cast<std::uint32_t>(multiplier / divisor));
        }
    } else if (divisor <= (value >> 4)) {
        return static_cast<std::int32_t>(
            static_cast<std::uint32_t>(multiplier) * static_cast<std::uint32_t>(value / divisor));
    }
    return static_cast<std::int32_t>(static_cast<std::int64_t>(multiplier) * value / divisor);
}

auto ReadSlot(const std::uint8_t* packet, const ElementSlot& slot) noexcept -> std::int32_t {
    std::int32_t value = 0;
    std::memcpy(&value, packet + slot.damageOffset, sizeof(value));
    return value;
}

void WriteSlot(std::uint8_t* packet, const ElementSlot& slot, std::int32_t value) noexcept {
    std::memcpy(packet + slot.damageOffset, &value, sizeof(value));
}

// AddElementalDamage 0x437980's EType mapping. False for the rows that deal no
// life damage.
auto LookupSlot(std::uint32_t eType, ElementSlot& slot) noexcept -> bool {
    switch (eType) {
    case 1:  slot = FireSlot;      return true;
    case 2:  slot = LightningSlot; return true;
    case 3:  slot = MagicSlot;     return true;
    case 4:  slot = ColdSlot;      return true;
    case 5:  slot = PoisonSlot;    return true;
    case 6:
    case 7:
    case 8:
    case 9:  return false;
    case 11: slot = BurnSlot;      return true;
    case 12: slot = ColdSlot;      return true;
    default: slot = PhysicalSlot;  return true;
    }
}

// EType 10: the pulse's roll put its damage into exactly one of the four
// random elements.
auto LookupRolledRandomSlot(const std::uint8_t* packet, ElementSlot& slot) noexcept -> bool {
    for (const std::uint8_t element : RandomElements) {
        ElementSlot candidate{};
        if (LookupSlot(element, candidate) && ReadSlot(packet, candidate) > 0) {
            slot = candidate;
            return true;
        }
    }
    return false;
}

void ReportUnsupportedEType(std::int32_t skillId, std::uint8_t eType) noexcept {
    const std::uint32_t bit = 1U << eType;
    if ((ReportedETypes.fetch_or(bit, std::memory_order_relaxed) & bit) != 0) return;
    D2RL::LogWarnF(Context,
        "AuraPercentLife: skill %d has a percent in calc%d but EType %u (life, mana, stamina "
        "or stun). Those deal no life damage, so it gets no percent part.",
        skillId, Settings.calcColumn, static_cast<unsigned>(eType));
}

// Static Field's floor, in its own order: the caster's difficulty context, the
// difficultylevels.txt row of ctx[0], no floor when ctx[1] is 1, then a target
// at or below maxlife x StaticFieldMin / 100 is left alone.
auto AboveStaticFieldMin(void* game, void* caster, void* target, std::uint32_t dataContext,
        std::int32_t life) noexcept -> bool {
    alignas(8) std::uint8_t context[DifficultyContextBytes]{};
    GetDifficultyContext(context, game, caster);
    const std::uint8_t* levels = GetDifficultyLevels(dataContext, context[0]);
    if (levels == nullptr) return false;
    if (context[1] == ContextWithoutFloor) return true;

    std::int32_t floorPercent = 0;
    std::memcpy(&floorPercent, levels + StaticFieldMinOffset, sizeof(floorPercent));
    if (floorPercent == 0) return true;

    const std::int32_t maxLife = GetMaxLife(target) >> 8;
    return life > Percentage(maxLife, floorPercent, 100);
}

void ApplyPercentLife(void* game, void* caster, void* target, std::int32_t skillId,
        std::int32_t skillLevel, std::uint8_t* packet) noexcept {
    if (game == nullptr || caster == nullptr || target == nullptr || packet == nullptr) return;

    const std::uint32_t dataContext =
        *(static_cast<const std::uint8_t*>(game) + GameDataContextOffset);
    const std::uint8_t* record = GetSkillsTxtRecord(dataContext, skillId);
    if (record == nullptr) return;

    std::uint32_t poolOffset = BlankCalc;
    std::memcpy(&poolOffset,
        record + SkillCalc1Offset + SkillCalcStride * static_cast<std::size_t>(Settings.calcColumn - 1),
        sizeof(poolOffset));
    if (poolOffset == BlankCalc) return;

    const std::int32_t percent = EvaluateFormula(dataContext, caster, poolOffset, skillId, skillLevel);
    if (percent <= 0) return;
    TargetsChecked.fetch_add(1, std::memory_order_relaxed);

    ElementSlot slot{};
    const std::uint8_t eType = record[SkillETypeOffset];
    if (eType == ETypeRandom) {
        if (!LookupRolledRandomSlot(packet, slot)) return;
    } else if (!LookupSlot(eType, slot)) {
        ReportUnsupportedEType(skillId, eType);
        return;
    }

    const std::int32_t life = GetUnitStat(target, StatHitpoints, 0) >> 8;
    if (life < 1) return;

    if (Settings.staticFieldMin && !AboveStaticFieldMin(game, caster, target, dataContext, life)) {
        FloorSkipped.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    std::int32_t points = Percentage(life, percent, 100);
    if (life - points < 1) points = life - 1;
    std::int32_t damage = static_cast<std::int32_t>(static_cast<std::uint32_t>(points) << 8);
    if (damage <= 0) return;

    if (slot.resistStat >= 0) {
        const std::int32_t resist = GetUnitStat(target, slot.resistStat, 0);
        if (resist < 0) damage = Percentage(damage, 100, 100 - resist);
    }

    const std::int32_t roll = ReadSlot(packet, slot);
    if (damage <= roll) {
        RollKept.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    WriteSlot(packet, slot, damage);
    PercentWon.fetch_add(1, std::memory_order_relaxed);
    LastSkill.store(skillId, std::memory_order_relaxed);
    LastLife.store(life, std::memory_order_relaxed);
    LastPercent.store(percent, std::memory_order_relaxed);
    LastRoll.store(roll, std::memory_order_relaxed);
    LastDamage.store(damage, std::memory_order_relaxed);

    if (!ReportedFirst.exchange(true, std::memory_order_relaxed)) {
        D2RL::LogInfoF(Context,
            "AuraPercentLife: first percent hit: skill %d, target life %d, %d%%, roll %d -> %d "
            "(packet values are 256ths).",
            skillId, life, percent, roll, damage);
    }
}

// 0x560F60's own signature. The original runs first, so its close-range bonus
// (vanilla ids only) is part of the roll the percent is compared against.
auto __fastcall HookedAuraBonus(void* game, void* caster, void* target, std::int32_t skillId,
        std::int32_t skillLevel, std::uint8_t* packet) noexcept -> std::int64_t {
    const std::int64_t result =
        OriginalAuraBonus(game, caster, target, skillId, skillLevel, packet);
    if (Active.load(std::memory_order_acquire)) {
        ApplyPercentLife(game, caster, target, skillId, skillLevel, packet);
    }
    return result;
}

// ---------------------------------------------------------------------------
// Verification and installation
// ---------------------------------------------------------------------------

auto Verify(std::uint64_t rva, const std::uint8_t* expected, std::size_t size,
        const char* label) noexcept -> bool {
    if (Context->CheckExpectedBytes(rva, expected, static_cast<std::uint32_t>(size))) return true;
    D2RL::LogErrorF(Context,
        "AuraPercentLife: %s at 0x%llX does not match the verified D2R image, or another "
        "plugin already owns it. Refusing to load.",
        label, static_cast<unsigned long long>(rva));
    return false;
}

auto VerifyNativeContract() noexcept -> bool {
    return Verify(AuraBonusRva, AuraBonusEntry, sizeof(AuraBonusEntry),
                "the close-range aura bonus (hook site)")
        && Verify(FireShockCallWindowRva, FireShockCallWindow, sizeof(FireShockCallWindow),
                "the srvdofunc 66 callback's damage block")
        && Verify(FreezeCallWindowRva, FreezeCallWindow, sizeof(FreezeCallWindow),
                "the srvdofunc 81 callback's damage block")
        && Verify(StaticFieldBodyRva, StaticFieldBody, sizeof(StaticFieldBody),
                "Static Field's argument setup")
        && Verify(ElementSwitchRva, ElementSwitch, sizeof(ElementSwitch),
                "AddElementalDamage's element cases")
        && Verify(ElementJumpTableRva, ElementJumpTable, sizeof(ElementJumpTable),
                "AddElementalDamage's jump table")
        && Verify(RandomElementTableRva, RandomElementTable, sizeof(RandomElementTable),
                "the random element table")
        && Verify(GetSkillsTxtRecordRva, GetSkillsTxtRecordEntry, sizeof(GetSkillsTxtRecordEntry),
                "the skills.txt record getter")
        && Verify(EvaluateFormulaRva, EvaluateFormulaEntry, sizeof(EvaluateFormulaEntry),
                "the skill formula evaluator")
        && Verify(GetUnitStatRva, GetUnitStatThunk, sizeof(GetUnitStatThunk),
                "the stat getter thunk to D2RCore")
        && Verify(GetMaxLifeRva, GetMaxLifeThunk, sizeof(GetMaxLifeThunk),
                "the max life thunk to D2RCore")
        && Verify(GetDifficultyContextRva, DifficultyContextEntry, sizeof(DifficultyContextEntry),
                "the difficulty context builder")
        && Verify(GetDifficultyLevelsRva, DifficultyLevelsEntry, sizeof(DifficultyLevelsEntry),
                "the difficultylevels.txt record getter");
}

auto InstallHook() noexcept -> bool {
    if (!Context->InstallInlineHook(AuraBonusRva, AuraBonusEntry,
            static_cast<std::uint32_t>(sizeof(AuraBonusEntry)), &HookedAuraBonus,
            &OriginalAuraBonus)
        || OriginalAuraBonus == nullptr) {
        Context->LogError(
            "AuraPercentLife: D2RLoader refused the hook on 0x560F60 (another plugin may own it).");
        return false;
    }
    Active.store(true, std::memory_order_release);
    State = HookState::Armed;
    return true;
}

// ---------------------------------------------------------------------------
// Console command
// ---------------------------------------------------------------------------

auto StateName() noexcept -> const char* {
    switch (State) {
    case HookState::DisabledByConfig: return "disabled by config";
    case HookState::Armed:            return "armed";
    default:                          return "not loaded";
    }
}

auto __cdecl StatusCommand(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command,
        void*) noexcept -> D2RL::ConsoleCommandResult {
    if (command == nullptr || command->plugin == nullptr) return D2RL::ConsoleCommandResult::Failed;

    char message[512];
    std::snprintf(message, sizeof(message),
        "Aura Percent Life Damage: %s | calc%d | StaticFieldMin %s | targets with a percent %llu | "
        "percent won %llu | roll kept %llu | below StaticFieldMin %llu | last percent hit: skill %d, "
        "life %d, %d%%, roll %d -> %d (256ths)",
        StateName(), Settings.calcColumn, Settings.staticFieldMin ? "on" : "off",
        static_cast<unsigned long long>(TargetsChecked.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(PercentWon.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(RollKept.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(FloorSkipped.load(std::memory_order_relaxed)),
        LastSkill.load(std::memory_order_relaxed), LastLife.load(std::memory_order_relaxed),
        LastPercent.load(std::memory_order_relaxed), LastRoll.load(std::memory_order_relaxed),
        LastDamage.load(std::memory_order_relaxed));
    command->plugin->WriteConsoleMessage(message);
    return D2RL::ConsoleCommandResult::Handled;
}

void RegisterStatusCommand() noexcept {
    if (!Context->RegisterConsoleCommand("aurapercent", &StatusCommand,
            "Reports Aura Percent Life Damage status, counters and the last percent hit.")) {
        Context->LogWarn("AuraPercentLife: the status console command was refused.");
    }
}

constexpr D2RL::PluginInfo Info{
    .infoSize    = D2RL::PluginInfoSize,
    .abiVersion  = D2RL_PLUGIN_ABI_VERSION,
    .id          = "celestialrayone.aura-percent-life",
    .name        = "Aura Percent Life Damage",
    .version     = "1.0.0",
    .author      = "CelestialRayOne",
    .description = "srvdofunc 66 and 81 auras deal the higher of their EMin..EMax roll and a calc "
                   "column percent of each target's current life, the way Static Field does.",
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
    GetSkillsTxtRecord   = reinterpret_cast<GetSkillsTxtRecordFn>(Base + GetSkillsTxtRecordRva);
    EvaluateFormula      = reinterpret_cast<EvaluateFormulaFn>(Base + EvaluateFormulaRva);
    GetUnitStat          = reinterpret_cast<GetUnitStatFn>(Base + GetUnitStatRva);
    GetMaxLife           = reinterpret_cast<GetMaxLifeFn>(Base + GetMaxLifeRva);
    GetDifficultyContext = reinterpret_cast<GetDifficultyContextFn>(Base + GetDifficultyContextRva);
    GetDifficultyLevels  = reinterpret_cast<GetDifficultyLevelsFn>(Base + GetDifficultyLevelsRva);

    ReadConfiguration();

    if (!Settings.enabled) {
        State = HookState::DisabledByConfig;
        Context->LogInfo("AuraPercentLife: disabled by configuration.");
        RegisterStatusCommand();
        return true;
    }

    if (!VerifyNativeContract()) return false;
    if (!InstallHook()) return false;

    D2RL::LogInfoF(Context,
        "AuraPercentLife: %s. Percent from calc%d, StaticFieldMin %s, hook on 0x560F60 "
        "(srvdofunc 66 and 81 per-target damage).",
        StateName(), Settings.calcColumn, Settings.staticFieldMin ? "on" : "off");

    RegisterStatusCommand();
    return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
    Active.store(false, std::memory_order_release);
}

}  // namespace CelestialRayOne::AuraPercentLife
