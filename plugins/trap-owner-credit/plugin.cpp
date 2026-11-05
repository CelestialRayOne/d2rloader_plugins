// Trap Owner Credit
//
// Credits the kills and the weapon-damage hits of listed monsters (assassin
// traps by default) to the player who owns them. The behaviour write-up is the
// embedded TOML below (celestialrayone.trap-owner-credit.toml); this header is
// the native map.
//
// Built for D2R 3.3 in the D2RLoader 1.3.1 process image (base 0x140000000),
// PluginSDK 0.3.0. Every byte below was read out of that image and
// disassembled before being relied on. Every window is checked at load: a part
// whose bytes do not match is left out and named in the log, the rest runs.
//
// ---------------------------------------------------------------------------
// Where the game fires the events: SUNITDMG_ExecuteEvents 0x44CE80
// ---------------------------------------------------------------------------
//   (game rcx -> r15, attacker rdx -> r14, defender r8 -> rsi,
//    bMissile r9d -> ebx, D2Damage* argument 5 -> rdi)
//   44CF98  movzx eax, word [rdi+4]        result flags
//   44CF9C  test al,20h / jne 44D001       soft hit: no hit events
//   44CFA0  test al,1   / je  44D001       no successful hit: no hit events
//   44CFA4  test ebx,ebx / je 44CFD1       melee branch
//   44CFA8  mov eax,[rdi] / test al,20h / je 44D001
//           missile: no hit events at all unless hit flag 20h is set.
//           SUNITDMG_FillDamageValues 0x44C030 sets it at 44C0F1 when it adds
//           the attacker's weapon damage (SrcDam).
//   44CFAE  test al,al / js 44CFCA         hit flag 80h: no domissiledamage
//   44CFC5  E8 A6 05 00 00  call 44D570    domissiledamage (6), attacker  RELAYED
//   44CFE4  E8 87 05 00 00  call 44D570    domeleedamage (5), attacker    RELAYED
//   44CFFC  E8 6F 05 00 00  call 44D570    damagedinmelee / damagedbymissile
//                                          (1 / 2) on the defender, untouched
//   44D4FE  movzx eax, word [rdi+4] / test al,20h / jne / test al,2 / je
//   44D51D  E8 4E 00 00 00  call 44D570    killed (10) on the defender, untouched
//   44D535  E8 36 00 00 00  call 44D570    kill (9) on the attacker       RELAYED
//   The three relayed calls pass (game, event, attacker, defender, D2Damage*)
//   and their return value is never read.
//
// D2GAME_ApplyUnitStatEvent 0x44D570 (game, eventId, unit, other, damage)
//   builds its callback object at rsp+30h and calls the dispatcher at 44D5F2
//   with (game, eventId, unit, other, damage, &object), returning its result.
//
// ---------------------------------------------------------------------------
// Damage over time deaths: D2GAME_MONSTER_ApplyStatRegen 0x448C00
// ---------------------------------------------------------------------------
//   The negative life tick (poison, bleed, burning, Open Wounds).
//   448DC7..448E2E  killer = SUNIT_GetServerUnit(game, owner type, owner id)
//                   of the state 2 stat list, else of the state 62 list
//   448EA4  E8 37 F3 13 00  call 5881E0    killed (10) on the victim, untouched
//   448EF5  test rbx,rbx / je              no killer: no kill event at all
//   448F17  [rsp+20h] = r15 = 0 (no damage packet), [rsp+28h] = &object
//   448F36  E8 A5 F2 13 00  call 5881E0    kill (9) on the killer         RELAYED
//
// 0x5881E0 is D2RLoader's jmp into D2RCore's event dispatcher, called with
// (game, eventId, unit, other, damage, object) -> int32.
//
// Every other code caller of 0x44D570 and 0x5881E0 was read as well. They fire
// events 0, 3, 7, 11, 12, 13, 15 and 16 only. So the four relayed calls are
// every place the game fires kill, domeleedamage and domissiledamage.
//
// ---------------------------------------------------------------------------
// Owner, type, class
// ---------------------------------------------------------------------------
//   D2GAME_GetMinionOwner 0x4A53C0: MonsterData [unit+10h] -> AI control
//     [+30h] -> SUNIT_GetServerUnit(game [+60h], type [+6Ch], id [+68h]).
//     It asserts when MonsterData or the AI control block is missing
//     (4A5416, 4A543A), so the plugin checks both pointers first.
//   SUNIT_IsDead 0x34C2C0: dead flag [unit+124h] & 10000h, players in mode 0
//     or 17, monsters in mode 0 or 12.
//   UNITS_GetUnitType 0x34B9D0 returns [unit+0]; UNITS_GetClassId 0x349860
//     returns [unit+4], the monstats.txt row. Both assert on a null unit, so
//     the plugin reads the two fields itself after its own null check.
//
// ---------------------------------------------------------------------------
// How the calls are relayed
// ---------------------------------------------------------------------------
//   Only the rel32 of each call changes. It lands in a page allocated next to
//   the image that holds two FF 25 absolute jumps (target qword 8-byte
//   aligned at +8 of each 16-byte slot), one into HookedUnitEvent
//   (the three 0x44D570 calls) and one into HookedDotKill (the 0x5881E0 call).
//   Both hooks take the callee's exact arguments and make the original call,
//   so nothing else in either function sees a difference. The function
//   entries 0x44D570 and 0x5881E0 stay untouched. D2RLoader restores the four
//   calls when the plugin unloads; on unload the plugin also points both relay
//   jumps at the native callees, and the page is never freed.

#include <D2RLPlugin/api.h>

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

