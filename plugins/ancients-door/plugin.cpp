// Summit Door Guardians
//
// The Arreat Summit's way-back door (objects.txt SummitDoor, row 564, InitFn
// 76, OperateFn 71) only opens once the campaign Ancients are dead. A custom
// level built on the same map preset keeps that door, and with it the
// campaign's condition, so its door stays shut forever. For every level
// listed in celestialrayone.summit-door-guardians.toml the door follows that
// level's own monsters instead: it opens once every listed monster has died
// in that level. Every other level, the real Arreat Summit included, runs the
// game's own code unchanged.
//
// Built against D2R 3.3.93847 in the D2RLoader 1.3.1 process image (base
// 0x140000000), PluginSDK 0.3.0. Everything below was read out of the live
// image and disassembled before being relied on.
//
// ===========================================================================
// The door in the game
// ===========================================================================
//   0x549C70  InitFn 76, the SummitDoor init. Slot 76 of the InitFn table
//             0x239B980 (81 slots). The object init handler 0x510BA0 calls
//             it from 510CB1..510D05: it reads the InitFn byte at objects.txt
//             record +15Ch, loads the table (lea at 510CBF) and passes
//               +00 game  +08 object  +10 room  +18 object control
//               +20 objects.txt record  +28 x  +2C y
//             (its prologue keeps game in rbp, object in rdi, room in r14).
//   0x549CF0  OperateFn 71, the SummitDoor operate. Slot 71 of the OperateFn
//             table 0x23A51C0 (75 slots), called by the operate handler
//             0x593630 with
//               +00 game  +08 object  +10 player
//
//   Both read the Rite of Passage quest record, QUESTS_GetQuestData(game, 35)
//   at 0x518220, which exists once per game:
//     +20 byte   the Ancients are dead      +68 byte   the Ancients were woken
//     +78 byte   the door was seen          +7C dword  the door's unit id
//     +80 dword  the door mode to restore
//
//   Object modes: 0 neutral = the way is open, 1 operating = the door
//   closing, 2 opened = the door is shut.
//
//   Init, line for line D2MOO's OBJECTS_InitFunction76_SummitDoor:
//     mark the door seen and remember its unit id (0x34A330), set the object
//     to the stored mode (UNITS_ChangeAnimMode 0x34E5F0); then, unless the
//     Ancients are dead or the stored mode already is 2, store 2 and play
//     mode 1. So the door shuts whenever the level loads before the kill.
//   Operate, line for line OBJECTS_OperateFunction71_SummitDoor, mode read
//   through UNITS_GetUnitMode 0x34AB60:
//     mode above 2  return 0.
//     mode 0        alive: play mode 1, store 2. Dead: warp the player
//                   through the door (0x5A11A0). Return 1.
//     mode 2        alive: sound 20 for the player (0x491960). Dead and
//                   woken: mode 0, store 0. Then as mode 1.
//     mode 1        dead: store 0, mode 0. Alive: sound 20, mode 2.
//                   Return 1.
//   The "dead" byte is set by the quest's monster-killed callback 0x54EB00
//   only, when the last of the ancientbarb1-3 (monstats 540-542) that the
//   altar spawned dies. A custom level's monsters can never set it, and if
//   they could, it is the campaign's flag for the whole game. Even in the
//   campaign the door does not move by itself when the Ancients die: it
//   opens on the next click.
//
// ===========================================================================
// What this plugin does
// ===========================================================================
//   Slots 76 and 71 are swapped (compare-exchange, restored on unload) for
//   InitHook and OperateHook. Each finds the door's level through
//   DUNGEON_GetLevelIdFromRoom 0x2EFC10: the init from the room the init
//   handler passes, the operate from UNITS_GetRoom 0x34B440 on the object,
//   the same call the operate handler makes before it calls the slot. A
//   level that is not listed goes to the original function unchanged.
//
//   For a listed level the hook runs the same steps as above, with the
//   level's own state in place of the quest record: "dead" becomes "every
//   listed monster has died in this level in this game", and the stored
//   mode and the unit id are kept by the plugin. The quest record is never
//   read or written for these doors. That also ends a vanilla clash: the
//   record holds one door per game, so the custom level's door used to
//   overwrite the campaign summit's door there. The "woken" test has no
//   counterpart in a custom level and is taken as not woken; its branch ends
//   in the same state as the code after it.
//
//   Kills come from D2RLoader's monster-death listener (level id, monstats
//   id, game id). It runs on the server thread inside the game's own death
//   routine 0x444F50. The death-mode slot 0x1D1A200 is left alone because
//   boss-set-drops owns it. When the last listed monster dies the door is
//   opened at once if it is loaded: SUNIT_GetServerUnit 0x48FE80 by the unit
//   id the hooks saw, then mode 0 and store 0. A door that is not loaded at
//   that moment is open when its room loads, through the init above.
//
//   Games are told apart by the word at game+08h, the id D2RCore puts in the
//   death event (found for boss-set-drops). Progress is cleared on every game
//   join and leave, and also when a door shows up with a different game
//   pointer under an id already in use.
//
// ===========================================================================
// The door's label
// ===========================================================================
//   0x0C0420  the client hover label for the unit under the cursor. For an
//             object it calls 0x0C17E0 (out buffer of 1024 bytes, unit), its
//             only caller (0C0916). For a warp tile, unit type 5, the block
//             0C070C..0C07D4 does: room = UNITS_GetRoom(tile), level =
//             0x2EFE50(data context, room, tile class), the destination of the
//             tile's room link; record = DATATBLS_GetLevelsTxtRecord 0x32C4A0;
//             text = the string key at record +125h, levels.txt LevelWarp
//             (+FDh is LevelName), through the call at 0C07BB. So every level
//             exit says "To The <level>" from the level it leads to.
//   0x0C17E0  the object label. objects.txt record via 0x38FD00 (data context
//             0x34A0E0); SubClass at +127h picks portal, waypoint or plain.
//             A plain object, the SummitDoor included, shows its objects.txt
//             Name. Row 564's Name is "Glacial Caves Level 1", the LevelName
//             key of level 118, which is why every SummitDoor says "The
//             Ancients' Way".
//   0x5A11A0  the door's warp: for every type 5 unit in the door's room
//             (0x2EFD90 first unit, 0x34B4A0 next, 0x34B9D0 type) it warps the
//             player through that tile (0x491FF0).
//
//   0x0C17E0 gets an inline hook. After the game wrote its label, a door
//   object (InitFn 76 and OperateFn 71) in a listed level gets the label of
//   the warp tile it sends you through: the first type 5 unit in its room,
//   the same walk 0x5A11A0 makes, resolved with the same calls and the same
//   string resolver as the warp tile label above. The door then says what
//   the level's other exits say, "To The Icecrown Citadel" for The Edge of
//   Eternity. A door with no warp tile in its room keeps the game's text.
//
// ---------------------------------------------------------------------------
//   Exact bytes of the live image, compared through CheckExpectedBytes:
//   - 549C70..549CEB and 549CF0..549E0F, the two door functions. Every game
//     function the hooks call is read out of these bytes, from the calls the
//     door makes itself;
//   - 510BA0..510BC9 and 510CB1..510D05 in the init handler;
//   - the entries of 0x34B440, 0x2EFC10 and 0x48FE80;
//   - slot 76 must hold 0x549C70 and slot 71 must hold 0x549CF0;
//   - for the label: 0C17E0..0C1876, 0C070C..0C07BB (plus the E8 at 0C07BB,
//     whose target is read live: the loader routes it) and 5A11A0..5A11F2.
//   Any failure leaves the game untouched and says why in the plugin log. The
//   label is checked and installed on its own: if it fails, the door still
//   works and only its label stays the game's.

