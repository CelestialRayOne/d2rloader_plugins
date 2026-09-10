// Dodge Avoid Evade Cap
//
// Caps the chance to dodge (passive_dodge, stat 338), avoid (passive_avoid,
// stat 339) and evade (passive_evade, stat 340) at a configurable percent,
// 75 by default. The server-side roll and the Advanced Stats panel are both
// capped, so the panel never shows a chance the roll will not honour.
//
// Port of the ESR D2R 2.4 memory patches (six roll hooks into a clamp cave
// at 3870F5, four display hooks into a stat-scoped clamp cave at 35C2F0) to
// D2R 3.3. Everything below was read out of the live D2RLoader.exe image and
// disassembled before being relied on.
//
// ---------------------------------------------------------------------------
// The getter
// ---------------------------------------------------------------------------
//   STATLIST_GetUnitStat @ 0x2F5020, int32 (unit, statId, layer).
//
//     2F5020  48 89 5C 24 10      mov   [rsp+10h], rbx
//     ...
//     2F5034  41 0F B7 E8         movzx ebp, r8w       ; layer, 16 bits
//     2F5038  8B FA               mov   edi, edx       ; stat id, raw
//     2F503A  48 8B D9            mov   rbx, rcx       ; unit
//
//   2.4 passed a packed key (statId << 16) in edx. In 3.3 edx carries the raw
//   stat id, so the stat check below compares it directly.
//
// ---------------------------------------------------------------------------
// Server: the avoidance roll, sub_14044EF70 (attacker, defender, bMissile)
// ---------------------------------------------------------------------------
//   Reached from the block roll sub_14044B930, which the melee resolver
//   sub_14044B720 runs after the to-hit roll sub_14044BA60 succeeds.
//   A defender that is not walking or running (player modes 2/3, monster
//   modes 2/15) gets a block check, then avoid for missiles or dodge for
//   melee. A moving defender rolls evade. Every roll is seed % 100 < chance
//   and returns 2 (avoid), 4 (dodge) or 8 (evade).
//
//   The 3.3 compiler merged the player and monster branches, so each stat is
//   read by exactly one call. 2.4 still had two calls per stat.
//
//     44F072  BA 53 01 00 00      mov   edx, 153h      ; passive_avoid
//     44F077  E8 A4 5F EA FF      call  2F5020         ; <- hook
//     44F07C  8B E8               mov   ebp, eax
//
//     44F1CE  BA 52 01 00 00      mov   edx, 152h      ; passive_dodge
//     44F1D3  E8 48 5E EA FF      call  2F5020         ; <- hook
//     44F1D8  8B E8               mov   ebp, eax
//
//     44F251  BA 54 01 00 00      mov   edx, 154h      ; passive_evade
//     44F256  E8 C5 5D EA FF      call  2F5020         ; <- hook
//     44F25B  44 8B F0            mov   r14d, eax
//
//   Branch targets around them are 44F072 (from 44F1C8), 44F1CE (from
//   44F06C), 44F24B (from 44F0FB and 44F104) and 44F24E (from 44EFAE). None
//   lands inside a replaced call.
//
// ---------------------------------------------------------------------------
// Client: the Advanced Stats panel (D2Client/src/UI/Widgets/AdvancedStatsPanel.cpp)
// ---------------------------------------------------------------------------
//   Every panel row reads its stat through one of two helpers. The stat id
//   comes from the row definition, so these calls read EVERY stat on the
//   panel and the clamp has to stay gated on 338..340. The first 2.4 display
//   cave was not gated and capped IAS, FRW, FHR, FCR and the rest at 75.
//
//     sub_1414EB1B0  row value, returned as value >> ItemStatCost ValShift
//       14EB1D6  0F BF 16               movsx edx, word ptr [rsi]   ; row stat id
//       14EB1D9  45 33 C0               xor   r8d, r8d
//       14EB1DC  48 8B 8F 68 01 00 00   mov   rcx, [rdi+168h]       ; panel unit
//       14EB1E3  48 8B D8               mov   rbx, rax
//       14EB1E6  E8 35 9E E0 FE         call  2F5020                ; <- hook
//
//     sub_1414EB250  row text, value handed to the formatter 2D97E0
//       14EB276  48 8B 89 68 01 00 00   mov   rcx, [rcx+168h]       ; panel unit
//       14EB28B  45 33 C0               xor   r8d, r8d
//       14EB28E  41 0F BF 16            movsx edx, word ptr [r14]   ; row stat id
//       14EB292  E8 89 9D E0 FE         call  2F5020                ; <- hook
//       14EB297  8B D0                  mov   edx, eax
//
//   Their callers are 14EAD50, 14EAF40, 14EC800 and 14ED3A0. There is no
//   other call to STATLIST_GetUnitStat (2F5020) or STATLIST_UnitGetStatValue
//   (2F5C60) anywhere in 14EA000..14EE000.
//
// ---------------------------------------------------------------------------
// Hook shape
// ---------------------------------------------------------------------------
//   Each site keeps its five-byte call and only the rel32 changes, to a near
//   relay (jmp qword ptr [rip+0]) that lands in a C++ function with the
//   getter's own ABI. That function calls the real getter and clamps the
//   result. To the call site it is an ordinary callee: same arguments, same
//   return register, and only volatile registers change, exactly as the
//   original getter is allowed to change them.
//
//   If the plugin unloads, or a later site fails to patch, both relays are
//   first pointed straight at the vanilla getter. A call site that could not
//   be restored then still reaches native code, uncapped, instead of code in
//   an unloaded DLL.

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
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

