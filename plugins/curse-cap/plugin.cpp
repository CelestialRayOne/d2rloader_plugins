// Extra Curses
//
// Every point of a configurable stat lets its owner keep one more curse on the
// same target:
//
//   curses a caster can keep on one target = 1 + the caster's total of the stat
//
// With 0 points nothing changes: a new curse replaces the target's curse, as
// the game always did.
//
// WHERE THE ONE-CURSE RULE LIVES (D2RLoader 1.3.1, D2R 3.3)
// Every curse, buff and timed debuff is built by the game's timed-state
// builder. D2RLoader 1.3 replaced it: the game's 0x433D20 is now
// jmp [import] into D2RCore.dll's ApplyWideTimedStatEffect (export +0x8339D0),
// whose body at +0x3E7650 is a faithful port of the retail builder that also
// carries wide (16-bit) stat ids. When the new state is a curse for the
// target's unit type (IsCurseState, game 0x335A40, asked at +0x3E76E9), the
// builder looks for ANY curse already on the target:
//
//   3E7795  test eax,eax              ; eax = "is a curse"
//   3E779B  mov edx,20h / cmove edx,[rsi+24h]   ; curse: flag 0x20, else the state
//   3E77A4  lea rax,[slot +0x6FED10]  ; {pointer, RVA 0x2F5810} FindStatListByFlags
//   3E77AB  lea r8,[slot +0x6FEC08]   ; {pointer, RVA 0x2F5940} GetStatListFromUnitAndState
//   3E77B2  cmovne r8,rax
//   3E77B6  call qword ptr [r8]       ; FindStatListByFlags(target, 0x20)
//   3E77B9                            ; <- the only return address this plugin acts on
//
// and acts on what the lookup returns:
//
//   nothing                         3E7854  create the new curse, remove nothing
//   same state, skill and level     3E77DC  refresh: new expire frame and removal event
//   same state and skill, lower lvl 3E7816  refuse the new curse
//   anything else                   3E781C  remove that list, then create the new curse.
//                                           The removal is D2RCore's unlink (+0x3DA6E0),
//                                           which ends by tail-calling the list's remove
//                                           callback (+0x3DA86E) exactly as an expiring
//                                           curse does, then the list is freed (game
//                                           0x2F9EB0). An extended list (bit 31 of its
//                                           flags) is never removed (3E7820 js).
//
// So the whole rule is "what that lookup returns". D2RCore reaches the GAME's
// own FindStatListByFlags through its slot (the slot's RVA field reads 0x2F5810
// and the live pointer is checked to be Base + 0x2F5810), so an ordinary inline
// hook on the game function, acting only when its caller is that one call site,
// decides it:
//
//   - the target's existing curse of the same state, if it has one
//       -> stock refresh or stock refusal, never a slot of its own
//   - nothing, while the target holds fewer other curses than the caster's cap
//       -> the curse is added
//   - otherwise the curse picked by the replace policy
//       -> stock removal of that one curse, then the new one is added
//
// A landing curse therefore never removes more than one curse, like vanilla.
// Everything the builder checks first is untouched: the monster checks
// (+0x3E769F..+0x3E76E1), the target's stat 109 shortening the curse by that
// percentage and refusing it at 100 or more (+0x3E76F5..+0x3E774C), and the
// refusal when the target has state 57 (+0x3E774F..+0x3E7760).
//
// THE CURSE RECORD
// The builder keeps its 0x30-byte curse record in rsi from +0x3E7660
// (mov rsi,rdx) to the lookup, with no write to rsi in between, and its context
// in rdi (+0x3E7663, mov rdi,rcx). The pinned window below proves both. The
// hook recovers the builder's rsi and rdi by unwinding its own frames with
// RtlCaptureContext / RtlVirtualUnwind, which restore each caller's
// nonvolatile registers from the callee's unwind data, so no assembly and no
// generated code is involved. record+8 (target) is cross-checked against the
// lookup's own unit argument, which the builder loads from [rsi+8] at
// +0x3E7797. Layout, read off the window:
//   +00h caster  +08h target  +10h skill  +14h level  +18h duration
//   +1Ch stat id (-1 none)  +20h stat value  +24h state  +28h remove callback
//   context (rdi): +00h data context byte, +08h game; game frame at game+170h.
//
// STAT LISTS (game image, checked byte for byte at load)
//   0x2F5810  FindStatListByFlags(unit, flags): GetStatListEx (0x34B870 ->
//             [unit+88h]); needs the extended flag (bit 31 of [ex+1Ch]); walks
//             [ex+90h] (or [ex+98h] when flags has 0x2000) through next [+68h];
//             returns the first list whose flags share a bit with `flags`.
//   0x2F7AB0  SetStatListExpireFrame: frame > 0 sets flag 0x2 and stores
//             (float)frame at [list+24h].
//   0x2F5020  STATLIST_GetUnitStat, the loader's thunk to D2RCore's
//             ReadWideUnitStat: the unit's total for (stat, layer), 0 for a stat
//             id of 0x8000 or more.
//   D2RCore's PostStatList (+0x3DA4F0) links a list at the HEAD of [ex+90h]
//   (unless it has flag 0x2000), so the chain is newest first. State at
//   [list+20h] is written by the builder at +0x3E7959 and compared at +0x3E77C2.
//
// OTHER USERS OF THE CURSE LOOKUP (audited, left alone by design)
//   0x5C372D, 0x5C3CAF  monster AI: casts a curse only at an uncursed target.
//   Every other game caller passes another flag (1, 2, 8, 0x80, 0x800,
//   0x4000, 0x10000), or sits in a retail body the loader bypasses, or is the
//   debug stat-list inspector. D2RCore's other users of the slot pass 0x40,
//   0x80, 0x10000 or walk every flag bit for its stat inspector. None of them
//   returns to +0x3E77B9, so all of them get the stock answer.
//
// CLIENT: EVERY CURSE KEEPS ITS OWN LOOK (1.1.0)
// What the client does with a state's visuals (game image):
//   0x21A880  state on: a state the unit did not have gets its visuals from
//             0x21B640 (call at 0x21A9C1).
//   0x21B640  castoverlay ([rec+0Ah]) set: ONE node,
//             dispatch(unit, castoverlay, 4 (9 for some states), 0,
//                      overlay1 [rec+2], overlay2 [rec+4], 0 (or the state));
//             no castoverlay: overlay1 and overlay2 as looping nodes (mode 3/8).
//   0x150280  dispatcher: before adding, deletes the first node of the unit
//             with the same id, or a cast node (mode 4/9) whose follow-ups
//             include the id (0x1503BD..0x1503FF -> 0x155CB0, returns 0x150404).
//   0x153300  overlay tick: when a cast node's animation ends it is deleted and
//             its follow-ups are dispatched as looping nodes, mode 4 -> mode 3
//             (0x153551..0x1535B9), mode 9 -> mode 8 while the state is on
//             (0x1535BE..0x15363C).
//   0x21AC50  state off: plays removerlay, then removes overlay1 and overlay2
//             (every size variant, 0x21B400 -> 0x152090). It never removes by
//             the cast overlay.
// The curses share one cast overlay, so a curse that lands while another
// curse's cast animation still plays deletes that cast node before it hands
// over, and the earlier curse's look never appears although the curse is on.
// With one curse at a time the earlier curse was gone anyway.
//
// The fix hooks the node removal (0x155CB0) and acts only on the call from
// the dispatcher's collision check (return address 0x150404). When the node is
// a cast for a DIFFERENT state's visuals (same cast overlay, other follow-ups),
// it hands over exactly as the tick would at the end of the animation: the
// cast node is deleted and its follow-ups are dispatched (mode 3, or mode 8
// when the state is still on). Then the dispatcher adds the new cast as usual.
// Every other removal is stock. State-off removal is by each state's own
// overlay ids, so it keeps working per curse.
//
// The dispatcher's new command is read from its frame: id in edi
// (0x150295 mov edi,edx), mode in r15d (0x150292 mov r15d,r8d), follow-ups
// as its fifth and sixth arguments at [rsp+180h] / [rsp+188h] (five pushes and
// sub rsp,130h). Registers come from unwinding this plugin's own frames, as
// on the server side.

#include "curse_policy.h"
#include "overlay_policy.h"

#include <D2RLPlugin/api.h>

// Windows.h defines min and max as macros unless this is set, which breaks any
// later std::numeric_limits<...>::min() / max() call.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#if defined(_MSC_VER)
#include <intrin.h>
#define EXTRA_CURSES_RETURN_ADDRESS() _ReturnAddress()
#else
#define EXTRA_CURSES_RETURN_ADDRESS() __builtin_return_address(0)
#endif

#include <array>
#include <atomic>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