#include <D2RLPlugin/api.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace CelestialRayOne::SummitDoorGuardians {
namespace {

// ---------------------------------------------------------------------------
// Native anchors (RVAs into the D2RLoader process image)
// ---------------------------------------------------------------------------

constexpr std::uint64_t InitTableRva        = 0x239B980;
constexpr std::size_t   InitSlot            = 76;
constexpr std::uint64_t OperateTableRva     = 0x23A51C0;
constexpr std::size_t   OperateSlot         = 71;

constexpr std::uint64_t DoorInitRva         = 0x549C70;   // InitFn 76
constexpr std::uint64_t DoorOperateRva      = 0x549CF0;   // OperateFn 71
constexpr std::uint64_t InitHandlerRva      = 0x510BA0;
constexpr std::uint64_t InitHandlerCallRva  = 0x510CB1;
constexpr std::uint64_t UnitRoomRva         = 0x34B440;   // UNITS_GetRoom
constexpr std::uint64_t RoomLevelRva        = 0x2EFC10;   // DUNGEON_GetLevelIdFromRoom
constexpr std::uint64_t ServerUnitRva       = 0x48FE80;   // SUNIT_GetServerUnit

// The calls inside the door functions that the hooks reuse.
constexpr std::uint64_t UnitIdCallRva       = 0x549CA2;   // -> 0x34A330, the unit id
constexpr std::uint64_t AnimModeCallRva     = 0x549CB4;   // -> 0x34E5F0, UNITS_ChangeAnimMode
constexpr std::uint64_t UnitModeCallRva     = 0x549CFD;   // -> 0x34AB60, UNITS_GetUnitMode
constexpr std::uint64_t SoundCallRva        = 0x549D53;   // -> 0x491960, the refusal sound
constexpr std::uint64_t WarpCallRva         = 0x549E00;   // -> 0x5A11A0, D2GAME_WarpPlayerToTile

constexpr std::uint64_t UnitIdRva           = 0x34A330;
constexpr std::uint64_t AnimModeRva         = 0x34E5F0;
constexpr std::uint64_t UnitModeRva         = 0x34AB60;
constexpr std::uint64_t SoundRva            = 0x491960;
constexpr std::uint64_t WarpRva             = 0x5A11A0;

// 549C70..549CEB, InitFn 76: QUESTS_GetQuestData(game, 23h), seen = 1,
// unit id, ChangeAnimMode(stored), then the "not dead and not 2" close.
constexpr std::uint8_t DoorInitWitness[]{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B,
    0xF9, 0xBA, 0x23, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x09, 0xE8, 0x96, 0xE5,
    0xFC, 0xFF, 0x41, 0xB8, 0x54, 0x05, 0x00, 0x00, 0x48, 0x8D, 0x15, 0xB9,
    0x3B, 0x7F, 0x01, 0x48, 0x8B, 0xD8, 0xC6, 0x40, 0x78, 0x01, 0x48, 0x8B,
    0x4F, 0x08, 0xE8, 0x89, 0x06, 0xE0, 0xFF, 0x8B, 0x93, 0x80, 0x00, 0x00,
    0x00, 0x89, 0x43, 0x7C, 0x48, 0x8B, 0x4F, 0x08, 0xE8, 0x37, 0x49, 0xE0,
    0xFF, 0x80, 0x7B, 0x20, 0x00, 0x75, 0x21, 0x83, 0xBB, 0x80, 0x00, 0x00,
    0x00, 0x02, 0x74, 0x18, 0xC7, 0x83, 0x80, 0x00, 0x00, 0x00, 0x02, 0x00,
    0x00, 0x00, 0xBA, 0x01, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x4F, 0x08, 0xE8,
    0x10, 0x49, 0xE0, 0xFF, 0x48, 0x8B, 0x5C, 0x24, 0x30, 0x48, 0x83, 0xC4,
    0x20, 0x5F, 0xC3,
};

// 549CF0..549E0F, OperateFn 71, every branch described above.
constexpr std::uint8_t DoorOperateWitness[]{
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0x48, 0x8B, 0x49,
    0x08, 0xE8, 0x5E, 0x0E, 0xE0, 0xFF, 0x83, 0xF8, 0x02, 0x76, 0x08, 0x33,
    0xC0, 0x48, 0x83, 0xC4, 0x20, 0x5B, 0xC3, 0x48, 0x8B, 0x0B, 0xBA, 0x23,
    0x00, 0x00, 0x00, 0x48, 0x89, 0x7C, 0x24, 0x30, 0xE8, 0xFF, 0xE4, 0xFC,
    0xFF, 0x48, 0x8B, 0x4B, 0x08, 0x48, 0x8B, 0xF8, 0xE8, 0x33, 0x0E, 0xE0,
    0xFF, 0x85, 0xC0, 0x0F, 0x84, 0x9A, 0x00, 0x00, 0x00, 0x83, 0xE8, 0x01,
    0x74, 0x3B, 0x83, 0xF8, 0x01, 0x0F, 0x85, 0xC2, 0x00, 0x00, 0x00, 0x80,
    0x7F, 0x20, 0x00, 0x75, 0x11, 0x48, 0x8B, 0x4B, 0x10, 0x8D, 0x50, 0x13,
    0x4C, 0x8B, 0xC1, 0xE8, 0x08, 0x7C, 0xF4, 0xFF, 0xEB, 0x1B, 0x80, 0x7F,
    0x68, 0x00, 0x74, 0x15, 0x48, 0x8B, 0x4B, 0x08, 0x33, 0xD2, 0xE8, 0x85,
    0x48, 0xE0, 0xFF, 0xC7, 0x87, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x80, 0x7F, 0x20, 0x00, 0x75, 0x2F, 0x48, 0x8B, 0x4B, 0x10, 0xBA,
    0x14, 0x00, 0x00, 0x00, 0x4C, 0x8B, 0xC1, 0xE8, 0xD4, 0x7B, 0xF4, 0xFF,
    0x48, 0x8B, 0x4B, 0x08, 0xBA, 0x02, 0x00, 0x00, 0x00, 0xE8, 0x56, 0x48,
    0xE0, 0xFF, 0x48, 0x8B, 0x7C, 0x24, 0x30, 0xB8, 0x01, 0x00, 0x00, 0x00,
    0x48, 0x83, 0xC4, 0x20, 0x5B, 0xC3, 0xC7, 0x87, 0x80, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x33, 0xD2, 0x48, 0x8B, 0x4B, 0x08, 0xE8, 0x31,
    0x48, 0xE0, 0xFF, 0x48, 0x8B, 0x7C, 0x24, 0x30, 0xB8, 0x01, 0x00, 0x00,
    0x00, 0x48, 0x83, 0xC4, 0x20, 0x5B, 0xC3, 0x80, 0x7F, 0x20, 0x00, 0x75,
    0x28, 0x48, 0x8B, 0x4B, 0x08, 0xBA, 0x01, 0x00, 0x00, 0x00, 0xE8, 0x0D,
    0x48, 0xE0, 0xFF, 0xC7, 0x87, 0x80, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00,
    0x00, 0xB8, 0x01, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x7C, 0x24, 0x30, 0x48,
    0x83, 0xC4, 0x20, 0x5B, 0xC3, 0x48, 0x8B, 0xCB, 0xE8, 0x9B, 0x73, 0x05,
    0x00, 0x48, 0x8B, 0x7C, 0x24, 0x30, 0xB8, 0x01, 0x00, 0x00, 0x00,
};

// 510BA0..510BC9: saves, then mov rbp, rcx (game) / mov r14, r9 (room) /
// mov rcx, rdx / mov ebx, r8d / mov rdi, rdx (object).
constexpr std::uint8_t InitHandlerEntryWitness[]{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89,
    0x74, 0x24, 0x18, 0x48, 0x89, 0x7C, 0x24, 0x20, 0x41, 0x56, 0x48, 0x83,
    0xEC, 0x60, 0x48, 0x8B, 0xE9, 0x4D, 0x8B, 0xF1, 0x48, 0x8B, 0xCA, 0x41,
    0x8B, 0xD8, 0x48, 0x8B, 0xFA,
};

// 510CB1..510D05: movzx eax, byte [rsi+15Ch] / cmp al, 51h / lea rdx, table /
// mov rdx, [rdx+rax*8] / ... / [rsp+28h] = rbp, [rsp+30h] = rdi,
// [rsp+38h] = r14, [rsp+40h] = rbx, [rsp+48h] = rsi / call rdx.
constexpr std::uint8_t InitHandlerCallWitness[]{
    0x48, 0x8B, 0xD8, 0x0F, 0xB6, 0x86, 0x5C, 0x01, 0x00, 0x00, 0x3C, 0x51,
    0x73, 0x46, 0x48, 0x8D, 0x15, 0xBA, 0xAC, 0xE8, 0x01, 0x48, 0x8B, 0x14,
    0xC2, 0x48, 0x85, 0xD2, 0x74, 0x36, 0x8B, 0x84, 0x24, 0x90, 0x00, 0x00,
    0x00, 0x48, 0x8D, 0x4C, 0x24, 0x28, 0x89, 0x44, 0x24, 0x50, 0x8B, 0x84,
    0x24, 0x98, 0x00, 0x00, 0x00, 0x89, 0x44, 0x24, 0x54, 0x48, 0x89, 0x6C,
    0x24, 0x28, 0x48, 0x89, 0x7C, 0x24, 0x30, 0x4C, 0x89, 0x74, 0x24, 0x38,
    0x48, 0x89, 0x5C, 0x24, 0x40, 0x48, 0x89, 0x74, 0x24, 0x48, 0xFF, 0xD2,
};

// 34B440: push rbx / sub rsp, 20h / mov rbx, rcx / null assert.
constexpr std::uint8_t UnitRoomWitness[]{
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0x48, 0x85, 0xC9,
    0x75, 0x13, 0x88, 0x4C, 0x24, 0x30, 0x48, 0x8D, 0x4C, 0x24, 0x30, 0xE8,
    0x54, 0xA7, 0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x01,
};

// 2EFC10: null room -> 0, else room+18h tail-jumps to DRLGROOM_GetLevelId 0x360FC0.
constexpr std::uint8_t RoomLevelWitness[]{
    0x48, 0x85, 0xC9, 0x75, 0x03, 0x33, 0xC0, 0xC3, 0x48, 0x8B, 0x49, 0x18,
    0xE9, 0x9F, 0x13, 0x07, 0x00,
};

// 48FE80: (game rcx, unit type edx, unit id r8d).
constexpr std::uint8_t ServerUnitWitness[]{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48,
    0x83, 0xEC, 0x20, 0x41, 0x8B, 0xD8, 0x8B, 0xF2, 0x48, 0x8B, 0xF9, 0x48,
    0x85, 0xC9, 0x75, 0x13, 0x88, 0x4C, 0x24, 0x38,
};

// ---------------------------------------------------------------------------
// The label anchors
// ---------------------------------------------------------------------------

constexpr std::uint64_t LabelRva              = 0x0C17E0;   // object hover label
constexpr std::uint64_t TileLabelRva          = 0x0C070C;   // warp tile hover label block
constexpr std::uint64_t ResolveCallRva        = 0x0C07BB;   // E8 to the string key resolver
constexpr std::uint64_t WarpLoopRva           = 0x5A11A0;   // the door's warp through its tiles
constexpr std::uint64_t UnitContextCallRva    = 0x0C1855;   // -> 0x34A0E0, the unit's data context
constexpr std::uint64_t ObjectsRecordCallRva  = 0x0C186A;   // -> 0x38FD00, DATATBLS_GetObjectsTxtRecord
constexpr std::uint64_t TileLevelCallRva      = 0x0C0746;   // -> 0x2EFE50, a warp tile's destination level
constexpr std::uint64_t LevelsRecordCallRva   = 0x0C075A;   // -> 0x32C4A0, DATATBLS_GetLevelsTxtRecord
constexpr std::uint64_t FirstUnitCallRva      = 0x5A11B9;   // -> 0x2EFD90, first unit in a room
constexpr std::uint64_t UnitTypeCallRva       = 0x5A11C9;   // -> 0x34B9D0, UNITS_GetUnitType
constexpr std::uint64_t NextUnitCallRva       = 0x5A11E5;   // -> 0x34B4A0, next unit in the room

constexpr std::uint64_t UnitContextRva        = 0x34A0E0;
constexpr std::uint64_t ObjectsRecordRva      = 0x38FD00;
constexpr std::uint64_t TileLevelRva          = 0x2EFE50;
constexpr std::uint64_t LevelsRecordRva       = 0x32C4A0;
constexpr std::uint64_t FirstUnitRva          = 0x2EFD90;
constexpr std::uint64_t UnitTypeRva           = 0x34B9D0;
constexpr std::uint64_t NextUnitRva           = 0x34B4A0;

constexpr std::size_t   LabelBufferBytes      = 1024;    // the caller's buffer, 1024 in every copy
constexpr std::size_t   LevelWarpOffset       = 0x125;   // levels.txt LevelWarp key, lea at 0C0786
constexpr std::size_t   LevelKeyBytes         = 40;      // LevelName +FDh to LevelWarp +125h
constexpr std::size_t   ObjectInitFnOffset    = 0x15C;   // byte, read at 510CB4
constexpr std::size_t   ObjectOperateFnOffset = 0x15E;   // byte, read by the operate handler
constexpr std::uint8_t  DoorInitFn            = 76;
constexpr std::uint8_t  DoorOperateFn         = 71;
constexpr std::uint32_t UnitTypeTile          = 5;

// 0C17E0..0C1876: push rbp/rsi/r14 / lea rbp / sub rsp, 1E0h / cookie /
// out in r14, unit in rsi / the two hide-label tests / class id, data context,
// objects.txt record / movzx ecx, byte [rax+127h] (SubClass).
constexpr std::uint8_t LabelWitness[]{
    0x40, 0x55, 0x56, 0x41, 0x56, 0x48, 0x8D, 0xAC, 0x24, 0x20, 0xFF, 0xFF,
    0xFF, 0x48, 0x81, 0xEC, 0xE0, 0x01, 0x00, 0x00, 0x48, 0x8B, 0x05, 0xCD,
    0x9A, 0x90, 0x02, 0x48, 0x33, 0xC4, 0x48, 0x89, 0x85, 0xD0, 0x00, 0x00,
    0x00, 0x4C, 0x8B, 0xF1, 0x48, 0x8B, 0xF2, 0x48, 0x8D, 0x4C, 0x24, 0x40,
    0xE8, 0x3B, 0xD9, 0x04, 0x00, 0x84, 0xC0, 0x0F, 0x85, 0x36, 0x03, 0x00,
    0x00, 0xE8, 0x8E, 0x75, 0x0D, 0x00, 0x3C, 0x01, 0x0F, 0x84, 0x29, 0x03,
    0x00, 0x00, 0x48, 0x89, 0x9C, 0x24, 0x10, 0x02, 0x00, 0x00, 0x48, 0x8D,
    0x15, 0x87, 0x12, 0xC0, 0x01, 0x41, 0xB8, 0xEC, 0x04, 0x00, 0x00, 0x48,
    0x89, 0xBC, 0x24, 0x18, 0x02, 0x00, 0x00, 0x48, 0x8B, 0xCE, 0xE8, 0x11,
    0x80, 0x28, 0x00, 0x48, 0x8B, 0xCE, 0x0F, 0xB7, 0xD8, 0xE8, 0x86, 0x88,
    0x28, 0x00, 0x48, 0x8B, 0xCE, 0x0F, 0xB6, 0xF8, 0xE8, 0x7B, 0x88, 0x28,
    0x00, 0x0F, 0xB6, 0xC8, 0x8B, 0xD3, 0xE8, 0x91, 0xE4, 0x2C, 0x00, 0x0F,
    0xB6, 0x88, 0x27, 0x01, 0x00, 0x00,
};

// 0C17E0: the first 20 bytes, whole instructions with nothing relative,
// moved by the inline hook. The next one is the RIP-relative cookie load.
constexpr std::size_t LabelPrologueSize = 20;

// 0C070C..0C07BB: UNITS_GetRoom(tile), class id, data context, 0x2EFE50,
// DATATBLS_GetLevelsTxtRecord, 0x2EFED0, lea rdx, [r13+125h], the key length
// loop, lea rcx, [rbp+1D0h]; then 0C07BB calls the resolver.
constexpr std::uint8_t TileLabelWitness[]{
    0x48, 0x8B, 0xCE, 0xE8, 0x2C, 0xAD, 0x28, 0x00, 0x41, 0xB8, 0x35, 0x04,
    0x00, 0x00, 0x48, 0x89, 0x85, 0xD0, 0x01, 0x00, 0x00, 0x48, 0x8D, 0x15,
    0x98, 0x23, 0xC0, 0x01, 0x48, 0x8B, 0xCE, 0x4C, 0x8B, 0xE8, 0xE8, 0x2D,
    0x91, 0x28, 0x00, 0x48, 0x8B, 0xCE, 0x8B, 0xD8, 0xE8, 0xA3, 0x99, 0x28,
    0x00, 0x0F, 0xB6, 0xC8, 0x44, 0x8B, 0xC3, 0x49, 0x8B, 0xD5, 0xE8, 0x05,
    0xF7, 0x22, 0x00, 0x48, 0x8B, 0xCE, 0x8B, 0xD8, 0xE8, 0x8B, 0x99, 0x28,
    0x00, 0x0F, 0xB6, 0xC8, 0x8B, 0xD3, 0xE8, 0x41, 0xBD, 0x26, 0x00, 0x4C,
    0x8B, 0xE8, 0x48, 0x85, 0xC0, 0x0F, 0x84, 0x37, 0x06, 0x00, 0x00, 0x48,
    0x8B, 0x8D, 0xD0, 0x01, 0x00, 0x00, 0x48, 0x8B, 0xD6, 0xE8, 0x56, 0xF7,
    0x22, 0x00, 0x48, 0x8B, 0xD8, 0x48, 0x85, 0xC0, 0x0F, 0x84, 0x1C, 0x06,
    0x00, 0x00, 0x49, 0x8D, 0x95, 0x25, 0x01, 0x00, 0x00, 0x48, 0x89, 0x95,
    0xD0, 0x01, 0x00, 0x00, 0x44, 0x38, 0x22, 0x74, 0x12, 0x49, 0x8B, 0xCE,
    0x0F, 0x1F, 0x40, 0x00, 0x48, 0xFF, 0xC1, 0x44, 0x38, 0x24, 0x0A, 0x75,
    0xF7, 0xEB, 0x02, 0x33, 0xC9, 0x48, 0x89, 0x8D, 0xD8, 0x01, 0x00, 0x00,
    0x48, 0x8D, 0x8D, 0xD0, 0x01, 0x00, 0x00,
};

// 5A11A0..5A11F2: UNITS_GetRoom(object), first unit, then for every unit:
// type == 5 -> warp the player through it (0x491FF0), next unit.
constexpr std::uint8_t WarpLoopWitness[]{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B,
    0xF9, 0x48, 0x8B, 0x49, 0x08, 0xE8, 0x8A, 0xA2, 0xDA, 0xFF, 0x48, 0x8B,
    0xC8, 0xE8, 0xD2, 0xEB, 0xD4, 0xFF, 0x48, 0x8B, 0xD8, 0x48, 0x85, 0xC0,
    0x74, 0x2C, 0x48, 0x8B, 0xCB, 0xE8, 0x02, 0xA8, 0xDA, 0xFF, 0x83, 0xF8,
    0x05, 0x75, 0x0F, 0x48, 0x8B, 0x57, 0x10, 0x4C, 0x8B, 0xC3, 0x48, 0x8B,
    0x0F, 0xE8, 0x0E, 0x0E, 0xEF, 0xFF, 0x48, 0x8B, 0xCB, 0xE8, 0xB6, 0xA2,
    0xDA, 0xFF, 0x48, 0x8B, 0xD8, 0x48, 0x85, 0xC0, 0x75, 0xD4,
};

// Init and operate argument blocks.
constexpr std::size_t InitGameOffset      = 0x00;
constexpr std::size_t InitObjectOffset    = 0x08;
constexpr std::size_t InitRoomOffset      = 0x10;
constexpr std::size_t OperateGameOffset   = 0x00;
constexpr std::size_t OperateObjectOffset = 0x08;
constexpr std::size_t OperatePlayerOffset = 0x10;

// Unit and game fields.
constexpr std::size_t   UnitTypeOffset  = 0x00;
constexpr std::size_t   UnitClassOffset = 0x04;
constexpr std::size_t   GameIdOffset    = 0x08;   // word, the id D2RCore reports
constexpr std::uint32_t UnitTypeObject  = 2;

// The door's modes, as the engine names them: 0 neutral, 1 operating,
// 2 opened. For this door they mean open, closing and shut.
constexpr std::uint32_t DoorOpen    = 0;
constexpr std::uint32_t DoorClosing = 1;
constexpr std::uint32_t DoorShut    = 2;
constexpr std::int32_t  RefusalSound = 20;   // edx at 549D4D and 549D7F

constexpr std::int32_t MinimumLevelId     = 1;
constexpr std::int32_t MaximumLevelId     = 32767;
constexpr std::int32_t MaximumMonsterId   = 32767;
constexpr std::size_t  MaximumLevels      = 64;
constexpr std::size_t  MaximumMonsters    = 64;
constexpr std::size_t  InitialConfigBytes = 16'384;
constexpr std::size_t  MaximumConfigBytes = 1'048'576;

constexpr const char* MainTable  = "summit_door_guardians";
constexpr const char* LevelTable = "summit_door_guardians.level";

using InitFn        = void (*)(void* init);
using OperateFn     = std::uint32_t (*)(void* operate);
using UnitIdFn      = std::uint32_t (*)(void* unit);
using AnimModeFn    = void (*)(void* unit, std::uint32_t mode);
using UnitModeFn    = std::uint32_t (*)(void* unit);
using SoundFn       = void (*)(void* unit, std::int32_t sound, void* target);
using WarpFn        = void (*)(void* operate);
using UnitRoomFn    = void* (*)(void* unit);
using RoomLevelFn   = std::int32_t (*)(void* room);
using ServerUnitFn  = void* (*)(void* game, std::uint32_t unitType, std::uint32_t unitId);

struct StringKey {
    const char*   text;
    std::uint64_t length;
};

using LabelFn         = std::uint8_t (*)(char* out, void* unit);
using UnitContextFn   = std::uint8_t (*)(void* unit);
using ObjectsRecordFn = const std::uint8_t* (*)(std::uint8_t dataContext, std::uint32_t classId);
using TileLevelFn     = std::uint32_t (*)(std::uint8_t dataContext, void* room, std::uint32_t tileClass);
using LevelsRecordFn  = const std::uint8_t* (*)(std::uint8_t dataContext, std::uint32_t levelId);
using FirstUnitFn     = void* (*)(void* room);
using NextUnitFn      = void* (*)(void* unit);
using UnitTypeFn      = std::uint32_t (*)(void* unit);
using ResolveKeyFn    = const char* (*)(const StringKey* key);

constexpr char DefaultConfigToml[] =
    "# Summit Door Guardians\n"
    "#\n"
    "# Opens the way-back door of a custom level once that level's guardians\n"
    "# are dead, the way the Arreat Summit door opens once the campaign\n"
    "# Ancients are dead.\n"
    "#\n"
    "# The door is the Arreat Summit's way back: objects.txt row 564\n"
    "# (SummitDoor), or any objects.txt row with InitFn 76 and OperateFn 71.\n"
    "# In the campaign it shuts when the level loads and only opens once the\n"
    "# Rite of Passage Ancients are dead. That condition belongs to the\n"
    "# campaign quest, so a custom level built on the same map preset keeps its\n"
    "# door shut forever.\n"
    "#\n"
    "# In every level listed below the door follows that level's own monsters\n"
    "# instead:\n"
    "#   - It shuts when the level loads, like the campaign door.\n"
    "#   - It opens the moment the last listed monster dies. If the door is\n"
    "#     far away and not loaded at that moment, it is open when you get\n"
    "#     back to it.\n"
    "#   - Clicking it works like the campaign door: while it is shut you get\n"
    "#     the same refusal sound, once it is open it takes you through.\n"
    "#   - Its label names the level it leads to, from levels.txt LevelWarp,\n"
    "#     the same text as the level's other exits.\n"
    "#   - The campaign quest is never read or changed for these doors, so the\n"
    "#     real Arreat Summit (level 120) and its Ancients are not affected.\n"
    "# Levels that are not listed keep the game's own behaviour.\n"
    "#\n"
    "# Progress\n"
    "#   - A kill counts when a listed monster dies inside its level, whoever\n"
    "#     or whatever killed it.\n"
    "#   - Counted per game, in memory only. Leaving the game clears it, and\n"
    "#     the next game starts with the door shut again.\n"
    "#\n"
    "# The door works on the server and its label on the client. Single player\n"
    "# is both, so everything applies there.\n"
    "#\n"
    "# Console command: summitdoor (status, the levels, and the progress and\n"
    "# door state in the current game)\n"
    "\n"
    "[summit_door_guardians]\n"
    "\n"
    "# Master switch. false installs nothing and every door works as in vanilla.\n"
    "enabled = true\n"
    "\n"
    "# One block per level:\n"
    "#   level     the levels.txt Id column of the level, 1 to 32767.\n"
    "#   monsters  the monstats.txt *hcIdx values (the row id) that must die,\n"
    "#             0 to 32767, on one line or several, # comments allowed.\n"
    "#             List an id once for every monster of that kind that has to\n"
    "#             die: [976, 976] needs two of them.\n"
    "#\n"
    "# Any mistake (a missing key, an id that is not a number, a level listed\n"
    "# twice, ...) stops the plugin from installing and is written to the\n"
    "# plugin log, so a typo can never open a door early. The doors then keep\n"
    "# the game's own behaviour.\n"
    "\n"
    "# Tier 5 Map - The Edge of Eternity: Talic, Madawc and Korlic the Eternal.\n"
    "[[summit_door_guardians.level]]\n"
    "level = 203\n"
    "monsters = [976, 977, 978]\n";

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

struct LevelRule {
    int                       line = 0;
    std::int32_t              level = 0;
    std::vector<std::int32_t> monsters;
};

struct LevelDraft {
    int                       line = 0;
    bool                      levelSet = false;
    bool                      monstersSet = false;
    bool                      broken = false;
    std::int32_t              level = 0;
    std::vector<std::int32_t> monsters;
};

struct DoorState {
    std::vector<std::uint8_t> killed;            // one per entry of the level's monsters list
    std::uint32_t             killedCount = 0;
    bool                      defeated = false;  // every listed monster died
    std::uint32_t             storedMode = DoorOpen;
    bool                      seen = false;      // a door hook ran for this level in this game
    std::uint32_t             unitId = 0;
    std::uint32_t             classId = 0;
};

struct GameState {
    void*                  game = nullptr;       // from the door hooks
    std::vector<DoorState> levels;               // one per rule
};

enum class InstallState : std::uint8_t {
    NotAttempted,
    DisabledByConfig,
    ConfigError,
    ServiceUnavailable,
    UnsupportedBuild,
    InstallFailed,
    Active,
};

const D2RL::PluginContext*      Context{};
std::uintptr_t                  Base{};
bool                            Enabled = true;
bool                            ConfigFileWasRead{};
std::vector<LevelRule>          Rules;
std::atomic<InstallState>       State{ InstallState::NotAttempted };
std::atomic<bool>               Armed{};
std::atomic<InstallState>       LabelState{ InstallState::NotAttempted };
std::atomic<bool>               LabelArmed{};
std::atomic<bool>               LabelMissingTileLogged{};

const D2RL::LifecycleService*   LifecycleApi{};
D2RL::Lifecycle::ListenerHandle DeathListener{};
D2RL::Lifecycle::ListenerHandle JoinListener{};
D2RL::Lifecycle::ListenerHandle LeaveListener{};

std::mutex                                    StateMutex;
std::unordered_map<std::uint32_t, GameState>  Games;

InitFn       OriginalInit{};
OperateFn    OriginalOperate{};
bool         InitSlotSwapped{};
bool         OperateSlotSwapped{};
UnitIdFn     UnitId{};
AnimModeFn   SetAnimMode{};
UnitModeFn   UnitMode{};
SoundFn      PlaySound{};
WarpFn       WarpThroughDoor{};
UnitRoomFn   UnitRoom{};
RoomLevelFn  RoomLevel{};
ServerUnitFn ServerUnit{};

LabelFn         OriginalLabel{};
UnitContextFn   UnitContext{};
ObjectsRecordFn ObjectsRecord{};
TileLevelFn     TileLevel{};
LevelsRecordFn  LevelsRecord{};
FirstUnitFn     FirstUnit{};
NextUnitFn      NextUnit{};
UnitTypeFn      UnitTypeOf{};
ResolveKeyFn    ResolveKey{};

template <typename T>
auto ReadAt(const void* base, std::size_t offset) noexcept -> T {
    T value{};
    std::memcpy(&value, static_cast<const std::uint8_t*>(base) + offset, sizeof(T));
    return value;
}

template <typename Function>
auto At(std::uint64_t rva) noexcept -> Function {
    return reinterpret_cast<Function>(Base + rva);
}

auto GameIdOf(void* game) noexcept -> std::uint32_t {
    return ReadAt<std::uint16_t>(game, GameIdOffset);
}

// "976 977 978", stopping before `limit` characters.
auto JoinIds(const std::vector<std::int32_t>& ids, std::size_t limit) -> std::string {
    std::string out;
    char number[16]{};
    for (const std::int32_t id : ids) {
        const int length = std::snprintf(number, sizeof(number), out.empty() ? "%d" : " %d", id);
        if (length <= 0) continue;
        if (out.size() + static_cast<std::size_t>(length) + 4 > limit) {
            out += " ...";
            break;
        }
        out.append(number, static_cast<std::size_t>(length));
    }
    return out;
}

auto RuleForLevel(std::int32_t level) noexcept -> int {
    for (std::size_t index = 0; index < Rules.size(); ++index) {
        if (Rules[index].level == level) return static_cast<int>(index);
    }
    return -1;
}

auto RuleForRoom(void* room) noexcept -> int {
    if (room == nullptr) return -1;
    return RuleForLevel(RoomLevel(room));
}

void Shape(GameState& game) {
    if (game.levels.size() == Rules.size()) return;
    game.levels.assign(Rules.size(), DoorState{});
    for (std::size_t index = 0; index < Rules.size(); ++index) {
        game.levels[index].killed.assign(Rules[index].monsters.size(), 0);
    }
}

// The level's state in this game, for a door hook. Caller holds StateMutex.
auto DoorStateFor(void* game, std::size_t rule) -> DoorState& {
    const std::uint32_t gameId = GameIdOf(game);
    GameState& state = Games[gameId];
    if (state.game != nullptr && state.game != game) {
        // Same id, another game: what was kept belongs to the old one.
        D2RL::LogInfoF(Context, "SummitDoorGuardians: game %u is a new game, its progress starts over.", gameId);
        state = GameState{};
    }
    state.game = game;
    Shape(state);
    return state.levels[rule];
}

// ---------------------------------------------------------------------------
// The door hooks
// ---------------------------------------------------------------------------

// InitFn 76 for every SummitDoor-type object. Steps of 0x549C70 with the
// level's own state; other levels go to the original.
void InitHook(void* init) noexcept {
    void* const game   = ReadAt<void*>(init, InitGameOffset);
    void* const object = ReadAt<void*>(init, InitObjectOffset);
    void* const room   = ReadAt<void*>(init, InitRoomOffset);
    const int   rule   = Armed.load(std::memory_order_acquire) && game != nullptr && object != nullptr
                       ? RuleForRoom(room) : -1;
    if (rule < 0) {
        OriginalInit(init);
        return;
    }

    const std::uint32_t unitId  = UnitId(object);
    const std::uint32_t classId = ReadAt<std::uint32_t>(object, UnitClassOffset);
    const auto    index = static_cast<std::size_t>(rule);
    std::uint32_t stored = DoorOpen;
    std::uint32_t left = 0;
    bool defeated = false;
    bool shut = false;
    {
        std::lock_guard<std::mutex> lock(StateMutex);
        DoorState& door = DoorStateFor(game, index);
        door.seen    = true;
        door.unitId  = unitId;
        door.classId = classId;
        stored   = door.storedMode;
        defeated = door.defeated;
        left     = static_cast<std::uint32_t>(Rules[index].monsters.size()) - door.killedCount;
        if (!defeated && stored != DoorShut) {
            door.storedMode = DoorShut;
            shut = true;
        }
    }

    SetAnimMode(object, stored);
    if (shut) SetAnimMode(object, DoorClosing);

    if (defeated) {
        D2RL::LogInfoF(Context, "SummitDoorGuardians: level %d: door loaded (unit %u) in game %u, open.",
            Rules[index].level, unitId, GameIdOf(game));
    } else {
        D2RL::LogInfoF(Context, "SummitDoorGuardians: level %d: door loaded (unit %u) in game %u, shut until "
            "%u more listed monster%s die%s.", Rules[index].level, unitId, GameIdOf(game), left,
            left == 1 ? "" : "s", left == 1 ? "s" : "");
    }
}

// OperateFn 71 for every SummitDoor-type object. Steps of 0x549CF0 with the
// level's own state; other levels go to the original.
auto OperateHook(void* operate) noexcept -> std::uint32_t {
    void* const game   = ReadAt<void*>(operate, OperateGameOffset);
    void* const object = ReadAt<void*>(operate, OperateObjectOffset);
    void* const player = ReadAt<void*>(operate, OperatePlayerOffset);
    const int   rule   = Armed.load(std::memory_order_acquire) && game != nullptr && object != nullptr
                       ? RuleForRoom(UnitRoom(object)) : -1;
    if (rule < 0) return OriginalOperate(operate);

    const std::uint32_t mode = UnitMode(object);
    if (mode > DoorShut) return 0;

    const std::uint32_t unitId  = UnitId(object);
    const std::uint32_t classId = ReadAt<std::uint32_t>(object, UnitClassOffset);
    bool defeated = false;
    {
        std::lock_guard<std::mutex> lock(StateMutex);
        DoorState& door = DoorStateFor(game, static_cast<std::size_t>(rule));
        door.seen    = true;
        door.unitId  = unitId;
        door.classId = classId;
        defeated = door.defeated;
        if (mode == DoorOpen) {
            if (!defeated) door.storedMode = DoorShut;
        } else if (defeated) {
            door.storedMode = DoorOpen;
        }
    }

    if (mode == DoorOpen) {
        if (!defeated) {
            SetAnimMode(object, DoorClosing);
            return 1;
        }
        WarpThroughDoor(operate);
        return 1;
    }
    // Shut or closing. The game plays the refusal sound twice for a shut
    // door, once here and once below; so does this.
    if (mode == DoorShut && !defeated) PlaySound(player, RefusalSound, player);
    if (defeated) {
        SetAnimMode(object, DoorOpen);
    } else {
        PlaySound(player, RefusalSound, player);
        SetAnimMode(object, DoorShut);
    }
    return 1;
}

// ---------------------------------------------------------------------------
// The door's label (client)
// ---------------------------------------------------------------------------

// The LevelWarp text of the level the first warp tile in `room` leads to,
// or nullptr. The same calls as the warp tile label at 0C070C.
auto WarpTileLabel(std::uint8_t dataContext, void* room) noexcept -> const char* {
    for (void* unit = FirstUnit(room); unit != nullptr; unit = NextUnit(unit)) {
        if (UnitTypeOf(unit) != UnitTypeTile) continue;
        const std::uint32_t tileClass = ReadAt<std::uint32_t>(unit, UnitClassOffset);
        const std::uint8_t* level = LevelsRecord(dataContext, TileLevel(dataContext, room, tileClass));
        if (level == nullptr) return nullptr;
        const char* key = reinterpret_cast<const char*>(level + LevelWarpOffset);
        const StringKey view{ key, static_cast<std::uint64_t>(strnlen(key, LevelKeyBytes)) };
        if (view.length == 0) return nullptr;
        return ResolveKey(&view);
    }
    return nullptr;
}

// 0x0C17E0. The game builds its label first; a door in a listed level then
// gets the label of the warp tile it leads through.
auto LabelHook(char* out, void* unit) noexcept -> std::uint8_t {
    const std::uint8_t result = OriginalLabel(out, unit);
    if (!LabelArmed.load(std::memory_order_acquire) || out == nullptr || out[0] == '\0' || unit == nullptr) {
        return result;
    }
    if (ReadAt<std::uint32_t>(unit, UnitTypeOffset) != UnitTypeObject) return result;
    const std::uint8_t  dataContext = UnitContext(unit);
    const std::uint8_t* object = ObjectsRecord(dataContext, ReadAt<std::uint32_t>(unit, UnitClassOffset));
    if (object == nullptr || object[ObjectInitFnOffset] != DoorInitFn || object[ObjectOperateFnOffset] != DoorOperateFn) {
        return result;
    }
    void* const room = UnitRoom(unit);
    if (RuleForRoom(room) < 0) return result;

    const char* text = WarpTileLabel(dataContext, room);
    if (text == nullptr || text[0] == '\0') {
        if (!LabelMissingTileLogged.exchange(true, std::memory_order_acq_rel)) {
            D2RL::LogWarnF(Context, "SummitDoorGuardians: level %d: no warp tile was found next to the door, so it "
                "keeps the game's label.", RoomLevel(room));
        }
        return result;
    }
    const std::size_t length = strnlen(text, LabelBufferBytes - 1);
    std::memcpy(out, text, length);
    out[length] = '\0';
    return result;
}

// ---------------------------------------------------------------------------
// Kills and game boundaries
// ---------------------------------------------------------------------------

// Opens the door the hooks last saw for this level, if it is loaded now.
void OpenLoadedDoor(void* game, std::uint32_t gameId, std::size_t rule, bool seen, std::uint32_t unitId,
        std::uint32_t classId) noexcept {
    const std::int32_t level = Rules[rule].level;
    if (game == nullptr || !seen) {
        D2RL::LogInfoF(Context, "SummitDoorGuardians: level %d: the door has not loaded in game %u yet; it is "
            "open when it does.", level, gameId);
        return;
    }
    if (GameIdOf(game) != gameId) {
        D2RL::LogWarnF(Context, "SummitDoorGuardians: level %d: the kill came from game %u but the door was "
            "seen in game %u; the door is open when it loads again.", level, gameId, GameIdOf(game));
        return;
    }
    void* const door = ServerUnit(game, UnitTypeObject, unitId);
    if (door == nullptr || ReadAt<std::uint32_t>(door, UnitTypeOffset) != UnitTypeObject
            || ReadAt<std::uint32_t>(door, UnitClassOffset) != classId
            || RuleForRoom(UnitRoom(door)) != static_cast<int>(rule)) {
        D2RL::LogInfoF(Context, "SummitDoorGuardians: level %d: the door (unit %u) is not loaded right now; it "
            "is open when its room loads.", level, unitId);
        return;
    }
    if (UnitMode(door) != DoorOpen) SetAnimMode(door, DoorOpen);
    D2RL::LogInfoF(Context, "SummitDoorGuardians: level %d: the door (unit %u) is open.", level, unitId);
}

void __cdecl OnMonsterDeath(const D2RL::PluginContext*, const D2RL::Lifecycle::MonsterDeathEvent* event,
        void*) noexcept {
    if (!Armed.load(std::memory_order_acquire)) return;
    if (!D2RL::Lifecycle::HasMonsterDeathEventField(event, D2RL::Lifecycle::MonsterDeathEventRequiredSize)) return;
    if (event->monster.type != D2RL::Lifecycle::UnitType::Monster) return;
    const int rule = RuleForLevel(event->levelId);
    if (rule < 0) return;
    const auto       index = static_cast<std::size_t>(rule);
    const LevelRule& level = Rules[index];
    const auto classId = static_cast<std::int32_t>(event->monster.classId);
    if (std::find(level.monsters.begin(), level.monsters.end(), classId) == level.monsters.end()) return;

    const std::uint32_t gameId = event->gameId;
    const std::size_t   needed = level.monsters.size();
    std::uint32_t count = 0;
    bool          opened = false;
    void*         game = nullptr;
    bool          seen = false;
    std::uint32_t unitId = 0;
    std::uint32_t doorClass = 0;
    {
        std::lock_guard<std::mutex> lock(StateMutex);
        GameState& state = Games[gameId];
        Shape(state);
        DoorState& door = state.levels[index];
        if (door.defeated) return;
        std::size_t slot = needed;
        for (std::size_t entry = 0; entry < needed; ++entry) {
            if (level.monsters[entry] == classId && door.killed[entry] == 0) {
                slot = entry;
                break;
            }
        }
        if (slot == needed) return;   // every copy of this id is already counted
        door.killed[slot] = 1;
        count = ++door.killedCount;
        if (count >= needed) {
            door.defeated   = true;
            door.storedMode = DoorOpen;
            opened    = true;
            game      = state.game;
            seen      = door.seen;
            unitId    = door.unitId;
            doorClass = door.classId;
        }
    }

    if (!opened) {
        D2RL::LogInfoF(Context, "SummitDoorGuardians: level %d: monster %d died, %u of %zu (game %u).",
            level.level, classId, count, needed, gameId);
        return;
    }
    D2RL::LogInfoF(Context, "SummitDoorGuardians: level %d: monster %d died, %u of %zu, all dead: the door "
        "opens (game %u).", level.level, classId, count, needed, gameId);
    OpenLoadedDoor(game, gameId, index, seen, unitId, doorClass);
}

void __cdecl OnGameBoundary(const D2RL::PluginContext*, const D2RL::Lifecycle::GameplayEvent*, void*) noexcept {
    std::lock_guard<std::mutex> lock(StateMutex);
    Games.clear();
}

// ---------------------------------------------------------------------------
// Config parsing (small TOML subset, no external dependency)
// ---------------------------------------------------------------------------

auto Trim(std::string_view value) noexcept -> std::string_view {
    std::size_t first = 0;
    while (first < value.size()
            && (value[first] == ' ' || value[first] == '\t' || value[first] == '\r' || value[first] == '\n')) {
        ++first;
    }
    std::size_t last = value.size();
    while (last > first
            && (value[last - 1] == ' ' || value[last - 1] == '\t' || value[last - 1] == '\r'
                || value[last - 1] == '\n')) {
        --last;
    }
    return value.substr(first, last - first);
}

// Cuts a # comment, ignoring # inside "basic" and 'literal' strings.
auto StripComment(std::string_view line) noexcept -> std::string_view {
    bool inBasic = false;
    bool inLiteral = false;
    bool escaped = false;
    for (std::size_t index = 0; index < line.size(); ++index) {
        const char character = line[index];
        if (inBasic) {
            if (escaped) escaped = false;
            else if (character == '\\') escaped = true;
            else if (character == '"') inBasic = false;
            continue;
        }
        if (inLiteral) {
            if (character == '\'') inLiteral = false;
            continue;
        }
        if (character == '"') inBasic = true;
        else if (character == '\'') inLiteral = true;
        else if (character == '#') return line.substr(0, index);
    }
    return line;
}

auto WithoutSpaces(std::string_view text) -> std::string {
    std::string out;
    for (const char character : text) {
        if (character != ' ' && character != '\t') out.push_back(character);
    }
    return out;
}

int ConfigErrors = 0;

void ConfigError(int line, const char* format, ...) noexcept {
    char message[640]{};
    va_list args;
    va_start(args, format);
    std::vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    ++ConfigErrors;
    D2RL::LogErrorF(Context, "SummitDoorGuardians: config line %d: %s", line, message);
}

void ConfigWarning(int line, const char* format, ...) noexcept {
    char message[640]{};
    va_list args;
    va_start(args, format);
    std::vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    D2RL::LogWarnF(Context, "SummitDoorGuardians: config line %d: %s", line, message);
}

// Decimal, optional leading '+', '_' separators allowed as in TOML.
auto ParseNumber(std::string_view text, std::int32_t maximum, std::int32_t& out) noexcept -> bool {
    std::size_t index = 0;
    if (!text.empty() && text[0] == '+') index = 1;
    std::int32_t value = 0;
    bool anyDigit = false;
    for (; index < text.size(); ++index) {
        const char character = text[index];
        if (character == '_') continue;
        if (character < '0' || character > '9') return false;
        anyDigit = true;
        value = value * 10 + (character - '0');
        if (value > maximum) return false;
    }
    if (!anyDigit) return false;
    out = value;
    return true;
}

// The text between the brackets, lines already joined with commas.
void ApplyMonsterList(std::string_view text, LevelDraft& draft, int line) {
    std::size_t cursor = 0;
    while (cursor <= text.size()) {
        const std::size_t comma = text.find(',', cursor);
        const std::size_t end = comma == std::string_view::npos ? text.size() : comma;
        const std::string_view element = Trim(text.substr(cursor, end - cursor));
        if (!element.empty()) {
            std::int32_t monsterId = 0;
            if (!ParseNumber(element, MaximumMonsterId, monsterId)) {
                const std::string shown(element.substr(0, 24));
                ConfigError(line, "monsters entry '%s' is not a whole number from 0 to %d.", shown.c_str(),
                    MaximumMonsterId);
                draft.broken = true;
            } else {
                draft.monsters.push_back(monsterId);
            }
        }
        if (comma == std::string_view::npos) break;
        cursor = comma + 1;
    }
}

enum class Table : std::uint8_t { Root, Main, Level, Other };

void ParseConfig(std::string_view text, std::vector<LevelDraft>& drafts) {
    Table       table = Table::Root;
    bool        capturing = false;
    int         captureLine = 0;
    std::string arrayText;
    int         lineNumber = 0;

    std::size_t cursor = 0;
    while (cursor < text.size()) {
        std::size_t end = text.find('\n', cursor);
        if (end == std::string_view::npos) end = text.size();
        ++lineNumber;
        const std::string_view line = Trim(StripComment(text.substr(cursor, end - cursor)));
        cursor = end + 1;

        if (capturing) {
            if (!line.empty() && line.front() == '[') {
                ConfigError(captureLine, "monsters is missing its closing ']'.");
                drafts.back().broken = true;
                capturing = false;
            } else {
                const std::size_t close = line.find(']');
                arrayText.append(line.substr(0, close));
                arrayText.push_back(',');
                if (close == std::string_view::npos) continue;
                capturing = false;
                if (!Trim(line.substr(close + 1)).empty()) {
                    ConfigError(lineNumber, "unexpected text after the closing ']' of monsters.");
                    drafts.back().broken = true;
                }
                ApplyMonsterList(arrayText, drafts.back(), captureLine);
                continue;
            }
        }

        if (line.empty()) continue;

        if (line.front() == '[') {
            const bool arrayTable = line.size() >= 2 && line[1] == '[';
            const std::size_t close = line.find(arrayTable ? "]]" : "]");
            if (close == std::string_view::npos) {
                ConfigError(lineNumber, "table header without its closing bracket.");
                table = Table::Other;
                continue;
            }
            const std::size_t open = arrayTable ? 2 : 1;
            const std::string name = WithoutSpaces(line.substr(open, close - open));
            if (arrayTable && name == LevelTable) {
                if (drafts.size() >= MaximumLevels) {
                    ConfigError(lineNumber, "more than %zu levels.", MaximumLevels);
                    table = Table::Other;
                    continue;
                }
                LevelDraft draft{};
                draft.line = lineNumber;
                drafts.push_back(std::move(draft));
                table = Table::Level;
            } else if (!arrayTable && name == MainTable) {
                table = Table::Main;
            } else if (name == LevelTable || name == MainTable) {
                ConfigError(lineNumber, "%s needs %s brackets.", name.c_str(),
                    arrayTable ? "single [ ]" : "double [[ ]]");
                table = Table::Other;
            } else {
                ConfigWarning(lineNumber, "unknown table [%s] ignored.", name.c_str());
                table = Table::Other;
            }
            continue;
        }

        const std::size_t equals = line.find('=');
        if (equals == std::string_view::npos) {
            ConfigError(lineNumber, "expected key = value.");
            if (table == Table::Level) drafts.back().broken = true;
            continue;
        }
        const std::string_view key = Trim(line.substr(0, equals));
        const std::string_view value = Trim(line.substr(equals + 1));
        const std::string keyText(key);

        if (table == Table::Main) {
            if (key == "enabled") {
                if (value == "true") Enabled = true;
                else if (value == "false") Enabled = false;
                else ConfigError(lineNumber, "enabled must be true or false.");
            } else {
                ConfigWarning(lineNumber, "unknown key '%s' ignored.", keyText.c_str());
            }
            continue;
        }
        if (table != Table::Level) {
            ConfigWarning(lineNumber, "key '%s' outside [%s] and [[%s]] ignored.", keyText.c_str(), MainTable,
                LevelTable);
            continue;
        }

        LevelDraft& draft = drafts.back();
        if (key == "level") {
            if (draft.levelSet) {
                ConfigError(lineNumber, "level is given twice in the same block.");
                draft.broken = true;
                continue;
            }
            draft.levelSet = true;
            std::int32_t level = 0;
            if (!ParseNumber(value, MaximumLevelId, level) || level < MinimumLevelId) {
                const std::string shown(value.substr(0, 24));
                ConfigError(lineNumber, "level '%s' is not a whole number from %d to %d.", shown.c_str(),
                    MinimumLevelId, MaximumLevelId);
                draft.broken = true;
                continue;
            }
            draft.level = level;
        } else if (key == "monsters") {
            if (draft.monstersSet) {
                ConfigError(lineNumber, "monsters is given twice in the same block.");
                draft.broken = true;
                continue;
            }
            draft.monstersSet = true;
            if (value.empty() || value.front() != '[') {
                ConfigError(lineNumber, "monsters must be a list in square brackets, for example "
                    "[976, 977, 978].");
                draft.broken = true;
                continue;
            }
            const std::size_t close = value.find(']');
            if (close == std::string_view::npos) {
                arrayText.assign(value.substr(1));
                arrayText.push_back(',');
                capturing = true;
                captureLine = lineNumber;
                continue;
            }
            if (!Trim(value.substr(close + 1)).empty()) {
                ConfigError(lineNumber, "unexpected text after the closing ']' of monsters.");
                draft.broken = true;
            }
            ApplyMonsterList(value.substr(1, close - 1), draft, lineNumber);
        } else {
            ConfigWarning(lineNumber, "unknown key '%s' ignored.", keyText.c_str());
        }
    }

    if (capturing) {
        ConfigError(captureLine, "monsters is missing its closing ']'.");
        drafts.back().broken = true;
    }
}

auto ReadConfigText(std::string& out) noexcept -> bool {
    std::string buffer(InitialConfigBytes, '\0');
    std::uint32_t required = 0;
    if (!Context->ReadConfig(buffer.data(), static_cast<std::uint32_t>(buffer.size()), &required)) {
        if (required < buffer.size() || required >= MaximumConfigBytes) return false;
        buffer.assign(static_cast<std::size_t>(required) + 1, '\0');
        if (!Context->ReadConfig(buffer.data(), static_cast<std::uint32_t>(buffer.size()), &required)) {
            return false;
        }
    }
    buffer.resize(std::strlen(buffer.c_str()));
    out = std::move(buffer);
    return true;
}

// False when anything is wrong: nothing is installed then, so a typo can
// never open a door early.
auto ReadConfiguration() -> bool {
    std::string text;
    if (!Context->EnsureConfig(DefaultConfigToml)) {
        Context->LogWarn("SummitDoorGuardians: the config file could not be created; using the embedded defaults.");
        text = DefaultConfigToml;
    } else if (!ReadConfigText(text)) {
        Context->LogWarn("SummitDoorGuardians: the config file could not be read; using the embedded defaults.");
        text = DefaultConfigToml;
    } else {
        ConfigFileWasRead = true;
    }

    ConfigErrors = 0;
    Enabled = true;
    std::vector<LevelDraft> drafts;
    ParseConfig(text, drafts);

    for (std::size_t index = 0; index < drafts.size(); ++index) {
        LevelDraft& draft = drafts[index];
        if (!draft.levelSet) {
            ConfigError(draft.line, "this block has no level.");
            draft.broken = true;
        }
        if (!draft.monstersSet) {
            ConfigError(draft.line, "this block has no monsters list.");
            draft.broken = true;
        } else if (draft.monsters.empty() && !draft.broken) {
            ConfigError(draft.line, "this block's monsters list is empty.");
            draft.broken = true;
        } else if (draft.monsters.size() > MaximumMonsters) {
            ConfigError(draft.line, "this block lists more than %zu monsters.", MaximumMonsters);
            draft.broken = true;
        }
        if (draft.levelSet && draft.level != 0) {
            for (std::size_t other = 0; other < index; ++other) {
                if (drafts[other].level == draft.level) {
                    ConfigError(draft.line, "level %d is already listed on line %d.", draft.level,
                        drafts[other].line);
                    draft.broken = true;
                    break;
                }
            }
        }
    }

    if (ConfigErrors > 0) {
        D2RL::LogErrorF(Context, "SummitDoorGuardians: NOT installed, %d config error%s (see above). Every door "
            "keeps the game's own behaviour.", ConfigErrors, ConfigErrors == 1 ? "" : "s");
        return false;
    }

    Rules.clear();
    for (LevelDraft& draft : drafts) {
        Rules.push_back(LevelRule{ .line = draft.line, .level = draft.level, .monsters = std::move(draft.monsters) });
    }
    return true;
}

// ---------------------------------------------------------------------------
// Verification and installation
// ---------------------------------------------------------------------------

auto Verify(std::uint64_t rva, const std::uint8_t* expected, std::size_t size, const char* label) noexcept -> bool {
    if (Context->CheckExpectedBytes(rva, expected, static_cast<std::uint32_t>(size))) return true;
    D2RL::LogErrorF(Context, "SummitDoorGuardians: NOT installed. %s at 0x%llX does not match the verified D2R "
        "image, or another plugin already changed it. Nothing was patched.", label,
        static_cast<unsigned long long>(rva));
    return false;
}

struct Witness {
    std::uint64_t       rva;
    const std::uint8_t* bytes;
    std::size_t         size;
    const char*         label;
};

constexpr Witness Witnesses[]{
    { DoorInitRva, DoorInitWitness, sizeof(DoorInitWitness), "The SummitDoor init (InitFn 76)" },
    { DoorOperateRva, DoorOperateWitness, sizeof(DoorOperateWitness), "The SummitDoor operate (OperateFn 71)" },
    { InitHandlerRva, InitHandlerEntryWitness, sizeof(InitHandlerEntryWitness), "The object init handler entry" },
    { InitHandlerCallRva, InitHandlerCallWitness, sizeof(InitHandlerCallWitness), "The object init handler's InitFn call" },
    { UnitRoomRva, UnitRoomWitness, sizeof(UnitRoomWitness), "UNITS_GetRoom" },
    { RoomLevelRva, RoomLevelWitness, sizeof(RoomLevelWitness), "DUNGEON_GetLevelIdFromRoom" },
    { ServerUnitRva, ServerUnitWitness, sizeof(ServerUnitWitness), "SUNIT_GetServerUnit" },
};

auto CallTarget(std::uint64_t rva) noexcept -> std::uint64_t {
    std::int32_t displacement = 0;
    std::memcpy(&displacement, reinterpret_cast<const void*>(Base + rva + 1), sizeof(displacement));
    return static_cast<std::uint64_t>(static_cast<std::int64_t>(rva + 5) + displacement);
}

auto SlotAddress(std::uint64_t tableRva, std::size_t slot) noexcept -> LONG64* {
    return reinterpret_cast<LONG64*>(Base + tableRva) + slot;
}

// Atomic, and only when the slot still holds `expected`.
auto SwapSlot(LONG64* slot, std::uint64_t expected, std::uint64_t desired) noexcept -> bool {
    DWORD previous = 0;
    if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &previous)) return false;
    const LONG64 seen = InterlockedCompareExchange64(slot, static_cast<LONG64>(desired), static_cast<LONG64>(expected));
    DWORD ignored = 0;
    VirtualProtect(slot, sizeof(*slot), previous, &ignored);
    return seen == static_cast<LONG64>(expected);
}

