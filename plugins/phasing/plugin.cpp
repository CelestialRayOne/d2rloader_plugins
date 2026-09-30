// No Monster Collision
//
// While a player carries a chosen stat (ItemStatCost id 530 by default) with a
// value above 0, its walking and running pass through monsters, the same way
// Whirlwind carries it through a pack. Walls, doors, objects and blocked ground
// still stop it.
//
// Built for D2R 3.3.93847 as hosted by D2RLoader 1.3.1. Every address below is
// an RVA against the executable base, read out of the live D2RLoader.exe image
// and disassembled before being relied on. The plugin checks all of them byte
// for byte at load and installs nothing on any mismatch.
//
// ---------------------------------------------------------------------------
// How movement collision works
// ---------------------------------------------------------------------------
//   Every unit standing on the map writes a presence flag, 1000h (D2MOO
//   COLLIDE_NO_PATH), into the collision grid under itself: monsters, town
//   NPCs and players alike. Hirelings and summons write 2000h (COLLIDE_PET)
//   instead, which a player's walk never tests.
//
//   Each unit's path holds its move-test mask at path+64h. The path planner and
//   every movement step test the grid against it. A player's path is created
//   with 1C09h: wall 1, no-player ground 8, object 400h, door 800h and the
//   presence flag 1000h, so any unit standing in the way stops the player.
//
//     sub_140342C30 (path allocator), player branch (unit type 0):
//       342EB6  BA 07 00 00 00          mov edx, 7             ; straight path
//       342EBB  C7 46 60 80 00 00 00    mov [rsi+60h], 80h     ; footprint: player
//       342EC2  48 8B CE                mov rcx, rsi
//       342EC5  C7 46 64 09 1C 00 00    mov [rsi+64h], 1C09h   ; move-test mask
//       342ECC  E8 9F FB FF FF          call 342A70            ; set path type
//       342ED1  66 C7 86 BD 00 00 00 49 46  mov word [rsi+0BDh], 4649h
//                                            ; max distance 73, A* score 70
//     The monster branch of the same function writes 3C01h or 3401h there,
//     both with the presence flag, so monsters stay blocked by players.
//
//   Whirlwind's server start sub_14056A420 plans and moves with masks that
//   leave the presence flag out, which is exactly why it passes through:
//       56A540  BA 01 0C 00 00          mov edx, 0C01h         ; plan mask
//       56A548  E8 F3 81 DD FF          call 342740            ; set move-test mask
//       56A54D  BF 09 1C 00 00          mov edi, 1C09h         ; kept to restore
//       56A5A0  E8 CB 68 DD FF          call 340E70            ; plan the path
//       56A5CC  BA 01 04 00 00          mov edx, 401h          ; movement mask
//       56A5D4  E8 67 81 DD FF          call 342740
//     342740 is the whole setter:  89 51 64 C3  mov [rcx+64h], edx / ret.
//
// ---------------------------------------------------------------------------
// The change: one inline hook on the path planner
// ---------------------------------------------------------------------------
//   sub_140340E70 (path, unit, a3, a4) plans every path in the game. Client
//   and server both use it (60+ call sites; the client reaches it through its
//   wrapper 215EF0, which calls it as (path, unit, 0, 1)), so one hook covers
//   both sides:
//
//     340E70  48 89 5C 24 08          mov [rsp+8], rbx       ; <- hook, 16 bytes,
//     340E75  48 89 74 24 10          mov [rsp+10h], rsi     ;    no RIP-relative
//     340E7A  48 89 7C 24 18          mov [rsp+18h], rdi     ;    code, ends on an
//     340E7F  55                      push rbp               ;    instruction edge
//     ...
//     340E95  4C 8B F2                mov r14, rdx           ; unit
//     340E98  48 8B D9                mov rbx, rcx           ; path
//     ...
//     340F58  8B 43 64                mov eax, [rbx+64h]     ; move-test mask,
//     340F5B  89 45 F4                mov [rbp-0Ch], eax     ; into the planner
//
//   Before the planner runs, for a player whose path holds the stock walking
//   mask 1C09h and whose stat is above 0, the mask becomes 0C09h: 1C09h
//   without the presence flag and nothing else, so walls, no-player ground,
//   objects and doors keep blocking. When the stat is gone, 0C09h goes back to
//   1C09h. Every other value is a skill's own temporary rule (Whirlwind's 0C01h
//   and 401h, for one) and is never touched. The planner and every following
//   movement step read the field, so the whole move honours it.
//
//   The switch lands on the player's next planned move after the stat appears
//   or disappears. Anything that resets the mask to 1C09h (the end of
//   Whirlwind, the client's position reassign handler at 12AE3C) is re-applied
//   the same way on the next move.
//
// ---------------------------------------------------------------------------
// Other anchors
// ---------------------------------------------------------------------------
//   Unit type is the dword at unit+0, 0 = player:
//     UNITS_GetUnitType 34B9D0 ... 34B9F6  8B 01  mov eax, [rcx]
//
//   STATLIST_GetUnitStat 2F5020 (unit, statId, layer) -> int32 is the
//   D2RLoader thunk to D2RCore ReadWideUnitStat: jmp [rip+disp32], four nops,
//   then the untouched old body. The loader-owned disp32 is not pinned; the
//   thunk shape and the old body identify the function (same check as
//   dodge-cap). Layer 0 is passed.
//
//   Server units carry 4000000h in unit+128h. The server unit allocator sets
//   it right after writing type, class and game (4906A4..4906D4) through
//   34E140, whose body is  mov eax,[rcx+128h] / test r8d,r8d / je / or eax,edx
//   / mov [rcx+128h],eax. Only the diagnostics use it, to report the server
//   and the client separately.

