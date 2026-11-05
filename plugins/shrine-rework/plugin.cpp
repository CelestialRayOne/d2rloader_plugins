// Shrine Rework
//
// One plugin, three parts with their own switches in the config:
//   Removal  takes a configurable list of shrine types (shrines.txt Code) out
//            of the game. Shrine objects draw from the same pools minus the
//            removed shrines, so the remaining shrines keep their relative
//            odds, and pools that removal empties fall back to another
//            effectclass (Refilling by default).
//   Effects  Armor: a percent less damage taken. Combat: configurable stats.
//            Resists: maximum resistance on top. Skill: +all skills by
//            difficulty. Mana Recharge: percent of max life and mana per
//            second.
//   Sharing  a clicked shrine also reaches every other player and every
//            hireling and summon within a configurable radius of it, 40
//            yards by default.
//
// D2R 3.3 on D2RLoader 1.3.1. Everything below was read out of the live
// D2RLoader.exe image and disassembled before being relied on. Every game
// address the plugin calls, hooks or patches is fingerprinted at load; a part
// whose fingerprints do not match is left out and the other part still runs.
//
// ===========================================================================
// Part 1: Removal
// ===========================================================================
//
// ---------------------------------------------------------------------------
// shrines.txt as the 3.3 loader compiles it (0x391E60, record 0x1C bytes)
// ---------------------------------------------------------------------------
//   +00 Code (byte)          +04 Arg0            +08 Arg1
//   +0C Duration in frames   +10 reset time in minutes (byte)
//   +12 StringName           +14 StringPhrase    +16 effectclass (byte)
//   +18 LevelMin
//   The loader has no rarity column. Code selects the effect: the shrine
//   operate function (objects.txt OperateFn 2, 0x58F2A0) indexes the 24-slot
//   chart at 0x1D3F1D0 with it. So Code is what identifies a shrine type.
//
// ---------------------------------------------------------------------------
// Where the type is chosen: objects.txt InitFn 1 = 0x50E450 (hooked)
// ---------------------------------------------------------------------------
//   50E463  8B B8 34 01 00 00     mov  edi, [rax+134h]       ; objects.txt Parm0
//   50E469  E8 A2 17 DE FF        call 2EFC10                ; level id of the room
//
//   Parm0 == 0, the whole table (50E4E8..50E58E):
//     up to 8 draws of row = 1 + roll(count - 1) on the object region seed
//     [ctx+18h]. It stops at the first row whose LevelMin [rec+18h] is not
//     above the level id (unsigned, 50E584 cmp / jae), otherwise it keeps
//     the 8th draw.
//
//   Parm0 != 0, an effectclass pool (50E47A..50E4E3):
//     Parm0 1 -> class 2, Parm0 2 -> class 3, any other value -> class 4,
//     or class 1 when a region-seed roll is a multiple of 10 (50E4A8..50E4BD).
//     The picker 0x512050 then draws up to 8 times from that class list on
//     the object control seed with the same LevelMin rule. A list entry of 0
//     reads as row 1 (512119 cmove ebx, ebp).
//
//   Then (50E59A..50E5C3) the drawn row is remapped: everything becomes
//   row 17 while the byte at 0x2AAF4B8 is set (only toggled by the 15-byte
//   0x510AC0), otherwise 4 -> 2, 5 -> 3, 16 -> 18. That remap is why the
//   Health/Mana Exchange and Enirhs shrines never appear. The final row goes
//   to ObjectData.InteractType (0x34E9D0) and its record to ObjectData
//   (0x38FE40, tail jump to 0x34EE70).
//
//   The class lists are built per game by 0x5125A0 from effectclass
//   [rec+16h]: every row with effectclass 0..7 is appended to
//   list[effectclass] in row order. Object control is [Game+21D0h]: seed
//   pair at +0, int32 counts[8] at +8, int32* lists[8] at +28h. 0x512BE0
//   returns the object control (its seed) and 0x512DA0 returns +8.
//
//   The picker's only other caller is the healing well spawner 0x50CF10.
//   When a level still needs a healing well and the shrine it just placed
//   is not row 2, it turns the object into a HealingWell, calls the picker
//   for class 2 only for its seed side effect, and hard-sets row 2:
//     50D10B  48 8B CE              mov  rcx, rsi              ; game
//     50D10E  41 B8 01 00 00 00     mov  r8d, 1
//     50D114  B2 02                 mov  dl, 2
//     50D116  E8 35 4F 00 00        call 512050                ; result unused
//     50D11B  B2 02                 mov  dl, 2
//     50D11D  48 8B CB              mov  rcx, rbx              ; the object
//     50D120  E8 AB 18 E4 FF        call 34E9D0                ; InteractType = 2
//     50D125  0F B6 8E 06 01 00 00  movzx ecx, byte [rsi+106h]
//     50D12C  BA 02 00 00 00        mov  edx, 2
//     50D131  E8 0A 2D E8 FF        call 38FE40                ; record of row 2
//     50D136  48 8B D0              mov  rdx, rax
//     50D139  48 8B CB              mov  rcx, rbx
//     50D13C  E8 2F 1D E4 FF        call 34EE70
//     50D141  8B D5                 mov  edx, ebp              ; healing count
//   50D11B..50D140 (38 bytes) becomes mov rcx, rsi / mov rdx, rbx / call
//   into a near relay to ForcedHealingWell, then NOPs. rbx, rsi, rbp and
//   r14 are nonvolatile and 50D141 reads only those, and no branch lands
//   inside the window. ForcedHealingWell keeps row 2 unless its shrine is
//   removed, and otherwise draws from class 2 with the same rules as above,
//   so a healing well never carries a removed shrine.
//
//   When removal empties the pool an object draws from (Health and Mana
//   wells draw only from effectclass 2 and 3), the object draws from the
//   fallback effectclass instead (default 1, Refilling).
//
// ---------------------------------------------------------------------------
// The hook
// ---------------------------------------------------------------------------
//   0x50E450 is replaced by SelectRow plus the original tail. SelectRow is
//   that function with one difference: a pool only holds rows whose FINAL
//   shrine (after the remap above) keeps its Code. The draw count, the seeds
//   and how much of them each step consumes, the LevelMin rule, the remap
//   and the three native calls at the end are those of the original. With
//   nothing removed it returns the same row and leaves both seeds in the
//   same state as the original machine code (checked offline: the original
//   0x50E450 and 0x512050 bytes run under emulation against this code).
//
//   The kept rows are counted and indexed in place, so pools have no size
//   limit. When removal empties an effectclass pool, the draw moves to the
//   fallback effectclass. When nothing is left there either, that object
//   gets the original draw over the full pool (same seeds, same result as
//   without the plugin) and the plugin log says so. When the game's own pool
//   is already empty, the result is the original's (row 1).
//
//   The whole 0x50E450 body and the whole picker body are fingerprinted
//   before anything is installed, so a build where either differs is left
//   untouched.
//
// ===========================================================================
// Part 2: Effects
// ===========================================================================
//
// ---------------------------------------------------------------------------
// How a shrine applies its effect
// ---------------------------------------------------------------------------
//   objects.txt OperateFn 2 = 0x58F2A0 calls chart[Code].handler through the
//   24-slot chart at 0x1D3F1D0 (16 bytes: handler, stat id, state id), with
//   (dataContext, operate, shrine record). operate: +00 game, +08 the shrine
//   object, +10 the player. Record: +00 Code, +04 Arg0, +08 Arg1, +0C Duration
//   in frames.
//
//   Chart (stat, state): 6 Armor (171 skill_armor_percent, 128), 7 Combat
//   (-1, 129), 8 Fire (39, 131), 9 Cold (43, 132), 10 Lightning (41, 130),
//   11 Poison (45, 133), 12 Skill (11, 134), 13 Mana Recharge (27, 135).
//
//   Handlers:
//     0x591E20  Armor, the four resists, Mana Recharge, Experience: value =
//               0x594DA0(context, record, stat, Arg0, player), then one timed
//               state with that stat.
//     0x591F70  Combat: damagepercent 25 = Arg1, then tohit 19 = Arg0 % of
//               the AR snapshot from 0x593910.
//     0x591F00  Skill: state 134 with a dummy stat (11 = 0) and the remove
//               callback 0x594FB0, then the skill refresh 0x438970. The +2
//               itself is in the skill-level code, which D2RLoader replaced
//               (0x33D1E0 / 0x3400A0 jump to D2RCore), keyed on state 134.
//
//   The timed state is 0x433D20, a thunk to D2RCore!ApplyWideTimedStatEffect,
//   StatList* (args). The 30h args record, as the handlers fill it:
//     +00 source (object)  +08 target (player)  +10 skill 0  +14 level 0
//     +18 duration  +1C stat (-1 = none)  +20 value  +24 state  +28 remove cb
//   Extra stats go on the returned list through 0x2F3030, a thunk to
//   SetWideListStat (context, list, stat, value, layer), as the Combat
//   handler does for tohit.
//
// ---------------------------------------------------------------------------
// Armor: damage taken (SUNITDMG_CalculateTotalDamage 0x44DF10)
// ---------------------------------------------------------------------------
//   The function first applies the DifficultyLevels damage percent to
//   physical, fire, lightning, cold, magic, poison and the life, mana and
//   stamina drains, then fires absorbdamage (event 11), then resistances,
//   then sums the life total:
//     44EB25  48 8B 4C 24 30        mov  rcx, [rsp+30h]      ; game
//     44EB2A  4D 8B CD              mov  r9, r13             ; attacker
//     44EB2D  4C 8B C6              mov  r8, rsi             ; defender
//     44EB30  4C 89 7C 24 20        mov  [rsp+20h], r15      ; D2Damage
//     44EB35  BA 0B 00 00 00        mov  edx, 0Bh
//     44EB3A  E8 31 EA FF FF        call 44D570              ; event 11
//   Only the rel32 of that call changes. It lands in a near relay that jumps
//   to HookedUnitEvent, which scales the hit when the defender has the Armor
//   state and then makes the original call. So the reduction sits next to
//   the game's own all-damage percent, before absorbs and resistances.
//
//   D2Damage: +18 Phys, +20 Fire, +24 Burn, +28 Burn length, +2C Ligt,
//   +30 Magc, +34 Cold, +38 Pois, +3C Pois length, +40/+48 per-source poison
//   (12-byte entries: id, damage per frame, length; 44D1CF), +120/+124/+128
//   Life/Mana/Stam drain, +134 life total. The commit (44D145..) applies each
//   source, then the rest as Pois - sum(sources), so both are scaled.
//   Poison, burning and bleed (event 16, after resistances) are all derived
//   from these values, so they are reduced as well. The poison immunity and
//   bleed plugins touch 44DF10, 44EC8C and 44ECA5, none of which this plugin
//   writes.
//
// ---------------------------------------------------------------------------
// Mana Recharge: regeneration
// ---------------------------------------------------------------------------
//   Player stat regen 0x42E600 runs every frame (reschedules at frame+1):
//   life += hpregen (stat 74); mana (0x42E350) += manarecovery (stat 26) +
//   the charstats base. Both are 1/256 of a point per frame, so a percent of
//   the maximum per second is max * percent / 2500 per frame.
//
// ---------------------------------------------------------------------------
// The hooks
// ---------------------------------------------------------------------------
//   0x591E20, 0x591F00 and 0x591F70 are replaced by the functions below,
//   which build the same args record the originals build and call the same
//   natives, plus the extra stats. A shrine or a part that is off calls the
//   original. Every handler body is fingerprinted whole before installing.
//   The natives they call are proven through those verified bodies (the
//   call, jmp or lea that reaches them), never by their own entry bytes,
//   because other plugins may hook those: damage-overflow-fix's
//   stat_write_watch rewrites the 0x2F3030 thunk, and the first version of
//   this plugin refused to load its effects because of it. Each part checks
//   only what it uses, so one mismatch leaves the other parts running.
//
// ===========================================================================
// Part 3: Sharing
// ===========================================================================
//
// ---------------------------------------------------------------------------
// Where a shrine is used: objects.txt OperateFn 2 = 0x58F2A0 (hooked)
// ---------------------------------------------------------------------------
//   Both object dispatchers (0x593630 for a click or Telekinesis, 0x592C40)
//   build the same operate record and pass it in rcx:
//     +00 Game*  +08 the shrine  +10 the player  +18 object control
//     +20 objects.txt row (int32)
//   0x58F2A0 reads it as:
//     58F2BC  call 34AB60             ; shrine mode, not 0: return 0 (used)
//     58F3AD  call 34B520             ; rsi = the shrine's shrines.txt record
//     58F432  movzx eax, byte [rsi]   ; Code
//     58F435  cmp al, 18h / jae 58F461
//     58F43C  lea r9, [1D3F1D0] / mov r9, [r9+rax*8] (rax = Code*2)
//     58F447  test r9, r9 / je 58F461
//     58F44F  r8 = shrines.txt record, rdx = operate record,
//             ecx = byte [Game+106h]
//     58F45C  call r9                 ; the effect, on +10 only
//     58F461  no handler: life += max life - life, mana += max mana - mana
//             (2F5020, 2F4D20, 2F34F0, then 2F5020, 2F4E20, 2F34F0)
//     58F540  mov eax, 1
//   The hook runs the original and, when it returned 1, calls the same chart
//   handler once more for every other unit in range, with a copy of the
//   record whose +10 is that unit. Codes without a handler share through the
//   Refilling handler 0x590DB0, which makes the same six calls on the same
//   stats (proven at compile time below). Because the calls go through the
//   chart, the Effects hooks on 0x591E20 / 0x591F00 / 0x591F70 apply to
//   everyone exactly as they do to the clicker.
//   The hook displaces 58F2A0..58F2A5 (push rdi / sub rsp,70h); the next
//   instruction is the RIP-relative security cookie load.
//
// ---------------------------------------------------------------------------
// Who is in range
// ---------------------------------------------------------------------------
//   Players: 0x490D00 (game, callback, arg) walks the 128 player buckets at
//   Game+2230h (next at unit+158h) and calls callback(game, player, arg) for
//   every player without state 7 (playerbody) or 186 (shared stash proxy).
//   Hirelings and summons: 0x4FF990 (game, player, callback, arg, all) walks
//   the pet lists of a player (qword +98h of what 0x34B240 returns for it,
//   pettype 1 and up) through 0x5014C0, which resolves each pet with
//   SUNIT_GetServerUnit 0x48FE80 (game, 1, guid) and calls
//   callback(game, player, pet, arg). all = 0 skips list nodes with flag 1,
//   as the healing well does when it heals pets (call at 0x594B37).
//   A unit is in range when it is alive (0x34C2C0), in the shrine's act
//   (room 0x34B440, level 0x2EFC10, act 0x326710) and within the radius.
//   Positions are read the way the Fire shrine reads them (59127C..5912F5):
//   unit+38h is the path; objects, items and tiles (unit types 2, 4, 5) keep
//   x and y at path+10h / +14h, players and monsters go through PATH_GetX
//   0x341A20 / PATH_GetY 0x341A30 (words at path+2 / +6). The radius test
//   has the game's own shape, dx*dx + dy*dy against radius*radius in game
//   units, as the area scan 0x4398B0 applies it through 0x3412C0.
//
// ---------------------------------------------------------------------------
// Yards
// ---------------------------------------------------------------------------
//   A yard is 1.5 game units (subtiles). skilldesc.txt prints radii in yards
//   as game units * 2 / 3: Inner Sight's "Radius: %d yards" line (descline
//   36, StrSkill18) is rng*2 over 3, rng being its aurarangecalc. The test
//   4 * (dx*dx + dy*dy) <= 9 * yards * yards is exactly that, unrounded.
//
// ---------------------------------------------------------------------------
// Who gets which shrine
// ---------------------------------------------------------------------------
//   Everyone (players, hirelings, summons): Codes 1, 2, 3, 6 to 13, 15 and
//   the Codes without a handler. Their handlers only touch the unit at +10:
//   its stats, a timed state through ApplyWideTimedStatEffect, or the skill
//   refresh 0x438970, which checks the unit type (438983) before its
//   player-only pet step.
//   Players only: 4 and 5 trade the unit's own life and mana, 14 is stamina,
//   16 (Enirhs) rewrites the PlayerData name, 18 (Gem) reads the backpack
//   and drops a gem next to the unit.
//   The clicker only: 17 Portal, 19 Fire, 20 Monster, 21 Exploding and
//   22 Poison act on the area around the shrine, not on the unit at +10.
//   The 24 handler slots of the chart are checked against these functions
//   at load; a different chart leaves this part out.
//
// ===========================================================================
// Shared
// ===========================================================================
//   Both patched call sites (the healing well window at 0x50D11B and the
//   absorbdamage call at 0x44EB3A) need a rel32 target, so both land in one
//   page allocated next to the image holding two FF 25 jumps, one into
//   ForcedHealingWell and one into HookedUnitEvent. The page is written once,
//   made read-execute and never freed; D2RLoader restores both sites and
//   removes the inline hooks when the plugin unloads.

#include <D2RLPlugin/api.h>

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
#include <vector>