auto VerifySlot(std::uint64_t tableRva, std::size_t slot, std::uint64_t functionRva, const char* label) noexcept
        -> bool {
    const auto value = static_cast<std::uint64_t>(*SlotAddress(tableRva, slot));
    if (value == Base + functionRva) return true;
    D2RL::LogErrorF(Context, "SummitDoorGuardians: NOT installed. %s slot %zu (0x%llX) holds 0x%llX instead of "
        "0x%llX: another plugin already owns it, or this is not the verified D2R image. Nothing was patched.",
        label, slot, static_cast<unsigned long long>(tableRva + slot * sizeof(void*)),
        static_cast<unsigned long long>(value), static_cast<unsigned long long>(Base + functionRva));
    return false;
}

auto VerifyNativeContract() noexcept -> bool {
    for (const Witness& witness : Witnesses) {
        if (!Verify(witness.rva, witness.bytes, witness.size, witness.label)) return false;
    }
    struct Call {
        std::uint64_t site;
        std::uint64_t target;
    };
    constexpr Call Calls[]{
        { UnitIdCallRva, UnitIdRva },     { AnimModeCallRva, AnimModeRva }, { UnitModeCallRva, UnitModeRva },
        { SoundCallRva, SoundRva },       { WarpCallRva, WarpRva },
    };
    for (const Call& call : Calls) {
        if (CallTarget(call.site) == call.target) continue;
        D2RL::LogErrorF(Context, "SummitDoorGuardians: NOT installed. The call at 0x%llX goes to 0x%llX, not "
            "0x%llX.", static_cast<unsigned long long>(call.site),
            static_cast<unsigned long long>(CallTarget(call.site)), static_cast<unsigned long long>(call.target));
        return false;
    }
    if (!VerifySlot(InitTableRva, InitSlot, DoorInitRva, "The InitFn table")
            || !VerifySlot(OperateTableRva, OperateSlot, DoorOperateRva, "The OperateFn table")) {
        return false;
    }
    OriginalInit    = At<InitFn>(DoorInitRva);
    OriginalOperate = At<OperateFn>(DoorOperateRva);
    UnitId          = At<UnitIdFn>(CallTarget(UnitIdCallRva));
    SetAnimMode     = At<AnimModeFn>(CallTarget(AnimModeCallRva));
    UnitMode        = At<UnitModeFn>(CallTarget(UnitModeCallRva));
    PlaySound       = At<SoundFn>(CallTarget(SoundCallRva));
    WarpThroughDoor = At<WarpFn>(CallTarget(WarpCallRva));
    UnitRoom        = At<UnitRoomFn>(UnitRoomRva);
    RoomLevel       = At<RoomLevelFn>(RoomLevelRva);
    ServerUnit      = At<ServerUnitFn>(ServerUnitRva);
    return true;
}