namespace CelestialRayOne::DodgeAvoidEvadeCap {
namespace {

// ---------------------------------------------------------------------------
// Native anchors
// ---------------------------------------------------------------------------

constexpr std::uint64_t GetUnitStatRva = 0x2F5020;

constexpr std::array<std::uint8_t, 32> GetUnitStatExpected{
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x48, 0x89,
    0x74, 0x24, 0x20, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x41, 0x0F, 0xB7, 0xE8,
    0x8B, 0xFA, 0x48, 0x8B, 0xD9, 0x48, 0x85, 0xC9,
};

constexpr std::int32_t StatDodge = 338;
constexpr std::size_t  StatCount = 3;          // indexed by statId - 338
constexpr std::uint32_t CallSize = 5;

enum class SiteKind : std::uint8_t { Gameplay = 0, Display = 1 };
constexpr std::size_t KindCount = 2;

// Each witness covers the call and the instructions that prove its
// arguments: the stat id immediate at the roll sites, the row id load and
// panel unit load at the panel sites.

// 44F063  xor r8d,r8d / mov rcx,rdi / test r14d,r14d / je 44F1CE /
//         mov edx,153h / call 2F5020 / mov ebp,eax
constexpr std::uint8_t AvoidRollWitness[]{
    0x45, 0x33, 0xC0, 0x48, 0x8B, 0xCF, 0x45, 0x85, 0xF6,
    0x0F, 0x84, 0x5C, 0x01, 0x00, 0x00,
    0xBA, 0x53, 0x01, 0x00, 0x00,
    0xE8, 0xA4, 0x5F, 0xEA, 0xFF,
    0x8B, 0xE8,
};

// 44F1BF  xor r8d,r8d / mov rcx,rdi / test r14d,r14d / jne 44F072 /
//         mov edx,152h / call 2F5020 / mov ebp,eax
constexpr std::uint8_t DodgeRollWitness[]{
    0x45, 0x33, 0xC0, 0x48, 0x8B, 0xCF, 0x45, 0x85, 0xF6,
    0x0F, 0x85, 0xA4, 0xFE, 0xFF, 0xFF,
    0xBA, 0x52, 0x01, 0x00, 0x00,
    0xE8, 0x48, 0x5E, 0xEA, 0xFF,
    0x8B, 0xE8,
};

// 44F24B  mov rcx,rdi / xor r8d,r8d / mov edx,154h / call 2F5020 /
//         mov r14d,eax
constexpr std::uint8_t EvadeRollWitness[]{
    0x48, 0x8B, 0xCF, 0x45, 0x33, 0xC0,
    0xBA, 0x54, 0x01, 0x00, 0x00,
    0xE8, 0xC5, 0x5D, 0xEA, 0xFF,
    0x44, 0x8B, 0xF0,
};

// 14EB1D6  movsx edx,word ptr [rsi] / xor r8d,r8d / mov rcx,[rdi+168h] /
//          mov rbx,rax / call 2F5020 / movsx rcx,word ptr [rsi]
constexpr std::uint8_t PanelValueWitness[]{
    0x0F, 0xBF, 0x16, 0x45, 0x33, 0xC0,
    0x48, 0x8B, 0x8F, 0x68, 0x01, 0x00, 0x00,
    0x48, 0x8B, 0xD8,
    0xE8, 0x35, 0x9E, 0xE0, 0xFE,
    0x48, 0x0F, 0xBF, 0x0E,
};

// 14EB276  mov rcx,[rcx+168h] / mov rdi,rdx / mov [rsp+570h],r14 /
//          mov r14,r8 / xor r8d,r8d / movsx edx,word ptr [r14] /
//          call 2F5020 / mov edx,eax
constexpr std::uint8_t PanelTextWitness[]{
    0x48, 0x8B, 0x89, 0x68, 0x01, 0x00, 0x00,
    0x48, 0x8B, 0xFA,
    0x4C, 0x89, 0xB4, 0x24, 0x70, 0x05, 0x00, 0x00,
    0x4D, 0x8B, 0xF0, 0x45, 0x33, 0xC0,
    0x41, 0x0F, 0xBF, 0x16,
    0xE8, 0x89, 0x9D, 0xE0, 0xFE,
    0x8B, 0xD0,
};

struct Site {
    const char*         name;
    SiteKind            kind;
    std::uint64_t       witnessRva;
    const std::uint8_t* witness;
    std::uint32_t       witnessSize;
    std::uint64_t       callRva;
};

constexpr std::size_t SiteCount = 5;

constexpr std::array<Site, SiteCount> Sites{{
    { "avoid roll",  SiteKind::Gameplay, 0x44F063,  AvoidRollWitness,
      static_cast<std::uint32_t>(sizeof(AvoidRollWitness)),  0x44F077  },
    { "dodge roll",  SiteKind::Gameplay, 0x44F1BF,  DodgeRollWitness,
      static_cast<std::uint32_t>(sizeof(DodgeRollWitness)),  0x44F1D3  },
    { "evade roll",  SiteKind::Gameplay, 0x44F24B,  EvadeRollWitness,
      static_cast<std::uint32_t>(sizeof(EvadeRollWitness)),  0x44F256  },
    { "panel value", SiteKind::Display,  0x14EB1D6, PanelValueWitness,
      static_cast<std::uint32_t>(sizeof(PanelValueWitness)), 0x14EB1E6 },
    { "panel text",  SiteKind::Display,  0x14EB276, PanelTextWitness,
      static_cast<std::uint32_t>(sizeof(PanelTextWitness)),  0x14EB292 },
}};

// Relay page: one 14-byte absolute jump per site kind.
constexpr std::size_t RelayPageBytes   = 4'096;
constexpr std::size_t JumpTargetOffset = 6;     // FF 25 00 00 00 00 <abs64>
constexpr std::array<std::size_t, KindCount> RelayOffset{ 0, 16 };

constexpr std::size_t MaximumConfigBytes = 32'768;

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

constexpr std::int32_t Inherit = -1;
constexpr std::int32_t MinimumCap = 0;
constexpr std::int32_t MaximumCap = 100;

struct Config {
    bool         enabled  = true;
    std::int32_t cap      = 75;
    std::array<std::int32_t, StatCount> perStat{ Inherit, Inherit, Inherit };
    bool         gameplay = true;
    bool         display  = true;
};

constexpr char DefaultConfigToml[] =
    "# Dodge Avoid Evade Cap\n"
    "#\n"
    "# Caps the chance to dodge, avoid and evade at a hard percent, 75 by default.\n"
    "#\n"
    "#   passive_dodge  stat 338  melee attacks, while not walking or running\n"
    "#   passive_avoid  stat 339  missiles, while not walking or running\n"
    "#   passive_evade  stat 340  melee and missiles, while walking or running\n"
    "#\n"
    "# Every defender is covered: players, hirelings and monsters.\n"
    "#\n"
    "# The game rolls each chance as a random number from 0 to 99 against the\n"
    "# stat, so a total of 100 or more already succeeds every time. With the cap\n"
    "# at 75, any total above 75 plays exactly like 75.\n"
    "#\n"
    "# Two things are capped, and each can be switched off on its own:\n"
    "#   gameplay  the server-side roll itself\n"
    "#   display   the value shown in the Advanced Stats panel\n"
    "# Keep both on so the panel never shows a chance the roll will not honour.\n"
    "#\n"
    "# Only the value read at those sites is capped. The stat itself, on the\n"
    "# character and on items, is left untouched.\n"
    "\n"
    "[dodge_avoid_evade_cap]\n"
    "\n"
    "# Master switch.\n"
    "enabled = true\n"
    "\n"
    "# Maximum chance in percent, 0 to 100. 75 is the original ESR cap.\n"
    "# 100 plays the same as vanilla, since the roll never goes above 99.\n"
    "cap_percent = 75\n"
    "\n"
    "# Optional per-stat overrides, 0 to 100. -1 means inherit cap_percent.\n"
    "dodge_cap_percent = -1\n"
    "avoid_cap_percent = -1\n"
    "evade_cap_percent = -1\n"
    "\n"
    "# Cap the server-side roll.\n"
    "cap_gameplay = true\n"
    "\n"
    "# Cap the value shown in the Advanced Stats panel.\n"
    "cap_display = true\n";

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

enum class HookState : std::uint8_t {
    NotLoaded,
    DisabledByConfig,
    Armed,
    PartiallyArmed,
};

using GetUnitStatFn =
    std::int32_t(__fastcall*)(void* unit, std::int32_t statId, std::uint16_t layer);

const D2RL::PluginContext* Context{};
std::uint8_t*              Base{};
GetUnitStatFn              GetUnitStat{};
Config                     Settings{};
HookState                  State{ HookState::NotLoaded };
std::int32_t               EffectiveCap[StatCount]{};
void*                      RelayPage{};
bool                       Patched[SiteCount]{};
std::atomic<std::uint64_t> Clamped[KindCount][StatCount]{};

auto KindIndex(SiteKind kind) noexcept -> std::size_t {
    return static_cast<std::size_t>(kind);
}

auto SiteEnabled(const Site& site) noexcept -> bool {
    return site.kind == SiteKind::Gameplay ? Settings.gameplay : Settings.display;
}

auto OriginalCallBytes(const Site& site) noexcept -> const std::uint8_t* {
    return site.witness + (site.callRva - site.witnessRva);
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

auto ParseInteger(std::string_view value, std::int32_t& out) noexcept -> bool {
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
        if (accumulator > 1'000'000) return false;
    }
    out = static_cast<std::int32_t>(negative ? -accumulator : accumulator);
    return true;
}

void ApplyConfigLine(std::string_view key, std::string_view value) noexcept {
    std::int32_t number = 0;
    if (key == "enabled") {
        ParseBool(value, Settings.enabled);
    } else if (key == "cap_percent") {
        if (ParseInteger(value, number)) Settings.cap = number;
    } else if (key == "dodge_cap_percent") {
        if (ParseInteger(value, number)) Settings.perStat[0] = number;
    } else if (key == "avoid_cap_percent") {
        if (ParseInteger(value, number)) Settings.perStat[1] = number;
    } else if (key == "evade_cap_percent") {
        if (ParseInteger(value, number)) Settings.perStat[2] = number;
    } else if (key == "cap_gameplay") {
        ParseBool(value, Settings.gameplay);
    } else if (key == "cap_display") {
        ParseBool(value, Settings.display);
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
                ApplyConfigLine(Trim(line.substr(0, equals)),
                                Trim(line.substr(equals + 1)));
            }
        }
        if (breakAt == std::string_view::npos) break;
    }
}

