// Treasure Goblin
//
// A new monster AI for treasure goblins, selected through its own monai.txt
// row, plus a timed escape: after a set time the goblin stops, a portal opens
// next to it, and three seconds later both are gone. The goblin never
// dies on the way out, so it drops nothing and gives no experience.
//
// Written for D2R 3.3 on D2RLoader 1.3.1. Every address and byte below was read
// out of the live D2RLoader.exe process image (md5 8af7a47ce9077de0378c69af2930854d)
// and disassembled before being relied on.
//
// ---------------------------------------------------------------------------
// 1. How a monster gets its AI
// ---------------------------------------------------------------------------
//   monstats.txt's AI column is stored as a monai.txt row number, a signed
//   word at monstats record +0x52. 0x4A36C0 turns it into a 0x20-byte record
//   of the built-in AI table at 0x2396E90 (155 rows):
//     +0x00 target mode. 2 = the AI also runs when no target is in reach.
//     +0x08 setup routine, optional
//     +0x10 think routine, called as think(game, monster, tick)
//     +0x18 special-state routine, optional
//   A row number above 154 falls back to row 0, the do-nothing AI.
//   0x4A36C0 is asked when the AI is set up (0x4A2690 stores the record's
//   think routine in the monster's AI control at +0x08) and on every AI tick
//   (0x4A2A00 reads the target mode, picks the target, then calls the stored
//   think routine). The hook on 0x4A36C0 hands monsters whose row is the
//   TreasureGoblin row this plugin's record instead of row 0. The game does
//   not keep monai.txt names after loading, so the row is found by reading the
//   same monai.txt the game loaded and checking its row count against the
//   loaded table.
//
// ---------------------------------------------------------------------------
// 2. The tick the think routine receives
// ---------------------------------------------------------------------------
//   +0x10 target picked for target mode 2 (0x595750), may be null
//   +0x30 monstats2 record. Mode flags at record +0xF0, bit 15 = mRN.
//
// ---------------------------------------------------------------------------
// 3. Running
// ---------------------------------------------------------------------------
//   AITACTICS_MoveToTarget 0x4A8A10 (game, unit, target, mode, x, y, step,
//   flags). Mode 15 runs, mode 2 walks; the game itself turns a run into a
//   walk under Decrepify. With flags 0 a refused move returns 0 and changes
//   nothing, so the next direction can be tried at once. When a walk or run
//   ends the game calls the AI tick straight away, so runs chain without a
//   pause. AITACTICS_IdleInNeutralMode 0x4A6D10 (game, unit, frames) stops in
//   neutral and books the next tick (event 2) frames later.
//   The ground check is the game's own line test COLLISION_RayTrace 0x363F20
//   (room, start, end, mask): 0 when the whole line is free, else 1 with the
//   blocked point written to end. It crosses room borders by itself. The mask
//   is the monster pathing mask 0x3C01 (wall, object, door, no-path, pet).
//
// ---------------------------------------------------------------------------
// 4. The portal
// ---------------------------------------------------------------------------
//   The portal is objects.txt row 189, the portal Cain and the freed Act 5
//   prisoners leave through (the A5Q2 and A1Q4 quest code spawns it the same
//   way). Row 189 is Selectable 0 in every mode, OperateFn 0 and SubClass 0.
//   Clickability is not something the server can hand a client: every client
//   recomputes it in its own 0x34E5F0 (unit flags +0x124, bit 0x2, from
//   objects.txt Selectable of the new mode, record +0x85) whenever the object
//   is created or changes mode. Row 59, the town portal, is Selectable 1 once
//   open, so it can never stay unclickable; row 189 is never clickable, on any
//   client, and has no portal label or use.
//   Its InitFn 61 (0x5DBF20) sets the opening mode and books a quest update
//   (event 7) 25 frames later. The quest code behind that event belongs to Cain
//   and the Act 5 prisoners, so the plugin deletes the event and drives the
//   portal itself. Spawning follows the game's own portal spawn,
//   D2GAME_CreatePortalObject 0x432CE0 (block at 0x432E05, verified byte for
//   byte):
//     free spot  0x3632F0 (room, &spot, &spot, 3, 0x3E01, 0xC01, 0)
//     room       0x38DFB0 (room, x, y)
//     spawn      0x490410 (2, 189, x, y, game, room, 1, 1, 0, 0)
//     quest      0x48B890 (game, portal, 7, 0) deletes the booked event
//     opening    0x34E5F0 (portal, 1)
//     open       after (objects.txt record +0x9C >> 8) + 1 frames, mode 2
//   Removal is the game's own portal removal: 0x2EF4D0 (room, 2, guid) tells
//   the clients, then 0x48FAA0 (game, unit).
//
// ---------------------------------------------------------------------------
// 5. Timers and leaving
// ---------------------------------------------------------------------------
//   The game's timer setup 0x48BE80 (game, unit, event, frame, callback, id,
//   param, extra) stores an optional callback at timer +0x48; the timer run at
//   0x48C545 calls callback(game, unit, event, id, param) instead of the unit's
//   default handler. The timer code is built for a unit removing itself during
//   its own timer. Event 11 is used: monsters never use it and nothing deletes
//   it, and the callback replaces the default handler anyway.
//     goblin  one timer at escape + 75 frames: leaves (skipped when dead)
//     portal  one timer at the end of its opening, one at escape + 75 frames
//   Leaving is the sequence the 3.3 Act 5 prisoners use to walk out through
//   their portal (0x5D0A58, verified byte for byte): stop path 0x342B40, delete
//   the AI tick 0x48B890, tell the clients 0x2EF4D0, free collision 0x348B00,
//   leave the room 0x38EFE0, remove 0x48FAA0. No death: no loot, no experience.
//
// ---------------------------------------------------------------------------
// 6. Fields read
// ---------------------------------------------------------------------------
//   unit  +0x00 type, +0x04 class, +0x08 guid, +0x0C mode, +0x10 monster or
//         object data, +0x38 path, +0x124 flags (0x2 clickable, 0x10000 dead)
//   path  +0x02 x, +0x06 y (words)
//   game  +0x106 data table context, +0x170 frame
//
//   The callbacks and the cached think routine point into this DLL: do not
//   unload the plugin while a game is running.