constexpr char DefaultConfig[] = R"toml(# ============================================================================
# Extra Curses
# ============================================================================
#
# WHAT IT DOES
# Every point of the stat set below lets its owner keep one more curse on the
# same target:
#
#   curses a caster can keep on one target = 1 + the caster's value of the stat
#
# With 0 points nothing changes: a new curse replaces the target's curse, the
# way the game always worked.
#
#   - Recasting a curse the target already has refreshes it, exactly as
#     before, and a lower-level recast is still refused. It never needs a
#     free slot.
#   - A new curse on a target with room is added. Nothing is removed.
#   - A new curse on a full target replaces one curse, chosen by "replace"
#     below. A curse that lands never removes more than one curse.
#   - The replaced curse ends the normal way, as if it had run out: its state
#     is turned off, its timer is cancelled and its end-of-curse code runs.
#   - Everything the game checks before a curse lands still applies to
#     every curse: the target's stat 109 shortens it by that percentage and
#     blocks it at 100 or more, a target with state 57 takes no new curse,
#     and the monster checks are unchanged.
#   - The count belongs to whoever casts. Players, hirelings, summons and
#     monsters that have the stat all get it. A monster without it keeps the
#     game's single curse when it curses you.
#   - Every curse on a target shows its own graphic, also when a second curse
#     lands while the first one's cast flash is still playing.
#
# STAT
# Add a row for the stat to itemstatcost.txt and put its row id in stat_id
# (0 to 32767). -1 leaves the plugin idle.
#
# CONSOLE
#   extra-curses       settings, hook state, counters and the last decision
#
# LOG (<scope>\d2rloader\logs\celestialrayone.extra-curses.log)
#   At load: the stat, the replace policy and the hook state.
#   With [log] decisions = true, one line per curse that lands, e.g.
#     state 9 on monster 37 by player 1: cap 3 (stat 600 = 2), 2 other curses -> added
#     state 61 on monster 37 by player 1: cap 3 (stat 600 = 2), 3 other curses -> replaces state 19 (40 frames left)
#     state 9 on monster 37 by player 1: cap 3 (stat 600 = 2), 2 other curses -> same curse, refreshed or refused by the game
#
# HOW IT WORKS
# The game applies every curse through one timed-state builder (D2RLoader's
# copy lives in D2RCore.dll). Before adding a curse it asks "which curse does
# this target already have?" and removes the answer. This plugin answers that
# one question from the caster's stat.
#
# On the client, the curses share one cast flash, and the flash of a new curse
# used to delete the previous curse's flash before it could turn into that
# curse's graphic. The plugin lets the earlier flash turn into its graphic
# instead, exactly as it would have at the end of its animation.
#
# ============================================================================

# File format version. Leave this value unchanged.
config_version = 1

# Master switch. false loads without hooking anything.
enabled = true


[curses]
# itemstatcost.txt row id of the extra-curses stat, 0 to 32767.
# -1 = not set: the plugin stays idle and every caster keeps one curse.
stat_id = -1

# Which curse a new one replaces when the target is full:
#   "shortest_remaining"  the curse with the least time left. A curse you just
#                         refreshed is never the one lost. Curses without a
#                         timer go last.
#   "oldest"              the curse applied first. Refreshing a curse does not
#                         make it newer.
replace = "shortest_remaining"


[log]
# One log line per curse that lands. Meant for testing.
decisions = false


# Lets D2RLoader verify that multiplayer peers use matching settings.
[d2rl]
match = true
)toml";

// ---------------------------------------------------------------- native map

// Game image (D2RLoader 1.3.1 process, D2R 3.3).
constexpr std::uintptr_t FindStatListByFlagsRva = 0x2F5810;
constexpr std::uint32_t FindStatListByFlagsHookBytes = 18U;
constexpr std::uintptr_t FindStatListByFlagsBodyRva =
    FindStatListByFlagsRva + FindStatListByFlagsHookBytes;
constexpr std::uintptr_t GetStatListFromUnitAndStateRva = 0x2F5940;
constexpr std::uintptr_t IsCurseStateRva = 0x335A40;
constexpr std::uintptr_t GetStatListExRva = 0x34B870;
constexpr std::uintptr_t SetStatListExpireFrameRva = 0x2F7AB0;
constexpr std::uintptr_t GetUnitStatRva = 0x2F5020;
constexpr std::uintptr_t GetUnitStatThunkNopsRva = GetUnitStatRva + 6U;
constexpr std::uintptr_t GetUnitStatBodyRva = GetUnitStatRva + 10U;

// FindStatListByFlags, 2F5810..2F58B4:
//   2F5810  mov [rsp+10h],rbx / push rdi / sub rsp,20h / mov edi,edx /
//           mov rbx,rcx / test rcx,rcx                        <- hooked (18 bytes)
//   2F583A  call 0x34B870 (GetStatListEx)
//   2F5861  cmp dword [rbx+1Ch],0 / jge -> null               extended only
//   2F5867  bt edi,0Dh / [rbx+98h] or [rbx+90h]                chain head
//   2F5882  mov eax,[rcx+1Ch] / and eax,edi / test eax,0FFFFDFFFh / jne found
//   2F588E  mov rcx,[rcx+68h] / test / jne loop                next
//
// GetStatListEx, 34B870..34B8AB: returns [unit+88h] (both paths).
// SetStatListExpireFrame, 2F7AB0..2F7AC9: frame > 0 -> or [rcx+1Ch],2;
//   cvtdq2ps; movss [rcx+24h],xmm0.
// STATLIST_GetUnitStat as D2RLoader 1.3.x leaves it: FF 25 thunk to
//   D2RCore!ReadWideUnitStat, four NOPs, then the untouched start of the old
//   body (the same witness Summon Resist Cap checks).
constexpr std::array<std::uint8_t, 2> GetUnitStatThunkJump{0xFF, 0x25};
constexpr std::array<std::uint8_t, 4> GetUnitStatThunkNops{
    0x90, 0x90, 0x90, 0x90,
};

constexpr std::array<std::uint8_t, 18> FindStatListByFlagsEntry{
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x8B, 0xFA,
    0x48, 0x8B, 0xD9, 0x48, 0x85, 0xC9,
};

constexpr std::array<std::uint8_t, 146> FindStatListByFlagsBody{
    0x75, 0x13, 0x88, 0x4C, 0x24, 0x30, 0x48, 0x8D, 0x4C, 0x24, 0x30, 0xE8,
    0xDE, 0xBF, 0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x01, 0xCC, 0x48, 0x8B, 0xCB,
    0xE8, 0x31, 0x60, 0x05, 0x00, 0x48, 0x8B, 0xD8, 0x48, 0x85, 0xC0, 0x74,
    0x60, 0x83, 0x78, 0x1C, 0x00, 0x7C, 0x14, 0x48, 0x8D, 0x4C, 0x24, 0x30,
    0xC6, 0x44, 0x24, 0x30, 0x00, 0xE8, 0xC4, 0xBE, 0xFF, 0xFF, 0x84, 0xC0,
    0x74, 0x01, 0xCC, 0x83, 0x7B, 0x1C, 0x00, 0x7D, 0x40, 0x0F, 0xBA, 0xE7,
    0x0D, 0x73, 0x09, 0x48, 0x8B, 0x8B, 0x98, 0x00, 0x00, 0x00, 0xEB, 0x07,
    0x48, 0x8B, 0x8B, 0x90, 0x00, 0x00, 0x00, 0x48, 0x85, 0xC9, 0x74, 0x15,
    0x8B, 0x41, 0x1C, 0x23, 0xC7, 0xA9, 0xFF, 0xDF, 0xFF, 0xFF, 0x75, 0x0B,
    0x48, 0x8B, 0x49, 0x68, 0x48, 0x85, 0xC9, 0x75, 0xEB, 0x33, 0xC9, 0x48,
    0x8B, 0xC1, 0x48, 0x8B, 0x5C, 0x24, 0x38, 0x48, 0x83, 0xC4, 0x20, 0x5F,
    0xC3, 0x48, 0x8B, 0x5C, 0x24, 0x38, 0x33, 0xC0, 0x48, 0x83, 0xC4, 0x20,
    0x5F, 0xC3,
};

constexpr std::array<std::uint8_t, 59> GetStatListExBody{
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0x48, 0x85, 0xC9,
    0x75, 0x20, 0x88, 0x4C, 0x24, 0x30, 0x48, 0x8D, 0x4C, 0x24, 0x30, 0xE8,
    0x44, 0x9B, 0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x01, 0xCC, 0x48, 0x8B, 0x83,
    0x88, 0x00, 0x00, 0x00, 0x48, 0x83, 0xC4, 0x20, 0x5B, 0xC3, 0x48, 0x8B,
    0x81, 0x88, 0x00, 0x00, 0x00, 0x48, 0x83, 0xC4, 0x20, 0x5B, 0xC3,
};

constexpr std::array<std::uint8_t, 26> SetStatListExpireFrameBody{
    0x48, 0x85, 0xC9, 0x74, 0x14, 0x85, 0xD2, 0x7E, 0x04, 0x83, 0x49, 0x1C,
    0x02, 0x66, 0x0F, 0x6E, 0xC2, 0x0F, 0x5B, 0xC0, 0xF3, 0x0F, 0x11, 0x41,
    0x24, 0xC3,
};

constexpr std::array<std::uint8_t, 22> GetUnitStatBody{
    0x48, 0x89, 0x74, 0x24, 0x20, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x41, 0x0F,
    0xB7, 0xE8, 0x8B, 0xFA, 0x48, 0x8B, 0xD9, 0x48, 0x85, 0xC9,
};