void ReadConfiguration() noexcept {
    if (!Context->EnsureConfig(DefaultConfigToml)) {
        Context->LogWarn(
            "DodgeAvoidEvadeCap: config file could not be created; using defaults.");
    } else {
        std::string buffer(MaximumConfigBytes, '\0');
        if (Context->ReadConfig(buffer.data(),
                static_cast<std::uint32_t>(buffer.size()), nullptr)) {
            buffer.resize(std::strlen(buffer.c_str()));
            ParseConfig(buffer);
        } else {
            Context->LogWarn(
                "DodgeAvoidEvadeCap: config file could not be read; using defaults.");
        }
    }

    Settings.cap = std::clamp(Settings.cap, MinimumCap, MaximumCap);
    for (std::size_t i = 0; i < StatCount; ++i) {
        const std::int32_t override = Settings.perStat[i];
        EffectiveCap[i] = override < 0
            ? Settings.cap
            : std::clamp(override, MinimumCap, MaximumCap);
    }
}

// ---------------------------------------------------------------------------
// The capped reads, reached from the relays with the getter's own ABI
// ---------------------------------------------------------------------------

auto CapRead(SiteKind kind, std::int32_t statId, std::int32_t value) noexcept
        -> std::int32_t {
    const std::uint32_t index =
        static_cast<std::uint32_t>(statId) - static_cast<std::uint32_t>(StatDodge);
    if (index >= StatCount) return value;
    const std::int32_t cap = EffectiveCap[index];
    if (value <= cap) return value;
    Clamped[KindIndex(kind)][index].fetch_add(1, std::memory_order_relaxed);
    return cap;
}