#include <D2RLPlugin/api.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

namespace CelestialRayOne::NoMonsterCollision {
namespace {

// ---------------------------------------------------------------------------
// Native anchors
// ---------------------------------------------------------------------------

constexpr std::uint64_t PathPlannerRva = 0x340E70;

// mov [rsp+8],rbx / mov [rsp+10h],rsi / mov [rsp+18h],rdi / push rbp
constexpr std::array<std::uint8_t, 16> PathPlannerPrologue{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74,
    0x24, 0x10, 0x48, 0x89, 0x7C, 0x24, 0x18, 0x55,
};

// 340E80  push r12 / push r13 / push r14 / push r15 / mov rbp,rsp /
//         sub rsp,70h / mov r12d,r9d / mov r15d,r8d / mov r14,rdx /
//         mov rbx,rcx / test rcx,rcx
constexpr std::uint8_t PlannerArguments[]{
    0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8B,
    0xEC, 0x48, 0x83, 0xEC, 0x70, 0x45, 0x8B, 0xE1, 0x45, 0x8B,
    0xF8, 0x4C, 0x8B, 0xF2, 0x48, 0x8B, 0xD9, 0x48, 0x85, 0xC9,
};

// 340F48  mov eax,[rbx+58h] / lea rcx,[rbp-40h] / mov [rbp-14h],eax /
//         mov eax,[rbx+5Ch] / mov [rbp-10h],eax /
//         mov eax,[rbx+64h] / mov [rbp-0Ch],eax
constexpr std::uint8_t PlannerMaskRead[]{
    0x8B, 0x43, 0x58, 0x48, 0x8D, 0x4D, 0xC0, 0x89, 0x45, 0xEC, 0x8B,
    0x43, 0x5C, 0x89, 0x45, 0xF0, 0x8B, 0x43, 0x64, 0x89, 0x45, 0xF4,
};

// 342EB6  mov edx,7 / mov [rsi+60h],80h / mov rcx,rsi / mov [rsi+64h],1C09h /
//         call 342A70 / mov word [rsi+0BDh],4649h
constexpr std::uint8_t AllocatorPlayerBranch[]{
    0xBA, 0x07, 0x00, 0x00, 0x00, 0xC7, 0x46, 0x60, 0x80, 0x00, 0x00, 0x00,
    0x48, 0x8B, 0xCE, 0xC7, 0x46, 0x64, 0x09, 0x1C, 0x00, 0x00, 0xE8, 0x9F,
    0xFB, 0xFF, 0xFF, 0x66, 0xC7, 0x86, 0xBD, 0x00, 0x00, 0x00, 0x49, 0x46,
};

// 342740  mov [rcx+64h],edx / ret
constexpr std::uint8_t SetMoveTestMask[]{ 0x89, 0x51, 0x64, 0xC3 };

// 34B9D0  UNITS_GetUnitType: null path returns 6, else mov eax,[rcx]
constexpr std::uint8_t GetUnitTypeBody[]{
    0x48, 0x83, 0xEC, 0x28, 0x48, 0x85, 0xC9, 0x75, 0x1D, 0x88, 0x4C, 0x24,
    0x30, 0x48, 0x8D, 0x4C, 0x24, 0x30, 0xE8, 0x39, 0x9E, 0xFF, 0xFF, 0x84,
    0xC0, 0x74, 0x01, 0xCC, 0xB8, 0x06, 0x00, 0x00, 0x00, 0x48, 0x83, 0xC4,
    0x28, 0xC3, 0x8B, 0x01, 0x48, 0x83, 0xC4, 0x28, 0xC3,
};

constexpr std::uint64_t GetUnitStatRva = 0x2F5020;

// jmp qword ptr [rip+disp32], opcode and ModRM only
constexpr std::uint8_t GetUnitStatThunkJump[]{ 0xFF, 0x25 };
constexpr std::uint8_t GetUnitStatThunkNops[]{ 0x90, 0x90, 0x90, 0x90 };

// 2F502A  mov [rsp+20h],rsi / push rdi / sub rsp,20h / movzx ebp,r8w /
//         mov edi,edx / mov rbx,rcx / test rcx,rcx
constexpr std::uint8_t GetUnitStatBody[]{
    0x48, 0x89, 0x74, 0x24, 0x20, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x41,
    0x0F, 0xB7, 0xE8, 0x8B, 0xFA, 0x48, 0x8B, 0xD9, 0x48, 0x85, 0xC9,
};

// 4906A4  mov [rax],r14d / mov rcx,rdi / mov [rax+4],r13d /
//         mov [rax+0D8h],r15 / movzx edx,byte [r15+106h] / call 34E1E0 /
//         mov edx,4000000h / mov r8d,1 / mov rcx,rdi / call 34E140
constexpr std::uint8_t ServerUnitAllocator[]{
    0x44, 0x89, 0x30, 0x48, 0x8B, 0xCF, 0x44, 0x89, 0x68, 0x04, 0x4C, 0x89,
    0xB8, 0xD8, 0x00, 0x00, 0x00, 0x41, 0x0F, 0xB6, 0x97, 0x06, 0x01, 0x00,
    0x00, 0xE8, 0x1E, 0xDB, 0xEB, 0xFF, 0xBA, 0x00, 0x00, 0x00, 0x04, 0x41,
    0xB8, 0x01, 0x00, 0x00, 0x00, 0x48, 0x8B, 0xCF, 0xE8, 0x6B, 0xDA, 0xEB,
    0xFF,
};

// 34E161  mov eax,[rcx+128h] / test r8d,r8d / je / or eax,edx /
//         mov [rcx+128h],eax
constexpr std::uint8_t SetUnitFlagsExBody[]{
    0x8B, 0x81, 0x28, 0x01, 0x00, 0x00, 0x45, 0x85, 0xC0, 0x74,
    0x0D, 0x0B, 0xC2, 0x89, 0x81, 0x28, 0x01, 0x00, 0x00,
};

struct Witness {
    const char*         what;
    std::uint64_t       rva;
    const std::uint8_t* bytes;
    std::uint32_t       size;
};

template <std::size_t N>
constexpr auto MakeWitness(const char* what, std::uint64_t rva,
        const std::uint8_t (&bytes)[N]) noexcept -> Witness {
    return { what, rva, bytes, static_cast<std::uint32_t>(N) };
}

constexpr std::array<Witness, 10> Witnesses{{
    MakeWitness("path planner arguments", 0x340E80, PlannerArguments),
    MakeWitness("path planner mask read", 0x340F48, PlannerMaskRead),
    MakeWitness("path allocator player branch", 0x342EB6, AllocatorPlayerBranch),
    MakeWitness("move-test mask setter", 0x342740, SetMoveTestMask),
    MakeWitness("unit type getter", 0x34B9D0, GetUnitTypeBody),
    MakeWitness("stat getter thunk jump", GetUnitStatRva, GetUnitStatThunkJump),
    MakeWitness("stat getter thunk padding", GetUnitStatRva + 6, GetUnitStatThunkNops),
    MakeWitness("stat getter old body", GetUnitStatRva + 10, GetUnitStatBody),
    MakeWitness("server unit allocator", 0x4906A4, ServerUnitAllocator),
    MakeWitness("unit flag setter", 0x34E161, SetUnitFlagsExBody),
}};

// ---------------------------------------------------------------------------
// Layout and the collision rule
// ---------------------------------------------------------------------------

constexpr std::size_t   UnitTypeOffset     = 0x00;
constexpr std::size_t   UnitFlagsExOffset  = 0x128;
constexpr std::size_t   MoveTestMaskOffset = 0x64;
constexpr std::uint32_t UnitTypePlayer     = 0;
constexpr std::uint32_t ServerUnitFlag     = 0x04000000;

constexpr std::uint32_t UnitPresenceFlag = 0x1000;
constexpr std::uint32_t StockPlayerMask  = 0x1C09;
constexpr std::uint32_t PassThroughMask  = StockPlayerMask & ~UnitPresenceFlag;

// The mask a player's path should hold, given what it holds now. Only the
// stock walking mask and this plugin's own value are ever changed.
constexpr auto WantedMask(std::uint32_t current, bool hasStat) noexcept -> std::uint32_t {
    if (current != StockPlayerMask && current != PassThroughMask) return current;
    return hasStat ? PassThroughMask : StockPlayerMask;
}

static_assert(PassThroughMask == 0x0C09);
static_assert(WantedMask(0x1C09, true) == 0x0C09);   // stat on: walk through
static_assert(WantedMask(0x0C09, true) == 0x0C09);   // stays on
static_assert(WantedMask(0x0C09, false) == 0x1C09);  // stat gone: stock again
static_assert(WantedMask(0x1C09, false) == 0x1C09);  // vanilla untouched
static_assert(WantedMask(0x0C01, true) == 0x0C01);   // Whirlwind plan mask
static_assert(WantedMask(0x0401, true) == 0x0401);   // Whirlwind movement mask
static_assert(WantedMask(0x0000, true) == 0x0000);   // no collision at all

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

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

constexpr std::int32_t DefaultStatId      = 530;
constexpr std::int32_t MaximumStatId      = 32'767;
constexpr std::size_t  MaximumConfigBytes = 32'768;

struct Config {
    bool         enabled = true;
    std::int32_t statId  = DefaultStatId;
};

constexpr char DefaultConfigToml[] = R"toml(# No Monster Collision
#
# While a character's stat (stat_id below) is above 0, the character walks and
# runs through monsters, the same way Whirlwind carries it through a pack.
#
#   - Monsters, town NPCs and other players no longer stop the character.
#   - Walls, doors, objects and blocked ground still do.
#   - Monsters still cannot walk through the character.
#   - Skills that move the character by their own rules, such as Whirlwind,
#     keep them.
#   - Players only. Hirelings, summons and monsters are never affected.
#
# The change takes effect on the character's next move after the stat appears
# or disappears.
#
# The server and your client each check the stat on their own copy of the
# character. A stat from an item is always on both. If you grant it another
# way, type nomonstercollision in the console after walking through a pack:
# the server and client parts must both show the stat. If only the server
# does, your client stops at monsters while the server walks on, and the game
# snaps the character forward.
#
# Console command: nomonstercollision (status, last stat read, counts)
# Log: d2rloader\logs\celestialrayone.no-monster-collision.log

[no_monster_collision]

# Master switch.
enabled = true

# ItemStatCost.txt id of the stat that grants the effect, 0 to 32767.
# Any value above 0 on the character turns it on.
stat_id = 530
)toml";

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

enum class PluginState : std::uint8_t {
    NotLoaded,
    DisabledByConfig,
    BadConfig,
    Armed,
};

enum class Side : std::size_t { Server = 0, Client = 1 };
constexpr std::size_t SideCount = 2;

struct SideCounters {
    std::atomic<std::uint64_t> walksPlanned{ 0 };
    std::atomic<std::uint64_t> passThroughSet{ 0 };
    std::atomic<std::uint64_t> collisionRestored{ 0 };
    std::atomic<std::int32_t>  lastStatValue{ 0 };
    std::atomic<bool>          seen{ false };
};

using GetUnitStatFn =
    std::int32_t(__fastcall*)(void* unit, std::int32_t statId, std::uint32_t layer);
using PathPlannerFn =
    std::uint64_t(__fastcall*)(void* path, void* unit, std::int32_t a3, std::int32_t a4);

constexpr std::uint32_t MaximumLoggedChanges = 64;

const D2RL::PluginContext* Context{};
std::uintptr_t             Base{};
GetUnitStatFn              GetUnitStat{};
void*                      OriginalPathPlanner{};
Config                     Settings{};
PluginState                State{ PluginState::NotLoaded };
SideCounters               Counters[SideCount]{};
std::atomic<std::uint32_t> LoggedChanges{ 0 };

auto Index(Side side) noexcept -> std::size_t {
    return static_cast<std::size_t>(side);
}

auto SideName(Side side) noexcept -> const char* {
    return side == Side::Server ? "server" : "client";
}

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
    if (key == "enabled") {
        ParseBool(value, Settings.enabled);
    } else if (key == "stat_id") {
        std::int32_t number = 0;
        if (ParseInteger(value, number)) Settings.statId = number;
    }
}