constexpr std::array<std::uint8_t, 516> CoreBuilderWindow{
    0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x56, 0x57, 0x55, 0x53,
    0x48, 0x83, 0xEC, 0x58, 0x48, 0x89, 0xD6, 0x48, 0x89, 0xCF, 0x48, 0x8B,
    0x05, 0x13, 0x3F, 0x31, 0x00, 0x48, 0x31, 0xE0, 0x48, 0x89, 0x44, 0x24,
    0x50, 0x44, 0x8B, 0x62, 0x24, 0x0F, 0xB6, 0x09, 0xFF, 0x15, 0xEE, 0xA7,
    0x31, 0x00, 0x4C, 0x39, 0xA0, 0x98, 0x02, 0x00, 0x00, 0x0F, 0x86, 0xD3,
    0x00, 0x00, 0x00, 0x4C, 0x8B, 0xB8, 0x90, 0x02, 0x00, 0x00, 0x4D, 0x85,
    0xFF, 0x0F, 0x84, 0xC3, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x46, 0x08, 0x83,
    0x38, 0x01, 0x75, 0x3B, 0x8B, 0x58, 0x04, 0x0F, 0xB6, 0x0F, 0x89, 0xDA,
    0xFF, 0x15, 0xB2, 0xA9, 0x31, 0x00, 0x48, 0x85, 0xC0, 0x0F, 0x84, 0xA3,
    0x00, 0x00, 0x00, 0xF6, 0x40, 0x3D, 0x01, 0x0F, 0x85, 0x99, 0x00, 0x00,
    0x00, 0x0F, 0xB6, 0x0F, 0x89, 0xDA, 0xFF, 0x15, 0x7C, 0xA9, 0x31, 0x00,
    0x48, 0x85, 0xC0, 0x0F, 0x84, 0x85, 0x00, 0x00, 0x00, 0xF6, 0x40, 0x05,
    0x02, 0x74, 0x7F, 0x8B, 0x56, 0x24, 0x0F, 0xB6, 0x0F, 0xFF, 0x15, 0x61,
    0x87, 0x31, 0x00, 0x85, 0xC0, 0x74, 0x39, 0x89, 0xC3, 0x48, 0x8B, 0x4E,
    0x08, 0xBA, 0x6D, 0x00, 0x00, 0x00, 0x45, 0x31, 0xC0, 0xE8, 0xCA, 0x14,
    0xFF, 0xFF, 0x83, 0xF8, 0x63, 0x7F, 0x57, 0x85, 0xC0, 0x74, 0x40, 0x8B,
    0x4E, 0x18, 0x81, 0xF9, 0x01, 0x00, 0x10, 0x00, 0x7C, 0x17, 0x89, 0xCA,
    0x48, 0x69, 0xD2, 0x1F, 0x85, 0xEB, 0x51, 0x48, 0xC1, 0xEA, 0x25, 0x0F,
    0xAF, 0xD0, 0xEB, 0x1E, 0x45, 0x31, 0xED, 0xEB, 0x64, 0x0F, 0xAF, 0xC1,
    0x48, 0x98, 0x48, 0x69, 0xD0, 0x1F, 0x85, 0xEB, 0x51, 0x48, 0x89, 0xD0,
    0x48, 0xC1, 0xE8, 0x3F, 0x48, 0xC1, 0xFA, 0x25, 0x01, 0xC2, 0x29, 0xD1,
    0x89, 0x4E, 0x18, 0x48, 0x8B, 0x4E, 0x08, 0xBA, 0x39, 0x00, 0x00, 0x00,
    0xFF, 0x15, 0x3A, 0x87, 0x31, 0x00, 0x85, 0xC0, 0x74, 0x2B, 0x31, 0xC0,
    0x48, 0x8B, 0x4C, 0x24, 0x50, 0x48, 0x31, 0xE1, 0x48, 0x8B, 0x15, 0x0D,
    0x3E, 0x31, 0x00, 0x48, 0x39, 0xCA, 0x0F, 0x85, 0x84, 0x02, 0x00, 0x00,
    0x48, 0x83, 0xC4, 0x58, 0x5B, 0x5D, 0x5F, 0x5E, 0x41, 0x5C, 0x41, 0x5D,
    0x41, 0x5E, 0x41, 0x5F, 0xC3, 0x41, 0xBD, 0x20, 0x00, 0x00, 0x00, 0x89,
    0xD8, 0x85, 0xC0, 0x48, 0x8B, 0x4E, 0x08, 0xBA, 0x20, 0x00, 0x00, 0x00,
    0x0F, 0x44, 0x56, 0x24, 0x48, 0x8D, 0x05, 0x65, 0x75, 0x31, 0x00, 0x4C,
    0x8D, 0x05, 0x56, 0x74, 0x31, 0x00, 0x4C, 0x0F, 0x45, 0xC0, 0x41, 0xFF,
    0x10, 0x48, 0x85, 0xC0, 0x0F, 0x84, 0x92, 0x00, 0x00, 0x00, 0x8B, 0x48,
    0x20, 0x3B, 0x4E, 0x24, 0x75, 0x52, 0x8B, 0x48, 0x28, 0x3B, 0x4E, 0x10,
    0x75, 0x4A, 0x8B, 0x48, 0x2C, 0x8B, 0x56, 0x14, 0x39, 0xCA, 0x75, 0x3A,
    0x8B, 0x5E, 0x18, 0x85, 0xDB, 0x74, 0x81, 0x48, 0x8B, 0x4F, 0x08, 0x03,
    0x99, 0x70, 0x01, 0x00, 0x00, 0x48, 0x89, 0xC1, 0x89, 0xDA, 0x49, 0x89,
    0xC6, 0xFF, 0x15, 0xD5, 0x75, 0x31, 0x00, 0x44, 0x8B, 0x46, 0x24, 0x48,
    0x8B, 0x56, 0x08, 0x48, 0x89, 0xF9, 0x41, 0x89, 0xD9, 0xE8, 0x02, 0x02,
    0x00, 0x00, 0x4C, 0x89, 0xF0, 0xE9, 0x4E, 0xFF, 0xFF, 0xFF, 0x0F, 0x8C,
    0x46, 0xFF, 0xFF, 0xFF, 0x83, 0x78, 0x1C, 0x00, 0x78, 0x32, 0x0F, 0xB6,
    0x1F, 0x88, 0x5C, 0x24, 0x40, 0x49, 0x89, 0xC6, 0xFF, 0x15, 0x56, 0xD9,
    0x31, 0x00, 0x48, 0x89, 0x44, 0x24, 0x48, 0x48, 0x8D, 0x4C, 0x24, 0x40,
    0x4C, 0x89, 0xF2, 0xE8, 0x9C, 0x2E, 0xFF, 0xFF, 0x41, 0x80, 0x4E, 0x1D,
    0x20, 0x89, 0xD9, 0x4C, 0x89, 0xF2, 0xFF, 0x15, 0x5C, 0x74, 0x31, 0x00,
};