void UnregisterListeners() noexcept {
    if (LifecycleApi == nullptr) return;
    if (DeathListener != 0) LifecycleApi->unregisterMonsterDeathListener(Context, DeathListener);
    if (JoinListener != 0) LifecycleApi->unregisterGameplayEventListener(Context, JoinListener);
    if (LeaveListener != 0) LifecycleApi->unregisterGameplayEventListener(Context, LeaveListener);
    DeathListener = 0;
    JoinListener = 0;
    LeaveListener = 0;
}

auto RegisterListeners() noexcept -> bool {
    const D2RL::Lifecycle::MonsterDeathListener death{
        .structSize = D2RL::Lifecycle::MonsterDeathListenerSize,
        .flags      = 0,
        .callback   = &OnMonsterDeath,
        .userData   = nullptr,
    };
    const D2RL::Lifecycle::GameplayEventListener joined{
        .structSize = D2RL::Lifecycle::GameplayEventListenerSize,
        .flags      = 0,
        .kind       = D2RL::Lifecycle::GameplayEventKind::GameJoined,
        .reserved   = 0,
        .callback   = &OnGameBoundary,
        .userData   = nullptr,
    };
    const D2RL::Lifecycle::GameplayEventListener left{
        .structSize = D2RL::Lifecycle::GameplayEventListenerSize,
        .flags      = 0,
        .kind       = D2RL::Lifecycle::GameplayEventKind::GameLeft,
        .reserved   = 0,
        .callback   = &OnGameBoundary,
        .userData   = nullptr,
    };
    using D2RL::Lifecycle::Result;
    if (LifecycleApi->registerMonsterDeathListener(Context, &death, &DeathListener) != Result::Success
            || LifecycleApi->registerGameplayEventListener(Context, &joined, &JoinListener) != Result::Success
            || LifecycleApi->registerGameplayEventListener(Context, &left, &LeaveListener) != Result::Success) {
        Context->LogError("SummitDoorGuardians: NOT installed. The monster death or game join/leave listener "
            "was refused.");
        UnregisterListeners();
        return false;
    }
    return true;
}

