// Attack Rating
//
// Replaces the attack rating versus defense hit-chance formula with a
// configurable curve and configurable minimum and maximum hit chance. One
// formula drives the server-side hit roll and both hit-chance lines on the
// Character Screen, so the screen shows the chance the roll uses.
//
// Port of the D2R 2.4 attack rating overhaul (combat hook 329CB9, screen hook
// BFE7F8, server-accurate screen inputs BFE796, live defense export 1A2B37) to
// D2R 3.3. Every address, register and stack slot below was read out of the
// live D2RLoader.exe image and disassembled before being relied on.
//
// ---------------------------------------------------------------------------
// Combat: the hit roll, sub_14044BA60 (attacker, defender, skillAr, bMissile)
// ---------------------------------------------------------------------------
//   Shared by every attacker and defender. Player attackers: AR = 3483C0
//   (tohit + 5*dex - 35 + charstats ToHitFactor), 4503C0 applies stats
//   115/116/123/124, then the AR% bucket (skill ToHit, mastery, stat 119 and
//   the stat 179 per-monster-type loop at 44BB94). Monster attackers:
//   skillAr + tohit + 5*dex, times stat 119.
//
//     44BD0E  lea eax,[rdx+rbp]         AR
//     44BD11  .. 44BD2E                 negative Def moves onto AR, negative
//                                       AR onto Def, both floored at 0
//     44BD31  lea ecx,[rax+rbx]         <- hook: eax = AR, ebx = Def, both >= 0
//     44BD38  imul eax,eax,64h / idiv   100 * AR / (AR + Def)
//     44BD40  .. 44BD62                 2 * ratio * aLvl / (aLvl + dLvl), 5..95
//     44BD65  mov rcx,rsi               <- rejoin with the chance in edi
//     44BD68  call 34A1E0               seed, then roll = seed % 100
//     44BDBC  cmp ebx,edi / jge         hit when roll < chance
//
//   rsi = attacker, r14 = defender, r13d = attacker level (stat 12),
//   [rsp+38h] = defender level. ebp is 0 from 44BD17/44BD1D and the
//   prevent-heal block at 44BE41/44BE50 relies on that; the C++ call keeps
//   it. ebx is overwritten at 44BD6F. No branch from outside the function
//   body lands in 44BD32..44BD64, and [rsp+20h]/[rsp+28h] are only ever
//   outgoing argument slots.
//
// ---------------------------------------------------------------------------
// Character Screen, average chance to hit: sub_141514700 (slotAr)
// ---------------------------------------------------------------------------
//   The tooltip builder 14E7FE0 passes slotAr = 28A590(player, slot). Through
//   27FB60 and 28D160 that is 3483C0(player) * (1 + (mastery + stat 119 +
//   skill ToHit) / 100).
//
//     15147C8  mov [rbp-31h],eax        monster class (monstats row)
//     15147E9  movsx edi,word [...]     monster level, raised to the terror
//                                       zone level at 151481B
//     1514867  call 3A26C0              monster archetype, defense at [rbp-1]
//     1514873  mov ebx,[rbp-1]          Def
//     151486C  .. 1514898               screen-only Def = 10 * Def / 12
//     151489A  mov rcx,r12              <- hook: r14d = slotAr, ebx = Def,
//                                       r12 = player, edi = monster level
//     15148AA  .. 151493B               players: slotAr += ToHitFactor again
//     151493E  .. 15149B3               restores r15,rsi,rbx,r13,r12,rdi,
//                                       ratio and level scaling
//     15149C0  .. 15149D3               5..95
//     15149D6  mov rcx,[rbp+2Fh]        <- rejoin: cookie, add rsp, pops, ret
//
//   The hook restores the six registers itself before jumping to 15149D6.
//   The only branches into 151489A..15149D5 target 151489A itself.
//
// ---------------------------------------------------------------------------
// Character Screen, average chance to hit you: sub_1415149F0 ()
// ---------------------------------------------------------------------------
//   Called from 14E8242; the result goes to the formatter 1514CA0.
//
//     1514A59  mov [rbp-71h],ecx        player Def = 347B10 + stat 33
//     1514A9E  movsx r13d,ax            monster class
//     1514ABA  movsx ebx,word [...]     monster level, terror zone max after
//     1514B34  call 3A26C0              monster archetype, AR at [rbp-5]
//     1514BF2  .. 1514C1D               screen-only AR = 10 * AR / 15
//     1514C1F  mov r8d,[rbp-71h]        <- hook: edx = AR, r14 = player
//     1514C72  mov rcx,[rbp+27h]        <- rejoin: the epilogue restores all
//
//   Branches into the replaced bytes come from 1514BF9 and 1514BFF, both to
//   1514C1F itself; 1514AB4 targets the rejoin point.
//
// ---------------------------------------------------------------------------
// Character Screen clamps
// ---------------------------------------------------------------------------
//   14E801A  jne                                   hides the line at 0%
//   14E8066  cmp esi,5 / mov esi,5 / mov eax,5Fh   average chance to hit
//   1514CBD  cmp ecx,5 / mov edi,5 / mov eax,5Fh   average chance to hit you
//   The immediates take min_chance and max_chance and the jne becomes a jmp,
//   so a 0% chance is shown instead of dropping the line. The witnesses mask
//   these fields and the combat clamp immediates at 44BD45/44BD57, because
//   Hit Chance Bounds (ruffneckk-hit-chance-0-100.json) may own them.
//
// ---------------------------------------------------------------------------
// Server-accurate average chance to hit
// ---------------------------------------------------------------------------
//   4503C0 (attacker, defender, &AR, &Def) reads only the defender's type
//   (+0), class (+4), MonsterData (+10h, null-checked) and data-table
//   context (+1BDh), through 38E870, 3AEFF0, 3AF240, 3AF190 and 3AF2C0. A
//   zeroed stand-in with type 1, the screen's class and the player's context
//   byte is therefore a plain monster of that class. The stat 179 loop is
//   44BB94..44BC08 replayed: layer matched against monstats +50h by 449FB0.
//
//   AR = slotAr * (B + flat) / B + (B + flat) * p179 / 100 with B = 3483C0,
//   which drops the doubled ToHitFactor and adds what the roll adds.
//   Per-instance flags (superunique, unique) cannot exist on a monster type;
//   the live defense below supplies them after the first hit.
//
// ---------------------------------------------------------------------------
// Live values
// ---------------------------------------------------------------------------
//   Each roll by a player against a monster stores that monster's class and
//   final Def; each roll by a monster against a player stores its class and
//   final AR. The screen uses them while its monster class matches.
//
// ---------------------------------------------------------------------------
// Skill attack rating on missiles (vanilla bug, port of the 2.4 fix at 315B5D)
// ---------------------------------------------------------------------------
//   The missile resolver 4639A0 passes the MISSILE's own stat 19 as the skill
//   AR% bonus (463C4B..463C5F). Missile creation sub_1405371A0 (game, params)
//   writes that stat only when the creator set params flag 0x1000:
//
//     537B23  mov eax,[r14]             <- hook: r14 = params, r15 = missile
//     537B26  bt eax,0Ch / jae 537B42   flag 0x1000 clear: no stat 19
//     537B2C  mov r8d,[r14+58h]         params attack rating
//     537B3A  call 2F7D10               set unit stat (missile, 19, AR, 0)
//     537B3F  mov eax,[r14]
//     537B42  bt eax,11h                <- rejoin, expects eax = flags
//
//   CreateSkillMissile 4333F0 sets the flag for players from 339040 (unit,
//   skill, level), the skills.txt ToHit/LevToHit/ToHitCalc evaluator
//   (4336B5..4336DF). Missiles that skill functions build themselves, such as
//   Multiple Shot (srvdofunc 8), never get it. The hook keeps the flagged
//   path exactly and, for player owners only, evaluates 339040 from params
//   +3Ch skill and +40h level when the flag is clear. Params +8 is the owner.
//   Skill ids are bounds-checked against the skills table count (tables
//   +11B8h, as 097790 does) so the evaluator's assert path is never reached.
//   The params struct is never modified, so a builder that reuses it for its
//   next missile cannot pick up a stale value. No branch from outside the
//   hook lands in 537B24..537B41, and the function has no indirect jumps.
//
// ---------------------------------------------------------------------------
// Hook shape
// ---------------------------------------------------------------------------
//   Each hook is an in-place rewrite that loads the arguments, calls a near
//   relay (jmp qword ptr [rip+0]) into C++ and jumps on. The call is made
//   from the host function's own frame, so its unwind data covers it. On
//   unload the relays are first pointed at native stubs that do what the
//   original code did, then the original bytes go back.

#include <D2RLPlugin/api.h>

// Windows.h defines min/max as macros unless NOMINMAX is set.
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
#include <climits>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>

