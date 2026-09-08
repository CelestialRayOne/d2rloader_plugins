// Critical Strike Damage
//
// Adds a configurable itemstatcost stat (default id 434) that increases the
// damage multiplier applied when an attack rolls a critical / deadly strike.
//
// Native background (all of it read out of the live D2R image, build 92777,
// and disassembled before being relied on here):
//
//   SUNITDMG_FillDamageValues @ 0x44C030
//     void __fastcall(game, attacker, defender, D2Damage*, mode, srcDam)
//     prologue: rdi = D2Damage (a4), rsi = attacker (a2), r12 = defender (a3)
//
//   The critical sequence inside it is a short-circuited chain:
//     0x44C2D3  call GetWeaponMasteryChance(attacker, weapon, 0, 2)
//     0x44C31A  jl   0x44C3C8            <- weapon-mastery success
//     0x44C32B  call GetUnitStat(attacker, 337, 0)      passive_critical_strike
//     0x44C372  jl   0x44C3C8            <- passive-crit success
//     0x44C37F  call UnitGetStatValue(attacker, 141, 0) item_deadlystrike
//     0x44C3C6  jge  0x44C3D9            <- deadly failure skips the block
//     0x44C3C8  mov eax,[rdi+0x18] / add eax,eax / mov [rdi+0x18],eax
//               mov eax,0x2000     / or  word [rdi+4],ax
//     0x44C3D9  continuation
//
//   MSVC tail-merged all three success paths, so 0x44C3C8 is the ONE doubling
//   block shared by weapon mastery, passive critical strike and deadly strike.
//   A single hook there covers every source.
//
//   Missiles do NOT go through that function. The ranged crit is rolled once
//   at missile creation by MISSILE_HasBonusStats @ 0x3BA820 (same three
//   sources, same order), which sets flag bit 2 on the missile damage data;
//   that becomes item_deadlystrike = 1 on the missile unit. At impact,
//   MISSMODE_FillDamageParams @ 0x465420 reads that boolean off the missile
//   and doubles:
//
//     0x465A4A  call GetUnitStat(missile, 141, 0)
//     0x465A69  je   0x465A7C
//     0x465A6B  mov eax,[rdi+0x18] / add eax,eax / mov [rdi+0x18],eax
//               mov eax,0x2000     / or  word [rdi+4],ax
//     0x465A7C  continuation
//
//   That block is byte-identical to the melee one and also carries the
//   D2Damage in rdi, so the same relay shape serves both. The missile has no
//   crit-damage stat of its own, so the owner is resolved with the engine's
//   own resolver, sub_140490300(game, missile), which is null-safe.
//
// The 17-byte block is replaced with a jmp to a relay that preserves every
// volatile register, the flags and xmm0-5, scales [rdi+0x18], then reproduces
// the original "mov eax,0x2000 / or word [rdi+4],ax" pair verbatim so RAX and
// EFLAGS on exit are bit-identical to vanilla. With the stat at 0 the result
// is exactly the vanilla x2.

#include <D2RLPlugin/api.h>

// Windows.h defines min/max as macros unless NOMINMAX is set, which breaks
// any std::numeric_limits<T>::max() / ::min() call that follows.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