// Client overlays: dispatcher 0x150280..0x150404 (prologue through the
// collision call), the tick's cast hand-over 0x153551..0x15363C, the removal
// entry (hooked, four register saves) and STATES_CheckState's entry.
constexpr std::array<std::uint8_t, 388> OverlayDispatchWindow{
    0x40, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x57, 0x48, 0x81, 0xEC, 0x30,
    0x01, 0x00, 0x00, 0x45, 0x8B, 0xE1, 0x45, 0x8B, 0xF8, 0x8B, 0xFA, 0x48,
    0x8B, 0xF1, 0xE8, 0x41, 0x9E, 0x1F, 0x00, 0x0F, 0xB6, 0xC8, 0x8B, 0xD7,
    0xE8, 0xF7, 0xFE, 0xFF, 0xFF, 0x48, 0x8B, 0xE8, 0x48, 0x85, 0xC0, 0x0F,
    0x84, 0x49, 0x04, 0x00, 0x00, 0x4C, 0x89, 0xAC, 0x24, 0x68, 0x01, 0x00,
    0x00, 0xE8, 0x0E, 0xB0, 0xF3, 0xFF, 0x8B, 0xC8, 0xE8, 0xB7, 0xA1, 0xF4,
    0xFF, 0x48, 0x8B, 0xCE, 0x4C, 0x8B, 0xE8, 0xE8, 0xCC, 0xF6, 0x1F, 0x00,
    0x85, 0xC0, 0x0F, 0x85, 0x1A, 0x04, 0x00, 0x00, 0x48, 0x8B, 0xCE, 0x48,
    0x89, 0x9C, 0x24, 0x60, 0x01, 0x00, 0x00, 0xE8, 0xE4, 0xB6, 0x1F, 0x00,
    0x83, 0xF8, 0x01, 0x75, 0x53, 0x41, 0xB8, 0x12, 0x0D, 0x00, 0x00, 0x48,
    0x8D, 0x15, 0x12, 0xC8, 0xB7, 0x01, 0x48, 0x8B, 0xCE, 0xE8, 0x5A, 0x95,
    0x1F, 0x00, 0x48, 0x8B, 0xCE, 0x8B, 0xD8, 0xE8, 0xD0, 0x9D, 0x1F, 0x00,
    0x8B, 0xD3, 0x0F, 0xB6, 0xC8, 0xE8, 0x96, 0x72, 0xF4, 0xFF, 0x48, 0x85,
    0xC0, 0x75, 0x15, 0x48, 0x8D, 0x4C, 0x24, 0x40, 0x88, 0x44, 0x24, 0x40,
    0xE8, 0xC3, 0x65, 0xF4, 0xFF, 0x84, 0xC0, 0x74, 0x13, 0xCC, 0xEB, 0x10,
    0x0F, 0xB6, 0x40, 0x04, 0x23, 0x05, 0x9A, 0x93, 0xC4, 0x01, 0x0F, 0x85,
    0xAA, 0x03, 0x00, 0x00, 0x41, 0x83, 0xFF, 0x08, 0x75, 0x2B, 0x48, 0x8B,
    0xCE, 0xE8, 0xAE, 0xA2, 0xF4, 0xFF, 0x48, 0x8B, 0x88, 0x40, 0x04, 0x00,
    0x00, 0x48, 0x85, 0xC9, 0x74, 0x17, 0x66, 0x90, 0x39, 0x79, 0x04, 0x0F,
    0x84, 0x85, 0x03, 0x00, 0x00, 0x48, 0x8B, 0x89, 0xB0, 0x00, 0x00, 0x00,
    0x48, 0x85, 0xC9, 0x75, 0xEB, 0x8B, 0xCF, 0x4C, 0x89, 0xB4, 0x24, 0x70,
    0x01, 0x00, 0x00, 0x81, 0xE9, 0x8C, 0x00, 0x00, 0x00, 0x74, 0x7D, 0x83,
    0xE9, 0x01, 0x74, 0x78, 0x83, 0xE9, 0x01, 0x74, 0x73, 0x83, 0xF9, 0x10,
    0x74, 0x6E, 0x48, 0x85, 0xF6, 0x74, 0x69, 0x48, 0x8B, 0xCE, 0xE8, 0x5D,
    0xA2, 0xF4, 0xFF, 0x48, 0x8B, 0xD8, 0x48, 0x85, 0xC0, 0x74, 0x59, 0x48,
    0x8B, 0xCE, 0xE8, 0x2D, 0x9D, 0x1F, 0x00, 0x0F, 0xB6, 0xC8, 0x8B, 0xD7,
    0xE8, 0xE3, 0xFD, 0xFF, 0xFF, 0x48, 0x8B, 0x93, 0x40, 0x04, 0x00, 0x00,
    0x48, 0x85, 0xD2, 0x74, 0x3B, 0x0F, 0x1F, 0x80, 0x00, 0x00, 0x00, 0x00,
    0x48, 0x8B, 0x8A, 0xB0, 0x00, 0x00, 0x00, 0x39, 0x7A, 0x04, 0x74, 0x20,
    0x8B, 0x02, 0x83, 0xF8, 0x04, 0x74, 0x05, 0x83, 0xF8, 0x09, 0x75, 0x0A,
    0x39, 0x7A, 0x08, 0x74, 0x0F, 0x39, 0x7A, 0x0C, 0x74, 0x0A, 0x48, 0x8B,
    0xD1, 0x48, 0x85, 0xC9, 0x75, 0xD6, 0xEB, 0x08, 0x48, 0x8B, 0xCE, 0xE8,
    0xAC, 0x58, 0x00, 0x00,
};

constexpr std::array<std::uint8_t, 235> OverlayTickCastDoneWindow{
    0x8B, 0x6B, 0x08, 0x48, 0x8B, 0xD3, 0x8B, 0x73, 0x0C, 0x48, 0x8B, 0xCF,
    0xE8, 0x4E, 0x27, 0x00, 0x00, 0x83, 0xFD, 0xFF, 0x74, 0x20, 0x45, 0x33,
    0xC9, 0x44, 0x89, 0x64, 0x24, 0x30, 0x44, 0x89, 0x64, 0x24, 0x28, 0x8B,
    0xD5, 0x48, 0x8B, 0xCF, 0x44, 0x89, 0x64, 0x24, 0x20, 0x45, 0x8D, 0x41,
    0x03, 0xE8, 0xF9, 0xCC, 0xFF, 0xFF, 0x48, 0x8D, 0x2D, 0x12, 0x95, 0xB7,
    0x01, 0x83, 0xFE, 0xFF, 0x0F, 0x84, 0xB0, 0xFD, 0xFF, 0xFF, 0x44, 0x89,
    0x64, 0x24, 0x30, 0x41, 0xB8, 0x03, 0x00, 0x00, 0x00, 0x44, 0x89, 0x64,
    0x24, 0x28, 0x8B, 0xD6, 0x45, 0x33, 0xC9, 0x44, 0x89, 0x64, 0x24, 0x20,
    0x48, 0x8B, 0xCF, 0xE8, 0xC7, 0xCC, 0xFF, 0xFF, 0xE9, 0x82, 0xFD, 0xFF,
    0xFF, 0x8B, 0x6B, 0x08, 0x48, 0x8B, 0xD3, 0x44, 0x8B, 0x7B, 0x0C, 0x48,
    0x8B, 0xCF, 0x8B, 0x73, 0x10, 0xE8, 0xDD, 0x26, 0x00, 0x00, 0x8B, 0xD6,
    0x48, 0x8B, 0xCF, 0xE8, 0xD3, 0x1B, 0x1E, 0x00, 0x85, 0xC0, 0x0F, 0x84,
    0x5B, 0xFD, 0xFF, 0xFF, 0x83, 0xFD, 0xFF, 0x74, 0x1F, 0x45, 0x33, 0xC9,
    0x44, 0x89, 0x64, 0x24, 0x30, 0x44, 0x89, 0x64, 0x24, 0x28, 0x8B, 0xD5,
    0x48, 0x8B, 0xCF, 0x89, 0x74, 0x24, 0x20, 0x45, 0x8D, 0x41, 0x08, 0xE8,
    0x77, 0xCC, 0xFF, 0xFF, 0x48, 0x8D, 0x2D, 0x90, 0x94, 0xB7, 0x01, 0x41,
    0x83, 0xFF, 0xFF, 0x0F, 0x84, 0x2D, 0xFD, 0xFF, 0xFF, 0x44, 0x89, 0x64,
    0x24, 0x30, 0x41, 0xB8, 0x08, 0x00, 0x00, 0x00, 0x44, 0x89, 0x64, 0x24,
    0x28, 0x41, 0x8B, 0xD7, 0x45, 0x33, 0xC9, 0x89, 0x74, 0x24, 0x20, 0x48,
    0x8B, 0xCF, 0xE8, 0x44, 0xCC, 0xFF, 0xFF,
};

constexpr std::array<std::uint8_t, 20> OverlayCleanupEntry{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89,
    0x74, 0x24, 0x18, 0x48, 0x89, 0x7C, 0x24, 0x20,
};

constexpr std::array<std::uint8_t, 32> CheckStateEntry{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48,
    0x83, 0xEC, 0x20, 0x8B, 0xDA, 0x48, 0x8B, 0xF1, 0xE8, 0x07, 0x68, 0x01,
    0x00, 0x85, 0xC0, 0x74, 0x0E, 0x83, 0xE8, 0x01,
};

// D2RCore.dll (D2RLoader 1.3.1), SHA-256 2A868D01...5A2. The window above is
// the builder from its first instruction (+0x3E7650) through the removal of a
// mismatching list (+0x3E784E call, ends +0x3E7854). No base relocation falls
// inside it, so the loaded bytes equal the file's.
constexpr wchar_t CoreModuleName[] = L"D2RCore.dll";
constexpr std::uint64_t CoreBuilderRva = 0x3E7650;
constexpr std::uint64_t CoreIsCurseCallRva = 0x3E76E9;     // FF 15 disp32
constexpr std::uint64_t CoreCurseLookupLeaRva = 0x3E77A4;  // 48 8D 05 disp32
constexpr std::uint64_t CoreStateLookupLeaRva = 0x3E77AB;  // 4C 8D 05 disp32
constexpr std::uint64_t CoreLookupReturnRva = 0x3E77B9;

// Client overlays (game image).
constexpr std::uintptr_t OverlayDispatchRva = 0x150280;
constexpr std::uintptr_t OverlayDispatchCollisionReturnRva = 0x150404;
constexpr std::uintptr_t OverlayCleanupRva = 0x155CB0;
constexpr std::uint32_t OverlayCleanupHookBytes = 20U;
constexpr std::uintptr_t OverlayTickCastDoneRva = 0x153551;
constexpr std::uintptr_t CheckStateRva = 0x3351B0;
// The dispatcher's fifth and sixth arguments, from its rsp at the collision
// call: five pushes + sub rsp,130h = 158h, then the return address, the
// 20h home area and the two stack arguments.
constexpr std::size_t DispatchFollowUp1Offset = 0x180;
constexpr std::size_t DispatchFollowUp2Offset = 0x188;