void ParseConfig(std::string_view text) noexcept {
    std::size_t cursor = 0;
    while (cursor <= text.size()) {
        const std::size_t breakAt = text.find('\n', cursor);
        const std::size_t end = breakAt == std::string_view::npos ? text.size() : breakAt;
        std::string_view line = Trim(text.substr(cursor, end - cursor));
        cursor = end + 1;

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
        Context->LogWarn(
            "NoMonsterCollision: config file could not be created; using defaults.");
        return;
    }
    std::string buffer(MaximumConfigBytes, '\0');
    if (!Context->ReadConfig(buffer.data(), static_cast<std::uint32_t>(buffer.size()), nullptr)) {
        Context->LogWarn(
            "NoMonsterCollision: config file could not be read; using defaults.");
        return;
    }
    buffer.resize(std::strlen(buffer.c_str()));
    ParseConfig(buffer);
}

// ---------------------------------------------------------------------------
// The hook
// ---------------------------------------------------------------------------

auto IsPlayer(const void* unit) noexcept -> bool {
    return Read<std::uint32_t>(unit, UnitTypeOffset) == UnitTypePlayer;
}

auto SideOf(const void* unit) noexcept -> Side {
    return (Read<std::uint32_t>(unit, UnitFlagsExOffset) & ServerUnitFlag) != 0
        ? Side::Server
        : Side::Client;
}