namespace CelestialRayOne::AttackRating {
namespace {

// ---------------------------------------------------------------------------
// The formula. attack-rating-curve.html carries a line-for-line copy between
// the same markers; change both together.
// ---------------------------------------------------------------------------

// BEGIN FORMULA
struct CurveSettings {
    double       pivotRatio      = 5.0;
    double       steepness       = 5.0;
    double       tail            = 3.0;
    double       lateTail        = 1.0;
    double       blendStartRatio = 30.0;
    double       blendEndRatio   = 100.0;
    double       snapTo100At     = 99.5;
    std::int32_t minChance       = 5;
    std::int32_t maxChance       = 95;
    bool         levelScaling    = false;
};

inline auto ClampSetting(double value, double low, double high, double fallback) noexcept
        -> double {
    if (!std::isfinite(value)) return fallback;
    return std::clamp(value, low, high);
}

inline void SanitizeCurveSettings(CurveSettings& s) noexcept {
    const CurveSettings defaults{};
    s.pivotRatio      = ClampSetting(s.pivotRatio, 0.01, 1000.0, defaults.pivotRatio);
    s.steepness       = ClampSetting(s.steepness, 0.1, 50.0, defaults.steepness);
    s.tail            = ClampSetting(s.tail, 0.01, 1000.0, defaults.tail);
    s.lateTail        = ClampSetting(s.lateTail, 0.0, 1000.0, defaults.lateTail);
    s.blendStartRatio = ClampSetting(s.blendStartRatio, 0.0, 100000.0, defaults.blendStartRatio);
    s.blendEndRatio   = ClampSetting(s.blendEndRatio, 0.0, 100000.0, defaults.blendEndRatio);
    s.snapTo100At     = ClampSetting(s.snapTo100At, 50.0, 100.0, defaults.snapTo100At);
    s.minChance       = std::clamp(s.minChance, 0, 100);
    s.maxChance       = std::clamp(s.maxChance, 0, 100);
    if (s.minChance > s.maxChance) std::swap(s.minChance, s.maxChance);
}

// Whole percent from the curve alone. Inputs are already non-negative.
inline auto CurvePercent(double attackRating, double defense, const CurveSettings& s) noexcept
        -> std::int32_t {
    if (defense <= 0.0) return 100;
    if (attackRating <= 0.0) return 0;
    const double ratio = attackRating / defense;
    double value = 0.0;
    if (ratio < s.pivotRatio) {
        value = 100.0 / (1.0 + std::pow(s.pivotRatio / ratio, s.steepness));
    } else {
        const double above = ratio - s.pivotRatio;
        const double early = 50.0 + 50.0 * above / (above + s.tail);
        value = early;
        if (s.lateTail > 0.0) {
            double weight = 0.0;
            if (s.blendEndRatio > s.blendStartRatio) {
                const double t = std::clamp(
                    (ratio - s.blendStartRatio) / (s.blendEndRatio - s.blendStartRatio),
                    0.0, 1.0);
                weight = t * t * (3.0 - 2.0 * t);
            } else if (ratio >= s.blendStartRatio) {
                weight = 1.0;
            }
            if (weight > 0.0) {
                const double late = 50.0 + 50.0 * above / (above + s.lateTail);
                value = early + weight * (late - early);
            }
        }
    }
    if (value >= s.snapTo100At) return 100;
    return static_cast<std::int32_t>(std::clamp(std::floor(value + 1e-11), 0.0, 100.0));
}

// The game's own handling of negative inputs: a negative defense is added to
// the attack rating, a negative attack rating to the defense, then both are
// floored at 0.
inline void NormalizeInputs(double& attackRating, double& defense) noexcept {
    if (defense < 0.0) {
        attackRating -= defense;
        defense = 0.0;
    }
    if (attackRating < 0.0) {
        defense -= attackRating;
        attackRating = 0.0;
    }
    if (defense < 0.0) defense = 0.0;
}

inline auto FinalChance(double attackRating, double defense, std::int32_t attackerLevel,
        std::int32_t defenderLevel, const CurveSettings& s) noexcept -> std::int32_t {
    NormalizeInputs(attackRating, defense);
    std::int64_t chance = CurvePercent(attackRating, defense, s);
    if (s.levelScaling) {
        const std::int64_t levels = static_cast<std::int64_t>(attackerLevel) + defenderLevel;
        if (levels > 0) chance = 2 * chance * attackerLevel / levels;
    }
    return static_cast<std::int32_t>(
        std::clamp<std::int64_t>(chance, s.minChance, s.maxChance));
}
// END FORMULA

// ---------------------------------------------------------------------------
// Native anchors. Each callee is proven by a witnessed call site: the rel32
// of every call below sits inside a byte window checked at load.
// ---------------------------------------------------------------------------

constexpr std::uint64_t GetUnitStatRva            = 0x2F5020;  // 1514997
constexpr std::uint64_t PlayerBaseAttackRatingRva = 0x3483C0;  // 44BB13
constexpr std::uint64_t ApplyTargetModifiersRva   = 0x4503C0;  // 44BB2C
constexpr std::uint64_t GetMonStatsRecordRva      = 0x0976E0;  // 44BB9E, 15147CB
constexpr std::uint64_t GetLayeredStatsRva        = 0x2F84B0;  // 44BBC6
constexpr std::uint64_t MonTypeMatchesRva         = 0x449FB0;  // 44BBF5
constexpr std::uint64_t GetDataTablesRva          = 0x300A90;  // 15148CD, 0977A2
constexpr std::uint64_t SetUnitStatRva            = 0x2F7D10;  // 537B3A
constexpr std::uint64_t SkillToHitRva             = 0x339040;  // 4336BE

constexpr std::size_t   UnitTypeOffset        = 0x000;
constexpr std::size_t   UnitClassOffset       = 0x004;
constexpr std::size_t   UnitDataContextOffset = 0x1BD;
constexpr std::uint32_t UnitPlayer            = 0;
constexpr std::uint32_t UnitMonster           = 1;
constexpr std::size_t   StandInUnitBytes      = 0x400;

constexpr std::size_t MonStatsMonTypeOffset      = 0x50;
constexpr std::size_t CharStatsRecordsOffset     = 0x1240;
constexpr std::size_t CharStatsCountOffset       = 0x1248;
constexpr std::size_t CharStatsRecordBytes       = 0xD0;
constexpr std::size_t CharStatsToHitFactorOffset = 0x38;
constexpr std::size_t SkillsCountOffset          = 0x11B8;

constexpr std::size_t   MissileParamsFlagsOffset        = 0x00;
constexpr std::size_t   MissileParamsOwnerOffset        = 0x08;
constexpr std::size_t   MissileParamsSkillOffset        = 0x3C;
constexpr std::size_t   MissileParamsSkillLevelOffset   = 0x40;
constexpr std::size_t   MissileParamsAttackRatingOffset = 0x58;
constexpr std::uint32_t MissileParamsAttackRatingFlag   = 0x1000;

constexpr std::int32_t  StatLevel                 = 12;
constexpr std::int32_t  StatToHit                 = 19;
constexpr std::uint32_t StatAttackRatingVsMonType = 179;
constexpr std::uint32_t LayeredStatCapacity       = 128;
constexpr std::size_t   LayeredStatEntryBytes     = 8;

using GetUnitStatFn = std::int32_t (*)(void* unit, std::int32_t statId, std::uint16_t layer);
using PlayerBaseAttackRatingFn = std::int32_t (*)(void* player);
using ApplyTargetModifiersFn =
    void (*)(void* attacker, void* defender, std::int32_t* attackRating, std::int32_t* defense);
using GetMonStatsRecordFn = void* (*)(std::uint8_t context, std::int32_t monsterClass);
using GetLayeredStatsFn =
    std::int32_t (*)(void* unit, std::uint32_t statId, void* entries, std::uint32_t capacity);
using MonTypeMatchesFn = std::uint32_t (*)(std::uint8_t context, std::int32_t layer, std::int32_t monType);
using GetDataTablesFn = void* (*)(std::uint8_t context);
using SetUnitStatFn =
    void (*)(void* unit, std::int32_t statId, std::int32_t value, std::int32_t layer);
using SkillToHitFn = std::int32_t (*)(void* unit, std::int32_t skillId, std::int32_t skillLevel);

// ---------------------------------------------------------------------------
// Verified bytes (generated from the D2R 3.3 image)
// ---------------------------------------------------------------------------

// RVA 0x44BD0E, 95 bytes. Normalization, hook span and rejoin of the hit roll.
constexpr std::uint8_t CombatSiteWindow[95]{
    0x8D, 0x04, 0x2A, 0x85, 0xDB, 0x79, 0x08, 0x2B, 0xC3, 0x33, 0xED, 0x8B,
    0xDD, 0xEB, 0x02, 0x33, 0xED, 0x85, 0xC0, 0x79, 0x04, 0x2B, 0xD8, 0x8B,
    0xC5, 0x85, 0xDB, 0xBA, 0x64, 0x00, 0x00, 0x00, 0x0F, 0x48, 0xDD, 0x8D,
    0x0C, 0x18, 0x85, 0xC9, 0x74, 0x08, 0x6B, 0xC0, 0x64, 0x99, 0xF7, 0xF9,
    0x8B, 0xD0, 0x8B, 0x4C, 0x24, 0x38, 0xBF, 0x5F, 0x00, 0x00, 0x00, 0x41,
    0x03, 0xCD, 0x41, 0x0F, 0xAF, 0xD5, 0x8D, 0x04, 0x12, 0x99, 0xF7, 0xF9,
    0xB9, 0x05, 0x00, 0x00, 0x00, 0x3B, 0xC1, 0x0F, 0x4F, 0xC8, 0x3B, 0xCF,
    0x0F, 0x4C, 0xF9, 0x48, 0x8B, 0xCE, 0xE8, 0x73, 0xE4, 0xEF, 0xFF,
};

// RVA 0x44BB13, 247 bytes. Player AR, 4503C0 and the stat 179 loop (proves the natives).
constexpr std::uint8_t CombatHelpersWindow[247]{
    0xE8, 0xA8, 0xC8, 0xEF, 0xFF, 0x4C, 0x8D, 0x4C, 0x24, 0x30, 0x89, 0x44,
    0x24, 0x34, 0x4C, 0x8D, 0x44, 0x24, 0x34, 0x49, 0x8B, 0xD6, 0x48, 0x8B,
    0xCE, 0xE8, 0x8F, 0x48, 0x00, 0x00, 0x85, 0xED, 0x75, 0x20, 0x48, 0x8B,
    0xCE, 0xE8, 0x73, 0x87, 0xFD, 0xFF, 0x48, 0x85, 0xC0, 0x74, 0x13, 0x45,
    0x33, 0xC9, 0x45, 0x33, 0xC0, 0x48, 0x8B, 0xD0, 0x48, 0x8B, 0xCE, 0xE8,
    0x9D, 0x19, 0xEF, 0xFF, 0x8B, 0xF8, 0x45, 0x33, 0xC0, 0x48, 0x8B, 0xCE,
    0x41, 0x8D, 0x50, 0x77, 0xE8, 0xFC, 0xA0, 0xEA, 0xFF, 0x41, 0x03, 0xC7,
    0x49, 0x8B, 0xCE, 0x03, 0xF8, 0xE8, 0x5F, 0xFE, 0xEF, 0xFF, 0x83, 0xF8,
    0x01, 0x0F, 0x85, 0x90, 0x00, 0x00, 0x00, 0x41, 0xB8, 0xFE, 0x0C, 0x00,
    0x00, 0x48, 0x8D, 0x15, 0xA9, 0xED, 0x8C, 0x01, 0x49, 0x8B, 0xCE, 0xE8,
    0xD1, 0xDC, 0xEF, 0xFF, 0x49, 0x8B, 0xCE, 0x8B, 0xD8, 0xE8, 0x47, 0xE5,
    0xEF, 0xFF, 0x8B, 0xD3, 0x0F, 0xB6, 0xC8, 0xE8, 0x3D, 0xBB, 0xC4, 0xFF,
    0x48, 0x85, 0xC0, 0x74, 0x62, 0x0F, 0xBF, 0x40, 0x50, 0x66, 0x85, 0xC0,
    0x7E, 0x59, 0x41, 0xB9, 0x80, 0x00, 0x00, 0x00, 0x4C, 0x8D, 0x44, 0x24,
    0x70, 0x48, 0x8B, 0xCE, 0x44, 0x8B, 0xE0, 0x41, 0x8D, 0x51, 0x33, 0xE8,
    0xE5, 0xC8, 0xEA, 0xFF, 0x4C, 0x63, 0xF8, 0x85, 0xC0, 0x7E, 0x38, 0x33,
    0xED, 0x0F, 0x1F, 0x40, 0x00, 0x0F, 0x1F, 0x84, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x0F, 0xB7, 0x5C, 0xEC, 0x70, 0x48, 0x8B, 0xCE, 0xE8, 0xF3, 0xE4,
    0xEF, 0xFF, 0x45, 0x8B, 0xC4, 0x8B, 0xD3, 0x0F, 0xB6, 0xC8, 0xE8, 0xB6,
    0xE3, 0xFF, 0xFF, 0x85, 0xC0, 0x74, 0x04, 0x03, 0x7C, 0xEC, 0x74, 0x48,
    0xFF, 0xC5, 0x49, 0x3B, 0xEF, 0x7C, 0xD6,
};

// RVA 0x1514700, 701 bytes. Average chance to hit calculator up to its clamp.
constexpr std::uint8_t ChanceToHitWindow[701]{
    0x40, 0x55, 0x41, 0x56, 0x48, 0x8D, 0x6C, 0x24, 0xB1, 0x48, 0x81, 0xEC,
    0xC8, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x05, 0xB1, 0x6B, 0x4B, 0x01, 0x48,
    0x33, 0xC4, 0x48, 0x89, 0x45, 0x2F, 0x44, 0x8B, 0xF1, 0x85, 0xC9, 0x75,
    0x07, 0x33, 0xC0, 0xE9, 0xAA, 0x02, 0x00, 0x00, 0x48, 0x89, 0x9C, 0x24,
    0xE0, 0x00, 0x00, 0x00, 0x48, 0x89, 0xB4, 0x24, 0xE8, 0x00, 0x00, 0x00,
    0x48, 0x89, 0xBC, 0x24, 0xF0, 0x00, 0x00, 0x00, 0x4C, 0x89, 0xA4, 0x24,
    0xC0, 0x00, 0x00, 0x00, 0x4C, 0x89, 0xAC, 0x24, 0xB8, 0x00, 0x00, 0x00,
    0x4C, 0x89, 0xBC, 0x24, 0xB0, 0x00, 0x00, 0x00, 0xE8, 0x6F, 0x6B, 0xB7,
    0xFE, 0x8B, 0xC8, 0x8B, 0xF8, 0xE8, 0x16, 0x5D, 0xB8, 0xFE, 0x48, 0x8B,
    0xD0, 0xC7, 0x45, 0xCB, 0x64, 0x00, 0x00, 0x00, 0x48, 0x8D, 0x4D, 0xE7,
    0x4C, 0x8B, 0xE0, 0xE8, 0x90, 0x5E, 0xB8, 0xFE, 0xF2, 0x0F, 0x10, 0x00,
    0x8B, 0x48, 0x08, 0xF2, 0x0F, 0x11, 0x45, 0xD7, 0x44, 0x0F, 0xB6, 0x7D,
    0xD7, 0x89, 0x4D, 0xDF, 0x41, 0x83, 0xFF, 0x03, 0x72, 0x12, 0x48, 0x8D,
    0x4D, 0xC7, 0xC6, 0x45, 0xC7, 0x00, 0xE8, 0x29, 0xFF, 0xFF, 0xFF, 0x84,
    0xC0, 0x74, 0x01, 0xCC, 0x49, 0x8B, 0xCC, 0xE8, 0x2C, 0x59, 0xE3, 0xFE,
    0x8B, 0xCF, 0x88, 0x45, 0xC8, 0x0F, 0xB6, 0xF0, 0xE8, 0x9F, 0xD0, 0xBD,
    0xFE, 0x98, 0x40, 0x0F, 0xB6, 0xCE, 0x8B, 0xD0, 0x89, 0x45, 0xCF, 0xE8,
    0x10, 0x2F, 0xB8, 0xFE, 0x48, 0x8B, 0xF0, 0x48, 0x85, 0xC0, 0x75, 0x11,
    0x48, 0x8D, 0x4D, 0xC7, 0x88, 0x45, 0xC7, 0xE8, 0x8C, 0xFE, 0xFF, 0xFF,
    0x84, 0xC0, 0x74, 0x01, 0xCC, 0x42, 0x0F, 0xBF, 0xBC, 0x7E, 0xFC, 0x00,
    0x00, 0x00, 0x49, 0x8B, 0xCC, 0xE8, 0x46, 0x6C, 0xE3, 0xFE, 0x48, 0x85,
    0xC0, 0x74, 0x24, 0x48, 0x8B, 0xC8, 0xE8, 0x09, 0xB4, 0xDD, 0xFE, 0x8B,
    0xD0, 0x48, 0x8D, 0x4D, 0xE7, 0xE8, 0xCE, 0xC3, 0xB7, 0xFE, 0x80, 0x7D,
    0xEB, 0x00, 0x74, 0x07, 0x39, 0x7D, 0xE7, 0x0F, 0x4F, 0x7D, 0xE7, 0xC6,
    0x45, 0xEB, 0x00, 0x0F, 0x57, 0xC0, 0x33, 0xC0, 0x0F, 0x11, 0x45, 0xF7,
    0x48, 0x89, 0x45, 0x27, 0x0F, 0x11, 0x45, 0x07, 0x0F, 0x11, 0x45, 0x17,
    0xE8, 0x03, 0x68, 0xB7, 0xFE, 0x8B, 0x55, 0xCF, 0x44, 0x8B, 0xC0, 0x0F,
    0xB6, 0x4D, 0xC8, 0x48, 0x8D, 0x45, 0xF7, 0x45, 0x33, 0xED, 0x45, 0x8B,
    0xCF, 0x44, 0x89, 0x6C, 0x24, 0x38, 0x48, 0x89, 0x44, 0x24, 0x30, 0xC7,
    0x44, 0x24, 0x28, 0x02, 0x00, 0x00, 0x00, 0x89, 0x7C, 0x24, 0x20, 0xE8,
    0x54, 0xDE, 0xE8, 0xFE, 0x80, 0xBE, 0x87, 0x00, 0x00, 0x00, 0x01, 0x8B,
    0x5D, 0xFF, 0x74, 0x22, 0x80, 0x7D, 0xD8, 0x01, 0x75, 0x1C, 0x45, 0x84,
    0xFF, 0x74, 0x17, 0x8D, 0x0C, 0x9B, 0xB8, 0xAB, 0xAA, 0xAA, 0x2A, 0x03,
    0xC9, 0xF7, 0xE9, 0x8B, 0xDA, 0xD1, 0xFB, 0x8B, 0xC3, 0xC1, 0xE8, 0x1F,
    0x03, 0xD8, 0x49, 0x8B, 0xCC, 0xE8, 0x2E, 0x71, 0xE3, 0xFE, 0x85, 0xC0,
    0x0F, 0x85, 0x94, 0x00, 0x00, 0x00, 0x41, 0xB8, 0xA1, 0x00, 0x00, 0x00,
    0x48, 0x8D, 0x15, 0xA9, 0xC5, 0xAC, 0x00, 0x49, 0x8B, 0xCC, 0xE8, 0xA1,
    0x4F, 0xE3, 0xFE, 0x49, 0x8B, 0xCC, 0x48, 0x63, 0xF0, 0xE8, 0x16, 0x58,
    0xE3, 0xFE, 0x0F, 0xB6, 0xC8, 0xE8, 0xBE, 0xC1, 0xDE, 0xFE, 0x4C, 0x8B,
    0xF8, 0x85, 0xF6, 0x78, 0x09, 0x48, 0x3B, 0xB0, 0x48, 0x12, 0x00, 0x00,
    0x72, 0x12, 0x48, 0x8D, 0x4D, 0xC8, 0xE8, 0x25, 0x0C, 0xB7, 0xFE, 0x84,
    0xC0, 0x74, 0x01, 0xCC, 0x85, 0xF6, 0x78, 0x4A, 0x48, 0x8B, 0xC6, 0x49,
    0x3B, 0xB7, 0x48, 0x12, 0x00, 0x00, 0x73, 0x3E, 0x49, 0x8D, 0xB7, 0x40,
    0x12, 0x00, 0x00, 0x48, 0x89, 0x45, 0xE7, 0x48, 0x3B, 0x46, 0x08, 0x72,
    0x1A, 0x48, 0x8D, 0x45, 0xE7, 0x48, 0x89, 0x75, 0xDF, 0x48, 0x8D, 0x4D,
    0xD7, 0x48, 0x89, 0x45, 0xD7, 0xE8, 0xEA, 0x17, 0xB7, 0xFE, 0x84, 0xC0,
    0x74, 0x01, 0xCC, 0x48, 0x69, 0x45, 0xE7, 0xD0, 0x00, 0x00, 0x00, 0x48,
    0x03, 0x06, 0x74, 0x06, 0x8B, 0x40, 0x38, 0x44, 0x03, 0xF0, 0x4C, 0x8B,
    0xBC, 0x24, 0xB0, 0x00, 0x00, 0x00, 0x41, 0x8B, 0xC5, 0x48, 0x8B, 0xB4,
    0x24, 0xE8, 0x00, 0x00, 0x00, 0x41, 0x8B, 0xCE, 0x2B, 0xCB, 0x85, 0xDB,
    0x0F, 0x49, 0xC3, 0x41, 0x0F, 0x49, 0xCE, 0x48, 0x8B, 0x9C, 0x24, 0xE0,
    0x00, 0x00, 0x00, 0x85, 0xC9, 0x8B, 0xD0, 0x44, 0x0F, 0x49, 0xE9, 0x2B,
    0xD1, 0x85, 0xC9, 0x0F, 0x49, 0xD0, 0x45, 0x85, 0xED, 0x75, 0x04, 0x85,
    0xD2, 0x74, 0x0E, 0x41, 0x6B, 0xC5, 0x64, 0x42, 0x8D, 0x0C, 0x2A, 0x99,
    0xF7, 0xF9, 0x89, 0x45, 0xCB, 0x45, 0x33, 0xC0, 0x49, 0x8B, 0xCC, 0x41,
    0x8D, 0x50, 0x0C, 0xE8, 0x84, 0x06, 0xDE, 0xFE, 0x4C, 0x8B, 0xAC, 0x24,
    0xB8, 0x00, 0x00, 0x00, 0x4C, 0x8B, 0xA4, 0x24, 0xC0, 0x00, 0x00, 0x00,
    0x8D, 0x0C, 0x38, 0x0F, 0xAF, 0x45, 0xCB, 0x48, 0x8B, 0xBC, 0x24, 0xF0,
    0x00, 0x00, 0x00, 0x03, 0xC0,
};

// RVA 0x15149D6, 23 bytes. Its epilogue.
constexpr std::uint8_t ChanceToHitTailWindow[23]{
    0x48, 0x8B, 0x4D, 0x2F, 0x48, 0x33, 0xCC, 0xE8, 0x6E, 0xC7, 0xDB, 0xFF,
    0x48, 0x81, 0xC4, 0xC8, 0x00, 0x00, 0x00, 0x41, 0x5E, 0x5D, 0xC3,
};

// RVA 0x15149F0, 687 bytes. Average chance to hit you calculator, whole function.
constexpr std::uint8_t ChanceToBeHitWindow[687]{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x48, 0x89,
    0x7C, 0x24, 0x18, 0x55, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57,
    0x48, 0x8D, 0x6C, 0x24, 0xC9, 0x48, 0x81, 0xEC, 0xF0, 0x00, 0x00, 0x00,
    0x48, 0x8B, 0x05, 0xAD, 0x68, 0x4B, 0x01, 0x48, 0x33, 0xC4, 0x48, 0x89,
    0x45, 0x27, 0xE8, 0xA9, 0x68, 0xB7, 0xFE, 0x8B, 0xC8, 0x8B, 0xF8, 0xE8,
    0x50, 0x5A, 0xB8, 0xFE, 0x48, 0x8B, 0xC8, 0xC7, 0x45, 0x8B, 0x64, 0x00,
    0x00, 0x00, 0x4C, 0x8B, 0xF0, 0xE8, 0xCE, 0x30, 0xE3, 0xFE, 0x45, 0x33,
    0xC0, 0x49, 0x8B, 0xCE, 0x8B, 0xD8, 0x41, 0x8D, 0x50, 0x21, 0xE8, 0xCD,
    0x05, 0xDE, 0xFE, 0x49, 0x8B, 0xD6, 0x8D, 0x0C, 0x18, 0x89, 0x4D, 0x8F,
    0x48, 0x8D, 0x4D, 0xDF, 0xE8, 0xAB, 0x5B, 0xB8, 0xFE, 0xF2, 0x0F, 0x10,
    0x00, 0xF2, 0x0F, 0x11, 0x45, 0x97, 0x44, 0x0F, 0xB6, 0x7D, 0x97, 0x41,
    0x83, 0xFF, 0x03, 0x72, 0x12, 0x48, 0x8D, 0x4D, 0x87, 0xC6, 0x45, 0x87,
    0x00, 0xE8, 0x8A, 0xFB, 0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x01, 0xCC, 0x49,
    0x8B, 0xCE, 0xE8, 0x4D, 0x56, 0xE3, 0xFE, 0x8B, 0xCF, 0x44, 0x0F, 0xB6,
    0xE0, 0xE8, 0x12, 0xCE, 0xBD, 0xFE, 0x44, 0x0F, 0xBF, 0xE8, 0x41, 0x0F,
    0xB6, 0xCC, 0x41, 0x8B, 0xD5, 0xE8, 0x32, 0x2C, 0xB8, 0xFE, 0x48, 0x8B,
    0xF0, 0x48, 0x85, 0xC0, 0x0F, 0x84, 0xB8, 0x01, 0x00, 0x00, 0x42, 0x0F,
    0xBF, 0x9C, 0x78, 0xFC, 0x00, 0x00, 0x00, 0x49, 0x8B, 0xCE, 0xE8, 0x75,
    0x69, 0xE3, 0xFE, 0x48, 0x85, 0xC0, 0x74, 0x22, 0x48, 0x8B, 0xC8, 0xE8,
    0x38, 0xB1, 0xDD, 0xFE, 0x8B, 0xD0, 0x48, 0x8D, 0x4D, 0xDF, 0xE8, 0xFD,
    0xC0, 0xB7, 0xFE, 0x80, 0x7D, 0xE3, 0x00, 0x74, 0x09, 0x48, 0x8B, 0x45,
    0xDF, 0x3B, 0xC3, 0x0F, 0x4F, 0xD8, 0x0F, 0x57, 0xC0, 0x33, 0xC0, 0x0F,
    0x11, 0x45, 0xEF, 0x48, 0x89, 0x45, 0x1F, 0x0F, 0x11, 0x45, 0xFF, 0x0F,
    0x11, 0x45, 0x0F, 0xE8, 0x34, 0x65, 0xB7, 0xFE, 0x44, 0x8B, 0xC0, 0x33,
    0xFF, 0x89, 0x7C, 0x24, 0x38, 0x48, 0x8D, 0x45, 0xEF, 0x48, 0x89, 0x44,
    0x24, 0x30, 0x45, 0x8B, 0xCF, 0xC7, 0x44, 0x24, 0x28, 0x08, 0x00, 0x00,
    0x00, 0x41, 0x8B, 0xD5, 0x41, 0x0F, 0xB6, 0xCC, 0x89, 0x5C, 0x24, 0x20,
    0xE8, 0x87, 0xDB, 0xE8, 0xFE, 0x8B, 0x55, 0xFB, 0x8B, 0xC2, 0x85, 0xD2,
    0x0F, 0x85, 0xAC, 0x00, 0x00, 0x00, 0x0F, 0x57, 0xC0, 0x33, 0xC0, 0x48,
    0x89, 0x45, 0xD7, 0x0F, 0x11, 0x45, 0xEF, 0x0F, 0x11, 0x45, 0xFF, 0x0F,
    0x11, 0x45, 0x0F, 0xF2, 0x0F, 0x10, 0x45, 0xD7, 0xF2, 0x0F, 0x11, 0x45,
    0x1F, 0xE8, 0xD6, 0x64, 0xB7, 0xFE, 0x89, 0x7C, 0x24, 0x38, 0x44, 0x8B,
    0xC0, 0x48, 0x8D, 0x45, 0xEF, 0x45, 0x8B, 0xCF, 0x48, 0x89, 0x44, 0x24,
    0x30, 0x41, 0x8B, 0xD5, 0xC7, 0x44, 0x24, 0x28, 0x10, 0x00, 0x00, 0x00,
    0x41, 0x0F, 0xB6, 0xCC, 0x89, 0x5C, 0x24, 0x20, 0xE8, 0x2B, 0xDB, 0xE8,
    0xFE, 0x8B, 0x55, 0xFB, 0x8B, 0xC2, 0x85, 0xD2, 0x75, 0x54, 0x0F, 0x57,
    0xC0, 0x33, 0xC0, 0x48, 0x89, 0x45, 0xD7, 0x0F, 0x11, 0x45, 0xEF, 0x0F,
    0x11, 0x45, 0xFF, 0x0F, 0x11, 0x45, 0x0F, 0xF2, 0x0F, 0x10, 0x45, 0xD7,
    0xF2, 0x0F, 0x11, 0x45, 0x1F, 0xE8, 0x7E, 0x64, 0xB7, 0xFE, 0x89, 0x7C,
    0x24, 0x38, 0x44, 0x8B, 0xC0, 0x48, 0x8D, 0x45, 0xEF, 0x45, 0x8B, 0xCF,
    0x48, 0x89, 0x44, 0x24, 0x30, 0x41, 0x8B, 0xD5, 0xC7, 0x44, 0x24, 0x28,
    0x20, 0x00, 0x00, 0x00, 0x41, 0x0F, 0xB6, 0xCC, 0x89, 0x5C, 0x24, 0x20,
    0xE8, 0xD3, 0xDA, 0xE8, 0xFE, 0x8B, 0x55, 0xFB, 0x8B, 0xC2, 0x80, 0xBE,
    0x87, 0x00, 0x00, 0x00, 0x01, 0x74, 0x24, 0x80, 0x7D, 0x98, 0x01, 0x75,
    0x1E, 0x40, 0x38, 0x7D, 0x97, 0x74, 0x18, 0x8D, 0x0C, 0x80, 0xB8, 0x89,
    0x88, 0x88, 0x88, 0x03, 0xC9, 0xF7, 0xE9, 0x03, 0xD1, 0xC1, 0xFA, 0x03,
    0x8B, 0xC2, 0xC1, 0xE8, 0x1F, 0x03, 0xD0, 0x44, 0x8B, 0x45, 0x8F, 0x8B,
    0xCA, 0x41, 0x2B, 0xC8, 0x8B, 0xC7, 0x45, 0x85, 0xC0, 0x0F, 0x49, 0xCA,
    0x41, 0x0F, 0x49, 0xC0, 0x85, 0xC9, 0x8B, 0xD0, 0x0F, 0x49, 0xF9, 0x2B,
    0xD1, 0x85, 0xC9, 0x0F, 0x49, 0xD0, 0x85, 0xFF, 0x75, 0x04, 0x85, 0xD2,
    0x74, 0x0C, 0x6B, 0xC7, 0x64, 0x8D, 0x0C, 0x3A, 0x99, 0xF7, 0xF9, 0x89,
    0x45, 0x8B, 0x45, 0x33, 0xC0, 0x49, 0x8B, 0xCE, 0x41, 0x8D, 0x50, 0x0C,
    0xE8, 0xBB, 0x03, 0xDE, 0xFE, 0x8D, 0x0C, 0x03, 0x0F, 0xAF, 0x5D, 0x8B,
    0x8D, 0x04, 0x1B, 0x99, 0xF7, 0xF9, 0x48, 0x8B, 0x4D, 0x27, 0x48, 0x33,
    0xCC, 0xE8, 0xD2, 0xC4, 0xDB, 0xFF, 0x4C, 0x8D, 0x9C, 0x24, 0xF0, 0x00,
    0x00, 0x00, 0x49, 0x8B, 0x5B, 0x30, 0x49, 0x8B, 0x73, 0x38, 0x49, 0x8B,
    0x7B, 0x40, 0x49, 0x8B, 0xE3, 0x41, 0x5F, 0x41, 0x5E, 0x41, 0x5D, 0x41,
    0x5C, 0x5D, 0xC3,
};

// RVA 0x14E8000, 132 bytes. Average chance to hit formatter and clamp.
constexpr std::uint8_t ChanceToHitFormatWindow[132]{
    0x49, 0x8B, 0xC8, 0x41, 0x0F, 0xB6, 0xD1, 0x49, 0x8B, 0xD8, 0xE8, 0x81,
    0x25, 0xDA, 0xFE, 0x8B, 0xC8, 0xE8, 0xEA, 0xC6, 0x02, 0x00, 0x8B, 0xF0,
    0x85, 0xC0, 0x75, 0x37, 0x33, 0xF6, 0x48, 0x8D, 0x4F, 0x18, 0x48, 0x89,
    0x0F, 0x48, 0xBA, 0x0F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x48,
    0x89, 0x57, 0x10, 0x33, 0xD2, 0x48, 0x89, 0x77, 0x08, 0x40, 0x88, 0x31,
    0x48, 0x8B, 0xCF, 0xE8, 0xCC, 0xE1, 0xB8, 0xFE, 0x48, 0x8B, 0x0F, 0x40,
    0x88, 0x31, 0x48, 0x89, 0x77, 0x08, 0xE9, 0xA5, 0x01, 0x00, 0x00, 0x48,
    0x8B, 0xCB, 0x48, 0x89, 0xAC, 0x24, 0x70, 0x02, 0x00, 0x00, 0xE8, 0x7D,
    0x20, 0xE6, 0xFE, 0x0F, 0xB6, 0xE8, 0x83, 0xFE, 0x05, 0x7D, 0x07, 0xBE,
    0x05, 0x00, 0x00, 0x00, 0xEB, 0x0A, 0xB8, 0x5F, 0x00, 0x00, 0x00, 0x3B,
    0xF0, 0x0F, 0x4F, 0xF0, 0x48, 0x8B, 0xCB, 0xE8, 0x9C, 0x27, 0xBB, 0xFE,
};

// RVA 0x1514CA0, 73 bytes. Average chance to hit you formatter and clamp.
constexpr std::uint8_t ChanceToBeHitFormatWindow[73]{
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x48, 0x89,
    0x74, 0x24, 0x20, 0x57, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x30,
    0x4C, 0x8B, 0xF2, 0x8B, 0xF9, 0x83, 0xF9, 0x05, 0x7D, 0x07, 0xBF, 0x05,
    0x00, 0x00, 0x00, 0xEB, 0x0A, 0xB8, 0x5F, 0x00, 0x00, 0x00, 0x3B, 0xF8,
    0x0F, 0x4F, 0xF8, 0xE8, 0xF8, 0x65, 0xB7, 0xFE, 0x8B, 0xC8, 0x8B, 0xD8,
    0xE8, 0x9F, 0x57, 0xB8, 0xFE, 0x48, 0x8B, 0xE8, 0xE8, 0xC7, 0x86, 0xB7,
    0xFE,
};

// Written at 44BD31. rel32 filled at runtime.
constexpr std::uint8_t CombatHookCode[32]{
    0x89, 0xC1, 0x89, 0xDA, 0x49, 0x89, 0xF0, 0x4D, 0x89, 0xF1, 0x44, 0x89,
    0x6C, 0x24, 0x20, 0x8B, 0x44, 0x24, 0x38, 0x89, 0x44, 0x24, 0x28, 0xE8,
    0x00, 0x00, 0x00, 0x00, 0x89, 0xC7, 0xEB, 0x14,
};

constexpr std::uint32_t CombatHookRel32Offset = 24;

static_assert(sizeof(CombatHookCode) == 32 && CombatHookRel32Offset + 4 <= sizeof(CombatHookCode));

// Written at 151489A. rel32 filled at runtime.
constexpr std::uint8_t ChanceToHitHookCode[81]{
    0x44, 0x89, 0xF1, 0x89, 0xDA, 0x4D, 0x89, 0xE0, 0x44, 0x8B, 0x4D, 0xCF,
    0x89, 0x7C, 0x24, 0x20, 0x8B, 0x45, 0xFF, 0x89, 0x44, 0x24, 0x28, 0xE8,
    0x00, 0x00, 0x00, 0x00, 0x4C, 0x8B, 0xBC, 0x24, 0xB0, 0x00, 0x00, 0x00,
    0x48, 0x8B, 0xB4, 0x24, 0xE8, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x9C, 0x24,
    0xE0, 0x00, 0x00, 0x00, 0x4C, 0x8B, 0xAC, 0x24, 0xB8, 0x00, 0x00, 0x00,
    0x4C, 0x8B, 0xA4, 0x24, 0xC0, 0x00, 0x00, 0x00, 0x48, 0x8B, 0xBC, 0x24,
    0xF0, 0x00, 0x00, 0x00, 0xE9, 0xEB, 0x00, 0x00, 0x00,
};

constexpr std::uint32_t ChanceToHitHookRel32Offset = 24;

static_assert(sizeof(ChanceToHitHookCode) == 81 && ChanceToHitHookRel32Offset + 4 <= sizeof(ChanceToHitHookCode));

// Written at 1514C1F. rel32 filled at runtime.
constexpr std::uint8_t ChanceToBeHitHookCode[29]{
    0x89, 0xD1, 0x8B, 0x55, 0x8F, 0x4D, 0x89, 0xF0, 0x45, 0x89, 0xE9, 0x89,
    0x5C, 0x24, 0x20, 0x8B, 0x45, 0xFB, 0x89, 0x44, 0x24, 0x28, 0xE8, 0x00,
    0x00, 0x00, 0x00, 0xEB, 0x36,
};

constexpr std::uint32_t ChanceToBeHitHookRel32Offset = 23;

static_assert(sizeof(ChanceToBeHitHookCode) == 29 && ChanceToBeHitHookRel32Offset + 4 <= sizeof(ChanceToBeHitHookCode));

// lea r8d,[rcx+rdx]; mov eax,100; test r8d,r8d; jle done; imul eax,ecx,100; cdq; idiv r8d; done: ret
constexpr std::uint8_t FallbackStub[22]{
    0x44, 0x8D, 0x04, 0x11, 0xB8, 0x64, 0x00, 0x00, 0x00, 0x45, 0x85, 0xC0,
    0x7E, 0x07, 0x6B, 0xC1, 0x64, 0x99, 0x41, 0xF7, 0xF8, 0xC3,
};

// RVA 0x537B23, 58 bytes. Missile creation: the params flag 0x1000 stat 19 stamp and its rejoin.
constexpr std::uint8_t CreationStampWindow[58]{
    0x41, 0x8B, 0x06, 0x0F, 0xBA, 0xE0, 0x0C, 0x73, 0x16, 0x45, 0x8B, 0x46,
    0x58, 0x45, 0x33, 0xC9, 0x49, 0x8B, 0xCF, 0x41, 0x8D, 0x51, 0x13, 0xE8,
    0xD1, 0x01, 0xDC, 0xFF, 0x41, 0x8B, 0x06, 0x0F, 0xBA, 0xE0, 0x11, 0x73,
    0x15, 0x49, 0x8B, 0xCF, 0xE8, 0x30, 0x35, 0xE8, 0xFF, 0x83, 0xC8, 0x02,
    0x49, 0x8B, 0xCF, 0x8B, 0xD0, 0xE8, 0x73, 0x58, 0xE8, 0xFF,
};

// RVA 0x4336A2, 62 bytes. CreateSkillMissile: skill ToHit evaluation and the call into missile creation.
constexpr std::uint8_t SkillToHitCallWindow[62]{
    0x44, 0x0F, 0xB6, 0xC3, 0x48, 0x8B, 0xD7, 0x48, 0x8B, 0xCE, 0xE8, 0x6F,
    0x5C, 0x00, 0x00, 0x85, 0xC0, 0x7E, 0x2C, 0x45, 0x8B, 0xC6, 0x41, 0x8B,
    0xD7, 0x48, 0x8B, 0xCF, 0xE8, 0x7D, 0x59, 0xF0, 0xFF, 0x85, 0xC0, 0x74,
    0x0B, 0x81, 0x4C, 0x24, 0x40, 0x00, 0x10, 0x00, 0x00, 0x89, 0x45, 0xA7,
    0x48, 0x8D, 0x54, 0x24, 0x40, 0x48, 0x8B, 0xCE, 0xE8, 0xC1, 0x3A, 0x10,
    0x00, 0xEB,
};

// RVA 0x97790, 80 bytes. Skills table record getter: data tables call and the count at +11B8h.
constexpr std::uint8_t SkillRecordWindow[80]{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57, 0x48,
    0x83, 0xEC, 0x30, 0x48, 0x63, 0xF2, 0xE8, 0xE9, 0x92, 0x26, 0x00, 0x48,
    0x8B, 0xF8, 0x48, 0x8B, 0xDE, 0x85, 0xF6, 0x78, 0x09, 0x48, 0x3B, 0x98,
    0xB8, 0x11, 0x00, 0x00, 0x72, 0x18, 0x48, 0x8D, 0x4C, 0x24, 0x48, 0xC6,
    0x44, 0x24, 0x48, 0x00, 0xE8, 0x57, 0xEC, 0xFE, 0xFF, 0x84, 0xC0, 0x74,
    0x01, 0xCC, 0x85, 0xF6, 0x78, 0x55, 0x48, 0x3B, 0x9F, 0xB8, 0x11, 0x00,
    0x00, 0x73, 0x4C, 0x48, 0x81, 0xC7, 0xB0, 0x11,
};

// Written at 537B23. rel32 filled at runtime.
constexpr std::uint8_t MissileHookCode[16]{
    0x4C, 0x89, 0xF1, 0x4C, 0x89, 0xFA, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x41,
    0x8B, 0x06, 0xEB, 0x0F,
};

constexpr std::uint32_t MissileHookRel32Offset = 7;

static_assert(sizeof(MissileHookCode) == 16 && MissileHookRel32Offset + 4 <= sizeof(MissileHookCode));

// If params flag 0x1000: set unit stat (missile, 19, params AR, 0), else return. Target filled at runtime.
constexpr std::uint8_t MissileFallbackStub[38]{
    0xF7, 0x01, 0x00, 0x10, 0x00, 0x00, 0x75, 0x01, 0xC3, 0x44, 0x8B, 0x41,
    0x58, 0x45, 0x31, 0xC9, 0x48, 0x89, 0xD1, 0xBA, 0x13, 0x00, 0x00, 0x00,
    0xFF, 0x25, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00,
};

constexpr std::size_t MissileFallbackTargetOffset = 30;

struct Skip {
    std::uint32_t offset;
    std::uint32_t size;
};

struct Window {
    const char*         name;
    std::uint64_t       rva;
    const std::uint8_t* bytes;
    std::uint32_t       size;
    const Skip*         skips;
    std::uint32_t       skipCount;
};

// Fields other patches may already own: the combat clamp immediates and the
// Character Screen clamp fields rewritten by Hit Chance Bounds.
constexpr Skip CombatSiteSkips[]{ { 0x37, 4 }, { 0x49, 4 } };
constexpr Skip ChanceToHitFormatSkips[]{ { 0x1A, 1 }, { 0x68, 1 }, { 0x6C, 4 }, { 0x73, 4 } };
constexpr Skip ChanceToBeHitFormatSkips[]{ { 0x1F, 1 }, { 0x23, 4 }, { 0x2A, 4 } };

enum class Part : std::uint8_t { Combat, CharacterScreen, Missiles };

struct Witness {
    Part   part;
    Window window;
};

constexpr std::array<Witness, 10> Witnesses{{
    { Part::Combat, { "combat hit roll", 0x44BD0E, CombatSiteWindow,
        sizeof(CombatSiteWindow), CombatSiteSkips, 2 } },
    { Part::CharacterScreen, { "attack rating helper calls", 0x44BB13, CombatHelpersWindow,
        sizeof(CombatHelpersWindow), nullptr, 0 } },
    { Part::CharacterScreen, { "chance to hit calculator", 0x1514700, ChanceToHitWindow,
        sizeof(ChanceToHitWindow), nullptr, 0 } },
    { Part::CharacterScreen, { "chance to hit epilogue", 0x15149D6, ChanceToHitTailWindow,
        sizeof(ChanceToHitTailWindow), nullptr, 0 } },
    { Part::CharacterScreen, { "chance to be hit calculator", 0x15149F0, ChanceToBeHitWindow,
        sizeof(ChanceToBeHitWindow), nullptr, 0 } },
    { Part::CharacterScreen, { "chance to hit formatter", 0x14E8000, ChanceToHitFormatWindow,
        sizeof(ChanceToHitFormatWindow), ChanceToHitFormatSkips, 4 } },
    { Part::CharacterScreen, { "chance to be hit formatter", 0x1514CA0,
        ChanceToBeHitFormatWindow, sizeof(ChanceToBeHitFormatWindow),
        ChanceToBeHitFormatSkips, 3 } },
    { Part::Missiles, { "missile creation attack rating stamp", 0x537B23, CreationStampWindow,
        sizeof(CreationStampWindow), nullptr, 0 } },
    { Part::Missiles, { "CreateSkillMissile skill ToHit call", 0x4336A2, SkillToHitCallWindow,
        sizeof(SkillToHitCallWindow), nullptr, 0 } },
    { Part::Missiles, { "skills table record getter", 0x97790, SkillRecordWindow,
        sizeof(SkillRecordWindow), nullptr, 0 } },
}};

struct Hook {
    const char*         name;
    Part                part;
    std::uint64_t       rva;
    const std::uint8_t* code;
    std::uint32_t       size;
    std::uint32_t       rel32Offset;
};

constexpr std::size_t HookCount        = 4;
constexpr std::size_t MaximumHookBytes = 96;

constexpr std::array<Hook, HookCount> Hooks{{
    { "combat hit roll", Part::Combat, 0x44BD31, CombatHookCode,
        sizeof(CombatHookCode), CombatHookRel32Offset },
    { "average chance to hit", Part::CharacterScreen, 0x151489A, ChanceToHitHookCode,
        sizeof(ChanceToHitHookCode), ChanceToHitHookRel32Offset },
    { "average chance to hit you", Part::CharacterScreen, 0x1514C1F, ChanceToBeHitHookCode,
        sizeof(ChanceToBeHitHookCode), ChanceToBeHitHookRel32Offset },
    { "missile skill attack rating", Part::Missiles, 0x537B23, MissileHookCode,
        sizeof(MissileHookCode), MissileHookRel32Offset },
}};

enum class FieldKind : std::uint8_t { ShowZeroJump, MinimumImm8, MinimumImm32, MaximumImm32 };

struct Field {
    const char*   name;
    std::uint64_t rva;
    std::uint32_t size;
    FieldKind     kind;
};

constexpr std::size_t FieldCount = 7;

constexpr std::array<Field, FieldCount> Fields{{
    { "chance to hit 0% line",      0x14E801A, 1, FieldKind::ShowZeroJump },
    { "chance to hit lower test",   0x14E8068, 1, FieldKind::MinimumImm8 },
    { "chance to hit lower value",  0x14E806C, 4, FieldKind::MinimumImm32 },
    { "chance to hit upper value",  0x14E8073, 4, FieldKind::MaximumImm32 },
    { "chance to be hit lower test",  0x1514CBF, 1, FieldKind::MinimumImm8 },
    { "chance to be hit lower value", 0x1514CC3, 4, FieldKind::MinimumImm32 },
    { "chance to be hit upper value", 0x1514CCA, 4, FieldKind::MaximumImm32 },
}};

constexpr std::uint8_t JneOpcode = 0x75;
constexpr std::uint8_t JmpShortOpcode = 0xEB;

// Relay page: one 14-byte absolute jump per hook, then the two fallback stubs.
constexpr std::size_t RelayPageBytes   = 4'096;
constexpr std::size_t RelaySlotBytes   = 16;
constexpr std::size_t JumpTargetOffset = 6;     // FF 25 00 00 00 00 <abs64>
constexpr std::size_t FallbackOffset   = RelaySlotBytes * HookCount;
constexpr std::size_t MissileFallbackOffset = FallbackOffset + 32;
static_assert(sizeof(FallbackStub) <= 32);
static_assert(MissileFallbackOffset + sizeof(MissileFallbackStub) <= RelayPageBytes);

constexpr std::size_t MaximumConfigBytes = 32'768;

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

struct Config {
    bool          enabled                    = true;
    bool          combat                     = true;
    bool          characterScreen            = true;
    CurveSettings curve{};
    bool          serverAccurateAttackRating = true;
    bool          liveMonsterDefense         = true;
    bool          liveMonsterAttackRating    = true;
    bool          skipClientFudge            = true;
    bool          missileSkillAttackRating   = true;
};

constexpr char DefaultConfigToml[] =
    "# Attack Rating\n"
    "#\n"
    "# Replaces the attack rating versus defense formula that decides whether an\n"
    "# attack hits. Every attacker and defender uses it: players, hirelings and\n"
    "# monsters. Both hit-chance lines on the Character Screen use it too:\n"
    "#   Average chance to hit       you against a monster type\n"
    "#   Average chance to hit you   that monster type against you\n"
    "# The Character Screen runs the same formula as the hit roll, so it shows\n"
    "# the chance the roll really uses.\n"
    "#\n"
    "# The [missiles] section fixes a vanilla bug that strips a skill's attack\n"
    "# rating bonus from player missiles.\n"
    "#\n"
    "# Open attack-rating-curve.html in a browser to see the curve, try values\n"
    "# and copy them back into this file.\n"
    "#\n"
    "# How a hit chance is worked out:\n"
    "#   1. r = attacker attack rating / defender defense\n"
    "#   2. the curve below turns r into a whole percent\n"
    "#   3. optional vanilla level scaling\n"
    "#   4. the result is kept between min_chance and max_chance\n"
    "#   5. the game rolls 0 to 99, and the attack hits when the roll is lower\n"
    "#\n"
    "# Vanilla D2R is exactly this formula with:\n"
    "#   pivot_ratio 1, steepness 1, tail 2, late_tail 0,\n"
    "#   level_scaling true, min_chance 5, max_chance 95\n"
    "\n"
    "[attack_rating]\n"
    "\n"
    "# Master switch.\n"
    "enabled = true\n"
    "\n"
    "# Use the formula for the server-side hit roll.\n"
    "apply_to_combat = true\n"
    "\n"
    "# Use the formula on the Character Screen.\n"
    "apply_to_character_screen = true\n"
    "\n"
    "[curve]\n"
    "\n"
    "# The curve is exactly 50% when attack rating is pivot_ratio times defense.\n"
    "# Below that point it falls away like a sigmoid, above it climbs like a tail:\n"
    "#\n"
    "#   below the pivot   100 * r^steepness / (r^steepness + pivot_ratio^steepness)\n"
    "#   above the pivot   50 + 50 * (r - pivot_ratio) / ((r - pivot_ratio) + tail)\n"
    "#\n"
    "# The default curve, as whole percents before min_chance and max_chance:\n"
    "#   r        1   2   3   4   5   6   8  10  20  30  40  50  60  80 100 104\n"
    "#   chance   0   1   7  24  50  62  75  81  91  94  96  97  98  99  99 100\n"
    "\n"
    "# Ratio where the chance is 50%. Greater than 0.\n"
    "pivot_ratio = 5.0\n"
    "\n"
    "# How hard the chance falls away below the pivot. Greater than 0.\n"
    "# 1 is gentle, as in vanilla. 5 punishes low attack rating hard.\n"
    "steepness = 5.0\n"
    "\n"
    "# How slowly the chance climbs toward 100% above the pivot. Greater than 0.\n"
    "# Smaller values climb faster. Vanilla is 2.\n"
    "tail = 3.0\n"
    "\n"
    "# Optional second tail for high ratios. It is blended in smoothly between\n"
    "# blend_start_ratio and blend_end_ratio, and past blend_end_ratio only\n"
    "# late_tail is used. 0 turns it off. Equal start and end switch at once.\n"
    "late_tail = 1.0\n"
    "blend_start_ratio = 30.0\n"
    "blend_end_ratio = 100.0\n"
    "\n"
    "# Any chance at or above this percent becomes 100%. 50 to 100.\n"
    "# 100 turns it off: the tails approach 100% but never reach it.\n"
    "snap_to_100_at = 99.5\n"
    "\n"
    "[chance]\n"
    "\n"
    "# Limits for the final chance, whole percents from 0 to 100.\n"
    "# Vanilla is 5 and 95. 0 and 100 leave the curve untouched.\n"
    "min_chance = 5\n"
    "max_chance = 95\n"
    "\n"
    "# Vanilla level scaling, applied before the limits:\n"
    "#   chance = 2 * chance * attacker level / (attacker level + defender level)\n"
    "level_scaling = false\n"
    "\n"
    "[character_screen]\n"
    "\n"
    "# Average chance to hit: use the attack rating the hit roll uses. Removes\n"
    "# the class ToHitFactor the screen counts twice, and adds attack rating\n"
    "# against demons and undead, -% target defense, ignore target defense and\n"
    "# attack rating against the monster's type, all for the monster shown.\n"
    "server_accurate_attack_rating = true\n"
    "\n"
    "# Average chance to hit: once you hit a monster type, use the defense that\n"
    "# hit rolled against. That includes auras, curses, champion and unique\n"
    "# bonuses, and the halved -% target defense against superuniques, uniques\n"
    "# and hirelings. Needs apply_to_combat.\n"
    "live_monster_defense = true\n"
    "\n"
    "# Average chance to hit you: once a monster type rolls to hit you, use the\n"
    "# attack rating it rolled with. Needs apply_to_combat.\n"
    "live_monster_attack_rating = true\n"
    "\n"
    "# Skip two reductions the Character Screen makes on its own in Nightmare\n"
    "# and Hell: monster defense times 10/12 and monster attack rating times\n"
    "# 10/15. The hit roll does not make them.\n"
    "skip_client_fudge = true\n"
    "\n"
    "[missiles]\n"
    "\n"
    "# Vanilla bug fix. A skill's ToHit, LevToHit and ToHitCalc give players an\n"
    "# attack rating percent, but a missile only receives it when the game creates\n"
    "# it through CreateSkillMissile. Missiles that skill functions create\n"
    "# themselves, such as Multiple Shot's (srvdofunc 8), roll to hit with no\n"
    "# skill bonus, even though the Character Screen includes it.\n"
    "# When on, every player missile gets its skill's ToHit as it is created,\n"
    "# the same value CreateSkillMissile would give it. Missiles that never roll\n"
    "# to hit are unaffected. Monsters and hirelings keep the vanilla behaviour.\n"
    "skill_attack_rating_on_missiles = true\n";

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

enum class InstallState : std::uint8_t { NotLoaded, DisabledByConfig, Armed, PartiallyArmed };

const D2RL::PluginContext* Context{};
std::uint8_t*              Base{};
Config                     Settings{};
InstallState               State{ InstallState::NotLoaded };
void*                      RelayPage{};

std::array<bool, HookCount> HookPatched{};
std::array<std::array<std::uint8_t, MaximumHookBytes>, HookCount> HookOriginal{};
std::array<std::array<std::uint8_t, MaximumHookBytes>, HookCount> HookWritten{};

std::array<bool, FieldCount> FieldPatched{};
std::array<std::array<std::uint8_t, 4>, FieldCount> FieldOriginal{};
std::array<std::array<std::uint8_t, 4>, FieldCount> FieldWritten{};

std::atomic<std::uint64_t> CombatRolls{};
std::atomic<std::uint64_t> ChanceToHitRuns{};
std::atomic<std::uint64_t> ChanceToBeHitRuns{};
std::atomic<std::uint64_t> MissilesStamped{};

// (monster class + 1) << 32 | value. Zero means nothing recorded yet.
std::atomic<std::uint64_t> LiveDefense{};
std::atomic<std::uint64_t> LiveAttackRating{};

GetUnitStatFn            GetUnitStat{};
PlayerBaseAttackRatingFn PlayerBaseAttackRating{};
ApplyTargetModifiersFn   ApplyTargetModifiers{};
GetMonStatsRecordFn      GetMonStatsRecord{};
GetLayeredStatsFn        GetLayeredStats{};
MonTypeMatchesFn         MonTypeMatches{};
GetDataTablesFn          GetDataTables{};
SetUnitStatFn            SetUnitStat{};
SkillToHitFn             SkillToHit{};

template <typename T>
auto ReadAt(const void* base, std::size_t offset) noexcept -> T {
    T value{};
    std::memcpy(&value, static_cast<const std::uint8_t*>(base) + offset, sizeof(T));
    return value;
}

template <typename T>
void WriteAt(void* base, std::size_t offset, T value) noexcept {
    std::memcpy(static_cast<std::uint8_t*>(base) + offset, &value, sizeof(T));
}

auto PackLive(std::int32_t monsterClass, std::int32_t value) noexcept -> std::uint64_t {
    const auto high = static_cast<std::uint64_t>(static_cast<std::uint32_t>(monsterClass) + 1U);
    return (high << 32) | static_cast<std::uint32_t>(std::max(value, 0));
}

auto UnpackLive(std::uint64_t packed, std::int32_t& monsterClass, std::int32_t& value) noexcept
        -> bool {
    const auto high = static_cast<std::uint32_t>(packed >> 32);
    if (high == 0) return false;
    monsterClass = static_cast<std::int32_t>(high - 1U);
    value = static_cast<std::int32_t>(static_cast<std::uint32_t>(packed));
    return true;
}

// ---------------------------------------------------------------------------
// Config parsing (small hand-rolled TOML subset, no external dependency)
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

auto ParseBool(std::string_view value, bool& out) noexcept -> bool {
    if (value == "true" || value == "1") { out = true; return true; }
    if (value == "false" || value == "0") { out = false; return true; }
    return false;
}

auto ParseInteger(std::string_view value, std::int64_t& out) noexcept -> bool {
    if (value.empty()) return false;
    bool negative = false;
    std::size_t index = 0;
    if (value[0] == '+' || value[0] == '-') {
        negative = value[0] == '-';
        index = 1;
    }
    if (index >= value.size()) return false;
    std::int64_t accumulator = 0;
    bool digits = false;
    for (; index < value.size(); ++index) {
        const char character = value[index];
        if (character == '_') continue;
        if (character < '0' || character > '9') return false;
        accumulator = accumulator * 10 + (character - '0');
        if (accumulator > 4'000'000'000LL) return false;
        digits = true;
    }
    if (!digits) return false;
    out = negative ? -accumulator : accumulator;
    return true;
}

// Locale independent, and correctly rounded for the short decimals a config
// holds: the digits become one integer, divided once by a power of ten.
auto ParseDecimal(std::string_view value, double& out) noexcept -> bool {
    if (value.empty()) return false;
    std::size_t index = 0;
    bool negative = false;
    if (value[0] == '+' || value[0] == '-') {
        negative = value[0] == '-';
        index = 1;
    }
    std::uint64_t mantissa = 0;
    int decimals = 0;
    bool digits = false;
    bool point = false;
    for (; index < value.size(); ++index) {
        const char character = value[index];
        if (character == '_') continue;
        if (character == '.') {
            if (point) return false;
            point = true;
            continue;
        }
        if (character < '0' || character > '9') return false;
        if (mantissa > 100'000'000'000'000ULL) return false;
        mantissa = mantissa * 10 + static_cast<std::uint64_t>(character - '0');
        if (point) ++decimals;
        digits = true;
    }
    if (!digits || decimals > 15) return false;
    static constexpr double PowersOfTen[16]{
        1e0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9, 1e10, 1e11, 1e12, 1e13, 1e14, 1e15,
    };
    const double magnitude = static_cast<double>(mantissa) / PowersOfTen[decimals];
    out = negative ? -magnitude : magnitude;
    return true;
}

void ApplyConfigLine(std::string_view key, std::string_view value) noexcept {
    auto& curve = Settings.curve;
    std::int64_t integer = 0;
    if (key == "enabled") ParseBool(value, Settings.enabled);
    else if (key == "apply_to_combat") ParseBool(value, Settings.combat);
    else if (key == "apply_to_character_screen") ParseBool(value, Settings.characterScreen);
    else if (key == "pivot_ratio") ParseDecimal(value, curve.pivotRatio);
    else if (key == "steepness") ParseDecimal(value, curve.steepness);
    else if (key == "tail") ParseDecimal(value, curve.tail);
    else if (key == "late_tail") ParseDecimal(value, curve.lateTail);
    else if (key == "blend_start_ratio") ParseDecimal(value, curve.blendStartRatio);
    else if (key == "blend_end_ratio") ParseDecimal(value, curve.blendEndRatio);
    else if (key == "snap_to_100_at") ParseDecimal(value, curve.snapTo100At);
    else if (key == "min_chance") {
        if (ParseInteger(value, integer)) {
            curve.minChance = static_cast<std::int32_t>(std::clamp<std::int64_t>(integer, 0, 100));
        }
    } else if (key == "max_chance") {
        if (ParseInteger(value, integer)) {
            curve.maxChance = static_cast<std::int32_t>(std::clamp<std::int64_t>(integer, 0, 100));
        }
    }
    else if (key == "level_scaling") ParseBool(value, curve.levelScaling);
    else if (key == "server_accurate_attack_rating") ParseBool(value, Settings.serverAccurateAttackRating);
    else if (key == "live_monster_defense") ParseBool(value, Settings.liveMonsterDefense);
    else if (key == "live_monster_attack_rating") ParseBool(value, Settings.liveMonsterAttackRating);
    else if (key == "skip_client_fudge") ParseBool(value, Settings.skipClientFudge);
    else if (key == "skill_attack_rating_on_missiles") ParseBool(value, Settings.missileSkillAttackRating);
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
        Context->LogWarn("AttackRating: config file could not be created; using defaults.");
    } else {
        std::string buffer(MaximumConfigBytes, '\0');
        if (Context->ReadConfig(buffer.data(), static_cast<std::uint32_t>(buffer.size()), nullptr)) {
            buffer.resize(std::strlen(buffer.c_str()));
            ParseConfig(buffer);
        } else {
            Context->LogWarn("AttackRating: config file could not be read; using defaults.");
        }
    }
    SanitizeCurveSettings(Settings.curve);
}

// ---------------------------------------------------------------------------
// Inputs the server uses, rebuilt for the monster type on the screen
// ---------------------------------------------------------------------------

auto ClassToHitFactor(void* player) noexcept -> std::int32_t {
    const auto playerClass = ReadAt<std::uint32_t>(player, UnitClassOffset);
    const void* tables = GetDataTables(ReadAt<std::uint8_t>(player, UnitDataContextOffset));
    if (!tables) return 0;
    const auto count = ReadAt<std::uint64_t>(tables, CharStatsCountOffset);
    const auto records = ReadAt<std::uint64_t>(tables, CharStatsRecordsOffset);
    if (playerClass >= count || records == 0) return 0;
    const auto record = records + CharStatsRecordBytes * playerClass;
    return ReadAt<std::int32_t>(reinterpret_cast<const void*>(record), CharStatsToHitFactorOffset);
}

auto MonsterTypeAttackRatingPercent(void* player, std::int32_t monsterClass) noexcept
        -> std::int64_t {
    const auto context = ReadAt<std::uint8_t>(player, UnitDataContextOffset);
    const void* record = GetMonStatsRecord(context, monsterClass);
    if (!record) return 0;
    const auto monType = ReadAt<std::int16_t>(record, MonStatsMonTypeOffset);
    if (monType <= 0) return 0;

    std::array<std::uint8_t, LayeredStatCapacity * LayeredStatEntryBytes> entries{};
    const std::int32_t count = GetLayeredStats(player, StatAttackRatingVsMonType,
        entries.data(), LayeredStatCapacity);
    const std::int32_t bounded = std::clamp<std::int32_t>(count, 0,
        static_cast<std::int32_t>(LayeredStatCapacity));

    std::int64_t total = 0;
    for (std::int32_t i = 0; i < bounded; ++i) {
        const std::size_t entry = static_cast<std::size_t>(i) * LayeredStatEntryBytes;
        const auto layer = ReadAt<std::uint16_t>(entries.data(), entry);
        if (MonTypeMatches(context, layer, monType) != 0) {
            total += ReadAt<std::int32_t>(entries.data(), entry + 4);
        }
    }
    return total;
}

void ApplyServerInputs(void* player, std::int32_t monsterClass, std::int32_t slotAttackRating,
        double& attackRating, double& defense) noexcept {
    const std::int32_t base = PlayerBaseAttackRating(player);
    std::int32_t adjustedBase = base;
    std::int32_t adjustedDefense = static_cast<std::int32_t>(defense);

    std::array<std::uint8_t, StandInUnitBytes> standIn{};
    WriteAt<std::uint32_t>(standIn.data(), UnitTypeOffset, UnitMonster);
    WriteAt<std::int32_t>(standIn.data(), UnitClassOffset, monsterClass);
    standIn[UnitDataContextOffset] = ReadAt<std::uint8_t>(player, UnitDataContextOffset);
    ApplyTargetModifiers(player, standIn.data(), &adjustedBase, &adjustedDefense);

    std::int64_t result = base > 0
        ? static_cast<std::int64_t>(slotAttackRating) * adjustedBase / base
        : static_cast<std::int64_t>(slotAttackRating) + adjustedBase - base;
    result += static_cast<std::int64_t>(adjustedBase)
        * MonsterTypeAttackRatingPercent(player, monsterClass) / 100;

    attackRating = static_cast<double>(result);
    defense = adjustedDefense;
}

// ---------------------------------------------------------------------------
// The three hooks, reached through the relays
// ---------------------------------------------------------------------------

// 44BD31: ecx = AR, edx = Def, r8 = attacker, r9 = defender,
// then attacker level and defender level on the stack.
std::int32_t CombatChance(std::int32_t attackRating, std::int32_t defense,
        void* attacker, void* defender,
        std::int32_t attackerLevel, std::int32_t defenderLevel) noexcept {
    CombatRolls.fetch_add(1, std::memory_order_relaxed);
    if (attacker && defender) {
        const auto attackerType = ReadAt<std::uint32_t>(attacker, UnitTypeOffset);
        const auto defenderType = ReadAt<std::uint32_t>(defender, UnitTypeOffset);
        if (attackerType == UnitPlayer && defenderType == UnitMonster) {
            LiveDefense.store(PackLive(ReadAt<std::int32_t>(defender, UnitClassOffset), defense),
                std::memory_order_relaxed);
        } else if (attackerType == UnitMonster && defenderType == UnitPlayer) {
            LiveAttackRating.store(
                PackLive(ReadAt<std::int32_t>(attacker, UnitClassOffset), attackRating),
                std::memory_order_relaxed);
        }
    }
    return FinalChance(attackRating, defense, attackerLevel, defenderLevel, Settings.curve);
}

// 151489A: ecx = slot AR, edx = Def after the screen-only reduction,
// r8 = player, r9d = monster class, then monster level and the raw Def.
std::int32_t ChanceToHit(std::int32_t slotAttackRating, std::int32_t reducedDefense,
        void* player, std::int32_t monsterClass,
        std::int32_t monsterLevel, std::int32_t rawDefense) noexcept {
    ChanceToHitRuns.fetch_add(1, std::memory_order_relaxed);
    double attackRating = slotAttackRating;
    double defense = Settings.skipClientFudge ? rawDefense : reducedDefense;
    std::int32_t playerLevel = 0;
    if (player) {
        playerLevel = GetUnitStat(player, StatLevel, 0);
        if (ReadAt<std::uint32_t>(player, UnitTypeOffset) == UnitPlayer) {
            if (Settings.serverAccurateAttackRating && monsterClass >= 0) {
                ApplyServerInputs(player, monsterClass, slotAttackRating, attackRating, defense);
            } else {
                attackRating += ClassToHitFactor(player);
            }
        }
    }
    std::int32_t liveClass = 0;
    std::int32_t liveValue = 0;
    if (Settings.liveMonsterDefense
            && UnpackLive(LiveDefense.load(std::memory_order_relaxed), liveClass, liveValue)
            && liveClass == monsterClass && liveValue > 0) {
        defense = liveValue;
    }
    return FinalChance(attackRating, defense, playerLevel, monsterLevel, Settings.curve);
}

// 1514C1F: ecx = monster AR after the screen-only reduction, edx = player Def,
// r8 = player, r9d = monster class, then monster level and the raw AR.
std::int32_t ChanceToBeHit(std::int32_t reducedAttackRating, std::int32_t playerDefense,
        void* player, std::int32_t monsterClass,
        std::int32_t monsterLevel, std::int32_t rawAttackRating) noexcept {
    ChanceToBeHitRuns.fetch_add(1, std::memory_order_relaxed);
    double attackRating = Settings.skipClientFudge ? rawAttackRating : reducedAttackRating;
    std::int32_t liveClass = 0;
    std::int32_t liveValue = 0;
    if (Settings.liveMonsterAttackRating
            && UnpackLive(LiveAttackRating.load(std::memory_order_relaxed), liveClass, liveValue)
            && liveClass == monsterClass && liveValue > 0) {
        attackRating = liveValue;
    }
    const std::int32_t playerLevel = player ? GetUnitStat(player, StatLevel, 0) : 0;
    return FinalChance(attackRating, playerDefense, monsterLevel, playerLevel, Settings.curve);
}

// 537B23: rcx = missile creation params, rdx = the new missile. Replaces the
// params flag 0x1000 stamp of stat 19.
void StampMissileAttackRating(void* params, void* missile) noexcept {
    const auto flags = ReadAt<std::uint32_t>(params, MissileParamsFlagsOffset);
    if ((flags & MissileParamsAttackRatingFlag) != 0) {
        SetUnitStat(missile, StatToHit,
            ReadAt<std::int32_t>(params, MissileParamsAttackRatingOffset), 0);
        return;
    }
    const void* owner = ReadAt<void*>(params, MissileParamsOwnerOffset);
    if (!owner || ReadAt<std::uint32_t>(owner, UnitTypeOffset) != UnitPlayer) return;

    const auto skill = ReadAt<std::int32_t>(params, MissileParamsSkillOffset);
    const auto skillLevel = ReadAt<std::int32_t>(params, MissileParamsSkillLevelOffset);
    if (skill < 0 || skillLevel <= 0) return;
    const void* tables = GetDataTables(ReadAt<std::uint8_t>(owner, UnitDataContextOffset));
    if (!tables || static_cast<std::uint64_t>(skill) >= ReadAt<std::uint64_t>(tables, SkillsCountOffset)) {
        return;
    }

    const std::int32_t toHit = SkillToHit(const_cast<void*>(owner), skill, skillLevel);
    if (toHit == 0) return;
    SetUnitStat(missile, StatToHit, toHit, 0);
    MissilesStamped.fetch_add(1, std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------
// Verification
// ---------------------------------------------------------------------------

auto PartEnabled(Part part) noexcept -> bool {
    switch (part) {
    case Part::Combat:          return Settings.combat;
    case Part::CharacterScreen: return Settings.characterScreen;
    default:                    return Settings.missileSkillAttackRating;
    }
}

auto WindowMatches(const Window& window) noexcept -> bool {
    const std::uint8_t* live = Base + window.rva;
    std::uint32_t cursor = 0;
    for (std::uint32_t i = 0; i < window.skipCount; ++i) {
        const Skip& skip = window.skips[i];
        if (std::memcmp(live + cursor, window.bytes + cursor, skip.offset - cursor) != 0) {
            return false;
        }
        cursor = skip.offset + skip.size;
    }
    return std::memcmp(live + cursor, window.bytes + cursor, window.size - cursor) == 0;
}

auto VerifyNativeContract() noexcept -> bool {
    for (const auto& witness : Witnesses) {
        if (!PartEnabled(witness.part) || WindowMatches(witness.window)) continue;
        char message[256];
        std::snprintf(message, sizeof(message),
            "AttackRating: the %s at 0x%llX does not match the verified D2R 3.3 image, "
            "or another plugin already patches it. Refusing to load.",
            witness.window.name, static_cast<unsigned long long>(witness.window.rva));
        Context->LogError(message);
        return false;
    }
    if (Settings.characterScreen) {
        const std::uint8_t jump = Base[Fields[0].rva];
        if (jump != JneOpcode && jump != JmpShortOpcode) {
            Context->LogError(
                "AttackRating: the chance to hit 0% branch at 0x14E801A is neither the "
                "vanilla jne nor a jmp. Refusing to load.");
            return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Relay page and patching
// ---------------------------------------------------------------------------

auto WithinRel32(std::uintptr_t next, std::uintptr_t target) noexcept -> bool {
    const std::int64_t delta = static_cast<std::int64_t>(target) - static_cast<std::int64_t>(next);
    return delta >= INT32_MIN && delta <= INT32_MAX;
}

auto AllocateNear(std::uintptr_t hint, std::size_t size) noexcept -> void* {
    SYSTEM_INFO systemInfo{};
    GetSystemInfo(&systemInfo);
    const auto granularity = static_cast<std::uintptr_t>(systemInfo.dwAllocationGranularity);
    const auto aligned = hint & ~(granularity - 1U);
    for (std::uintptr_t delta = granularity; delta < 0x7000'0000ULL; delta += granularity) {
        const auto candidate = aligned + delta;
        if (!WithinRel32(hint, candidate + size)) break;
        if (auto* memory = VirtualAlloc(reinterpret_cast<void*>(candidate), size,
                MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)) {
            return memory;
        }
    }
    return nullptr;
}

auto RelayAddress(std::size_t hookIndex) noexcept -> std::uintptr_t {
    return reinterpret_cast<std::uintptr_t>(RelayPage) + RelaySlotBytes * hookIndex;
}

auto HookTarget(std::size_t hookIndex) noexcept -> std::uint64_t {
    switch (hookIndex) {
    case 0:  return reinterpret_cast<std::uint64_t>(&CombatChance);
    case 1:  return reinterpret_cast<std::uint64_t>(&ChanceToHit);
    case 2:  return reinterpret_cast<std::uint64_t>(&ChanceToBeHit);
    default: return reinterpret_cast<std::uint64_t>(&StampMissileAttackRating);
    }
}

void WriteJumpStub(std::uint8_t* stub, std::uint64_t target) noexcept {
    static constexpr std::uint8_t JumpQwordRip[]{ 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00 };
    std::memcpy(stub, JumpQwordRip, sizeof(JumpQwordRip));
    std::memcpy(stub + JumpTargetOffset, &target, sizeof(target));
}

void ReleaseRelayPage() noexcept {
    if (RelayPage) VirtualFree(RelayPage, 0, MEM_RELEASE);
    RelayPage = nullptr;
}

// Points every relay at a native stub that does what the original code did,
// so a call that is already on its way never lands in an unloaded DLL.
auto RetargetRelaysToFallback() noexcept -> bool {
    if (!RelayPage) return true;
    auto* page = static_cast<std::uint8_t*>(RelayPage);
    DWORD previous = 0;
    if (!VirtualProtect(page, RelayPageBytes, PAGE_EXECUTE_READWRITE, &previous)) return false;
    for (std::size_t i = 0; i < HookCount; ++i) {
        const std::uint64_t fallback = reinterpret_cast<std::uint64_t>(page)
            + (Hooks[i].part == Part::Missiles ? MissileFallbackOffset : FallbackOffset);
        std::memcpy(page + RelaySlotBytes * i + JumpTargetOffset, &fallback, sizeof(fallback));
    }
    DWORD ignored = 0;
    VirtualProtect(page, RelayPageBytes, previous, &ignored);
    FlushInstructionCache(GetCurrentProcess(), page, RelayPageBytes);
    return true;
}

auto RestoreAll() noexcept -> bool {
    bool restored = true;
    for (std::size_t i = FieldCount; i-- > 0;) {
        if (!FieldPatched[i]) continue;
        const Field& field = Fields[i];
        if (Context->PatchBytes(field.rva, FieldWritten[i].data(), field.size,
                FieldOriginal[i].data(), field.size)) {
            FieldPatched[i] = false;
        } else {
            restored = false;
        }
    }
    for (std::size_t i = HookCount; i-- > 0;) {
        if (!HookPatched[i]) continue;
        const Hook& hook = Hooks[i];
        if (Context->PatchBytes(hook.rva, HookWritten[i].data(), hook.size,
                HookOriginal[i].data(), hook.size)) {
            HookPatched[i] = false;
        } else {
            restored = false;
        }
    }
    return restored;
}

auto InstallHook(std::size_t index) noexcept -> bool {
    const Hook& hook = Hooks[index];
    const auto imageBase = reinterpret_cast<std::uintptr_t>(Base);
    const std::uintptr_t next = imageBase + hook.rva + hook.rel32Offset + sizeof(std::int32_t);
    if (!WithinRel32(next, RelayAddress(index))) return false;

    auto& written = HookWritten[index];
    auto& original = HookOriginal[index];
    std::memcpy(written.data(), hook.code, hook.size);
    const auto displacement = static_cast<std::int32_t>(
        static_cast<std::int64_t>(RelayAddress(index)) - static_cast<std::int64_t>(next));
    std::memcpy(written.data() + hook.rel32Offset, &displacement, sizeof(displacement));
    std::memcpy(original.data(), Base + hook.rva, hook.size);

    if (!Context->PatchBytes(hook.rva, original.data(), hook.size, written.data(), hook.size)) {
        return false;
    }
    HookPatched[index] = true;
    return true;
}

auto InstallField(std::size_t index) noexcept -> bool {
    const Field& field = Fields[index];
    auto& written = FieldWritten[index];
    auto& original = FieldOriginal[index];
    written.fill(0);
    const auto minimum = static_cast<std::uint32_t>(Settings.curve.minChance);
    const auto maximum = static_cast<std::uint32_t>(Settings.curve.maxChance);
    switch (field.kind) {
    case FieldKind::ShowZeroJump: written[0] = JmpShortOpcode; break;
    case FieldKind::MinimumImm8:  written[0] = static_cast<std::uint8_t>(minimum); break;
    case FieldKind::MinimumImm32: std::memcpy(written.data(), &minimum, sizeof(minimum)); break;
    case FieldKind::MaximumImm32: std::memcpy(written.data(), &maximum, sizeof(maximum)); break;
    }
    std::memcpy(original.data(), Base + field.rva, field.size);
    if (std::memcmp(original.data(), written.data(), field.size) == 0) return true;

    if (!Context->PatchBytes(field.rva, original.data(), field.size, written.data(), field.size)) {
        return false;
    }
    FieldPatched[index] = true;
    return true;
}

// Returns false only when nothing in the image can reach this DLL any more,
// so the loader may safely unload it.
auto RollBack(const char* failedPart) noexcept -> bool {
    char message[192];
    std::snprintf(message, sizeof(message), "AttackRating: the %s could not be patched.",
        failedPart);
    Context->LogError(message);

    const bool retargeted = RetargetRelaysToFallback();
    if (RestoreAll()) {
        ReleaseRelayPage();
        return false;
    }
    if (retargeted) {
        Context->LogError(
            "AttackRating: some sites could not be restored; they now use the vanilla "
            "ratio without limits.");
        return false;
    }
    Context->LogError(
        "AttackRating: rollback failed, staying loaded so the patched sites keep a valid "
        "target. The formula is only partially applied.");
    State = InstallState::PartiallyArmed;
    return true;
}

auto InstallHooks() noexcept -> bool {
    const auto imageBase = reinterpret_cast<std::uintptr_t>(Base);
    std::uint64_t lowest = UINT64_MAX;
    for (const auto& hook : Hooks) {
        if (PartEnabled(hook.part) && hook.rva < lowest) lowest = hook.rva;
    }

    RelayPage = AllocateNear(imageBase + lowest, RelayPageBytes);
    if (!RelayPage) {
        Context->LogError("AttackRating: no relay page was available within rel32 reach.");
        return false;
    }

    auto* page = static_cast<std::uint8_t*>(RelayPage);
    std::memset(page, 0xCC, RelayPageBytes);
    for (std::size_t i = 0; i < HookCount; ++i) {
        WriteJumpStub(page + RelaySlotBytes * i, HookTarget(i));
    }
    std::memcpy(page + FallbackOffset, FallbackStub, sizeof(FallbackStub));
    std::memcpy(page + MissileFallbackOffset, MissileFallbackStub, sizeof(MissileFallbackStub));
    const std::uint64_t setUnitStat = Context->exeBase + SetUnitStatRva;
    std::memcpy(page + MissileFallbackOffset + MissileFallbackTargetOffset, &setUnitStat,
        sizeof(setUnitStat));

    DWORD previousProtection = 0;
    if (!VirtualProtect(page, RelayPageBytes, PAGE_EXECUTE_READ, &previousProtection)) {
        Context->LogError("AttackRating: relay page protection could not be finalized.");
        ReleaseRelayPage();
        return false;
    }
    FlushInstructionCache(GetCurrentProcess(), page, RelayPageBytes);

    for (std::size_t i = 0; i < HookCount; ++i) {
        if (!PartEnabled(Hooks[i].part)) continue;
        if (!InstallHook(i)) return RollBack(Hooks[i].name);
    }
    if (Settings.characterScreen) {
        for (std::size_t i = 0; i < FieldCount; ++i) {
            if (!InstallField(i)) return RollBack(Fields[i].name);
        }
    }

    State = InstallState::Armed;
    return true;
}

void ResolveNatives() noexcept {
    const auto at = [](std::uint64_t rva) { return Context->exeBase + rva; };
    GetUnitStat            = reinterpret_cast<GetUnitStatFn>(at(GetUnitStatRva));
    PlayerBaseAttackRating = reinterpret_cast<PlayerBaseAttackRatingFn>(at(PlayerBaseAttackRatingRva));
    ApplyTargetModifiers   = reinterpret_cast<ApplyTargetModifiersFn>(at(ApplyTargetModifiersRva));
    GetMonStatsRecord      = reinterpret_cast<GetMonStatsRecordFn>(at(GetMonStatsRecordRva));
    GetLayeredStats        = reinterpret_cast<GetLayeredStatsFn>(at(GetLayeredStatsRva));
    MonTypeMatches         = reinterpret_cast<MonTypeMatchesFn>(at(MonTypeMatchesRva));
    GetDataTables          = reinterpret_cast<GetDataTablesFn>(at(GetDataTablesRva));
    SetUnitStat            = reinterpret_cast<SetUnitStatFn>(at(SetUnitStatRva));
    SkillToHit             = reinterpret_cast<SkillToHitFn>(at(SkillToHitRva));
}

// ---------------------------------------------------------------------------
// Console command
// ---------------------------------------------------------------------------

auto StateName() noexcept -> const char* {
    switch (State) {
    case InstallState::DisabledByConfig: return "disabled by config";
    case InstallState::Armed:            return "armed";
    case InstallState::PartiallyArmed:   return "PARTIALLY armed, see log";
    default:                             return "not loaded";
    }
}

auto ParseArguments(std::string_view text, std::array<std::int64_t, 4>& values) noexcept -> int {
    int count = 0;
    std::size_t cursor = 0;
    while (cursor < text.size()) {
        while (cursor < text.size() && (text[cursor] == ' ' || text[cursor] == '\t')) ++cursor;
        if (cursor >= text.size()) break;
        std::size_t end = cursor;
        while (end < text.size() && text[end] != ' ' && text[end] != '\t') ++end;
        if (count >= static_cast<int>(values.size())) return -1;
        if (!ParseInteger(text.substr(cursor, end - cursor), values[static_cast<std::size_t>(count)])) {
            return -1;
        }
        ++count;
        cursor = end;
    }
    return count;
}

void DescribeLive(char* out, std::size_t size, const char* label,
        const std::atomic<std::uint64_t>& live) noexcept {
    std::int32_t monsterClass = 0;
    std::int32_t value = 0;
    if (UnpackLive(live.load(std::memory_order_relaxed), monsterClass, value)) {
        std::snprintf(out, size, "%s monstats row %d = %d", label, monsterClass, value);
    } else {
        std::snprintf(out, size, "%s none yet", label);
    }
}

auto __cdecl ArChanceCommand(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command,
        void*) noexcept -> D2RL::ConsoleCommandResult {
    if (!command || !command->plugin) return D2RL::ConsoleCommandResult::Failed;

    const std::string_view args = command->args && command->argsLength
        ? std::string_view(command->args, command->argsLength)
        : std::string_view{};
    std::array<std::int64_t, 4> values{};
    const int count = ParseArguments(args, values);
    const auto& curve = Settings.curve;
    char message[768];

    if (count >= 2) {
        const auto attackerLevel = static_cast<std::int32_t>(count >= 3 ? values[2] : 1);
        const auto defenderLevel = static_cast<std::int32_t>(count >= 4 ? values[3] : attackerLevel);
        const std::int32_t chance = FinalChance(static_cast<double>(values[0]),
            static_cast<double>(values[1]), attackerLevel, defenderLevel, curve);
        std::snprintf(message, sizeof(message),
            "Attack Rating: %lld attack rating against %lld defense hits %d%% of the time "
            "(levels %d vs %d, level scaling %s).",
            static_cast<long long>(values[0]), static_cast<long long>(values[1]), chance,
            attackerLevel, defenderLevel, curve.levelScaling ? "on" : "off");
    } else if (count == 0) {
        char defense[96];
        char attack[96];
        DescribeLive(defense, sizeof(defense), "live defense", LiveDefense);
        DescribeLive(attack, sizeof(attack), "live attack rating", LiveAttackRating);
        std::snprintf(message, sizeof(message),
            "Attack Rating: %s | combat %s, character screen %s, missile skill attack rating %s | "
            "pivot %.2f, steepness %.2f, tail %.2f, late tail %.2f from %.1f to %.1f, "
            "snap at %.1f | chance %d%% to %d%%, level scaling %s | rolls %llu, screen %llu/%llu, "
            "missiles given skill attack rating %llu | %s | %s",
            StateName(), Settings.combat ? "on" : "off", Settings.characterScreen ? "on" : "off",
            Settings.missileSkillAttackRating ? "on" : "off",
            curve.pivotRatio, curve.steepness, curve.tail, curve.lateTail,
            curve.blendStartRatio, curve.blendEndRatio, curve.snapTo100At,
            curve.minChance, curve.maxChance, curve.levelScaling ? "on" : "off",
            static_cast<unsigned long long>(CombatRolls.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(ChanceToHitRuns.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(ChanceToBeHitRuns.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(MissilesStamped.load(std::memory_order_relaxed)),
            defense, attack);
    } else {
        std::snprintf(message, sizeof(message),
            "Usage: archance, or archance <attack rating> <defense> "
            "[attacker level] [defender level]");
    }
    command->plugin->WriteConsoleMessage(message);
    return D2RL::ConsoleCommandResult::Handled;
}

void RegisterStatusCommand() noexcept {
    if (!Context->RegisterConsoleCommand("archance", &ArChanceCommand,
            "Attack Rating status, or the hit chance for an attack rating and defense.")) {
        Context->LogWarn("AttackRating: the archance console command was refused.");
    }
}

constexpr D2RL::PluginInfo Info{
    .infoSize = D2RL::PluginInfoSize,
    .apiVersion = D2RL_PLUGIN_API_VERSION,
    .id = "celestialrayone.attack-rating",
    .name = "Attack Rating",
    .version = "1.0.0",
    .author = "CelestialRayOne",
    .description =
        "Configurable attack rating hit-chance curve and hit chance limits, applied to the "
        "hit roll and both Character Screen hit-chance lines, plus skill attack rating on "
        "player missiles.",
    .flags = D2RL::PluginFlags::Shared | D2RL::PluginFlags::NativeHooks,
};

}  // namespace

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* {
    return &Info;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
    if (!D2RL::HasContext(context)) return false;
    Context = context;
    Base = reinterpret_cast<std::uint8_t*>(context->exeBase);
    ResolveNatives();
    ReadConfiguration();

    if (!Settings.enabled
            || (!Settings.combat && !Settings.characterScreen && !Settings.missileSkillAttackRating)) {
        State = InstallState::DisabledByConfig;
        Context->LogInfo(Settings.enabled
            ? "AttackRating: apply_to_combat, apply_to_character_screen and "
              "skill_attack_rating_on_missiles are all off; no hooks installed."
            : "AttackRating: disabled by configuration.");
        RegisterStatusCommand();
        return true;
    }

    if (!VerifyNativeContract()) return false;
    if (!InstallHooks()) return false;

    const auto& curve = Settings.curve;
    char message[400];
    std::snprintf(message, sizeof(message),
        "AttackRating: %s. combat %s, character screen %s, missile skill attack rating %s. "
        "pivot %.2f, steepness %.2f, "
        "tail %.2f, late tail %.2f (%.1f to %.1f), snap %.1f, chance %d%% to %d%%, "
        "level scaling %s.",
        StateName(), Settings.combat ? "on" : "off", Settings.characterScreen ? "on" : "off",
        Settings.missileSkillAttackRating ? "on" : "off",
        curve.pivotRatio, curve.steepness, curve.tail, curve.lateTail, curve.blendStartRatio,
        curve.blendEndRatio, curve.snapTo100At, curve.minChance, curve.maxChance,
        curve.levelScaling ? "on" : "off");
    Context->LogInfo(message);

    RegisterStatusCommand();
    return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
    if (!Context || !RelayPage) return;
    RetargetRelaysToFallback();
    RestoreAll();
    // The relay page is deliberately kept: a thread may be inside a jump
    // through it right now, and any site that could not be restored still
    // needs it.
}

}  // namespace CelestialRayOne::AttackRating