namespace CelestialRayOne::TrapOwnerCredit {
namespace {

// ---------------------------------------------------------------------------
// Native anchors
// ---------------------------------------------------------------------------

constexpr std::uint64_t UnitEventRva       = 0x44D570;   // D2GAME_ApplyUnitStatEvent
constexpr std::uint64_t DispatchRva        = 0x5881E0;   // loader jmp to D2RCore's dispatcher
constexpr std::uint64_t GetMinionOwnerRva  = 0x4A53C0;
constexpr std::uint64_t IsDeadRva          = 0x34C2C0;
constexpr std::uint64_t GetUnitTypeRva     = 0x34B9D0;
constexpr std::uint64_t GetClassIdRva      = 0x349860;

constexpr std::uint64_t OnHitWindowRva     = 0x44CF98;
constexpr std::uint64_t KillWindowRva      = 0x44D4FE;
constexpr std::uint64_t DotKillWindowRva   = 0x448EF5;

constexpr std::uint64_t MissileHitCallRva  = 0x44CFC5;   // event 6
constexpr std::uint64_t MeleeHitCallRva    = 0x44CFE4;   // event 5
constexpr std::uint64_t CombatKillCallRva  = 0x44D535;   // event 9
constexpr std::uint64_t DotKillCallRva     = 0x448F36;   // event 9

constexpr std::uint32_t CallSize = 5;

constexpr std::int32_t EventDoMeleeDamage   = 5;
constexpr std::int32_t EventDoMissileDamage = 6;
constexpr std::int32_t EventKill            = 9;

constexpr std::uint32_t UnitTypePlayer  = 0;
constexpr std::uint32_t UnitTypeMonster = 1;

constexpr std::size_t UnitTypeOffset    = 0x00;
constexpr std::size_t UnitClassIdOffset = 0x04;
constexpr std::size_t UnitIdOffset      = 0x08;
constexpr std::size_t MonsterDataOffset = 0x10;
constexpr std::size_t AiControlOffset   = 0x30;

// D2Damage+0 is the hit flags dword; 20h means the hit carries weapon damage.
constexpr std::uint32_t HitFlagWeaponDamage = 0x20;

constexpr std::size_t MaxOwnerDepth   = 8;
constexpr std::size_t MaxMonsterIds   = 256;
constexpr std::uint32_t MaxMonsterId  = 0xFFFF;
constexpr std::size_t MaxSeenRows     = 32;
constexpr std::uint64_t MaxCreditLogs = 8;
constexpr std::uint32_t ConfigReadBufferBytes = 16'384;

// ---------------------------------------------------------------------------
// Witnesses, cut from the live image
// ---------------------------------------------------------------------------

// 0x44CF98..0x44D001: the hit event block of SUNITDMG_ExecuteEvents, both
// relayed hit calls included (6 at +2Dh, 5 at +4Ch).
constexpr std::uint8_t OnHitWindow[]{
    0x0F, 0xB7, 0x47, 0x04, 0xA8, 0x20, 0x75, 0x61, 0xA8, 0x01, 0x74, 0x5D, 0x85, 0xDB, 0x74, 0x29,
    0x8B, 0x07, 0xA8, 0x20, 0x74, 0x53, 0x84, 0xC0, 0x78, 0x18, 0x4C, 0x8B, 0xCE, 0x48, 0x89, 0x7C,
    0x24, 0x20, 0x4D, 0x8B, 0xC6, 0xBA, 0x06, 0x00, 0x00, 0x00, 0x49, 0x8B, 0xCF, 0xE8, 0xA6, 0x05,
    0x00, 0x00, 0xBA, 0x02, 0x00, 0x00, 0x00, 0xEB, 0x1D, 0x4C, 0x8B, 0xCE, 0x48, 0x89, 0x7C, 0x24,
    0x20, 0x4D, 0x8B, 0xC6, 0xBA, 0x05, 0x00, 0x00, 0x00, 0x49, 0x8B, 0xCF, 0xE8, 0x87, 0x05, 0x00,
    0x00, 0xBA, 0x01, 0x00, 0x00, 0x00, 0x4D, 0x8B, 0xCE, 0x48, 0x89, 0x7C, 0x24, 0x20, 0x4C, 0x8B,
    0xC6, 0x49, 0x8B, 0xCF, 0xE8, 0x6F, 0x05, 0x00, 0x00,
};

// 0x44D4FE..0x44D53A: the kill event block of SUNITDMG_ExecuteEvents, the
// relayed kill call at +37h.
constexpr std::uint8_t KillWindow[]{
    0x0F, 0xB7, 0x47, 0x04, 0xA8, 0x20, 0x75, 0x34, 0xA8, 0x02, 0x74, 0x30, 0x4D, 0x8B, 0xCE, 0x48,
    0x89, 0x7C, 0x24, 0x20, 0x4C, 0x8B, 0xC6, 0xBA, 0x0A, 0x00, 0x00, 0x00, 0x49, 0x8B, 0xCF, 0xE8,
    0x4E, 0x00, 0x00, 0x00, 0x4C, 0x8B, 0xCE, 0x48, 0x89, 0x7C, 0x24, 0x20, 0x4D, 0x8B, 0xC6, 0xBA,
    0x09, 0x00, 0x00, 0x00, 0x49, 0x8B, 0xCF, 0xE8, 0x36, 0x00, 0x00, 0x00,
};

// 0x448EF5..0x448F3B: the regen tick's kill event, the relayed call at +41h.
constexpr std::uint8_t DotKillWindow[]{
    0x48, 0x85, 0xDB, 0x0F, 0x84, 0x89, 0x00, 0x00, 0x00, 0x48, 0x8D, 0x44, 0x24, 0x48, 0x48, 0xC7,
    0x44, 0x24, 0x48, 0x01, 0x00, 0x00, 0x00, 0x0F, 0x57, 0xC0, 0x48, 0x89, 0x44, 0x24, 0x28, 0x0F,
    0x57, 0xC9, 0x4C, 0x89, 0x7C, 0x24, 0x20, 0x4C, 0x8B, 0xCF, 0x4C, 0x8B, 0xC3, 0xBA, 0x09, 0x00,
    0x00, 0x00, 0x48, 0x8B, 0xCD, 0xF3, 0x0F, 0x7F, 0x44, 0x24, 0x50, 0xF3, 0x0F, 0x7F, 0x4C, 0x24,
    0x60, 0xE8, 0xA5, 0xF2, 0x13, 0x00,
};

// 0x44D570..0x44D5FC: D2GAME_ApplyUnitStatEvent up to and including its call
// into the dispatcher, the proof of both ABIs.
constexpr std::uint8_t UnitEventWindow[]{
    0x40, 0x53, 0x55, 0x56, 0x57, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x60, 0x48, 0x8B, 0x05, 0x46, 0xDD,
    0x57, 0x02, 0x48, 0x33, 0xC4, 0x48, 0x89, 0x44, 0x24, 0x58, 0x48, 0x8B, 0x9C, 0x24, 0xB0, 0x00,
    0x00, 0x00, 0x0F, 0x57, 0xC0, 0x48, 0xC7, 0x44, 0x24, 0x30, 0x01, 0x00, 0x00, 0x00, 0x0F, 0x57,
    0xC9, 0x4D, 0x8B, 0xF1, 0x49, 0x8B, 0xE8, 0x8B, 0xF2, 0x48, 0x8B, 0xF9, 0xF3, 0x0F, 0x7F, 0x44,
    0x24, 0x38, 0xF3, 0x0F, 0x7F, 0x4C, 0x24, 0x48, 0x48, 0x85, 0xDB, 0x74, 0x1B, 0x48, 0x8D, 0x93,
    0x58, 0x01, 0x00, 0x00, 0x48, 0x8D, 0x44, 0x24, 0x30, 0x48, 0x3B, 0xD0, 0x74, 0x0A, 0x48, 0x8D,
    0x4C, 0x24, 0x30, 0xE8, 0xE8, 0xB9, 0xFF, 0xFF, 0x48, 0x8D, 0x44, 0x24, 0x30, 0x4D, 0x8B, 0xCE,
    0x48, 0x89, 0x44, 0x24, 0x28, 0x4C, 0x8B, 0xC5, 0x8B, 0xD6, 0x48, 0x89, 0x5C, 0x24, 0x20, 0x48,
    0x8B, 0xCF, 0xE8, 0xE9, 0xAB, 0x13, 0x00, 0x48, 0x8B, 0x4C, 0x24, 0x30,
};

// 0x4A53C0..0x4A547D: D2GAME_GetMinionOwner, whole body.
constexpr std::uint8_t OwnerWindow[]{
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0xE8, 0x02, 0x66, 0xEA, 0xFF, 0x83, 0xF8,
    0x01, 0x0F, 0x85, 0x9E, 0x00, 0x00, 0x00, 0x48, 0x85, 0xDB, 0x74, 0x0D, 0x48, 0x8B, 0xCB, 0xE8,
    0xEC, 0x65, 0xEA, 0xFF, 0x83, 0xF8, 0x01, 0x74, 0x19, 0x48, 0x8D, 0x4C, 0x24, 0x38, 0xC6, 0x44,
    0x24, 0x38, 0x00, 0xE8, 0x48, 0xB6, 0xF5, 0xFF, 0x84, 0xC0, 0x74, 0x01, 0xCC, 0x48, 0x85, 0xDB,
    0x74, 0x73, 0x48, 0x8B, 0xCB, 0xE8, 0xC6, 0x65, 0xEA, 0xFF, 0x83, 0xF8, 0x01, 0x75, 0x66, 0x48,
    0x83, 0x7B, 0x10, 0x00, 0x75, 0x14, 0x48, 0x8D, 0x4C, 0x24, 0x38, 0xC6, 0x44, 0x24, 0x38, 0x00,
    0xE8, 0xBB, 0xB5, 0xF5, 0xFF, 0x84, 0xC0, 0x74, 0x01, 0xCC, 0x48, 0x8B, 0x43, 0x10, 0x48, 0x85,
    0xC0, 0x74, 0x42, 0x48, 0x83, 0x78, 0x30, 0x00, 0x75, 0x14, 0x48, 0x8D, 0x4C, 0x24, 0x38, 0xC6,
    0x44, 0x24, 0x38, 0x00, 0xE8, 0x67, 0xB5, 0xF5, 0xFF, 0x84, 0xC0, 0x74, 0x01, 0xCC, 0x48, 0x8B,
    0x43, 0x10, 0x48, 0x8B, 0x50, 0x30, 0x48, 0x85, 0xD2, 0x74, 0x1A, 0x48, 0x8B, 0x4A, 0x60, 0x48,
    0x85, 0xC9, 0x74, 0x11, 0x44, 0x8B, 0x42, 0x68, 0x8B, 0x52, 0x6C, 0x48, 0x83, 0xC4, 0x20, 0x5B,
    0xE9, 0x0B, 0xAA, 0xFE, 0xFF, 0x33, 0xC0, 0x48, 0x83, 0xC4, 0x20, 0x5B, 0xC3,
};

// 0x34C2C0..0x34C2F2: SUNIT_IsDead up to the dead flag test.
constexpr std::uint8_t DeadWindow[]{
    0x48, 0x83, 0xEC, 0x28, 0x48, 0x85, 0xC9, 0x75, 0x1D, 0x88, 0x4C, 0x24, 0x30, 0x48, 0x8D, 0x4C,
    0x24, 0x30, 0xE8, 0x59, 0x94, 0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x4D, 0xCC, 0xB8, 0x01, 0x00, 0x00,
    0x00, 0x48, 0x83, 0xC4, 0x28, 0xC3, 0xF7, 0x81, 0x24, 0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
    0x75, 0x36,
};

// 0x34B9D0..0x34B9FD: UNITS_GetUnitType, mov eax,[rcx] at +26h.
constexpr std::uint8_t UnitTypeWindow[]{
    0x48, 0x83, 0xEC, 0x28, 0x48, 0x85, 0xC9, 0x75, 0x1D, 0x88, 0x4C, 0x24, 0x30, 0x48, 0x8D, 0x4C,
    0x24, 0x30, 0xE8, 0x39, 0x9E, 0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x01, 0xCC, 0xB8, 0x06, 0x00, 0x00,
    0x00, 0x48, 0x83, 0xC4, 0x28, 0xC3, 0x8B, 0x01, 0x48, 0x83, 0xC4, 0x28, 0xC3,
};

// 0x349860..0x34988E: UNITS_GetClassId, mov eax,[rcx+4] at +26h.
constexpr std::uint8_t ClassIdWindow[]{
    0x48, 0x83, 0xEC, 0x28, 0x48, 0x85, 0xC9, 0x75, 0x1D, 0x88, 0x4C, 0x24, 0x30, 0x48, 0x8D, 0x4C,
    0x24, 0x30, 0xE8, 0x49, 0xCB, 0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x01, 0xCC, 0xB8, 0xFF, 0xFF, 0xFF,
    0xFF, 0x48, 0x83, 0xC4, 0x28, 0xC3, 0x8B, 0x41, 0x04, 0x48, 0x83, 0xC4, 0x28, 0xC3,
};

template <std::size_t Size>
constexpr auto ByteCount(const std::uint8_t (&)[Size]) noexcept -> std::uint32_t {
    return static_cast<std::uint32_t>(Size);
}

// Each relayed call, its window and where the call sits inside it.
struct CallSite {
    const char*         name;
    std::uint64_t       rva;
    std::uint64_t       windowRva;
    const std::uint8_t* window;
    std::uint32_t       windowSize;
    std::uint64_t       nativeTarget;
    std::size_t         relaySlot;
};

enum SiteIndex : std::size_t { MissileHitSite = 0, MeleeHitSite, CombatKillSite, DotKillSite, SiteCount };

constexpr std::size_t UnitEventRelaySlot = 0;
constexpr std::size_t DispatchRelaySlot  = 1;

constexpr std::array<CallSite, SiteCount> Sites{{
    { "on hit, missiles (domissiledamage)", MissileHitCallRva, OnHitWindowRva, OnHitWindow,
      ByteCount(OnHitWindow), UnitEventRva, UnitEventRelaySlot },
    { "on hit, melee (domeleedamage)", MeleeHitCallRva, OnHitWindowRva, OnHitWindow,
      ByteCount(OnHitWindow), UnitEventRva, UnitEventRelaySlot },
    { "combat kill", CombatKillCallRva, KillWindowRva, KillWindow,
      ByteCount(KillWindow), UnitEventRva, UnitEventRelaySlot },
    { "poison/bleed/burn kill", DotKillCallRva, DotKillWindowRva, DotKillWindow,
      ByteCount(DotKillWindow), DispatchRva, DispatchRelaySlot },
}};

constexpr auto CallTargetInWindow(const CallSite& site) noexcept -> std::uint64_t {
    const std::size_t offset = static_cast<std::size_t>(site.rva - site.windowRva);
    const std::uint32_t rel = static_cast<std::uint32_t>(site.window[offset + 1])
        | (static_cast<std::uint32_t>(site.window[offset + 2]) << 8)
        | (static_cast<std::uint32_t>(site.window[offset + 3]) << 16)
        | (static_cast<std::uint32_t>(site.window[offset + 4]) << 24);
    return site.rva + CallSize + static_cast<std::uint64_t>(static_cast<std::int64_t>(static_cast<std::int32_t>(rel)));
}

constexpr auto SitesAreWellFormed() noexcept -> bool {
    for (const CallSite& site : Sites) {
        if (site.rva < site.windowRva || site.rva + CallSize > site.windowRva + site.windowSize) return false;
        if (site.window[site.rva - site.windowRva] != 0xE8) return false;
        if (CallTargetInWindow(site) != site.nativeTarget) return false;
    }
    return true;
}
static_assert(SitesAreWellFormed(), "every relayed call must be an E8 to its native target inside its window");

// ---------------------------------------------------------------------------
// Game functions
// ---------------------------------------------------------------------------

using UnitEventFn = std::int32_t(__fastcall*)(void* game, std::int32_t eventId, void* unit, void* other,
    void* damage) noexcept;
using DispatchFn = std::int32_t(__fastcall*)(void* game, std::int32_t eventId, void* unit, void* other,
    void* damage, void* object) noexcept;
using GetMinionOwnerFn = void*(__fastcall*)(void* unit) noexcept;
using IsDeadFn = std::int32_t(__fastcall*)(void* unit) noexcept;

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

constexpr char DefaultConfigToml[] = R"toml(# ============================================================================
# Trap Owner Credit
# ============================================================================
#
# WHAT IT DOES
# A trap is a monster, so in the stock game its kills and its hits belong to
# the trap. The owner's own "on kill" and "on hit" effects never fire for
# them. This plugin credits them to the player who owns the trap.
#
# Kills: the owner gets the kill instead of the trap.
#   - Life after each Kill, Mana after each Kill, Life after each Demon Kill
#   - Chance to cast on kill, Reanimate As, Slain Monsters Rest in Peace
#   - Any skills.txt auraevent set to kill on the owner
#   - Combat kills and kills by poison, bleed, burning and Open Wounds alike
#   - Rest in Peace on the owner now also stops corpses of trap kills from
#     being used, Death Sentry's corpse explosion included
#
# On hit: the owner's on-hit effects also fire on the trap's hits.
#   - Crushing Blow, Open Wounds, chance to cast on striking, Knockback,
#     Slows Target, Freezes Target, Hit Blinds Target, Hit Causes Monster
#     to Flee
#   - Any skills.txt auraevent set to domeleedamage or domissiledamage on
#     the owner
#   - Only hits that carry weapon damage count (SrcDam). That is the rule
#     the game already applies to every missile hit; the plugin applies it
#     to melee-type trap hits as well.
#   - The trap keeps its own on-hit effects, if it has any.
#
# Every effect keeps its own native rules, exactly as when the owner makes
# the hit or the kill herself:
#   - Chance to cast on kill needs the killing hit to carry weapon damage.
#     A kill by poison, bleed or burning counts.
#   - Procs are cast by the owner, so they start from her position.
#
# Experience, magic find and gold find already go to the owner in the stock
# game. The plugin does not touch them.
#
# WHICH MONSTERS
# monster_ids lists monstats.txt rows (the *hcIdx column). A listed unit is
# credited to its owner only while the owner is a live player. A minion owned
# by another of that player's minions belongs to the same player. Anything
# else, or an owner that is dead or gone, keeps the stock behaviour. Any
# monstats row can be listed, so other summons work the same way.
#
# Vanilla trap rows (check ESR's rows with the console command below):
#   410 wakeofdestruction  Wake of Fire
#   411 chargeboltsentry   Charged Bolt Sentry
#   412 lightningsentry    Lightning Sentry
#   413 bladecreeper       Blade Sentinel
#   415 infernosentry      Wake of Inferno
#   416 deathsentry        Death Sentry
#
# CONSOLE
#   trapcredit   settings, the state of each patched call, counters, and
#                every monstats row that made a kill while owned by a player
#                (listed or not), so you can see which rows your traps use
#
# LOG (<scope>\d2rloader\logs\celestialrayone.trap-owner-credit.log)
#   - At load: the rows read from this file and the state of each part.
#   - A key this plugin does not know, or a key under the wrong [section],
#     is named with its section. Nothing is ignored silently.
#   - The first 8 credited kills and the first 8 credited hits, with the
#     row, the trap and the owner.
#
# HOW IT WORKS
# The game fires kill, domeleedamage and domissiledamage from four calls:
# three in the damage commit (0x44CFC5, 0x44CFE4, 0x44D535) and one in the
# monster life tick for poison, bleed and burning deaths (0x448F36). The
# plugin changes only the target of those four calls. For a listed monster
# with a live player owner it hands the event to the owner; everything else
# goes through unchanged.
#
# Changes take effect the next time the game starts.
#
# ============================================================================

# File format version. Leave this value unchanged.
config_version = 1

# Master switch. false loads without patching anything.
enabled = true


[credit]
# The owner gets the kill instead of the trap.
kills = true

# The owner's on-hit effects also fire on the trap's weapon-damage hits.
on_hit = true


[monsters]
# monstats.txt rows (*hcIdx). Any separators: [410, 411] and 410 411 both
# parse. Up to 256 rows, on one line or across several.
monster_ids = [410, 411, 412, 413, 415, 416]


# Lets D2RLoader verify that multiplayer peers use matching settings.
[d2rl]
match = true
)toml";

struct Config {
    bool enabled{true};
    bool kills{true};
    bool onHit{true};
    std::array<std::uint32_t, MaxMonsterIds> ids{};
    std::size_t idCount{};
};

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

const D2RL::PluginContext* Context{};
std::uintptr_t             Base{};
Config                     Settings{};
std::string                LoadedConfigPath{"embedded defaults"};
std::array<std::uint64_t, (MaxMonsterId + 1) / 64> ListedRows{};

UnitEventFn      NativeUnitEvent{};
DispatchFn       NativeDispatch{};
GetMinionOwnerFn GetMinionOwner{};
IsDeadFn         IsDead{};

void* RelayPage{};
std::array<bool, SiteCount> SitePatched{};

std::atomic_bool Armed{false};
std::atomic_bool KillsArmed{false};
std::atomic_bool HitsArmed{false};

std::atomic<std::uint64_t> CombatKillsCredited{};
std::atomic<std::uint64_t> DotKillsCredited{};
std::atomic<std::uint64_t> MeleeHitsForwarded{};
std::atomic<std::uint64_t> MissileHitsForwarded{};
std::atomic<std::uint64_t> HitsWithoutWeaponDamage{};
std::atomic<std::uint64_t> SkippedNoOwner{};
std::atomic<std::uint64_t> SkippedDeadOwner{};
std::atomic<std::uint64_t> SkippedNotPlayer{};
std::atomic<std::uint64_t> KillLogs{};
std::atomic<std::uint64_t> HitLogs{};

struct SeenRow {
    std::uint32_t row;
    bool          listed;
    std::uint64_t kills;
    std::uint64_t credited;
};
SRWLOCK                        SeenLock = SRWLOCK_INIT;
std::array<SeenRow, MaxSeenRows> SeenRows{};
std::size_t                    SeenCount{};

constexpr D2RL::PluginInfo Info{
    .infoSize    = D2RL::PluginInfoSize,
    .abiVersion  = D2RL_PLUGIN_ABI_VERSION,
    .id          = "celestialrayone.trap-owner-credit",
    .name        = "Trap Owner Credit",
    .version     = "1.0.0",
    .author      = "CelestialRayOne",
    .description = "Credits the kills and weapon-damage hits of listed monsters (assassin traps by default) "
                   "to the player who owns them.",
    .flags       = D2RL::PluginFlags::Server | D2RL::PluginFlags::NativeHooks,
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

template <typename T>
auto At(std::uint64_t rva) noexcept -> T {
    return reinterpret_cast<T>(Base + rva);
}

auto ReadU32(const void* base, std::size_t offset) noexcept -> std::uint32_t {
    std::uint32_t value = 0;
    std::memcpy(&value, static_cast<const std::uint8_t*>(base) + offset, sizeof(value));
    return value;
}

auto ReadPointer(const void* base, std::size_t offset) noexcept -> void* {
    void* value = nullptr;
    std::memcpy(&value, static_cast<const std::uint8_t*>(base) + offset, sizeof(value));
    return value;
}

auto IsListed(std::uint32_t row) noexcept -> bool {
    return row <= MaxMonsterId && (ListedRows[row / 64] & (std::uint64_t{1} << (row % 64))) != 0;
}

// Appends to a fixed buffer and never runs past its end.
template <typename... Args>
void Append(char* buffer, std::size_t size, std::size_t& used, const char* format, Args... args) noexcept {
    if (used >= size) return;
    const int written = std::snprintf(buffer + used, size - used, format, args...);
    if (written <= 0) return;
    used += static_cast<std::size_t>(written);
    if (used >= size) used = size - 1;
}

// ---------------------------------------------------------------------------
// The rule
// ---------------------------------------------------------------------------

enum class OwnerResult : std::uint8_t { Player, NoOwner, DeadOwner, NotPlayer };

// Follows the owner links the game itself uses (D2GAME_GetMinionOwner) until a
// player is reached. Both pointers the lookup asserts on are checked first.
auto ResolvePlayerOwner(void* unit, void*& owner) noexcept -> OwnerResult {
    owner = nullptr;
    void* current = unit;
    for (std::size_t depth = 0; depth < MaxOwnerDepth; ++depth) {
        void* monsterData = ReadPointer(current, MonsterDataOffset);
        if (monsterData == nullptr || ReadPointer(monsterData, AiControlOffset) == nullptr) {
            return OwnerResult::NoOwner;
        }
        void* next = GetMinionOwner(current);
        if (next == nullptr) return OwnerResult::NoOwner;
        const std::uint32_t type = ReadU32(next, UnitTypeOffset);
        if (type == UnitTypePlayer) {
            if (IsDead(next) != 0) return OwnerResult::DeadOwner;
            owner = next;
            return OwnerResult::Player;
        }
        if (type != UnitTypeMonster || next == current) return OwnerResult::NotPlayer;
        current = next;
    }
    return OwnerResult::NotPlayer;
}

void CountSkip(OwnerResult result) noexcept {
    switch (result) {
    case OwnerResult::NoOwner:   SkippedNoOwner.fetch_add(1, std::memory_order_relaxed); break;
    case OwnerResult::DeadOwner: SkippedDeadOwner.fetch_add(1, std::memory_order_relaxed); break;
    case OwnerResult::NotPlayer: SkippedNotPlayer.fetch_add(1, std::memory_order_relaxed); break;
    case OwnerResult::Player:    break;
    }
}

// The owner to credit, or null: the unit must be a monster of a listed row
// with a live player owner.
auto CreditedOwner(void* unit) noexcept -> void* {
    if (unit == nullptr || ReadU32(unit, UnitTypeOffset) != UnitTypeMonster) return nullptr;
    if (!IsListed(ReadU32(unit, UnitClassIdOffset))) return nullptr;
    void* owner = nullptr;
    const OwnerResult result = ResolvePlayerOwner(unit, owner);
    if (result != OwnerResult::Player) {
        CountSkip(result);
        return nullptr;
    }
    return owner;
}

// Records every monster row that made a kill while owned by a player, for
// the console. Kills are rare enough for a lock.
void NoteKiller(std::uint32_t row, bool listed, bool credited) noexcept {
    AcquireSRWLockExclusive(&SeenLock);
    std::size_t index = 0;
    while (index < SeenCount && SeenRows[index].row != row) ++index;
    if (index == SeenCount && SeenCount < SeenRows.size()) {
        SeenRows[SeenCount] = SeenRow{ row, listed, 0, 0 };
        ++SeenCount;
    }
    if (index < SeenCount) {
        ++SeenRows[index].kills;
        if (credited) ++SeenRows[index].credited;
    }
    ReleaseSRWLockExclusive(&SeenLock);
}

// The owner to credit with a kill, or null. Also feeds the console's list of
// owned killers, listed or not.
auto KillOwner(void* killer) noexcept -> void* {
    if (killer == nullptr || ReadU32(killer, UnitTypeOffset) != UnitTypeMonster) return nullptr;
    const std::uint32_t row    = ReadU32(killer, UnitClassIdOffset);
    const bool          listed = IsListed(row);
    void*               owner  = nullptr;
    const OwnerResult   result = ResolvePlayerOwner(killer, owner);
    if (result == OwnerResult::Player) NoteKiller(row, listed, listed);
    if (!listed) return nullptr;
    if (result != OwnerResult::Player) {
        CountSkip(result);
        return nullptr;
    }
    return owner;
}

void LogCredit(std::atomic<std::uint64_t>& logs, const char* what, void* unit, void* owner, void* other) noexcept {
    if (logs.fetch_add(1, std::memory_order_relaxed) >= MaxCreditLogs) return;
    D2RL::LogInfoF(Context, "TrapOwnerCredit: %s: row %u, unit %u -> owner player unit %u (other unit %u).",
        what, ReadU32(unit, UnitClassIdOffset), ReadU32(unit, UnitIdOffset), ReadU32(owner, UnitIdOffset),
        other != nullptr ? ReadU32(other, UnitIdOffset) : 0U);
}

// The three 0x44D570 calls of SUNITDMG_ExecuteEvents land here with the
// call's own arguments.
auto __fastcall HookedUnitEvent(void* game, std::int32_t eventId, void* unit, void* other, void* damage) noexcept
    -> std::int32_t {
    const UnitEventFn native = NativeUnitEvent;
    if (!Armed.load(std::memory_order_acquire)) return native(game, eventId, unit, other, damage);

    if (eventId == EventKill) {
        if (KillsArmed.load(std::memory_order_relaxed)) {
            if (void* owner = KillOwner(unit)) {
                CombatKillsCredited.fetch_add(1, std::memory_order_relaxed);
                LogCredit(KillLogs, "combat kill credited", unit, owner, other);
                return native(game, eventId, owner, other, damage);
            }
        }
        return native(game, eventId, unit, other, damage);
    }

    const std::int32_t result = native(game, eventId, unit, other, damage);
    if ((eventId == EventDoMeleeDamage || eventId == EventDoMissileDamage)
            && HitsArmed.load(std::memory_order_relaxed)) {
        if (void* owner = CreditedOwner(unit)) {
            if (damage == nullptr || (ReadU32(damage, 0) & HitFlagWeaponDamage) == 0) {
                HitsWithoutWeaponDamage.fetch_add(1, std::memory_order_relaxed);
            } else {
                (eventId == EventDoMeleeDamage ? MeleeHitsForwarded : MissileHitsForwarded)
                    .fetch_add(1, std::memory_order_relaxed);
                LogCredit(HitLogs, eventId == EventDoMeleeDamage ? "melee hit forwarded" : "missile hit forwarded",
                    unit, owner, other);
                native(game, eventId, owner, other, damage);
            }
        }
    }
    return result;
}

// The regen tick's 0x5881E0 kill call lands here with the call's own six
// arguments.
auto __fastcall HookedDotKill(void* game, std::int32_t eventId, void* unit, void* other, void* damage,
    void* object) noexcept -> std::int32_t {
    const DispatchFn native = NativeDispatch;
    if (Armed.load(std::memory_order_acquire) && KillsArmed.load(std::memory_order_relaxed)
            && eventId == EventKill) {
        if (void* owner = KillOwner(unit)) {
            DotKillsCredited.fetch_add(1, std::memory_order_relaxed);
            LogCredit(KillLogs, "poison/bleed/burn kill credited", unit, owner, other);
            return native(game, eventId, owner, other, damage, object);
        }
    }
    return native(game, eventId, unit, other, damage, object);
}

// ---------------------------------------------------------------------------
// Configuration parsing
// ---------------------------------------------------------------------------

auto Trim(std::string_view text) noexcept -> std::string_view {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) {
        text.remove_suffix(1);
    }
    return text;
}

auto StripComment(std::string_view line) noexcept -> std::string_view {
    const std::size_t hash = line.find('#');
    return Trim(hash == std::string_view::npos ? line : line.substr(0, hash));
}

auto ParseBool(std::string_view value, bool& out) noexcept -> bool {
    if (value == "true") { out = true; return true; }
    if (value == "false") { out = false; return true; }
    return false;
}

void ParseIds(std::string_view value) {
    Settings.idCount = 0;
    ListedRows.fill(0);
    std::size_t i = 0;
    while (i < value.size()) {
        const char c = value[i];
        if (c >= '0' && c <= '9') {
            std::uint64_t number = 0;
            while (i < value.size() && value[i] >= '0' && value[i] <= '9') {
                number = number * 10 + static_cast<std::uint64_t>(value[i] - '0');
                if (number > 0xFFFFFFFFULL) number = 0xFFFFFFFFULL;
                ++i;
            }
            if (number > MaxMonsterId) {
                D2RL::LogWarnF(Context, "TrapOwnerCredit: [monsters] monster_ids: %llu is not a monstats row "
                    "(0 to %u); skipped.", static_cast<unsigned long long>(number), MaxMonsterId);
                continue;
            }
            const auto row = static_cast<std::uint32_t>(number);
            if (IsListed(row)) continue;
            if (Settings.idCount >= Settings.ids.size()) {
                D2RL::LogWarnF(Context, "TrapOwnerCredit: [monsters] monster_ids holds more than %zu rows; "
                    "row %u and later are skipped.", Settings.ids.size(), row);
                return;
            }
            Settings.ids[Settings.idCount++] = row;
            ListedRows[row / 64] |= std::uint64_t{1} << (row % 64);
            continue;
        }
        if (c == '-') {
            D2RL::LogWarn(Context, "TrapOwnerCredit: [monsters] monster_ids: negative rows do not exist; skipped.");
            ++i;
            while (i < value.size() && value[i] >= '0' && value[i] <= '9') ++i;
            continue;
        }
        if (c != '[' && c != ']' && c != ',' && c != ' ' && c != '\t' && c != '\r' && c != '\n') {
            D2RL::LogWarnF(Context, "TrapOwnerCredit: [monsters] monster_ids: unexpected character '%c'; "
                "only numbers are read.", c);
        }
        ++i;
    }
}

void ApplySetting(std::string_view section, std::string_view key, std::string_view value) {
    const std::string sectionText(section);
    const std::string keyText(key);
    if (section.empty()) {
        if (key == "config_version") return;
        if (key == "enabled") {
            if (!ParseBool(value, Settings.enabled)) {
                D2RL::LogWarnF(Context, "TrapOwnerCredit: enabled must be true or false; kept %s.",
                    Settings.enabled ? "true" : "false");
            }
            return;
        }
    } else if (section == "credit") {
        bool* target = key == "kills" ? &Settings.kills : key == "on_hit" ? &Settings.onHit : nullptr;
        if (target != nullptr) {
            if (!ParseBool(value, *target)) {
                D2RL::LogWarnF(Context, "TrapOwnerCredit: [credit] %s must be true or false; kept %s.",
                    keyText.c_str(), *target ? "true" : "false");
            }
            return;
        }
    } else if (section == "monsters") {
        if (key == "monster_ids") {
            ParseIds(value);
            return;
        }
    } else if (section == "d2rl") {
        if (key == "match") return;
    }
    D2RL::LogWarnF(Context, "TrapOwnerCredit: unknown key '%s' under [%s]; ignored.", keyText.c_str(),
        section.empty() ? "top level" : sectionText.c_str());
}

void ParseConfig(std::string_view text) {
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF
            && static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF) {
        text.remove_prefix(3);
    }
    ListedRows.fill(0);
    bool sawIds = false;
    std::string section;
    std::size_t position = 0;
    auto nextLine = [&]() -> std::string_view {
        const std::size_t end  = text.find('\n', position);
        const std::size_t stop = end == std::string_view::npos ? text.size() : end;
        const std::string_view line = text.substr(position, stop - position);
        position = end == std::string_view::npos ? text.size() : end + 1;
        return line;
    };
    while (position < text.size()) {
        std::string_view line = StripComment(nextLine());
        if (line.empty()) continue;
        if (line.front() == '[') {
            const std::size_t close = line.find(']');
            section = std::string(Trim(close == std::string_view::npos ? line.substr(1) : line.substr(1, close - 1)));
            continue;
        }
        const std::size_t equals = line.find('=');
        if (equals == std::string_view::npos) {
            const std::string bad(line);
            D2RL::LogWarnF(Context, "TrapOwnerCredit: line '%s' is not key = value; ignored.", bad.c_str());
            continue;
        }
        const std::string_view key = Trim(line.substr(0, equals));
        std::string value(Trim(line.substr(equals + 1)));
        if (!value.empty() && value.front() == '[') {
            int depth = 0;
            for (const char c : value) depth += c == '[' ? 1 : c == ']' ? -1 : 0;
            while (depth > 0 && position < text.size()) {
                const std::string_view more = StripComment(nextLine());
                value.push_back(' ');
                value.append(more);
                for (const char c : more) depth += c == '[' ? 1 : c == ']' ? -1 : 0;
            }
        }
        if (section == "monsters" && key == "monster_ids") sawIds = true;
        ApplySetting(section, key, value);
    }
    if (!sawIds) {
        D2RL::LogWarn(Context, "TrapOwnerCredit: [monsters] monster_ids is missing; no monster is credited.");
    }
}