// Changes are rare (once per stat change, and once after anything resets the
// mask to stock), so each one is logged up to a fixed total.
void LogChange(Side side, bool passThrough, std::int32_t value) noexcept {
    const std::uint32_t index = LoggedChanges.fetch_add(1, std::memory_order_relaxed);
    if (index > MaximumLoggedChanges) return;

    char message[256];
    if (index == MaximumLoggedChanges) {
        std::snprintf(message, sizeof(message),
            "NoMonsterCollision: %u changes logged; further ones are only counted "
            "(console command nomonstercollision).",
            MaximumLoggedChanges);
    } else {
        std::snprintf(message, sizeof(message),
            passThrough
                ? "NoMonsterCollision: %s: stat %d is %d on a player; its walking "
                  "and running now pass through monsters."
                : "NoMonsterCollision: %s: stat %d is %d on a player; its walking "
                  "and running collide with monsters again.",
            SideName(side), Settings.statId, value);
    }
    Context->LogInfo(message);
}

// Runs for every player path about to be planned, on the client and the
// server alike. Anything but the stock walking mask or this plugin's own value
// is a skill's temporary rule and is left alone.
void ApplyStat(void* path, void* unit) noexcept {
    const auto current = Read<std::uint32_t>(path, MoveTestMaskOffset);
    if (current != StockPlayerMask && current != PassThroughMask) return;

    const std::int32_t value = GetUnitStat(unit, Settings.statId, 0);
    const Side side = SideOf(unit);
    SideCounters& counters = Counters[Index(side)];
    counters.walksPlanned.fetch_add(1, std::memory_order_relaxed);
    counters.lastStatValue.store(value, std::memory_order_relaxed);
    counters.seen.store(true, std::memory_order_relaxed);

    const std::uint32_t wanted = WantedMask(current, value > 0);
    if (wanted == current) return;

    Write<std::uint32_t>(path, MoveTestMaskOffset, wanted);
    const bool passThrough = wanted == PassThroughMask;
    (passThrough ? counters.passThroughSet : counters.collisionRestored)
        .fetch_add(1, std::memory_order_relaxed);
    LogChange(side, passThrough, value);
}