// Windows.h defines min/max as macros unless NOMINMAX is set, which breaks
// std::max under MSVC.
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <D2RLPlugin/api.h>

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace CelestialRayOne::TreasureGoblin {
namespace {

// ---------------------------------------------------------------------------
// Addresses (RVAs into D2RLoader.exe)
// ---------------------------------------------------------------------------

constexpr std::uint64_t GetAiRecordRva       = 0x4A36C0;
constexpr std::uint64_t NormalAiTableRva     = 0x2396E90;
constexpr std::uint64_t IdleInNeutralModeRva = 0x4A6D10;
constexpr std::uint64_t MoveToTargetRva      = 0x4A8A10;
constexpr std::uint64_t RayTraceRva          = 0x363F20;
constexpr std::uint64_t InitTimerRva         = 0x48BE80;
constexpr std::uint64_t TimerCallbackSiteRva = 0x48C545;
constexpr std::uint64_t PortalSpawnBlockRva  = 0x432E05;
constexpr std::uint64_t LeaveBlockRva        = 0x5D0A58;

// Proven by the rel32 calls inside the two blocks above.
constexpr std::uint64_t GetFreeCoordsRva     = 0x3632F0;
constexpr std::uint64_t RoomAtPositionRva    = 0x38DFB0;
constexpr std::uint64_t AllocUnitRva         = 0x490410;
constexpr std::uint64_t ChangeAnimModeRva    = 0x34E5F0;
constexpr std::uint64_t ObjectsTxtRecordRva  = 0x38FD00;
constexpr std::uint64_t PathStopRva          = 0x342B40;
constexpr std::uint64_t EventDeleteRva       = 0x48B890;
constexpr std::uint64_t UnitRoomRva          = 0x34B440;
constexpr std::uint64_t DrlgDeleteRva        = 0x2EF4D0;
constexpr std::uint64_t FreeCollisionRva     = 0x348B00;
constexpr std::uint64_t LeaveRoomRva         = 0x38EFE0;
constexpr std::uint64_t RemoveUnitRva        = 0x48FAA0;

// 0x4A36C0, hooked: mov [rsp+20h],rsi / push rdi / sub rsp,30h /
// mov [rsp+40h],rbx / xor edi,edi / mov [rsp+50h],rbp / mov ebp,edx /
// mov rbx,rcx. Whole instructions, no branch, no RIP-relative operand.
constexpr std::uint8_t GetAiRecordEntry[]{
    0x48, 0x89, 0x74, 0x24, 0x20, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x48, 0x89, 0x5C, 0x24,
    0x40, 0x33, 0xFF, 0x48, 0x89, 0x6C, 0x24, 0x50, 0x8B, 0xEA, 0x48, 0x8B, 0xD9 };

// 0x4A6D10 AITACTICS_IdleInNeutralMode, whole body up to its event calls
// (0x48B890 delete, 0x48B720 set).
constexpr std::uint8_t IdleInNeutralModeBody[]{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC,
    0x70, 0x45, 0x85, 0xC0, 0x48, 0x8B, 0xF9, 0xBE, 0x01, 0x00, 0x00, 0x00, 0x48, 0x8B,
    0xCA, 0x41, 0x0F, 0x45, 0xF0, 0x48, 0x8B, 0xDA, 0xE8, 0x27, 0x3E, 0xEA, 0xFF, 0x83,
    0xF8, 0x01, 0x74, 0x2A, 0x4C, 0x8D, 0x44, 0x24, 0x40, 0xBA, 0x01, 0x00, 0x00, 0x00,
    0x48, 0x8B, 0xCB, 0xE8, 0x90, 0x04, 0xFA, 0xFF, 0x41, 0xB8, 0x01, 0x00, 0x00, 0x00,
    0x48, 0x89, 0x5C, 0x24, 0x50, 0x48, 0x8D, 0x54, 0x24, 0x40, 0x48, 0x8B, 0xCF, 0xE8,
    0x58, 0x08, 0xFA, 0xFF, 0x45, 0x33, 0xC9, 0x48, 0x8B, 0xD3, 0x48, 0x8B, 0xCF, 0x45,
    0x8D, 0x41, 0x02, 0xE8, 0x16, 0x4B, 0xFE, 0xFF, 0x44, 0x8B, 0x8F, 0x70, 0x01, 0x00,
    0x00, 0x33, 0xC0, 0x89, 0x44, 0x24, 0x30, 0x44, 0x03, 0xCE, 0x89, 0x44, 0x24, 0x28,
    0x48, 0x8B, 0xD3, 0x48, 0x8B, 0xCF, 0x89, 0x44, 0x24, 0x20, 0x44, 0x8D, 0x40, 0x02,
    0xE8, 0x7F, 0x49, 0xFE, 0xFF };

// 0x4A8A10 AITACTICS_MoveToTarget entry.
constexpr std::uint8_t MoveToTargetEntry[]{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24,
    0x18, 0x57, 0x48, 0x83, 0xEC, 0x50, 0x41, 0x8B, 0xF1, 0x49, 0x8B, 0xE8, 0x48, 0x8B,
    0xDA, 0x48, 0x8B, 0xF9, 0x48, 0x85, 0xC9 };

// 0x363F20 COLLISION_RayTrace entry.
constexpr std::uint8_t RayTraceEntry[]{
    0x44, 0x89, 0x4C, 0x24, 0x20, 0x4C, 0x89, 0x44, 0x24, 0x18, 0x55, 0x53, 0x56, 0x57,
    0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8B, 0xEC, 0x48, 0x83, 0xEC,
    0x48, 0x45, 0x8B, 0x10, 0x4C, 0x8B, 0xE9, 0x8B, 0x5A, 0x04 };

// 0x48BE80 timer setup entry.
constexpr std::uint8_t InitTimerEntry[]{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24,
    0x20, 0x57, 0x48, 0x83, 0xEC, 0x40, 0x41, 0x8B, 0xE9, 0x41, 0x8B, 0xF8, 0x48, 0x8B,
    0xDA, 0x48, 0x8B, 0xF1, 0x48, 0x85, 0xC9 };

// 0x48C545 timer run: mov r10,[rbx+48h] / mov rcx,rdi / mov rdx,[rbx+8] /
// test r10,r10 / jz default / mov eax,[rbx+1Ch] / mov r9d,[rbx+18h] /
// movzx r8d,byte [rbx] / mov [rsp+20h],eax / call r10
constexpr std::uint8_t TimerCallbackSite[]{
    0x4C, 0x8B, 0x53, 0x48, 0x48, 0x8B, 0xCF, 0x48, 0x8B, 0x53, 0x08, 0x4D, 0x85, 0xD2,
    0x74, 0x14, 0x8B, 0x43, 0x1C, 0x44, 0x8B, 0x4B, 0x18, 0x44, 0x0F, 0xB6, 0x03, 0x89,
    0x44, 0x24, 0x20, 0x41, 0xFF, 0xD2 };

// 0x432E05 the spawn block of D2GAME_CreatePortalObject: free spot, room,
// spawn, opening mode, objects.txt record, end-of-opening event.
constexpr std::uint8_t PortalSpawnBlock[]{
    0x4C, 0x8D, 0x84, 0x24, 0x90, 0x00, 0x00, 0x00, 0xC7, 0x44, 0x24, 0x28, 0x01, 0x0C,
    0x00, 0x00, 0x48, 0x8D, 0x94, 0x24, 0x90, 0x00, 0x00, 0x00, 0x41, 0xB9, 0x03, 0x00,
    0x00, 0x00, 0xC7, 0x44, 0x24, 0x20, 0x01, 0x3E, 0x00, 0x00, 0x48, 0x8B, 0xCF, 0xE8,
    0xBD, 0x04, 0xF3, 0xFF, 0x48, 0x85, 0xC0, 0x0F, 0x84, 0x2B, 0x01, 0x00, 0x00, 0x8B,
    0x9C, 0x24, 0x94, 0x00, 0x00, 0x00, 0x44, 0x8B, 0xBC, 0x24, 0x90, 0x00, 0x00, 0x00,
    0x44, 0x8B, 0xC3, 0x41, 0x8B, 0xD7, 0x48, 0x8B, 0xCF, 0xE8, 0x57, 0xB1, 0xF5, 0xFF,
    0x48, 0x8B, 0xF8, 0x48, 0x85, 0xC0, 0x0F, 0x84, 0x02, 0x01, 0x00, 0x00, 0x8B, 0x94,
    0x24, 0xB8, 0x00, 0x00, 0x00, 0x33, 0xC0, 0x48, 0x89, 0x44, 0x24, 0x48, 0xB9, 0x01,
    0x00, 0x00, 0x00, 0x89, 0x44, 0x24, 0x40, 0x44, 0x8B, 0xCB, 0x89, 0x4C, 0x24, 0x38,
    0x45, 0x8B, 0xC7, 0x66, 0x89, 0x4C, 0x24, 0x30, 0x8D, 0x48, 0x02, 0x48, 0x89, 0x7C,
    0x24, 0x28, 0x48, 0x89, 0x6C, 0x24, 0x20, 0xE8, 0x73, 0xD5, 0x05, 0x00, 0x48, 0x8B,
    0xD8, 0x48, 0x85, 0xF6, 0x74, 0x03, 0x48, 0x89, 0x06, 0xBA, 0x01, 0x00, 0x00, 0x00,
    0x48, 0x8B, 0xCB, 0xE8, 0x3B, 0xB7, 0xF1, 0xFF, 0x41, 0xB8, 0x6E, 0x10, 0x00, 0x00,
    0x48, 0x8D, 0x15, 0xDE, 0x64, 0x8E, 0x01, 0x48, 0x8B, 0xCB, 0xE8, 0x96, 0x69, 0xF1,
    0xFF, 0x0F, 0xB6, 0x8D, 0x06, 0x01, 0x00, 0x00, 0x8B, 0xD0, 0xE8, 0x28, 0xCE, 0xF5,
    0xFF, 0x44, 0x8B, 0x8D, 0x70, 0x01, 0x00, 0x00, 0x48, 0x8B, 0xD3, 0x41, 0xFF, 0xC1,
    0x8B, 0x88, 0x9C, 0x00, 0x00, 0x00, 0x33, 0xC0, 0xC1, 0xE9, 0x08, 0x89, 0x44, 0x24,
    0x30, 0x44, 0x03, 0xC9, 0x89, 0x44, 0x24, 0x28, 0x48, 0x8B, 0xCD, 0x44, 0x8D, 0x40,
    0x01, 0x89, 0x44, 0x24, 0x20, 0xE8, 0x15, 0x88, 0x05, 0x00 };

// 0x5D0A58 the prisoners' leave block: path, AI tick, clients, collision,
// room, remove.
constexpr std::uint8_t LeaveBlock[]{
    0x48, 0x8B, 0xCE, 0xE8, 0x20, 0xA4, 0xD7, 0xFF, 0x48, 0x8B, 0xC8, 0xE8, 0xD8, 0x20,
    0xD7, 0xFF, 0x45, 0x33, 0xC9, 0x48, 0x8B, 0xD6, 0x48, 0x8B, 0xCD, 0x45, 0x8D, 0x41,
    0x02, 0xE8, 0x16, 0xAE, 0xEB, 0xFF, 0x41, 0xB8, 0xA6, 0x15, 0x00, 0x00, 0x48, 0x8D,
    0x15, 0x89, 0x02, 0x77, 0x01, 0x48, 0x8B, 0xCE, 0xE8, 0xA1, 0x98, 0xD7, 0xFF, 0x48,
    0x8B, 0xCE, 0x8B, 0xF8, 0xE8, 0x37, 0xAF, 0xD7, 0xFF, 0x48, 0x8B, 0xCE, 0x8B, 0xD8,
    0xE8, 0x9D, 0xA9, 0xD7, 0xFF, 0x44, 0x8B, 0xC7, 0x8B, 0xD3, 0x48, 0x8B, 0xC8, 0xE8,
    0x20, 0xEA, 0xD1, 0xFF, 0x48, 0x8B, 0xCE, 0xE8, 0x48, 0x80, 0xD7, 0xFF, 0x48, 0x8B,
    0xCE, 0xE8, 0x20, 0xE5, 0xDB, 0xFF, 0x48, 0x8B, 0xD6, 0x48, 0x8B, 0xCD, 0xE8, 0xD5,
    0xEF, 0xEB, 0xFF };

// ---------------------------------------------------------------------------
// Layout and game constants
// ---------------------------------------------------------------------------

constexpr std::size_t UnitTypeOffset         = 0x00;
constexpr std::size_t UnitGuidOffset         = 0x08;
constexpr std::size_t UnitModeOffset         = 0x0C;
constexpr std::size_t UnitDataOffset         = 0x10;
constexpr std::size_t UnitPathOffset         = 0x38;
constexpr std::size_t UnitFlagsOffset        = 0x124;
constexpr std::size_t PathXOffset            = 0x02;
constexpr std::size_t PathYOffset            = 0x06;
constexpr std::size_t GameDataContextOffset  = 0x106;
constexpr std::size_t GameFrameOffset        = 0x170;
constexpr std::size_t TickTargetOffset       = 0x10;
constexpr std::size_t TickMonStats2Offset    = 0x30;
constexpr std::size_t MonStatsAiOffset       = 0x52;
constexpr std::size_t MonStats2ModesOffset   = 0xF0;
constexpr std::size_t ObjectsOpenFramesOffset = 0x9C;

constexpr std::uint32_t UnitTypePlayer   = 0;
constexpr std::uint32_t UnitTypeMonster  = 1;
constexpr std::uint32_t UnitTypeObject   = 2;
constexpr std::uint32_t FlagClickable    = 0x2;
constexpr std::uint32_t FlagDead         = 0x10000;
constexpr std::uint32_t RunModeBit       = 1U << 15;

constexpr std::int32_t ModeDeath         = 0;
constexpr std::int32_t ModeWalk          = 2;
constexpr std::int32_t ModeDead          = 12;
constexpr std::int32_t ModeRun           = 15;
constexpr std::int32_t ObjectModeOpening = 1;
constexpr std::int32_t ObjectModeOpen    = 2;

constexpr std::int32_t  PortalClassId     = 189;  // objects.txt, Cain's and the Act 5 prisoners' portal
constexpr std::int32_t  QuestEvent        = 7;
constexpr std::int32_t  AiTickEvent       = 2;
constexpr std::int32_t  CustomTimerEvent  = 11;
constexpr std::int32_t  BuiltInAiRows     = 155;
constexpr std::int32_t  MonsterPathMask   = 0x3C01;
constexpr std::uint32_t SpawnMask         = 0x3E01;
constexpr std::uint32_t SpawnFieldMask    = 0x0C01;
constexpr std::int32_t  PortalUnitSize    = 3;
constexpr std::uint32_t NoGuid            = 0xFFFFFFFFU;

constexpr std::uint32_t FramesPerSecond    = 25;
constexpr std::uint32_t EscapeFrames       = 3 * FramesPerSecond;
constexpr std::int32_t  SegmentLength      = 14;  // subtiles, about three tiles
constexpr std::int32_t  MinRunLength       = 6;
constexpr std::int32_t  RayStartOffset     = 2;   // clear of the goblin's own footprint
constexpr std::uint32_t StuckCheckFrames   = FramesPerSecond;
constexpr std::int32_t  StuckDistance      = 3;
constexpr std::uint32_t AvoidFrames        = 2 * FramesPerSecond;
constexpr std::int32_t  NoTargetIdleFrames = 8;
constexpr std::int32_t  BlockedIdleFrames  = 5;
constexpr std::int32_t  PortalOffset       = 3;
constexpr std::uint32_t PruneFrames        = 5 * 60 * FramesPerSecond;
constexpr std::size_t   PruneAbove         = 32;

constexpr std::size_t InitialConfigBytes = 8192;
constexpr std::size_t MaximumConfigBytes = 1 << 20;
constexpr std::string_view ConfigSection = "treasure_goblin";

constexpr const char* DefaultConfigToml = R"toml(# celestialrayone.treasure-goblin
#
# Treasure Goblin
#   A new monster AI for treasure goblins, selected through its own row in
#   monai.txt, and a timed escape: after despawn_seconds the goblin stops, a
#   portal opens next to it, and 3 seconds later the goblin and the
#   portal are gone. The goblin does not die, so it drops no loot and gives
#   no experience. Killing it before then works as usual.
#
# Setting it up in the txt files
#   1. Add one row at the END of data/global/excel/monai.txt with
#      TreasureGoblin in the AI column. The aip comment columns can stay
#      empty. Never insert it in the middle: every row after the insert would
#      get the wrong AI.
#   2. In monstats.txt, set the AI column of every goblin row to
#      TreasureGoblin, spelled exactly like the monai.txt row.
#   The game has 155 built-in AI routines, monai.txt rows 0 to 154. The new
#   row has to come after them; a row inside that range is refused.
#   When the tables load, the plugin finds the row by name in the monai.txt
#   the game loaded, checks that its row count matches the loaded table, and
#   logs the row number and how many monsters use it.
#
# What the AI does
#   - Runs from the nearest enemy without stopping. Each run goes about three
#     tiles straight away from the enemy, and the next run starts the moment
#     the last one ends. The run animation is used when the monster has one
#     (monstats2.txt mRN), otherwise the walk animation.
#   - Before every run it checks the ground ahead. When the way straight away
#     from the enemy is blocked, it takes the free direction that turns away
#     from it the least. In a 90 degree corner that means running along one
#     of the two walls.
#   - Every second it checks whether it actually moved. If it did not, the
#     direction it was trying is skipped for 2 seconds and it runs the closest
#     free direction instead.
#   - With no enemy in reach it waits, checking again every 8 frames.
#   - monstats.txt aip1 to aip8 and aidel are not used by this AI.
#
# Escaping
#   - The clock starts the first time the goblin's AI runs, which is when a
#     player first comes near it. It is counted in game time and keeps running
#     while players are away.
#   - When it runs out the goblin stops and a portal opens beside it: the
#     portal Cain and the freed Act 5 prisoners leave through (objects.txt row
#     189). Like theirs it cannot be hovered, clicked or used.
#   - 3 seconds later both disappear. If the goblin is killed during those 3
#     seconds it dies normally and the portal still closes on time.
#
# Console
#   treasuregoblin   shows whether the plugin is active, the monai.txt row it
#                    found for each table bank, and every goblin it is
#                    running, with the time each has left.

[treasure_goblin]

# true runs the TreasureGoblin AI and the escape. false leaves the game stock:
# monsters pointed at the TreasureGoblin row then get the do-nothing AI.
enabled = true

# The monai.txt AI name this plugin runs. Must match the monai.txt row and the
# monstats.txt AI column exactly.
ai_name = "TreasureGoblin"

# Seconds before a goblin escapes through its portal. 0 means never.
despawn_seconds = 60
)toml";

// --- pure logic begin --------------------------------------------------------

constexpr std::int32_t DirectionCount = 16;
constexpr double       Pi             = 3.14159265358979323846;

struct Coord {
    std::int32_t x;
    std::int32_t y;
};

auto DirectionAngle(std::int32_t index) noexcept -> double {
    return static_cast<double>(index) * (2.0 * Pi / DirectionCount);
}

auto AngleGap(double a, double b) noexcept -> double {
    const double d = std::fmod(std::fabs(a - b), 2.0 * Pi);
    return d > Pi ? 2.0 * Pi - d : d;
}

auto CircularGap(std::int32_t a, std::int32_t b) noexcept -> std::int32_t {
    const std::int32_t d = std::abs(a - b) % DirectionCount;
    return d > DirectionCount / 2 ? DirectionCount - d : d;
}

auto Step(Coord from, std::int32_t direction, std::int32_t length) noexcept -> Coord {
    const double angle = DirectionAngle(direction);
    return { from.x + static_cast<std::int32_t>(std::lround(std::cos(angle) * length)),
             from.y + static_cast<std::int32_t>(std::lround(std::sin(angle) * length)) };
}

// The 16 directions ordered by how far each turns from (dx, dy). On a tie the
// side closer to the previous run comes first, so the goblin keeps one side of
// a wall instead of flipping between the two.
auto OrderDirections(double dx, double dy, std::int32_t previous) noexcept
        -> std::array<std::int32_t, DirectionCount> {
    std::array<std::int32_t, DirectionCount> order{};
    for (std::int32_t i = 0; i < DirectionCount; ++i) order[static_cast<std::size_t>(i)] = i;
    const double ideal = std::atan2(dy, dx);
    std::stable_sort(order.begin(), order.end(), [&](std::int32_t a, std::int32_t b) {
        const double gapA = AngleGap(DirectionAngle(a), ideal);
        const double gapB = AngleGap(DirectionAngle(b), ideal);
        if (std::fabs(gapA - gapB) > 1e-9) return gapA < gapB;
        if (previous >= 0) {
            const std::int32_t sideA = CircularGap(a, previous);
            const std::int32_t sideB = CircularGap(b, previous);
            if (sideA != sideB) return sideA < sideB;
        }
        return a < b;
    });
    return order;
}

// Free run length from the line test result: the whole segment when the line
// is clear, else one subtile short of the blocked point.
auto FreeLengthFromTrace(Coord origin, bool blocked, Coord blockedAt) noexcept -> std::int32_t {
    if (!blocked) return SegmentLength;
    const std::int32_t reach = std::max(std::abs(blockedAt.x - origin.x), std::abs(blockedAt.y - origin.y));
    return reach > 0 ? reach - 1 : 0;
}

// --- pure logic end ----------------------------------------------------------

// ---------------------------------------------------------------------------
// Native signatures (Windows x64 ABI)
// ---------------------------------------------------------------------------

using GetAiRecordFn       = void* (*)(void* unit, std::int32_t specialState) noexcept;
using IdleInNeutralModeFn = std::int64_t (*)(void* game, void* unit, std::int32_t frames) noexcept;
using MoveToTargetFn      = std::int32_t (*)(void* game, void* unit, void* target, std::int32_t mode,
                                             std::int32_t x, std::int32_t y, std::uint8_t step,
                                             std::int16_t flags) noexcept;
using RayTraceFn          = std::int32_t (*)(void* room, Coord* start, Coord* end, std::int32_t mask) noexcept;
using InitTimerFn         = void* (*)(void* game, void* unit, std::int32_t event, std::uint32_t frame,
                                      void* callback, std::int32_t id, std::int32_t param,
                                      std::int32_t extra) noexcept;
using GetFreeCoordsFn     = void* (*)(void* room, Coord* spot, Coord* field, std::int32_t unitSize,
                                      std::uint32_t mask, std::uint32_t fieldMask,
                                      std::int32_t neighbourRooms) noexcept;
using RoomAtPositionFn    = void* (*)(void* room, std::int32_t x, std::int32_t y) noexcept;
using AllocUnitFn         = void* (*)(std::int32_t type, std::int32_t classId, std::int32_t x, std::int32_t y,
                                      void* game, void* room, std::int16_t init, std::int32_t mode,
                                      std::int32_t unused, void* unusedPointer) noexcept;
using ChangeAnimModeFn    = std::int32_t (*)(void* unit, std::int32_t mode) noexcept;
using ObjectsTxtRecordFn  = const std::uint8_t* (*)(std::uint8_t dataContext, std::int32_t classId) noexcept;
using PathStopFn          = void (*)(void* path) noexcept;
using EventDeleteFn       = void (*)(void* game, void* unit, std::int32_t event, std::int32_t id) noexcept;
using UnitRoomFn          = void* (*)(void* unit) noexcept;
using DrlgDeleteFn        = void (*)(void* room, std::uint32_t unitType, std::uint32_t guid) noexcept;
using UnitFn              = void (*)(void* unit) noexcept;
using RemoveUnitFn        = void (*)(void* game, void* unit) noexcept;

// The record handed out for the TreasureGoblin row, laid out like the game's.
struct AiRecord {
    std::uint64_t targetMode;
    void*         setup;
    void*         think;
    void*         special;
};

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

const D2RL::PluginContext*       Context{};
std::uintptr_t                   Base{};
const D2RL::DataTableService*    DataTableApi{};
const D2RL::LifecycleService*    LifecycleApi{};
D2RL::Lifecycle::ListenerHandle  TablesListener{};

GetAiRecordFn       OriginalGetAiRecord{};
IdleInNeutralModeFn IdleInNeutralMode{};
MoveToTargetFn      MoveToTarget{};
RayTraceFn          RayTrace{};
InitTimerFn         InitTimer{};
GetFreeCoordsFn     GetFreeCoords{};
RoomAtPositionFn    RoomAtPosition{};
AllocUnitFn         AllocUnit{};
ChangeAnimModeFn    ChangeAnimMode{};
ObjectsTxtRecordFn  ObjectsTxtRecord{};
PathStopFn          PathStop{};
EventDeleteFn       EventDelete{};
UnitRoomFn          UnitRoom{};
DrlgDeleteFn        DrlgDelete{};
UnitFn              FreeCollision{};
UnitFn              LeaveRoom{};
RemoveUnitFn        RemoveUnit{};

AiRecord       GoblinRecord{ 2, nullptr, nullptr, nullptr };
std::uintptr_t NormalAiTable{};

struct Settings {
    bool         enabled = true;
    std::string  aiName  = "TreasureGoblin";
    std::int32_t despawnSeconds = 60;
};
Settings Config;
bool     ConfigFileWasRead = false;
std::atomic<std::uint32_t> DespawnFrames{ 60 * FramesPerSecond };

enum class InstallState : std::uint32_t {
    NotAttempted, Installed, DisabledByConfig, ConfigError, UnsupportedBuild, NoTables, InstallFailed,
};
std::atomic<InstallState> State{ InstallState::NotAttempted };

// One table bank's binding: the compiled monstats rows of that bank and the
// TreasureGoblin row number of its monai.txt.
struct BankBinding {
    bool                loaded = false;
    const std::uint8_t* statsBegin = nullptr;
    const std::uint8_t* statsEnd = nullptr;
    std::int32_t        aiRow = -1;
    std::uint32_t       monsters = 0;
    std::string         status;
};

constexpr std::size_t BankCount = 3;
constexpr std::array<D2RL::DataTables::Bank, BankCount> Banks{
    D2RL::DataTables::Bank::Classic, D2RL::DataTables::Bank::Lod, D2RL::DataTables::Bank::Rotw };
constexpr std::array<const char*, BankCount> BankNames{ "Classic", "LoD", "RotW" };
constexpr std::array<const wchar_t*, BankCount> BankMonAiFiles{
    L"\\data\\global\\excel\\base\\monai.txt", L"\\data\\global\\excel\\base\\monai.txt",
    L"\\data\\global\\excel\\monai.txt" };

struct Resolution {
    std::array<BankBinding, BankCount> banks;
};
std::atomic<const Resolution*>            Current{};
std::vector<std::unique_ptr<Resolution>>  Resolutions;  // never freed: the hook may hold one

enum class Phase : std::uint8_t { Fleeing, Escaping };

struct GoblinState {
    void*         game = nullptr;
    void*         unit = nullptr;
    std::uint32_t firstFrame = 0;
    std::uint32_t lastSeen = 0;
    std::uint32_t checkFrame = 0;
    Coord         checkPos{};
    std::int32_t  lastDir = -1;
    std::int32_t  avoidDir = -1;
    std::uint32_t avoidUntil = 0;
    std::uint32_t escapeEnd = 0;
    std::uint32_t portalGuid = NoGuid;
    std::uint32_t stuckTurns = 0;
    Phase         phase = Phase::Fleeing;
};

std::recursive_mutex                           StateLock;
std::unordered_map<std::uint32_t, GoblinState> Goblins;
std::atomic<std::uint32_t>                     LastFrame{};
std::atomic<std::uint64_t>                     Escapes{};
std::atomic<std::uint64_t>                     Departures{};
std::atomic<std::uint64_t>                     PortalsOpened{};

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

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

auto GameFrame(void* game) noexcept -> std::uint32_t {
    return Read<std::uint32_t>(game, GameFrameOffset);
}

auto UnitPosition(void* unit, Coord& out) noexcept -> bool {
    if (unit == nullptr) return false;
    const std::uint32_t type = Read<std::uint32_t>(unit, UnitTypeOffset);
    if (type != UnitTypePlayer && type != UnitTypeMonster) return false;
    const void* path = Read<const void*>(unit, UnitPathOffset);
    if (path == nullptr) return false;
    out.x = Read<std::uint16_t>(path, PathXOffset);
    out.y = Read<std::uint16_t>(path, PathYOffset);
    return true;
}

auto IsUnit(void* unit, std::uint32_t type, std::int32_t guid) noexcept -> bool {
    return unit != nullptr && Read<std::uint32_t>(unit, UnitTypeOffset) == type
        && Read<std::uint32_t>(unit, UnitGuidOffset) == static_cast<std::uint32_t>(guid);
}

auto IsDeadOrDying(void* unit) noexcept -> bool {
    const std::int32_t mode = Read<std::int32_t>(unit, UnitModeOffset);
    return mode == ModeDeath || mode == ModeDead
        || (Read<std::uint32_t>(unit, UnitFlagsOffset) & FlagDead) != 0;
}

std::atomic<bool> ClickableWarned{};

// Row 189 is Selectable 0 in every mode. If a mod changed that, say so once.
void WarnIfClickable(void* portal, std::int32_t mode) noexcept {
    if ((Read<std::uint32_t>(portal, UnitFlagsOffset) & FlagClickable) == 0) return;
    if (ClickableWarned.exchange(true)) return;
    D2RL::LogWarnF(Context, "TreasureGoblin: objects.txt row %d is Selectable in mode %d, so goblin portals can "
        "be clicked. Set Selectable0 to Selectable7 of that row to 0.", PortalClassId, mode);
}

auto HasRunMode(const std::uint8_t* monStats2) noexcept -> bool {
    return monStats2 != nullptr && (Read<std::uint32_t>(monStats2, MonStats2ModesOffset) & RunModeBit) != 0;
}

auto FreeLength(void* room, Coord origin, std::int32_t direction) noexcept -> std::int32_t {
    if (room == nullptr) return 0;
    Coord start = Step(origin, direction, RayStartOffset);
    Coord end = Step(origin, direction, SegmentLength);
    const bool blocked = RayTrace(room, &start, &end, MonsterPathMask) != 0;
    return FreeLengthFromTrace(origin, blocked, end);
}

// ---------------------------------------------------------------------------
// Goblin bookkeeping (StateLock held)
// ---------------------------------------------------------------------------

void Prune(void* game, std::uint32_t frame, std::uint32_t keep) noexcept {
    for (auto it = Goblins.begin(); it != Goblins.end();) {
        const GoblinState& s = it->second;
        const bool stale = it->first != keep
            && (s.game != game || frame < s.lastSeen || frame - s.lastSeen > PruneFrames);
        it = stale ? Goblins.erase(it) : std::next(it);
    }
}

auto TrackGoblin(void* game, void* unit, std::uint32_t guid, std::uint32_t frame) noexcept -> GoblinState& {
    auto [it, inserted] = Goblins.try_emplace(guid);
    GoblinState& s = it->second;
    if (inserted || s.game != game || s.unit != unit || frame < s.lastSeen) {
        s = GoblinState{};
        s.game = game;
        s.unit = unit;
        s.firstFrame = frame;
        s.checkFrame = frame;
        UnitPosition(unit, s.checkPos);
        if (Goblins.size() > PruneAbove) Prune(game, frame, guid);
    }
    s.lastSeen = frame;
    return s;
}

// ---------------------------------------------------------------------------
// Portal
// ---------------------------------------------------------------------------

void PortalOpened(void*, void* unit, std::int32_t, std::int32_t id, std::int32_t) noexcept {
    if (!IsUnit(unit, UnitTypeObject, id)) return;
    if (Read<std::int32_t>(unit, UnitModeOffset) != ObjectModeOpening) return;
    ChangeAnimMode(unit, ObjectModeOpen);
    WarnIfClickable(unit, ObjectModeOpen);
}

void PortalCloses(void* game, void* unit, std::int32_t, std::int32_t id, std::int32_t) noexcept {
    if (!IsUnit(unit, UnitTypeObject, id)) return;
    void* room = UnitRoom(unit);
    if (room != nullptr) DrlgDelete(room, UnitTypeObject, static_cast<std::uint32_t>(id));
    RemoveUnit(game, unit);
}

auto TrySpawnPortal(void* game, void* room, Coord at) noexcept -> void* {
    Coord spot = at;
    if (GetFreeCoords(room, &spot, &spot, PortalUnitSize, SpawnMask, SpawnFieldMask, 0) == nullptr) return nullptr;
    void* spotRoom = RoomAtPosition(room, spot.x, spot.y);
    if (spotRoom == nullptr) return nullptr;
    return AllocUnit(static_cast<std::int32_t>(UnitTypeObject), PortalClassId, spot.x, spot.y, game, spotRoom,
                     1, 1, 0, nullptr);
}

// Opens the portal beside the goblin, a step ahead in the direction it was
// running when there is room, and books its own open and close timers.
auto SpawnPortal(void* game, void* goblin, std::int32_t direction, std::uint32_t frame) noexcept -> std::uint32_t {
    void* room = UnitRoom(goblin);
    Coord at{};
    if (room == nullptr || !UnitPosition(goblin, at)) return NoGuid;

    void* portal = direction >= 0 ? TrySpawnPortal(game, room, Step(at, direction, PortalOffset)) : nullptr;
    if (portal == nullptr) portal = TrySpawnPortal(game, room, at);
    if (portal == nullptr) return NoGuid;

    // InitFn 61 booked the Cain / Act 5 prisoner quest update; none of that
    // quest code is meant for this portal.
    EventDelete(game, portal, QuestEvent, 0);
    ChangeAnimMode(portal, ObjectModeOpening);
    WarnIfClickable(portal, ObjectModeOpening);

    const std::uint32_t guid = Read<std::uint32_t>(portal, UnitGuidOffset);
    const std::uint8_t* objects = ObjectsTxtRecord(Read<std::uint8_t>(game, GameDataContextOffset), PortalClassId);
    const std::uint32_t openFrames = objects != nullptr ? (Read<std::uint32_t>(objects, ObjectsOpenFramesOffset) >> 8) + 1 : 0;
    if (openFrames != 0 && openFrames < EscapeFrames) {
        InitTimer(game, portal, CustomTimerEvent, frame + openFrames, reinterpret_cast<void*>(&PortalOpened),
                  static_cast<std::int32_t>(guid), 0, 0);
    }
    InitTimer(game, portal, CustomTimerEvent, frame + EscapeFrames, reinterpret_cast<void*>(&PortalCloses),
              static_cast<std::int32_t>(guid), 0, 0);
    PortalsOpened.fetch_add(1, std::memory_order_relaxed);
    return guid;
}

// ---------------------------------------------------------------------------
// Leaving
// ---------------------------------------------------------------------------

void LeaveNow(void* game, void* unit, std::uint32_t guid) noexcept {
    std::lock_guard lock(StateLock);
    Goblins.erase(guid);
    if (IsDeadOrDying(unit)) return;

    void* path = Read<void*>(unit, UnitPathOffset);
    if (path != nullptr) PathStop(path);
    EventDelete(game, unit, AiTickEvent, 0);
    void* room = UnitRoom(unit);
    if (room != nullptr) DrlgDelete(room, UnitTypeMonster, guid);
    FreeCollision(unit);
    LeaveRoom(unit);
    RemoveUnit(game, unit);
    Departures.fetch_add(1, std::memory_order_relaxed);
}

void GoblinLeaves(void* game, void* unit, std::int32_t, std::int32_t id, std::int32_t) noexcept {
    if (!IsUnit(unit, UnitTypeMonster, id)) return;
    LeaveNow(game, unit, static_cast<std::uint32_t>(id));
}

void BeginEscape(void* game, void* unit, std::uint32_t guid, std::uint32_t frame, GoblinState& state) noexcept {
    state.phase = Phase::Escaping;
    state.escapeEnd = frame + EscapeFrames;
    IdleInNeutralMode(game, unit, static_cast<std::int32_t>(EscapeFrames));
    state.portalGuid = SpawnPortal(game, unit, state.lastDir, frame);
    InitTimer(game, unit, CustomTimerEvent, frame + EscapeFrames, reinterpret_cast<void*>(&GoblinLeaves),
              static_cast<std::int32_t>(guid), 0, 0);
    Escapes.fetch_add(1, std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------
// The AI
// ---------------------------------------------------------------------------

auto GoblinThink(void* game, void* unit, void* tick) noexcept -> std::int64_t {
    if (game == nullptr || unit == nullptr) return 0;
    const std::uint32_t frame = GameFrame(game);
    const std::uint32_t guid = Read<std::uint32_t>(unit, UnitGuidOffset);
    LastFrame.store(frame, std::memory_order_relaxed);

    std::lock_guard lock(StateLock);
    GoblinState& state = TrackGoblin(game, unit, guid, frame);

    if (state.phase == Phase::Escaping) {
        // The leave timer does the removal. This only covers a timer that
        // never fired, two frames late at most.
        if (frame >= state.escapeEnd + 2) {
            LeaveNow(game, unit, guid);
            return 0;
        }
        const std::uint32_t left = state.escapeEnd > frame ? state.escapeEnd - frame : 1;
        return IdleInNeutralMode(game, unit, static_cast<std::int32_t>(left));
    }

    const std::uint32_t despawn = DespawnFrames.load(std::memory_order_relaxed);
    if (despawn != 0 && frame - state.firstFrame >= despawn) {
        BeginEscape(game, unit, guid, frame, state);
        return 0;
    }

    void* target = tick != nullptr ? Read<void*>(tick, TickTargetOffset) : nullptr;
    Coord self{};
    Coord threat{};
    if (target == nullptr || !UnitPosition(unit, self) || !UnitPosition(target, threat)) {
        state.lastDir = -1;
        return IdleInNeutralMode(game, unit, NoTargetIdleFrames);
    }

    // Once a second: did the last runs actually move the goblin?
    if (frame - state.checkFrame >= StuckCheckFrames) {
        const std::int32_t moved = std::max(std::abs(self.x - state.checkPos.x), std::abs(self.y - state.checkPos.y));
        if (state.lastDir >= 0 && moved < StuckDistance) {
            state.avoidDir = state.lastDir;
            state.avoidUntil = frame + AvoidFrames;
            ++state.stuckTurns;
        }
        state.checkFrame = frame;
        state.checkPos = self;
    }

    double dx = static_cast<double>(self.x - threat.x);
    double dy = static_cast<double>(self.y - threat.y);
    if (dx == 0.0 && dy == 0.0) {
        const double angle = DirectionAngle(state.lastDir >= 0 ? state.lastDir : static_cast<std::int32_t>(guid % DirectionCount));
        dx = std::cos(angle);
        dy = std::sin(angle);
    }

    void* room = UnitRoom(unit);
    const std::uint8_t* monStats2 = tick != nullptr ? Read<const std::uint8_t*>(tick, TickMonStats2Offset) : nullptr;
    const std::int32_t mode = HasRunMode(monStats2) ? ModeRun : ModeWalk;
    const bool avoiding = state.avoidDir >= 0 && frame < state.avoidUntil;
    const auto order = OrderDirections(dx, dy, state.lastDir);

    for (std::int32_t pass = avoiding ? 0 : 1; pass < 2; ++pass) {
        for (const std::int32_t direction : order) {
            if (pass == 0 && CircularGap(direction, state.avoidDir) <= 1) continue;
            const std::int32_t length = FreeLength(room, self, direction);
            if (length < MinRunLength) continue;
            const Coord destination = Step(self, direction, length);
            if (MoveToTarget(game, unit, nullptr, mode, destination.x, destination.y, 1, 0) != 0) {
                state.lastDir = direction;
                return 1;
            }
        }
    }

    state.lastDir = -1;
    return IdleInNeutralMode(game, unit, BlockedIdleFrames);
}

// ---------------------------------------------------------------------------
// The hook
// ---------------------------------------------------------------------------

auto UsesGoblinAi(const std::uint8_t* stats, std::int32_t row) noexcept -> bool {
    const Resolution* resolution = Current.load(std::memory_order_acquire);
    if (resolution == nullptr || row < BuiltInAiRows) return false;
    for (const BankBinding& bank : resolution->banks) {
        if (stats >= bank.statsBegin && stats < bank.statsEnd) return bank.aiRow >= 0 && row == bank.aiRow;
    }
    for (const BankBinding& bank : resolution->banks) {
        if (bank.aiRow >= 0 && row == bank.aiRow) return true;
    }
    return false;
}

auto HookGetAiRecord(void* unit, std::int32_t specialState) noexcept -> void* {
    void* record = OriginalGetAiRecord(unit, specialState);
    if (reinterpret_cast<std::uintptr_t>(record) != NormalAiTable || unit == nullptr) return record;
    if (Read<std::uint32_t>(unit, UnitTypeOffset) != UnitTypeMonster) return record;
    const void* monsterData = Read<const void*>(unit, UnitDataOffset);
    if (monsterData == nullptr) return record;
    const std::uint8_t* stats = Read<const std::uint8_t*>(monsterData, 0);
    if (stats == nullptr) return record;
    const std::int32_t row = Read<std::int16_t>(stats, MonStatsAiOffset);
    return UsesGoblinAi(stats, row) ? static_cast<void*>(&GoblinRecord) : record;
}

// ---------------------------------------------------------------------------
// monai.txt
// ---------------------------------------------------------------------------

auto Utf8ToWide(const char* text) -> std::wstring {
    if (text == nullptr || *text == '\0') return {};
    const int length = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
    if (length <= 1) return {};
    std::wstring out(static_cast<std::size_t>(length - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text, -1, out.data(), length);
    return out;
}

auto WideToUtf8(const std::wstring& text) -> std::string {
    if (text.empty()) return {};
    const int length = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (length <= 1) return {};
    std::string out(static_cast<std::size_t>(length - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, out.data(), length, nullptr, nullptr);
    return out;
}

auto ReadWholeFile(const std::wstring& path, std::string& out) -> bool {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    bool ok = GetFileSizeEx(file, &size) != 0 && size.QuadPart > 0 && size.QuadPart < (64LL << 20);
    if (ok) {
        out.assign(static_cast<std::size_t>(size.QuadPart), '\0');
        DWORD read = 0;
        ok = ReadFile(file, out.data(), static_cast<DWORD>(out.size()), &read, nullptr) != 0 && read == out.size();
    }
    CloseHandle(file);
    return ok;
}

auto EqualsNoCase(std::string_view a, std::string_view b) noexcept -> bool {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        char x = a[i];
        char y = b[i];
        if (x >= 'A' && x <= 'Z') x = static_cast<char>(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = static_cast<char>(y - 'A' + 'a');
        if (x != y) return false;
    }
    return true;
}

auto IsBlank(std::string_view line) noexcept -> bool {
    return line.find_first_not_of(" \t") == std::string_view::npos;
}

auto Cell(std::string_view line, std::size_t column) noexcept -> std::string_view {
    std::size_t start = 0;
    for (std::size_t i = 0; i < column; ++i) {
        const std::size_t tab = line.find('\t', start);
        if (tab == std::string_view::npos) return {};
        start = tab + 1;
    }
    const std::size_t tab = line.find('\t', start);
    return line.substr(start, tab == std::string_view::npos ? std::string_view::npos : tab - start);
}

// The AI names of monai.txt in row order, counted the way that gives the row
// count the game loaded. False when no counting matches.
auto ParseMonAi(std::string_view text, std::uint32_t loadedRows, std::vector<std::string>& names) -> bool {
    std::vector<std::string_view> lines;
    std::size_t cursor = 0;
    while (cursor < text.size()) {
        std::size_t end = text.find('\n', cursor);
        if (end == std::string_view::npos) end = text.size();
        std::string_view line = text.substr(cursor, end - cursor);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        lines.push_back(line);
        cursor = end + 1;
    }
    if (lines.empty()) return false;

    std::size_t aiColumn = std::string_view::npos;
    for (std::size_t column = 0;; ++column) {
        const std::string_view header = Cell(lines[0], column);
        if (header.empty() && Cell(lines[0], column + 1).empty()) break;
        if (EqualsNoCase(header, "AI")) {
            aiColumn = column;
            break;
        }
    }
    if (aiColumn == std::string_view::npos) return false;

    std::vector<std::string_view> rows;
    for (std::size_t i = 1; i < lines.size(); ++i) {
        if (!IsBlank(lines[i])) rows.push_back(lines[i]);
    }
    if (rows.size() != loadedRows) {
        std::size_t last = lines.size();
        while (last > 1 && IsBlank(lines[last - 1])) --last;
        rows.assign(lines.begin() + 1, lines.begin() + static_cast<std::ptrdiff_t>(last));
        if (rows.size() != loadedRows) return false;
    }

    names.clear();
    names.reserve(rows.size());
    for (const std::string_view row : rows) names.emplace_back(Cell(row, aiColumn));
    return true;
}

void AddRoot(std::vector<std::wstring>& roots, std::wstring root) {
    while (!root.empty() && (root.back() == L'\\' || root.back() == L'/')) root.pop_back();
    if (root.empty()) return;
    for (const std::wstring& existing : roots) {
        if (existing.size() == root.size()
            && CompareStringOrdinal(existing.c_str(), static_cast<int>(existing.size()), root.c_str(),
                                    static_cast<int>(root.size()), TRUE) == CSTR_EQUAL) {
            return;
        }
    }
    roots.push_back(std::move(root));
}

auto CandidateRoots() -> std::vector<std::wstring> {
    std::vector<std::wstring> roots;
    const std::wstring mod = Utf8ToWide(Context->activeMod);
    for (const wchar_t* folder : { Context->modDirectory, Context->scopeRootDirectory }) {
        if (folder == nullptr || *folder == L'\0') continue;
        const std::wstring root(folder);
        AddRoot(roots, root);
        if (!mod.empty()) AddRoot(roots, root + L"\\" + mod + L".mpq");
    }
    return roots;
}

auto LoadedTable(D2RL::DataTables::Bank bank, D2RL::DataTables::TableId id, D2RL::DataTables::TableView& view) -> bool {
    view = {};
    view.structSize = D2RL::DataTables::TableViewSize;
    return DataTableApi->getTable(Context, bank, id, &view) == D2RL::DataTables::Result::Success
        && D2RL::DataTables::HasTableViewField(&view, D2RL::DataTables::TableViewRequiredSize)
        && view.rows != nullptr && view.rowCount != 0;
}

void BindBank(std::size_t index, const std::vector<std::wstring>& roots, BankBinding& binding) {
    char text[512]{};
    D2RL::DataTables::TableView stats{};
    D2RL::DataTables::TableView monAi{};
    if (!LoadedTable(Banks[index], D2RL::DataTables::TableId::MonStats, stats)
        || !LoadedTable(Banks[index], D2RL::DataTables::TableId::MonAi, monAi)) {
        binding.status = "tables not loaded";
        return;
    }
    binding.loaded = true;
    binding.statsBegin = static_cast<const std::uint8_t*>(stats.rows);
    binding.statsEnd = binding.statsBegin + static_cast<std::size_t>(stats.rowCount) * stats.rowSize;

    std::vector<std::string> names;
    std::string path;
    bool foundFile = false;
    for (const std::wstring& root : roots) {
        const std::wstring candidate = root + BankMonAiFiles[index];
        std::string fileText;
        if (!ReadWholeFile(candidate, fileText)) continue;
        foundFile = true;
        if (ParseMonAi(fileText, monAi.rowCount, names)) {
            path = WideToUtf8(candidate);
            break;
        }
        names.clear();
    }
    if (names.empty()) {
        std::snprintf(text, sizeof(text), foundFile
                ? "no monai.txt matches the %u loaded rows"
                : "monai.txt not found in the mod folder (%u rows loaded)", monAi.rowCount);
        binding.status = text;
        return;
    }

    std::int32_t row = -1;
    for (std::size_t i = 0; i < names.size() && row < 0; ++i) {
        if (names[i] == Config.aiName) row = static_cast<std::int32_t>(i);
    }
    for (std::size_t i = 0; i < names.size() && row < 0; ++i) {
        if (EqualsNoCase(names[i], Config.aiName)) row = static_cast<std::int32_t>(i);
    }
    if (row < 0) {
        std::snprintf(text, sizeof(text), "no %s row in %s", Config.aiName.c_str(), path.c_str());
        binding.status = text;
        return;
    }
    if (row < BuiltInAiRows) {
        std::snprintf(text, sizeof(text), "%s is row %d of %s, a built-in AI row; it must come after row %d",
                      Config.aiName.c_str(), row, path.c_str(), BuiltInAiRows - 1);
        binding.status = text;
        return;
    }

    if (stats.rowSize > MonStatsAiOffset + 1) {
        for (std::uint32_t i = 0; i < stats.rowCount; ++i) {
            const std::uint8_t* record = binding.statsBegin + static_cast<std::size_t>(i) * stats.rowSize;
            if (Read<std::int16_t>(record, MonStatsAiOffset) == row) ++binding.monsters;
        }
    }
    binding.aiRow = row;
    std::snprintf(text, sizeof(text), "%s is monai.txt row %d, used by %u monster%s (%s)", Config.aiName.c_str(),
                  row, binding.monsters, binding.monsters == 1 ? "" : "s", path.c_str());
    binding.status = text;
}

std::mutex ResolveLock;  // the load-time try and the tables-loaded listener may overlap

void Resolve() {
    if (DataTableApi == nullptr) return;
    std::lock_guard lock(ResolveLock);
    auto resolution = std::make_unique<Resolution>();
    const std::vector<std::wstring> roots = CandidateRoots();
    bool anyLoaded = false;
    for (std::size_t index = 0; index < BankCount; ++index) {
        BankBinding& binding = resolution->banks[index];
        BindBank(index, roots, binding);
        if (!binding.loaded) continue;
        anyLoaded = true;
        if (binding.aiRow >= 0) {
            D2RL::LogInfoF(Context, "TreasureGoblin: %s tables: %s.", BankNames[index], binding.status.c_str());
        } else {
            D2RL::LogWarnF(Context, "TreasureGoblin: %s tables: %s.", BankNames[index], binding.status.c_str());
        }
    }
    if (!anyLoaded) return;
    Current.store(resolution.get(), std::memory_order_release);
    Resolutions.push_back(std::move(resolution));
}

void __cdecl OnDataTablesLoaded(const D2RL::PluginContext*, const D2RL::Lifecycle::DataTablesLoadedEvent*,
        void*) noexcept {
    Resolve();
}

// ---------------------------------------------------------------------------
// Config
// ---------------------------------------------------------------------------

auto Trim(std::string_view text) noexcept -> std::string_view {
    const std::size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return {};
    const std::size_t last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

auto StripComment(std::string_view line) noexcept -> std::string_view {
    bool quoted = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        if (line[i] == '"') quoted = !quoted;
        if (line[i] == '#' && !quoted) return line.substr(0, i);
    }
    return line;
}

void ParseConfig(std::string_view text) {
    std::string_view section;
    std::size_t cursor = 0;
    while (cursor < text.size()) {
        std::size_t end = text.find('\n', cursor);
        if (end == std::string_view::npos) end = text.size();
        const std::string_view line = Trim(StripComment(text.substr(cursor, end - cursor)));
        cursor = end + 1;
        if (line.empty()) continue;
        if (line.front() == '[') {
            const std::size_t close = line.find(']');
            section = Trim(line.substr(1, close == std::string_view::npos ? std::string_view::npos : close - 1));
            continue;
        }
        const std::size_t equals = line.find('=');
        if (equals == std::string_view::npos || section != ConfigSection) continue;
        const std::string_view key = Trim(line.substr(0, equals));
        const std::string_view value = Trim(line.substr(equals + 1));

        if (key == "enabled") {
            if (value == "true") Config.enabled = true;
            else if (value == "false") Config.enabled = false;
            else Context->LogWarn("TreasureGoblin: enabled must be true or false; keeping the previous value.");
        } else if (key == "ai_name") {
            if (value.size() >= 3 && value.front() == '"' && value.back() == '"') {
                Config.aiName.assign(value.substr(1, value.size() - 2));
            } else {
                Context->LogWarn("TreasureGoblin: ai_name must be a quoted name, for example \"TreasureGoblin\".");
            }
        } else if (key == "despawn_seconds") {
            std::int32_t seconds = 0;
            bool ok = !value.empty() && value.size() <= 5;
            for (const char c : value) {
                if (c < '0' || c > '9') ok = false;
                else seconds = seconds * 10 + (c - '0');
            }
            if (ok) Config.despawnSeconds = seconds;
            else Context->LogWarn("TreasureGoblin: despawn_seconds must be a whole number from 0 to 99999.");
        }
    }
}

auto ReadConfiguration() -> bool {
    std::string text;
    if (!Context->EnsureConfig(DefaultConfigToml)) {
        Context->LogWarn("TreasureGoblin: the config file could not be created; using the built-in defaults.");
        text = DefaultConfigToml;
    } else {
        std::string buffer(InitialConfigBytes, '\0');
        std::uint32_t required = 0;
        bool ok = Context->ReadConfig(buffer.data(), static_cast<std::uint32_t>(buffer.size()), &required);
        if (!ok && required >= buffer.size() && required < MaximumConfigBytes) {
            buffer.assign(static_cast<std::size_t>(required) + 1, '\0');
            ok = Context->ReadConfig(buffer.data(), static_cast<std::uint32_t>(buffer.size()), &required);
        }
        if (!ok) {
            Context->LogWarn("TreasureGoblin: the config file could not be read; using the built-in defaults.");
            text = DefaultConfigToml;
        } else {
            buffer.resize(std::strlen(buffer.c_str()));
            text = std::move(buffer);
            ConfigFileWasRead = true;
        }
    }
    ParseConfig(text);
    if (Config.aiName.empty()) {
        Context->LogError("TreasureGoblin: ai_name is empty.");
        return false;
    }
    DespawnFrames.store(static_cast<std::uint32_t>(Config.despawnSeconds) * FramesPerSecond, std::memory_order_relaxed);
    return true;
}

// ---------------------------------------------------------------------------
// Install
// ---------------------------------------------------------------------------

struct Witness {
    const char*         name;
    std::uint64_t       rva;
    const std::uint8_t* bytes;
    std::uint32_t       size;
};

auto VerifyImage() -> bool {
    const Witness witnesses[]{
        { "AI record lookup", GetAiRecordRva, GetAiRecordEntry, sizeof(GetAiRecordEntry) },
        { "idle in neutral mode", IdleInNeutralModeRva, IdleInNeutralModeBody, sizeof(IdleInNeutralModeBody) },
        { "move to target", MoveToTargetRva, MoveToTargetEntry, sizeof(MoveToTargetEntry) },
        { "line test", RayTraceRva, RayTraceEntry, sizeof(RayTraceEntry) },
        { "timer setup", InitTimerRva, InitTimerEntry, sizeof(InitTimerEntry) },
        { "timer callback call", TimerCallbackSiteRva, TimerCallbackSite, sizeof(TimerCallbackSite) },
        { "town portal spawn block", PortalSpawnBlockRva, PortalSpawnBlock, sizeof(PortalSpawnBlock) },
        { "leave block", LeaveBlockRva, LeaveBlock, sizeof(LeaveBlock) },
    };
    bool verified = true;
    for (const Witness& witness : witnesses) {
        if (!Context->CheckExpectedBytes(witness.rva, witness.bytes, witness.size)) {
            D2RL::LogErrorF(Context,
                "TreasureGoblin: the %s at 0x%llX does not match the verified D2R image, or another plugin "
                "already patches it.", witness.name, static_cast<unsigned long long>(witness.rva));
            verified = false;
        }
    }
    return verified;
}

template <typename Fn>
void Bind(Fn& function, std::uint64_t rva) noexcept {
    function = reinterpret_cast<Fn>(Base + rva);
}

void BindNatives() noexcept {
    Bind(IdleInNeutralMode, IdleInNeutralModeRva);
    Bind(MoveToTarget, MoveToTargetRva);
    Bind(RayTrace, RayTraceRva);
    Bind(InitTimer, InitTimerRva);
    Bind(GetFreeCoords, GetFreeCoordsRva);
    Bind(RoomAtPosition, RoomAtPositionRva);
    Bind(AllocUnit, AllocUnitRva);
    Bind(ChangeAnimMode, ChangeAnimModeRva);
    Bind(ObjectsTxtRecord, ObjectsTxtRecordRva);
    Bind(PathStop, PathStopRva);
    Bind(EventDelete, EventDeleteRva);
    Bind(UnitRoom, UnitRoomRva);
    Bind(DrlgDelete, DrlgDeleteRva);
    Bind(FreeCollision, FreeCollisionRva);
    Bind(LeaveRoom, LeaveRoomRva);
    Bind(RemoveUnit, RemoveUnitRva);
    NormalAiTable = Base + NormalAiTableRva;
    GoblinRecord.think = reinterpret_cast<void*>(&GoblinThink);
}

void ConnectTables() {
    if (Context->QueryService(&DataTableApi) != D2RL::ServiceQueryResult::Success
            || !D2RL::HasDataTableServiceField(DataTableApi, D2RL::DataTableServiceRequiredSize)) {
        DataTableApi = nullptr;
    }
    if (Context->QueryService(&LifecycleApi) != D2RL::ServiceQueryResult::Success
            || !D2RL::HasLifecycleServiceField(LifecycleApi, D2RL::LifecycleServiceRequiredSize)) {
        LifecycleApi = nullptr;
    }
    if (DataTableApi == nullptr || LifecycleApi == nullptr) return;

    const D2RL::Lifecycle::DataTablesLoadedListener tables{
        .structSize = D2RL::Lifecycle::DataTablesLoadedListenerSize,
        .flags      = 0,
        .callback   = &OnDataTablesLoaded,
        .userData   = nullptr,
    };
    if (LifecycleApi->registerDataTablesLoadedListener(Context, &tables, &TablesListener)
            != D2RL::Lifecycle::Result::Success) {
        TablesListener = 0;
    }
    Resolve();  // In case the tables are already loaded.
}

void Install(bool configUsable) {
    if (!configUsable) {
        State.store(InstallState::ConfigError, std::memory_order_release);
        return;
    }
    if (!Config.enabled) {
        State.store(InstallState::DisabledByConfig, std::memory_order_release);
        Context->LogInfo("TreasureGoblin: disabled in the config file; nothing installed.");
        return;
    }
    if (!VerifyImage()) {
        State.store(InstallState::UnsupportedBuild, std::memory_order_release);
        Context->LogError("TreasureGoblin: nothing installed; the game runs stock.");
        return;
    }
    BindNatives();
    ConnectTables();
    if (DataTableApi == nullptr || LifecycleApi == nullptr || TablesListener == 0) {
        State.store(InstallState::NoTables, std::memory_order_release);
        Context->LogError("TreasureGoblin: D2RLoader's data table services are unavailable, so the monai.txt row "
                          "cannot be found; nothing installed.");
        return;
    }
    if (!Context->InstallInlineHook(GetAiRecordRva, GetAiRecordEntry,
            static_cast<std::uint32_t>(sizeof(GetAiRecordEntry)), &HookGetAiRecord, &OriginalGetAiRecord)
            || OriginalGetAiRecord == nullptr) {
        State.store(InstallState::InstallFailed, std::memory_order_release);
        Context->LogError("TreasureGoblin: the AI record lookup at 0x4A36C0 could not be hooked; nothing active.");
        return;
    }
    State.store(InstallState::Installed, std::memory_order_release);
    D2RL::LogInfoF(Context, "TreasureGoblin: active, ai_name \"%s\", escape after %d second%s%s.",
        Config.aiName.c_str(), Config.despawnSeconds, Config.despawnSeconds == 1 ? "" : "s",
        Config.despawnSeconds == 0 ? " (never)" : "");
}

// ---------------------------------------------------------------------------
// Console
// ---------------------------------------------------------------------------

auto StateText(InstallState state) noexcept -> const char* {
    switch (state) {
    case InstallState::Installed:        return "active";
    case InstallState::DisabledByConfig: return "off in the config file";
    case InstallState::ConfigError:      return "NOT ACTIVE, the config could not be used, see the log";
    case InstallState::UnsupportedBuild: return "NOT ACTIVE, unrecognised game build or already patched, see the log";
    case InstallState::NoTables:         return "NOT ACTIVE, data table services unavailable, see the log";
    case InstallState::InstallFailed:    return "NOT ACTIVE, the hook failed, see the log";
    case InstallState::NotAttempted:
    default:                             return "NOT ACTIVE, never attempted";
    }
}

auto __cdecl StatusCommand(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command, void*) noexcept
        -> D2RL::ConsoleCommandResult {
    if (command == nullptr || command->plugin == nullptr) return D2RL::ConsoleCommandResult::Failed;
    const D2RL::PluginContext* plugin = command->plugin;
    char line[512]{};

    std::snprintf(line, sizeof(line), "Treasure Goblin: %s | config %s | ai_name \"%s\" | escape after %d s%s",
        StateText(State.load(std::memory_order_acquire)), ConfigFileWasRead ? "loaded" : "NOT READ (built-in defaults)",
        Config.aiName.c_str(), Config.despawnSeconds, Config.despawnSeconds == 0 ? " (never)" : "");
    plugin->WriteConsoleMessage(line);

    const Resolution* resolution = Current.load(std::memory_order_acquire);
    if (resolution == nullptr) {
        plugin->WriteConsoleMessage("tables: not resolved yet");
    } else {
        for (std::size_t index = 0; index < BankCount; ++index) {
            const BankBinding& bank = resolution->banks[index];
            if (!bank.loaded) continue;
            std::snprintf(line, sizeof(line), "%s: %s", BankNames[index], bank.status.c_str());
            plugin->WriteConsoleMessage(line);
        }
    }

    std::lock_guard lock(StateLock);
    const std::uint32_t now = LastFrame.load(std::memory_order_relaxed);
    const std::uint32_t despawn = DespawnFrames.load(std::memory_order_relaxed);
    std::snprintf(line, sizeof(line), "goblins running: %zu | escapes %llu, portals %llu, left %llu", Goblins.size(),
        static_cast<unsigned long long>(Escapes.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(PortalsOpened.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(Departures.load(std::memory_order_relaxed)));
    plugin->WriteConsoleMessage(line);

    std::size_t shown = 0;
    for (const auto& [guid, s] : Goblins) {
        if (++shown > 8) {
            plugin->WriteConsoleMessage("...");
            break;
        }
        const double alive = now >= s.firstFrame ? static_cast<double>(now - s.firstFrame) / FramesPerSecond : 0.0;
        if (s.phase == Phase::Escaping) {
            const double left = s.escapeEnd > now ? static_cast<double>(s.escapeEnd - now) / FramesPerSecond : 0.0;
            std::snprintf(line, sizeof(line), "goblin %u: escaping, portal %s, gone in %.1f s", guid,
                s.portalGuid == NoGuid ? "could not open" : "open", left);
        } else if (despawn == 0) {
            std::snprintf(line, sizeof(line), "goblin %u: fleeing for %.1f s, no escape timer, %u stuck turn%s", guid,
                alive, s.stuckTurns, s.stuckTurns == 1 ? "" : "s");
        } else {
            const double left = static_cast<double>(despawn) / FramesPerSecond - alive;
            std::snprintf(line, sizeof(line), "goblin %u: fleeing for %.1f s, escapes in %.1f s, %u stuck turn%s",
                guid, alive, left > 0.0 ? left : 0.0, s.stuckTurns, s.stuckTurns == 1 ? "" : "s");
        }
        plugin->WriteConsoleMessage(line);
    }
    return D2RL::ConsoleCommandResult::Handled;
}

constexpr D2RL::PluginInfo Info{
    .infoSize    = D2RL::PluginInfoSize,
    .abiVersion  = D2RL_PLUGIN_ABI_VERSION,
    .id          = "celestialrayone.treasure-goblin",
    .name        = "Treasure Goblin",
    .version     = "1.0.0",
    .author      = "CelestialRayOne",
    .description = "TreasureGoblin monster AI (monai.txt row): always flees and escapes corners, then leaves "
                   "through a portal after a set time without dropping loot.",
    // Server: monster AI only runs on the server.
    .flags       = D2RL::PluginFlags::Server | D2RL::PluginFlags::NativeHooks,
};

static_assert(D2RL::HasValidPluginRole(Info.flags), "Exactly one execution role must be set.");
static_assert(D2RL::HasOnlyKnownPluginFlags(Info.flags), "Unknown plugin flag set.");
static_assert(sizeof(AiRecord) == 0x20, "The AI record must match the game's 0x20-byte layout.");

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
    if (!Context->RegisterConsoleCommand("treasuregoblin", &StatusCommand,
            "Reports Treasure Goblin status, the monai.txt row found and the goblins being run.")) {
        Context->LogWarn("TreasureGoblin: the status console command was refused.");
    }

    const bool configUsable = ReadConfiguration();
    Install(configUsable);
    return true;
}

// The loader removes the inline hook when the plugin unloads.
D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
    if (LifecycleApi != nullptr && TablesListener != 0) {
        LifecycleApi->unregisterDataTablesLoadedListener(Context, TablesListener);
        TablesListener = 0;
    }
}

}  // namespace CelestialRayOne::TreasureGoblin