auto NarrowPath(const wchar_t* wide) -> std::string {
    if (wide == nullptr || wide[0] == L'\0') return {};
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (bytes <= 1) return {};
    std::string out(static_cast<std::size_t>(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, out.data(), bytes, nullptr, nullptr);
    out.resize(static_cast<std::size_t>(bytes - 1));
    return out;
}

auto ConfigPathForDisplay() -> std::string {
    constexpr std::size_t needed = offsetof(D2RL::PluginContext, pluginConfigPath) + sizeof(const wchar_t*);
    if (Context->contextSize < needed) return {};
    return NarrowPath(Context->pluginConfigPath);
}

auto ReadConfigText(std::string& text) -> bool {
    if (!Context->EnsureConfig(DefaultConfigToml)) {
        D2RL::LogWarn(Context, "TrapOwnerCredit: the TOML could not be created; embedded defaults are in use.");
        return false;
    }
    text.assign(ConfigReadBufferBytes, '\0');
    std::uint32_t required = 0;
    bool read = Context->ReadConfig(text.data(), static_cast<std::uint32_t>(text.size()), &required);
    if (!read && required > text.size()) {
        text.assign(required, '\0');
        read = Context->ReadConfig(text.data(), required, &required);
    }
    if (!read) {
        D2RL::LogWarn(Context, "TrapOwnerCredit: the TOML could not be read; embedded defaults are in use.");
        return false;
    }
    text.resize(std::strlen(text.c_str()));
    return true;
}

void LoadConfig() noexcept {
    Settings = Config{};
    ListedRows.fill(0);
    try {
        LoadedConfigPath = "embedded defaults";
        std::string text;
        if (ReadConfigText(text)) {
            const std::string path = ConfigPathForDisplay();
            LoadedConfigPath = path.empty() ? std::string{"<plugin config>"} : path;
        } else {
            text = DefaultConfigToml;
        }
        ParseConfig(text);
    } catch (...) {
        Settings = Config{};
        Settings.enabled = false;
        ListedRows.fill(0);
        D2RL::LogError(Context, "TrapOwnerCredit: the TOML could not be loaded; nothing is credited.");
    }
}

// ---------------------------------------------------------------------------
// Relay page and the four calls
// ---------------------------------------------------------------------------

constexpr std::size_t RelayBytes     = 0x1000;
constexpr std::size_t RelaySlotBytes = 16;

auto CanEncodeRel32(std::uintptr_t from, std::uintptr_t to) noexcept -> bool {
    const std::int64_t delta = static_cast<std::int64_t>(to) - static_cast<std::int64_t>(from) - CallSize;
    return delta >= INT32_MIN && delta <= INT32_MAX;
}

auto AllocateNear(std::uintptr_t low, std::uintptr_t high, std::size_t size) noexcept -> void* {
    SYSTEM_INFO systemInfo{};
    GetSystemInfo(&systemInfo);
    const auto granularity = static_cast<std::uintptr_t>(systemInfo.dwAllocationGranularity);
    if (granularity == 0) return nullptr;
    const auto aligned = high & ~(granularity - 1U);
    for (std::uintptr_t delta = granularity; delta < 0x7000'0000ULL; delta += granularity) {
        const std::uintptr_t candidates[2]{ aligned + delta, aligned > delta ? aligned - delta : 0 };
        for (const std::uintptr_t candidate : candidates) {
            if (candidate == 0) continue;
            if (!CanEncodeRel32(low, candidate) || !CanEncodeRel32(high, candidate)
                    || !CanEncodeRel32(low, candidate + size) || !CanEncodeRel32(high, candidate + size)) {
                continue;
            }
            if (void* memory = VirtualAlloc(reinterpret_cast<void*>(candidate), size, MEM_COMMIT | MEM_RESERVE,
                    PAGE_READWRITE)) {
                return memory;
            }
        }
    }
    return nullptr;
}

// +00 FF 25 02 00 00 00   jmp qword [rip+2]
// +06 CC CC
// +08 dq target            8-byte aligned, so it can be swapped atomically
constexpr std::size_t RelayTargetOffset = 8;

void WriteRelayJump(std::uint8_t* at, void* target) noexcept {
    const std::uint8_t jump[8]{ 0xFF, 0x25, 0x02, 0x00, 0x00, 0x00, 0xCC, 0xCC };
    std::memcpy(at, jump, sizeof(jump));
    const auto absolute = reinterpret_cast<std::uint64_t>(target);
    std::memcpy(at + RelayTargetOffset, &absolute, sizeof(absolute));
}

auto RelaySlotAddress(std::size_t slot) noexcept -> std::uintptr_t {
    return reinterpret_cast<std::uintptr_t>(RelayPage) + slot * RelaySlotBytes;
}

auto BuildRelayPage() noexcept -> bool {
    if (RelayPage != nullptr) return true;
    std::uintptr_t low = UINTPTR_MAX;
    std::uintptr_t high = 0;
    for (const CallSite& site : Sites) {
        low  = std::min<std::uintptr_t>(low, Base + site.rva);
        high = std::max<std::uintptr_t>(high, Base + site.rva);
    }
    void* page = AllocateNear(low, high, RelayBytes);
    if (page == nullptr) return false;
    auto* code = static_cast<std::uint8_t*>(page);
    std::memset(code, 0xCC, RelayBytes);
    WriteRelayJump(code + UnitEventRelaySlot * RelaySlotBytes, reinterpret_cast<void*>(&HookedUnitEvent));
    WriteRelayJump(code + DispatchRelaySlot * RelaySlotBytes, reinterpret_cast<void*>(&HookedDotKill));
    DWORD previous = 0;
    if (!VirtualProtect(code, RelayBytes, PAGE_EXECUTE_READ, &previous)) {
        VirtualFree(page, 0, MEM_RELEASE);
        return false;
    }
    FlushInstructionCache(GetCurrentProcess(), code, 2 * RelaySlotBytes);
    RelayPage = page;
    return true;
}

// On unload both relay jumps are pointed straight at the native callees, so
// the four calls behave exactly like stock whether D2RLoader has restored them
// yet or not, and nothing can jump into the unloaded DLL.
void RetargetRelayToNative() noexcept {
    if (RelayPage == nullptr) return;
    auto* code = static_cast<std::uint8_t*>(RelayPage);
    DWORD previous = 0;
    if (!VirtualProtect(code, RelayBytes, PAGE_EXECUTE_READWRITE, &previous)) return;
    InterlockedExchange64(reinterpret_cast<volatile LONG64*>(code + UnitEventRelaySlot * RelaySlotBytes
        + RelayTargetOffset), static_cast<LONG64>(Base + UnitEventRva));
    InterlockedExchange64(reinterpret_cast<volatile LONG64*>(code + DispatchRelaySlot * RelaySlotBytes
        + RelayTargetOffset), static_cast<LONG64>(Base + DispatchRva));
    DWORD ignored = 0;
    VirtualProtect(code, RelayBytes, PAGE_EXECUTE_READ, &ignored);
    FlushInstructionCache(GetCurrentProcess(), code, 2 * RelaySlotBytes);
}

auto EncodeRelayCall(const CallSite& site, std::uint8_t (&call)[CallSize]) noexcept -> bool {
    const std::uintptr_t from = Base + site.rva;
    const std::uintptr_t to   = RelaySlotAddress(site.relaySlot);
    if (!CanEncodeRel32(from, to)) return false;
    const auto displacement = static_cast<std::int32_t>(static_cast<std::int64_t>(to)
        - static_cast<std::int64_t>(from + CallSize));
    call[0] = 0xE8;
    std::memcpy(call + 1, &displacement, sizeof(displacement));
    return true;
}

auto PatchSite(SiteIndex index) noexcept -> bool {
    const CallSite& site = Sites[index];
    std::uint8_t call[CallSize]{};
    if (!EncodeRelayCall(site, call)) {
        D2RL::LogErrorF(Context, "TrapOwnerCredit: the relay page is out of rel32 reach of the %s call at 0x%llX.",
            site.name, static_cast<unsigned long long>(site.rva));
        return false;
    }
    const std::uint8_t* expected = site.window + (site.rva - site.windowRva);
    if (!Context->PatchBytes(site.rva, expected, CallSize, call, CallSize)) {
        D2RL::LogErrorF(Context, "TrapOwnerCredit: the %s call at 0x%llX could not be retargeted; it is owned by "
            "another plugin or no longer matches.", site.name, static_cast<unsigned long long>(site.rva));
        return false;
    }
    SitePatched[index] = true;
    return true;
}

auto SiteState(SiteIndex index) noexcept -> const char* {
    const CallSite& site = Sites[index];
    const auto* live = reinterpret_cast<const std::uint8_t*>(Base + site.rva);
    const std::uint8_t* stock = site.window + (site.rva - site.windowRva);
    if (std::memcmp(live, stock, CallSize) == 0) return "STOCK";
    if (live[0] == 0xE8 && RelayPage != nullptr) {
        std::int32_t displacement = 0;
        std::memcpy(&displacement, live + 1, sizeof(displacement));
        const auto target = static_cast<std::uintptr_t>(
            static_cast<std::int64_t>(Base + site.rva + CallSize) + displacement);
        if (target == RelaySlotAddress(site.relaySlot)) return "LIVE";
    }
    return "FOREIGN";
}

// ---------------------------------------------------------------------------
// Verification
// ---------------------------------------------------------------------------

auto Verify(std::uint64_t rva, const std::uint8_t* bytes, std::uint32_t size, const char* what) noexcept -> bool {
    if (Context->CheckExpectedBytes(rva, bytes, size)) return true;
    D2RL::LogErrorF(Context, "TrapOwnerCredit: %s at 0x%llX does not match D2RLoader 1.3.1 (wrong game build, "
        "or another plugin patched it).", what, static_cast<unsigned long long>(rva));
    return false;
}

auto VerifyCommon() noexcept -> bool {
    bool ok = true;
    ok = Verify(GetMinionOwnerRva, OwnerWindow, ByteCount(OwnerWindow), "D2GAME_GetMinionOwner") && ok;
    ok = Verify(IsDeadRva, DeadWindow, ByteCount(DeadWindow), "SUNIT_IsDead") && ok;
    ok = Verify(GetUnitTypeRva, UnitTypeWindow, ByteCount(UnitTypeWindow), "UNITS_GetUnitType") && ok;
    ok = Verify(GetClassIdRva, ClassIdWindow, ByteCount(ClassIdWindow), "UNITS_GetClassId") && ok;
    return ok;
}

// ---------------------------------------------------------------------------
// Console
// ---------------------------------------------------------------------------

auto Count(const std::atomic<std::uint64_t>& counter) noexcept -> unsigned long long {
    return static_cast<unsigned long long>(counter.load(std::memory_order_relaxed));
}

auto Status(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command, void*) noexcept
    -> D2RL::ConsoleCommandResult {
    if (command == nullptr || command->plugin == nullptr) return D2RL::ConsoleCommandResult::Failed;
    const D2RL::PluginContext* plugin = command->plugin;

    char line[1024]{};
    std::size_t used = 0;
    Append(line, sizeof(line), used, "Trap Owner Credit 1.0.0: %s, kills %s, on hit %s; %zu monster rows:",
        Settings.enabled ? "enabled" : "disabled", Settings.kills ? "on" : "off", Settings.onHit ? "on" : "off",
        Settings.idCount);
    for (std::size_t i = 0; i < Settings.idCount; ++i) Append(line, sizeof(line), used, " %u", Settings.ids[i]);
    if (Settings.idCount == 0) Append(line, sizeof(line), used, "%s", " none");
    plugin->WriteConsoleMessage(line);

    for (std::size_t i = 0; i < SiteCount; ++i) {
        used = 0;
        Append(line, sizeof(line), used, "  %-36s 0x%06llX  %s", Sites[i].name,
            static_cast<unsigned long long>(Sites[i].rva), SiteState(static_cast<SiteIndex>(i)));
        plugin->WriteConsoleMessage(line);
    }

    used = 0;
    Append(line, sizeof(line), used, "  kills credited: %llu combat, %llu poison/bleed/burn; hits forwarded: "
        "%llu melee, %llu missile; listed hits without weapon damage: %llu", Count(CombatKillsCredited),
        Count(DotKillsCredited), Count(MeleeHitsForwarded), Count(MissileHitsForwarded),
        Count(HitsWithoutWeaponDamage));
    plugin->WriteConsoleMessage(line);

    used = 0;
    Append(line, sizeof(line), used, "  not credited: %llu no live owner, %llu owner dead, %llu owner not a player",
        Count(SkippedNoOwner), Count(SkippedDeadOwner), Count(SkippedNotPlayer));
    plugin->WriteConsoleMessage(line);

    used = 0;
    Append(line, sizeof(line), used, "%s", "  monsters that killed while owned by a player:");
    AcquireSRWLockShared(&SeenLock);
    for (std::size_t i = 0; i < SeenCount; ++i) {
        Append(line, sizeof(line), used, " | row %u%s: %llu kills, %llu credited", SeenRows[i].row,
            SeenRows[i].listed ? " (listed)" : " (not listed)", static_cast<unsigned long long>(SeenRows[i].kills),
            static_cast<unsigned long long>(SeenRows[i].credited));
    }
    const bool none = SeenCount == 0;
    ReleaseSRWLockShared(&SeenLock);
    if (none) Append(line, sizeof(line), used, "%s", " none yet");
    plugin->WriteConsoleMessage(line);

    used = 0;
    Append(line, sizeof(line), used, "  config=%s", LoadedConfigPath.c_str());
    plugin->WriteConsoleMessage(line);
    return D2RL::ConsoleCommandResult::Handled;
}

}  // namespace
}  // namespace CelestialRayOne::TrapOwnerCredit