auto InitHookAddress() noexcept -> std::uint64_t {
    return reinterpret_cast<std::uint64_t>(&InitHook);
}

auto OperateHookAddress() noexcept -> std::uint64_t {
    return reinterpret_cast<std::uint64_t>(&OperateHook);
}

void RestoreSlots() noexcept {
    if (InitSlotSwapped) {
        if (SwapSlot(SlotAddress(InitTableRva, InitSlot), InitHookAddress(), Base + DoorInitRva)) {
            InitSlotSwapped = false;
        } else {
            Context->LogError("SummitDoorGuardians: InitFn slot 76 no longer points at this plugin and was left "
                "alone.");
        }
    }
    if (OperateSlotSwapped) {
        if (SwapSlot(SlotAddress(OperateTableRva, OperateSlot), OperateHookAddress(), Base + DoorOperateRva)) {
            OperateSlotSwapped = false;
        } else {
            Context->LogError("SummitDoorGuardians: OperateFn slot 71 no longer points at this plugin and was "
                "left alone.");
        }
    }
}

void Install(bool configUsable) noexcept {
    if (!configUsable) {
        State.store(InstallState::ConfigError, std::memory_order_release);
        return;
    }
    if (!Enabled) {
        State.store(InstallState::DisabledByConfig, std::memory_order_release);
        Context->LogInfo("SummitDoorGuardians: not installed, turned off in the config file. Every door keeps the "
            "game's own behaviour.");
        return;
    }
    if (Rules.empty()) {
        State.store(InstallState::DisabledByConfig, std::memory_order_release);
        Context->LogInfo("SummitDoorGuardians: not installed, no levels are listed in the config file.");
        return;
    }

    if (Context->QueryService(&LifecycleApi) != D2RL::ServiceQueryResult::Success
            || !D2RL::HasLifecycleServiceField(LifecycleApi, D2RL::LifecycleServiceRequiredSize)) {
        LifecycleApi = nullptr;
        State.store(InstallState::ServiceUnavailable, std::memory_order_release);
        Context->LogError("SummitDoorGuardians: NOT installed. D2RLoader's lifecycle service (monster deaths, "
            "game join and leave) is not available.");
        return;
    }
    if (!VerifyNativeContract()) {
        State.store(InstallState::UnsupportedBuild, std::memory_order_release);
        return;
    }
    if (!RegisterListeners()) {
        State.store(InstallState::InstallFailed, std::memory_order_release);
        return;
    }
    if (!SwapSlot(SlotAddress(OperateTableRva, OperateSlot), Base + DoorOperateRva, OperateHookAddress())) {
        UnregisterListeners();
        State.store(InstallState::InstallFailed, std::memory_order_release);
        Context->LogError("SummitDoorGuardians: NOT installed. OperateFn slot 71 could not be redirected.");
        return;
    }
    OperateSlotSwapped = true;
    if (!SwapSlot(SlotAddress(InitTableRva, InitSlot), Base + DoorInitRva, InitHookAddress())) {
        RestoreSlots();
        UnregisterListeners();
        State.store(InstallState::InstallFailed, std::memory_order_release);
        Context->LogError("SummitDoorGuardians: NOT installed. InitFn slot 76 could not be redirected.");
        return;
    }
    InitSlotSwapped = true;
    Armed.store(true, std::memory_order_release);
    State.store(InstallState::Active, std::memory_order_release);

    D2RL::LogInfoF(Context, "SummitDoorGuardians: installed, %zu level%s:", Rules.size(),
        Rules.size() == 1 ? "" : "s");
    for (const LevelRule& rule : Rules) {
        D2RL::LogInfoF(Context, "SummitDoorGuardians:   level %d: the way-back door opens once %s %s dead.",
            rule.level, JoinIds(rule.monsters, 400).c_str(), rule.monsters.size() == 1 ? "is" : "are");
    }
}