// Curse record (builder rsi) and builder context (builder rdi).
constexpr std::size_t CurseCasterOffset = 0x00;
constexpr std::size_t CurseTargetOffset = 0x08;
constexpr std::size_t CurseStateOffset = 0x24;
constexpr std::size_t BuilderGameOffset = 0x08;
constexpr std::size_t GameFrameOffset = 0x170;

// Unit.
constexpr std::size_t UnitTypeOffset = 0x00;
constexpr std::size_t UnitIdOffset = 0x08;
constexpr std::size_t UnitStatListExOffset = 0x88;

constexpr std::int32_t MaxStatId = 0x7FFF;  // ReadWideUnitStat answers 0 above
constexpr std::size_t MaxUnwindFrames = 6U;
constexpr std::uint32_t ConfigReadBufferBytes = 16384U;
constexpr std::size_t DecisionTextBytes = 256U;

using FindStatListByFlagsFn =
    std::uint8_t*(__fastcall*)(void* unit, std::uint32_t flags) noexcept;
using GetUnitStatFn =
    std::int32_t(__fastcall*)(void* unit, std::int32_t statId, std::uint32_t layer) noexcept;
using OverlayCleanupFn = void(__fastcall*)(void* unit, std::uint8_t* node) noexcept;
using OverlayDispatchFn = std::uint64_t(__fastcall*)(
    void* unit,
    std::int32_t overlay,
    std::int32_t mode,
    std::int32_t a4,
    std::int32_t followUp1,
    std::int32_t followUp2,
    std::int32_t state) noexcept;
using CheckStateFn = std::int32_t(__fastcall*)(void* unit, std::int32_t state) noexcept;

struct Config {
    bool enabled{true};
    std::int32_t statId{-1};
    extra_curses::ReplacePolicy replace{extra_curses::ReplacePolicy::ShortestRemaining};
    bool logDecisions{false};
};

// A caller's registers at one pinned call site, recovered by unwinding.
// rsp == 0: the call site was not found on the stack.
struct CallerFrame {
    std::uintptr_t rsp{};
    std::uintptr_t rsi{};
    std::uintptr_t rdi{};
    std::uintptr_t r15{};
};

const D2RL::PluginContext* Context{};
std::uint8_t* Base{};
Config Settings{};
std::string LoadedConfigPath{"embedded defaults"};
std::string RuntimeBuild{"<unavailable>"};

FindStatListByFlagsFn OriginalFindStatListByFlags{};
GetUnitStatFn GetUnitStat{};
std::uintptr_t CoreBase{};
std::uintptr_t CoreLookupReturn{};
std::uintptr_t CoreCurseSlot{};
bool HookInstalled{};

OverlayCleanupFn OriginalOverlayCleanup{};
OverlayDispatchFn OverlayDispatch{};
CheckStateFn CheckState{};
std::uintptr_t DispatchCollisionReturn{};
bool ClientFixInstalled{};
std::atomic_bool ClientFixActive{};
std::atomic<std::uint64_t> CastHandOvers{};
std::atomic<std::uint64_t> CastHandOverStates{};
std::atomic<std::uint64_t> ClientFrameMisses{};

std::atomic_bool Operational{};
std::atomic<std::uint64_t> BuilderLookups{};
std::atomic<std::uint64_t> SameStateCount{};
std::atomic<std::uint64_t> AddedCount{};
std::atomic<std::uint64_t> ReplacedCount{};
std::atomic<std::uint64_t> UnremovableCount{};
std::atomic<std::uint64_t> NoStatListCount{};
std::atomic<std::uint64_t> FrameMisses{};
std::atomic<std::uint64_t> RecordMismatches{};
std::atomic<std::uint64_t> TruncatedChains{};

SRWLOCK DecisionLock = SRWLOCK_INIT;
char LastDecision[DecisionTextBytes]{};

constexpr D2RL::PluginInfo Info{
    .infoSize = D2RL::PluginInfoSize,
    .abiVersion = D2RL_PLUGIN_ABI_VERSION,
    .id = "celestialrayone.extra-curses",
    .name = "Extra Curses",
    .version = "1.1.0",
    .author = "CelestialRayOne",
    .description =
        "Each point of a configurable stat lets its owner keep one more curse "
        "on the same target, and every curse keeps its own graphic.",
    .flags = D2RL::PluginFlags::Shared | D2RL::PluginFlags::NativeHooks,
};

template <typename T>
auto At(std::uintptr_t rva) noexcept -> T {
    return reinterpret_cast<T>(Base + rva);
}

template <typename T>
auto Read(const void* base, std::size_t offset) noexcept -> T {
    T value{};
    std::memcpy(&value, static_cast<const std::uint8_t*>(base) + offset, sizeof(T));
    return value;
}

auto VerifyBytes(
        std::uintptr_t rva,
        const std::uint8_t* expected,
        std::size_t size,
        const char* what) noexcept -> bool {
    if (Context->CheckExpectedBytes(rva, expected, static_cast<std::uint32_t>(size))) {
        return true;
    }
    char message[192]{};
    std::snprintf(
        message,
        sizeof(message),
        "ExtraCurses: %s at 0x%llX does not match the expected bytes; plugin refused.",
        what,
        static_cast<unsigned long long>(rva));
    Context->LogError(message);
    return false;
}

void Warn(const char* message) noexcept {
    if (Context != nullptr) Context->LogWarn(message);
}

// ------------------------------------------------------------------ config

auto Trim(std::string value) noexcept -> std::string {
    const auto isSpace = [](char c) {
        return std::isspace(static_cast<unsigned char>(c)) != 0;
    };
    while (!value.empty() && isSpace(value.front())) value.erase(value.begin());
    while (!value.empty() && isSpace(value.back())) value.pop_back();
    return value;
}

