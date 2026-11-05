// Monster Life Cap
//
// Keeps the life of every monster, summon and bound demon inside the 32-bit
// life stat. Life is stored in 256ths (hitpoints 6, maxhp 7), so the most a
// life stat can hold is 2147483647 = 8,388,607.99 life. A computation that goes
// past that wraps around to a garbage or negative number and the monster ends
// up with nonsense life or dies on the spot. This plugin finds every place that
// can do that and holds the result at the cap instead. Below the cap every
// result is exactly what the game computed before.
//
// Everything below was read out of the D2RLoader 1.3.1 process image (D2R
// 3.3.93847, md5 8af7a47ce9077de0378c69af2930854d) and D2RCore.dll (SHA-256
// 2a868d013d2e0830bd2d9e04b918b19e46a73cf726c833e70d089b948fdeb5a2). Every
// patched byte is checked before it is written; a part whose bytes differ (a
// different build, or another plugin already owns them) stays off and says so
// in the log and in the "lifecap" console command.
//
// ---------------------------------------------------------------------------
// Already held at the cap (left alone)
// ---------------------------------------------------------------------------
//   Spawn (543E90): rolled life + player-count bonus, cmp edi,800000h /
//     cmovge 7FFFFFh at 544284, then shl 8.
//   Summons (55EF80): D2RLoader's own cave at 55F367 computes max + max *
//     calc1 / 100 in 64 bits and saturates it to 32 bits.
//   Heralds (D2RCore 40EC00): lowers the requested boost so max life stays
//     under 32 bits: allowed = (7FFFFFFFh - max) * 100 / max.
//   Bonuses (D2RCore 3DC2D0, a stat's full value): every list value and every
//     op result (64-bit imul, /100, clamped) is added with a saturating add
//     (sar 31 / add 80000000h / cmovo), so Battle Orders, Oak Sage, item and
//     aura max life percent and flat bonuses top out at 7FFFFFFFh.
//   Life percent (34A220 for the AI, 588CE0 for the health bar): both shift
//     life down to whole life before multiplying (by 100 / 128), no overflow.
//
// ---------------------------------------------------------------------------
// Fixed here
// ---------------------------------------------------------------------------
//   1. Monster-mod life (49BD50: unit, percent): champion, unique, minion,
//      Possessed and Herald bonuses. max = ReadWideMaxLife, max * percent /
//      100 with three 32-bit branches, then stats 7 and 6 = max + that, all in
//      32 bits. Replaced through D2RLoader's inline hook: the same three
//      branches in 64 bits, the sum held at 7FFFFFFFh.
//   2. Level scaling (3A26C0: data context, monstats row, column set,
//      difficulty, level, field flags, out int32[], extra): for flag 1 the
//      monstats MinHP / MaxHP WORD (signed) times monlvl.txt HP percent, a
//      32-bit product, / 100, into out[0] / out[1] (whole life). Hooked: after
//      the original, the same math in 64 bits; only when it differs from what
//      the game wrote (its product wrapped) the value is replaced, held at
//      8,388,607, the spawn cap.
//   3. Bind Demon (D2RCore 8316F0, ApplyBindDemonBoundStats): each bound stat
//      value from its calc is shifted left 8 for stats 6..11 and 216/217 at
//      83188A..8318A3 (26 bytes, only inbound branch 831892 from inside), then
//      set at 8318B0. The span jumps to a stub: life and max life shifted in 64
//      bits and held at 7FFFFFFFh; the other shifted stats exactly as before.
//   4. Life gained (D2RCore 3D9EE0, the stat add behind AddWideUnitStat): the
//      new value is add eax,r9d at 3D9FD7, 32 bits, no clamp, so a heal, leech
//      or any gain on a monster near the cap wrapped it negative. The 15 bytes
//      3D9FD7..3D9FE5 (add eax,r9d / mov qword [rsp+20h],0 / mov rsi,r8; no
//      branch lands inside) jump to a stub: for hitpoints and maxhp the add is
//      done in 64 bits and held to 32 bits; every other stat adds as before.
//   5. Regeneration tick (448C00): life + regen is add esi,ebx at 448D19, then
//      cmp esi,r14d (max) / jle 448D35, else 448D20 stops the regen event and
//      takes max. A wrapped sum is below max, so it skipped the clamp, became
//      0 at 448D35 and the monster died. 448D19..448D1F (7 bytes, the whole
//      function is branch-audited, nothing lands inside) jump to a stub that
//      does the add and the compare in 64 bits.
//   6. Max life change (D2RCore 3CC660, on a change of stat 7 / 9 / 11): the
//      current stat is rescaled in single-precision floats, current * newMax /
//      max(oldMax, 100h) at 3CC9C2..3CCA12, and a float result at or above
//      2^31 gives 1. Near the cap a float that large is reached easily (float
//      steps are 128 there), so a monster near the cap whose max life changed
//      (an aura, Battle Orders) dropped to 1 life. The span (no branch and no
//      jump-table target lands inside) jumps to a stub calling RescaleValue:
//      the game's float math step by step, except that for life a result at
//      or above 2^31 keeps the exact 64-bit share instead of 1. Mana and
//      stamina behave exactly as before.
//   7. Life set from a sum (D2RCore 3D9BF0, the stat set behind
//      SetWideUnitStat: context, unit, key, value). Dozens of skill, missile
//      and item functions heal by setting life to life + heal (or max life to
//      a computed sum) in 32 bits instead of adding; the same sum near the
//      cap wraps negative and the unit dies. A damage result cannot get
//      there: damage is at most 2^31 - 1, so life - damage is at least
//      old - (2^31 - 1), while old + heal wrapped is at most old - 2^31 - 1.
//      So a negative new value below old - (2^31 - 1) is always a wrapped
//      sum and never damage (kills set 0, not a negative value). The
//      entry (16 relocatable bytes: 8 pushes and sub rsp,58h) jumps to
//      HookCoreSet, which then sets life to max life (a heal stops at max) or
//      max life to the cap; every other set goes through unchanged.

#include <D2RLPlugin/api.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <initializer_list>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>