auto VerifyLabelContract() noexcept -> bool {
    constexpr Witness LabelWitnesses[]{
        { LabelRva, LabelWitness, sizeof(LabelWitness), "The object hover label (0x0C17E0)" },
        { TileLabelRva, TileLabelWitness, sizeof(TileLabelWitness), "The warp tile hover label" },
        { WarpLoopRva, WarpLoopWitness, sizeof(WarpLoopWitness), "The door's warp through its tiles" },
    };
    for (const Witness& witness : LabelWitnesses) {
        if (!Verify(witness.rva, witness.bytes, witness.size, witness.label)) return false;
    }
    if (*reinterpret_cast<const std::uint8_t*>(Base + ResolveCallRva) != 0xE8) {
        D2RL::LogErrorF(Context, "SummitDoorGuardians: the door label is NOT installed. 0x%llX is no longer a call "
            "to the string key resolver.", static_cast<unsigned long long>(ResolveCallRva));
        return false;
    }
    struct Call {
        std::uint64_t site;
        std::uint64_t target;
    };
    constexpr Call Calls[]{
        { UnitContextCallRva, UnitContextRva }, { ObjectsRecordCallRva, ObjectsRecordRva },
        { TileLevelCallRva, TileLevelRva },     { LevelsRecordCallRva, LevelsRecordRva },
        { FirstUnitCallRva, FirstUnitRva },     { UnitTypeCallRva, UnitTypeRva },
        { NextUnitCallRva, NextUnitRva },
    };
    for (const Call& call : Calls) {
        if (CallTarget(call.site) == call.target) continue;
        D2RL::LogErrorF(Context, "SummitDoorGuardians: the door label is NOT installed. The call at 0x%llX goes to "
            "0x%llX, not 0x%llX.", static_cast<unsigned long long>(call.site),
            static_cast<unsigned long long>(CallTarget(call.site)), static_cast<unsigned long long>(call.target));
        return false;
    }
    UnitContext   = At<UnitContextFn>(UnitContextRva);
    ObjectsRecord = At<ObjectsRecordFn>(ObjectsRecordRva);
    TileLevel     = At<TileLevelFn>(TileLevelRva);
    LevelsRecord  = At<LevelsRecordFn>(LevelsRecordRva);
    FirstUnit     = At<FirstUnitFn>(FirstUnitRva);
    UnitTypeOf    = At<UnitTypeFn>(UnitTypeRva);
    NextUnit      = At<NextUnitFn>(NextUnitRva);
    ResolveKey    = At<ResolveKeyFn>(CallTarget(ResolveCallRva));
    // The label also uses UNITS_GetRoom and DUNGEON_GetLevelIdFromRoom, set
    // and checked with the door above.
    return UnitRoom != nullptr && RoomLevel != nullptr;
}