namespace CelestialRayOne::ShrineRework {
namespace {

// ---------------------------------------------------------------------------
// Native anchors
// ---------------------------------------------------------------------------

constexpr std::uint64_t ShrineInitRva       = 0x50E450;   // objects.txt InitFn 1, hooked
constexpr std::uint64_t ClassPickRva        = 0x512050;   // effectclass picker, mirrored
constexpr std::uint64_t LevelIdFromRoomRva  = 0x2EFC10;
constexpr std::uint64_t ShrineRowCountRva   = 0x38FE00;
constexpr std::uint64_t ShrineRecordRva     = 0x38FE40;
constexpr std::uint64_t SetInteractTypeRva  = 0x34E9D0;
constexpr std::uint64_t SetShrineRecordRva  = 0x34EE70;
constexpr std::uint64_t ObjectControlRva    = 0x512BE0;
constexpr std::uint64_t ShrineClassListsRva = 0x512DA0;
constexpr std::uint64_t ForcePortalFlagRva  = 0x2AAF4B8;
constexpr std::uint64_t HealingWellRva      = 0x50D105;   // witness 50D105..50D145
constexpr std::uint64_t HealingWellPatchRva = 0x50D11B;   // 38-byte window, replaced

// 50E450..50E5F6, the whole original. It is reproduced below, so all of it
// has to match, not only the hooked prologue.
constexpr std::uint8_t ShrineInitBody[]{
    0x40, 0x53, 0x55, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0x41, 0x20,
    0x48, 0x8B, 0xD9, 0x48, 0x8B, 0x49, 0x10, 0x8B, 0xB8, 0x34, 0x01, 0x00,
    0x00, 0xE8, 0xA2, 0x17, 0xDE, 0xFF, 0x44, 0x8B, 0xD8, 0xBD, 0x03, 0x00,
    0x00, 0x00, 0x85, 0xFF, 0x74, 0x6E, 0x83, 0xEF, 0x01, 0x74, 0x49, 0x83,
    0xFF, 0x01, 0x74, 0x3F, 0x48, 0x8B, 0x4B, 0x18, 0x44, 0x8D, 0x55, 0x01,
    0x8B, 0x01, 0x4C, 0x69, 0xC0, 0xC5, 0x90, 0xC6, 0x6A, 0x8B, 0x41, 0x04,
    0x4C, 0x03, 0xC0, 0x49, 0x8B, 0xC0, 0x44, 0x89, 0x01, 0x48, 0xC1, 0xE8,
    0x20, 0x89, 0x41, 0x04, 0xB8, 0xCD, 0xCC, 0xCC, 0xCC, 0x41, 0xF7, 0xE0,
    0xC1, 0xEA, 0x03, 0x8D, 0x04, 0x92, 0x03, 0xC0, 0x44, 0x3B, 0xC0, 0x75,
    0x11, 0x44, 0x8D, 0x55, 0xFE, 0xEB, 0x0B, 0x44, 0x8B, 0xD5, 0xEB, 0x06,
    0x41, 0xBA, 0x02, 0x00, 0x00, 0x00, 0x4C, 0x8B, 0x4B, 0x10, 0x45, 0x8B,
    0xC3, 0x48, 0x8B, 0x0B, 0x41, 0x0F, 0xB6, 0xD2, 0xE8, 0x6F, 0x3B, 0x00,
    0x00, 0x8B, 0xF8, 0xE9, 0xB2, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x4B, 0x10,
    0x48, 0x89, 0x74, 0x24, 0x48, 0xBE, 0x08, 0x00, 0x00, 0x00, 0x4C, 0x89,
    0x74, 0x24, 0x50, 0xE8, 0x10, 0x17, 0xDE, 0xFF, 0x44, 0x8B, 0xF0, 0x0F,
    0x1F, 0x40, 0x00, 0x66, 0x0F, 0x1F, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x48, 0x8B, 0x0B, 0x0F, 0xB6, 0x89, 0x06, 0x01, 0x00, 0x00, 0xE8, 0xE1,
    0x18, 0xE8, 0xFF, 0x48, 0x8B, 0x53, 0x18, 0x44, 0x8D, 0x48, 0xFF, 0x45,
    0x85, 0xC9, 0x7F, 0x04, 0x33, 0xD2, 0xEB, 0x40, 0x8B, 0x02, 0x8B, 0x4A,
    0x04, 0x4C, 0x69, 0xC0, 0xC5, 0x90, 0xC6, 0x6A, 0x4C, 0x03, 0xC1, 0x41,
    0x8D, 0x49, 0xFF, 0x44, 0x89, 0x02, 0x41, 0x85, 0xC9, 0x74, 0x14, 0x49,
    0x8B, 0xC8, 0x41, 0x8B, 0xC0, 0x48, 0xC1, 0xE9, 0x20, 0x89, 0x4A, 0x04,
    0x33, 0xD2, 0x41, 0xF7, 0xF1, 0xEB, 0x11, 0x49, 0x8B, 0xC0, 0x48, 0xC1,
    0xE8, 0x20, 0x89, 0x42, 0x04, 0x41, 0x8D, 0x51, 0xFF, 0x41, 0x23, 0xD0,
    0x48, 0x8B, 0x03, 0x8D, 0x7A, 0x01, 0x8B, 0xD7, 0x0F, 0xB6, 0x88, 0x06,
    0x01, 0x00, 0x00, 0xE8, 0xBC, 0x18, 0xE8, 0xFF, 0x44, 0x3B, 0x70, 0x18,
    0x73, 0x06, 0xFF, 0xCE, 0x85, 0xF6, 0x7F, 0x80, 0x48, 0x8B, 0x74, 0x24,
    0x48, 0x4C, 0x8B, 0x74, 0x24, 0x50, 0x80, 0x3D, 0x17, 0x0F, 0x5A, 0x02,
    0x00, 0x74, 0x07, 0xBD, 0x11, 0x00, 0x00, 0x00, 0xEB, 0x1C, 0x83, 0xFF,
    0x05, 0x74, 0x17, 0x83, 0xFF, 0x04, 0x75, 0x05, 0x8D, 0x6F, 0xFE, 0xEB,
    0x0D, 0x83, 0xFF, 0x10, 0x8B, 0xEF, 0xB8, 0x12, 0x00, 0x00, 0x00, 0x0F,
    0x44, 0xE8, 0x48, 0x8B, 0x4B, 0x08, 0x40, 0x0F, 0xB6, 0xD5, 0xE8, 0xFD,
    0x03, 0xE4, 0xFF, 0x48, 0x8B, 0x03, 0x8B, 0xD5, 0x0F, 0xB6, 0x88, 0x06,
    0x01, 0x00, 0x00, 0xE8, 0x5C, 0x18, 0xE8, 0xFF, 0x48, 0x8B, 0x4B, 0x08,
    0x48, 0x8B, 0xD0, 0x48, 0x83, 0xC4, 0x20, 0x5F, 0x5D, 0x5B, 0xE9, 0x79,
    0x08, 0xE4, 0xFF,
};
// The bytes handed to the loader: the prologue up to the first call
// (50E450..50E469), no branch and no RIP-relative operand in it.
constexpr std::uint32_t HookWitnessSize = 25;

// 512050..512177, the effectclass picker, mirrored by SelectRow.
constexpr std::uint8_t ClassPickBody[]{
    0x48, 0x89, 0x5C, 0x24, 0x20, 0x55, 0x56, 0x41, 0x55, 0x41, 0x56, 0x41,
    0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xF1, 0x0F, 0xB6, 0xDA, 0x8D,
    0x42, 0xFF, 0xB9, 0x02, 0x00, 0x00, 0x00, 0x3C, 0x03, 0x4D, 0x8B, 0xF9,
    0x45, 0x8B, 0xE8, 0x0F, 0x47, 0xD9, 0x48, 0x8B, 0xCE, 0xE8, 0x1E, 0x0D,
    0x00, 0x00, 0x0F, 0xB6, 0xCB, 0x83, 0x3C, 0x88, 0x00, 0x4C, 0x8D, 0x34,
    0x88, 0x0F, 0x84, 0xBA, 0x00, 0x00, 0x00, 0x48, 0x89, 0x7C, 0x24, 0x50,
    0xBF, 0x08, 0x00, 0x00, 0x00, 0x4C, 0x89, 0x64, 0x24, 0x60, 0x4C, 0x8D,
    0x24, 0xC8, 0x8D, 0x6F, 0xF9, 0x0F, 0x1F, 0x80, 0x00, 0x00, 0x00, 0x00,
    0x41, 0x8B, 0x1E, 0x49, 0x8B, 0xD7, 0x48, 0x8B, 0xCE, 0xE8, 0x22, 0x0B,
    0x00, 0x00, 0x48, 0x8B, 0xD0, 0x85, 0xDB, 0x7F, 0x04, 0x33, 0xD2, 0xEB,
    0x3C, 0x8B, 0x00, 0x8B, 0x4A, 0x04, 0x4C, 0x69, 0xC0, 0xC5, 0x90, 0xC6,
    0x6A, 0x4C, 0x03, 0xC1, 0x8D, 0x4B, 0xFF, 0x44, 0x89, 0x02, 0x85, 0xCB,
    0x74, 0x13, 0x49, 0x8B, 0xC8, 0x41, 0x8B, 0xC0, 0x48, 0xC1, 0xE9, 0x20,
    0x89, 0x4A, 0x04, 0x33, 0xD2, 0xF7, 0xF3, 0xEB, 0x10, 0x49, 0x8B, 0xC0,
    0x48, 0xC1, 0xE8, 0x20, 0x89, 0x42, 0x04, 0x8D, 0x53, 0xFF, 0x41, 0x23,
    0xD0, 0x49, 0x8B, 0x44, 0x24, 0x20, 0x48, 0x63, 0xCA, 0x8B, 0x1C, 0x88,
    0x85, 0xDB, 0x0F, 0xB6, 0x8E, 0x06, 0x01, 0x00, 0x00, 0x0F, 0x44, 0xDD,
    0x8B, 0xD3, 0xE8, 0x1D, 0xDD, 0xE7, 0xFF, 0x44, 0x3B, 0x68, 0x18, 0x73,
    0x06, 0xFF, 0xCF, 0x85, 0xFF, 0x7F, 0x81, 0x4C, 0x8B, 0x64, 0x24, 0x60,
    0x8B, 0xC3, 0x48, 0x8B, 0x7C, 0x24, 0x50, 0x48, 0x8B, 0x5C, 0x24, 0x68,
    0x48, 0x83, 0xC4, 0x20, 0x41, 0x5F, 0x41, 0x5E, 0x41, 0x5D, 0x5E, 0x5D,
    0xC3, 0x48, 0x8D, 0x4C, 0x24, 0x58, 0xC6, 0x44, 0x24, 0x58, 0x00, 0xE8,
    0x94, 0xE0, 0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x01, 0xCC, 0x48, 0x8B, 0x5C,
    0x24, 0x68, 0xB8, 0x01, 0x00, 0x00, 0x00, 0x48, 0x83, 0xC4, 0x20, 0x41,
    0x5F, 0x41, 0x5E, 0x41, 0x5D, 0x5E, 0x5D, 0xC3,
};

// 2EFC10: test rcx, rcx / jne / xor eax, eax / ret / mov rcx, [rcx+18h] / jmp.
constexpr std::uint8_t LevelIdFromRoomBody[]{
    0x48, 0x85, 0xC9, 0x75, 0x03, 0x33, 0xC0, 0xC3, 0x48, 0x8B, 0x49, 0x18,
    0xE9, 0x9F, 0x13, 0x07, 0x00,
};

// 38FE00..38FE34: data tables, then the shrines.txt count at +19B8h.
constexpr std::uint8_t ShrineRowCountBody[]{
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0xE8, 0x85, 0x0C, 0xF7, 0xFF, 0x48,
    0x8B, 0x98, 0xB8, 0x19, 0x00, 0x00, 0x8B, 0xCB, 0x48, 0x3B, 0xCB, 0x74,
    0x14, 0x48, 0x8D, 0x4C, 0x24, 0x38, 0xC6, 0x44, 0x24, 0x38, 0x00, 0xE8,
    0x08, 0xE4, 0xD1, 0xFF, 0x84, 0xC0, 0x74, 0x01, 0xCC, 0x8B, 0xC3, 0x48,
    0x83, 0xC4, 0x20, 0x5B, 0xC3,
};

// 38FE40: bounds check against +19B8h, base at +19B0h, stride 1Ch after it.
constexpr std::uint8_t ShrineRecordEntry[]{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57, 0x48,
    0x83, 0xEC, 0x30, 0x8B, 0xDA, 0xE8, 0x3A, 0x0C, 0xF7, 0xFF, 0x48, 0x8B,
    0xF0, 0x8B, 0xFB, 0x48, 0x3B, 0x98, 0xB8, 0x19, 0x00, 0x00, 0x72, 0x14,
    0x48, 0x8D, 0x4C, 0x24, 0x48, 0xC6, 0x44, 0x24, 0x48, 0x00, 0xE8, 0x1D,
    0xF6, 0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x01, 0xCC, 0x48, 0x8D, 0x9E, 0xB0,
    0x19, 0x00, 0x00,
};

// 34E9D0..34EA19. The tail from +4Ah is D2RLoader's own relay and is not
// part of the witness.
constexpr std::uint8_t SetInteractTypeEntry[]{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x0F, 0xB6,
    0xFA, 0x48, 0x8B, 0xD9, 0x48, 0x85, 0xC9, 0x75, 0x13, 0x88, 0x4C, 0x24,
    0x38, 0x48, 0x8D, 0x4C, 0x24, 0x38, 0xE8, 0x5D, 0x6E, 0xFF, 0xFF, 0x84,
    0xC0, 0x74, 0x01, 0xCC, 0x83, 0x3B, 0x02, 0x74, 0x14, 0x48, 0x8D, 0x4C,
    0x24, 0x38, 0xC6, 0x44, 0x24, 0x38, 0x00, 0xE8, 0x14, 0x61, 0xFF, 0xFF,
    0x84, 0xC0, 0x74, 0x01, 0xCC, 0x48, 0x8B, 0x43, 0x10, 0x48, 0x8B, 0x5C,
    0x24, 0x30,
};

// 34EE70..34EEAF: stores the record at ObjectData+10h for unit type 2.
constexpr std::uint8_t SetShrineRecordBody[]{
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B,
    0xFA, 0x48, 0x8B, 0xD9, 0x48, 0x85, 0xC9, 0x75, 0x13, 0x88, 0x4C, 0x24,
    0x30, 0x48, 0x8D, 0x4C, 0x24, 0x30, 0xE8, 0x4D, 0x73, 0xFF, 0xFF, 0x84,
    0xC0, 0x74, 0x01, 0xCC, 0x83, 0x3B, 0x02, 0x75, 0x13, 0x48, 0x8B, 0x43,
    0x10, 0x48, 0x8B, 0x5C, 0x24, 0x38, 0x48, 0x89, 0x78, 0x10, 0x48, 0x83,
    0xC4, 0x20, 0x5F, 0xC3,
};

// 512BE0: mov rax, [rcx+21D0h] / ret.
constexpr std::uint8_t ObjectControlBody[]{
    0x48, 0x8B, 0x81, 0xD0, 0x21, 0x00, 0x00, 0xC3,
};

// 512DA0: mov rax, [rcx+21D0h] / add rax, 8 / ret.
constexpr std::uint8_t ShrineClassListsBody[]{
    0x48, 0x8B, 0x81, 0xD0, 0x21, 0x00, 0x00, 0x48, 0x83, 0xC0, 0x08, 0xC3,
};

// 50D105..50D145: class id store, the picker call, the forced row 2 and the
// healing count call that follows.
constexpr std::uint8_t HealingWellBody[]{
    0x89, 0x4B, 0x04, 0x4D, 0x8B, 0xCE, 0x48, 0x8B, 0xCE, 0x41, 0xB8, 0x01,
    0x00, 0x00, 0x00, 0xB2, 0x02, 0xE8, 0x35, 0x4F, 0x00, 0x00, 0xB2, 0x02,
    0x48, 0x8B, 0xCB, 0xE8, 0xAB, 0x18, 0xE4, 0xFF, 0x0F, 0xB6, 0x8E, 0x06,
    0x01, 0x00, 0x00, 0xBA, 0x02, 0x00, 0x00, 0x00, 0xE8, 0x0A, 0x2D, 0xE8,
    0xFF, 0x48, 0x8B, 0xD0, 0x48, 0x8B, 0xCB, 0xE8, 0x2F, 0x1D, 0xE4, 0xFF,
    0x8B, 0xD5, 0x48, 0x8B, 0xCE,
};
constexpr std::size_t   HealingWellPatchOffset = HealingWellPatchRva - HealingWellRva;
constexpr std::uint32_t HealingWellPatchSize   = 38;
constexpr std::uint32_t CallSize               = 5;

static_assert(sizeof(ShrineInitBody) == 0x1A7);
static_assert(sizeof(ClassPickBody) == 0x128);
static_assert(HookWitnessSize <= sizeof(ShrineInitBody));
static_assert(ShrineInitBody[0] == 0x40 && ShrineInitBody[1] == 0x53 && ShrineInitBody[25] == 0xE8);

// Offsets inside the InitFn context the original reads.
constexpr std::size_t InitGameOffset       = 0x00;    // Game*
constexpr std::size_t InitObjectOffset     = 0x08;    // the shrine object
constexpr std::size_t InitRoomOffset       = 0x10;    // its room
constexpr std::size_t InitSeedOffset       = 0x18;    // object region seed pair
constexpr std::size_t InitObjectsTxtOffset = 0x20;    // its objects.txt record
constexpr std::size_t ObjectsTxtParm0      = 0x134;   // objects.txt Parm0, int32
constexpr std::size_t GameDataContext      = 0x106;   // data context byte

constexpr std::uint64_t BoostHandlerRva      = 0x591E20;   // Codes 6, 8-11, 13, 15; hooked
constexpr std::uint64_t SkillHandlerRva      = 0x591F00;   // Code 12; hooked
constexpr std::uint64_t CombatHandlerRva     = 0x591F70;   // Code 7; hooked
constexpr std::uint64_t ShrineChartRva       = 0x1D3F1D0;  // 24 x {handler, stat, state}
constexpr std::uint64_t RefillHandlerRva     = 0x590DB0;   // Code 1, proof of the max life / mana readers
constexpr std::uint64_t ValueHelperRva       = 0x594DA0;
constexpr std::uint64_t ApplyTimedEffectRva  = 0x433D20;   // thunk, D2RCore!ApplyWideTimedStatEffect
constexpr std::uint64_t SetListStatRva       = 0x2F3030;   // thunk, D2RCore!SetWideListStat
constexpr std::uint64_t MaxLifeRva           = 0x2F4D20;   // thunk, D2RCore!ReadWideMaxLife
constexpr std::uint64_t MaxManaRva           = 0x2F4E20;   // thunk, D2RCore!ReadWideMaxMana
constexpr std::uint64_t CheckStateRva        = 0x3351B0;   // STATES_CheckState(unit, state)
constexpr std::uint64_t SkillRefreshRva      = 0x438970;
constexpr std::uint64_t SkillStateRemovedRva = 0x594FB0;
constexpr std::uint64_t UnitEventRva         = 0x44D570;   // (game, event, unit, other, D2Damage)
constexpr std::uint64_t AbsorbEventSiteRva   = 0x44EB25;
constexpr std::uint64_t AbsorbEventCallRva   = 0x44EB3A;   // rel32 retargeted
constexpr std::uint64_t ResistLoopLengthsRva = 0x44EBA3;   // state 133 / 131 length checks
constexpr std::uint64_t LifeTotalSumRva      = 0x44EC71;
constexpr std::uint64_t LifeTotalTailRva     = 0x44ECAA;
constexpr std::uint64_t PoisonSourcesRva     = 0x44D1CF;
constexpr std::uint64_t GameLayoutRva        = 0x44DF40;   // difficulty at Game+104h, context at +106h

// 591E20..591EF4, 591F00..591F6B and 591F70..5920CD, whole.
constexpr std::uint8_t BoostHandlerBody[]{
    0x4C, 0x8B, 0xDC, 0x49, 0x89, 0x5B, 0x08, 0x49, 0x89, 0x6B, 0x18, 0x56,
    0x57, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x60, 0x41, 0x0F, 0xB6, 0x00, 0x49,
    0x8B, 0xD8, 0x3B, 0x05, 0x10, 0x33, 0xE1, 0x01, 0x48, 0x8B, 0xF2, 0x0F,
    0xB6, 0xE9, 0x7C, 0x13, 0x49, 0x8D, 0x4B, 0x10, 0x41, 0xC6, 0x43, 0x10,
    0x00, 0xE8, 0xEA, 0x07, 0x00, 0x00, 0x84, 0xC0, 0x74, 0x01, 0xCC, 0x0F,
    0xB6, 0x3B, 0x4C, 0x8D, 0x3D, 0x6B, 0xD3, 0x7A, 0x01, 0x48, 0x03, 0xFF,
    0x45, 0x8B, 0x44, 0xFF, 0x08, 0x41, 0x83, 0xF8, 0xFF, 0x74, 0x22, 0x48,
    0x8B, 0x46, 0x10, 0x48, 0x8B, 0xD3, 0x44, 0x8B, 0x4B, 0x04, 0x40, 0x0F,
    0xB6, 0xCD, 0x48, 0x89, 0x44, 0x24, 0x20, 0xE8, 0x14, 0x2F, 0x00, 0x00,
    0x41, 0x8B, 0x54, 0xFF, 0x08, 0x8B, 0xC8, 0xEB, 0x05, 0x33, 0xC9, 0x8D,
    0x51, 0xFF, 0x48, 0x8B, 0x46, 0x08, 0x48, 0x89, 0x44, 0x24, 0x30, 0x48,
    0x8B, 0x46, 0x10, 0x48, 0x89, 0x44, 0x24, 0x38, 0x8B, 0x43, 0x0C, 0x89,
    0x44, 0x24, 0x48, 0x41, 0x8B, 0x44, 0xFF, 0x0C, 0x89, 0x4C, 0x24, 0x50,
    0x48, 0x8D, 0x4C, 0x24, 0x30, 0x89, 0x44, 0x24, 0x54, 0x48, 0xC7, 0x44,
    0x24, 0x40, 0x00, 0x00, 0x00, 0x00, 0x89, 0x54, 0x24, 0x4C, 0x48, 0xC7,
    0x44, 0x24, 0x58, 0x00, 0x00, 0x00, 0x00, 0xE8, 0x40, 0x1E, 0xEA, 0xFF,
    0x4C, 0x8D, 0x5C, 0x24, 0x60, 0x49, 0x8B, 0x5B, 0x20, 0x49, 0x8B, 0x6B,
    0x30, 0x49, 0x8B, 0xE3, 0x41, 0x5F, 0x5F, 0x5E, 0xC3,
};
constexpr std::uint8_t SkillHandlerBody[]{
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x50, 0x48, 0x8B, 0x42, 0x08, 0x33, 0xC9,
    0x48, 0x89, 0x44, 0x24, 0x20, 0x48, 0x8B, 0xDA, 0x48, 0x8B, 0x42, 0x10,
    0x48, 0x89, 0x44, 0x24, 0x28, 0x41, 0x8B, 0x40, 0x0C, 0x89, 0x44, 0x24,
    0x38, 0x41, 0x0F, 0xB6, 0x00, 0x48, 0x03, 0xC0, 0x48, 0x89, 0x4C, 0x24,
    0x30, 0x48, 0x8D, 0x0D, 0xA4, 0xD2, 0x7A, 0x01, 0x48, 0xC7, 0x44, 0x24,
    0x3C, 0x0B, 0x00, 0x00, 0x00, 0x8B, 0x04, 0xC1, 0x48, 0x8D, 0x4C, 0x24,
    0x20, 0x89, 0x44, 0x24, 0x44, 0x48, 0x8D, 0x05, 0x5C, 0x30, 0x00, 0x00,
    0x48, 0x89, 0x44, 0x24, 0x48, 0xE8, 0xC2, 0x1D, 0xEA, 0xFF, 0x48, 0x8B,
    0x4B, 0x10, 0x48, 0x83, 0xC4, 0x50, 0x5B, 0xE9, 0x04, 0x6A, 0xEA, 0xFF,
};
constexpr std::uint8_t CombatHandlerBody[]{
    0x4C, 0x8B, 0xDC, 0x49, 0x89, 0x5B, 0x08, 0x49, 0x89, 0x6B, 0x18, 0x49,
    0x89, 0x73, 0x20, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57,
    0x48, 0x83, 0xEC, 0x60, 0x41, 0x0F, 0xB6, 0x00, 0x49, 0x8B, 0xF8, 0x3B,
    0x05, 0xB7, 0x31, 0xE1, 0x01, 0x4C, 0x8B, 0xF2, 0x44, 0x0F, 0xB6, 0xF9,
    0x7C, 0x13, 0x49, 0x8D, 0x4B, 0x10, 0x41, 0xC6, 0x43, 0x10, 0x00, 0xE8,
    0x70, 0x05, 0x00, 0x00, 0x84, 0xC0, 0x74, 0x01, 0xCC, 0x0F, 0xB6, 0x37,
    0x45, 0x33, 0xED, 0x49, 0x8B, 0x5E, 0x10, 0x48, 0x03, 0xF6, 0x44, 0x8B,
    0x67, 0x04, 0x41, 0x8B, 0xED, 0x48, 0x85, 0xDB, 0x74, 0x25, 0x41, 0xB8,
    0xCB, 0x08, 0x00, 0x00, 0x48, 0x8D, 0x15, 0xF5, 0xD5, 0x7A, 0x01, 0x48,
    0x8B, 0xCB, 0xE8, 0x7D, 0x83, 0xDB, 0xFF, 0x48, 0x85, 0xC0, 0x74, 0x0B,
    0x48, 0x8B, 0xC8, 0xE8, 0xA0, 0x56, 0xDF, 0xFF, 0x48, 0x8B, 0xE8, 0x33,
    0xD2, 0x41, 0xB8, 0xFF, 0xFF, 0xFF, 0xFF, 0x48, 0x8B, 0xCB, 0xE8, 0xCD,
    0xBC, 0xDA, 0xFF, 0x41, 0xB9, 0x01, 0x00, 0x00, 0x00, 0x48, 0x89, 0x6C,
    0x24, 0x28, 0x4C, 0x8D, 0x84, 0x24, 0x98, 0x00, 0x00, 0x00, 0x48, 0x89,
    0x44, 0x24, 0x20, 0x48, 0x8B, 0xD3, 0x41, 0x0F, 0xB6, 0xCF, 0xE8, 0xE9,
    0x18, 0x00, 0x00, 0x8B, 0x8C, 0x24, 0x98, 0x00, 0x00, 0x00, 0xB8, 0x1F,
    0x85, 0xEB, 0x51, 0x41, 0x0F, 0xAF, 0xCC, 0x4C, 0x89, 0x6C, 0x24, 0x40,
    0xC7, 0x44, 0x24, 0x4C, 0x19, 0x00, 0x00, 0x00, 0x4C, 0x89, 0x6C, 0x24,
    0x58, 0xF7, 0xE9, 0x48, 0x8D, 0x4C, 0x24, 0x30, 0x8B, 0xDA, 0xC1, 0xFB,
    0x05, 0x8B, 0xC3, 0xC1, 0xE8, 0x1F, 0x03, 0xD8, 0x49, 0x8B, 0x46, 0x08,
    0x48, 0x89, 0x44, 0x24, 0x30, 0x49, 0x8B, 0x46, 0x10, 0x48, 0x89, 0x44,
    0x24, 0x38, 0x8B, 0x47, 0x0C, 0x89, 0x44, 0x24, 0x48, 0x8B, 0x47, 0x08,
    0x89, 0x44, 0x24, 0x50, 0x48, 0x8D, 0x05, 0x59, 0xD1, 0x7A, 0x01, 0x8B,
    0x04, 0xF0, 0x89, 0x44, 0x24, 0x54, 0xE8, 0x91, 0x1C, 0xEA, 0xFF, 0x49,
    0x8B, 0x0E, 0x44, 0x8B, 0xCB, 0x41, 0xB8, 0x13, 0x00, 0x00, 0x00, 0x90,
    0x44, 0x89, 0x6C, 0x24, 0x20, 0x48, 0x8B, 0xD0, 0x0F, 0xB6, 0x89, 0x06,
    0x01, 0x00, 0x00, 0xE8, 0x80, 0x0F, 0xD6, 0xFF, 0x4C, 0x8D, 0x5C, 0x24,
    0x60, 0x49, 0x8B, 0x5B, 0x30, 0x49, 0x8B, 0x6B, 0x40, 0x49, 0x8B, 0x73,
    0x48, 0x49, 0x8B, 0xE3, 0x41, 0x5F, 0x41, 0x5E, 0x41, 0x5D, 0x41, 0x5C,
    0x5F, 0xC3,
};
// The hooked windows handed to the loader: each prologue up to its first
// RIP-relative operand or call.
constexpr std::uint32_t BoostHookSize  = 26;   // 591E20..591E39
constexpr std::uint32_t SkillHookSize  = 49;   // 591F00..591F30
constexpr std::uint32_t CombatHookSize = 35;   // 591F70..591F92

// Chart +8 (stat, state) for Codes 6..13.
constexpr std::uint8_t ChartStatsAndStates[8][8]{
    { 0xAB, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00 },   // Code 6
    { 0xFF, 0xFF, 0xFF, 0xFF, 0x81, 0x00, 0x00, 0x00 },   // Code 7
    { 0x27, 0x00, 0x00, 0x00, 0x83, 0x00, 0x00, 0x00 },   // Code 8
    { 0x2B, 0x00, 0x00, 0x00, 0x84, 0x00, 0x00, 0x00 },   // Code 9
    { 0x29, 0x00, 0x00, 0x00, 0x82, 0x00, 0x00, 0x00 },   // Code 10
    { 0x2D, 0x00, 0x00, 0x00, 0x85, 0x00, 0x00, 0x00 },   // Code 11
    { 0x0B, 0x00, 0x00, 0x00, 0x86, 0x00, 0x00, 0x00 },   // Code 12
    { 0x1B, 0x00, 0x00, 0x00, 0x87, 0x00, 0x00, 0x00 },   // Code 13
};

// 590DB0..590E20, the Refilling shrine handler: it reads max life through
// 2F4D20 and max mana through 2F4E20, which proves both readers.
constexpr std::uint8_t RefillHandlerBody[]{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B,
    0xFA, 0x45, 0x33, 0xC0, 0x48, 0x8B, 0x4F, 0x10, 0x41, 0x8D, 0x50, 0x06,
    0xE8, 0x53, 0x42, 0xD6, 0xFF, 0x48, 0x8B, 0x4F, 0x10, 0x8B, 0xD8, 0xE8,
    0x48, 0x3F, 0xD6, 0xFF, 0x2B, 0xC3, 0x74, 0x13, 0x48, 0x8B, 0x4F, 0x10,
    0x45, 0x33, 0xC9, 0x44, 0x8B, 0xC0, 0x41, 0x8D, 0x51, 0x06, 0xE8, 0x01,
    0x27, 0xD6, 0xFF, 0x48, 0x8B, 0x4F, 0x10, 0x45, 0x33, 0xC0, 0x41, 0x8D,
    0x50, 0x08, 0xE8, 0x21, 0x42, 0xD6, 0xFF, 0x48, 0x8B, 0x4F, 0x10, 0x8B,
    0xD8, 0xE8, 0x16, 0x40, 0xD6, 0xFF, 0x2B, 0xC3, 0x74, 0x13, 0x48, 0x8B,
    0x4F, 0x10, 0x45, 0x33, 0xC9, 0x44, 0x8B, 0xC0, 0x41, 0x8D, 0x51, 0x08,
    0xE8, 0xCF, 0x26, 0xD6, 0xFF,
};
constexpr std::uint8_t AbsorbEventSite[]{
    0x48, 0x8B, 0x4C, 0x24, 0x30, 0x4D, 0x8B, 0xCD, 0x4C, 0x8B, 0xC6, 0x4C,
    0x89, 0x7C, 0x24, 0x20, 0xBA, 0x0B, 0x00, 0x00, 0x00, 0xE8, 0x31, 0xEA,
    0xFF, 0xFF,
};
constexpr std::uint8_t ResistLoopLengths[]{
    0x45, 0x39, 0x77, 0x3C, 0x7E, 0x15, 0xBA, 0x85, 0x00, 0x00, 0x00, 0x48,
    0x8B, 0xCE, 0xE8, 0xFA, 0x65, 0xEE, 0xFF, 0x85, 0xC0, 0x74, 0x04, 0x45,
    0x89, 0x77, 0x3C, 0x45, 0x39, 0x77, 0x28, 0x7E, 0x15, 0xBA, 0x83, 0x00,
    0x00, 0x00, 0x48, 0x8B, 0xCE, 0xE8, 0xDF, 0x65, 0xEE, 0xFF, 0x85, 0xC0,
    0x74, 0x04, 0x45, 0x89, 0x77, 0x28,
};
constexpr std::uint8_t LifeTotalSum[]{
    0x41, 0x8B, 0x47, 0x30, 0x4D, 0x8B, 0xCD, 0x41, 0x03, 0x47, 0x34, 0x4C,
    0x8B, 0xC6, 0x41, 0x03, 0x47, 0x18, 0xBA, 0x10, 0x00, 0x00, 0x00, 0x41,
    0x03, 0x47, 0x2C,
};
constexpr std::uint8_t LifeTotalTail[]{
    0x45, 0x85, 0xE4, 0x74, 0x0E, 0x41, 0x8B, 0x87, 0x20, 0x01, 0x00, 0x00,
    0x41, 0x01, 0x87, 0x34, 0x01, 0x00, 0x00,
};
constexpr std::uint8_t PoisonSources[]{
    0x48, 0x8B, 0x5F, 0x40, 0x33, 0xC0, 0x44, 0x8B, 0xE0, 0x44, 0x8B, 0xE8,
    0x48, 0x8B, 0x47, 0x48, 0x48, 0x8D, 0x0C, 0x40, 0x48, 0x8D, 0x04, 0x8B,
    0x48, 0x3B, 0xD8, 0x74, 0x41, 0x4C, 0x8B, 0xF8, 0x90, 0x8B, 0x03, 0x48,
    0x8B, 0xD6, 0x44, 0x8B, 0x43, 0x04, 0x49, 0x8B, 0xCE, 0x44, 0x8B, 0x4B,
    0x08, 0x45, 0x03, 0xE0, 0x89, 0x44, 0x24, 0x30, 0x45, 0x03, 0xE9, 0xC7,
    0x44, 0x24, 0x28, 0xFF, 0xFF, 0xFF, 0xFF, 0xC7, 0x44, 0x24, 0x20, 0xFF,
    0xFF, 0xFF, 0xFF, 0xE8, 0xA1, 0x46, 0x00, 0x00, 0x48, 0x83, 0xC3, 0x0C,
    0x49, 0x3B, 0xDF, 0x75, 0xC8, 0x4C, 0x8B, 0x7C, 0x24, 0x60, 0x80, 0x7F,
    0x0C, 0x00, 0x0F, 0x84, 0xEF, 0x00, 0x00, 0x00, 0x44, 0x8B, 0x4F, 0x3C,
    0x44, 0x8B, 0x47, 0x38,
};
constexpr std::uint8_t GameLayout[]{
    0x48, 0x89, 0x4C, 0x24, 0x30, 0x0F, 0xB6, 0x91, 0x04, 0x01, 0x00, 0x00,
    0x48, 0x8B, 0xF9, 0x48, 0x89, 0x4C, 0x24, 0x40, 0x4D, 0x8B, 0xF9, 0x0F,
    0xB6, 0x89, 0x06, 0x01, 0x00, 0x00, 0x49, 0x8B, 0xF0, 0xE8, 0xCA, 0x28,
    0xEB, 0xFF,
};

constexpr std::size_t   AbsorbCallOffsetInSite = AbsorbEventCallRva - AbsorbEventSiteRva;
static_assert(AbsorbCallOffsetInSite + CallSize == sizeof(AbsorbEventSite));
static_assert(sizeof(BoostHandlerBody) == 0xD5 && sizeof(SkillHandlerBody) == 0x6C
              && sizeof(CombatHandlerBody) == 0x15E && sizeof(RefillHandlerBody) == 0x71);

// ---------------------------------------------------------------------------
// The natives the effects call, proven by the verified code that calls them
// ---------------------------------------------------------------------------
//   Their own entry bytes are NOT fingerprinted: other plugins may hook them,
//   and calling through such a hook is exactly what the game's own call does
//   (damage-overflow-fix's stat_write_watch, on by default, rewrites the
//   0x2F3030 thunk). Instead each RVA is checked at compile time against a
//   call, jmp or lea inside a body that is fingerprinted whole at load.

// The rel32 of a call / jmp (E8, E9) or of a lea rax, [rip+d] (48 8D 05).
constexpr auto Rel32At(const std::uint8_t* bytes, std::size_t at) noexcept -> std::int64_t {
    const std::uint32_t raw = static_cast<std::uint32_t>(bytes[at]) | (static_cast<std::uint32_t>(bytes[at + 1]) << 8)
                            | (static_cast<std::uint32_t>(bytes[at + 2]) << 16)
                            | (static_cast<std::uint32_t>(bytes[at + 3]) << 24);
    return static_cast<std::int32_t>(raw);
}

constexpr auto BranchTarget(const std::uint8_t* body, std::uint64_t bodyRva, std::size_t at) noexcept -> std::uint64_t {
    return static_cast<std::uint64_t>(static_cast<std::int64_t>(bodyRva + at + 5) + Rel32At(body, at + 1));
}

constexpr auto LeaTarget(const std::uint8_t* body, std::uint64_t bodyRva, std::size_t at) noexcept -> std::uint64_t {
    return static_cast<std::uint64_t>(static_cast<std::int64_t>(bodyRva + at + 7) + Rel32At(body, at + 3));
}

static_assert(BoostHandlerBody[0x67] == 0xE8 && BranchTarget(BoostHandlerBody, BoostHandlerRva, 0x67) == ValueHelperRva);
static_assert(BoostHandlerBody[0xbb] == 0xE8 && BranchTarget(BoostHandlerBody, BoostHandlerRva, 0xbb) == ApplyTimedEffectRva);
static_assert(SkillHandlerBody[0x4d] == 0x48 && LeaTarget(SkillHandlerBody, SkillHandlerRva, 0x4d) == SkillStateRemovedRva);
static_assert(SkillHandlerBody[0x59] == 0xE8 && BranchTarget(SkillHandlerBody, SkillHandlerRva, 0x59) == ApplyTimedEffectRva);
static_assert(SkillHandlerBody[0x67] == 0xE9 && BranchTarget(SkillHandlerBody, SkillHandlerRva, 0x67) == SkillRefreshRva);
static_assert(CombatHandlerBody[0x11a] == 0xE8 && BranchTarget(CombatHandlerBody, CombatHandlerRva, 0x11a) == ApplyTimedEffectRva);
static_assert(CombatHandlerBody[0x13b] == 0xE8 && BranchTarget(CombatHandlerBody, CombatHandlerRva, 0x13b) == SetListStatRva);
static_assert(RefillHandlerBody[0x23] == 0xE8 && BranchTarget(RefillHandlerBody, RefillHandlerRva, 0x23) == MaxLifeRva);
static_assert(RefillHandlerBody[0x55] == 0xE8 && BranchTarget(RefillHandlerBody, RefillHandlerRva, 0x55) == MaxManaRva);
static_assert(ResistLoopLengths[0xe] == 0xE8 && BranchTarget(ResistLoopLengths, ResistLoopLengthsRva, 0xe) == CheckStateRva);
static_assert(ResistLoopLengths[0x29] == 0xE8 && BranchTarget(ResistLoopLengths, ResistLoopLengthsRva, 0x29) == CheckStateRva);
static_assert(AbsorbEventSite[0x15] == 0xE8 && BranchTarget(AbsorbEventSite, AbsorbEventSiteRva, 0x15) == UnitEventRva);


// ---------------------------------------------------------------------------
// Sharing anchors
// ---------------------------------------------------------------------------

constexpr std::uint64_t ShrineOperateRva      = 0x58F2A0;   // objects.txt OperateFn 2, hooked
constexpr std::uint64_t ObjectShrineRecordRva = 0x34B520;   // (object) -> shrines.txt record
constexpr std::uint64_t FireHandlerRva        = 0x591220;   // Code 19
constexpr std::uint64_t FirePositionRva       = 0x59127C;   // its unit position reads
constexpr std::uint64_t PathGetXRva           = 0x341A20;
constexpr std::uint64_t PathGetYRva           = 0x341A30;
constexpr std::uint64_t UnitIsDeadRva         = 0x34C2C0;   // SUNIT_IsDead
constexpr std::uint64_t UnitRoomRva           = 0x34B440;   // UNITS_GetRoom
constexpr std::uint64_t ActOfLevelRva         = 0x326710;   // (data context, level id) -> act
constexpr std::uint64_t LivingPlayersRva      = 0x490D00;   // (game, callback, arg)
constexpr std::uint64_t PetIterateRva         = 0x4FF990;   // (game, player, callback, arg, all)
constexpr std::uint64_t PetWalkRva            = 0x5014C0;   // one pet list, calls the callback
constexpr std::uint64_t GetServerUnitRva      = 0x48FE80;   // SUNIT_GetServerUnit (game, type, guid)
constexpr std::uint64_t StatReadRva           = 0x2F5020;   // the stat read of both refills
constexpr std::uint64_t StatAddRva            = 0x2F34F0;   // the stat add of both refills

constexpr std::size_t  ChartSlots     = 24;
constexpr std::size_t  ChartStride    = 16;
constexpr std::size_t  UnitTypeOffset = 0x00;   // int32
constexpr std::size_t  UnitPathOffset = 0x38;   // the unit's path
constexpr std::size_t  StaticPathX    = 0x10;   // int32, objects, items and tiles
constexpr std::size_t  StaticPathY    = 0x14;
constexpr std::int32_t UnitTypeObject = 2;
constexpr std::int32_t UnitTypeItem   = 4;
constexpr std::int32_t UnitTypeTile   = 5;

// 0X58F2A0..0X58F558
constexpr std::uint8_t ShrineOperateBody[]{
    0x40, 0x57, 0x48, 0x83, 0xEC, 0x70, 0x48, 0x8B, 0x05, 0x1B, 0xC0, 0x43,
    0x02, 0x48, 0x33, 0xC4, 0x48, 0x89, 0x44, 0x24, 0x60, 0x48, 0x8B, 0xF9,
    0x48, 0x8B, 0x49, 0x08, 0xE8, 0x9F, 0xB8, 0xDB, 0xFF, 0x85, 0xC0, 0x74,
    0x15, 0x33, 0xC0, 0x48, 0x8B, 0x4C, 0x24, 0x60, 0x48, 0x33, 0xCC, 0xE8,
    0x7C, 0x1E, 0xD4, 0x00, 0x48, 0x83, 0xC4, 0x70, 0x5F, 0xC3, 0x48, 0x8B,
    0x4F, 0x08, 0x48, 0x89, 0xAC, 0x24, 0x90, 0x00, 0x00, 0x00, 0x48, 0x89,
    0xB4, 0x24, 0x98, 0x00, 0x00, 0x00, 0xE8, 0xFD, 0xB9, 0xDB, 0xFF, 0x83,
    0xF8, 0xFF, 0x74, 0x14, 0x48, 0x8D, 0x4C, 0x24, 0x40, 0xC6, 0x44, 0x24,
    0x40, 0x00, 0xE8, 0x79, 0x38, 0x00, 0x00, 0x84, 0xC0, 0x74, 0x01, 0xCC,
    0x48, 0x8B, 0x4F, 0x10, 0x48, 0x8D, 0x15, 0xB9, 0x02, 0x7B, 0x01, 0x41,
    0xB8, 0x6D, 0x09, 0x00, 0x00, 0xE8, 0x0E, 0xB0, 0xDB, 0xFF, 0x48, 0x8B,
    0x4F, 0x08, 0x8B, 0xD0, 0xE8, 0x43, 0xF6, 0xDB, 0xFF, 0x48, 0x8B, 0x4F,
    0x08, 0xBA, 0x01, 0x00, 0x00, 0x00, 0xE8, 0xB5, 0xF2, 0xDB, 0xFF, 0x48,
    0x8B, 0x07, 0x8B, 0x57, 0x20, 0x0F, 0xB6, 0x88, 0x06, 0x01, 0x00, 0x00,
    0xE8, 0xB3, 0x09, 0xE0, 0xFF, 0x33, 0xED, 0x40, 0x38, 0xA8, 0xC9, 0x00,
    0x00, 0x00, 0x75, 0x3B, 0x40, 0x38, 0xA8, 0x00, 0x01, 0x00, 0x00, 0x74,
    0x32, 0x8B, 0x90, 0x9C, 0x00, 0x00, 0x00, 0x44, 0x8D, 0x45, 0x01, 0x48,
    0x8B, 0x0F, 0xC1, 0xEA, 0x08, 0x89, 0x6C, 0x24, 0x30, 0x89, 0x6C, 0x24,
    0x28, 0x44, 0x8B, 0x89, 0x70, 0x01, 0x00, 0x00, 0x41, 0xFF, 0xC1, 0x89,
    0x6C, 0x24, 0x20, 0x44, 0x03, 0xCA, 0x48, 0x8B, 0x57, 0x08, 0xE8, 0x8D,
    0xC3, 0xEF, 0xFF, 0x48, 0x8B, 0x4F, 0x08, 0xE8, 0x94, 0xA4, 0xDB, 0xFF,
    0x48, 0x85, 0xC0, 0x74, 0x08, 0x48, 0x8B, 0xC8, 0xE8, 0x57, 0xC7, 0xE3,
    0xFF, 0x48, 0x8B, 0x4F, 0x08, 0xE8, 0x6E, 0xC1, 0xDB, 0xFF, 0xBA, 0x14,
    0x00, 0x00, 0x00, 0x48, 0x8D, 0x4C, 0x24, 0x48, 0x48, 0x8B, 0xF0, 0x44,
    0x0F, 0xB7, 0x40, 0x14, 0xE8, 0x17, 0x36, 0xCA, 0x00, 0x48, 0x83, 0xF8,
    0x14, 0x48, 0x8B, 0xCD, 0x48, 0x0F, 0x42, 0xC8, 0x40, 0x88, 0x6C, 0x0C,
    0x48, 0x48, 0x8B, 0x0F, 0x8B, 0x91, 0x70, 0x01, 0x00, 0x00, 0x48, 0x8D,
    0x4C, 0x24, 0x48, 0xE8, 0x24, 0xC6, 0xE3, 0xFF, 0x48, 0x8B, 0x4F, 0x08,
    0x48, 0x8B, 0xD0, 0xE8, 0xE8, 0xE9, 0xDB, 0xFF, 0x48, 0x8B, 0x4F, 0x08,
    0xBA, 0x01, 0x00, 0x00, 0x00, 0xE8, 0x6A, 0xE9, 0xDB, 0xFF, 0x48, 0x8B,
    0x0F, 0x41, 0xB8, 0x06, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x57, 0x08, 0x89,
    0x6C, 0x24, 0x30, 0x89, 0x6C, 0x24, 0x28, 0x44, 0x8B, 0x89, 0x70, 0x01,
    0x00, 0x00, 0x41, 0x81, 0xC1, 0x2C, 0x01, 0x00, 0x00, 0x89, 0x6C, 0x24,
    0x20, 0xE8, 0xEE, 0xC2, 0xEF, 0xFF, 0x0F, 0xB6, 0x06, 0x3C, 0x18, 0x73,
    0x28, 0x48, 0x03, 0xC0, 0x4C, 0x8D, 0x0D, 0x8D, 0xFD, 0x7A, 0x01, 0x4D,
    0x8B, 0x0C, 0xC1, 0x4D, 0x85, 0xC9, 0x74, 0x15, 0x48, 0x8B, 0x07, 0x4C,
    0x8B, 0xC6, 0x48, 0x8B, 0xD7, 0x0F, 0xB6, 0x88, 0x06, 0x01, 0x00, 0x00,
    0x41, 0xFF, 0xD1, 0xEB, 0x74, 0x48, 0x8B, 0x4F, 0x10, 0x45, 0x33, 0xC0,
    0x48, 0x89, 0x9C, 0x24, 0x88, 0x00, 0x00, 0x00, 0x41, 0x8D, 0x50, 0x06,
    0xE8, 0xA7, 0x5B, 0xD6, 0xFF, 0x48, 0x8B, 0x4F, 0x10, 0x8B, 0xD8, 0xE8,
    0x9C, 0x58, 0xD6, 0xFF, 0x2B, 0xC3, 0x74, 0x13, 0x48, 0x8B, 0x4F, 0x10,
    0x45, 0x33, 0xC9, 0x44, 0x8B, 0xC0, 0x41, 0x8D, 0x51, 0x06, 0xE8, 0x55,
    0x40, 0xD6, 0xFF, 0x48, 0x8B, 0x4F, 0x10, 0x45, 0x33, 0xC0, 0x41, 0x8D,
    0x50, 0x08, 0xE8, 0x75, 0x5B, 0xD6, 0xFF, 0x48, 0x8B, 0x4F, 0x10, 0x8B,
    0xD8, 0xE8, 0x6A, 0x59, 0xD6, 0xFF, 0x2B, 0xC3, 0x48, 0x8B, 0x9C, 0x24,
    0x88, 0x00, 0x00, 0x00, 0x74, 0x13, 0x48, 0x8B, 0x4F, 0x10, 0x45, 0x33,
    0xC9, 0x44, 0x8B, 0xC0, 0x41, 0x8D, 0x51, 0x08, 0xE8, 0x1B, 0x40, 0xD6,
    0xFF, 0x0F, 0xB6, 0x46, 0x10, 0x48, 0x8B, 0xB4, 0x24, 0x98, 0x00, 0x00,
    0x00, 0x84, 0xC0, 0x74, 0x31, 0x48, 0x8B, 0x0F, 0x41, 0xB8, 0x05, 0x00,
    0x00, 0x00, 0x69, 0xD0, 0xB0, 0x04, 0x00, 0x00, 0x89, 0x6C, 0x24, 0x30,
    0x89, 0x6C, 0x24, 0x28, 0x44, 0x8B, 0x89, 0x70, 0x01, 0x00, 0x00, 0x41,
    0xFF, 0xC1, 0x89, 0x6C, 0x24, 0x20, 0x44, 0x03, 0xCA, 0x48, 0x8B, 0x57,
    0x08, 0xE8, 0x0A, 0xC2, 0xEF, 0xFF, 0x48, 0x8B, 0x4F, 0x08, 0xE8, 0x41,
    0xB6, 0xDB, 0xFF, 0x48, 0x8B, 0xAC, 0x24, 0x90, 0x00, 0x00, 0x00, 0x83,
    0xF8, 0x01, 0x74, 0x14, 0x48, 0x8D, 0x4C, 0x24, 0x40, 0xC6, 0x44, 0x24,
    0x40, 0x00, 0xE8, 0xC5, 0x31, 0x00, 0x00, 0x84, 0xC0, 0x74, 0x01, 0xCC,
    0xB8, 0x01, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x4C, 0x24, 0x60, 0x48, 0x33,
    0xCC, 0xE8, 0xFE, 0x1B, 0xD4, 0x00, 0x48, 0x83, 0xC4, 0x70, 0x5F, 0xC3,
};

// 0X59127C..0X5912F5
constexpr std::uint8_t FirePositionBlock[]{
    0x8B, 0x0B, 0x45, 0x33, 0xED, 0x83, 0xE9, 0x02, 0x74, 0x21, 0x83, 0xE9,
    0x02, 0x74, 0x1C, 0x83, 0xF9, 0x01, 0x74, 0x17, 0x48, 0x8B, 0x4B, 0x38,
    0x48, 0x85, 0xC9, 0x74, 0x09, 0xE8, 0x82, 0x07, 0xDB, 0xFF, 0x8B, 0xF8,
    0xEB, 0x0C, 0x41, 0x8B, 0xFD, 0xEB, 0x07, 0x48, 0x8B, 0x43, 0x38, 0x8B,
    0x78, 0x10, 0x48, 0x8B, 0x5E, 0x08, 0x48, 0x85, 0xDB, 0x75, 0x0F, 0x48,
    0x8D, 0x4C, 0x24, 0x20, 0xE8, 0x8F, 0x56, 0xB0, 0xFF, 0x84, 0xC0, 0x74,
    0x01, 0xCC, 0x8B, 0x0B, 0x83, 0xE9, 0x02, 0x74, 0x21, 0x83, 0xE9, 0x02,
    0x74, 0x1C, 0x83, 0xF9, 0x01, 0x74, 0x17, 0x48, 0x8B, 0x4B, 0x38, 0x48,
    0x85, 0xC9, 0x74, 0x09, 0xE8, 0x4B, 0x07, 0xDB, 0xFF, 0x8B, 0xC8, 0xEB,
    0x0C, 0x41, 0x8B, 0xCD, 0xEB, 0x07, 0x48, 0x8B, 0x43, 0x38, 0x8B, 0x48,
    0x14,
};

// 0X34C2C0..0X34C332
constexpr std::uint8_t UnitIsDeadBody[]{
    0x48, 0x83, 0xEC, 0x28, 0x48, 0x85, 0xC9, 0x75, 0x1D, 0x88, 0x4C, 0x24,
    0x30, 0x48, 0x8D, 0x4C, 0x24, 0x30, 0xE8, 0x59, 0x94, 0xFF, 0xFF, 0x84,
    0xC0, 0x74, 0x4D, 0xCC, 0xB8, 0x01, 0x00, 0x00, 0x00, 0x48, 0x83, 0xC4,
    0x28, 0xC3, 0xF7, 0x81, 0x24, 0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
    0x75, 0x36, 0x8B, 0x11, 0x85, 0xD2, 0x74, 0x24, 0x83, 0xFA, 0x01, 0x74,
    0x13, 0xF7, 0x81, 0x28, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x75,
    0x1F, 0x33, 0xC0, 0x48, 0x83, 0xC4, 0x28, 0xC3, 0x8B, 0x41, 0x0C, 0x85,
    0xC0, 0x74, 0x11, 0x83, 0xF8, 0x0C, 0xEB, 0x0A, 0x8B, 0x41, 0x0C, 0x85,
    0xC0, 0x74, 0x05, 0x83, 0xF8, 0x11, 0x75, 0xE1, 0xB8, 0x01, 0x00, 0x00,
    0x00, 0x48, 0x83, 0xC4, 0x28, 0xC3,
};

// 0X34B440..0X34B49A
constexpr std::uint8_t UnitRoomBody[]{
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0x48, 0x85, 0xC9,
    0x75, 0x13, 0x88, 0x4C, 0x24, 0x30, 0x48, 0x8D, 0x4C, 0x24, 0x30, 0xE8,
    0x54, 0xA7, 0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x01, 0xCC, 0x8B, 0x0B, 0x83,
    0xE9, 0x02, 0x74, 0x25, 0x83, 0xE9, 0x02, 0x74, 0x20, 0x83, 0xF9, 0x01,
    0x74, 0x1B, 0x48, 0x8B, 0x4B, 0x38, 0x48, 0x85, 0xC9, 0x74, 0x0A, 0x48,
    0x83, 0xC4, 0x20, 0x5B, 0xE9, 0xAB, 0x67, 0xFF, 0xFF, 0x33, 0xC0, 0x48,
    0x83, 0xC4, 0x20, 0x5B, 0xC3, 0x48, 0x8B, 0x43, 0x38, 0x48, 0x8B, 0x00,
    0x48, 0x83, 0xC4, 0x20, 0x5B, 0xC3,
};

// 0X326710..0X3267A7
constexpr std::uint8_t ActOfLevelBody[]{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48,
    0x83, 0xEC, 0x30, 0x8B, 0xF2, 0x0F, 0xB6, 0xD9, 0xE8, 0xB7, 0x9F, 0xFD,
    0xFF, 0x0F, 0xB6, 0xCB, 0x48, 0x8B, 0xF8, 0xE8, 0x5C, 0xA3, 0xFD, 0xFF,
    0x8B, 0x98, 0x08, 0x01, 0x00, 0x00, 0x83, 0xEB, 0x01, 0x78, 0x43, 0x90,
    0x8B, 0xCB, 0x48, 0x89, 0x4C, 0x24, 0x50, 0x48, 0x3B, 0x4F, 0x08, 0x72,
    0x1E, 0x48, 0x8D, 0x44, 0x24, 0x50, 0x48, 0x89, 0x7C, 0x24, 0x28, 0x48,
    0x8D, 0x4C, 0x24, 0x20, 0x48, 0x89, 0x44, 0x24, 0x20, 0xE8, 0xFA, 0x88,
    0xFC, 0xFF, 0x84, 0xC0, 0x74, 0x01, 0xCC, 0x48, 0x69, 0x4C, 0x24, 0x50,
    0x8C, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x07, 0x3B, 0x74, 0x01, 0x04, 0x7D,
    0x17, 0x83, 0xEB, 0x01, 0x79, 0xBE, 0x32, 0xC0, 0x48, 0x8B, 0x5C, 0x24,
    0x40, 0x48, 0x8B, 0x74, 0x24, 0x48, 0x48, 0x83, 0xC4, 0x30, 0x5F, 0xC3,
    0x48, 0x8B, 0x74, 0x24, 0x48, 0x0F, 0xB6, 0xC3, 0x48, 0x8B, 0x5C, 0x24,
    0x40, 0x48, 0x83, 0xC4, 0x30, 0x5F, 0xC3,
};

// 0X490D00..0X490DBB
constexpr std::uint8_t LivingPlayersBody[]{
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x48, 0x89,
    0x74, 0x24, 0x20, 0x57, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x20,
    0x4C, 0x8D, 0xB1, 0x30, 0x22, 0x00, 0x00, 0x49, 0x8B, 0xE8, 0x48, 0x8B,
    0xF2, 0x48, 0x8B, 0xF9, 0x48, 0x85, 0xC9, 0x74, 0x05, 0x48, 0x85, 0xD2,
    0x75, 0x14, 0x48, 0x8D, 0x4C, 0x24, 0x40, 0xC6, 0x44, 0x24, 0x40, 0x00,
    0xE8, 0x4F, 0xC8, 0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x01, 0xCC, 0x41, 0xBF,
    0x80, 0x00, 0x00, 0x00, 0x0F, 0x1F, 0x40, 0x00, 0x49, 0x8B, 0x1E, 0x48,
    0x85, 0xDB, 0x74, 0x59, 0x0F, 0x1F, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xBA, 0x07, 0x00, 0x00, 0x00, 0x48, 0x8B, 0xCB, 0xE8, 0x43, 0x44, 0xEA,
    0xFF, 0x85, 0xC0, 0x75, 0x34, 0xBA, 0xBA, 0x00, 0x00, 0x00, 0x48, 0x8B,
    0xCB, 0xE8, 0x32, 0x44, 0xEA, 0xFF, 0x85, 0xC0, 0x75, 0x0B, 0x4C, 0x8B,
    0xC5, 0x48, 0x8B, 0xD3, 0x48, 0x8B, 0xCF, 0xFF, 0xD6, 0x48, 0x85, 0xDB,
    0x75, 0x13, 0x48, 0x8D, 0x4C, 0x24, 0x40, 0x88, 0x5C, 0x24, 0x40, 0xE8,
    0xD0, 0xC9, 0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x01, 0xCC, 0x48, 0x8B, 0x9B,
    0x58, 0x01, 0x00, 0x00, 0x48, 0x85, 0xDB, 0x75, 0xAF, 0x49, 0x83, 0xC6,
    0x08, 0x49, 0x83, 0xEF, 0x01, 0x75, 0x95,
};

// 0X4FF990..0X4FFA6D
constexpr std::uint8_t PetIterateBody[]{
    0x40, 0x55, 0x56, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x48, 0x83, 0xEC,
    0x40, 0x48, 0x8B, 0xF1, 0x4D, 0x8B, 0xE1, 0x48, 0x8B, 0xCA, 0x4D, 0x8B,
    0xE8, 0x48, 0x8B, 0xEA, 0xE8, 0x8F, 0xB8, 0xE4, 0xFF, 0x4C, 0x8B, 0xB0,
    0x98, 0x00, 0x00, 0x00, 0x4D, 0x85, 0xF6, 0x0F, 0x84, 0x9F, 0x00, 0x00,
    0x00, 0x48, 0x89, 0x5C, 0x24, 0x70, 0xBB, 0x01, 0x00, 0x00, 0x00, 0x48,
    0x89, 0x7C, 0x24, 0x78, 0x4C, 0x89, 0xBC, 0x24, 0x80, 0x00, 0x00, 0x00,
    0x44, 0x0F, 0xB6, 0xBC, 0x24, 0x90, 0x00, 0x00, 0x00, 0x0F, 0xB6, 0x8E,
    0x06, 0x01, 0x00, 0x00, 0xE8, 0xA3, 0x10, 0xE0, 0xFF, 0x48, 0x8B, 0xB8,
    0xE0, 0x12, 0x00, 0x00, 0x48, 0x63, 0xC7, 0x48, 0x3B, 0xC7, 0x75, 0x04,
    0x85, 0xFF, 0x79, 0x14, 0x48, 0x8D, 0x4C, 0x24, 0x30, 0xC6, 0x44, 0x24,
    0x30, 0x00, 0xE8, 0x51, 0x6F, 0xB8, 0xFF, 0x84, 0xC0, 0x74, 0x01, 0xCC,
    0x3B, 0xDF, 0x7D, 0x36, 0x0F, 0xB6, 0x8E, 0x06, 0x01, 0x00, 0x00, 0x44,
    0x8B, 0xC3, 0x49, 0x8B, 0xD6, 0xE8, 0xC6, 0x19, 0x00, 0x00, 0x48, 0x85,
    0xC0, 0x74, 0x1B, 0x44, 0x88, 0x7C, 0x24, 0x28, 0x4D, 0x8B, 0xCC, 0x4D,
    0x8B, 0xC5, 0x48, 0x89, 0x44, 0x24, 0x20, 0x48, 0x8B, 0xD5, 0x48, 0x8B,
    0xCE, 0xE8, 0x76, 0x1A, 0x00, 0x00, 0xFF, 0xC3, 0xEB, 0x93, 0x48, 0x8B,
    0x7C, 0x24, 0x78, 0x48, 0x8B, 0x5C, 0x24, 0x70, 0x4C, 0x8B, 0xBC, 0x24,
    0x80, 0x00, 0x00, 0x00, 0x48, 0x83, 0xC4, 0x40, 0x41, 0x5E, 0x41, 0x5D,
    0x41, 0x5C, 0x5E, 0x5D, 0xC3,
};

// 0X5014C0..0X501540
constexpr std::uint8_t PetWalkBody[]{
    0x48, 0x89, 0x6C, 0x24, 0x18, 0x57, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83,
    0xEC, 0x20, 0x48, 0x8B, 0x44, 0x24, 0x60, 0x49, 0x8B, 0xE9, 0x4D, 0x8B,
    0xF0, 0x4C, 0x8B, 0xFA, 0x48, 0x8B, 0xF9, 0x48, 0x8B, 0x00, 0x48, 0x85,
    0xC0, 0x74, 0x54, 0x48, 0x89, 0x5C, 0x24, 0x40, 0x48, 0x89, 0x74, 0x24,
    0x48, 0x0F, 0xB6, 0x74, 0x24, 0x68, 0x48, 0x8B, 0x58, 0x18, 0x40, 0x84,
    0xF6, 0x75, 0x05, 0xF6, 0x00, 0x01, 0x75, 0x25, 0x44, 0x8B, 0x40, 0x04,
    0xBA, 0x01, 0x00, 0x00, 0x00, 0x48, 0x8B, 0xCF, 0xE8, 0x6B, 0xE9, 0xF8,
    0xFF, 0x48, 0x85, 0xC0, 0x74, 0x0F, 0x4C, 0x8B, 0xCD, 0x4C, 0x8B, 0xC0,
    0x49, 0x8B, 0xD7, 0x48, 0x8B, 0xCF, 0x41, 0xFF, 0xD6, 0x48, 0x8B, 0xC3,
    0x48, 0x85, 0xDB, 0x75, 0xC5, 0x48, 0x8B, 0x74, 0x24, 0x48, 0x48, 0x8B,
    0x5C, 0x24, 0x40, 0x48, 0x8B, 0x6C, 0x24, 0x50,
};

// The hooked window: push rdi / sub rsp,70h. Byte 6 starts the RIP-relative
// cookie load, which cannot be moved.
constexpr std::uint32_t ShrineOperateHookSize = 6;

// The handler each chart slot holds, slot = shrines.txt Code (0 = none).
constexpr std::array<std::uint64_t, ChartSlots> ChartHandlers{
    0,        0x590DB0, 0x590D10, 0x590D60, 0x590E30, 0x590EC0, 0x591E20, 0x591F70,
    0x591E20, 0x591E20, 0x591E20, 0x591E20, 0x591F00, 0x591E20, 0x5920D0, 0x591E20,
    0x590F40, 0x590F80, 0x591060, 0x591220, 0x591600, 0x591700, 0x591A90, 0,
};

static_assert(sizeof(ShrineOperateBody) == 0x2B8 && sizeof(FirePositionBlock) == 0x79
              && sizeof(UnitIsDeadBody) == 0x72 && sizeof(UnitRoomBody) == 0x5A && sizeof(ActOfLevelBody) == 0x97
              && sizeof(LivingPlayersBody) == 0xBB && sizeof(PetIterateBody) == 0xDD && sizeof(PetWalkBody) == 0x80);
static_assert(ChartHandlers[1] == RefillHandlerRva && ChartHandlers[6] == BoostHandlerRva
              && ChartHandlers[7] == CombatHandlerRva && ChartHandlers[12] == SkillHandlerRva
              && ChartHandlers[19] == FireHandlerRva);

// 58F2A0: push rdi / sub rsp,70h / mov rax, [rip+cookie].
static_assert(ShrineOperateBody[0] == 0x40 && ShrineOperateBody[1] == 0x57 && ShrineOperateBody[2] == 0x48
              && ShrineOperateBody[3] == 0x83 && ShrineOperateBody[4] == 0xEC && ShrineOperateBody[5] == 0x70
              && ShrineOperateBody[6] == 0x48 && ShrineOperateBody[7] == 0x8B && ShrineOperateBody[8] == 0x05);
// 58F3AD call 34B520, 58F435 cmp al,18h, 58F43C lea r9,[1D3F1D0], 58F455 the
// data context at Game+106h, 58F45C call r9, 58F540 mov eax,1.
static_assert(ShrineOperateBody[0x10D] == 0xE8
              && BranchTarget(ShrineOperateBody, ShrineOperateRva, 0x10D) == ObjectShrineRecordRva);
static_assert(ShrineOperateBody[0x195] == 0x3C && ShrineOperateBody[0x196] == ChartSlots);
static_assert(ShrineOperateBody[0x19C] == 0x4C && ShrineOperateBody[0x19D] == 0x8D
              && LeaTarget(ShrineOperateBody, ShrineOperateRva, 0x19C) == ShrineChartRva);
static_assert(ShrineOperateBody[0x1B5] == 0x0F && ShrineOperateBody[0x1B6] == 0xB6
              && ShrineOperateBody[0x1B8] == (GameDataContext & 0xFF) && ShrineOperateBody[0x1B9] == (GameDataContext >> 8));
static_assert(ShrineOperateBody[0x1BC] == 0x41 && ShrineOperateBody[0x1BD] == 0xFF && ShrineOperateBody[0x1BE] == 0xD1);
static_assert(ShrineOperateBody[0x2A0] == 0xB8 && ShrineOperateBody[0x2A1] == 0x01);

// 58F461..58F4D4 (no handler) and the Refilling handler 0x590DB0 make the
// same six calls, with the same stat ids in the same lea edx,[r8/r9+stat].
constexpr std::size_t OperateRefillCall[6]{ 0x1D4, 0x1DF, 0x1F6, 0x206, 0x211, 0x230 };
constexpr std::size_t HandlerRefillCall[6]{ 0x18, 0x23, 0x3A, 0x4A, 0x55, 0x6C };
constexpr std::size_t OperateRefillStat[4]{ 0x1D0, 0x1F2, 0x202, 0x22C };
constexpr std::size_t HandlerRefillStat[4]{ 0x14, 0x36, 0x46, 0x68 };

constexpr auto SameRefill() noexcept -> bool {
    for (std::size_t index = 0; index < 6; ++index) {
        if (ShrineOperateBody[OperateRefillCall[index]] != 0xE8 || RefillHandlerBody[HandlerRefillCall[index]] != 0xE8) {
            return false;
        }
        if (BranchTarget(ShrineOperateBody, ShrineOperateRva, OperateRefillCall[index])
            != BranchTarget(RefillHandlerBody, RefillHandlerRva, HandlerRefillCall[index])) {
            return false;
        }
    }
    for (std::size_t index = 0; index < 4; ++index) {
        for (std::size_t at = 0; at < 4; ++at) {
            if (ShrineOperateBody[OperateRefillStat[index] + at] != RefillHandlerBody[HandlerRefillStat[index] + at]) {
                return false;
            }
        }
    }
    return true;
}
static_assert(SameRefill());
static_assert(BranchTarget(RefillHandlerBody, RefillHandlerRva, 0x18) == StatReadRva
              && BranchTarget(RefillHandlerBody, RefillHandlerRva, 0x3A) == StatAddRva);

// 59127C..5912F5: unit+38h is the path; types 2, 4 and 5 read x and y at
// path+10h / +14h, the others through PATH_GetX / PATH_GetY.
static_assert(FirePositionBlock[0x1D] == 0xE8 && BranchTarget(FirePositionBlock, FirePositionRva, 0x1D) == PathGetXRva);
static_assert(FirePositionBlock[0x64] == 0xE8 && BranchTarget(FirePositionBlock, FirePositionRva, 0x64) == PathGetYRva);
static_assert(FirePositionBlock[0x17] == UnitPathOffset && FirePositionBlock[0x2E] == UnitPathOffset
              && FirePositionBlock[0x31] == StaticPathX && FirePositionBlock[0x78] == StaticPathY);
static_assert(FirePositionBlock[0x07] == UnitTypeObject && FirePositionBlock[0x0C] == UnitTypeItem - UnitTypeObject
              && FirePositionBlock[0x11] == UnitTypeTile - UnitTypeItem);

// 490D18 lea r14,[rcx+2230h]; 490D60/490D71 states 7 and 186 through
// STATES_CheckState; 490D8B call rsi with (game, player, arg).
static_assert(LivingPlayersBody[0x18] == 0x4C && LivingPlayersBody[0x1B] == 0x30 && LivingPlayersBody[0x1C] == 0x22);
static_assert(LivingPlayersBody[0x68] == 0xE8 && BranchTarget(LivingPlayersBody, LivingPlayersRva, 0x68) == CheckStateRva
              && LivingPlayersBody[0x61] == 7);
static_assert(LivingPlayersBody[0x79] == 0xE8 && BranchTarget(LivingPlayersBody, LivingPlayersRva, 0x79) == CheckStateRva
              && LivingPlayersBody[0x72] == 0xBA);
static_assert(LivingPlayersBody[0x8B] == 0xFF && LivingPlayersBody[0x8C] == 0xD6);

// 4FF9B1 pet lists at +98h, 4FF9D8 'all' is the fifth argument, 4FFA45
// call 5014C0; 501508 type 1 to 48FE80, 501526 call r14 with
// (game, player, pet, arg).
static_assert(PetIterateBody[0x24] == 0x98 && PetIterateBody[0x4D] == 0x90);
static_assert(PetIterateBody[0xB5] == 0xE8 && BranchTarget(PetIterateBody, PetIterateRva, 0xB5) == PetWalkRva);
static_assert(PetWalkBody[0x50] == 0xE8 && BranchTarget(PetWalkBody, PetWalkRva, 0x50) == GetServerUnitRva
              && PetWalkBody[0x49] == 1);
static_assert(PetWalkBody[0x66] == 0x41 && PetWalkBody[0x67] == 0xFF && PetWalkBody[0x68] == 0xD6);

// SELECTION-LOGIC-BEGIN
// Pure selection logic. It holds no game state: everything comes in through
// NativeCalls, so the offline check can run it against the original machine
// code with the same inputs.

using ShrineRowCountFn   = std::int32_t(__fastcall*)(std::uint8_t dataContext) noexcept;
using ShrineRecordFn     = const std::uint8_t*(__fastcall*)(std::uint8_t dataContext, std::uint32_t row) noexcept;
using ObjectControlFn    = std::uint32_t*(__fastcall*)(void* game) noexcept;
using ShrineClassListsFn = const std::uint8_t*(__fastcall*)(void* game) noexcept;

constexpr std::size_t   RecordCode        = 0x00;          // shrines.txt Code, byte
constexpr std::size_t   RecordLevelMin    = 0x18;          // shrines.txt LevelMin, uint32
constexpr std::size_t   ClassCountsOffset = 0x00;          // int32 counts[8]
constexpr std::size_t   ClassListsOffset  = 0x20;          // int32* lists[8]
constexpr std::uint64_t SeedMultiplier    = 0x6AC690C5ULL; // 1791398085, the game's seed step
constexpr std::int32_t  DrawAttempts      = 8;
constexpr std::uint32_t ForcedPortalRow   = 17;
constexpr std::size_t   CodeCount         = 256;
constexpr std::uint8_t  ClassCount        = 8;               // effectclass lists 0..7

struct NativeCalls {
    ShrineRowCountFn   rowCount;
    ShrineRecordFn     record;
    ObjectControlFn    objectControl;
    ShrineClassListsFn classLists;
};

enum class Outcome : std::uint8_t {
    Filtered,     // drawn from the pool with the removed shrines taken out
    Redirected,   // removal emptied the pool; drawn from the fallback effectclass instead
    AllRemoved,   // nothing was left in either; the original draw over the full pool was made
    EmptyInData,  // the game's own pool is empty; the result is what the original returns
};

struct Selection {
    std::uint32_t row         = 0;   // the drawn row, before the game's own remap
    std::uint8_t  shrineClass = 0;   // effectclass drawn from; 0 = the whole table (Parm0 0)
    Outcome       outcome     = Outcome::Filtered;
};

struct ClassList {
    const std::int32_t* entries = nullptr;
    std::int32_t        count   = 0;
};

inline auto ReadU32(const std::uint8_t* at) noexcept -> std::uint32_t {
    std::uint32_t value = 0;
    std::memcpy(&value, at, sizeof(value));
    return value;
}

inline auto ReadI32(const std::uint8_t* at) noexcept -> std::int32_t {
    std::int32_t value = 0;
    std::memcpy(&value, at, sizeof(value));
    return value;
}

// One step of the seed pair, exactly as 50E48C..50E4A5, 50E530..50E566 and
// 5120C9..5120FC store it: next = low * 6AC690C5h + high, written back as
// low/high halves. Returns the new low half.
inline auto AdvanceSeed(std::uint32_t* seed) noexcept -> std::uint32_t {
    const std::uint64_t next = static_cast<std::uint64_t>(seed[0]) * SeedMultiplier + seed[1];
    seed[0] = static_cast<std::uint32_t>(next);
    seed[1] = static_cast<std::uint32_t>(next >> 32);
    return static_cast<std::uint32_t>(next);
}

// 0..count-1 as the game rolls it: a mask for a power of two, otherwise an
// unsigned remainder of the low half. count must be at least 1.
inline auto RollBelow(std::uint32_t* seed, std::uint32_t count) noexcept -> std::uint32_t {
    const std::uint32_t low = AdvanceSeed(seed);
    return (count & (count - 1U)) != 0 ? low % count : low & (count - 1U);
}

// The game's own remap at 50E59A..50E5C3.
inline auto FinalRow(std::uint32_t row, bool forcePortal) noexcept -> std::uint32_t {
    if (forcePortal) return ForcedPortalRow;
    if (row == 5) return 3;
    if (row == 4) return 2;
    if (row == 16) return 18;
    return row;
}

inline auto LevelMinOf(const NativeCalls& natives, std::uint8_t dataContext, std::uint32_t row) noexcept
    -> std::uint32_t {
    return ReadU32(natives.record(dataContext, row) + RecordLevelMin);
}

// True when the shrine this row finally turns into is not removed. A final
// row outside the table cannot be judged and is kept, which leaves it to
// the game exactly as it would be without this plugin.
inline auto Keeps(const NativeCalls& natives, const bool* removedCodes, std::uint8_t dataContext,
                  std::int32_t rowCount, std::uint32_t row, bool forcePortal) noexcept -> bool {
    const std::uint32_t finalRow = FinalRow(row, forcePortal);
    if (rowCount <= 0 || finalRow >= static_cast<std::uint32_t>(rowCount)) return true;
    return !removedCodes[natives.record(dataContext, finalRow)[RecordCode]];
}

// Parm0 0: the domain drawn at 50E570 is rows 1..count-1. The kept rows are
// counted and indexed in place, so there is no pool to size.
inline auto TableKeptCount(const NativeCalls& natives, const bool* removedCodes, std::uint8_t dataContext,
                           std::int32_t rowCount, bool forcePortal) noexcept -> std::uint32_t {
    std::uint32_t kept = 0;
    for (std::int32_t row = 1; row < rowCount; ++row) {
        if (Keeps(natives, removedCodes, dataContext, rowCount, static_cast<std::uint32_t>(row), forcePortal)) ++kept;
    }
    return kept;
}

inline auto TableKeptAt(const NativeCalls& natives, const bool* removedCodes, std::uint8_t dataContext,
                        std::int32_t rowCount, bool forcePortal, std::uint32_t index) noexcept -> std::uint32_t {
    for (std::int32_t row = 1; row < rowCount; ++row) {
        const auto candidate = static_cast<std::uint32_t>(row);
        if (!Keeps(natives, removedCodes, dataContext, rowCount, candidate, forcePortal)) continue;
        if (index == 0) return candidate;
        --index;
    }
    return 1;
}

// The picker's class rule at 512067..512077: anything outside 1..4 is 2.
inline auto NormaliseClass(std::uint8_t shrineClass) noexcept -> std::uint8_t {
    return static_cast<std::uint8_t>(shrineClass - 1U) > 3U ? std::uint8_t{2} : shrineClass;
}

inline auto ReadClassList(const NativeCalls& natives, void* game, std::uint8_t shrineClass) noexcept -> ClassList {
    const std::uint8_t* lists = natives.classLists(game);
    ClassList           list{};
    list.count = ReadI32(lists + ClassCountsOffset + 4U * shrineClass);
    std::memcpy(&list.entries, lists + ClassListsOffset + 8U * shrineClass, sizeof(list.entries));
    return list;
}

// A list entry as the picker reads it at 51210D..512119: 0 means row 1.
inline auto EntryRow(const ClassList& list, std::uint32_t index) noexcept -> std::uint32_t {
    const std::int32_t entry = list.entries[index];
    return entry != 0 ? static_cast<std::uint32_t>(entry) : 1U;
}

inline auto ClassKeptCount(const NativeCalls& natives, const bool* removedCodes, std::uint8_t dataContext,
                           std::int32_t rowCount, bool forcePortal, const ClassList& list) noexcept -> std::uint32_t {
    std::uint32_t kept = 0;
    for (std::int32_t index = 0; index < list.count; ++index) {
        if (Keeps(natives, removedCodes, dataContext, rowCount, EntryRow(list, static_cast<std::uint32_t>(index)),
                  forcePortal)) {
            ++kept;
        }
    }
    return kept;
}

inline auto ClassKeptAt(const NativeCalls& natives, const bool* removedCodes, std::uint8_t dataContext,
                        std::int32_t rowCount, bool forcePortal, const ClassList& list,
                        std::uint32_t index) noexcept -> std::uint32_t {
    for (std::int32_t position = 0; position < list.count; ++position) {
        const std::uint32_t row = EntryRow(list, static_cast<std::uint32_t>(position));
        if (!Keeps(natives, removedCodes, dataContext, rowCount, row, forcePortal)) continue;
        if (index == 0) return row;
        --index;
    }
    return 1;
}

// The picker 0x512050 with one change: each draw is made over the rows that
// are kept. When removal leaves the class empty, the fallback effectclass is
// used instead; when that is empty too, the original draw over the full
// class is made. Shared by the initializer and the healing well.
inline auto SelectClassRow(const NativeCalls& natives, const bool* removedCodes, void* game,
                           std::uint8_t shrineClass, std::uint8_t fallbackClass, std::uint32_t levelId,
                           std::uint8_t dataContext, std::int32_t rowCount, bool forcePortal) noexcept -> Selection {
    Selection out{};
    shrineClass     = NormaliseClass(shrineClass);
    out.shrineClass = shrineClass;

    const ClassList list = ReadClassList(natives, game, shrineClass);
    if (list.count == 0 || list.entries == nullptr) {
        out.row     = 1;   // 51214D: assert, return 1
        out.outcome = Outcome::EmptyInData;
        return out;
    }

    ClassList     drawList = list;
    std::uint32_t kept     = ClassKeptCount(natives, removedCodes, dataContext, rowCount, forcePortal, list);
    if (kept == 0 && fallbackClass != 0 && fallbackClass < ClassCount && fallbackClass != shrineClass) {
        const ClassList     fallback     = ReadClassList(natives, game, fallbackClass);
        const std::uint32_t fallbackKept = fallback.count > 0 && fallback.entries != nullptr
                                             ? ClassKeptCount(natives, removedCodes, dataContext, rowCount,
                                                              forcePortal, fallback)
                                             : 0;
        if (fallbackKept != 0) {
            drawList        = fallback;
            kept            = fallbackKept;
            out.outcome     = Outcome::Redirected;
            out.shrineClass = fallbackClass;
        }
    }
    if (kept == 0) out.outcome = Outcome::AllRemoved;

    std::uint32_t row = 0;
    for (std::int32_t attempt = DrawAttempts; attempt > 0; --attempt) {
        std::uint32_t* seed = natives.objectControl(game);
        if (kept != 0) {
            row = ClassKeptAt(natives, removedCodes, dataContext, rowCount, forcePortal, drawList,
                              RollBelow(seed, kept));
        } else {
            const std::uint32_t index = list.count > 0 ? RollBelow(seed, static_cast<std::uint32_t>(list.count)) : 0;
            row = EntryRow(list, index);
        }
        if (levelId >= LevelMinOf(natives, dataContext, row)) break;
    }
    out.row = row;
    return out;
}

// 0x50E450 up to the remap, with one change: each draw is made over the
// rows that are kept instead of over every row. Every roll consumes the
// same seed the original uses at the same moment.
inline auto SelectRow(const NativeCalls& natives, const bool* removedCodes, void* game,
                      std::uint32_t* regionSeed, std::int32_t parm0, std::uint32_t levelId,
                      std::uint8_t dataContext, bool forcePortal, std::uint8_t fallbackClass) noexcept -> Selection {
    const std::int32_t rowCount = natives.rowCount(dataContext);

    if (parm0 == 0) {
        // 50E4E8..50E58E, the whole table on the object region seed.
        Selection           out{};
        const std::uint32_t kept = TableKeptCount(natives, removedCodes, dataContext, rowCount, forcePortal);
        if (kept == 0) out.outcome = rowCount - 1 > 0 ? Outcome::AllRemoved : Outcome::EmptyInData;
        std::uint32_t row = 0;
        for (std::int32_t attempt = DrawAttempts; attempt > 0; --attempt) {
            if (kept != 0) {
                row = TableKeptAt(natives, removedCodes, dataContext, rowCount, forcePortal,
                                  RollBelow(regionSeed, kept));
            } else if (rowCount - 1 > 0) {
                row = 1U + RollBelow(regionSeed, static_cast<std::uint32_t>(rowCount - 1));
            } else {
                row = 1;   // 50E52C xor edx, edx: nothing to roll
            }
            if (levelId >= LevelMinOf(natives, dataContext, row)) break;
        }
        out.row         = row;
        out.shrineClass = 0;
        return out;
    }

    // 50E47A..50E4C8, the class from Parm0 and one region-seed roll.
    std::uint8_t shrineClass = 4;
    if (parm0 == 1) {
        shrineClass = 2;
    } else if (parm0 == 2) {
        shrineClass = 3;
    } else if (AdvanceSeed(regionSeed) % 10U == 0) {
        shrineClass = 1;
    }
    return SelectClassRow(natives, removedCodes, game, shrineClass, fallbackClass, levelId, dataContext, rowCount,
                          forcePortal);
}
// SELECTION-LOGIC-END

using ShrineInitFn      = void*(__fastcall*)(void* initContext) noexcept;
using LevelIdFromRoomFn = std::uint32_t(__fastcall*)(void* room) noexcept;
using SetInteractTypeFn = void(__fastcall*)(void* object, std::uint8_t interactType) noexcept;
using SetShrineRecordFn = void*(__fastcall*)(void* object, const std::uint8_t* record) noexcept;

constexpr std::size_t OperateGame     = 0x00;
constexpr std::size_t OperateObject   = 0x08;
constexpr std::size_t OperatePlayer   = 0x10;
constexpr std::size_t RecordArg0      = 0x04;
constexpr std::size_t RecordDuration  = 0x0C;
constexpr std::size_t GameDifficulty  = 0x104;

constexpr std::uint8_t ArmorCode        = 6;
constexpr std::uint8_t CombatCode       = 7;
constexpr std::uint8_t FireResistCode   = 8;
constexpr std::uint8_t PoisonResistCode = 11;
constexpr std::uint8_t SkillCode        = 12;
constexpr std::uint8_t ManaRechargeCode = 13;
constexpr std::uint8_t FirstChartCode   = 6;
constexpr std::uint8_t LastChartCode    = 13;

constexpr std::int32_t  NoStat             = -1;
constexpr std::uint32_t HpRegenStat        = 74;
constexpr std::uint32_t ManaRecoveryStat   = 26;
constexpr std::uint32_t AllSkillsStat      = 127;
constexpr std::int32_t  SkillDummyStat     = 11;
constexpr std::int32_t  EngineSkillBonus   = 2;
constexpr std::uint32_t AbsorbDamageEvent  = 11;

// Codes 8..11 -> maxfireresist 40, maxcoldresist 44, maxlightresist 42, maxpoisonresist 46.
constexpr std::array<std::uint32_t, 4> MaxResistStats{ 40, 44, 42, 46 };

struct ApplyArgs {
    void*        source;     // +00
    void*        target;     // +08
    std::int32_t skill;      // +10
    std::int32_t level;      // +14
    std::int32_t duration;   // +18
    std::int32_t stat;       // +1C
    std::int32_t value;      // +20
    std::int32_t state;      // +24
    void*        onRemove;   // +28
};
static_assert(sizeof(ApplyArgs) == 0x30);
static_assert(offsetof(ApplyArgs, duration) == 0x18 && offsetof(ApplyArgs, stat) == 0x1C
              && offsetof(ApplyArgs, value) == 0x20 && offsetof(ApplyArgs, state) == 0x24
              && offsetof(ApplyArgs, onRemove) == 0x28);

using ShrineHandlerFn  = std::uint64_t(__fastcall*)(std::uint8_t dataContext, std::uint8_t* operate,
                                                    const std::uint8_t* record) noexcept;
using ValueHelperFn    = std::int32_t(__fastcall*)(std::uint8_t dataContext, const std::uint8_t* record,
                                                   std::int32_t stat, std::uint32_t arg0, void* unit) noexcept;
using ApplyTimedFn     = void*(__fastcall*)(ApplyArgs* args) noexcept;
using SetListStatFn    = std::int64_t(__fastcall*)(std::uint32_t dataContext, void* list, std::uint32_t stat,
                                                   std::int32_t value, std::uint32_t layer) noexcept;
using MaxStatFn        = std::int32_t(__fastcall*)(void* unit) noexcept;
using CheckStateFn     = std::int32_t(__fastcall*)(void* unit, std::int32_t state) noexcept;
using SkillRefreshFn   = std::uint64_t(__fastcall*)(void* unit) noexcept;
using UnitEventFn      = std::int64_t(__fastcall*)(void* game, std::uint32_t eventId, void* unit, void* other,
                                                   std::uint8_t* damage) noexcept;

// DAMAGE-LOGIC-BEGIN
// Pure arithmetic, also compiled by the offline tests.

constexpr std::array<std::size_t, 10> DamageOffsets{
    0x18,    // Phys
    0x20,    // Fire
    0x24,    // Burn
    0x2C,    // Ligt
    0x30,    // Magc
    0x34,    // Cold
    0x38,    // Pois
    0x120,   // Life drain
    0x124,   // Mana drain
    0x128,   // Stam drain
};
constexpr std::size_t   PoisonSourceListOffset  = 0x40;
constexpr std::size_t   PoisonSourceCountOffset = 0x48;
constexpr std::size_t   PoisonSourceStride      = 12;
constexpr std::size_t   PoisonSourceDamage      = 4;
constexpr std::uint64_t MaximumPoisonSources    = 4096;
constexpr std::int64_t  FramesPerSecond         = 25;

// value * keep / 100 for positive values, as the game scales components.
inline auto ScaleDown(std::int32_t value, std::int32_t keepPercent) noexcept -> std::int32_t {
    if (value <= 0) return value;
    return static_cast<std::int32_t>(static_cast<std::int64_t>(value) * keepPercent / 100);
}

inline void ScaleIncomingDamage(std::uint8_t* damage, std::int32_t keepPercent) noexcept {
    for (const std::size_t offset : DamageOffsets) {
        std::int32_t value = 0;
        std::memcpy(&value, damage + offset, sizeof(value));
        value = ScaleDown(value, keepPercent);
        std::memcpy(damage + offset, &value, sizeof(value));
    }
    std::uint8_t* sources = nullptr;
    std::uint64_t count   = 0;
    std::memcpy(&sources, damage + PoisonSourceListOffset, sizeof(sources));
    std::memcpy(&count, damage + PoisonSourceCountOffset, sizeof(count));
    if (sources == nullptr || count == 0 || count > MaximumPoisonSources) return;
    for (std::uint64_t index = 0; index < count; ++index) {
        std::uint8_t* field = sources + index * PoisonSourceStride + PoisonSourceDamage;
        std::int32_t  value = 0;
        std::memcpy(&value, field, sizeof(value));
        value = ScaleDown(value, keepPercent);
        std::memcpy(field, &value, sizeof(value));
    }
}

// percent of `maximum` (1/256 units) per second, as 1/256 units per frame.
inline auto RegenPerFrame(std::int32_t maximum, std::int32_t percentPerSecond) noexcept -> std::int32_t {
    if (maximum <= 0 || percentPerSecond <= 0) return 0;
    const std::int64_t perFrame = static_cast<std::int64_t>(maximum) * percentPerSecond / (100 * FramesPerSecond);
    return perFrame > INT32_MAX ? INT32_MAX : static_cast<std::int32_t>(perFrame);
}
// DAMAGE-LOGIC-END

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

constexpr char         RemovalSection[]   = "removal";
constexpr char         EffectsSection[]   = "effects";
constexpr char         SharingSection[]   = "sharing";
constexpr std::size_t  InitialConfigBytes = 16'384;
constexpr std::size_t  MaximumConfigBytes = 1'048'576;
constexpr std::size_t  RejectedSampleSize = 5;
constexpr std::int32_t MaximumCode        = 255;
constexpr std::int32_t MaximumClass       = 7;
constexpr std::size_t  MaximumStatList    = 16;
constexpr std::int32_t MaximumStatId      = 0xFFFF;
constexpr std::int32_t MinimumRadius      = 1;
constexpr std::int32_t MaximumRadius      = 1000;

constexpr char DefaultConfigToml[] =
    "# Shrine Rework\n"
    "#\n"
    "# Three parts, each with its own switch:\n"
    "#   [removal]  takes shrine types out of the game\n"
    "#   [effects]  changes what the Armor, Combat, Resist, Skill and Mana\n"
    "#              Recharge shrines give\n"
    "#   [sharing]  gives a clicked shrine's effect to everyone around it\n"
    "#\n"
    "# Durations are not changed here: every shrine lasts its shrines.txt\n"
    "# Duration in frames (25 frames = 1 second, 1500 = 1 minute).\n"
    "#\n"
    "# Server side only. In single player the game hosts its own server, so it\n"
    "# applies there too.\n"
    "#\n"
    "# Console command: shrines (status, removed Codes, what was assigned, the\n"
    "# shrine effect counters and who sharing reached)\n"
    "\n"
    "# ---------------------------------------------------------------------------\n"
    "# Removal\n"
    "# ---------------------------------------------------------------------------\n"
    "# A removed shrine never appears on a shrine object again: the object gets\n"
    "# one of the remaining shrines instead, and the remaining shrines keep their\n"
    "# odds relative to each other.\n"
    "#\n"
    "# How a shrine object gets its type (objects.txt InitFn 1, when its level is\n"
    "# built):\n"
    "#   objects.txt Parm0 0          any shrines.txt row except row 0, equal odds\n"
    "#   objects.txt Parm0 1          a row whose effectclass is 2\n"
    "#   objects.txt Parm0 2          a row whose effectclass is 3\n"
    "#   objects.txt Parm0 any other  a row whose effectclass is 4, or 1 time in 10\n"
    "#                                a row whose effectclass is 1\n"
    "# If the area's level id is below the drawn row's LevelMin, the game draws\n"
    "# again, at most 8 times, then keeps the last draw anyway. Afterwards the game\n"
    "# itself turns row 4 into row 2, row 5 into row 3 and row 16 into row 18,\n"
    "# which is why the Health Exchange, Mana Exchange and Enirhs shrines never\n"
    "# appear.\n"
    "#\n"
    "# A row counts as removed when the shrine it ends up as (after that last\n"
    "# step) has one of the Codes below, so a row the game would turn into a\n"
    "# removed shrine is out too. The number of draws, LevelMin and everything\n"
    "# else stay as they are.\n"
    "#\n"
    "# Healing and mana wells are shrine objects too: they draw only from\n"
    "# effectclass 2 (Health) and 3 (Mana). When every shrine an object could draw\n"
    "# is removed, it draws from fallback_effectclass instead (1 = Refilling, which\n"
    "# restores both life and mana). Each level's guaranteed healing well is\n"
    "# forced to row 2 (Health) by the game; when that shrine is removed, the well\n"
    "# draws from effectclass 2 with the same fallback.\n"
    "#\n"
    "# If nothing is left in the fallback either, that object draws from the full\n"
    "# list exactly as the game does, and the plugin log reports it.\n"
    "\n"
    "[removal]\n"
    "\n"
    "# Switch for this part. false installs none of it.\n"
    "enabled = true\n"
    "\n"
    "# The shrines to remove, as shrines.txt Code values, 0 to 255. Code is the\n"
    "# column that decides what a shrine does. An empty list, [], removes nothing.\n"
    "# The vanilla Codes are:\n"
    "#    1 Refilling       2 Health          3 Mana            6 Armor\n"
    "#    7 Combat          8 Resist Fire     9 Resist Cold    10 Resist Lightning\n"
    "#   11 Resist Poison  12 Skill          13 Mana Recharge  14 Stamina\n"
    "#   15 Experience     17 Portal         18 Gem            19 Fire\n"
    "#   20 Monster        21 Exploding      22 Poison\n"
    "# The list may be written on one line or spread over several lines, and may\n"
    "# carry comments.\n"
    "removed_shrine_codes = [\n"
    "    2,    # Health Shrine\n"
    "    3,    # Mana Shrine\n"
    "    14,   # Stamina Shrine\n"
    "    17,   # Portal Shrine\n"
    "    19,   # Fire Shrine\n"
    "    21,   # Exploding Shrine\n"
    "    22,   # Poison Shrine\n"
    "]\n"
    "\n"
    "# The effectclass an object draws from when removal leaves its own\n"
    "# effectclass empty, 1 to 7. 0 turns the fallback off.\n"
    "fallback_effectclass = 1\n"
    "\n"
    "# true writes one plugin log line per shrine object: level id, objects.txt\n"
    "# Parm0, the pool it drew from, the row and its Code. For checking the result\n"
    "# in game; leave it false otherwise.\n"
    "log_picks = false\n"
    "\n"
    "# ---------------------------------------------------------------------------\n"
    "# Effects\n"
    "# ---------------------------------------------------------------------------\n"
    "# Every setting below can be switched off on its own; a shrine whose setting\n"
    "# is off works exactly as in the game, and so does every shrine not listed.\n"
    "\n"
    "[effects]\n"
    "\n"
    "# Switch for this part. false installs none of it.\n"
    "enabled = true\n"
    "\n"
    "# Armor Shrine (Code 6). While its state is on (128, shrine_armor), the\n"
    "# player takes this much less damage, in percent, from every hit: physical,\n"
    "# fire, lightning, cold, magic, poison, burning, and life, mana and stamina\n"
    "# drain. It is applied at the same point as the game's own difficulty damage\n"
    "# percent, before absorbs (Energy Shield and similar) and before\n"
    "# resistances, so the poison, burning and bleed a hit leaves behind are\n"
    "# reduced too. The shrine no longer gives defense, so its shrines.txt Arg0 is\n"
    "# not used. 0 turns this off.\n"
    "armor_damage_reduction = 33\n"
    "\n"
    "# Combat Shrine (Code 7). The stats it gives instead of attack rating and\n"
    "# damage, as itemstatcost.txt row ids, each with the value below. Its\n"
    "# shrines.txt Arg0 and Arg1 are not used. An empty list, [], turns this off.\n"
    "combat_stats = [398, 832, 833]\n"
    "combat_value = 50\n"
    "\n"
    "# Resist Fire, Cold, Lightning and Poison Shrines (Codes 8, 9, 10, 11). On\n"
    "# top of their resistance (shrines.txt Arg0, unchanged) they also give this\n"
    "# much maximum resistance of the same element: maxfireresist 40,\n"
    "# maxcoldresist 44, maxlightresist 42, maxpoisonresist 46. 0 turns this off.\n"
    "resist_max_bonus = 10\n"
    "\n"
    "# Skill Shrine (Code 12). Total bonus to all skills on Normal, Nightmare and\n"
    "# Hell. The game itself gives +2 while the shrine state (134) is on; the\n"
    "# plugin adds item_allskills for the rest. [2, 2, 2] turns this off.\n"
    "skill_bonus = [2, 4, 6]\n"
    "\n"
    "# Mana Recharge Shrine (Code 13). Regenerates this percent of maximum life\n"
    "# and of maximum mana every second, as hpregen and manarecovery on the\n"
    "# shrine state (135). The maximums are taken when the shrine is clicked.\n"
    "# Replaces the game's mana regeneration bonus, so its shrines.txt Arg0 is not\n"
    "# used. 0 turns this off.\n"
    "mana_recharge_regen_percent = 2\n"
    "\n"
    "# ---------------------------------------------------------------------------\n"
    "# Sharing\n"
    "# ---------------------------------------------------------------------------\n"
    "# When a shrine is clicked, everyone within the radius below gets its effect\n"
    "# too: every other player, in your party or not, and every hireling and\n"
    "# summon of any player. The one who clicks gets it exactly as before. The\n"
    "# radius is measured from the shrine. Dead units, player corpses and units in\n"
    "# another act are left out.\n"
    "#\n"
    "# Who gets which shrine (shrines.txt Code):\n"
    "#   everyone in range       1 Refilling, 2 Health, 3 Mana, 6 Armor, 7 Combat,\n"
    "#                           8 to 11 Resist Fire, Cold, Lightning and Poison,\n"
    "#                           12 Skill, 13 Mana Recharge, 15 Experience, and a\n"
    "#                           Code with no effect of its own (the game refills\n"
    "#                           life and mana for it)\n"
    "#   other players only      4 Health Exchange, 5 Mana Exchange, 14 Stamina,\n"
    "#                           16 Enirhs, 18 Gem. They work on a player's own\n"
    "#                           stamina, name or inventory: on a summon, Enirhs\n"
    "#                           would crash the game and Gem would drop a gem\n"
    "#                           next to it.\n"
    "#   only the one clicking   17 Portal, 19 Fire, 20 Monster, 21 Exploding,\n"
    "#                           22 Poison. They act on the area around the\n"
    "#                           shrine, so sharing them would open several\n"
    "#                           portals, release several novas or upgrade\n"
    "#                           several monsters.\n"
    "#\n"
    "# Everyone gets the effect as [effects] above sets it.\n"
    "\n"
    "[sharing]\n"
    "\n"
    "# Switch for this part. false installs none of it.\n"
    "enabled = true\n"
    "\n"
    "# The radius in yards, from 1 to 1000. A yard is 1.5 game units (subtiles),\n"
    "# the same yard skill descriptions use: Inner Sight's \"Radius: %d yards\" line\n"
    "# in skilldesc.txt is its aura range in game units times 2, divided by 3.\n"
    "# 40 yards = 60 game units.\n"
    "radius_yards = 40\n";

struct Settings {
    // [removal]
    bool         removalEnabled = true;
    bool         logPicks       = false;
    std::uint8_t fallbackClass  = 1;
    // [effects]
    bool                        effectsEnabled = true;
    std::int32_t                armorReduction = 33;
    std::vector<std::int32_t>   combatStats    = { 398, 832, 833 };
    std::int32_t                combatValue    = 50;
    std::int32_t                resistMaxBonus = 10;
    std::array<std::int32_t, 3> skillBonus     = { 2, 4, 6 };
    std::int32_t                regenPercent   = 2;
    // [sharing]
    bool         sharingEnabled = true;
    std::int32_t radiusYards    = 40;
};

struct ListReport {
    bool         found        = false;
    bool         malformed    = false;
    bool         unterminated = false;
    std::int32_t rejected     = 0;
    std::string  rejectedSample;
};

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

enum class HookState : std::uint8_t {
    NotAttempted,
    DisabledByConfig,
    NothingRemoved,
    ConfigError,
    UnsupportedBuild,
    InstallFailed,
    Installed,
};

const D2RL::PluginContext*  Context{};
std::uintptr_t              Base{};
Settings                    Config{};
std::array<bool, CodeCount> RemovedCodes{};
std::int32_t                RemovedCount{};
bool                        ConfigFileWasRead{};
bool                        RemovalConfigUsable{true};
bool                        EffectsConfigUsable{true};
bool                        SharingConfigUsable{true};
std::atomic<bool>           RuleActive{false};
std::atomic<HookState>      RemovalState{HookState::NotAttempted};
char                        EffectsInactive[200]{};

// Removal
NativeCalls       Natives{};
ShrineInitFn      OriginalShrineInit{};
LevelIdFromRoomFn LevelIdFromRoom{};
SetInteractTypeFn SetInteractType{};
SetShrineRecordFn SetShrineRecord{};
bool              HealingWellPatched{};

std::atomic<std::uint64_t>                        TableDraws{};
std::atomic<std::uint64_t>                        ClassDraws{};
std::atomic<std::uint64_t>                        RedirectedDraws{};
std::atomic<std::uint64_t>                        HealingWellsChanged{};
std::atomic<std::uint64_t>                        AllRemovedDraws{};
std::atomic<std::uint64_t>                        EmptyInDataDraws{};
std::array<std::atomic<std::uint32_t>, CodeCount> AssignedByCode{};
std::atomic<std::uint32_t>                        LastLevel{};
std::atomic<std::int32_t>                         LastParm0{};
std::atomic<std::uint32_t>                        LastClass{};
std::atomic<std::uint32_t>                        LastRow{};
std::atomic<std::uint32_t>                        LastCode{};
std::atomic<bool>                                 AnyAssigned{false};

constexpr std::uint64_t ProblemLogLimit = 20;

// Effects
std::atomic<bool> ArmorActive{false};
std::atomic<bool> CombatActive{false};
std::atomic<bool> ResistActive{false};
std::atomic<bool> SkillActive{false};
std::atomic<bool> ManaActive{false};

std::array<std::int32_t, LastChartCode + 1> ChartStat{};
std::array<std::int32_t, LastChartCode + 1> ChartState{};

ShrineHandlerFn OriginalBoost{};
ShrineHandlerFn OriginalSkill{};
ShrineHandlerFn OriginalCombat{};
ValueHelperFn   ValueHelper{};
ApplyTimedFn    ApplyTimed{};
SetListStatFn   SetListStat{};
MaxStatFn       MaxLife{};
MaxStatFn       MaxMana{};
CheckStateFn    CheckState{};
SkillRefreshFn  SkillRefresh{};
UnitEventFn     UnitEvent{};
void*           SkillStateRemoved{};
bool            AbsorbCallPatched{};

std::atomic<std::uint64_t> ArmorClicks{};
std::atomic<std::uint64_t> ReducedHits{};
std::atomic<std::uint64_t> CombatClicks{};
std::atomic<std::uint64_t> ResistClicks{};
std::atomic<std::uint64_t> SkillClicks{};
std::atomic<std::int32_t>  LastSkillExtra{};
std::atomic<std::uint32_t> LastSkillDifficulty{};
std::atomic<std::uint64_t> ManaClicks{};
std::atomic<std::int32_t>  LastLifePerFrame{};
std::atomic<std::int32_t>  LastManaPerFrame{};

// Shared relay page
void* RelayPage{};

void ClearRemoved() noexcept {
    RemovedCodes.fill(false);
    RemovedCount = 0;
}

void SetEffectsInactive(const char* reason) noexcept {
    std::snprintf(EffectsInactive, sizeof(EffectsInactive), "%s", reason);
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

auto StripComment(std::string_view line) noexcept -> std::string_view {
    const std::size_t hash = line.find('#');
    return hash == std::string_view::npos ? line : line.substr(0, hash);
}

auto ParseBool(std::string_view value, bool& out) noexcept -> bool {
    if (value == "true") {
        out = true;
        return true;
    }
    if (value == "false") {
        out = false;
        return true;
    }
    return false;
}

// Decimal, optional leading '+', '_' separators allowed as in TOML.
auto ParseCode(std::string_view text, std::int32_t& out) noexcept -> bool {
    std::size_t index = 0;
    if (!text.empty() && text[0] == '+') index = 1;
    std::int32_t value    = 0;
    bool         anyDigit = false;
    for (; index < text.size(); ++index) {
        const char character = text[index];
        if (character == '_') continue;
        if (character < '0' || character > '9') return false;
        anyDigit = true;
        value    = value * 10 + (character - '0');
        if (value > MaximumCode) return false;
    }
    if (!anyDigit) return false;
    out = value;
    return true;
}

// The text between the brackets, one or more lines already joined with
// commas. Empty elements (a trailing comma, a blank line) are skipped.
void ApplyCodeList(std::string_view text, ListReport& report) noexcept {
    std::size_t cursor = 0;
    while (cursor <= text.size()) {
        const std::size_t      comma   = text.find(',', cursor);
        const std::size_t      end     = comma == std::string_view::npos ? text.size() : comma;
        const std::string_view element = Trim(text.substr(cursor, end - cursor));
        if (!element.empty()) {
            std::int32_t code = 0;
            if (ParseCode(element, code)) {
                if (!RemovedCodes[static_cast<std::size_t>(code)]) {
                    RemovedCodes[static_cast<std::size_t>(code)] = true;
                    ++RemovedCount;
                }
            } else {
                ++report.rejected;
                if (report.rejected <= static_cast<std::int32_t>(RejectedSampleSize)) {
                    if (!report.rejectedSample.empty()) report.rejectedSample += ", ";
                    report.rejectedSample.append(element.substr(0, 24));
                }
            }
        }
        if (comma == std::string_view::npos) break;
        cursor = comma + 1;
    }
}

// Decimal with an optional sign, '_' separators allowed as in TOML.
auto ParseInt(std::string_view text, std::int64_t& out) noexcept -> bool {
    std::size_t index    = 0;
    bool        negative = false;
    if (!text.empty() && (text[0] == '+' || text[0] == '-')) {
        negative = text[0] == '-';
        index    = 1;
    }
    std::int64_t value    = 0;
    bool         anyDigit = false;
    for (; index < text.size(); ++index) {
        const char character = text[index];
        if (character == '_') continue;
        if (character < '0' || character > '9') return false;
        anyDigit = true;
        value    = value * 10 + (character - '0');
        if (value > INT32_MAX) return false;
    }
    if (!anyDigit) return false;
    out = negative ? -value : value;
    return true;
}

// "a, b, c" (the text between the brackets) into integers.
auto ParseIntList(std::string_view text, std::vector<std::int64_t>& out) noexcept -> bool {
    out.clear();
    std::size_t cursor = 0;
    while (cursor <= text.size()) {
        const std::size_t      comma   = text.find(',', cursor);
        const std::size_t      end     = comma == std::string_view::npos ? text.size() : comma;
        const std::string_view element = Trim(text.substr(cursor, end - cursor));
        if (!element.empty()) {
            std::int64_t value = 0;
            if (!ParseInt(element, value)) return false;
            out.push_back(value);
        }
        if (comma == std::string_view::npos) break;
        cursor = comma + 1;
    }
    return true;
}

enum class Part : std::uint8_t {
    None,
    Removal,
    Effects,
    Sharing,
};

auto SectionPart(std::string_view section) noexcept -> Part {
    if (section == RemovalSection) return Part::Removal;
    if (section == EffectsSection) return Part::Effects;
    if (section == SharingSection) return Part::Sharing;
    return Part::None;
}

void WarnKey(const char* key, const char* rule) noexcept {
    D2RL::LogWarnF(Context, "ShrineRework: %s %s; keeping the default.", key, rule);
}

void ApplyRemovalScalar(std::string_view key, std::string_view value) noexcept {
    if (key == "enabled") {
        if (!ParseBool(value, Config.removalEnabled)) WarnKey("[removal] enabled", "must be true or false");
    } else if (key == "log_picks") {
        if (!ParseBool(value, Config.logPicks)) WarnKey("[removal] log_picks", "must be true or false");
    } else if (key == "fallback_effectclass") {
        std::int32_t fallback = 0;
        if (!ParseCode(value, fallback) || fallback > MaximumClass) {
            WarnKey("[removal] fallback_effectclass", "must be a whole number from 0 to 7");
        } else {
            Config.fallbackClass = static_cast<std::uint8_t>(fallback);
        }
    }
}

void ApplyEffectsScalar(std::string_view key, std::string_view value) noexcept {
    std::int64_t number = 0;
    if (key == "enabled") {
        if (!ParseBool(value, Config.effectsEnabled)) WarnKey("[effects] enabled", "must be true or false");
    } else if (key == "armor_damage_reduction") {
        if (!ParseInt(value, number) || number < 0 || number > 100) {
            WarnKey("[effects] armor_damage_reduction", "must be a whole number from 0 to 100");
        } else {
            Config.armorReduction = static_cast<std::int32_t>(number);
        }
    } else if (key == "combat_value") {
        if (!ParseInt(value, number)) {
            WarnKey("[effects] combat_value", "must be a whole number");
        } else {
            Config.combatValue = static_cast<std::int32_t>(number);
        }
    } else if (key == "resist_max_bonus") {
        if (!ParseInt(value, number)) {
            WarnKey("[effects] resist_max_bonus", "must be a whole number");
        } else {
            Config.resistMaxBonus = static_cast<std::int32_t>(number);
        }
    } else if (key == "mana_recharge_regen_percent") {
        if (!ParseInt(value, number) || number < 0 || number > 100) {
            WarnKey("[effects] mana_recharge_regen_percent", "must be a whole number from 0 to 100");
        } else {
            Config.regenPercent = static_cast<std::int32_t>(number);
        }
    }
}

void ApplySharingScalar(std::string_view key, std::string_view value) noexcept {
    std::int64_t number = 0;
    if (key == "enabled") {
        if (!ParseBool(value, Config.sharingEnabled)) WarnKey("[sharing] enabled", "must be true or false");
    } else if (key == "radius_yards") {
        if (!ParseInt(value, number) || number < MinimumRadius || number > MaximumRadius) {
            WarnKey("[sharing] radius_yards", "must be a whole number from 1 to 1000");
        } else {
            Config.radiusYards = static_cast<std::int32_t>(number);
        }
    }
}

void ApplyEffectsList(std::string_view key, std::string_view inner) noexcept {
    std::vector<std::int64_t> values;
    if (key == "combat_stats") {
        if (!ParseIntList(inner, values) || values.size() > MaximumStatList) {
            WarnKey("[effects] combat_stats", "must be a list of up to 16 itemstatcost.txt row ids");
            return;
        }
        std::vector<std::int32_t> stats;
        for (const std::int64_t value : values) {
            if (value < 0 || value > MaximumStatId) {
                WarnKey("[effects] combat_stats", "must hold row ids from 0 to 65535");
                return;
            }
            stats.push_back(static_cast<std::int32_t>(value));
        }
        Config.combatStats = std::move(stats);
    } else if (key == "skill_bonus") {
        if (!ParseIntList(inner, values) || values.size() != 3) {
            WarnKey("[effects] skill_bonus", "must be three whole numbers: Normal, Nightmare, Hell");
            return;
        }
        for (const std::int64_t value : values) {
            if (value < -100 || value > 100) {
                WarnKey("[effects] skill_bonus", "must hold numbers from -100 to 100");
                return;
            }
        }
        for (std::size_t index = 0; index < 3; ++index) {
            Config.skillBonus[index] = static_cast<std::int32_t>(values[index]);
        }
    }
}

void StartRemovedList(ListReport& report) noexcept {
    // The last assignment wins, as with every other key.
    ClearRemoved();
    report.found        = true;
    report.malformed    = false;
    report.unterminated = false;
    report.rejected     = 0;
    report.rejectedSample.clear();
}

void ApplyList(Part part, std::string_view key, std::string_view inner, ListReport& report) noexcept {
    if (part == Part::Removal) {
        if (key == "removed_shrine_codes") ApplyCodeList(inner, report);
    } else if (part == Part::Effects) {
        ApplyEffectsList(key, inner);
    }
}

void ListNeverClosed(Part part, std::string_view key, ListReport& report) noexcept {
    if (part == Part::Removal && key == "removed_shrine_codes") {
        report.unterminated = true;
        ClearRemoved();
    } else if (part == Part::Effects) {
        EffectsConfigUsable = false;
    } else if (part == Part::Sharing) {
        SharingConfigUsable = false;
    }
}

void ParseConfig(std::string_view text, ListReport& report) noexcept {
    Part        part      = Part::None;
    Part        arrayPart = Part::None;
    std::string key;
    std::string arrayText;
    bool        capturing = false;

    std::size_t cursor = 0;
    while (cursor < text.size()) {
        std::size_t end = text.find('\n', cursor);
        if (end == std::string_view::npos) end = text.size();
        const std::string_view line = Trim(StripComment(text.substr(cursor, end - cursor)));
        cursor                      = end + 1;

        if (capturing) {
            if (!line.empty() && line.front() == '[') {
                // A table header before the closing bracket: the list was
                // never closed. Drop it and read the header normally.
                capturing = false;
                ListNeverClosed(arrayPart, key, report);
            } else {
                const std::size_t close = line.find(']');
                arrayText.append(line.substr(0, close));
                arrayText.push_back(',');
                if (close != std::string_view::npos) {
                    capturing = false;
                    ApplyList(arrayPart, key, arrayText, report);
                }
                continue;
            }
        }

        if (line.empty()) continue;

        if (line.front() == '[') {
            const std::size_t close = line.find(']');
            part = SectionPart(Trim(line.substr(1, close == std::string_view::npos ? close : close - 1)));
            continue;
        }

        const std::size_t equals = line.find('=');
        if (equals == std::string_view::npos || part == Part::None) continue;
        const std::string_view name  = Trim(line.substr(0, equals));
        const std::string_view value = Trim(line.substr(equals + 1));
        const bool             codes = part == Part::Removal && name == "removed_shrine_codes";

        if (!value.empty() && value.front() == '[') {
            key.assign(name);
            arrayPart = part;
            if (codes) StartRemovedList(report);
            const std::size_t close = value.find(']');
            if (close == std::string_view::npos) {
                arrayText.assign(value.substr(1));
                arrayText.push_back(',');
                capturing = true;
            } else {
                arrayText.assign(value.substr(1, close - 1));
                ApplyList(part, key, arrayText, report);
            }
        } else if (codes) {
            StartRemovedList(report);
            report.malformed = true;
        } else if (part == Part::Removal) {
            ApplyRemovalScalar(name, value);
        } else if (part == Part::Effects) {
            ApplyEffectsScalar(name, value);
        } else {
            ApplySharingScalar(name, value);
        }
    }

    if (capturing) ListNeverClosed(arrayPart, key, report);
}

auto ReadConfigText(std::string& out) noexcept -> bool {
    std::string   buffer(InitialConfigBytes, '\0');
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

void ReadConfiguration() noexcept {
    std::string text;
    if (!Context->EnsureConfig(DefaultConfigToml)) {
        Context->LogWarn("ShrineRework: the config file could not be created; using the embedded defaults.");
        text = DefaultConfigToml;
    } else if (!ReadConfigText(text)) {
        Context->LogWarn("ShrineRework: the config file could not be read; using the embedded defaults.");
        text = DefaultConfigToml;
    } else {
        ConfigFileWasRead = true;
    }

    ListReport report{};
    ParseConfig(text, report);

    if (!report.found) {
        Context->LogWarn("ShrineRework: removed_shrine_codes is missing from [removal], so nothing is removed.");
    } else if (report.malformed) {
        Context->LogError("ShrineRework: removed_shrine_codes must be a list in square brackets, for example "
                          "[14, 19]. The removal part is not installed.");
        RemovalConfigUsable = false;
    } else if (report.unterminated) {
        Context->LogError("ShrineRework: removed_shrine_codes is missing its closing ']'. The removal part is not "
                          "installed.");
        RemovalConfigUsable = false;
    }
    if (report.rejected > 0) {
        D2RL::LogWarnF(Context,
                       "ShrineRework: %d entr%s in removed_shrine_codes ignored (%s%s). Every entry must be a "
                       "whole number from 0 to %d.",
                       report.rejected, report.rejected == 1 ? "y was" : "ies were", report.rejectedSample.c_str(),
                       report.rejected > static_cast<std::int32_t>(RejectedSampleSize) ? ", ..." : "",
                       MaximumCode);
    }
    if (!EffectsConfigUsable) {
        Context->LogError("ShrineRework: a list in [effects] is missing its closing ']'. The effects part is not "
                          "installed.");
    }
    if (!SharingConfigUsable) {
        Context->LogError("ShrineRework: a value in [sharing] opens a list that is never closed. The sharing part is "
                          "not installed.");
    }
}

// Appends " code code code" in ascending order, stopping before `limit` characters.
void AppendRemovedCodes(std::string& out, std::size_t limit) noexcept {
    char number[8]{};
    for (std::size_t code = 0; code < CodeCount; ++code) {
        if (!RemovedCodes[code]) continue;
        const int length = std::snprintf(number, sizeof(number), " %u", static_cast<unsigned>(code));
        if (length <= 0) continue;
        if (out.size() + static_cast<std::size_t>(length) + 4 > limit) {
            out += " ...";
            return;
        }
        out.append(number, static_cast<std::size_t>(length));
    }
}

// ---------------------------------------------------------------------------
// The removal hooks
// ---------------------------------------------------------------------------

auto ReadPointer(const std::uint8_t* base, std::size_t offset) noexcept -> void* {
    void* value = nullptr;
    std::memcpy(&value, base + offset, sizeof(value));
    return value;
}

auto ForcePortalFlagSet() noexcept -> bool {
    return *reinterpret_cast<const volatile std::uint8_t*>(Base + ForcePortalFlagRva) != 0;
}

void NoteAllRemoved(std::uint32_t levelId, std::int32_t parm0, std::uint8_t shrineClass) noexcept {
    const std::uint64_t count = AllRemovedDraws.fetch_add(1, std::memory_order_relaxed) + 1;
    if (count <= ProblemLogLimit) {
        char pool[48]{};
        if (shrineClass == 0) {
            std::snprintf(pool, sizeof(pool), "the whole table");
        } else {
            std::snprintf(pool, sizeof(pool), "effectclass %u", static_cast<unsigned>(shrineClass));
        }
        D2RL::LogErrorF(Context,
                        "ShrineRework: every shrine in %s and in the fallback effectclass is removed (level %u, "
                        "objects.txt Parm0 %d), so this object drew from the full list exactly as the game does and "
                        "may be a removed shrine. Keep at least one Code of that pool.%s",
                        pool, levelId, parm0, count == ProblemLogLimit ? " Further cases are only counted." : "");
    }
}

void NoteEmptyInData(std::uint32_t levelId, std::int32_t parm0, std::uint8_t shrineClass) noexcept {
    const std::uint64_t count = EmptyInDataDraws.fetch_add(1, std::memory_order_relaxed) + 1;
    if (count <= ProblemLogLimit) {
        D2RL::LogWarnF(Context,
                       "ShrineRework: shrines.txt has no row for this object to draw from (level %u, objects.txt "
                       "Parm0 %d, effectclass %u). It got row 1, as the game gives it. This is a data issue, not a "
                       "removal.%s",
                       levelId, parm0, static_cast<unsigned>(shrineClass),
                       count == ProblemLogLimit ? " Further cases are only counted." : "");
    }
}

auto __fastcall HookShrineInit(void* initContext) noexcept -> void* {
    if (!RuleActive.load(std::memory_order_acquire) || initContext == nullptr) {
        return OriginalShrineInit(initContext);
    }

    const auto* context    = static_cast<const std::uint8_t*>(initContext);
    void*       game       = ReadPointer(context, InitGameOffset);
    void*       object     = ReadPointer(context, InitObjectOffset);
    void*       room       = ReadPointer(context, InitRoomOffset);
    auto*       regionSeed = static_cast<std::uint32_t*>(ReadPointer(context, InitSeedOffset));
    const auto* objectsTxt = static_cast<const std::uint8_t*>(ReadPointer(context, InitObjectsTxtOffset));
    if (game == nullptr || object == nullptr || regionSeed == nullptr || objectsTxt == nullptr) {
        return OriginalShrineInit(initContext);
    }

    const std::int32_t  parm0       = ReadI32(objectsTxt + ObjectsTxtParm0);
    const std::uint32_t levelId     = LevelIdFromRoom(room);
    const std::uint8_t  dataContext = *(static_cast<const std::uint8_t*>(game) + GameDataContext);
    const bool          forcePortal = ForcePortalFlagSet();

    const Selection selection = SelectRow(Natives, RemovedCodes.data(), game, regionSeed, parm0, levelId, dataContext,
                                          forcePortal, Config.fallbackClass);
    if (selection.outcome == Outcome::Redirected) RedirectedDraws.fetch_add(1, std::memory_order_relaxed);
    if (selection.outcome == Outcome::AllRemoved) NoteAllRemoved(levelId, parm0, selection.shrineClass);
    if (selection.outcome == Outcome::EmptyInData) NoteEmptyInData(levelId, parm0, selection.shrineClass);

    // 50E5C6..50E5F2, the original tail.
    const std::uint32_t finalRow = FinalRow(selection.row, forcePortal);
    SetInteractType(object, static_cast<std::uint8_t>(finalRow));
    const std::uint8_t* record = Natives.record(dataContext, finalRow);
    void*               result = SetShrineRecord(object, record);

    const std::int32_t rowCount = Natives.rowCount(dataContext);
    const bool         inTable  = rowCount > 0 && finalRow < static_cast<std::uint32_t>(rowCount);
    const std::uint8_t code = inTable ? record[RecordCode] : std::uint8_t{0};
    (selection.shrineClass == 0 ? TableDraws : ClassDraws).fetch_add(1, std::memory_order_relaxed);
    if (inTable) AssignedByCode[code].fetch_add(1, std::memory_order_relaxed);
    LastLevel.store(levelId, std::memory_order_relaxed);
    LastParm0.store(parm0, std::memory_order_relaxed);
    LastClass.store(selection.shrineClass, std::memory_order_relaxed);
    LastRow.store(finalRow, std::memory_order_relaxed);
    LastCode.store(code, std::memory_order_relaxed);
    AnyAssigned.store(true, std::memory_order_release);

    if (Config.logPicks) {
        char pool[48]{};
        if (selection.shrineClass == 0) {
            std::snprintf(pool, sizeof(pool), "the whole table");
        } else {
            std::snprintf(pool, sizeof(pool), "effectclass %u", static_cast<unsigned>(selection.shrineClass));
        }
        char remap[40]{};
        if (finalRow != selection.row) {
            std::snprintf(remap, sizeof(remap), ", remapped from row %u", selection.row);
        }
        D2RL::LogInfoF(Context, "ShrineRework: level %u, objects.txt Parm0 %d, drew from %s: row %u, Code %u%s.",
                       levelId, parm0, pool, finalRow, static_cast<unsigned>(code), remap);
    }
    return result;
}

// ---------------------------------------------------------------------------
// The healing well (0x50D11B..0x50D140)
// ---------------------------------------------------------------------------

constexpr std::uint32_t HealingWellRow   = 2;   // forced by 50D11B / 50D12C
constexpr std::uint8_t  HealingWellClass = 2;   // the class the original asks the picker for
constexpr std::uint32_t HealingWellLevel = 1;   // the level the original passes at 50D10E

void __fastcall ForcedHealingWell(void* game, void* object) noexcept {
    const std::uint8_t dataContext = *(static_cast<const std::uint8_t*>(game) + GameDataContext);
    std::uint32_t      row         = HealingWellRow;
    if (RuleActive.load(std::memory_order_acquire)) {
        const std::int32_t rowCount = Natives.rowCount(dataContext);
        if (!Keeps(Natives, RemovedCodes.data(), dataContext, rowCount, HealingWellRow, false)) {
            const Selection selection = SelectClassRow(Natives, RemovedCodes.data(), game, HealingWellClass,
                                                       Config.fallbackClass, HealingWellLevel, dataContext, rowCount,
                                                       false);
            row = FinalRow(selection.row, false);
            HealingWellsChanged.fetch_add(1, std::memory_order_relaxed);
            if (selection.outcome == Outcome::AllRemoved) NoteAllRemoved(0, -1, selection.shrineClass);
            if (Config.logPicks) {
                const std::uint8_t code = row < static_cast<std::uint32_t>(rowCount)
                                            ? Natives.record(dataContext, row)[RecordCode]
                                            : std::uint8_t{0};
                D2RL::LogInfoF(Context, "ShrineRework: healing well set to row %u (Code %u) instead of row 2.", row,
                               static_cast<unsigned>(code));
            }
        }
    }
    // 50D11B..50D13C, the original's three calls with the chosen row.
    SetInteractType(object, static_cast<std::uint8_t>(row));
    SetShrineRecord(object, Natives.record(dataContext, row));
}

// ---------------------------------------------------------------------------
// The effects hooks
// ---------------------------------------------------------------------------

// The args record exactly as 591E9E..591ED6 builds it.
auto MakeArgs(const std::uint8_t* operate, const std::uint8_t* record, std::int32_t stat, std::int32_t value,
              std::int32_t state) noexcept -> ApplyArgs {
    ApplyArgs args{};
    args.source   = ReadPointer(operate, OperateObject);
    args.target   = ReadPointer(operate, OperatePlayer);
    args.skill    = 0;
    args.level    = 0;
    args.duration = ReadI32(record + RecordDuration);
    args.stat     = stat;
    args.value    = value;
    args.state    = state;
    args.onRemove = nullptr;
    return args;
}

auto __fastcall HookBoostHandler(std::uint8_t dataContext, std::uint8_t* operate, const std::uint8_t* record) noexcept
    -> std::uint64_t {
    if (!RuleActive.load(std::memory_order_acquire) || operate == nullptr || record == nullptr
        || ReadPointer(operate, OperatePlayer) == nullptr) {
        return OriginalBoost(dataContext, operate, record);
    }
    const std::uint8_t code   = record[RecordCode];
    void*              player = ReadPointer(operate, OperatePlayer);

    if (code == ArmorCode && ArmorActive.load(std::memory_order_relaxed)) {
        // The state alone: the defense bonus is replaced by the damage reduction.
        ApplyArgs args = MakeArgs(operate, record, NoStat, 0, ChartState[ArmorCode]);
        void*     list = ApplyTimed(&args);
        ArmorClicks.fetch_add(1, std::memory_order_relaxed);
        return reinterpret_cast<std::uint64_t>(list);
    }

    if (code >= FireResistCode && code <= PoisonResistCode && ResistActive.load(std::memory_order_relaxed)) {
        // The original resist, then the maximum resist of the same element.
        const std::int32_t stat  = ChartStat[code];
        const std::int32_t value = ValueHelper(dataContext, record, stat,
                                               static_cast<std::uint32_t>(ReadI32(record + RecordArg0)), player);
        ApplyArgs          args  = MakeArgs(operate, record, stat, value, ChartState[code]);
        void*              list  = ApplyTimed(&args);
        if (list != nullptr) {
            SetListStat(dataContext, list, MaxResistStats[static_cast<std::size_t>(code - FireResistCode)],
                        Config.resistMaxBonus, 0);
        }
        ResistClicks.fetch_add(1, std::memory_order_relaxed);
        return reinterpret_cast<std::uint64_t>(list);
    }

    if (code == ManaRechargeCode && ManaActive.load(std::memory_order_relaxed)) {
        const std::int32_t lifePerFrame = RegenPerFrame(MaxLife(player), Config.regenPercent);
        const std::int32_t manaPerFrame = RegenPerFrame(MaxMana(player), Config.regenPercent);
        ApplyArgs args = MakeArgs(operate, record, static_cast<std::int32_t>(HpRegenStat), lifePerFrame,
                                  ChartState[ManaRechargeCode]);
        void*     list = ApplyTimed(&args);
        if (list != nullptr) SetListStat(dataContext, list, ManaRecoveryStat, manaPerFrame, 0);
        ManaClicks.fetch_add(1, std::memory_order_relaxed);
        LastLifePerFrame.store(lifePerFrame, std::memory_order_relaxed);
        LastManaPerFrame.store(manaPerFrame, std::memory_order_relaxed);
        return reinterpret_cast<std::uint64_t>(list);
    }

    return OriginalBoost(dataContext, operate, record);
}

auto __fastcall HookCombatHandler(std::uint8_t dataContext, std::uint8_t* operate, const std::uint8_t* record) noexcept
    -> std::uint64_t {
    if (!RuleActive.load(std::memory_order_acquire) || !CombatActive.load(std::memory_order_relaxed)
        || operate == nullptr || record == nullptr || ReadPointer(operate, OperatePlayer) == nullptr
        || record[RecordCode] != CombatCode) {
        return OriginalCombat(dataContext, operate, record);
    }
    const std::vector<std::int32_t>& stats = Config.combatStats;
    const std::int32_t               first = stats.empty() ? NoStat : stats.front();
    ApplyArgs args = MakeArgs(operate, record, first, first == NoStat ? 0 : Config.combatValue, ChartState[CombatCode]);
    void*     list = ApplyTimed(&args);
    if (list != nullptr) {
        for (std::size_t index = 1; index < stats.size(); ++index) {
            SetListStat(dataContext, list, static_cast<std::uint32_t>(stats[index]), Config.combatValue, 0);
        }
    }
    CombatClicks.fetch_add(1, std::memory_order_relaxed);
    return reinterpret_cast<std::uint64_t>(list);
}

auto __fastcall HookSkillHandler(std::uint8_t dataContext, std::uint8_t* operate, const std::uint8_t* record) noexcept
    -> std::uint64_t {
    if (!RuleActive.load(std::memory_order_acquire) || !SkillActive.load(std::memory_order_relaxed)
        || operate == nullptr || record == nullptr || record[RecordCode] != SkillCode) {
        return OriginalSkill(dataContext, operate, record);
    }
    const auto* game   = static_cast<const std::uint8_t*>(ReadPointer(operate, OperateGame));
    void*       player = ReadPointer(operate, OperatePlayer);
    if (game == nullptr || player == nullptr) return OriginalSkill(dataContext, operate, record);

    std::uint8_t difficulty = game[GameDifficulty];
    if (difficulty > 2) difficulty = 2;
    const std::int32_t extra = Config.skillBonus[difficulty] - EngineSkillBonus;

    // 591F00..591F59: state 134 with the dummy stat and the original remove callback.
    ApplyArgs args = MakeArgs(operate, record, SkillDummyStat, 0, ChartState[SkillCode]);
    args.onRemove  = SkillStateRemoved;
    void* list     = ApplyTimed(&args);
    if (list != nullptr && extra != 0) SetListStat(dataContext, list, AllSkillsStat, extra, 0);
    SkillClicks.fetch_add(1, std::memory_order_relaxed);
    LastSkillExtra.store(extra, std::memory_order_relaxed);
    LastSkillDifficulty.store(difficulty, std::memory_order_relaxed);
    // 591F5E..591F67: the original ends with the skill refresh.
    return SkillRefresh(player);
}

// Alchemy (celestialrayone.alchemy) publishes "mana-burn-immunity"; from its
// version 2 the table also says how much of a hit a unit takes (Blessed Vials:
// less while a Healing Potion effect runs). That reduction is applied here,
// next to the Armor shrine's and the same way, so both act before absorbs and
// resistances. The service is looked up on the first hits and again every 256
// until found, so the load order of the two plugins does not matter. Without
// Alchemy nothing changes.
struct AlchemyServiceTable {
    std::uint32_t serviceSize;
    std::uint32_t serviceVersion;
    bool(__cdecl* manaBurnImmune)(void* player) noexcept;
    std::int32_t(__cdecl* incomingDamagePercent)(void* unit) noexcept;
};

const AlchemyServiceTable* AlchemyService = nullptr;
std::atomic<std::uint32_t> AlchemyLookups{ 0 };
std::atomic<std::uint64_t> AlchemyReducedHits{ 0 };

auto AlchemyDamagePercent(void* unit) noexcept -> std::int32_t {
    if (AlchemyService == nullptr && (AlchemyLookups.fetch_add(1, std::memory_order_relaxed) % 256) == 0) {
        const D2RL::PluginCommunicationService* communication = nullptr;
        if (Context->QueryService(&communication) == D2RL::ServiceQueryResult::Success
            && D2RL::HasPluginCommunicationServiceField(communication, D2RL::PluginCommunicationServiceRequiredSize)) {
            const D2RL::PluginCommunication::AcquireServiceRequest request{
                .structSize       = D2RL::PluginCommunication::AcquireServiceRequestSize,
                .flags            = 0,
                .providerPluginId = "celestialrayone.alchemy",
                .name             = "mana-burn-immunity",
                .minimumVersion   = 2,
                .minimumTableSize = static_cast<std::uint32_t>(sizeof(AlchemyServiceTable)),
            };
            D2RL::PluginCommunication::AcquiredService acquired{};
            acquired.structSize = D2RL::PluginCommunication::AcquiredServiceSize;
            if (communication->acquireService(Context, &request, &acquired) == D2RL::PluginCommunication::Result::Success
                && acquired.table != nullptr) {
                AlchemyService = static_cast<const AlchemyServiceTable*>(acquired.table);
                Context->LogInfo("ShrineRework: Alchemy found; Blessed Vials reduces damage here.");
            }
        }
    }
    if (AlchemyService == nullptr || AlchemyService->incomingDamagePercent == nullptr) return 100;
    const std::int32_t percent = AlchemyService->incomingDamagePercent(unit);
    return percent < 0 ? 0 : (percent > 100 ? 100 : percent);
}

auto __fastcall HookedUnitEvent(void* game, std::uint32_t eventId, void* unit, void* other,
                                std::uint8_t* damage) noexcept -> std::int64_t {
    if (eventId == AbsorbDamageEvent && unit != nullptr && damage != nullptr
        && RuleActive.load(std::memory_order_acquire)) {
        if (ArmorActive.load(std::memory_order_relaxed) && CheckState(unit, ChartState[ArmorCode]) != 0) {
            ScaleIncomingDamage(damage, 100 - Config.armorReduction);
            ReducedHits.fetch_add(1, std::memory_order_relaxed);
        }
        const std::int32_t keep = AlchemyDamagePercent(unit);
        if (keep < 100) {
            ScaleIncomingDamage(damage, keep);
            AlchemyReducedHits.fetch_add(1, std::memory_order_relaxed);
        }
    }
    return UnitEvent(game, eventId, unit, other, damage);
}

// ---------------------------------------------------------------------------
// The sharing hook
// ---------------------------------------------------------------------------

enum class Reach : std::uint8_t {
    ClickerOnly,   // acts on the area around the shrine
    Players,       // acts on a player's own stamina, name or inventory
    Everyone,      // players, hirelings and summons
};

// Indexed by shrines.txt Code. Codes 24 and up take the operate function's
// own refill (58F437), which reaches everyone too.
constexpr std::array<Reach, ChartSlots> CodeReach{
    Reach::Everyone,      //  0  no handler: the refill
    Reach::Everyone,      //  1  Refilling
    Reach::Everyone,      //  2  Health
    Reach::Everyone,      //  3  Mana
    Reach::Players,       //  4  Health Exchange
    Reach::Players,       //  5  Mana Exchange
    Reach::Everyone,      //  6  Armor
    Reach::Everyone,      //  7  Combat
    Reach::Everyone,      //  8  Resist Fire
    Reach::Everyone,      //  9  Resist Cold
    Reach::Everyone,      // 10  Resist Lightning
    Reach::Everyone,      // 11  Resist Poison
    Reach::Everyone,      // 12  Skill
    Reach::Everyone,      // 13  Mana Recharge
    Reach::Players,       // 14  Stamina
    Reach::Everyone,      // 15  Experience
    Reach::Players,       // 16  Enirhs
    Reach::ClickerOnly,   // 17  Portal
    Reach::Players,       // 18  Gem
    Reach::ClickerOnly,   // 19  Fire
    Reach::ClickerOnly,   // 20  Monster
    Reach::ClickerOnly,   // 21  Exploding
    Reach::ClickerOnly,   // 22  Poison
    Reach::Everyone,      // 23  no handler: the refill
};

constexpr auto ReachOf(std::uint8_t code) noexcept -> Reach {
    return code < ChartSlots ? CodeReach[code] : Reach::Everyone;
}

// SHARING-LOGIC-BEGIN
// A yard is 1.5 game units, so a unit is in range when its distance in game
// units is at most yards * 3 / 2: 4 * (dx*dx + dy*dy) <= 9 * yards * yards,
// with no rounding. Coordinates are words, so nothing here can overflow.
constexpr auto WithinYards(std::int64_t dx, std::int64_t dy, std::int64_t yards) noexcept -> bool {
    return 4 * (dx * dx + dy * dy) <= 9 * yards * yards;
}
// SHARING-LOGIC-END

using ShrineOperateFn      = std::int64_t(__fastcall*)(std::uint8_t* operate) noexcept;
using ObjectShrineRecordFn = const std::uint8_t*(__fastcall*)(void* object) noexcept;
using UnitIsDeadFn         = std::int32_t(__fastcall*)(void* unit) noexcept;
using UnitRoomFn           = void*(__fastcall*)(void* unit) noexcept;
using PathCoordinateFn     = std::int32_t(__fastcall*)(void* path) noexcept;
using ActOfLevelFn         = std::uint8_t(__fastcall*)(std::uint8_t dataContext, std::uint32_t levelId) noexcept;
using PlayerCallbackFn     = void(__fastcall*)(void* game, void* player, void* arg) noexcept;
using LivingPlayersFn      = void(__fastcall*)(void* game, PlayerCallbackFn callback, void* arg) noexcept;
using PetCallbackFn        = void(__fastcall*)(void* game, void* owner, void* pet, void* arg) noexcept;
using PetIterateFn         = void(__fastcall*)(void* game, void* player, PetCallbackFn callback, void* arg,
                                               std::uint8_t all) noexcept;

constexpr std::size_t OperateRecordBytes  = 0x24;   // game, shrine, player, object control, int32 row
constexpr std::size_t OperateRecordCopy   = 0x28;
constexpr std::size_t MaximumScanPlayers  = 64;     // a D2R game holds 8
constexpr std::size_t MaximumShareTargets = 512;

ShrineOperateFn      OriginalShrineOperate{};
ObjectShrineRecordFn ObjectShrineRecord{};
UnitIsDeadFn         UnitIsDead{};
UnitRoomFn           UnitRoom{};
PathCoordinateFn     PathGetX{};
PathCoordinateFn     PathGetY{};
ActOfLevelFn         ActOfLevel{};
LivingPlayersFn      LivingPlayers{};
PetIterateFn         PetIterate{};
ShrineHandlerFn      RefillHandler{};

std::atomic<bool>          SharingActive{false};
char                       SharingInactive[200]{};
std::atomic<std::uint64_t> SharedUses{};
std::atomic<std::uint64_t> ClickerOnlyUses{};
std::atomic<std::uint64_t> ReachedPlayers{};
std::atomic<std::uint64_t> ReachedPets{};
std::atomic<std::uint64_t> CappedScans{};
std::atomic<std::uint32_t> LastShareCode{};
std::atomic<std::uint32_t> LastSharePlayers{};
std::atomic<std::uint32_t> LastSharePets{};
std::atomic<bool>          LastShareClickerOnly{false};
std::atomic<bool>          AnyShare{false};

void SetSharingInactive(const char* reason) noexcept {
    std::snprintf(SharingInactive, sizeof(SharingInactive), "%s", reason);
}

struct ShareScan {
    void*                                  clicker     = nullptr;
    std::int32_t                           centerX     = 0;
    std::int32_t                           centerY     = 0;
    std::int32_t                           yards       = 0;
    std::uint8_t                           dataContext = 0;
    std::uint8_t                           act         = 0;
    bool                                   capped      = false;
    std::uint32_t                          playerCount = 0;   // every living player, for the pet walk
    std::uint32_t                          targetCount = 0;
    std::array<void*, MaximumScanPlayers>  players{};
    std::array<void*, MaximumShareTargets> targets{};
};

// 59127C..5912F5, as the Fire shrine reads a position.
auto UnitPosition(void* unit, std::int32_t& x, std::int32_t& y) noexcept -> bool {
    const auto* fields = static_cast<const std::uint8_t*>(unit);
    auto*       path   = static_cast<std::uint8_t*>(ReadPointer(fields, UnitPathOffset));
    if (path == nullptr) return false;
    const std::int32_t type = ReadI32(fields + UnitTypeOffset);
    if (type == UnitTypeObject || type == UnitTypeItem || type == UnitTypeTile) {
        x = ReadI32(path + StaticPathX);
        y = ReadI32(path + StaticPathY);
    } else {
        x = PathGetX(path);
        y = PathGetY(path);
    }
    return true;
}

auto UnitAct(void* unit, std::uint8_t dataContext, std::uint8_t& act) noexcept -> bool {
    void* room = UnitRoom(unit);
    if (room == nullptr) return false;
    act = ActOfLevel(dataContext, LevelIdFromRoom(room));
    return true;
}

auto InRange(const ShareScan& scan, void* unit) noexcept -> bool {
    if (unit == nullptr || unit == scan.clicker || UnitIsDead(unit) != 0) return false;
    std::int32_t x = 0;
    std::int32_t y = 0;
    if (!UnitPosition(unit, x, y)
        || !WithinYards(static_cast<std::int64_t>(x) - scan.centerX, static_cast<std::int64_t>(y) - scan.centerY,
                        scan.yards)) {
        return false;
    }
    std::uint8_t act = 0;
    return UnitAct(unit, scan.dataContext, act) && act == scan.act;
}

void AddTarget(ShareScan& scan, void* unit) noexcept {
    for (std::uint32_t index = 0; index < scan.targetCount; ++index) {
        if (scan.targets[index] == unit) return;
    }
    if (scan.targetCount >= scan.targets.size()) {
        scan.capped = true;
        return;
    }
    scan.targets[scan.targetCount++] = unit;
}

// 0x490D00 calls this with (game, player, arg).
void __fastcall CollectPlayer(void* /*game*/, void* player, void* arg) noexcept {
    auto& scan = *static_cast<ShareScan*>(arg);
    if (scan.playerCount >= scan.players.size()) {
        scan.capped = true;
        return;
    }
    scan.players[scan.playerCount++] = player;
}

// 0x5014C0 calls this with (game, player, pet, arg).
void __fastcall CollectPet(void* /*game*/, void* /*owner*/, void* pet, void* arg) noexcept {
    auto& scan = *static_cast<ShareScan*>(arg);
    if (InRange(scan, pet)) AddTarget(scan, pet);
}

// 58F435..58F461: the chart slot of the Code, or the refill when it has none.
auto ShareHandler(std::uint8_t code) noexcept -> ShrineHandlerFn {
    if (code < ChartSlots) {
        ShrineHandlerFn handler{};
        std::memcpy(&handler, reinterpret_cast<const void*>(Base + ShrineChartRva + ChartStride * code),
                    sizeof(handler));
        if (handler != nullptr) return handler;
    }
    return RefillHandler;
}

void NoteCapped(std::uint8_t code) noexcept {
    const std::uint64_t count = CappedScans.fetch_add(1, std::memory_order_relaxed) + 1;
    if (count <= ProblemLogLimit) {
        D2RL::LogWarnF(Context,
                       "ShrineRework: a shrine (Code %u) found more than %u players or %u units in range; the rest "
                       "did not get it.%s",
                       static_cast<unsigned>(code), static_cast<unsigned>(MaximumScanPlayers),
                       static_cast<unsigned>(MaximumShareTargets),
                       count == ProblemLogLimit ? " Further cases are only counted." : "");
    }
}

void ShareShrine(const std::uint8_t* operate) noexcept {
    void* game   = ReadPointer(operate, OperateGame);
    void* shrine = ReadPointer(operate, OperateObject);
    if (game == nullptr || shrine == nullptr) return;
    const std::uint8_t* record = ObjectShrineRecord(shrine);
    if (record == nullptr) return;

    const std::uint8_t code  = record[RecordCode];
    const Reach        reach = ReachOf(code);
    LastShareCode.store(code, std::memory_order_relaxed);
    if (reach == Reach::ClickerOnly) {
        ClickerOnlyUses.fetch_add(1, std::memory_order_relaxed);
        LastShareClickerOnly.store(true, std::memory_order_relaxed);
        LastSharePlayers.store(0, std::memory_order_relaxed);
        LastSharePets.store(0, std::memory_order_relaxed);
        AnyShare.store(true, std::memory_order_release);
        return;
    }

    ShareScan scan{};
    scan.clicker     = ReadPointer(operate, OperatePlayer);
    scan.yards       = Config.radiusYards;
    scan.dataContext = *(static_cast<const std::uint8_t*>(game) + GameDataContext);
    if (!UnitPosition(shrine, scan.centerX, scan.centerY) || !UnitAct(shrine, scan.dataContext, scan.act)) return;

    LivingPlayers(game, &CollectPlayer, &scan);
    for (std::uint32_t index = 0; index < scan.playerCount; ++index) {
        if (InRange(scan, scan.players[index])) AddTarget(scan, scan.players[index]);
    }
    const std::uint32_t players = scan.targetCount;
    if (reach == Reach::Everyone) {
        // Every player's pets, the clicker's included, wherever the player is.
        for (std::uint32_t index = 0; index < scan.playerCount; ++index) {
            PetIterate(game, scan.players[index], &CollectPet, &scan, 0);
        }
    }
    const std::uint32_t pets = scan.targetCount - players;

    // 58F44C..58F45C once per unit, on a copy of the record with that unit at +10.
    const ShrineHandlerFn handler = ShareHandler(code);
    std::uint8_t          copy[OperateRecordCopy]{};
    for (std::uint32_t index = 0; index < scan.targetCount; ++index) {
        std::memcpy(copy, operate, OperateRecordBytes);
        void* target = scan.targets[index];
        std::memcpy(copy + OperatePlayer, &target, sizeof(target));
        handler(scan.dataContext, copy, record);
    }

    SharedUses.fetch_add(1, std::memory_order_relaxed);
    ReachedPlayers.fetch_add(players, std::memory_order_relaxed);
    ReachedPets.fetch_add(pets, std::memory_order_relaxed);
    LastShareClickerOnly.store(false, std::memory_order_relaxed);
    LastSharePlayers.store(players, std::memory_order_relaxed);
    LastSharePets.store(pets, std::memory_order_relaxed);
    AnyShare.store(true, std::memory_order_release);
    if (scan.capped) NoteCapped(code);
}

auto __fastcall HookShrineOperate(std::uint8_t* operate) noexcept -> std::int64_t {
    const std::int64_t used = OriginalShrineOperate(operate);
    // 58F540: 1 once the shrine has been used now, 0 when it already was.
    if (static_cast<std::uint32_t>(used) == 1 && operate != nullptr && RuleActive.load(std::memory_order_acquire)
        && SharingActive.load(std::memory_order_acquire)) {
        ShareShrine(operate);
    }
    return used;
}

// ---------------------------------------------------------------------------
// The shared relay page and the two patched call sites
// ---------------------------------------------------------------------------

constexpr std::size_t RelayBytes           = 0x1000;
constexpr std::size_t RelaySlotBytes       = 16;
constexpr std::size_t AbsorbRelaySlot      = 0;
constexpr std::size_t HealingWellRelaySlot = 1;

auto CanEncodeRel32(std::uintptr_t from, std::uintptr_t to) noexcept -> bool {
    const std::int64_t delta = static_cast<std::int64_t>(to) - static_cast<std::int64_t>(from) - CallSize;
    return delta >= INT32_MIN && delta <= INT32_MAX;
}

auto AllocateNear(std::uintptr_t hint, std::size_t size) noexcept -> void* {
    SYSTEM_INFO systemInfo{};
    GetSystemInfo(&systemInfo);
    const auto granularity = static_cast<std::uintptr_t>(systemInfo.dwAllocationGranularity);
    const auto aligned     = hint & ~(granularity - 1U);
    for (std::uintptr_t delta = granularity; delta < 0x7000'0000ULL; delta += granularity) {
        const std::uintptr_t candidates[2]{ aligned + delta, aligned > delta ? aligned - delta : 0 };
        for (const std::uintptr_t candidate : candidates) {
            if (candidate == 0 || !CanEncodeRel32(hint, candidate) || !CanEncodeRel32(hint, candidate + size)) continue;
            if (void* memory = VirtualAlloc(reinterpret_cast<void*>(candidate), size, MEM_COMMIT | MEM_RESERVE,
                                            PAGE_READWRITE)) {
                return memory;
            }
        }
    }
    return nullptr;
}

// FF 25 00000000 <absolute target>
void WriteRelayJump(std::uint8_t* at, void* target) noexcept {
    const std::uint8_t jump[6]{ 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00 };
    std::memcpy(at, jump, sizeof(jump));
    const auto absolute = reinterpret_cast<std::uint64_t>(target);
    std::memcpy(at + sizeof(jump), &absolute, sizeof(absolute));
}

auto BuildRelayPage() noexcept -> bool {
    if (RelayPage != nullptr) return true;
    void* page = AllocateNear(Base + AbsorbEventCallRva, RelayBytes);
    if (page == nullptr) return false;
    auto* code = static_cast<std::uint8_t*>(page);
    std::memset(code, 0xCC, RelayBytes);
    WriteRelayJump(code + AbsorbRelaySlot * RelaySlotBytes, reinterpret_cast<void*>(&HookedUnitEvent));
    WriteRelayJump(code + HealingWellRelaySlot * RelaySlotBytes, reinterpret_cast<void*>(&ForcedHealingWell));
    DWORD previous = 0;
    if (!VirtualProtect(code, RelayBytes, PAGE_EXECUTE_READ, &previous)) {
        VirtualFree(page, 0, MEM_RELEASE);
        return false;
    }
    FlushInstructionCache(GetCurrentProcess(), code, 2 * RelaySlotBytes);
    RelayPage = page;
    return true;
}

auto RelaySlotAddress(std::size_t slot) noexcept -> std::uintptr_t {
    return reinterpret_cast<std::uintptr_t>(RelayPage) + slot * RelaySlotBytes;
}

// 50D11B..50D140: 48 8B CE (mov rcx, rsi) / 48 8B D3 (mov rdx, rbx) /
// E8 rel32 to the healing well relay / 27 x 90.
auto PatchHealingWell() noexcept -> bool {
    constexpr std::size_t CallOffset = 6;
    const std::uintptr_t  call       = Base + HealingWellPatchRva + CallOffset;
    if (!BuildRelayPage() || !CanEncodeRel32(call, RelaySlotAddress(HealingWellRelaySlot))) {
        Context->LogError("ShrineRework: no relay page could be placed within reach of 0x50D121; healing wells "
                          "keep row 2.");
        return false;
    }
    std::uint8_t window[HealingWellPatchSize]{};
    std::memset(window, 0x90, sizeof(window));
    const std::uint8_t moves[CallOffset]{ 0x48, 0x8B, 0xCE, 0x48, 0x8B, 0xD3 };
    std::memcpy(window, moves, sizeof(moves));
    window[CallOffset]      = 0xE8;
    const auto displacement = static_cast<std::int32_t>(static_cast<std::int64_t>(RelaySlotAddress(HealingWellRelaySlot))
                                                        - static_cast<std::int64_t>(call + CallSize));
    std::memcpy(window + CallOffset + 1, &displacement, sizeof(displacement));
    if (!Context->PatchBytes(HealingWellPatchRva, HealingWellBody + HealingWellPatchOffset, HealingWellPatchSize,
                             window, HealingWellPatchSize)) {
        Context->LogError("ShrineRework: the healing well window at 0x50D11B could not be patched; healing wells "
                          "keep row 2.");
        return false;
    }
    HealingWellPatched = true;
    return true;
}

// 44EB3A: E8 rel32 retargeted from 0x44D570 to the absorb relay.
auto PatchAbsorbCall() noexcept -> bool {
    const std::uintptr_t site = Base + AbsorbEventCallRva;
    if (!BuildRelayPage() || !CanEncodeRel32(site, RelaySlotAddress(AbsorbRelaySlot))) {
        Context->LogError("ShrineRework: no relay page could be placed within reach of 0x44EB3A.");
        return false;
    }
    std::uint8_t call[CallSize]{ 0xE8 };
    const auto   displacement = static_cast<std::int32_t>(static_cast<std::int64_t>(RelaySlotAddress(AbsorbRelaySlot))
                                                          - static_cast<std::int64_t>(site + CallSize));
    std::memcpy(call + 1, &displacement, sizeof(displacement));
    if (!Context->PatchBytes(AbsorbEventCallRva, AbsorbEventSite + AbsorbCallOffsetInSite, CallSize, call, CallSize)) {
        Context->LogError("ShrineRework: the absorbdamage call at 0x44EB3A could not be retargeted.");
        return false;
    }
    AbsorbCallPatched = true;
    return true;
}

// ---------------------------------------------------------------------------
// Verification and installation
// ---------------------------------------------------------------------------

auto Verify(std::uint64_t rva, const std::uint8_t* expected, std::size_t size, const char* label) noexcept -> bool {
    if (Context->CheckExpectedBytes(rva, expected, static_cast<std::uint32_t>(size))) return true;
    D2RL::LogErrorF(Context,
                    "ShrineRework: %s at 0x%llX does not match the verified D2R image, or another plugin already "
                    "owns it.",
                    label, static_cast<unsigned long long>(rva));
    return false;
}

auto VerifyRemovalContract() noexcept -> bool {
    return Verify(ShrineInitRva, ShrineInitBody, sizeof(ShrineInitBody), "The shrine initializer (InitFn 1)")
        && Verify(ClassPickRva, ClassPickBody, sizeof(ClassPickBody), "The effectclass picker")
        && Verify(LevelIdFromRoomRva, LevelIdFromRoomBody, sizeof(LevelIdFromRoomBody), "The room level id getter")
        && Verify(ShrineRowCountRva, ShrineRowCountBody, sizeof(ShrineRowCountBody), "The shrines.txt row count")
        && Verify(ShrineRecordRva, ShrineRecordEntry, sizeof(ShrineRecordEntry), "The shrines.txt record getter")
        && Verify(SetInteractTypeRva, SetInteractTypeEntry, sizeof(SetInteractTypeEntry),
                  "The object InteractType setter")
        && Verify(SetShrineRecordRva, SetShrineRecordBody, sizeof(SetShrineRecordBody), "The shrine record setter")
        && Verify(ObjectControlRva, ObjectControlBody, sizeof(ObjectControlBody), "The object control getter")
        && Verify(ShrineClassListsRva, ShrineClassListsBody, sizeof(ShrineClassListsBody),
                  "The effectclass list getter");
}

// The (stat, state) words every effect reads from the chart.
auto VerifyChart() noexcept -> bool {
    for (std::uint8_t code = FirstChartCode; code <= LastChartCode; ++code) {
        const std::uint8_t* words = ChartStatsAndStates[code - FirstChartCode];
        if (!Verify(ShrineChartRva + 16ULL * code + 8, words, 8, "The shrine chart")) return false;
        ChartStat[code]  = ReadI32(words);
        ChartState[code] = ReadI32(words + 4);
    }
    return true;
}

template <typename Function>
auto At(std::uint64_t rva) noexcept -> Function {
    return reinterpret_cast<Function>(Base + rva);
}

auto Hook(std::uint64_t rva, const std::uint8_t* witness, std::uint32_t size, void* target, void** original,
          const char* label) noexcept -> bool {
    if (Context->InstallInlineHook(D2RL::MakeInlineHook(rva, witness, size, target, original)) && *original != nullptr) {
        return true;
    }
    D2RL::LogErrorF(Context, "ShrineRework: the loader refused the hook on %s at 0x%llX.", label,
                    static_cast<unsigned long long>(rva));
    return false;
}

void InstallRemoval() noexcept {
    if (!Config.removalEnabled) {
        RemovalState.store(HookState::DisabledByConfig, std::memory_order_release);
        Context->LogInfo("ShrineRework: removal not installed, turned off in the config file.");
        return;
    }
    if (!RemovalConfigUsable) {
        RemovalState.store(HookState::ConfigError, std::memory_order_release);
        return;
    }
    if (RemovedCount == 0) {
        RemovalState.store(HookState::NothingRemoved, std::memory_order_release);
        Context->LogInfo("ShrineRework: removal not installed, removed_shrine_codes is empty.");
        return;
    }
    if (!VerifyRemovalContract()) {
        RemovalState.store(HookState::UnsupportedBuild, std::memory_order_release);
        return;
    }

    Natives.rowCount      = At<ShrineRowCountFn>(ShrineRowCountRva);
    Natives.record        = At<ShrineRecordFn>(ShrineRecordRva);
    Natives.objectControl = At<ObjectControlFn>(ObjectControlRva);
    Natives.classLists    = At<ShrineClassListsFn>(ShrineClassListsRva);
    LevelIdFromRoom       = At<LevelIdFromRoomFn>(LevelIdFromRoomRva);
    SetInteractType       = At<SetInteractTypeFn>(SetInteractTypeRva);
    SetShrineRecord       = At<SetShrineRecordFn>(SetShrineRecordRva);

    // The loader writes the call-through pointer into OriginalShrineInit
    // before the hook goes live.
    if (!Context->InstallInlineHook(D2RL::MakeInlineHook(ShrineInitRva, ShrineInitBody, HookWitnessSize,
                                                         reinterpret_cast<void*>(&HookShrineInit),
                                                         reinterpret_cast<void**>(&OriginalShrineInit)))
        || OriginalShrineInit == nullptr) {
        RemovalState.store(HookState::InstallFailed, std::memory_order_release);
        Context->LogError("ShrineRework: removal not installed. The loader refused the hook on the shrine "
                          "initializer at 0x50E450.");
        return;
    }
    RemovalState.store(HookState::Installed, std::memory_order_release);

    if (Verify(HealingWellRva, HealingWellBody, sizeof(HealingWellBody), "The healing well spawner")) {
        PatchHealingWell();
    }

    std::string codes;
    AppendRemovedCodes(codes, 900);
    D2RL::LogInfoF(Context, "ShrineRework: removal installed, healing wells %s. %d shrine Code%s removed:%s",
                   HealingWellPatched ? "patched" : "NOT patched", RemovedCount, RemovedCount == 1 ? "" : "s",
                   codes.c_str());
}

void InstallEffects() noexcept {
    if (!Config.effectsEnabled) {
        SetEffectsInactive("off in the config file");
        Context->LogInfo("ShrineRework: effects not installed, turned off in the config file.");
        return;
    }
    if (!EffectsConfigUsable) {
        SetEffectsInactive("the [effects] config could not be read, see the log");
        return;
    }
    if (!VerifyChart()) {
        SetEffectsInactive("the shrine chart does not match this game build, see the log");
        return;
    }

    const bool wantArmor  = Config.armorReduction > 0;
    const bool wantResist = Config.resistMaxBonus != 0;
    const bool wantMana   = Config.regenPercent > 0;
    const bool wantCombat = !Config.combatStats.empty();
    const bool wantSkill  = Config.skillBonus[0] != EngineSkillBonus || Config.skillBonus[1] != EngineSkillBonus
                         || Config.skillBonus[2] != EngineSkillBonus;

    // Each part needs only its own proofs, so one mismatch cannot switch the
    // others off. The combat handler's body is the proof of SetWideListStat,
    // so every part that writes extra stats needs it, even with Combat off.
    const bool needBoost    = wantArmor || wantResist || wantMana;
    const bool needListStat = wantResist || wantMana || wantCombat || wantSkill;
    const bool boostBody  = needBoost && Verify(BoostHandlerRva, BoostHandlerBody, sizeof(BoostHandlerBody),
                                                "The boost handler");
    const bool combatBody = needListStat && Verify(CombatHandlerRva, CombatHandlerBody, sizeof(CombatHandlerBody),
                                                   "The combat handler");
    const bool skillBody  = wantSkill && Verify(SkillHandlerRva, SkillHandlerBody, sizeof(SkillHandlerBody),
                                                "The skill handler");
    const bool refillBody = wantMana && Verify(RefillHandlerRva, RefillHandlerBody, sizeof(RefillHandlerBody),
                                               "The Refilling shrine handler");
    const bool gameLayout = wantSkill && Verify(GameLayoutRva, GameLayout, sizeof(GameLayout), "The Game difficulty layout");
    const bool damageCode = wantArmor
                         && Verify(AbsorbEventSiteRva, AbsorbEventSite, sizeof(AbsorbEventSite), "The absorbdamage call")
                         && Verify(ResistLoopLengthsRva, ResistLoopLengths, sizeof(ResistLoopLengths),
                                   "The damage length checks")
                         && Verify(LifeTotalSumRva, LifeTotalSum, sizeof(LifeTotalSum), "The life total sum")
                         && Verify(LifeTotalTailRva, LifeTotalTail, sizeof(LifeTotalTail), "The life total tail")
                         && Verify(PoisonSourcesRva, PoisonSources, sizeof(PoisonSources), "The per-source poison loop");

    ValueHelper       = At<ValueHelperFn>(ValueHelperRva);
    ApplyTimed        = At<ApplyTimedFn>(ApplyTimedEffectRva);
    SetListStat       = At<SetListStatFn>(SetListStatRva);
    MaxLife           = At<MaxStatFn>(MaxLifeRva);
    MaxMana           = At<MaxStatFn>(MaxManaRva);
    CheckState        = At<CheckStateFn>(CheckStateRva);
    SkillRefresh      = At<SkillRefreshFn>(SkillRefreshRva);
    UnitEvent         = At<UnitEventFn>(UnitEventRva);
    SkillStateRemoved = reinterpret_cast<void*>(Base + SkillStateRemovedRva);

    const bool armorReady  = wantArmor && boostBody && damageCode && PatchAbsorbCall();
    const bool resistReady = wantResist && boostBody && combatBody;
    const bool manaReady   = wantMana && boostBody && combatBody && refillBody;
    bool       boostHooked = false;
    if (armorReady || resistReady || manaReady) {
        boostHooked = Hook(BoostHandlerRva, BoostHandlerBody, BoostHookSize, reinterpret_cast<void*>(&HookBoostHandler),
                           reinterpret_cast<void**>(&OriginalBoost), "the boost handler");
    }
    ArmorActive.store(armorReady && boostHooked, std::memory_order_release);
    ResistActive.store(resistReady && boostHooked, std::memory_order_release);
    ManaActive.store(manaReady && boostHooked, std::memory_order_release);

    if (wantCombat && combatBody) {
        CombatActive.store(Hook(CombatHandlerRva, CombatHandlerBody, CombatHookSize,
                                reinterpret_cast<void*>(&HookCombatHandler), reinterpret_cast<void**>(&OriginalCombat),
                                "the combat handler"),
                           std::memory_order_release);
    }
    if (wantSkill && skillBody && combatBody && gameLayout) {
        SkillActive.store(Hook(SkillHandlerRva, SkillHandlerBody, SkillHookSize,
                               reinterpret_cast<void*>(&HookSkillHandler), reinterpret_cast<void**>(&OriginalSkill),
                               "the skill handler"),
                          std::memory_order_release);
    }

    D2RL::LogInfoF(Context,
                   "ShrineRework: effects: armor %s (%d%% less damage), combat %s (%u stats = %d), resists %s (+%d "
                   "max), skill %s (%d/%d/%d), mana recharge %s (%d%% per second).",
                   ArmorActive.load() ? "on" : "off", Config.armorReduction, CombatActive.load() ? "on" : "off",
                   static_cast<unsigned>(Config.combatStats.size()), Config.combatValue,
                   ResistActive.load() ? "on" : "off", Config.resistMaxBonus, SkillActive.load() ? "on" : "off",
                   Config.skillBonus[0], Config.skillBonus[1], Config.skillBonus[2], ManaActive.load() ? "on" : "off",
                   Config.regenPercent);
}

auto VerifyChartHandlers() noexcept -> bool {
    for (std::size_t code = 0; code < ChartSlots; ++code) {
        const std::uint64_t expected = ChartHandlers[code] == 0 ? 0 : static_cast<std::uint64_t>(Base) + ChartHandlers[code];
        if (!Context->CheckExpectedBytes(ShrineChartRva + ChartStride * code, &expected, sizeof(expected))) {
            D2RL::LogErrorF(Context,
                            "ShrineRework: shrine chart slot %u at 0x%llX does not hold the verified handler, or "
                            "another plugin changed it.",
                            static_cast<unsigned>(code),
                            static_cast<unsigned long long>(ShrineChartRva + ChartStride * code));
            return false;
        }
    }
    return true;
}

auto VerifySharingContract() noexcept -> bool {
    return Verify(ShrineOperateRva, ShrineOperateBody, sizeof(ShrineOperateBody), "The shrine operate function (OperateFn 2)")
        && Verify(RefillHandlerRva, RefillHandlerBody, sizeof(RefillHandlerBody), "The Refilling shrine handler")
        && VerifyChartHandlers()
        && Verify(FirePositionRva, FirePositionBlock, sizeof(FirePositionBlock), "The Fire shrine's position reads")
        && Verify(UnitIsDeadRva, UnitIsDeadBody, sizeof(UnitIsDeadBody), "The death check")
        && Verify(UnitRoomRva, UnitRoomBody, sizeof(UnitRoomBody), "The unit room getter")
        && Verify(LevelIdFromRoomRva, LevelIdFromRoomBody, sizeof(LevelIdFromRoomBody), "The room level id getter")
        && Verify(ActOfLevelRva, ActOfLevelBody, sizeof(ActOfLevelBody), "The act resolver")
        && Verify(LivingPlayersRva, LivingPlayersBody, sizeof(LivingPlayersBody), "The living player walk")
        && Verify(PetIterateRva, PetIterateBody, sizeof(PetIterateBody), "The pet walk")
        && Verify(PetWalkRva, PetWalkBody, sizeof(PetWalkBody), "The pet list walk");
}

// yards * 1.5, as text.
void FormatGameUnits(char* out, std::size_t size, std::int32_t yards) noexcept {
    const std::int32_t tenths = yards * 15;
    if (tenths % 10 == 0) {
        std::snprintf(out, size, "%d", tenths / 10);
    } else {
        std::snprintf(out, size, "%d.%d", tenths / 10, tenths % 10);
    }
}

void InstallSharing() noexcept {
    if (!Config.sharingEnabled) {
        SetSharingInactive("off in the config file");
        Context->LogInfo("ShrineRework: sharing not installed, turned off in the config file.");
        return;
    }
    if (!SharingConfigUsable) {
        SetSharingInactive("the [sharing] config could not be read, see the log");
        return;
    }
    if (!VerifySharingContract()) {
        SetSharingInactive("this game build does not match, see the log");
        return;
    }

    ObjectShrineRecord = At<ObjectShrineRecordFn>(ObjectShrineRecordRva);
    UnitIsDead         = At<UnitIsDeadFn>(UnitIsDeadRva);
    UnitRoom           = At<UnitRoomFn>(UnitRoomRva);
    PathGetX           = At<PathCoordinateFn>(PathGetXRva);
    PathGetY           = At<PathCoordinateFn>(PathGetYRva);
    LevelIdFromRoom    = At<LevelIdFromRoomFn>(LevelIdFromRoomRva);
    ActOfLevel         = At<ActOfLevelFn>(ActOfLevelRva);
    LivingPlayers      = At<LivingPlayersFn>(LivingPlayersRva);
    PetIterate         = At<PetIterateFn>(PetIterateRva);
    RefillHandler      = At<ShrineHandlerFn>(RefillHandlerRva);

    if (!Hook(ShrineOperateRva, ShrineOperateBody, ShrineOperateHookSize, reinterpret_cast<void*>(&HookShrineOperate),
              reinterpret_cast<void**>(&OriginalShrineOperate), "the shrine operate function")) {
        SetSharingInactive("the loader refused the hook on the shrine operate function, see the log");
        return;
    }
    SharingActive.store(true, std::memory_order_release);

    char units[24]{};
    FormatGameUnits(units, sizeof(units), Config.radiusYards);
    D2RL::LogInfoF(Context, "ShrineRework: sharing installed, radius %d yards (%s game units) around the shrine.",
                   Config.radiusYards, units);
}

void Install() noexcept {
    RuleActive.store(true, std::memory_order_release);
    InstallRemoval();
    InstallEffects();
    InstallSharing();
}

// ---------------------------------------------------------------------------
// Console command
// ---------------------------------------------------------------------------

auto StateText(HookState state) noexcept -> const char* {
    switch (state) {
    case HookState::Installed:        return "active";
    case HookState::DisabledByConfig: return "off in the config file";
    case HookState::NothingRemoved:   return "off, removed_shrine_codes is empty";
    case HookState::ConfigError:      return "NOT ACTIVE, removed_shrine_codes could not be read, see the log";
    case HookState::UnsupportedBuild: return "NOT ACTIVE, unrecognised game build or the initializer is already hooked, see the log";
    case HookState::InstallFailed:    return "NOT ACTIVE, the hook failed, see the log";
    case HookState::NotAttempted:
    default:                          return "NOT ACTIVE, never attempted";
    }
}

auto OnOff(const std::atomic<bool>& flag) noexcept -> const char* {
    return flag.load(std::memory_order_relaxed) ? "on" : "off";
}

void WriteSharingStatus(const D2RL::PluginContext* plugin) noexcept {
    char line[512]{};
    if (!SharingActive.load(std::memory_order_acquire)) {
        std::snprintf(line, sizeof(line), "Sharing: inactive, %s",
                      SharingInactive[0] != '\0' ? SharingInactive : "never attempted");
        plugin->WriteConsoleMessage(line);
        return;
    }
    char units[24]{};
    FormatGameUnits(units, sizeof(units), Config.radiusYards);
    const std::uint64_t clickerOnly = ClickerOnlyUses.load(std::memory_order_relaxed);
    std::snprintf(line, sizeof(line),
                  "Sharing on: %d yards (%s game units) | shrines used %llu, %llu of them clicker only | reached %llu "
                  "player%s, %llu hireling%s/summon%s",
                  Config.radiusYards, units,
                  static_cast<unsigned long long>(SharedUses.load(std::memory_order_relaxed) + clickerOnly),
                  static_cast<unsigned long long>(clickerOnly),
                  static_cast<unsigned long long>(ReachedPlayers.load(std::memory_order_relaxed)),
                  ReachedPlayers.load(std::memory_order_relaxed) == 1 ? "" : "s",
                  static_cast<unsigned long long>(ReachedPets.load(std::memory_order_relaxed)),
                  ReachedPets.load(std::memory_order_relaxed) == 1 ? "" : "s",
                  ReachedPets.load(std::memory_order_relaxed) == 1 ? "" : "s");
    plugin->WriteConsoleMessage(line);
    if (AnyShare.load(std::memory_order_acquire)) {
        const std::uint32_t players = LastSharePlayers.load(std::memory_order_relaxed);
        const std::uint32_t pets    = LastSharePets.load(std::memory_order_relaxed);
        if (LastShareClickerOnly.load(std::memory_order_relaxed)) {
            std::snprintf(line, sizeof(line), "last: Code %u, clicker only",
                          LastShareCode.load(std::memory_order_relaxed));
        } else {
            std::snprintf(line, sizeof(line), "last: Code %u reached %u player%s and %u hireling%s/summon%s",
                          LastShareCode.load(std::memory_order_relaxed), players, players == 1 ? "" : "s", pets,
                          pets == 1 ? "" : "s", pets == 1 ? "" : "s");
        }
        plugin->WriteConsoleMessage(line);
    }
    const std::uint64_t capped = CappedScans.load(std::memory_order_relaxed);
    if (capped != 0) {
        std::snprintf(line, sizeof(line), "uses with more units in range than the %u cap: %llu",
                      static_cast<unsigned>(MaximumShareTargets), static_cast<unsigned long long>(capped));
        plugin->WriteConsoleMessage(line);
    }
}

auto __cdecl StatusCommand(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command, void*) noexcept
    -> D2RL::ConsoleCommandResult {
    if (command == nullptr || command->plugin == nullptr) return D2RL::ConsoleCommandResult::Failed;
    const D2RL::PluginContext* plugin = command->plugin;
    char                       line[512]{};

    std::snprintf(line, sizeof(line), "Shrine Rework: config %s",
                  ConfigFileWasRead ? "loaded" : "NOT READ (embedded defaults)");
    plugin->WriteConsoleMessage(line);

    std::snprintf(line, sizeof(line), "Removal: %s | %d Code%s removed | fallback effectclass %u | healing wells %s",
                  StateText(RemovalState.load(std::memory_order_acquire)), RemovedCount, RemovedCount == 1 ? "" : "s",
                  static_cast<unsigned>(Config.fallbackClass), HealingWellPatched ? "patched" : "NOT patched");
    plugin->WriteConsoleMessage(line);

    std::string codes = "removed Codes:";
    AppendRemovedCodes(codes, 380);
    plugin->WriteConsoleMessage(RemovedCount == 0 ? "removed Codes: none" : codes.c_str());

    std::snprintf(line, sizeof(line),
                  "set up since load: %llu from the whole table, %llu from an effectclass, %llu moved to the "
                  "fallback | healing wells changed %llu | nothing left %llu | empty in shrines.txt %llu",
                  static_cast<unsigned long long>(TableDraws.load(std::memory_order_relaxed)),
                  static_cast<unsigned long long>(ClassDraws.load(std::memory_order_relaxed)),
                  static_cast<unsigned long long>(RedirectedDraws.load(std::memory_order_relaxed)),
                  static_cast<unsigned long long>(HealingWellsChanged.load(std::memory_order_relaxed)),
                  static_cast<unsigned long long>(AllRemovedDraws.load(std::memory_order_relaxed)),
                  static_cast<unsigned long long>(EmptyInDataDraws.load(std::memory_order_relaxed)));
    plugin->WriteConsoleMessage(line);

    std::string assigned = "by Code:";
    char        entry[24]{};
    bool        any = false;
    for (std::size_t code = 0; code < CodeCount; ++code) {
        const std::uint32_t count = AssignedByCode[code].load(std::memory_order_relaxed);
        if (count == 0) continue;
        any = true;
        std::snprintf(entry, sizeof(entry), " %u:%u", static_cast<unsigned>(code), count);
        if (assigned.size() + std::strlen(entry) + 4 > 380) {
            assigned += " ...";
            break;
        }
        assigned += entry;
    }
    plugin->WriteConsoleMessage(any ? assigned.c_str() : "by Code: none yet");

    if (AnyAssigned.load(std::memory_order_acquire)) {
        std::snprintf(line, sizeof(line), "last: level %u, objects.txt Parm0 %d, %s %u, row %u, Code %u",
                      LastLevel.load(std::memory_order_relaxed), LastParm0.load(std::memory_order_relaxed),
                      LastClass.load(std::memory_order_relaxed) == 0 ? "whole table" : "effectclass",
                      LastClass.load(std::memory_order_relaxed), LastRow.load(std::memory_order_relaxed),
                      LastCode.load(std::memory_order_relaxed));
        plugin->WriteConsoleMessage(line);
    }

    if (EffectsInactive[0] != '\0') {
        std::snprintf(line, sizeof(line), "Effects: inactive, %s", EffectsInactive);
        plugin->WriteConsoleMessage(line);
        WriteSharingStatus(plugin);
        return D2RL::ConsoleCommandResult::Handled;
    }
    std::snprintf(line, sizeof(line), "Armor %s: %d%% less damage | taken %llu | hits reduced %llu",
                  OnOff(ArmorActive), Config.armorReduction,
                  static_cast<unsigned long long>(ArmorClicks.load(std::memory_order_relaxed)),
                  static_cast<unsigned long long>(ReducedHits.load(std::memory_order_relaxed)));
    plugin->WriteConsoleMessage(line);
    std::snprintf(line, sizeof(line), "Alchemy (Blessed Vials): %s | hits reduced %llu",
                  AlchemyService != nullptr ? "found" : "not found yet",
                  static_cast<unsigned long long>(AlchemyReducedHits.load(std::memory_order_relaxed)));
    plugin->WriteConsoleMessage(line);
    std::snprintf(line, sizeof(line), "Combat %s: %u stat%s = %d | taken %llu", OnOff(CombatActive),
                  static_cast<unsigned>(Config.combatStats.size()), Config.combatStats.size() == 1 ? "" : "s",
                  Config.combatValue, static_cast<unsigned long long>(CombatClicks.load(std::memory_order_relaxed)));
    plugin->WriteConsoleMessage(line);
    std::snprintf(line, sizeof(line), "Resists %s: +%d max resist | taken %llu", OnOff(ResistActive),
                  Config.resistMaxBonus, static_cast<unsigned long long>(ResistClicks.load(std::memory_order_relaxed)));
    plugin->WriteConsoleMessage(line);
    std::snprintf(line, sizeof(line), "Skill %s: %d/%d/%d | taken %llu, last difficulty %u, item_allskills %+d",
                  OnOff(SkillActive), Config.skillBonus[0], Config.skillBonus[1], Config.skillBonus[2],
                  static_cast<unsigned long long>(SkillClicks.load(std::memory_order_relaxed)),
                  LastSkillDifficulty.load(std::memory_order_relaxed), LastSkillExtra.load(std::memory_order_relaxed));
    plugin->WriteConsoleMessage(line);
    std::snprintf(line, sizeof(line), "Mana Recharge %s: %d%% per second | taken %llu, last hpregen %d, manarecovery %d",
                  OnOff(ManaActive), Config.regenPercent,
                  static_cast<unsigned long long>(ManaClicks.load(std::memory_order_relaxed)),
                  LastLifePerFrame.load(std::memory_order_relaxed), LastManaPerFrame.load(std::memory_order_relaxed));
    plugin->WriteConsoleMessage(line);
    WriteSharingStatus(plugin);
    return D2RL::ConsoleCommandResult::Handled;
}

constexpr D2RL::PluginInfo Info{
    .infoSize    = D2RL::PluginInfoSize,
    .abiVersion  = D2RL_PLUGIN_ABI_VERSION,
    .id          = "celestialrayone.shrine-rework",
    .name        = "Shrine Rework",
    .version     = "1.2.0",
    .author      = "CelestialRayOne",
    .description = "Takes chosen shrine types out of the game, reworks the Armor, Combat, Resist, Skill and Mana "
                   "Recharge shrines, and gives a clicked shrine's effect to everyone around it.",
    // Server: shrine selection, shrine effects, sharing and damage are resolved on the server.
    .flags       = D2RL::PluginFlags::Server | D2RL::PluginFlags::NativeHooks,
};

static_assert(D2RL::HasValidPluginRole(Info.flags), "Exactly one execution role must be set.");
static_assert(D2RL::HasOnlyKnownPluginFlags(Info.flags), "Unknown plugin flag set.");

}  // namespace

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* {
    return &Info;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
    if (!D2RL::HasContext(context) || context->exeBase == 0) return false;
    Context = context;
    Base    = context->exeBase;

    // Registered first, and the load always succeeds: returning false would
    // unload the DLL and take the command with it, leaving no in-game way to
    // see why a part is off.
    if (!Context->RegisterConsoleCommand("shrines", &StatusCommand,
                                         "Reports Shrine Rework status: removed shrines, assignments, shrine "
                                         "effects and sharing.")) {
        Context->LogWarn("ShrineRework: the status console command was refused.");
    }

    ReadConfiguration();
    Install();
    return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
    // D2RLoader removes the hooks and restores both patched sites itself.
    // Until it does, every hook runs the original.
    RuleActive.store(false, std::memory_order_relaxed);
}

}  // namespace CelestialRayOne::ShrineRework