namespace CelestialRayOne::MonsterLifeCap {
namespace {

constexpr char PluginIdText[]  = "celestialrayone.monster-life-cap";
constexpr char PluginVersion[] = "1.0.1";

// ---------------------------------------------------------------------------
// Stats and game addresses
// ---------------------------------------------------------------------------

constexpr std::uint32_t StatHitpoints = 6;
constexpr std::uint32_t StatMaxHp     = 7;
constexpr std::int32_t  CapRaw        = std::numeric_limits<std::int32_t>::max();  // 8,388,607.99 life
constexpr std::int32_t  CapWhole      = 0x7FFFFF;                                  // 8,388,607 life

constexpr std::size_t MaximumConfigBytes = 16'384;

// Game image (RVAs)
constexpr std::uint64_t ReadUnitStatRva   = 0x2F5020;  // (unit, stat, layer) -> int32
constexpr std::uint64_t SetUnitStatRva    = 0x2F7D10;  // (unit, stat, value, layer)
constexpr std::uint64_t ReadMaxLifeRva    = 0x2F4D20;  // (unit) -> int32
constexpr std::uint64_t LifeMultiplierRva = 0x49BD50;
constexpr std::uint64_t LevelStatsRva     = 0x3A26C0;
constexpr std::uint64_t GetMonStatsRva    = 0x0976E0;  // (data context, row) -> monstats record
constexpr std::uint64_t GetDataTablesRva  = 0x300A90;  // (data context) -> DataTables
constexpr std::uint64_t BitMasksRva       = 0x1D996D8; // gdwBitMasks[2] (= 4)
constexpr std::uint64_t RegenAddRva       = 0x448D19;
constexpr std::uint64_t RegenClampRva     = 0x448D20;  // stop the regen event, life = max
constexpr std::uint64_t RegenStoreRva     = 0x448D35;  // life < 1 -> 0, then stat 6

constexpr std::size_t MonStatsMinHp     = 0x102;  // WORD x3 (normal, nightmare, hell)
constexpr std::size_t MonStatsMaxHp     = 0x108;  // WORD x3
constexpr std::size_t MonStatsFlags     = 0x3C;   // byte tested against gdwBitMasks[2]: no level scaling
constexpr std::size_t TablesMonLvl      = 0x1140; // monlvl.txt records
constexpr std::size_t TablesMonLvlCount = 0x1148;
constexpr std::size_t MonLvlRecordSize  = 0x78;

// mov [rsp+8],rbx / mov [rsp+10h],rsi / push rdi / sub rsp,20h / movsxd rdi,edx
constexpr std::uint8_t LifeMultiplierEntry[] {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20,
    0x48, 0x63, 0xFA,
};
// mov [rsp+18h],rbx / push rbp / push rsi / push rdi / push r12 / push r14 /
// sub rsp,40h / movsxd r14,dword [rsp+0A8h]
constexpr std::uint8_t LevelStatsEntry[] {
    0x48, 0x89, 0x5C, 0x24, 0x18, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x40,
    0x4C, 0x63, 0xB4, 0x24, 0xA8, 0x00, 0x00, 0x00,
};
// mov [rsp+8],rbx / mov [rsp+20h],rsi / push rdi / sub rsp,30h / movsxd rsi,edx / call 300A90
constexpr std::uint8_t GetMonStatsEntry[] {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57, 0x48, 0x83, 0xEC, 0x30,
    0x48, 0x63, 0xF2, 0xE8, 0x99, 0x93, 0x26, 0x00,
};
// sub rsp,28h / movzx eax,cl / mov [rsp+38h],rax / cmp rax,4 / jb
constexpr std::uint8_t GetDataTablesEntry[] {
    0x48, 0x83, 0xEC, 0x28, 0x0F, 0xB6, 0xC1, 0x48, 0x89, 0x44, 0x24, 0x38, 0x48, 0x83, 0xF8, 0x04,
    0x72, 0x19,
};
// add esi,ebx / cmp esi,r14d / jle 448D35
constexpr std::uint8_t RegenAddWitness[] { 0x03, 0xF3, 0x41, 0x3B, 0xF6, 0x7E, 0x15 };

// D2RCore (RVAs into D2RCore.dll)
constexpr wchar_t       CoreModuleName[]   = L"D2RCore.dll";
constexpr std::uint64_t CoreBindRva        = 0x83188A;
constexpr std::uint64_t CoreBindBackRva    = 0x8318A4;
constexpr std::uint64_t CoreAddRva         = 0x3D9FD7;
constexpr std::uint64_t CoreAddBackRva     = 0x3D9FE6;
constexpr std::uint64_t CoreRescaleRva     = 0x3CC9C2;
constexpr std::uint64_t CoreRescaleBackRva = 0x3CCA13;
constexpr std::uint64_t CoreSetRva         = 0x3D9BF0;
constexpr std::uint64_t HitpointsKey       = static_cast<std::uint64_t>(StatHitpoints) << 32;  // layer 0
constexpr std::uint64_t MaxHpKey           = static_cast<std::uint64_t>(StatMaxHp) << 32;

// lea ecx,[r12-6] / cmp ecx,6 / jb +0Dh / lea ecx,[r12-0D8h] / cmp ecx,1 / ja +3 / shl eax,8
constexpr std::uint8_t CoreBindWitness[] {
    0x41, 0x8D, 0x4C, 0x24, 0xFA, 0x83, 0xF9, 0x06, 0x72, 0x0D, 0x41, 0x8D, 0x8C, 0x24, 0x28,
    0xFF, 0xFF, 0xFF, 0x83, 0xF9, 0x01, 0x77, 0x03, 0xC1, 0xE0, 0x08,
};
// push r15 / push r14 / push r13 / push r12 / push rsi / push rdi / push rbp / push rbx /
// sub rsp,58h
constexpr std::uint8_t CoreSetEntry[] {
    0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x56, 0x57, 0x55, 0x53, 0x48, 0x83, 0xEC, 0x58,
};
// add eax,r9d / mov qword [rsp+20h],0 / mov rsi,r8
constexpr std::uint8_t CoreAddWitness[] {
    0x44, 0x01, 0xC8, 0x48, 0xC7, 0x44, 0x24, 0x20, 0x00, 0x00, 0x00, 0x00, 0x4C, 0x89, 0xC6,
};
// cvtsi2ss xmm0,r13d / cmp edi,101h / mov ecx,100h / cmovae ecx,edi / cvtsi2ss xmm1,ecx /
// cvtsi2ss xmm2,eax / divss xmm0,xmm1 / mulss xmm2,xmm0 / cvttss2si edi,xmm2 / cmp edi,2 /
// mov eax,1 / cmovl edi,eax / movss xmm0,[2^31] / ucomiss xmm0,xmm2 / cmovbe edi,eax /
// ucomiss xmm2,[-2^31] / cmovb edi,eax / cmp edi,r13d / cmovge edi,r13d
constexpr std::uint8_t CoreRescaleWitness[] {
    0xF3, 0x41, 0x0F, 0x2A, 0xC5, 0x81, 0xFF, 0x01, 0x01, 0x00, 0x00, 0xB9, 0x00, 0x01, 0x00, 0x00,
    0x0F, 0x43, 0xCF, 0xF3, 0x0F, 0x2A, 0xC9, 0xF3, 0x0F, 0x2A, 0xD0, 0xF3, 0x0F, 0x5E, 0xC1, 0xF3,
    0x0F, 0x59, 0xD0, 0xF3, 0x0F, 0x2C, 0xFA, 0x83, 0xFF, 0x02, 0xB8, 0x01, 0x00, 0x00, 0x00, 0x0F,
    0x4C, 0xF8, 0xF3, 0x0F, 0x10, 0x05, 0x48, 0x35, 0x26, 0x00, 0x0F, 0x2E, 0xC2, 0x0F, 0x46, 0xF8,
    0x0F, 0x2E, 0x15, 0x3F, 0x35, 0x26, 0x00, 0x0F, 0x42, 0xF8, 0x44, 0x39, 0xEF, 0x41, 0x0F, 0x4D,
    0xFD,
};

// ---------------------------------------------------------------------------
// Configuration (also the file's documentation)
// ---------------------------------------------------------------------------

constexpr char DefaultConfigToml[] = R"toml(# Monster Life Cap
#
# Keeps the life of every monster, summon and bound demon at or below
# 8,388,607, the most a D2R life stat can hold. Without it, life that goes
# past that number wraps around: the monster ends up with garbage life, or
# dies on the spot.
#
# Held at the cap by this plugin:
#   - champion, unique, minion, Possessed and Herald life bonuses
#   - Bind Demon's bound life and max life
#   - the monster level life multiplier (monlvl.txt HP), when it would wrap
#   - life gained from heals, leech and regeneration, also when a skill or
#     item sets life to "life + heal" (Engorge, life steal, monster heals)
#   - current life when max life changes (Battle Orders, auras): it keeps its
#     share instead of dropping to 1 near the cap
# Already held at the cap by the game, D2RLoader and D2RCore: a monster's
# life when it spawns, summon life, percent and flat max life bonuses.
#
# Below the cap everything behaves exactly as before. Do not load it together
# with Monster Life Bars: both change the same places.
#
# Console command: lifecap (installed parts and counters).

version = 1

# false = install nothing.
enabled = true
)toml";

struct Settings {
    bool enabled{true};
};

// ---------------------------------------------------------------------------
// Game functions
// ---------------------------------------------------------------------------

using ReadUnitStatFn   = std::int32_t(__fastcall*)(void* unit, std::uint32_t stat, std::uint32_t layer);
using SetUnitStatFn    = std::uint64_t(__fastcall*)(void* unit, std::uint32_t stat, std::int32_t value,
                             std::uint32_t layer);
using CoreSetFn        = std::uint64_t(__fastcall*)(void* context, void* unit, std::uint64_t key,
                             std::int32_t value);
using ReadMaxLifeFn    = std::int32_t(__fastcall*)(void* unit);
using LifeMultiplierFn = std::uint64_t(__fastcall*)(void* unit, std::int32_t percent);
using LevelStatsFn     = std::uint64_t(__fastcall*)(std::uint64_t dataContext, std::uint64_t row,
                             std::uint64_t columnSet, std::uint64_t difficulty, std::int32_t level,
                             std::uint64_t fields, std::int32_t* out, std::int32_t extra);
using GetMonStatsFn    = const std::uint8_t*(__fastcall*)(std::uint64_t dataContext, std::int32_t row);
using GetDataTablesFn  = const std::uint8_t*(__fastcall*)(std::uint64_t dataContext);

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

const D2RL::PluginContext* Context{};
std::uintptr_t             Base{};
std::uintptr_t             Core{};
Settings                   Config{};

ReadUnitStatFn   ReadUnitStat{};
SetUnitStatFn    SetUnitStat{};
CoreSetFn        OriginalCoreSet{};
ReadMaxLifeFn    ReadMaxLife{};
LifeMultiplierFn OriginalLifeMultiplier{};
LevelStatsFn     OriginalLevelStats{};
GetMonStatsFn    GetMonStats{};
GetDataTablesFn  GetDataTables{};

constexpr std::size_t StubPageBytes = 4'096;
std::uint8_t*         StubPage{};
std::size_t           StubUsed{};

struct CorePatch {
    std::uint8_t*                site{};
    std::array<std::uint8_t, 32> original{};
    std::array<std::uint8_t, 32> written{};
    std::size_t                  size{};
};
CorePatch CoreBindPatch{};
CorePatch CoreAddPatch{};
CorePatch CoreRescalePatch{};
CorePatch CoreSetPatch{};

struct Installed {
    bool multiplier{};
    bool levelStats{};
    bool bind{};
    bool lifeAdd{};
    bool regen{};
    bool rescale{};
    bool wrappedSets{};
};
Installed Parts{};

std::atomic<std::uint64_t> MultiplierCapped{0};
std::atomic<std::uint64_t> LevelFixed{0};
std::atomic<std::uint64_t> BindCapped{0};
std::atomic<std::uint64_t> AddsHeld{0};      // incremented by the life-add stub
std::atomic<std::uint64_t> RegenHeld{0};     // incremented by the regen stub
std::atomic<std::uint64_t> RescaleKept{0};
std::atomic<std::uint64_t> WrappedSets{0};
static_assert(sizeof(std::atomic<std::uint64_t>) == 8 && std::atomic<std::uint64_t>::is_always_lock_free);

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

template <typename T>
auto At(std::uint64_t rva) noexcept -> T {
    return reinterpret_cast<T>(Base + rva);
}

template <std::size_t Size>
constexpr auto ByteCount(const std::uint8_t (&)[Size]) noexcept -> std::uint32_t {
    return static_cast<std::uint32_t>(Size);
}

// a * b saturated to 64 bits.
auto SafeMul(std::int64_t a, std::int64_t b) noexcept -> std::int64_t {
    constexpr std::int64_t Max = std::numeric_limits<std::int64_t>::max();
    constexpr std::int64_t Min = std::numeric_limits<std::int64_t>::min();
    if (a == 0 || b == 0) return 0;
    const auto magnitude = [](std::int64_t v) {
        return v < 0 ? 0ULL - static_cast<std::uint64_t>(v) : static_cast<std::uint64_t>(v);
    };
    const bool negative = (a < 0) != (b < 0);
    if (magnitude(a) > static_cast<std::uint64_t>(Max) / magnitude(b)) return negative ? Min : Max;
    return a * b;
}

auto Saturate(std::int64_t value) noexcept -> std::int32_t {
    return static_cast<std::int32_t>(std::clamp<std::int64_t>(value,
        std::numeric_limits<std::int32_t>::min(), std::numeric_limits<std::int32_t>::max()));
}

// ---------------------------------------------------------------------------
// 1. Monster-mod life (champion, unique, minion, Possessed, Herald)
// ---------------------------------------------------------------------------

// 49BD50's three branches with 64-bit products. Identical to the game whenever
// its 32-bit products do not overflow.
auto MultiplierDelta(std::int64_t max, std::int32_t percent) noexcept -> std::int64_t {
    if (max > 0x100000) return SafeMul(max / 100, percent);
    const auto max32 = static_cast<std::int32_t>(max);
    if (percent > 0x10000) return static_cast<std::int64_t>(static_cast<std::uint32_t>(percent) / 100U) * max32;
    return (static_cast<std::int64_t>(max32) * percent) / 100;
}

auto MonsterModLife(std::int32_t max, std::int32_t percent) noexcept -> std::int32_t {
    const std::int64_t life = static_cast<std::int64_t>(max) + MultiplierDelta(max, percent);
    if (life > CapRaw) {
        MultiplierCapped.fetch_add(1, std::memory_order_relaxed);
        return CapRaw;
    }
    return Saturate(life);
}

std::uint64_t __fastcall HookLifeMultiplier(void* unit, std::int32_t percent) noexcept {
    const std::int32_t value = MonsterModLife(ReadMaxLife(unit), percent);
    SetUnitStat(unit, StatMaxHp, value, 0);
    return SetUnitStat(unit, StatHitpoints, value, 0);
}

// ---------------------------------------------------------------------------
// 2. Level scaling: monstats base life x monlvl.txt HP percent
// ---------------------------------------------------------------------------

// base * percent / 100 with 3A26C0's three rounding branches, in 64 bits.
auto ScaleByPercent(std::int64_t base, std::int32_t percent) noexcept -> std::int64_t {
    if (percent <= 0x100000) return (base * percent) / 100;
    if ((static_cast<std::uint32_t>(percent) & 0xFFFFFFF0U) >= 1600U) return base * (percent / 100);
    return (base * percent) / 100;
}

// The game's value when its product did not wrap, else the exact value held
// at the spawn cap.
auto LevelLife(std::int32_t written, std::int64_t exact) noexcept -> std::int32_t {
    if (exact == written) return written;
    LevelFixed.fetch_add(1, std::memory_order_relaxed);
    return static_cast<std::int32_t>(std::clamp<std::int64_t>(exact,
        std::numeric_limits<std::int32_t>::min(), CapWhole));
}

std::uint64_t __fastcall HookLevelStats(std::uint64_t dataContext, std::uint64_t row, std::uint64_t columnSet,
        std::uint64_t difficulty, std::int32_t level, std::uint64_t fields, std::int32_t* out,
        std::int32_t extra) noexcept {
    const std::uint64_t result =
        OriginalLevelStats(dataContext, row, columnSet, difficulty, level, fields, out, extra);
    if (static_cast<std::uint32_t>(result) == 0 || out == nullptr || (fields & 1U) == 0) return result;

    const auto          context = static_cast<std::uint8_t>(dataContext);
    const std::uint8_t* record  = GetMonStats(context, static_cast<std::int32_t>(row));
    if (record == nullptr) return result;
    std::uint32_t mask = 0;
    std::memcpy(&mask, reinterpret_cast<const void*>(Base + BitMasksRva), sizeof(mask));
    if ((record[MonStatsFlags] & static_cast<std::uint8_t>(mask)) != 0) return result;  // no product

    const std::uint8_t* tables = GetDataTables(context);
    if (tables == nullptr) return result;
    std::uint64_t       count  = 0;
    const std::uint8_t* levels = nullptr;
    std::memcpy(&count, tables + TablesMonLvlCount, sizeof(count));
    std::memcpy(&levels, tables + TablesMonLvl, sizeof(levels));
    if (count == 0 || levels == nullptr) return result;
    std::int64_t index = level;
    if (index >= static_cast<std::int64_t>(count) - 1) index = static_cast<std::int64_t>(count) - 1;
    if (index < 0) return result;

    const std::int32_t  diff        = std::min(std::max(static_cast<std::int32_t>(difficulty), 0), 2);
    const std::uint8_t* levelRecord = levels + MonLvlRecordSize * static_cast<std::uint64_t>(index);
    const std::int64_t  column      = (static_cast<std::int32_t>(columnSet) != 0 ? 15 : 12) + diff;
    std::int32_t        percent     = 0;
    std::memcpy(&percent, levelRecord + 4 * column, sizeof(percent));
    std::int16_t minBase = 0;
    std::int16_t maxBase = 0;
    std::memcpy(&minBase, record + MonStatsMinHp + 2 * diff, sizeof(minBase));
    std::memcpy(&maxBase, record + MonStatsMaxHp + 2 * diff, sizeof(maxBase));
    out[0] = LevelLife(out[0], ScaleByPercent(minBase, percent));
    out[1] = LevelLife(out[1], ScaleByPercent(maxBase, percent));
    return result;
}

// ---------------------------------------------------------------------------
// 3. Bind Demon bound stats (called from the stub at D2RCore 83188A)
// ---------------------------------------------------------------------------

std::int32_t __fastcall BoundStatValue(std::uint32_t stat, std::int32_t calc, void*) noexcept {
    const bool shifted = stat - 6U < 6U || stat - 0xD8U <= 1U;
    if (!shifted) return calc;
    if (stat != StatHitpoints && stat != StatMaxHp) {
        return static_cast<std::int32_t>(static_cast<std::uint32_t>(calc) << 8);   // as before
    }
    const std::int64_t value = static_cast<std::int64_t>(calc) * 256;
    if (value > CapRaw) {
        BindCapped.fetch_add(1, std::memory_order_relaxed);
        return CapRaw;
    }
    return Saturate(value);
}

// ---------------------------------------------------------------------------
// 6. Max life change (called from the stub at D2RCore 3CC9C2)
// ---------------------------------------------------------------------------

// stat is the stat being rescaled (6 life, 8 mana, 10 stamina), current its
// value (> 0), oldMax (> 0) and newMax the max before and after. The game's
// float math, step by step; only for life a float result at or above 2^31
// keeps the exact share instead of becoming 1.
std::int32_t __fastcall RescaleValue(std::uint32_t stat, std::int32_t current, std::int32_t oldMax,
        std::int32_t newMax) noexcept {
    const std::int32_t divisor = static_cast<std::uint32_t>(oldMax) >= 0x101U ? oldMax : 0x100;
    const float        ratio   = static_cast<float>(newMax) / static_cast<float>(divisor);
    const float        product = static_cast<float>(current) * ratio;
    const bool         high    = !(product < 2147483648.0F);   // also NaN, like ucomiss
    const bool         low     = product < -2147483648.0F;
    std::int32_t value = (high || low) ? std::numeric_limits<std::int32_t>::min() : static_cast<std::int32_t>(product);
    if (value < 2) value = 1;
    if (high || low) value = 1;
    if (high && stat == StatHitpoints) {
        const std::int64_t exact = static_cast<std::int64_t>(current) * newMax / divisor;
        value = static_cast<std::int32_t>(std::clamp<std::int64_t>(exact, 1, CapRaw));
        RescaleKept.fetch_add(1, std::memory_order_relaxed);
    }
    if (value >= newMax) value = newMax;
    return value;
}

// ---------------------------------------------------------------------------
// 7. Life set from a sum that wrapped (D2RCore 3D9BF0)
// ---------------------------------------------------------------------------

// The value a life or max life set should get. Only a wrapped sum is changed:
// old - damage is never below old - (2^31 - 1), old + heal wrapped always is.
auto UnwrappedSet(std::uint64_t key, std::int32_t old, std::int32_t value, std::int32_t maxLife) noexcept
        -> std::int32_t {
    const std::int64_t lowestDamage = static_cast<std::int64_t>(old) - std::numeric_limits<std::int32_t>::max();
    if ((key != HitpointsKey && key != MaxHpKey) || value >= 0 || value >= lowestDamage) return value;
    WrappedSets.fetch_add(1, std::memory_order_relaxed);
    if (key == MaxHpKey) return CapRaw;
    return std::max(maxLife, 1);   // a heal that went past the cap stops at max life
}

std::uint64_t __fastcall HookCoreSet(void* context, void* unit, std::uint64_t key, std::int32_t value) noexcept {
    if ((key == HitpointsKey || key == MaxHpKey) && value < 0 && unit != nullptr) {
        const std::int32_t maxLife = ReadMaxLife(unit);
        const std::int32_t old     = key == HitpointsKey ? ReadUnitStat(unit, StatHitpoints, 0) : maxLife;
        value = UnwrappedSet(key, old, value, maxLife);
    }
    return OriginalCoreSet(context, unit, key, value);
}

// ---------------------------------------------------------------------------
// Code writing
// ---------------------------------------------------------------------------

auto CanEncodeRel32(std::uintptr_t from, std::uintptr_t to) noexcept -> bool {
    const std::int64_t delta = static_cast<std::int64_t>(to) - static_cast<std::int64_t>(from) - 5;
    return delta >= std::numeric_limits<std::int32_t>::min() && delta <= std::numeric_limits<std::int32_t>::max();
}

auto AllocateNear(std::uintptr_t hint, std::size_t size) noexcept -> void* {
    SYSTEM_INFO systemInfo{};
    GetSystemInfo(&systemInfo);
    const auto granularity = static_cast<std::uintptr_t>(systemInfo.dwAllocationGranularity);
    const auto aligned     = hint & ~(granularity - 1U);
    for (std::uintptr_t delta = granularity; delta < 0x7000'0000ULL; delta += granularity) {
        const auto candidate = aligned + delta;
        if (!CanEncodeRel32(hint, candidate + size)) break;
        if (auto* memory = VirtualAlloc(reinterpret_cast<void*>(candidate), size, MEM_COMMIT | MEM_RESERVE,
                PAGE_READWRITE)) {
            return memory;
        }
    }
    return nullptr;
}

class Emitter {
  public:
    explicit Emitter(std::uint8_t* out) noexcept : out_(out) {}
    void Bytes(std::initializer_list<std::uint8_t> bytes) noexcept {
        for (const std::uint8_t b : bytes) out_[size_++] = b;
    }
    void Raw(const std::uint8_t* bytes, std::size_t count) noexcept {
        std::memcpy(out_ + size_, bytes, count);
        size_ += count;
    }
    void U32(std::uint32_t value) noexcept {
        std::memcpy(out_ + size_, &value, sizeof(value));
        size_ += sizeof(value);
    }
    void U64(std::uint64_t value) noexcept {
        std::memcpy(out_ + size_, &value, sizeof(value));
        size_ += sizeof(value);
    }
    void MovRaxImm(std::uintptr_t value) noexcept {
        Bytes({0x48, 0xB8});
        U64(value);
    }
    void MovR11Imm(std::uintptr_t value) noexcept {
        Bytes({0x49, 0xBB});
        U64(value);
    }
    void MovRcxImm(std::uintptr_t value) noexcept {
        Bytes({0x48, 0xB9});
        U64(value);
    }
    void JumpAbsolute(std::uintptr_t target) noexcept {
        Bytes({0xFF, 0x25, 0x00, 0x00, 0x00, 0x00});
        U64(target);
    }
    // Short conditional jump to a label written later; returns the offset to patch.
    auto ShortJump(std::uint8_t opcode) noexcept -> std::size_t {
        Bytes({opcode, 0x00});
        return size_ - 1;
    }
    void Land(std::size_t at) noexcept { out_[at] = static_cast<std::uint8_t>(size_ - (at + 1)); }
    [[nodiscard]] auto Size() const noexcept -> std::size_t { return size_; }