auto Unquote(std::string value) noexcept -> std::string {
    value = Trim(value);
    if (value.size() >= 2U
            && (value.front() == '"' || value.front() == '\'')
            && value.back() == value.front()) {
        value = value.substr(1U, value.size() - 2U);
    }
    for (char& c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return Trim(value);
}

auto ParseBool(const std::string& value, bool fallback) noexcept -> bool {
    if (value == "true" || value == "1") return true;
    if (value == "false" || value == "0") return false;
    return fallback;
}

void ParseStatId(const std::string& value) noexcept {
    char* end = nullptr;
    const long parsed = std::strtol(value.c_str(), &end, 10);
    if (end == value.c_str() || (end != nullptr && *end != '\0')) {
        Warn("ExtraCurses: [curses] stat_id is not a whole number; it was ignored.");
        return;
    }
    if (parsed < -1 || parsed > MaxStatId) {
        char message[192]{};
        std::snprintf(
            message,
            sizeof(message),
            "ExtraCurses: [curses] stat_id = %ld is outside -1..32767; it was ignored.",
            parsed);
        Warn(message);
        return;
    }
    Settings.statId = static_cast<std::int32_t>(parsed);
}

void ParseReplace(const std::string& value) noexcept {
    const std::string policy = Unquote(value);
    if (policy == "shortest_remaining") {
        Settings.replace = extra_curses::ReplacePolicy::ShortestRemaining;
    } else if (policy == "oldest") {
        Settings.replace = extra_curses::ReplacePolicy::Oldest;
    } else {
        Warn("ExtraCurses: [curses] replace must be \"shortest_remaining\" or "
             "\"oldest\"; \"shortest_remaining\" is in use.");
    }
}

// Returns false for a key this plugin does not read in that section.
auto ApplyConfigLine(
        const std::string& section,
        const std::string& key,
        const std::string& value) noexcept -> bool {
    if (section.empty()) {
        if (key == "enabled") {
            Settings.enabled = ParseBool(value, Settings.enabled);
            return true;
        }
        return key == "config_version";
    }
    if (section == "curses") {
        if (key == "stat_id") {
            ParseStatId(value);
            return true;
        }
        if (key == "replace") {
            ParseReplace(value);
            return true;
        }
        return false;
    }
    if (section == "log") {
        if (key == "decisions") {
            Settings.logDecisions = ParseBool(value, Settings.logDecisions);
            return true;
        }
        return false;
    }
    // [d2rl] is read by D2RLoader itself.
    return section == "d2rl";
}

auto StripComment(const std::string& line) noexcept -> std::string {
    // A # inside a quoted value is part of the value.
    bool quoted = false;
    char quote = '\0';
    for (std::size_t i = 0U; i < line.size(); ++i) {
        const char c = line[i];
        if (quoted) {
            if (c == quote) quoted = false;
        } else if (c == '"' || c == '\'') {
            quoted = true;
            quote = c;
        } else if (c == '#') {
            return Trim(line.substr(0U, i));
        }
    }
    return Trim(line);
}

auto IsSectionHeader(const std::string& line) noexcept -> bool {
    if (line.size() < 3U || line.front() != '[' || line.back() != ']') return false;
    const std::string name = Trim(line.substr(1U, line.size() - 2U));
    if (name.empty()) return false;
    for (const char c : name) {
        if (std::isalnum(static_cast<unsigned char>(c)) == 0
                && c != '_' && c != '-' && c != '.') {
            return false;
        }
    }
    return true;
}

void ParseConfig(const std::string& text) noexcept {
    std::string section;
    // A UTF-8 byte order mark would otherwise stick to the first line.
    std::size_t begin = text.rfind("\xEF\xBB\xBF", 0) == 0 ? 3U : 0U;
    while (begin <= text.size()) {
        std::size_t end = text.find('\n', begin);
        if (end == std::string::npos) end = text.size();
        const std::string line = StripComment(text.substr(begin, end - begin));
        begin = end + 1U;
        if (line.empty()) continue;

        if (IsSectionHeader(line)) {
            section = Trim(line.substr(1U, line.size() - 2U));
            continue;
        }
        const auto equals = line.find('=');
        if (equals == std::string::npos) continue;
        const std::string key = Trim(line.substr(0U, equals));
        const std::string value = Trim(line.substr(equals + 1U));
        if (!ApplyConfigLine(section, key, value)) {
            char message[224]{};
            std::snprintf(
                message,
                sizeof(message),
                "ExtraCurses: unknown setting %.48s under [%.32s]; it was ignored.",
                key.c_str(),
                section.empty() ? "no section" : section.c_str());
            Warn(message);
        }
    }
}

auto NarrowPath(const wchar_t* wide) noexcept -> std::string {
    if (wide == nullptr || wide[0] == L'\0') return {};
    const int needed =
        ::WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (needed <= 1) return {};
    std::string narrow(static_cast<std::size_t>(needed - 1), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, wide, -1, narrow.data(), needed, nullptr, nullptr);
    return narrow;
}

auto ConfigPathForDisplay() noexcept -> std::string {
    constexpr std::size_t needed =
        offsetof(D2RL::PluginContext, pluginConfigPath) + sizeof(const wchar_t*);
    if (Context->contextSize < needed) return {};
    return NarrowPath(Context->pluginConfigPath);
}

auto LoadConfig() noexcept -> bool {
    Settings = Config{};
    LoadedConfigPath = "embedded defaults";

    if (!Context->EnsureConfig(DefaultConfig)) {
        Context->LogWarn(
            "ExtraCurses: the TOML could not be created; embedded defaults are in use.");
        return true;
    }

    std::string text(ConfigReadBufferBytes, '\0');
    std::uint32_t required = 0U;
    if (!Context->ReadConfig(text.data(), static_cast<std::uint32_t>(text.size()), &required)) {
        if (required > text.size()) {
            text.assign(required, '\0');
            if (!Context->ReadConfig(text.data(), required, &required)) {
                Context->LogWarn(
                    "ExtraCurses: the TOML could not be read; embedded defaults are in use.");
                return true;
            }
        } else {
            Context->LogWarn(
                "ExtraCurses: the TOML could not be read; embedded defaults are in use.");
            return true;
        }
    }

    text.resize(std::strlen(text.c_str()));
    ParseConfig(text);

    const std::string path = ConfigPathForDisplay();
    LoadedConfigPath = path.empty() ? std::string{"<plugin config>"} : path;
    return true;
}

auto PolicyName(extra_curses::ReplacePolicy policy) noexcept -> const char* {
    return policy == extra_curses::ReplacePolicy::Oldest ? "oldest" : "shortest_remaining";
}

// ------------------------------------------------------------------ D2RCore

auto CoreImage(std::uint32_t* imageSize) noexcept -> const std::uint8_t* {
    const HMODULE module = ::GetModuleHandleW(CoreModuleName);
    if (module == nullptr) return nullptr;
    const auto* image = reinterpret_cast<const std::uint8_t*>(module);
    const auto ntOffset = static_cast<std::size_t>(Read<std::uint32_t>(image, 0x3C));
    *imageSize = Read<std::uint32_t>(image, ntOffset + 0x50);
    return image;
}

// The slot a RIP-relative instruction addresses: {pointer, game RVA}.
auto CoreSlot(
        const std::uint8_t* image,
        std::uint32_t imageSize,
        std::uint64_t instructionRva,
        std::size_t dispOffset,
        std::size_t instructionLength) noexcept -> std::uint64_t {
    if (instructionRva + instructionLength > imageSize) return 0U;
    const auto disp = Read<std::int32_t>(image, static_cast<std::size_t>(instructionRva + dispOffset));
    const auto slot = static_cast<std::int64_t>(instructionRva + instructionLength) + disp;
    if (slot <= 0 || static_cast<std::uint64_t>(slot) + 16U > imageSize) return 0U;
    return static_cast<std::uint64_t>(slot);
}

// True when the slot names `expectedRva` and is unbound or bound to it.
auto CoreSlotNames(
        const std::uint8_t* image,
        std::uint64_t slot,
        std::uintptr_t expectedRva) noexcept -> bool {
    if (slot == 0U) return false;
    const auto resolved = Read<std::uint64_t>(image, static_cast<std::size_t>(slot));
    const auto rva = Read<std::uint64_t>(image, static_cast<std::size_t>(slot) + 8U);
    return rva == expectedRva
        && (resolved == 0U || resolved == reinterpret_cast<std::uintptr_t>(Base) + expectedRva);
}

auto ResolveCoreLookup() noexcept -> bool {
    std::uint32_t imageSize = 0U;
    const auto* image = CoreImage(&imageSize);
    if (image == nullptr) {
        Context->LogError("ExtraCurses: D2RCore.dll is not loaded; plugin refused.");
        return false;
    }
    if (CoreBuilderRva + CoreBuilderWindow.size() > imageSize
            || std::memcmp(image + CoreBuilderRva, CoreBuilderWindow.data(),
                           CoreBuilderWindow.size()) != 0) {
        Context->LogError(
            "ExtraCurses: D2RCore.dll's timed-state builder is not the D2RLoader 1.3.1 "
            "code this plugin was verified against; plugin refused.");
        return false;
    }
    const auto isCurseSlot = CoreSlot(image, imageSize, CoreIsCurseCallRva, 2U, 6U);
    const auto curseSlot = CoreSlot(image, imageSize, CoreCurseLookupLeaRva, 3U, 7U);
    const auto stateSlot = CoreSlot(image, imageSize, CoreStateLookupLeaRva, 3U, 7U);
    if (!CoreSlotNames(image, isCurseSlot, IsCurseStateRva)
            || !CoreSlotNames(image, curseSlot, FindStatListByFlagsRva)
            || !CoreSlotNames(image, stateSlot, GetStatListFromUnitAndStateRva)) {
        Context->LogError(
            "ExtraCurses: D2RCore.dll's builder does not call the expected game functions "
            "(IsCurseState, FindStatListByFlags, GetStatListFromUnitAndState); plugin refused.");
        return false;
    }
    CoreBase = reinterpret_cast<std::uintptr_t>(image);
    CoreCurseSlot = CoreBase + static_cast<std::uintptr_t>(curseSlot);
    CoreLookupReturn = CoreBase + static_cast<std::uintptr_t>(CoreLookupReturnRva);
    return true;
}

// Live state of D2RCore's curse-lookup slot, for the console.
auto DescribeCoreSlot() noexcept -> const char* {
    if (CoreCurseSlot == 0U) return "not resolved";
    const auto resolved = Read<std::uint64_t>(reinterpret_cast<const void*>(CoreCurseSlot), 0U);
    if (resolved == reinterpret_cast<std::uintptr_t>(Base) + FindStatListByFlagsRva) {
        return "bound to the game's FindStatListByFlags";
    }
    if (resolved == 0U) return "not bound yet";
    return "bound elsewhere, curse lookups do not reach this hook";
}

// ---------------------------------------------------------------- the hook

// Walks back to the frame whose rip is `returnAddress` and returns that
// frame's rsp, rsi, rdi and r15. RtlVirtualUnwind restores each caller's
// nonvolatile registers from the callee's unwind data, so the values are the
// caller's own whatever this plugin's code did with the registers.
__declspec(noinline) auto CaptureCallerFrame(std::uintptr_t returnAddress) noexcept -> CallerFrame {
    CONTEXT context{};
    ::RtlCaptureContext(&context);
    for (std::size_t depth = 0U; depth < MaxUnwindFrames; ++depth) {
        if (static_cast<std::uintptr_t>(context.Rip) == returnAddress) {
            return {static_cast<std::uintptr_t>(context.Rsp),
                    static_cast<std::uintptr_t>(context.Rsi),
                    static_cast<std::uintptr_t>(context.Rdi),
                    static_cast<std::uintptr_t>(context.R15)};
        }
        DWORD64 imageBase = 0U;
        PRUNTIME_FUNCTION function = ::RtlLookupFunctionEntry(context.Rip, &imageBase, nullptr);
        if (function == nullptr) {
            // A leaf frame keeps its return address at rsp.
            context.Rip = *reinterpret_cast<const DWORD64*>(context.Rsp);
            context.Rsp += sizeof(DWORD64);
            continue;
        }
        PVOID handlerData = nullptr;
        DWORD64 establisherFrame = 0U;
        ::RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, context.Rip, function, &context,
                           &handlerData, &establisherFrame, nullptr);
    }
    return {};
}