// The three roll sites. The stat id is the immediate proven by the witness.
std::int32_t __fastcall GameplayStatRead(void* unit, std::int32_t statId,
                                         std::uint16_t layer) noexcept {
    return CapRead(SiteKind::Gameplay, statId, GetUnitStat(unit, statId, layer));
}

// The two panel sites. They read every stat on the panel; CapRead leaves
// anything outside 338..340 untouched.
std::int32_t __fastcall DisplayStatRead(void* unit, std::int32_t statId,
                                        std::uint16_t layer) noexcept {
    return CapRead(SiteKind::Display, statId, GetUnitStat(unit, statId, layer));
}

// ---------------------------------------------------------------------------
// Relay page
// ---------------------------------------------------------------------------

auto CanEncodeRel32(std::uintptr_t from, std::uintptr_t to) noexcept -> bool {
    const std::int64_t delta =
        static_cast<std::int64_t>(to) - static_cast<std::int64_t>(from) - CallSize;
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

auto RelayAddress(SiteKind kind) noexcept -> std::uintptr_t {
    return reinterpret_cast<std::uintptr_t>(RelayPage) + RelayOffset[KindIndex(kind)];
}

void WriteJumpStub(std::uint8_t* stub, std::uint64_t target) noexcept {
    static constexpr std::uint8_t JumpQwordRip[]{ 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00 };
    std::memcpy(stub, JumpQwordRip, sizeof(JumpQwordRip));
    std::memcpy(stub + JumpTargetOffset, &target, sizeof(target));
}

auto EncodeCall(std::uintptr_t from, std::uintptr_t to) noexcept
        -> std::array<std::uint8_t, CallSize> {
    std::array<std::uint8_t, CallSize> bytes{ 0xE8 };
    const auto displacement = static_cast<std::int32_t>(
        static_cast<std::int64_t>(to) - static_cast<std::int64_t>(from + CallSize));
    std::memcpy(bytes.data() + 1, &displacement, sizeof(displacement));
    return bytes;
}

// Points both relays at the vanilla getter. Anything still calling a relay
// then runs native code only.
auto RetargetRelaysToVanilla() noexcept -> bool {
    if (!RelayPage) return true;
    auto* page = static_cast<std::uint8_t*>(RelayPage);
    DWORD previous = 0;
    if (!VirtualProtect(page, RelayPageBytes, PAGE_EXECUTE_READWRITE, &previous)) {
        return false;
    }
    const std::uint64_t vanilla = reinterpret_cast<std::uint64_t>(Base) + GetUnitStatRva;
    for (const auto offset : RelayOffset) {
        std::memcpy(page + offset + JumpTargetOffset, &vanilla, sizeof(vanilla));
    }
    DWORD ignored = 0;
    VirtualProtect(page, RelayPageBytes, previous, &ignored);
    FlushInstructionCache(GetCurrentProcess(), page, RelayPageBytes);
    return true;
}

// Writes the original call back over every patched site.
auto RestoreSites() noexcept -> bool {
    const auto imageBase = reinterpret_cast<std::uintptr_t>(Base);
    bool restored = true;
    for (std::size_t i = SiteCount; i-- > 0;) {
        if (!Patched[i]) continue;
        const Site& site = Sites[i];
        const auto current = EncodeCall(imageBase + site.callRva, RelayAddress(site.kind));
        if (Context->PatchBytes(site.callRva, current.data(), CallSize,
                OriginalCallBytes(site), CallSize)) {
            Patched[i] = false;
        } else {
            restored = false;
        }
    }
    return restored;
}

// ---------------------------------------------------------------------------
// Verification and installation
// ---------------------------------------------------------------------------

auto VerifyNativeContract() noexcept -> bool {
    if (!Context->CheckExpectedBytes(GetUnitStatRva, GetUnitStatExpected.data(),
            static_cast<std::uint32_t>(GetUnitStatExpected.size()))) {
        Context->LogError(
            "DodgeAvoidEvadeCap: STATLIST_GetUnitStat at 0x2F5020 does not match "
            "the verified D2R image. Refusing to load.");
        return false;
    }
    for (const auto& site : Sites) {
        if (!SiteEnabled(site)) continue;
        if (!Context->CheckExpectedBytes(site.witnessRva, site.witness, site.witnessSize)) {
            char message[256];
            std::snprintf(message, sizeof(message),
                "DodgeAvoidEvadeCap: the %s site at 0x%llX does not match the "
                "verified D2R image, or another plugin already owns it. "
                "Refusing to load.",
                site.name, static_cast<unsigned long long>(site.callRva));
            Context->LogError(message);
            return false;
        }
    }
    return true;
}

void ReleaseRelayPage() noexcept {
    if (RelayPage) VirtualFree(RelayPage, 0, MEM_RELEASE);
    RelayPage = nullptr;
}

// Returns false only when nothing in the image can reach this DLL any more,
// so the loader may safely unload it.
auto InstallHooks() noexcept -> bool {
    const auto imageBase = reinterpret_cast<std::uintptr_t>(Base);

    // The relay is searched for upward from the lowest enabled site, so every
    // higher site is closer to it and within reach too (re-checked below).
    std::uint64_t lowest = UINT64_MAX;
    for (const auto& site : Sites) {
        if (SiteEnabled(site) && site.callRva < lowest) lowest = site.callRva;
    }

    RelayPage = AllocateNear(reinterpret_cast<void*>(imageBase + lowest), RelayPageBytes);
    if (!RelayPage) {
        Context->LogError(
            "DodgeAvoidEvadeCap: no relay page was available within rel32 reach.");
        return false;
    }

    auto* page = static_cast<std::uint8_t*>(RelayPage);
    std::memset(page, 0xCC, RelayPageBytes);
    WriteJumpStub(page + RelayOffset[KindIndex(SiteKind::Gameplay)],
        reinterpret_cast<std::uint64_t>(&GameplayStatRead));
    WriteJumpStub(page + RelayOffset[KindIndex(SiteKind::Display)],
        reinterpret_cast<std::uint64_t>(&DisplayStatRead));

    DWORD previousProtection = 0;
    if (!VirtualProtect(page, RelayPageBytes, PAGE_EXECUTE_READ, &previousProtection)) {
        Context->LogError(
            "DodgeAvoidEvadeCap: relay page protection could not be finalized.");
        ReleaseRelayPage();
        return false;
    }
    FlushInstructionCache(GetCurrentProcess(), page, RelayPageBytes);

    for (const auto& site : Sites) {
        if (SiteEnabled(site)
                && !CanEncodeRel32(imageBase + site.callRva, RelayAddress(site.kind))) {
            Context->LogError(
                "DodgeAvoidEvadeCap: relay displacement validation failed.");
            ReleaseRelayPage();
            return false;
        }
    }

    for (std::size_t i = 0; i < SiteCount; ++i) {
        const Site& site = Sites[i];
        if (!SiteEnabled(site)) continue;

        if (Context->PatchCallRel32(site.callRva, OriginalCallBytes(site), CallSize,
                RelayAddress(site.kind) - imageBase, CallSize)) {
            Patched[i] = true;
            continue;
        }

        char message[192];
        std::snprintf(message, sizeof(message),
            "DodgeAvoidEvadeCap: the %s call at 0x%llX could not be redirected.",
            site.name, static_cast<unsigned long long>(site.callRva));
        Context->LogError(message);

        const bool retargeted = RetargetRelaysToVanilla();
        const bool restored = RestoreSites();
        if (restored) {
            ReleaseRelayPage();
            return false;
        }
        if (retargeted) {
            Context->LogError(
                "DodgeAvoidEvadeCap: some sites could not be restored; they now "
                "forward to the vanilla getter, uncapped.");
            return false;
        }
        Context->LogError(
            "DodgeAvoidEvadeCap: rollback failed, staying loaded so the patched "
            "sites keep a valid target. The cap is only partially applied.");
        State = HookState::PartiallyArmed;
        return true;
    }

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
    case HookState::PartiallyArmed:   return "PARTIALLY armed, see log";
    default:                          return "not loaded";
    }
}