  private:
    std::uint8_t* out_{};
    std::size_t   size_{};
};

// Reserves room in the stub page, 16-byte aligned.
auto Reserve(std::size_t bytes) noexcept -> std::uint8_t* {
    StubUsed = (StubUsed + 15U) & ~static_cast<std::size_t>(15U);
    if (StubPage == nullptr || StubUsed + bytes > StubPageBytes) return nullptr;
    std::uint8_t* at = StubPage + StubUsed;
    StubUsed += bytes;
    return at;
}

auto WriteCode(std::uint8_t* address, const std::uint8_t* bytes, std::size_t size) noexcept -> bool {
    DWORD previous = 0;
    if (!VirtualProtect(address, size, PAGE_EXECUTE_READWRITE, &previous)) return false;
    std::memcpy(address, bytes, size);
    DWORD ignored = 0;
    VirtualProtect(address, size, previous, &ignored);
    FlushInstructionCache(GetCurrentProcess(), address, size);
    return std::memcmp(address, bytes, size) == 0;
}

auto IsCommittedIn(const void* address, std::size_t size, HMODULE module) noexcept -> bool {
    MEMORY_BASIC_INFORMATION info{};
    if (VirtualQuery(address, &info, sizeof(info)) != sizeof(info)) return false;
    if (info.State != MEM_COMMIT || info.AllocationBase != module) return false;
    const auto end = reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize;
    return reinterpret_cast<std::uintptr_t>(address) + size <= end;
}

// Writes a patch inside D2RCore after checking the bytes it replaces.
auto PatchCore(CorePatch& patch, std::uint64_t rva, const std::uint8_t* expected, std::size_t expectedSize,
        const std::uint8_t* bytes, std::size_t size, const char* what) noexcept -> bool {
    auto* site = reinterpret_cast<std::uint8_t*>(Core + rva);
    if (size > patch.original.size()
            || !IsCommittedIn(site, std::max(expectedSize, size), reinterpret_cast<HMODULE>(Core))
            || std::memcmp(site, expected, expectedSize) != 0) {
        D2RL::LogErrorF(Context, "LifeCap: D2RCore+%llX (%s) does not hold the expected bytes, or another plugin "
            "owns them; that part is off.", static_cast<unsigned long long>(rva), what);
        return false;
    }
    patch.site = site;
    patch.size = size;
    std::memcpy(patch.original.data(), site, size);
    std::memcpy(patch.written.data(), bytes, size);
    if (!WriteCode(site, bytes, size)) {
        D2RL::LogErrorF(Context, "LifeCap: D2RCore+%llX (%s) could not be written.",
            static_cast<unsigned long long>(rva), what);
        patch.site = nullptr;
        return false;
    }
    return true;
}

void UnpatchCore(CorePatch& patch) noexcept {
    if (patch.site == nullptr) return;
    if (std::memcmp(patch.site, patch.written.data(), patch.size) == 0) {
        WriteCode(patch.site, patch.original.data(), patch.size);
    }
    patch.site = nullptr;
}

auto EncodeRel32(std::uint8_t opcode, std::uintptr_t site, std::uintptr_t target, std::uint8_t* out) noexcept
        -> bool {
    if (!CanEncodeRel32(site, target)) return false;
    out[0] = opcode;
    const auto displacement = static_cast<std::int32_t>(
        static_cast<std::int64_t>(target) - static_cast<std::int64_t>(site + 5));
    std::memcpy(out + 1, &displacement, sizeof(displacement));
    return true;
}

auto PatchGame(std::uint64_t rva, const std::uint8_t* expected, std::uint32_t expectedSize,
        const std::uint8_t* bytes, std::uint32_t size, const char* what) noexcept -> bool {
    if (Context->PatchBytes(rva, expected, expectedSize, bytes, size)) return true;
    D2RL::LogErrorF(Context, "LifeCap: %llX (%s) does not hold the expected bytes, or another plugin owns them; "
        "that part is off.", static_cast<unsigned long long>(rva), what);
    return false;
}

template <typename Fn>
auto Hook(std::uint64_t rva, const std::uint8_t* entry, std::uint32_t entrySize, void* target, Fn& original,
        const char* what) noexcept -> bool {
    void* trampoline = nullptr;
    if (Context->InstallInlineHook(rva, entry, entrySize, target, &trampoline) && trampoline != nullptr) {
        original = reinterpret_cast<Fn>(trampoline);
        return true;
    }
    D2RL::LogErrorF(Context, "LifeCap: %llX (%s) could not be hooked (bytes differ or another plugin owns it); "
        "that part is off.", static_cast<unsigned long long>(rva), what);
    return false;
}

// ---------------------------------------------------------------------------
// Installing
// ---------------------------------------------------------------------------

auto InstallMultiplier() noexcept -> bool {
    return Hook(LifeMultiplierRva, LifeMultiplierEntry, ByteCount(LifeMultiplierEntry),
        reinterpret_cast<void*>(&HookLifeMultiplier), OriginalLifeMultiplier, "monster-mod life");
}

auto InstallLevelStats() noexcept -> bool {
    if (!Context->CheckExpectedBytes(GetMonStatsRva, GetMonStatsEntry, ByteCount(GetMonStatsEntry))
            || !Context->CheckExpectedBytes(GetDataTablesRva, GetDataTablesEntry, ByteCount(GetDataTablesEntry))) {
        Context->LogError("LifeCap: the monstats / data table lookups do not hold the expected bytes; level "
                          "scaling is left as it is.");
        return false;
    }
    GetMonStats   = At<GetMonStatsFn>(GetMonStatsRva);
    GetDataTables = At<GetDataTablesFn>(GetDataTablesRva);
    return Hook(LevelStatsRva, LevelStatsEntry, ByteCount(LevelStatsEntry), reinterpret_cast<void*>(&HookLevelStats),
        OriginalLevelStats, "level scaling");
}

// 83188A..8318A3: eax = calc value, r12d = stat, rsi = unit; 8318A4 sets the stat.
auto InstallBind() noexcept -> bool {
    std::uint8_t* stub = Reserve(64);
    if (stub == nullptr) return false;
    Emitter s(stub);
    s.Bytes({0x48, 0x83, 0xEC, 0x20});   // sub rsp,20h
    s.Bytes({0x44, 0x89, 0xE1});         // mov ecx,r12d     stat
    s.Bytes({0x89, 0xC2});               // mov edx,eax      calc
    s.Bytes({0x49, 0x89, 0xF0});         // mov r8,rsi       unit
    s.MovRaxImm(reinterpret_cast<std::uintptr_t>(&BoundStatValue));
    s.Bytes({0xFF, 0xD0});               // call rax
    s.Bytes({0x48, 0x83, 0xC4, 0x20});   // add rsp,20h
    s.JumpAbsolute(Core + CoreBindBackRva);

    std::uint8_t patch[sizeof(CoreBindWitness)]{};
    Emitter p(patch);
    p.JumpAbsolute(reinterpret_cast<std::uintptr_t>(stub));
    while (p.Size() < sizeof(patch)) p.Bytes({0x90});
    return PatchCore(CoreBindPatch, CoreBindRva, CoreBindWitness, sizeof(CoreBindWitness), patch, sizeof(patch),
        "Bind Demon bound stats");
}

// 3D9FD7..3D9FE5: eax = the stat's current value, r9d = the amount, r8 = key
// (stat << 32 | layer). r10 and r11 are free here.
auto InstallLifeAdd() noexcept -> bool {
    std::uint8_t* stub = Reserve(128);
    if (stub == nullptr) return false;
    Emitter s(stub);
    s.Bytes({0x4D, 0x89, 0xC2});                      // mov r10,r8
    s.Bytes({0x49, 0xC1, 0xEA, 0x20});                // shr r10,32           stat id
    s.Bytes({0x66, 0x41, 0x83, 0xFA, 0x06});          // cmp r10w,6
    const std::size_t toLifeA = s.ShortJump(0x74);    // je life
    s.Bytes({0x66, 0x41, 0x83, 0xFA, 0x07});          // cmp r10w,7
    const std::size_t toLifeB = s.ShortJump(0x74);    // je life
    s.Bytes({0x44, 0x01, 0xC8});                      // add eax,r9d          every other stat, as before
    const std::size_t toDone = s.ShortJump(0xEB);     // jmp done
    s.Land(toLifeA);
    s.Land(toLifeB);
    s.Bytes({0x4C, 0x63, 0xD0});                      // movsxd r10,eax
    s.Bytes({0x4D, 0x63, 0xD9});                      // movsxd r11,r9d
    s.Bytes({0x4D, 0x01, 0xDA});                      // add r10,r11          64-bit sum
    s.Bytes({0x49, 0xC7, 0xC3, 0xFF, 0xFF, 0xFF, 0x7F});  // mov r11,7FFFFFFFh
    s.Bytes({0x4D, 0x39, 0xDA});                      // cmp r10,r11
    const std::size_t toNotHigh = s.ShortJump(0x7E);  // jle notHigh
    s.Bytes({0x4D, 0x89, 0xDA});                      // mov r10,r11
    const std::size_t toCount = s.ShortJump(0xEB);    // jmp count
    s.Land(toNotHigh);
    s.Bytes({0x49, 0xC7, 0xC3, 0x00, 0x00, 0x00, 0x80});  // mov r11,-80000000h
    s.Bytes({0x4D, 0x39, 0xDA});                      // cmp r10,r11
    const std::size_t toStore = s.ShortJump(0x7D);    // jge store
    s.Bytes({0x4D, 0x89, 0xDA});                      // mov r10,r11
    s.Land(toCount);
    s.MovR11Imm(reinterpret_cast<std::uintptr_t>(&AddsHeld));
    s.Bytes({0xF0, 0x49, 0xFF, 0x03});                // lock inc qword [r11]
    s.Land(toStore);
    s.Bytes({0x44, 0x89, 0xD0});                      // mov eax,r10d
    s.Land(toDone);
    s.Bytes({0x48, 0xC7, 0x44, 0x24, 0x20, 0x00, 0x00, 0x00, 0x00});  // mov qword [rsp+20h],0
    s.Bytes({0x4C, 0x89, 0xC6});                      // mov rsi,r8
    s.JumpAbsolute(Core + CoreAddBackRva);

    std::uint8_t patch[sizeof(CoreAddWitness)]{};
    Emitter p(patch);
    p.JumpAbsolute(reinterpret_cast<std::uintptr_t>(stub));
    while (p.Size() < sizeof(patch)) p.Bytes({0x90});
    return PatchCore(CoreAddPatch, CoreAddRva, CoreAddWitness, sizeof(CoreAddWitness), patch, sizeof(patch),
        "life gained");
}

// 448D19..448D1F: esi = life, ebx = regen amount, r14d = max life. rax and rcx
// are free here (both are rewritten at 448D20 and 448D35 before any read).
auto InstallRegen() noexcept -> bool {
    std::uint8_t* stub = Reserve(96);
    if (stub == nullptr) return false;
    Emitter s(stub);
    s.Bytes({0x48, 0x63, 0xC6});                      // movsxd rax,esi
    s.Bytes({0x48, 0x63, 0xCB});                      // movsxd rcx,ebx
    s.Bytes({0x48, 0x01, 0xC8});                      // add rax,rcx          64-bit life + regen
    s.Bytes({0x48, 0xC7, 0xC1, 0xFF, 0xFF, 0xFF, 0x7F});  // mov rcx,7FFFFFFFh
    s.Bytes({0x48, 0x39, 0xC8});                      // cmp rax,rcx
    const std::size_t toFits = s.ShortJump(0x7E);     // jle fits
    s.MovRcxImm(reinterpret_cast<std::uintptr_t>(&RegenHeld));
    s.Bytes({0xF0, 0x48, 0xFF, 0x01});                // lock inc qword [rcx]  (the game would have wrapped)
    s.Land(toFits);
    s.Bytes({0x49, 0x63, 0xCE});                      // movsxd rcx,r14d
    s.Bytes({0x48, 0x39, 0xC8});                      // cmp rax,rcx
    const std::size_t toAbove = s.ShortJump(0x7F);    // jg above
    s.Bytes({0x89, 0xC6});                            // mov esi,eax
    s.JumpAbsolute(Base + RegenStoreRva);
    s.Land(toAbove);
    s.JumpAbsolute(Base + RegenClampRva);

    std::uint8_t patch[sizeof(RegenAddWitness)]{};
    if (!EncodeRel32(0xE9, Base + RegenAddRva, reinterpret_cast<std::uintptr_t>(stub), patch)) return false;
    std::memset(patch + 5, 0x90, sizeof(patch) - 5);
    return PatchGame(RegenAddRva, RegenAddWitness, ByteCount(RegenAddWitness), patch, sizeof(patch),
        "regeneration");
}

// 3CC9C2..3CCA12: eax = current value of stat r12d (6 / 8 / 10), edi = old
// max, r13d = new max; 3CCA13 sets the stat to edi. The function body keeps
// rsp 16-byte aligned (8 pushes + sub rsp,98h), xmm0..xmm5 are not read after
// the span.
auto InstallRescale() noexcept -> bool {
    std::uint8_t* stub = Reserve(64);
    if (stub == nullptr) return false;
    Emitter s(stub);
    s.Bytes({0x48, 0x83, 0xEC, 0x20});   // sub rsp,20h
    s.Bytes({0x44, 0x89, 0xE1});         // mov ecx,r12d     stat
    s.Bytes({0x89, 0xC2});               // mov edx,eax      current
    s.Bytes({0x41, 0x89, 0xF8});         // mov r8d,edi      old max
    s.Bytes({0x45, 0x89, 0xE9});         // mov r9d,r13d     new max
    s.MovRaxImm(reinterpret_cast<std::uintptr_t>(&RescaleValue));
    s.Bytes({0xFF, 0xD0});               // call rax
    s.Bytes({0x48, 0x83, 0xC4, 0x20});   // add rsp,20h
    s.Bytes({0x89, 0xC7});               // mov edi,eax
    s.JumpAbsolute(Core + CoreRescaleBackRva);

    std::uint8_t patch[14]{};
    Emitter p(patch);
    p.JumpAbsolute(reinterpret_cast<std::uintptr_t>(stub));
    return PatchCore(CoreRescalePatch, CoreRescaleRva, CoreRescaleWitness, sizeof(CoreRescaleWitness), patch,
        sizeof(patch), "max life change");
}

// Entry of 3D9BF0: the 16 bytes (8 pushes, sub rsp,58h) are copied to a
// trampoline that continues at 3D9C00; the entry jumps to HookCoreSet.
auto InstallWrappedSets() noexcept -> bool {
    std::uint8_t* trampoline = Reserve(sizeof(CoreSetEntry) + 14);
    if (trampoline == nullptr) return false;
    Emitter t(trampoline);
    t.Raw(CoreSetEntry, sizeof(CoreSetEntry));
    t.JumpAbsolute(Core + CoreSetRva + sizeof(CoreSetEntry));
    OriginalCoreSet = reinterpret_cast<CoreSetFn>(trampoline);

    std::uint8_t patch[sizeof(CoreSetEntry)]{};
    Emitter p(patch);
    p.JumpAbsolute(reinterpret_cast<std::uintptr_t>(&HookCoreSet));
    while (p.Size() < sizeof(patch)) p.Bytes({0x90});
    return PatchCore(CoreSetPatch, CoreSetRva, CoreSetEntry, sizeof(CoreSetEntry), patch, sizeof(patch),
        "life set from a sum");
}

void ReportMissing() noexcept {
    std::string missing;
    const auto add = [&](bool ok, const char* name) {
        if (ok) return;
        if (!missing.empty()) missing += ", ";
        missing += name;
    };
    add(Parts.multiplier, "monster-mod life");
    add(Parts.levelStats, "level scaling");
    add(Parts.bind, "Bind Demon");
    add(Parts.lifeAdd, "life gained");
    add(Parts.regen, "regeneration");
    add(Parts.rescale, "max life change");
    add(Parts.wrappedSets, "life set from a sum");
    if (missing.empty()) {
        Context->LogInfo("LifeCap: every part installed.");
    } else {
        D2RL::LogWarnF(Context, "LifeCap: not installed: %s. Those places behave as before.", missing.c_str());
    }
}

auto InstallAll() noexcept -> bool {
    const auto core = GetModuleHandleW(CoreModuleName);
    if (core == nullptr) {
        Context->LogError("LifeCap: D2RCore.dll is not loaded.");
        return false;
    }
    Core = reinterpret_cast<std::uintptr_t>(core);
    StubPage = static_cast<std::uint8_t*>(AllocateNear(Base + RegenAddRva, StubPageBytes));
    if (StubPage == nullptr) {
        Context->LogError("LifeCap: no memory for the stubs near the game image.");
        return false;
    }
    std::memset(StubPage, 0xCC, StubPageBytes);
    ReadUnitStat = At<ReadUnitStatFn>(ReadUnitStatRva);
    SetUnitStat = At<SetUnitStatFn>(SetUnitStatRva);
    ReadMaxLife = At<ReadMaxLifeFn>(ReadMaxLifeRva);

    // Stubs are written before any site points at them.
    Parts.multiplier = InstallMultiplier();
    Parts.levelStats = InstallLevelStats();
    Parts.bind       = InstallBind();
    Parts.lifeAdd    = InstallLifeAdd();
    Parts.regen      = InstallRegen();
    Parts.rescale    = InstallRescale();
    Parts.wrappedSets = InstallWrappedSets();

    DWORD previous = 0;
    if (!VirtualProtect(StubPage, StubPageBytes, PAGE_EXECUTE_READ, &previous)) {
        Context->LogError("LifeCap: the stub page could not be made executable.");
        return false;
    }
    FlushInstructionCache(GetCurrentProcess(), StubPage, StubPageBytes);
    ReportMissing();
    return true;
}

void UninstallCore() noexcept {
    UnpatchCore(CoreSetPatch);
    UnpatchCore(CoreRescalePatch);
    UnpatchCore(CoreAddPatch);
    UnpatchCore(CoreBindPatch);
}

// ---------------------------------------------------------------------------
// Configuration and console
// ---------------------------------------------------------------------------

auto Trim(std::string_view text) noexcept -> std::string_view {
    const auto first = text.find_first_not_of(" \t\r");
    if (first == std::string_view::npos) return {};
    const auto last = text.find_last_not_of(" \t\r");
    return text.substr(first, last - first + 1);
}

void ParseConfig(std::string_view text) noexcept {
    std::size_t cursor = 0;
    while (cursor < text.size()) {
        const std::size_t breakAt = text.find('\n', cursor);
        const std::size_t end     = breakAt == std::string_view::npos ? text.size() : breakAt;
        std::string_view  line    = Trim(text.substr(cursor, end - cursor));
        cursor                    = end + 1;
        const std::size_t comment = line.find('#');
        if (comment != std::string_view::npos) line = Trim(line.substr(0, comment));
        if (line.empty() || line.front() == '[') continue;
        const std::size_t equals = line.find('=');
        if (equals == std::string_view::npos) continue;
        const std::string_view key   = Trim(line.substr(0, equals));
        const std::string_view value = Trim(line.substr(equals + 1));
        if (key == "enabled") {
            if (value == "true") Config.enabled = true;
            else if (value == "false") Config.enabled = false;
            else Context->LogWarn("LifeCap: enabled is not true or false; keeping true.");
        }
    }
}

void ReadConfiguration() noexcept {
    if (!Context->EnsureConfig(DefaultConfigToml)) {
        Context->LogWarn("LifeCap: the config file could not be created; using defaults.");
        return;
    }
    std::string   buffer(MaximumConfigBytes, '\0');
    std::uint32_t required = 0;
    if (!Context->ReadConfig(buffer.data(), static_cast<std::uint32_t>(buffer.size()), &required)) {
        Context->LogWarn("LifeCap: the config file could not be read; using defaults.");
        return;
    }
    buffer.resize(std::strlen(buffer.c_str()));
    ParseConfig(buffer);
}

auto OnOff(bool on) noexcept -> const char* { return on ? "on" : "OFF"; }

auto __cdecl StatusCommand(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command, void*) noexcept
        -> D2RL::ConsoleCommandResult {
    char line[512];
    std::snprintf(line, sizeof(line), "Monster Life Cap %s: %s. Cap 8,388,607 life.", PluginVersion,
        Config.enabled ? "enabled" : "disabled by config");
    command->plugin->WriteConsoleMessage(line);
    std::snprintf(line, sizeof(line), "Parts: monster-mod life %s, level scaling %s, Bind Demon %s, life gained %s, "
        "regeneration %s, max life change %s, life set from a sum %s.", OnOff(Parts.multiplier),
        OnOff(Parts.levelStats), OnOff(Parts.bind), OnOff(Parts.lifeAdd), OnOff(Parts.regen), OnOff(Parts.rescale),
        OnOff(Parts.wrappedSets));
    command->plugin->WriteConsoleMessage(line);
    std::snprintf(line, sizeof(line), "Held at the cap: monster-mod life %llu, level scaling %llu, Bind Demon %llu, "
        "life gained %llu, regeneration %llu, max life change kept %llu, life set from a sum %llu.",
        static_cast<unsigned long long>(MultiplierCapped.load()), static_cast<unsigned long long>(LevelFixed.load()),
        static_cast<unsigned long long>(BindCapped.load()), static_cast<unsigned long long>(AddsHeld.load()),
        static_cast<unsigned long long>(RegenHeld.load()), static_cast<unsigned long long>(RescaleKept.load()),
        static_cast<unsigned long long>(WrappedSets.load()));
    command->plugin->WriteConsoleMessage(line);
    return D2RL::ConsoleCommandResult::Handled;
}

constexpr D2RL::PluginInfo Info{
    .infoSize    = D2RL::PluginInfoSize,
    .abiVersion  = D2RL_PLUGIN_ABI_VERSION,
    .id          = PluginIdText,
    .name        = "Monster Life Cap",
    .version     = PluginVersion,
    .author      = "CelestialRayOne",
    .description = "Holds monster, summon and bound demon life at 8,388,607 instead of letting it wrap.",
    .flags       = D2RL::PluginFlags::Server | D2RL::PluginFlags::NativeHooks,
};

}  // namespace

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* {
    return &Info;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
    if (!D2RL::HasContext(context)) return false;
    Context = context;
    Base    = context->exeBase;
    if (Base == 0) {
        Context->LogError("LifeCap: the game image base is unknown.");
        return false;
    }
    ReadConfiguration();
    if (!Context->RegisterConsoleCommand("lifecap", &StatusCommand,
            "Monster Life Cap: installed parts and how often each held life at the cap.")) {
        Context->LogWarn("LifeCap: the console command was refused.");
    }
    if (!Config.enabled) {
        Context->LogInfo("LifeCap: disabled by configuration; nothing installed.");
        return true;
    }
    if (!InstallAll()) {
        UninstallCore();
        return false;
    }
    return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
    UninstallCore();
    // The game-image sites and the stub page stay: a thread may be inside a stub
    // right now, and every stub behaves like the game below the cap.
}

}  // namespace CelestialRayOne::MonsterLifeCap