auto UnitTypeName(std::uint32_t type) noexcept -> const char* {
    switch (type) {
        case 0U: return "player";
        case 1U: return "monster";
        case 2U: return "object";
        case 3U: return "missile";
        case 4U: return "item";
        default: return "unit";
    }
}

void NoteDecision(
        void* target,
        void* caster,
        std::int32_t state,
        std::int32_t points,
        std::int64_t cap,
        const extra_curses::Decision& decision,
        const CallerFrame& frame) noexcept {
    char outcome[96]{};
    switch (decision.outcome) {
        case extra_curses::Outcome::Added:
            std::snprintf(outcome, sizeof(outcome), "added");
            break;
        case extra_curses::Outcome::SameState:
            std::snprintf(outcome, sizeof(outcome), "same curse, refreshed or refused by the game");
            break;
        case extra_curses::Outcome::Replaced: {
            if (!decision.replacedTimed) {
                std::snprintf(outcome, sizeof(outcome), "replaces state %d (no timer)",
                              decision.replacedState);
                break;
            }
            long long left = -1;
            if (frame.rdi != 0U) {
                const auto* game = Read<const std::uint8_t*>(
                    reinterpret_cast<const void*>(frame.rdi), BuilderGameOffset);
                if (game != nullptr) {
                    const auto now = Read<std::uint32_t>(game, GameFrameOffset);
                    left = static_cast<long long>(decision.replacedExpire)
                         - static_cast<long long>(now);
                }
            }
            if (left >= 0) {
                std::snprintf(outcome, sizeof(outcome), "replaces state %d (%lld frames left)",
                              decision.replacedState, left);
            } else {
                std::snprintf(outcome, sizeof(outcome), "replaces state %d",
                              decision.replacedState);
            }
            break;
        }
        case extra_curses::Outcome::Unremovable:
            std::snprintf(outcome, sizeof(outcome), "no curse the game would remove, added");
            break;
        case extra_curses::Outcome::NoStatListEx:
        default:
            std::snprintf(outcome, sizeof(outcome), "target has no stat list, added");
            break;
    }

    char line[DecisionTextBytes]{};
    std::snprintf(
        line,
        sizeof(line),
        "state %d on %s %u by %s %u: cap %lld (stat %d = %d), %u other curses -> %s%s",
        state,
        UnitTypeName(Read<std::uint32_t>(target, UnitTypeOffset)),
        Read<std::uint32_t>(target, UnitIdOffset),
        UnitTypeName(Read<std::uint32_t>(caster, UnitTypeOffset)),
        Read<std::uint32_t>(caster, UnitIdOffset),
        static_cast<long long>(cap),
        Settings.statId,
        points,
        decision.otherCurses,
        outcome,
        decision.chainTruncated ? " (stat list chain cut at 4096 entries)" : "");

    ::AcquireSRWLockExclusive(&DecisionLock);
    std::memcpy(LastDecision, line, sizeof(LastDecision));
    ::ReleaseSRWLockExclusive(&DecisionLock);

    if (Settings.logDecisions) Context->LogInfo(line);
}

auto HandleBuilderLookup(void* unit) noexcept -> std::uint8_t* {
    BuilderLookups.fetch_add(1U, std::memory_order_relaxed);

    // The builder keeps its curse record in rsi and its context in rdi.
    const CallerFrame frame = CaptureCallerFrame(CoreLookupReturn);
    if (frame.rsp == 0U || frame.rsi == 0U) {
        FrameMisses.fetch_add(1U, std::memory_order_relaxed);
        return OriginalFindStatListByFlags(unit, extra_curses::StatListCurseFlag);
    }
    const auto* record = reinterpret_cast<const std::uint8_t*>(frame.rsi);
    void* target = Read<void*>(record, CurseTargetOffset);
    void* caster = Read<void*>(record, CurseCasterOffset);
    if (target != unit || caster == nullptr) {
        RecordMismatches.fetch_add(1U, std::memory_order_relaxed);
        return OriginalFindStatListByFlags(unit, extra_curses::StatListCurseFlag);
    }

    const std::int32_t state = Read<std::int32_t>(record, CurseStateOffset);
    const std::int32_t points = GetUnitStat(caster, Settings.statId, 0U);
    const std::int64_t cap = extra_curses::CurseCap(points);
    const auto* statListEx = Read<const std::uint8_t*>(unit, UnitStatListExOffset);

    const extra_curses::Decision decision =
        extra_curses::Decide(statListEx, state, cap, Settings.replace);

    switch (decision.outcome) {
        case extra_curses::Outcome::SameState:
            SameStateCount.fetch_add(1U, std::memory_order_relaxed);
            break;
        case extra_curses::Outcome::Added:
            AddedCount.fetch_add(1U, std::memory_order_relaxed);
            break;
        case extra_curses::Outcome::Replaced:
            ReplacedCount.fetch_add(1U, std::memory_order_relaxed);
            break;
        case extra_curses::Outcome::Unremovable:
            UnremovableCount.fetch_add(1U, std::memory_order_relaxed);
            break;
        case extra_curses::Outcome::NoStatListEx:
        default:
            NoStatListCount.fetch_add(1U, std::memory_order_relaxed);
            break;
    }
    if (decision.chainTruncated) TruncatedChains.fetch_add(1U, std::memory_order_relaxed);

    NoteDecision(unit, caster, state, points, cap, decision, frame);
    return const_cast<std::uint8_t*>(decision.compare);
}

// Every FindStatListByFlags call. Only the builder's curse lookup is answered
// by the plugin; every other caller gets the stock function.
std::uint8_t* __fastcall HookFindStatListByFlags(void* unit, std::uint32_t flags) noexcept {
    if (flags == extra_curses::StatListCurseFlag
            && CoreLookupReturn != 0U
            && reinterpret_cast<std::uintptr_t>(EXTRA_CURSES_RETURN_ADDRESS()) == CoreLookupReturn
            && Operational.load(std::memory_order_acquire)) {
        return HandleBuilderLookup(unit);
    }
    return OriginalFindStatListByFlags(unit, flags);
}

// ---------------------------------------------------------- client visuals

// What the overlay tick does when a cast node's animation ends, instruction
// for instruction (0x153551..0x1535B9 for mode 4, 0x1535BE..0x15363C for
// mode 9): delete the node, then dispatch its follow-ups as looping overlays.
void HandOverCast(void* unit, std::uint8_t* node) noexcept {
    const auto mode = Read<std::int32_t>(node, extra_curses::OverlayNodeModeOffset);
    const auto followUp1 = Read<std::int32_t>(node, extra_curses::OverlayNodeFollowUp1Offset);
    const auto followUp2 = Read<std::int32_t>(node, extra_curses::OverlayNodeFollowUp2Offset);
    const auto state = Read<std::int32_t>(node, extra_curses::OverlayNodeStateOffset);
    OriginalOverlayCleanup(unit, node);
    CastHandOvers.fetch_add(1U, std::memory_order_relaxed);

    if (mode == extra_curses::OverlayModeCast) {
        if (followUp1 != extra_curses::NoOverlay) {
            OverlayDispatch(unit, followUp1, extra_curses::OverlayModeLoop, 0, 0, 0, 0);
        }
        if (followUp2 != extra_curses::NoOverlay) {
            OverlayDispatch(unit, followUp2, extra_curses::OverlayModeLoop, 0, 0, 0, 0);
        }
        return;
    }
    if (CheckState(unit, state) == 0) return;
    CastHandOverStates.fetch_add(1U, std::memory_order_relaxed);
    if (followUp1 != extra_curses::NoOverlay) {
        OverlayDispatch(unit, followUp1, extra_curses::OverlayModeStateLoop, 0, state, 0, 0);
    }
    if (followUp2 != extra_curses::NoOverlay) {
        OverlayDispatch(unit, followUp2, extra_curses::OverlayModeStateLoop, 0, state, 0, 0);
    }
}

// Every overlay node removal. Only the dispatcher's collision check is looked
// at; every other caller gets the stock removal.
void __fastcall HookOverlayCleanup(void* unit, std::uint8_t* node) noexcept {
    if (node != nullptr
            && DispatchCollisionReturn != 0U
            && reinterpret_cast<std::uintptr_t>(EXTRA_CURSES_RETURN_ADDRESS())
                == DispatchCollisionReturn
            && ClientFixActive.load(std::memory_order_acquire)) {
        const CallerFrame frame = CaptureCallerFrame(DispatchCollisionReturn);
        if (frame.rsp == 0U) {
            ClientFrameMisses.fetch_add(1U, std::memory_order_relaxed);
        } else {
            const auto* dispatcherStack = reinterpret_cast<const void*>(frame.rsp);
            const extra_curses::OverlayCommand command{
                .id = static_cast<std::int32_t>(frame.rdi),
                .mode = static_cast<std::int32_t>(frame.r15),
                .followUp1 = Read<std::int32_t>(dispatcherStack, DispatchFollowUp1Offset),
                .followUp2 = Read<std::int32_t>(dispatcherStack, DispatchFollowUp2Offset),
            };
            if (extra_curses::IsSupersededForeignCast(node, command)) {
                HandOverCast(unit, node);
                return;
            }
        }
    }
    OriginalOverlayCleanup(unit, node);
}