auto __cdecl StatusCommand(D2R::Game::Client*,
        const D2RL::ConsoleCommandContext* command, void*) noexcept
        -> D2RL::ConsoleCommandResult {
    if (!command || !command->plugin) return D2RL::ConsoleCommandResult::Failed;

    const auto count = [](SiteKind kind, std::size_t stat) {
        return static_cast<unsigned long long>(
            Clamped[KindIndex(kind)][stat].load(std::memory_order_relaxed));
    };

    char message[448];
    std::snprintf(message, sizeof(message),
        "Dodge Avoid Evade Cap: %s | cap dodge %d%% avoid %d%% evade %d%% | "
        "gameplay %s, clamped dodge %llu avoid %llu evade %llu | "
        "display %s, clamped dodge %llu avoid %llu evade %llu",
        StateName(),
        EffectiveCap[0], EffectiveCap[1], EffectiveCap[2],
        Settings.gameplay ? "on" : "off",
        count(SiteKind::Gameplay, 0), count(SiteKind::Gameplay, 1),
        count(SiteKind::Gameplay, 2),
        Settings.display ? "on" : "off",
        count(SiteKind::Display, 0), count(SiteKind::Display, 1),
        count(SiteKind::Display, 2));
    command->plugin->WriteConsoleMessage(message);
    return D2RL::ConsoleCommandResult::Handled;
}