std::uint64_t __fastcall HookedPathPlanner(void* path, void* unit,
                                           std::int32_t a3, std::int32_t a4) noexcept {
    if (path != nullptr && unit != nullptr && IsPlayer(unit)) {
        ApplyStat(path, unit);
    }
    return reinterpret_cast<PathPlannerFn>(OriginalPathPlanner)(path, unit, a3, a4);
}

// ---------------------------------------------------------------------------
// Verification and installation
// ---------------------------------------------------------------------------

auto VerifyNativeContract() noexcept -> bool {
    for (const Witness& witness : Witnesses) {
        if (Context->CheckExpectedBytes(witness.rva, witness.bytes, witness.size)) continue;
        char message[256];
        std::snprintf(message, sizeof(message),
            "NoMonsterCollision: the %s at 0x%llX does not match the verified D2R "
            "3.3 image under D2RLoader 1.3.1. Nothing installed.",
            witness.what, static_cast<unsigned long long>(witness.rva));
        Context->LogError(message);
        return false;
    }
    return true;
}

auto InstallHook() noexcept -> bool {
    if (!Context->InstallInlineHook(PathPlannerRva, PathPlannerPrologue.data(),
            static_cast<std::uint32_t>(PathPlannerPrologue.size()),
            reinterpret_cast<void*>(&HookedPathPlanner), &OriginalPathPlanner)
            || OriginalPathPlanner == nullptr) {
        Context->LogError(
            "NoMonsterCollision: the path planner hook at 0x340E70 could not be "
            "installed. Its first 16 bytes differ, or another plugin already hooks it.");
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Console command
// ---------------------------------------------------------------------------

auto StateName() noexcept -> const char* {
    switch (State) {
    case PluginState::DisabledByConfig: return "disabled by config";
    case PluginState::BadConfig:        return "NOT installed, stat_id is outside 0 to 32767";
    case PluginState::Armed:            return "armed";
    default:                            return "not loaded";
    }
}

void FormatSide(char* out, std::size_t size, Side side) noexcept {
    const SideCounters& counters = Counters[Index(side)];
    if (!counters.seen.load(std::memory_order_relaxed)) {
        std::snprintf(out, size, "%s: no walk planned yet", SideName(side));
        return;
    }
    std::snprintf(out, size,
        "%s: last stat read %d, walks planned %llu, "
        "pass-through set %llu, collision restored %llu",
        SideName(side),
        counters.lastStatValue.load(std::memory_order_relaxed),
        static_cast<unsigned long long>(counters.walksPlanned.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(counters.passThroughSet.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(counters.collisionRestored.load(std::memory_order_relaxed)));
}

auto __cdecl StatusCommand(D2R::Game::Client*,
        const D2RL::ConsoleCommandContext* command, void*) noexcept
        -> D2RL::ConsoleCommandResult {
    if (command == nullptr || command->plugin == nullptr) {
        return D2RL::ConsoleCommandResult::Failed;
    }

    char server[192];
    char client[192];
    FormatSide(server, sizeof(server), Side::Server);
    FormatSide(client, sizeof(client), Side::Client);

    char message[512];
    std::snprintf(message, sizeof(message),
        "No Monster Collision: %s | stat %d | %s | %s",
        StateName(), Settings.statId, server, client);
    command->plugin->WriteConsoleMessage(message);
    return D2RL::ConsoleCommandResult::Handled;
}

void RegisterStatusCommand() noexcept {
    if (!Context->RegisterConsoleCommand("nomonstercollision", &StatusCommand,
            "Reports No Monster Collision status, the stat each side last read, "
            "and change counts.")) {
        Context->LogWarn("NoMonsterCollision: the status console command was refused.");
    }
}

constexpr D2RL::PluginInfo Info{
    .infoSize = D2RL::PluginInfoSize,
    .abiVersion = D2RL_PLUGIN_ABI_VERSION,
    .id = "celestialrayone.no-monster-collision",
    .name = "No Monster Collision",
    .version = "1.0.0",
    .author = "CelestialRayOne",
    .description =
        "A character with a chosen stat walks and runs through monsters, "
        "the way Whirlwind does.",
    .flags = D2RL::PluginFlags::Shared | D2RL::PluginFlags::NativeHooks,
    .reserved = {},
};

}  // namespace

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* {
    return &Info;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(
        const D2RL::PluginContext* context) noexcept -> bool {
    if (!D2RL::HasContext(context)) return false;
    Context = context;
    Base = context->exeBase;

    ReadConfiguration();

    if (!Settings.enabled) {
        State = PluginState::DisabledByConfig;
        Context->LogInfo("NoMonsterCollision: disabled by configuration.");
        RegisterStatusCommand();
        return true;
    }

    if (Settings.statId < 0 || Settings.statId > MaximumStatId) {
        State = PluginState::BadConfig;
        char message[160];
        std::snprintf(message, sizeof(message),
            "NoMonsterCollision: stat_id %d is outside 0 to 32767. Nothing installed.",
            Settings.statId);
        Context->LogError(message);
        RegisterStatusCommand();
        return true;
    }

    if (!VerifyNativeContract()) return false;

    // Everything the hook reads is set before the hook can run.
    GetUnitStat = reinterpret_cast<GetUnitStatFn>(Base + GetUnitStatRva);
    if (!InstallHook()) return false;

    State = PluginState::Armed;
    char message[192];
    std::snprintf(message, sizeof(message),
        "NoMonsterCollision: armed. A player with stat %d above 0 walks and runs "
        "through monsters.",
        Settings.statId);
    Context->LogInfo(message);

    RegisterStatusCommand();
    return true;
}

}  // namespace CelestialRayOne::NoMonsterCollision