namespace CelestialRayOne::CriticalStrikeDamage {
namespace {

// ---------------------------------------------------------------------------
// Native anchors
// ---------------------------------------------------------------------------

constexpr std::uintptr_t FillDamageValuesRva     = 0x44C030;
constexpr std::uintptr_t PassiveCriticalReadRva  = 0x44C32B;
constexpr std::uintptr_t DeadlyStrikeReadRva     = 0x44C37F;
constexpr std::uintptr_t CriticalDoubleRva       = 0x44C3C8;
constexpr std::uintptr_t CriticalDoubleResumeRva = 0x44C3D9;
constexpr std::uintptr_t GetUnitStatRva          = 0x2F5020;
constexpr std::uintptr_t UnitGetStatValueRva     = 0x2F5C60;

// Ranged path.
constexpr std::uintptr_t MissileFillDamageRva     = 0x465420;
constexpr std::uintptr_t MissileDoubleRva         = 0x465A6B;
constexpr std::uintptr_t MissileDoubleResumeRva   = 0x465A7C;
constexpr std::uintptr_t GetUnitOwnerRva          = 0x490300;

constexpr std::size_t   DamageResultFlagsOffset = 0x04;
constexpr std::size_t   DamagePhysicalOffset    = 0x18;
constexpr std::uint32_t CriticalDoubleSize      = 17;

constexpr std::int32_t PassiveCriticalStrikeStatId = 337;
constexpr std::int32_t DeadlyStrikeStatId          = 141;

constexpr std::size_t MaximumConfigBytes = 32'768;
constexpr std::size_t RelayBytes         = 4'096;

// 0x44C030 entry, 30 bytes. Verified byte-for-byte against the live image.
constexpr auto FillDamageValuesExpected = std::to_array<std::uint8_t>({
    0x40, 0x56, 0x57, 0x41, 0x54, 0x48, 0x81, 0xEC,
    0x10, 0x05, 0x00, 0x00, 0x48, 0x8B, 0x05, 0x85,
    0xF2, 0x57, 0x02, 0x48, 0x33, 0xC4, 0x48, 0x89,
    0x84, 0x24, 0xD0, 0x04, 0x00, 0x00,
});

// 0x44C3C8, the shared doubling block.
//   mov eax,[rdi+0x18] / add eax,eax / mov [rdi+0x18],eax
//   mov eax,0x2000     / or  word ptr [rdi+4],ax
constexpr auto CriticalDoubleExpected = std::to_array<std::uint8_t>({
    0x8B, 0x47, 0x18, 0x03, 0xC0, 0x89, 0x47, 0x18,
    0xB8, 0x00, 0x20, 0x00, 0x00, 0x66, 0x09, 0x47,
    0x04,
});

// 0x44C32B: call 0x2F5020   (passive_critical_strike read)
constexpr auto PassiveCriticalReadExpected = std::to_array<std::uint8_t>({
    0xE8, 0xF0, 0x8C, 0xEA, 0xFF,
});

// 0x44C37F: call 0x2F5C60   (item_deadlystrike read)
constexpr auto DeadlyStrikeReadExpected = std::to_array<std::uint8_t>({
    0xE8, 0xDC, 0x98, 0xEA, 0xFF,
});

// 0x2F5020 entry, 32 bytes. (unit in rcx, statId in edx, layer in r8w)
constexpr auto GetUnitStatExpected = std::to_array<std::uint8_t>({
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C,
    0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57,
    0x48, 0x83, 0xEC, 0x20, 0x41, 0x0F, 0xB7, 0xE8,
    0x8B, 0xFA, 0x48, 0x8B, 0xD9, 0x48, 0x85, 0xC9,
});

// 0x465420 entry, 32 bytes. MISSMODE_FillDamageParams.
//   mov r11,rsp / push rbp / push rbx / push rsi / push rdi
//   lea rbp,[r11-0x398] / sub rsp,0x478 / stack cookie
constexpr auto MissileFillDamageExpected = std::to_array<std::uint8_t>({
    0x4C, 0x8B, 0xDC, 0x55, 0x53, 0x56, 0x57, 0x49,
    0x8D, 0xAB, 0x68, 0xFC, 0xFF, 0xFF, 0x48, 0x81,
    0xEC, 0x78, 0x04, 0x00, 0x00, 0x48, 0x8B, 0x05,
    0x8C, 0x5E, 0x56, 0x02, 0x48, 0x33, 0xC4, 0x48,
});

// 0x490300 entry, 24 bytes. Owner resolver, ABI (game, unit) -> owner|null.
constexpr auto GetUnitOwnerExpected = std::to_array<std::uint8_t>({
    0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48, 0x83,
    0xEC, 0x20, 0x48, 0x8B, 0xFA, 0x48, 0x8B, 0xF1,
    0x48, 0x85, 0xD2, 0x75, 0x20, 0x48, 0x8D, 0x4C,
});

using FillDamageValuesFn = void(__fastcall*)(
    void*, void*, void*, void*, std::int32_t, std::uint8_t) noexcept;
using GetUnitStatFn = std::int32_t(__fastcall*)(
    void*, std::int32_t, std::uint16_t) noexcept;
using MissileFillDamageFn = void(__fastcall*)(
    void*, void*, void*, void*, void*) noexcept;
using GetUnitOwnerFn = void*(__fastcall*)(void*, void*) noexcept;

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

enum class CriticalSource : std::uint32_t {
    WeaponMastery = 0,
    PassiveCritical,
    DeadlyStrike,
};

struct Config {
    bool         enabled              = true;
    std::int32_t statId               = 434;
    std::int64_t basePercent          = 200;
    std::int64_t maximumPercent       = 100'000;
    bool         applyToDeadlyStrike  = true;
    bool         applyToCriticalStrike = true;
    bool         applyToWeaponMastery = true;
    bool         applyToMissiles      = true;
};

constexpr char DefaultConfigToml[] =
    "# Critical Strike Damage\n"
    "#\n"
    "# Scales the damage multiplier used when an attack rolls a critical or\n"
    "# deadly strike. The engine resolves weapon-mastery Critical,\n"
    "# passive_critical_strike (stat 337) and item_deadlystrike (stat 141)\n"
    "# through one shared doubling block, so all three are covered here.\n"
    "\n"
    "[critical_strike_damage]\n"
    "\n"
    "# Master switch.\n"
    "enabled = true\n"
    "\n"
    "# itemstatcost.txt id carrying the bonus. The stat is a percentage that\n"
    "# is ADDED to base_percent, so 50 turns a x2 critical into a x2.5.\n"
    "stat_id = 434\n"
    "\n"
    "# The vanilla critical multiplier, in percent. 200 = x2. Leave at 200\n"
    "# unless you want to retune criticals for everyone, stat or no stat.\n"
    "base_percent = 200\n"
    "\n"
    "# Safety ceiling on (base_percent + stat), in percent.\n"
    "max_percent = 100000\n"
    "\n"
    "# Which critical sources the stat applies to. A disabled source keeps\n"
    "# the vanilla x2 exactly. Leaving these mixed is usually a trap: the\n"
    "# three sources are mutually exclusive and resolved in the order\n"
    "# weapon mastery -> critical strike -> deadly strike, so disabling one\n"
    "# means a character who has it loses the bonus the others would give.\n"
    "apply_to_deadly_strike = true\n"
    "apply_to_critical_strike = true\n"
    "apply_to_weapon_mastery = true\n"
    "\n"
    "# Ranged attacks. Missiles roll their critical once at creation and only\n"
    "# carry a yes/no flag to impact, so the engine itself discards which of\n"
    "# the three sources won. The three toggles above therefore cannot apply\n"
    "# per-source here: the bonus is used when any of them is enabled.\n"
    "apply_to_missiles = true\n";

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

struct PendingCritical {
    void*          attacker;
    void*          damage;
    CriticalSource source;
};

// Ranged: the owner is resolved lazily, only when a missile actually crits.
struct PendingMissile {
    void* game;
    void* missile;
    void* damage;
};

const D2RL::PluginContext* Context{};
std::uint8_t*              Base{};
Config                     Settings{};
FillDamageValuesFn         OriginalFillDamageValues{};
MissileFillDamageFn        OriginalMissileFillDamage{};
GetUnitStatFn              GetUnitStat{};
GetUnitOwnerFn             GetUnitOwner{};
void*                      RelayPage{};
bool                       ProvenanceInstalled{};
bool                       MissilesInstalled{};

// Damage resolution is single-threaded per game, but this costs nothing and
// removes the question entirely. POD with constant initialisers, so it is a
// plain TLS slot with no lazy-init guard on the hot path.
thread_local PendingCritical Pending{nullptr, nullptr, CriticalSource::WeaponMastery};
thread_local PendingMissile  MissilePending{nullptr, nullptr, nullptr};

std::atomic<std::uint64_t> ScaledCriticals{};
std::atomic<std::uint64_t> ScaledMissileCriticals{};
std::atomic<std::uint64_t> UnattributedCriticals{};

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
    for (; index < value.size(); ++index) {
        const char character = value[index];
        if (character == '_') continue;
        if (character < '0' || character > '9') return false;
        accumulator = accumulator * 10 + (character - '0');
        if (accumulator > 0x7FFF'FFFF'FLL) return false;
    }
    out = negative ? -accumulator : accumulator;
    return true;
}

void ApplyConfigLine(std::string_view key, std::string_view value) noexcept {
    std::int64_t number = 0;
    if (key == "enabled") {
        ParseBool(value, Settings.enabled);
    } else if (key == "stat_id") {
        if (ParseInteger(value, number) && number >= 0 && number <= 510) {
            Settings.statId = static_cast<std::int32_t>(number);
        }
    } else if (key == "base_percent") {
        if (ParseInteger(value, number)) Settings.basePercent = number;
    } else if (key == "max_percent") {
        if (ParseInteger(value, number)) Settings.maximumPercent = number;
    } else if (key == "apply_to_deadly_strike") {
        ParseBool(value, Settings.applyToDeadlyStrike);
    } else if (key == "apply_to_critical_strike") {
        ParseBool(value, Settings.applyToCriticalStrike);
    } else if (key == "apply_to_weapon_mastery") {
        ParseBool(value, Settings.applyToWeaponMastery);
    } else if (key == "apply_to_missiles") {
        ParseBool(value, Settings.applyToMissiles);
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
        if (line.empty() || line.front() == '[') continue;

        const std::size_t equals = line.find('=');
        if (equals == std::string_view::npos) continue;
        ApplyConfigLine(Trim(line.substr(0, equals)), Trim(line.substr(equals + 1)));

        if (breakAt == std::string_view::npos) break;
    }
}

auto ReadConfiguration() noexcept -> bool {
    if (!Context->EnsureConfig(DefaultConfigToml)) {
        Context->LogWarn(
            "CriticalStrikeDamage: config file could not be created; using defaults.");
        return true;
    }
    std::string buffer(MaximumConfigBytes, '\0');
    std::uint32_t required = 0;
    if (!Context->ReadConfig(buffer.data(),
            static_cast<std::uint32_t>(buffer.size()), &required)) {
        Context->LogWarn(
            "CriticalStrikeDamage: config file could not be read; using defaults.");
        return true;
    }
    buffer.resize(std::strlen(buffer.c_str()));
    ParseConfig(buffer);

    if (Settings.basePercent < 0) Settings.basePercent = 0;
    if (Settings.maximumPercent < Settings.basePercent) {
        Settings.maximumPercent = Settings.basePercent;
    }
    return true;
}

auto SourceEnabled(CriticalSource source) noexcept -> bool {
    switch (source) {
    case CriticalSource::DeadlyStrike:    return Settings.applyToDeadlyStrike;
    case CriticalSource::PassiveCritical: return Settings.applyToCriticalStrike;
    case CriticalSource::WeaponMastery:   return Settings.applyToWeaponMastery;
    }
    return false;
}

auto AnySourceEnabled() noexcept -> bool {
    return Settings.applyToDeadlyStrike
        || Settings.applyToCriticalStrike
        || Settings.applyToWeaponMastery;
}

auto AllSourcesMatch() noexcept -> bool {
    return Settings.applyToDeadlyStrike == Settings.applyToCriticalStrike
        && Settings.applyToDeadlyStrike == Settings.applyToWeaponMastery;
}

// ---------------------------------------------------------------------------
// The scaling callback, reached from the relay at 0x44C3C8
// ---------------------------------------------------------------------------

auto ResolvePercent(void* attacker, bool enabled) noexcept -> std::int64_t {
    std::int64_t percent = Settings.basePercent;
    if (enabled && attacker && GetUnitStat) {
        percent += GetUnitStat(attacker, Settings.statId, 0);
    }
    if (percent < 0) percent = 0;
    if (percent > Settings.maximumPercent) percent = Settings.maximumPercent;
    return percent;
}

void ApplyPercent(void* damage, std::int64_t percent) noexcept {
    auto* physical = reinterpret_cast<std::int32_t*>(
        static_cast<std::uint8_t*>(damage) + DamagePhysicalOffset);
    std::int64_t scaled =
        (static_cast<std::int64_t>(*physical) * percent) / 100;
    constexpr std::int64_t Ceiling = INT32_MAX;
    constexpr std::int64_t Floor   = INT32_MIN;
    if (scaled > Ceiling) scaled = Ceiling;
    if (scaled < Floor)   scaled = Floor;
    *physical = static_cast<std::int32_t>(scaled);
}

// Melee / direct path, reached from the relay at 0x44C3C8.
void __fastcall CriticalStrikeDamageScale(void* damage) noexcept {
    if (!damage) return;

    // Pending is written by the FillDamageValues detour at the top of the same
    // invocation. If the damage pointer does not match, something reached this
    // block by a path we do not own: fall back to plain vanilla behaviour.
    if (Pending.damage != damage) {
        UnattributedCriticals.fetch_add(1, std::memory_order_relaxed);
        ApplyPercent(damage, Settings.basePercent);
        return;
    }
    ScaledCriticals.fetch_add(1, std::memory_order_relaxed);
    ApplyPercent(damage,
        ResolvePercent(Pending.attacker, SourceEnabled(Pending.source)));
}

// Ranged path, reached from the relay at 0x465A6B. The missile itself carries
// no crit-damage stat, and the engine has already discarded which of the three
// sources rolled it at creation time, so the owner is resolved here and the
// bonus applies when any source is enabled.
void __fastcall MissileCriticalDamageScale(void* damage) noexcept {
    if (!damage) return;

    if (MissilePending.damage != damage || !Settings.applyToMissiles) {
        UnattributedCriticals.fetch_add(1, std::memory_order_relaxed);
        ApplyPercent(damage, Settings.basePercent);
        return;
    }

    void* owner = nullptr;
    if (GetUnitOwner && MissilePending.game && MissilePending.missile) {
        owner = GetUnitOwner(MissilePending.game, MissilePending.missile);
    }
    ScaledMissileCriticals.fetch_add(1, std::memory_order_relaxed);
    ApplyPercent(damage, ResolvePercent(owner, AnySourceEnabled()));
}

// ---------------------------------------------------------------------------
// Detour and provenance probes
// ---------------------------------------------------------------------------

void __fastcall FillDamageValuesDetour(
        void* game, void* attacker, void* defender, void* damage,
        std::int32_t mode, std::uint8_t srcDam) noexcept {
    // Default provenance is weapon mastery: it is the only source that reaches
    // the doubling block without first calling one of the two stat getters.
    Pending.attacker = attacker;
    Pending.damage   = damage;
    Pending.source   = CriticalSource::WeaponMastery;

    OriginalFillDamageValues(game, attacker, defender, damage, mode, srcDam);

    Pending.damage = nullptr;
}

// a2 is the owner the caller already resolved, but it is unused inside the
// native function and 12 call sites feed it, so it is not trusted here. The
// game and the missile are both load-bearing inside the function, and the
// owner is resolved from them on demand instead.
void __fastcall MissileFillDamageDetour(
        void* game, void* owner, void* missile, void* target,
        void* damage) noexcept {
    MissilePending.game    = game;
    MissilePending.missile = missile;
    MissilePending.damage  = damage;

    OriginalMissileFillDamage(game, owner, missile, target, damage);

    MissilePending.damage = nullptr;
}

std::int32_t __fastcall PassiveCriticalReadProbe(
        void* unit, std::int32_t statId, std::uint16_t layer) noexcept {
    Pending.source = CriticalSource::PassiveCritical;
    return GetUnitStat(unit, statId, layer);
}

GetUnitStatFn OriginalUnitGetStatValue{};

std::int32_t __fastcall DeadlyStrikeReadProbe(
        void* unit, std::int32_t statId, std::uint16_t layer) noexcept {
    Pending.source = CriticalSource::DeadlyStrike;
    return OriginalUnitGetStatValue(unit, statId, layer);
}

// ---------------------------------------------------------------------------
// Relay page
// ---------------------------------------------------------------------------

auto CanEncodeRel32(std::uintptr_t from, std::uintptr_t to) noexcept -> bool {
    const std::int64_t delta =
        static_cast<std::int64_t>(to) - static_cast<std::int64_t>(from) - 5;
    return delta >= INT32_MIN && delta <= INT32_MAX;
}

auto AllocateNear(void* hint, std::size_t size) noexcept -> void* {
    SYSTEM_INFO systemInfo{};
    GetSystemInfo(&systemInfo);
    const auto granularity =
        static_cast<std::uintptr_t>(systemInfo.dwAllocationGranularity);
    const auto aligned =
        reinterpret_cast<std::uintptr_t>(hint) & ~(granularity - 1U);
    for (std::uintptr_t delta = granularity;
            delta < 0x7000'0000ULL; delta += granularity) {
        const auto candidate = aligned + delta;
        if (!CanEncodeRel32(reinterpret_cast<std::uintptr_t>(hint), candidate)) break;
        if (auto* memory = VirtualAlloc(reinterpret_cast<void*>(candidate), size,
                MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)) {
            return memory;
        }
    }
    return nullptr;
}

// jmp qword ptr [rip+0] ; dq target
auto WriteAbsoluteJump(std::uint8_t* destination, const void* target) noexcept
        -> std::size_t {
    destination[0] = 0xFF;
    destination[1] = 0x25;
    destination[2] = destination[3] = destination[4] = destination[5] = 0x00;
    const auto address = reinterpret_cast<std::uint64_t>(target);
    std::memcpy(destination + 6, &address, sizeof(address));
    return 14;
}

// Builds the stub that replaces the 17-byte doubling block. Entry contract is
// rdi = D2Damage*, which is proven by the very bytes being replaced
// (mov eax,[rdi+0x18] ... or word ptr [rdi+4],ax).
//
// Every volatile register, the flags and xmm0-5 are saved, because the block
// contained no call and the surrounding code therefore assumes they survive.
// The stub ends with the original "mov eax,0x2000 / or word [rdi+4],ax" pair,
// so RAX and EFLAGS on exit match vanilla exactly.
auto BuildCriticalRelay(std::uint8_t* out, const void* callback,
        std::uintptr_t continuation) noexcept -> std::size_t {
    std::size_t index = 0;
    const auto emit = [&](std::initializer_list<std::uint8_t> bytes) {
        for (const auto byte : bytes) out[index++] = byte;
    };

    emit({0x50});                                   // push rax
    emit({0x51});                                   // push rcx
    emit({0x52});                                   // push rdx
    emit({0x41, 0x50});                             // push r8
    emit({0x41, 0x51});                             // push r9
    emit({0x41, 0x52});                             // push r10
    emit({0x41, 0x53});                             // push r11
    emit({0x9C});                                   // pushfq
    emit({0x55});                                   // push rbp
    emit({0x48, 0x89, 0xE5});                       // mov rbp, rsp
    emit({0x48, 0x83, 0xE4, 0xF0});                 // and rsp, -16
    emit({0x48, 0x81, 0xEC, 0x80, 0x00, 0x00, 0x00}); // sub rsp, 0x80

    // movaps [rsp+0x20 + 0x10*i], xmmI
    for (std::uint8_t slot = 0; slot < 6; ++slot) {
        emit({0x0F, 0x29,
              static_cast<std::uint8_t>(0x44 + (slot << 3)),
              0x24,
              static_cast<std::uint8_t>(0x20 + (slot * 0x10))});
    }

    emit({0x48, 0x89, 0xF9});                       // mov rcx, rdi
    emit({0x48, 0xB8});                             // mov rax, imm64
    const auto target = reinterpret_cast<std::uint64_t>(callback);
    std::memcpy(out + index, &target, sizeof(target));
    index += sizeof(target);
    emit({0xFF, 0xD0});                             // call rax

    // movaps xmmI, [rsp+0x20 + 0x10*i]
    for (std::uint8_t slot = 0; slot < 6; ++slot) {
        emit({0x0F, 0x28,
              static_cast<std::uint8_t>(0x44 + (slot << 3)),
              0x24,
              static_cast<std::uint8_t>(0x20 + (slot * 0x10))});
    }

    emit({0x48, 0x89, 0xEC});                       // mov rsp, rbp
    emit({0x5D});                                   // pop rbp
    emit({0x9D});                                   // popfq
    emit({0x41, 0x5B});                             // pop r11
    emit({0x41, 0x5A});                             // pop r10
    emit({0x41, 0x59});                             // pop r9
    emit({0x41, 0x58});                             // pop r8
    emit({0x5A});                                   // pop rdx
    emit({0x59});                                   // pop rcx
    emit({0x58});                                   // pop rax
    emit({0xB8, 0x00, 0x20, 0x00, 0x00});           // mov eax, 0x2000
    emit({0x66, 0x09, 0x47, 0x04});                 // or word ptr [rdi+4], ax
    emit({0xFF, 0x25, 0x00, 0x00, 0x00, 0x00});     // jmp qword ptr [rip+0]

    const auto resume = static_cast<std::uint64_t>(continuation);
    std::memcpy(out + index, &resume, sizeof(resume));
    index += sizeof(resume);
    return index;
}

// ---------------------------------------------------------------------------
// Verification and installation
// ---------------------------------------------------------------------------

template <std::size_t Size>
auto Verify(std::uintptr_t rva, const std::array<std::uint8_t, Size>& expected,
        const char* label) noexcept -> bool {
    if (Context->CheckExpectedBytes(rva, expected.data(),
            static_cast<std::uint32_t>(expected.size()))) {
        return true;
    }
    char message[192];
    std::snprintf(message, sizeof(message),
        "CriticalStrikeDamage: %s at 0x%llX does not match build 92777, or is "
        "already owned by another plugin. Refusing to load.",
        label, static_cast<unsigned long long>(rva));
    Context->LogError(message);
    return false;
}

auto VerifyNativeContract() noexcept -> bool {
    if (!Verify(FillDamageValuesRva, FillDamageValuesExpected, "damage builder")
        || !Verify(CriticalDoubleRva, CriticalDoubleExpected,
                   "critical doubling block")
        || !Verify(PassiveCriticalReadRva, PassiveCriticalReadExpected,
                   "passive critical strike read")
        || !Verify(DeadlyStrikeReadRva, DeadlyStrikeReadExpected,
                   "deadly strike read")
        || !Verify(GetUnitStatRva, GetUnitStatExpected, "unit stat getter")) {
        return false;
    }
    if (Settings.applyToMissiles
        && (!Verify(MissileFillDamageRva, MissileFillDamageExpected,
                    "missile damage builder")
            || !Verify(MissileDoubleRva, CriticalDoubleExpected,
                       "missile critical doubling block")
            || !Verify(GetUnitOwnerRva, GetUnitOwnerExpected,
                       "unit owner resolver"))) {
        return false;
    }
    return true;
}

auto InstallHooks() noexcept -> bool {
    GetUnitStat = reinterpret_cast<GetUnitStatFn>(Base + GetUnitStatRva);
    OriginalUnitGetStatValue =
        reinterpret_cast<GetUnitStatFn>(Base + UnitGetStatValueRva);
    GetUnitOwner = reinterpret_cast<GetUnitOwnerFn>(Base + GetUnitOwnerRva);

    RelayPage = AllocateNear(Base + CriticalDoubleRva, RelayBytes);
    if (!RelayPage) {
        Context->LogError(
            "CriticalStrikeDamage: no relay page was available within rel32 reach.");
        return false;
    }
    auto* relay = static_cast<std::uint8_t*>(RelayPage);
    const auto relayBase = reinterpret_cast<std::uintptr_t>(relay);
    const auto imageBase = reinterpret_cast<std::uintptr_t>(Base);

    std::size_t cursor = 0;

    // The damage-scaling stub.
    const std::size_t scaleStub = cursor;
    cursor += BuildCriticalRelay(relay + cursor,
        reinterpret_cast<const void*>(&CriticalStrikeDamageScale),
        imageBase + CriticalDoubleResumeRva);
    cursor = (cursor + 15) & ~static_cast<std::size_t>(15);

    // The ranged stub. Same shape, different continuation and callback.
    std::size_t missileStub = 0;
    if (Settings.applyToMissiles) {
        missileStub = cursor;
        cursor += BuildCriticalRelay(relay + cursor,
            reinterpret_cast<const void*>(&MissileCriticalDamageScale),
            imageBase + MissileDoubleResumeRva);
        cursor = (cursor + 15) & ~static_cast<std::size_t>(15);
    }

    // Far-jump thunks for the two provenance probes. A rel32 call cannot reach
    // the DLL directly, so the call sites are pointed at these instead.
    const bool needProvenance = !AllSourcesMatch();
    std::size_t passiveThunk = 0;
    std::size_t deadlyThunk  = 0;
    if (needProvenance) {
        passiveThunk = cursor;
        cursor += WriteAbsoluteJump(relay + cursor,
            reinterpret_cast<const void*>(&PassiveCriticalReadProbe));
        cursor = (cursor + 15) & ~static_cast<std::size_t>(15);
        deadlyThunk = cursor;
        cursor += WriteAbsoluteJump(relay + cursor,
            reinterpret_cast<const void*>(&DeadlyStrikeReadProbe));
        cursor = (cursor + 15) & ~static_cast<std::size_t>(15);
    }

    DWORD previousProtection = 0;
    if (!VirtualProtect(relay, RelayBytes, PAGE_EXECUTE_READ, &previousProtection)) {
        Context->LogError(
            "CriticalStrikeDamage: relay page protection could not be finalized.");
        return false;
    }
    FlushInstructionCache(GetCurrentProcess(), relay, RelayBytes);

    if (!CanEncodeRel32(imageBase + CriticalDoubleRva, relayBase + scaleStub)
        || (Settings.applyToMissiles
            && !CanEncodeRel32(imageBase + MissileDoubleRva,
                               relayBase + missileStub))) {
        Context->LogError(
            "CriticalStrikeDamage: relay displacement validation failed.");
        return false;
    }

    if (!Context->InstallInlineHook(
            FillDamageValuesRva,
            FillDamageValuesExpected.data(),
            static_cast<std::uint32_t>(FillDamageValuesExpected.size()),
            &FillDamageValuesDetour,
            &OriginalFillDamageValues)) {
        Context->LogError(
            "CriticalStrikeDamage: the damage builder could not be hooked.");
        return false;
    }

    if (needProvenance) {
        if (!Context->PatchCallRel32(
                PassiveCriticalReadRva,
                PassiveCriticalReadExpected.data(),
                static_cast<std::uint32_t>(PassiveCriticalReadExpected.size()),
                relayBase + passiveThunk - imageBase)
            || !Context->PatchCallRel32(
                DeadlyStrikeReadRva,
                DeadlyStrikeReadExpected.data(),
                static_cast<std::uint32_t>(DeadlyStrikeReadExpected.size()),
                relayBase + deadlyThunk - imageBase)) {
            Context->LogError(
                "CriticalStrikeDamage: a critical-source probe could not be installed.");
            return false;
        }
        ProvenanceInstalled = true;
    }

    if (!Context->PatchJmpRel32(
            CriticalDoubleRva,
            CriticalDoubleExpected.data(),
            static_cast<std::uint32_t>(CriticalDoubleExpected.size()),
            relayBase + scaleStub - imageBase,
            CriticalDoubleSize)) {
        Context->LogError(
            "CriticalStrikeDamage: the critical doubling block could not be redirected.");
        return false;
    }

    if (Settings.applyToMissiles) {
        if (!Context->InstallInlineHook(
                MissileFillDamageRva,
                MissileFillDamageExpected.data(),
                static_cast<std::uint32_t>(MissileFillDamageExpected.size()),
                &MissileFillDamageDetour,
                &OriginalMissileFillDamage)) {
            Context->LogError(
                "CriticalStrikeDamage: the missile damage builder could not be hooked.");
            return false;
        }
        if (!Context->PatchJmpRel32(
                MissileDoubleRva,
                CriticalDoubleExpected.data(),
                static_cast<std::uint32_t>(CriticalDoubleExpected.size()),
                relayBase + missileStub - imageBase,
                CriticalDoubleSize)) {
            Context->LogError(
                "CriticalStrikeDamage: the missile critical doubling block could "
                "not be redirected.");
            return false;
        }
        MissilesInstalled = true;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Console command
// ---------------------------------------------------------------------------

auto __cdecl StatusCommand(D2R::Game::Client*,
        const D2RL::ConsoleCommandContext* command, void*) noexcept
        -> D2RL::ConsoleCommandResult {
    if (!command || !command->plugin) return D2RL::ConsoleCommandResult::Failed;
    char message[448];
    std::snprintf(message, sizeof(message),
        "Critical Strike Damage: %s | stat %d | base %lld%% | cap %lld%% | "
        "deadly %s critical %s mastery %s missiles %s | provenance %s | "
        "melee crits %llu | missile crits %llu | unattributed %llu",
        Settings.enabled ? "on" : "off",
        Settings.statId,
        static_cast<long long>(Settings.basePercent),
        static_cast<long long>(Settings.maximumPercent),
        Settings.applyToDeadlyStrike ? "on" : "off",
        Settings.applyToCriticalStrike ? "on" : "off",
        Settings.applyToWeaponMastery ? "on" : "off",
        MissilesInstalled ? "on" : "off",
        ProvenanceInstalled ? "tracked" : "uniform",
        static_cast<unsigned long long>(ScaledCriticals.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(ScaledMissileCriticals.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(UnattributedCriticals.load(std::memory_order_relaxed)));
    command->plugin->WriteConsoleMessage(message);
    return D2RL::ConsoleCommandResult::Handled;
}

constexpr D2RL::PluginInfo Info{
    .infoSize = D2RL::PluginInfoSize,
    .apiVersion = D2RL_PLUGIN_API_VERSION,
    .id = "celestialrayone.critical-strike-damage",
    .name = "Critical Strike Damage",
    .version = "1.0.0",
    .author = "CelestialRayOne",
    .description =
        "Adds a configurable stat that increases the damage multiplier of "
        "critical and deadly strikes.",
    .flags = D2RL::PluginFlags::Server | D2RL::PluginFlags::NativeHooks,
};

}  // namespace

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept
        -> const D2RL::PluginInfo* {
    return &Info;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(
        const D2RL::PluginContext* context) noexcept -> bool {
    if (!D2RL::HasContext(context)) return false;
    Context = context;
    Base = reinterpret_cast<std::uint8_t*>(context->exeBase);

    if (!ReadConfiguration()) return false;

    if (!Settings.enabled) {
        Context->LogInfo("CriticalStrikeDamage: disabled by configuration.");
        return true;
    }
    if (!AnySourceEnabled()) {
        Context->LogInfo(
            "CriticalStrikeDamage: every critical source is disabled; "
            "no hooks installed.");
        return true;
    }
    if (!VerifyNativeContract()) return false;
    if (!InstallHooks()) return false;

    char message[352];
    std::snprintf(message, sizeof(message),
        "CriticalStrikeDamage: armed on stat %d, base %lld%%, sources "
        "deadly=%s critical=%s mastery=%s. Melee/direct block 0x%llX covers "
        "all three sources; missiles %s (block 0x%llX).",
        Settings.statId,
        static_cast<long long>(Settings.basePercent),
        Settings.applyToDeadlyStrike ? "on" : "off",
        Settings.applyToCriticalStrike ? "on" : "off",
        Settings.applyToWeaponMastery ? "on" : "off",
        static_cast<unsigned long long>(CriticalDoubleRva),
        MissilesInstalled ? "hooked" : "off",
        static_cast<unsigned long long>(MissileDoubleRva));
    Context->LogInfo(message);

    if (!Context->RegisterConsoleCommand("critdmg", &StatusCommand,
            "Reports Critical Strike Damage status and counters.")) {
        Context->LogWarn(
            "CriticalStrikeDamage: the status console command was refused.");
    }
    return true;
}

}  // namespace CelestialRayOne::CriticalStrikeDamage