// Separate from the door: a label that cannot be installed leaves the door
// working with the game's label.
void InstallLabel() noexcept {
    if (State.load(std::memory_order_acquire) != InstallState::Active) {
        LabelState.store(State.load(std::memory_order_acquire), std::memory_order_release);
        return;
    }
    if (!VerifyLabelContract()) {
        LabelState.store(InstallState::UnsupportedBuild, std::memory_order_release);
        return;
    }
    void* original = nullptr;
    if (!Context->InstallInlineHook(LabelRva, LabelWitness, static_cast<std::uint32_t>(LabelPrologueSize),
            reinterpret_cast<void*>(&LabelHook), &original) || original == nullptr) {
        LabelState.store(InstallState::InstallFailed, std::memory_order_release);
        Context->LogError("SummitDoorGuardians: the door label is NOT installed. The hook on the object hover "
            "label (0x0C17E0) was refused; the door works, its label stays the game's.");
        return;
    }
    OriginalLabel = reinterpret_cast<LabelFn>(original);
    LabelArmed.store(true, std::memory_order_release);
    LabelState.store(InstallState::Active, std::memory_order_release);
    Context->LogInfo("SummitDoorGuardians: door label installed. A door in a listed level is labelled with the "
        "levels.txt LevelWarp of the level it leads to.");
}