// ---------------------------------------------------------------- console

template <typename... Args>
void Append(
        char* buffer,
        std::size_t size,
        std::size_t& used,
        const char* format,
        Args... args) noexcept {
    if (used + 1U >= size) return;
    const int written = std::snprintf(buffer + used, size - used, format, args...);
    if (written <= 0) return;
    used += static_cast<std::size_t>(written);
    if (used >= size) used = size - 1U;
}

auto Count(const std::atomic<std::uint64_t>& counter) noexcept -> unsigned long long {
    return static_cast<unsigned long long>(counter.load(std::memory_order_relaxed));
}

auto Status(
        D2R::Game::Client*,
        const D2RL::ConsoleCommandContext* command,
        void*) noexcept -> D2RL::ConsoleCommandResult {
    if (command == nullptr || command->plugin == nullptr) {
        return D2RL::ConsoleCommandResult::Failed;
    }
    char last[DecisionTextBytes]{};
    ::AcquireSRWLockShared(&DecisionLock);
    std::memcpy(last, LastDecision, sizeof(last));
    ::ReleaseSRWLockShared(&DecisionLock);

    char message[1536]{};
    std::size_t used = 0U;
    Append(
        message,
        sizeof(message),
        used,
        "Extra Curses 1.1.0: active=%s; build=%s; stat_id=%d; replace=%s; "
        "hook 0x2F5810: %s; D2RCore curse lookup: %s; builder lookups=%llu: "
        "same curse=%llu, added=%llu, replaced=%llu, unremovable=%llu, "
        "no stat list=%llu; frame not recovered=%llu; record mismatch=%llu; "
        "cut chains=%llu; last: %s; log decisions=%s; curse graphics fix "
        "0x155CB0: %s, casts handed over=%llu (state-tied %llu), frame not "
        "recovered=%llu; config=%s.",
        Operational.load(std::memory_order_acquire) ? "true" : "false",
        RuntimeBuild.c_str(),
        Settings.statId,
        PolicyName(Settings.replace),
        HookInstalled ? "installed" : "not installed",
        DescribeCoreSlot(),
        Count(BuilderLookups),
        Count(SameStateCount),
        Count(AddedCount),
        Count(ReplacedCount),
        Count(UnremovableCount),
        Count(NoStatListCount),
        Count(FrameMisses),
        Count(RecordMismatches),
        Count(TruncatedChains),
        last[0] != '\0' ? last : "none yet",
        Settings.logDecisions ? "on" : "off",
        ClientFixInstalled ? "installed" : "not installed",
        Count(CastHandOvers),
        Count(CastHandOverStates),
        Count(ClientFrameMisses),
        LoadedConfigPath.c_str());
    command->plugin->WriteConsoleMessage(message);
    return D2RL::ConsoleCommandResult::Handled;
}

auto ValidateGameImage() noexcept -> bool {
    return VerifyBytes(
               FindStatListByFlagsBodyRva,
               FindStatListByFlagsBody.data(),
               FindStatListByFlagsBody.size(),
               "FindStatListByFlags")
        && VerifyBytes(
               GetStatListExRva,
               GetStatListExBody.data(),
               GetStatListExBody.size(),
               "UNITS_GetStatListEx")
        && VerifyBytes(
               SetStatListExpireFrameRva,
               SetStatListExpireFrameBody.data(),
               SetStatListExpireFrameBody.size(),
               "STATLIST_SetStatListExpireFrame")
        && VerifyBytes(
               GetUnitStatRva,
               GetUnitStatThunkJump.data(),
               GetUnitStatThunkJump.size(),
               "the loader's stat thunk")
        && VerifyBytes(
               GetUnitStatThunkNopsRva,
               GetUnitStatThunkNops.data(),
               GetUnitStatThunkNops.size(),
               "the loader's stat thunk padding")
        && VerifyBytes(
               GetUnitStatBodyRva,
               GetUnitStatBody.data(),
               GetUnitStatBody.size(),
               "STATLIST_GetUnitStat");
}

auto ValidateClientImage() noexcept -> bool {
    return VerifyBytes(
               OverlayDispatchRva,
               OverlayDispatchWindow.data(),
               OverlayDispatchWindow.size(),
               "the overlay dispatcher")
        && VerifyBytes(
               OverlayTickCastDoneRva,
               OverlayTickCastDoneWindow.data(),
               OverlayTickCastDoneWindow.size(),
               "the overlay tick's cast hand-over")
        && VerifyBytes(
               CheckStateRva,
               CheckStateEntry.data(),
               CheckStateEntry.size(),
               "STATES_CheckState");
}

// Server part: the curse lookup inside the timed-state builder.
void InstallCurseLookup() noexcept {
    if (Settings.statId < 0) {
        Context->LogWarn(
            "ExtraCurses: [curses] stat_id is not set; the curse lookup was not hooked and "
            "every caster keeps one curse. Put the itemstatcost.txt row id of the stat in "
            "the TOML.");
        return;
    }
    if (!ValidateGameImage() || !ResolveCoreLookup()) {
        Context->LogError("ExtraCurses: the curse lookup was not hooked; every caster keeps one curse.");
        return;
    }
    GetUnitStat = At<GetUnitStatFn>(GetUnitStatRva);

    // Armed before the hook lands: the hook can be entered the instant the
    // bytes change.
    Operational.store(true, std::memory_order_release);
    if (!Context->InstallInlineHook(
            FindStatListByFlagsRva,
            FindStatListByFlagsEntry.data(),
            FindStatListByFlagsHookBytes,
            &HookFindStatListByFlags,
            &OriginalFindStatListByFlags)
        || OriginalFindStatListByFlags == nullptr) {
        Operational.store(false, std::memory_order_release);
        OriginalFindStatListByFlags = nullptr;
        Context->LogError(
            "ExtraCurses: FindStatListByFlags (0x2F5810) is already hooked by another "
            "plugin, or the hook was refused; every caster keeps one curse.");
        return;
    }
    HookInstalled = true;
}

// Client part: every curse keeps its own graphic.
void InstallCurseGraphicsFix() noexcept {
    if (!ValidateClientImage()) {
        Context->LogError(
            "ExtraCurses: the curse graphics fix was not hooked; a curse that lands during "
            "another curse's cast flash can lose its graphic.");
        return;
    }
    OverlayDispatch = At<OverlayDispatchFn>(OverlayDispatchRva);
    CheckState = At<CheckStateFn>(CheckStateRva);
    DispatchCollisionReturn = reinterpret_cast<std::uintptr_t>(Base) + OverlayDispatchCollisionReturnRva;

    ClientFixActive.store(true, std::memory_order_release);
    if (!Context->InstallInlineHook(
            OverlayCleanupRva,
            OverlayCleanupEntry.data(),
            OverlayCleanupHookBytes,
            &HookOverlayCleanup,
            &OriginalOverlayCleanup)
        || OriginalOverlayCleanup == nullptr) {
        ClientFixActive.store(false, std::memory_order_release);
        OriginalOverlayCleanup = nullptr;
        Context->LogError(
            "ExtraCurses: the overlay removal (0x155CB0) is already hooked by another plugin, "
            "or the hook was refused; the curse graphics fix is off.");
        return;
    }
    ClientFixInstalled = true;
}

}  // namespace

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* {
    return &Info;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept
        -> bool {
    if (!D2RL::HasContext(context) || context->abiVersion != D2RL_PLUGIN_ABI_VERSION) {
        return false;
    }
    Context = context;
    Base = reinterpret_cast<std::uint8_t*>(context->exeBase);
    if (Base == nullptr || !LoadConfig()) return false;

    const auto* runtimeBuild = D2RL::GetBuildName(context);
    if (runtimeBuild != nullptr && runtimeBuild[0] != '\0') {
        RuntimeBuild = runtimeBuild;
    }

    if (!context->RegisterConsoleCommand(
            "extra-curses",
            Status,
            "Show Extra Curses settings, hook state and counters.")) {
        context->LogWarn("ExtraCurses: optional status command was not registered.");
    }

    if (!Settings.enabled) {
        context->LogInfo("ExtraCurses: loaded disabled; nothing was hooked.");
        return true;
    }

    // The two parts are independent: each logs why it is off.
    InstallCurseGraphicsFix();
    InstallCurseLookup();

    char message[640]{};
    std::snprintf(
        message,
        sizeof(message),
        "Extra Curses 1.1.0 for observed D2R %s: curse lookup %s (stat %d, cap = 1 + "
        "points, replace=%s, decisions log %s, D2RCore slot %s); curse graphics fix "
        "%s; config=%s.",
        RuntimeBuild.c_str(),
        HookInstalled ? "hooked" : "not hooked",
        Settings.statId,
        PolicyName(Settings.replace),
        Settings.logDecisions ? "on" : "off",
        DescribeCoreSlot(),
        ClientFixInstalled ? "hooked" : "not hooked",
        LoadedConfigPath.c_str());
    context->LogInfo(message);
    return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
    // Disarmed, both hooks hand every call to the stock function.
    Operational.store(false, std::memory_order_release);
    ClientFixActive.store(false, std::memory_order_release);
}