void RegisterStatusCommand() noexcept {
    if (!Context->RegisterConsoleCommand("daecap", &StatusCommand,
            "Reports Dodge Avoid Evade Cap status and clamp counters.")) {
        Context->LogWarn(
            "DodgeAvoidEvadeCap: the status console command was refused.");
    }
}

constexpr D2RL::PluginInfo Info{
    .infoSize = D2RL::PluginInfoSize,
    .apiVersion = D2RL_PLUGIN_API_VERSION,
    .id = "celestialrayone.dodge-avoid-evade-cap",
    .name = "Dodge Avoid Evade Cap",
    .version = "1.0.0",
    .author = "CelestialRayOne",
    .description =
        "Caps the chance to dodge, avoid and evade at a configurable percent, "
        "in the server roll and in the Advanced Stats panel.",
    .flags = D2RL::PluginFlags::Shared | D2RL::PluginFlags::NativeHooks,
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
    GetUnitStat = reinterpret_cast<GetUnitStatFn>(context->exeBase + GetUnitStatRva);

    ReadConfiguration();

    if (!Settings.enabled || (!Settings.gameplay && !Settings.display)) {
        State = HookState::DisabledByConfig;
        Context->LogInfo(Settings.enabled
            ? "DodgeAvoidEvadeCap: cap_gameplay and cap_display are both off; "
              "no hooks installed."
            : "DodgeAvoidEvadeCap: disabled by configuration.");
        RegisterStatusCommand();
        return true;
    }

    if (!VerifyNativeContract()) return false;
    if (!InstallHooks()) return false;

    char message[256];
    std::snprintf(message, sizeof(message),
        "DodgeAvoidEvadeCap: %s. cap dodge %d%%, avoid %d%%, evade %d%%. "
        "gameplay %s, display %s.",
        StateName(), EffectiveCap[0], EffectiveCap[1], EffectiveCap[2],
        Settings.gameplay ? "on" : "off", Settings.display ? "on" : "off");
    Context->LogInfo(message);

    RegisterStatusCommand();
    return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
    if (!Context || !RelayPage) return;
    RetargetRelaysToVanilla();
    RestoreSites();
    // The relay page is deliberately kept: a thread may be inside a jump
    // through it right now, and any site that could not be restored still
    // needs it to reach the vanilla getter.
}

}  // namespace CelestialRayOne::DodgeAvoidEvadeCap