// ---------------------------------------------------------------------------
// Console command
// ---------------------------------------------------------------------------

auto StateText(InstallState state) noexcept -> const char* {
    switch (state) {
    case InstallState::Active:             return "active";
    case InstallState::DisabledByConfig:   return "off in the config file (vanilla doors)";
    case InstallState::ConfigError:        return "NOT ACTIVE, config errors, see the log (vanilla doors)";
    case InstallState::ServiceUnavailable: return "NOT ACTIVE, no lifecycle service, see the log";
    case InstallState::UnsupportedBuild:   return "NOT ACTIVE, unrecognised game build, see the log";
    case InstallState::InstallFailed:      return "NOT ACTIVE, install failed, see the log";
    case InstallState::NotAttempted:
    default:                               return "NOT ACTIVE, never attempted";
    }
}

auto ModeText(std::uint32_t mode) noexcept -> const char* {
    return mode == DoorOpen ? "open" : "shut";
}

auto __cdecl StatusCommand(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command, void*) noexcept
        -> D2RL::ConsoleCommandResult {
    if (command == nullptr || command->plugin == nullptr) return D2RL::ConsoleCommandResult::Failed;
    const D2RL::PluginContext* plugin = command->plugin;

    char line[512]{};
    std::snprintf(line, sizeof(line), "Summit Door Guardians: %s | config %s | %zu level%s",
        StateText(State.load(std::memory_order_acquire)),
        ConfigFileWasRead ? "loaded" : "NOT READ (embedded defaults)", Rules.size(), Rules.size() == 1 ? "" : "s");
    plugin->WriteConsoleMessage(line);
    std::snprintf(line, sizeof(line), "door label: %s", StateText(LabelState.load(std::memory_order_acquire)));
    plugin->WriteConsoleMessage(line);

    std::lock_guard<std::mutex> lock(StateMutex);
    for (std::size_t index = 0; index < Rules.size(); ++index) {
        const LevelRule& rule = Rules[index];
        std::snprintf(line, sizeof(line), "level %d: opens once %s %s dead", rule.level,
            JoinIds(rule.monsters, 300).c_str(), rule.monsters.size() == 1 ? "is" : "are");
        plugin->WriteConsoleMessage(line);
        for (const auto& [gameId, game] : Games) {
            if (index >= game.levels.size()) continue;
            const DoorState& door = game.levels[index];
            std::vector<std::int32_t> dead;
            for (std::size_t slot = 0; slot < rule.monsters.size(); ++slot) {
                if (door.killed[slot] != 0) dead.push_back(rule.monsters[slot]);
            }
            char doorText[96]{};
            if (door.seen) {
                std::snprintf(doorText, sizeof(doorText), "door unit %u %s", door.unitId, ModeText(door.storedMode));
            } else {
                std::snprintf(doorText, sizeof(doorText), "door not loaded yet%s",
                    door.defeated ? ", open when it loads" : "");
            }
            std::snprintf(line, sizeof(line), "  game %u: %zu of %zu dead%s%s | %s", gameId, dead.size(),
                rule.monsters.size(), dead.empty() ? "" : ": ", JoinIds(dead, 200).c_str(), doorText);
            plugin->WriteConsoleMessage(line);
        }
    }
    return D2RL::ConsoleCommandResult::Handled;
}

constexpr D2RL::PluginInfo Info{
    .infoSize    = D2RL::PluginInfoSize,
    .abiVersion  = D2RL_PLUGIN_ABI_VERSION,
    .id          = "celestialrayone.summit-door-guardians",
    .name        = "Summit Door Guardians",
    .version     = "1.1.0",
    .author      = "CelestialRayOne",
    .description = "Opens the Arreat Summit way-back door in listed custom levels once that level's listed "
                   "monsters are dead.",
    // Shared: object init and operate and monster deaths run on the server,
    // the door's hover label on the client.
    .flags       = D2RL::PluginFlags::Shared | D2RL::PluginFlags::NativeHooks,
};

static_assert(D2RL::HasValidPluginRole(Info.flags), "Exactly one execution role must be set.");
static_assert(D2RL::HasOnlyKnownPluginFlags(Info.flags), "Unknown plugin flag set.");

}  // namespace

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* {
    return &Info;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
    if (!D2RL::HasContext(context)) return false;
    Context = context;
    Base = context->exeBase;

    // Registered first, and the load always succeeds: returning false would
    // unload the DLL and take the command with it.
    if (!Context->RegisterConsoleCommand("summitdoor", &StatusCommand,
            "Reports Summit Door Guardians status, levels, progress and door state in the current game.")) {
        Context->LogWarn("SummitDoorGuardians: the status console command was refused.");
    }

    const bool configUsable = ReadConfiguration();
    Install(configUsable);
    InstallLabel();
    return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
    if (Context == nullptr) return;
    Armed.store(false, std::memory_order_release);
    LabelArmed.store(false, std::memory_order_release);
    RestoreSlots();
    UnregisterListeners();
}

}  // namespace CelestialRayOne::SummitDoorGuardians