namespace TOC = CelestialRayOne::TrapOwnerCredit;

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* {
    return &TOC::Info;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
    using namespace TOC;
    if (!D2RL::HasContext(context) || context->abiVersion != D2RL_PLUGIN_ABI_VERSION) return false;
    Context = context;
    Base    = static_cast<std::uintptr_t>(context->exeBase);
    if (Base == 0) return false;

    LoadConfig();

    if (!context->RegisterConsoleCommand("trapcredit", Status,
            "Show Trap Owner Credit settings, patch state and counters.")) {
        D2RL::LogWarn(context, "TrapOwnerCredit: the trapcredit console command was not registered.");
    }

    if (!Settings.enabled) {
        D2RL::LogInfo(context, "TrapOwnerCredit: loaded disabled; nothing was patched.");
        return true;
    }
    if (Settings.idCount == 0 || (!Settings.kills && !Settings.onHit)) {
        D2RL::LogInfo(context, "TrapOwnerCredit: no monster rows or both credits off; nothing was patched.");
        return true;
    }

    if (!VerifyCommon()) {
        D2RL::LogError(context, "TrapOwnerCredit: refused; the owner lookup could not be verified.");
        return false;
    }
    NativeUnitEvent = At<UnitEventFn>(UnitEventRva);
    NativeDispatch  = At<DispatchFn>(DispatchRva);
    GetMinionOwner  = At<GetMinionOwnerFn>(GetMinionOwnerRva);
    IsDead          = At<IsDeadFn>(IsDeadRva);

    const bool wrapperOk = Verify(UnitEventRva, UnitEventWindow, ByteCount(UnitEventWindow),
        "D2GAME_ApplyUnitStatEvent");
    const bool killOk = Settings.kills && wrapperOk
        && Verify(KillWindowRva, KillWindow, ByteCount(KillWindow), "the combat kill event");
    const bool dotOk = Settings.kills && wrapperOk
        && Verify(DotKillWindowRva, DotKillWindow, ByteCount(DotKillWindow), "the poison/bleed/burn kill event");
    const bool hitOk = Settings.onHit && wrapperOk
        && Verify(OnHitWindowRva, OnHitWindow, ByteCount(OnHitWindow), "the on-hit events");

    if (!killOk && !dotOk && !hitOk) {
        D2RL::LogError(context, "TrapOwnerCredit: refused; none of the event calls could be verified.");
        return false;
    }
    if (!BuildRelayPage()) {
        D2RL::LogError(context, "TrapOwnerCredit: refused; no relay page could be placed within rel32 reach.");
        return false;
    }

    // Armed before the calls change: a relayed call can arrive the instant
    // its bytes are written.
    KillsArmed.store(killOk || dotOk, std::memory_order_relaxed);
    HitsArmed.store(hitOk, std::memory_order_relaxed);
    Armed.store(true, std::memory_order_release);

    bool combatKill = false;
    bool dotKill    = false;
    bool meleeHit   = false;
    bool missileHit = false;
    if (killOk) combatKill = PatchSite(CombatKillSite);
    if (dotOk) dotKill = PatchSite(DotKillSite);
    if (hitOk) {
        meleeHit   = PatchSite(MeleeHitSite);
        missileHit = PatchSite(MissileHitSite);
    }
    if (!combatKill && !dotKill && !meleeHit && !missileHit) {
        Armed.store(false, std::memory_order_release);
        D2RL::LogError(context, "TrapOwnerCredit: refused; no event call could be retargeted.");
        return false;
    }

    char rows[1024]{};
    std::size_t used = 0;
    Append(rows, sizeof(rows), used, "TrapOwnerCredit: %zu monster rows read:", Settings.idCount);
    for (std::size_t i = 0; i < Settings.idCount; ++i) Append(rows, sizeof(rows), used, " %u", Settings.ids[i]);
    D2RL::LogInfo(context, rows);

    D2RL::LogInfoF(context, "Trap Owner Credit 1.0.0 active: combat kills %s, poison/bleed/burn kills %s, "
        "melee hits %s, missile hits %s; config=%s.",
        combatKill ? "credited" : (Settings.kills ? "FAILED" : "off"),
        dotKill ? "credited" : (Settings.kills ? "FAILED" : "off"),
        meleeHit ? "forwarded" : (Settings.onHit ? "FAILED" : "off"),
        missileHit ? "forwarded" : (Settings.onHit ? "FAILED" : "off"),
        LoadedConfigPath.c_str());
    return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
    // D2RLoader restores the four calls. Until then the relay jumps straight to
    // the native callees, and a thread already inside a hook passes its event
    // through unchanged. The relay page stays mapped: a thread may be in it.
    TOC::Armed.store(false, std::memory_order_release);
    TOC::RetargetRelayToNative();
}
