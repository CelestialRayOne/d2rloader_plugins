// Bleed
//
// Physical hits make the target bleed: it loses life every frame for a
// while, carried by negative life regeneration on a timed state. The item
// tooltip shows the three bleed stats as one composed line.
//
// Port of the ESR D2R 2.4 bleed system to D2R 3.3 on D2RLoader 1.3.0. The
// 2.4 memory patches were:
//   326772 -> cave 386E83   bleed on hit, in the per-hit life total
//   1C709E -> cave 386D20   tooltip accumulator, reads the bleed stats
//   1C81E9 -> cave 386DC9   tooltip composer, one line for the three stats
// Everything below was read out of the live D2RLoader.exe image and
// D2RCore.dll and disassembled before being relied on.
//
// ---------------------------------------------------------------------------
// What the 2.4 cave did, and what this plugin does the same way
// ---------------------------------------------------------------------------
//   On every hit whose life total is computed:
//     physical = post-resistance, post-absorb physical damage of the hit
//     skip if physical <= 0, no attacker, or no attacker stat list
//     min = attacker min stat, skip if <= 0      (total over every source)
//     max = attacker max stat
//     len = attacker length stat, skip if <= 0   (total over every source)
//     N   = number of stat lists attached to the attacker whose own
//           length stat is above 0, at least 1
//     avg = len / N, skip if <= 0
//     seed: low = low * 0x6AC690C5 + high, only the low half is written
//     roll = max >= min ? min + low % (max - min + 1) : min
//     perFrame = (roll * physical) / (avg * 100), 64-bit, clamped to int32,
//                skip if <= 0
//     apply the timed state: level = perFrame, duration = avg,
//                            stat 74 = -perFrame, callback = default
//   The arithmetic below is the 2.4 cave instruction for instruction,
//   including the 32-bit avg * 100 and the low-half-only seed write.
//
// ---------------------------------------------------------------------------
// Part 1: the hit, SUNITDMG_CalculateTotalDamage 0x44DF10 (2.4 sub_140325AF0)
// ---------------------------------------------------------------------------
//   void (game, attacker, defender, D2Damage*)
//     44DF3D  4C 8B EA              mov  r13, rdx            ; attacker
//     44DF54  4D 8B F9              mov  r15, r9             ; D2Damage
//     44DF5E  49 8B F0              mov  rsi, r8             ; defender
//   A full linear sweep of the function (899 instructions) finds no other
//   write to r13, r15 or rsi before the epilogue.
//
//   Physical is D2Damage+18h: the resistance loop's first record points at
//   it and uses damageresist (stat 36):
//     44E142  49 8D 47 18           lea  rax, [r15+18h]
//     44E149  48 89 45 80           mov  [rbp-80h], rax
//     44E14D  C7 45 88 24 00 00 00  mov  dword [rbp-78h], 24h
//
//   The absorb event (11) fires at 44EB3A, then the life total is summed:
//     44EC71  41 8B 47 30 ...       Magc + Cold + Phys + Ligt + Pois + Fire
//     44EC99  41 89 87 34 01 00 00  mov  [r15+134h], eax
//     44ECA0  4C 89 7C 24 20        mov  [rsp+20h], r15
//     44ECA5  E8 C6 E8 FF FF        call 44D570              ; event 16
//     44ECAA  45 85 E4              test r12d, r12d
//   The only branch into 44EC71..44ECAA lands on 44EC71, so reaching 44ECA5
//   means the life total was just summed, which is exactly where the 2.4
//   hook sat. Nothing between the two touches D2Damage+18h.
//
//   The poison immunity plugin patches 44EC8C and pins 44EC71..44ECA4, so
//   this plugin changes only the rel32 of the call at 44ECA5. It lands in a
//   near relay that jumps to HookedLifeTotalEvent, which receives the call's
//   own arguments (game, 16, defender, attacker, D2Damage), applies bleed,
//   then makes the original call to D2GAME_ApplyUnitStatEvent 0x44D570.
//
// ---------------------------------------------------------------------------
// Stats and the stat list chain in D2RLoader 1.3.0
// ---------------------------------------------------------------------------
//   D2RLoader owns stat storage. The game image entries are thunks:
//     2F6CD0  FF 25 / 90 x4         D2RCore!ReadWideEffectiveStat
//             int32 (dataContext, statList, statId, layer): full stats for
//             an extended list, base stats otherwise. This is the 2.4 reader
//             sub_1401E2E70 the cave used for the totals and for each list.
//     2F5DF0  FF 25 / 90 x4         D2RCore!ReadWideListStat
//             int32 (dataContext, statList, statId, layer): base stats. This
//             is the 2.4 reader sub_1401E2E00 the tooltip accumulator used.
//   Data context is Unit+1BDh (34A0E0), the stat list is Unit+88h (34B870).
//
//   Child lists: 2.4 walked [list+68h] then [node+48h]. 3.3 moved both:
//     2F59BB  48 8B 87 90 00 00 00  mov  rax, [rdi+90h]      ; first child
//     2F59C7  39 58 20              cmp  [rax+20h], ebx      ; state id
//     2F59CC  48 8B 40 68           mov  rax, [rax+68h]      ; next child
//   D2RCore's attach links merged lists into +90h and lists flagged 2000h
//   (never merged into the parent) into +98h, both through +68h. 2.4 walked
//   only the merged chain, so this plugin walks only +90h.
//
//   The seed pair is Unit+28h (UNITS_GetSeed 34A1E0).
//
// ---------------------------------------------------------------------------
// The timed state, 0x433D20 (2.4 sub_140295240)
// ---------------------------------------------------------------------------
//   Thunk to D2RCore!ApplyWideTimedStatEffect, StatList* (ApplyArgs*).
//   The native Open Wounds callback 584170 fills the same 30h record:
//     +00 source  +08 target  +10 skill 0  +14 level 1  +18 duration 200
//     +1C stat 74  +20 value -damage  +24 state 62  +28 callback 0
//   D2RCore's implementation looks the state up on the target: an equal
//   level only refreshes the duration, a lower level is refused, a higher
//   level replaces the list. 2.4 put perFrame in the level field for exactly
//   that highest-per-frame-wins rule, and so does this plugin.
//
// ---------------------------------------------------------------------------
// Part 2: the tooltip
// ---------------------------------------------------------------------------
//   D2RLoader replaced the item stat list renderer 2DA760 with
//   D2RCore!RenderWideItemStatList, which still calls the game image through
//   its import slots: the accumulator 2DAED0 once per tooltip (which calls
//   2DB370 first) and the composer 2DB800 once per stat entry, in
//   description order. Both are hooked at their entries.
//
//   2DB370 (unit, statList, accumulator): clears 118h bytes, then reads the
//   special stats with ReadWideListStat(context, statList, stat, 0). The hook
//   runs it, then reads the three bleed stats the same way. The flag is
//   min > 0 && max > 0, as in 2.4. The values are kept per accumulator on the
//   calling thread, never written into the game's accumulator.
//
//   2DB800 (accumulator, statId, output, capacity, chronicle):
//     2DB84B  83 C3 EF              add  ebx, -11h
//     2DB84E  83 FB 2A              cmp  ebx, 2Ah
//     2DB851  0F 87 62 06 00 00     ja   2DBEB9              ; return 0
//   Stats 17..59 go to the native switch first, as the 2.4 cave did. For the
//   min stat with the flag set the hook formats the line into a 256-byte
//   buffer from (text, min, max, seconds), the argument order of the native
//   poison range line, then runs the native tail: the chronicle bullet when
//   requested, the line, and the newline key, each through the composer's
//   own string resolver and 12276A0. The max and length stats return the flag.
//
//   The line is formatted by the plugin itself rather than by the game's
//   formatter 847F0, so the text may show the seconds with decimals.
//   FormatBleedLine fills the text one placeholder at a time: placeholder n,
//   numbered (%3$.2f) or in order (%.2f), takes value n. A floating
//   placeholder gets the exact seconds, length / 25.0, without the trailing
//   zeros of its decimals (3.20 shows as 3.2, 10.00 as 10); an integer one
//   gets the whole seconds, length / 25. A text still written with %d keeps
//   working and never reads a double as an int.

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

namespace CelestialRayOne::Bleed {
namespace {

// ---------------------------------------------------------------------------
// Native anchors
// ---------------------------------------------------------------------------

constexpr std::uint64_t ApplyTimedStateRva          = 0x433D20;
constexpr std::uint64_t ReadEffectiveStatRva        = 0x2F6CD0;
constexpr std::uint64_t ReadListStatRva             = 0x2F5DF0;
constexpr std::uint64_t GetDataContextRva           = 0x34A0E0;
constexpr std::uint64_t GetUnitSeedRva              = 0x34A1E0;
constexpr std::uint64_t GetUnitStatListRva          = 0x34B870;
constexpr std::uint64_t StatListChainRva            = 0x2F59BB;

constexpr std::uint64_t DamageRegistersRva          = 0x44DF3D;
constexpr std::uint64_t DamagePhysicalRecordRva     = 0x44E142;
constexpr std::uint64_t LifeTotalEventCallRva       = 0x44ECA5;
constexpr std::uint64_t ApplyUnitStatEventRva       = 0x44D570;

constexpr std::uint64_t AccumulatorRva              = 0x2DB370;
constexpr std::uint64_t AccumulatorSetupRva         = 0x2DB383;
constexpr std::uint64_t AccumulatorPoisonLengthRva  = 0x2DB504;
constexpr std::uint64_t ComposerRva                 = 0x2DB800;
constexpr std::uint64_t ComposerHeadRva             = 0x2DB82C;
constexpr std::uint64_t ComposerTailBulletRva       = 0x2DBE41;
constexpr std::uint64_t ComposerTailLineRva         = 0x2DBE69;
constexpr std::uint64_t ComposerTailReturnRva       = 0x2DBEA5;
constexpr std::uint64_t ComposerBulletResolveCallRva = 0x2DBE64;
constexpr std::uint64_t ComposerNewlineResolveCallRva = 0x2DBEA0;
constexpr std::uint64_t AppendTextRva               = 0x12276A0;

// The thunks keep an FF 25 disp32 jump into D2RLoader's import slot. The
// slot moves between loader builds, so only the opcode, the padding and the
// untouched remainder of the old body are pinned.
constexpr std::uint64_t ThunkPaddingOffset = 6;
constexpr std::uint64_t ThunkBodyOffset    = 10;
constexpr std::uint64_t ApplyTimedStateBodyOffset = 6;

// ---------------------------------------------------------------------------
// Witnesses, generated from the live image
// ---------------------------------------------------------------------------

// FF 25 disp32 and the four NOPs every padded thunk keeps.
constexpr std::uint8_t ThunkJumpWitness[]{ 0xFF, 0x25 };
constexpr std::uint8_t ThunkPaddingWitness[]{ 0x90, 0x90, 0x90, 0x90 };

// 433D26: the old body after the timed state thunk (push rsi .. mov rcx,rbp).
constexpr std::uint8_t ApplyTimedStateBodyWitness[]{
    0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83,
    0xEC, 0x50, 0x48, 0x8B, 0x69, 0x08, 0x48, 0x8B, 0xF1, 0x48, 0x8B, 0xCD,
};

// 2F6CDA: the old effective stat reader body (dataContext, list, stat, layer).
constexpr std::uint8_t ReadEffectiveStatBodyWitness[]{
    0x57, 0x48, 0x83, 0xEC, 0x20, 0x41, 0x0F, 0xB7, 0xF1, 0x41, 0x8B, 0xD8,
    0x48, 0x8B, 0xFA, 0x48, 0x85, 0xD2, 0x74, 0x22, 0x8B, 0xD3, 0xE8, 0x3B,
    0x2A, 0xFE, 0xFF,
};

// 2F5DFA: the old list stat reader body (dataContext, list, stat, layer).
constexpr std::uint8_t ReadListStatBodyWitness[]{
    0x57, 0x48, 0x83, 0xEC, 0x20, 0x41, 0x0F, 0xB7, 0xF1, 0x41, 0x8B, 0xD8,
    0x48, 0x8B, 0xFA, 0x48, 0x85, 0xD2, 0x75, 0x12,
};

// 34A0E0: returns byte [unit+1BDh].
constexpr std::uint8_t GetDataContextBodyWitness[]{
    0x48, 0x83, 0xEC, 0x28, 0x48, 0x85, 0xC9, 0x75, 0x1A, 0x88, 0x4C, 0x24,
    0x30, 0x48, 0x8D, 0x4C, 0x24, 0x30, 0xE8, 0x49, 0xC7, 0xFF, 0xFF, 0x84,
    0xC0, 0x74, 0x01, 0xCC, 0x32, 0xC0, 0x48, 0x83, 0xC4, 0x28, 0xC3, 0x0F,
    0xB6, 0x81, 0xBD, 0x01, 0x00, 0x00, 0x48, 0x83, 0xC4, 0x28, 0xC3,
};

// 34A1E0: returns unit+28h.
constexpr std::uint8_t GetUnitSeedBodyWitness[]{
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0x48, 0x85, 0xC9,
    0x75, 0x1D, 0x88, 0x4C, 0x24, 0x30, 0x48, 0x8D, 0x4C, 0x24, 0x30, 0xE8,
    0x94, 0xBB, 0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x01, 0xCC, 0x48, 0x8D, 0x43,
    0x28, 0x48, 0x83, 0xC4, 0x20, 0x5B, 0xC3, 0x48, 0x8D, 0x41, 0x28, 0x48,
    0x83, 0xC4, 0x20, 0x5B, 0xC3,
};

// 34B870: returns [unit+88h].
constexpr std::uint8_t GetUnitStatListBodyWitness[]{
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0x48, 0x85, 0xC9,
    0x75, 0x20, 0x88, 0x4C, 0x24, 0x30, 0x48, 0x8D, 0x4C, 0x24, 0x30, 0xE8,
    0x44, 0x9B, 0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x01, 0xCC, 0x48, 0x8B, 0x83,
    0x88, 0x00, 0x00, 0x00, 0x48, 0x83, 0xC4, 0x20, 0x5B, 0xC3,
};

// 2F59BB: first child [list+90h], second chain [list+98h], next [child+68h].
constexpr std::uint8_t StatListChainWitness[]{
    0x48, 0x8B, 0x87, 0x90, 0x00, 0x00, 0x00, 0x48, 0x85, 0xC0, 0x74, 0x0E,
    0x39, 0x58, 0x20, 0x74, 0x25, 0x48, 0x8B, 0x40, 0x68, 0x48, 0x85, 0xC0,
    0x75, 0xF2, 0x48, 0x8B, 0x87, 0x98, 0x00, 0x00, 0x00, 0x48, 0x85, 0xC0,
    0x74, 0x0E, 0x39, 0x58, 0x20, 0x74, 0x0B, 0x48, 0x8B, 0x40, 0x68,
};

// 44DF3D: mov r13,rdx (attacker) / mov r15,r9 (D2Damage) / mov rsi,r8 (defender).
constexpr std::uint8_t DamageRegistersWitness[]{
    0x4C, 0x8B, 0xEA, 0x48, 0x89, 0x4C, 0x24, 0x30, 0x0F, 0xB6, 0x91, 0x04,
    0x01, 0x00, 0x00, 0x48, 0x8B, 0xF9, 0x48, 0x89, 0x4C, 0x24, 0x40, 0x4D,
    0x8B, 0xF9, 0x0F, 0xB6, 0x89, 0x06, 0x01, 0x00, 0x00, 0x49, 0x8B, 0xF0,
};

// 44E142: lea rax,[r15+18h] / record resistance stat 24h (damageresist).
constexpr std::uint8_t DamagePhysicalRecordWitness[]{
    0x49, 0x8D, 0x47, 0x18, 0x48, 0x8B, 0xCF, 0x48, 0x89, 0x45, 0x80, 0xC7,
    0x45, 0x88, 0x24, 0x00, 0x00, 0x00,
};

// 44ECA5: call 44D570 (event 16) / test r12d,r12d / je / add [r15+134h].
constexpr std::uint8_t LifeTotalEventCallWitness[]{
    0xE8, 0xC6, 0xE8, 0xFF, 0xFF, 0x45, 0x85, 0xE4, 0x74, 0x0E, 0x41, 0x8B,
    0x87, 0x20, 0x01, 0x00, 0x00, 0x41, 0x01, 0x87, 0x34, 0x01, 0x00, 0x00,
};

// 2DB370: push rbx / sub rsp,30h / mov [rsp+40h],rbp / mov rbx,r8. Hooked.
constexpr std::uint8_t AccumulatorPrologueWitness[]{
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x30, 0x48, 0x89, 0x6C, 0x24, 0x40, 0x49,
    0x8B, 0xD8,
};

// 2DB383: memset 118h, rsi = statList, r14 = unit, ebp = data context.
constexpr std::uint8_t AccumulatorSetupWitness[]{
    0x41, 0xB8, 0x18, 0x01, 0x00, 0x00, 0x48, 0x8B, 0xF2, 0x48, 0x89, 0x7C,
    0x24, 0x50, 0x4C, 0x89, 0x74, 0x24, 0x28, 0x33, 0xD2, 0x4C, 0x8B, 0xF1,
    0x48, 0x8B, 0xCB, 0xE8, 0x6D, 0x89, 0xFF, 0x00, 0x49, 0x8B, 0xCE, 0xE8,
    0x35, 0xED, 0x06, 0x00, 0x33, 0xFF, 0x0F, 0xB6, 0xE8,
};

// 2DB504: ReadListStat(bpl, rsi, stat, 0) and ReadUnitStat(r14, 146h, 0).
constexpr std::uint8_t AccumulatorPoisonLengthWitness[]{
    0x45, 0x33, 0xC9, 0x48, 0x8B, 0xD6, 0x40, 0x0F, 0xB6, 0xCD, 0xE8, 0xDD,
    0xA8, 0x01, 0x00, 0x45, 0x33, 0xC0, 0x89, 0x83, 0xD8, 0x00, 0x00, 0x00,
    0xBA, 0x46, 0x01, 0x00, 0x00, 0x49, 0x8B, 0xCE, 0xE8, 0xF7, 0x9A, 0x01,
    0x00, 0x89, 0x83, 0xDC, 0x00, 0x00,
};

// 2DB800: mov [rsp+10h],rbx / push rbp,rsi,rdi,r14,r15. Hooked.
constexpr std::uint8_t ComposerPrologueWitness[]{
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x55, 0x56, 0x57, 0x41, 0x56, 0x41, 0x57,
};

// 2DB82C: r14 = output, ebx = stat, rdi = accumulator, r15 = capacity, stats 17..59 switch.
constexpr std::uint8_t ComposerHeadWitness[]{
    0x4D, 0x8B, 0xF0, 0x8B, 0xDA, 0x48, 0x8B, 0xF9, 0x33, 0xD2, 0x41, 0xB8,
    0x00, 0x01, 0x00, 0x00, 0x48, 0x8D, 0x4D, 0x90, 0x4D, 0x8B, 0xF9, 0xE8,
    0xC8, 0x84, 0xFF, 0x00, 0x45, 0x33, 0xC0, 0x83, 0xC3, 0xEF, 0x83, 0xFB,
    0x2A, 0x0F, 0x87, 0x62, 0x06, 0x00, 0x00,
};

// 2DBE41: chronicle test and the ChroniclePropertyGroupListBullet key, up to its E8.
constexpr std::uint8_t ComposerTailBulletWitness[]{
    0x80, 0xBD, 0xF0, 0x02, 0x00, 0x00, 0x00, 0x74, 0x2D, 0x48, 0x8D, 0x05,
    0x2F, 0x9A, 0xA1, 0x01, 0x48, 0xC7, 0x44, 0x24, 0x38, 0x20, 0x00, 0x00,
    0x00, 0x48, 0x8D, 0x4C, 0x24, 0x30, 0x48, 0x89, 0x44, 0x24, 0x30, 0xE8,
};

// 2DBE69: append bullet, append line, the newline key, up to its E8.
constexpr std::uint8_t ComposerTailLineWitness[]{
    0x4C, 0x8B, 0xC0, 0x49, 0x8B, 0xD7, 0x49, 0x8B, 0xCE, 0xE8, 0x29, 0xB8,
    0xF4, 0x00, 0x4C, 0x8D, 0x45, 0x90, 0x49, 0x8B, 0xD7, 0x49, 0x8B, 0xCE,
    0xE8, 0x1A, 0xB8, 0xF4, 0x00, 0x48, 0x8D, 0x05, 0xF3, 0x70, 0x9E, 0x01,
    0x48, 0xC7, 0x44, 0x24, 0x38, 0x07, 0x00, 0x00, 0x00, 0x48, 0x8D, 0x4C,
    0x24, 0x30, 0x48, 0x89, 0x44, 0x24, 0x30, 0xE8,
};

// 2DBEA5: append newline / mov r8d,1.
constexpr std::uint8_t ComposerTailReturnWitness[]{
    0x4C, 0x8B, 0xC0, 0x49, 0x8B, 0xD7, 0x49, 0x8B, 0xCE, 0xE8, 0xED, 0xB7,
    0xF4, 0x00, 0x41, 0xB8, 0x01, 0x00, 0x00, 0x00,
};

// 12276A0: append(output, capacity, text).
constexpr std::uint8_t AppendTextPrologueWitness[]{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89,
    0x74, 0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0xC7, 0xC3, 0xFF,
    0xFF, 0xFF, 0xFF, 0x49, 0x8B, 0xC0, 0x48, 0x8B,
};

// ---------------------------------------------------------------------------
// Native layout
// ---------------------------------------------------------------------------

constexpr std::size_t DamagePhysicalOffset     = 0x18;
constexpr std::size_t UnitStatListOffset       = 0x88;
constexpr std::size_t StatListFirstChildOffset = 0x90;
constexpr std::size_t StatListNextChildOffset  = 0x68;

constexpr std::int32_t HitpointRegenStatId = 74;
constexpr std::int32_t FramesPerSecond     = 25;
constexpr std::int32_t FirstNativeComposerStat = 17;
constexpr std::int32_t LastNativeComposerStat  = 59;

constexpr std::uint32_t CallSize               = 5;
constexpr std::uint32_t AccumulatorHookSize    = sizeof(AccumulatorPrologueWitness);
constexpr std::uint32_t ComposerHookSize       = sizeof(ComposerPrologueWitness);
constexpr std::size_t   TooltipBufferBytes     = 256;

constexpr char BulletKey[]  = "ChroniclePropertyGroupListBullet";
constexpr char NewlineKey[] = "newline";

// D2RCore!ApplyWideTimedStatEffect argument record, laid out as the native
// Open Wounds callback fills it.
struct ApplyArgs {
    void*        source;
    void*        target;
    std::int32_t skill;
    std::int32_t level;
    std::int32_t duration;
    std::int32_t statId;
    std::int32_t value;
    std::int32_t stateId;
    void*        callback;
};

static_assert(sizeof(ApplyArgs) == 0x30);
static_assert(offsetof(ApplyArgs, skill) == 0x10);
static_assert(offsetof(ApplyArgs, level) == 0x14);
static_assert(offsetof(ApplyArgs, duration) == 0x18);
static_assert(offsetof(ApplyArgs, statId) == 0x1C);
static_assert(offsetof(ApplyArgs, value) == 0x20);
static_assert(offsetof(ApplyArgs, stateId) == 0x24);
static_assert(offsetof(ApplyArgs, callback) == 0x28);

// The string resolver takes a pointer to { text, length }.
struct StringKey {
    const char*   text;
    std::uint64_t length;
};

static_assert(sizeof(StringKey) == 16);

// Every narrow argument is passed as a zero-extended 64-bit value: D2RCore
// builds its lookup keys from full registers.
using ApplyTimedStateFn   = void*(__fastcall*)(ApplyArgs* args);
using ReadStatFn          = std::int32_t(__fastcall*)(std::uint64_t dataContext, void* statList,
                                std::uint64_t statId, std::uint64_t layer);
using GetDataContextFn    = std::uint8_t(__fastcall*)(void* unit);
using GetUnitSeedFn       = std::uint32_t*(__fastcall*)(void* unit);
using UnitStatEventFn     = std::int32_t(__fastcall*)(void* game, std::uint64_t eventId,
                                void* unit, void* otherUnit, void* damage);
using AccumulateFn        = std::uint64_t(__fastcall*)(void* unit, void* statList,
                                std::int32_t* accumulator) noexcept;
using ComposeFn           = std::uint64_t(__fastcall*)(std::int32_t* accumulator,
                                std::uint64_t statId, char* output, std::uint64_t capacity,
                                std::uint64_t chronicle) noexcept;
using AppendTextFn        = void(__fastcall*)(char* output, std::uint64_t capacity,
                                const char* text);
using ResolveStringKeyFn  = const char*(__fastcall*)(const StringKey* key);

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

constexpr std::int32_t MaximumTableId        = 0x7FFF;
constexpr std::size_t  MaximumStringKeyBytes = 128;
constexpr std::size_t  MaximumConfigBytes    = 32'768;

struct Config {
    bool         bleedEnabled   = true;
    std::int32_t minimumStat    = 435;
    std::int32_t maximumStat    = 436;
    std::int32_t lengthStat     = 437;
    std::int32_t stateId        = 62;
    bool         tooltipEnabled = true;
    std::string  stringKey      = "strModBleedDamageRange";
};

constexpr char DefaultConfigToml[] =
    "# Bleed\n"
    "#\n"
    "# Physical hits make the target bleed: it loses life every frame for a\n"
    "# while, the way poison drains life. The item tooltip shows the three bleed\n"
    "# stats as one line. Port of the ESR D2R 2.4 bleed system; the damage and\n"
    "# how it is dealt are unchanged from 2.4.\n"
    "#\n"
    "# How a hit applies bleed\n"
    "#   1. The hit must still deal physical damage after the target's damage\n"
    "#      reduction and absorb.\n"
    "#   2. The attacker needs min_damage_stat above 0 and length_stat above 0.\n"
    "#      Each of the three stats is read as the attacker's total from every\n"
    "#      source (items, charms, states and so on).\n"
    "#   3. A percent is rolled from min_damage_stat to max_damage_stat, both\n"
    "#      included. If max is below min, min is used.\n"
    "#   4. The length is averaged: the total length is divided by the number of\n"
    "#      separate stat sources on the attacker that carry a length. Two items\n"
    "#      with 100 frames each give an average of 100, not 200.\n"
    "#   5. The bleed deals physical damage x percent / 100 in total, spread\n"
    "#      evenly over the averaged length. Per frame that is\n"
    "#      physical x percent / (100 x averaged length), rounded down.\n"
    "#      Example: 100 physical, 100 percent, 100 frames drains 1 life per\n"
    "#      frame for 4 seconds.\n"
    "#   6. The target gets state_id for the averaged length, carrying negative\n"
    "#      life regeneration (stat 74) equal to that per-frame amount.\n"
    "#      A stronger bleed replaces a weaker one, an equal one only refreshes\n"
    "#      the duration, a weaker one is ignored. Bleed and poison stack.\n"
    "#\n"
    "# Stat ids and state ids are itemstatcost.txt and states.txt row ids,\n"
    "# 0 to 32767.\n"
    "#\n"
    "# Console command: bleed (status and counters)\n"
    "\n"
    "[bleed]\n"
    "\n"
    "# Master switch for bleed on hit.\n"
    "enabled = true\n"
    "\n"
    "# Lowest bleed percent of the hit's physical damage.\n"
    "min_damage_stat = 435\n"
    "\n"
    "# Highest bleed percent of the hit's physical damage.\n"
    "max_damage_stat = 436\n"
    "\n"
    "# Bleed length in frames, 25 frames per second.\n"
    "length_stat = 437\n"
    "\n"
    "# states.txt row the bleed is attached to. 2.4 used 62 (openwounds).\n"
    "# Point it at a new row to give bleed its own state, so it no longer\n"
    "# shares a state with Open Wounds on the same target. Use a row that is\n"
    "# not flagged as a curse, like openwounds: curse rows take the game's\n"
    "# curse rules, which shorten the duration and can refuse the state.\n"
    "state_id = 62\n"
    "\n"
    "[tooltip]\n"
    "\n"
    "# Master switch for the composed tooltip line.\n"
    "enabled = true\n"
    "\n"
    "# String key of the tooltip line. The text receives three values in this\n"
    "# order: min percent, max percent, length in seconds (length / 25).\n"
    "# Write the percents as %d. Write the seconds as %.2f for up to two\n"
    "# decimals or as %d for whole seconds. Trailing zeros of the decimals are\n"
    "# never shown: 80 frames shows 3.2 (not 3.20), 250 frames shows 10 (not\n"
    "# 10.00). %d rounds down: 80 frames shows 3.\n"
    "# A floating placeholder (%f, %e, %g, with any precision) gets the exact\n"
    "# seconds, an integer one (%d, %i, %u) gets whole seconds. Placeholders\n"
    "# may be numbered (%1$d, %2$d, %3$.2f), so a language can put the three\n"
    "# values in another order.\n"
    "#\n"
    "# The line appears where the min stat's own line would be. While both min\n"
    "# and max are above 0 on the item, the max and length stats show no line\n"
    "# of their own; otherwise every stat shows its normal description.\n"
    "# The stats need their itemstatcost.txt description setup from 2.4: the\n"
    "# game only lists stats that have a description, and descpriority decides\n"
    "# where the line appears. Stat ids 17 to 59 are the game's own damage lines\n"
    "# and never get the composed line.\n"
    "string_key = \"strModBleedDamageRange\"\n";

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

enum class PartState : std::uint8_t {
    NotLoaded,
    DisabledByConfig,
    Armed,
    PartiallyArmed,
    Failed,
};

const D2RL::PluginContext* Context{};
std::uintptr_t             Base{};
Config                     Settings{};

ApplyTimedStateFn  ApplyTimedState{};
ReadStatFn         ReadEffectiveStat{};
ReadStatFn         ReadListStat{};
GetDataContextFn   GetDataContext{};
GetUnitSeedFn      GetUnitSeed{};
UnitStatEventFn    OriginalUnitStatEvent{};
AccumulateFn       OriginalAccumulate{};
ComposeFn          OriginalCompose{};
AppendTextFn       AppendText{};
ResolveStringKeyFn ResolveStringKey{};

PartState DamageState{ PartState::NotLoaded };
PartState TooltipState{ PartState::NotLoaded };

void* RelayPage{};
bool  LifeTotalCallPatched{};

std::atomic<std::uint64_t> BleedsApplied{};
std::atomic<std::uint64_t> BleedsRefused{};
std::atomic<std::uint64_t> TooltipLines{};

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
    if (value == "true") { out = true; return true; }
    if (value == "false") { out = false; return true; }
    return false;
}

auto ParseTableId(std::string_view value, std::int32_t& out) noexcept -> bool {
    if (value.empty()) return false;
    std::int64_t accumulator = 0;
    for (const char character : value) {
        if (character == '_') continue;
        if (character < '0' || character > '9') return false;
        accumulator = accumulator * 10 + (character - '0');
        if (accumulator > MaximumTableId) return false;
    }
    out = static_cast<std::int32_t>(accumulator);
    return true;
}

// A quoted TOML string without escapes: "..." or '...'. String keys are
// plain identifiers, so escapes are refused rather than interpreted.
auto ParseStringKey(std::string_view value, std::string& out) noexcept -> bool {
    if (value.size() < 2) return false;
    const char quote = value.front();
    if ((quote != '"' && quote != '\'') || value.back() != quote) return false;
    const std::string_view inner = value.substr(1, value.size() - 2);
    if (inner.empty() || inner.size() > MaximumStringKeyBytes) return false;
    for (const char character : inner) {
        if (character <= ' ' || character > '~' || character == '"'
                || character == '\'' || character == '\\') {
            return false;
        }
    }
    out.assign(inner.data(), inner.size());
    return true;
}

// The value ends at the first # outside quotes.
auto StripComment(std::string_view line) noexcept -> std::string_view {
    char quote = 0;
    for (std::size_t index = 0; index < line.size(); ++index) {
        const char character = line[index];
        if (quote != 0) {
            if (character == quote) quote = 0;
        } else if (character == '"' || character == '\'') {
            quote = character;
        } else if (character == '#') {
            return line.substr(0, index);
        }
    }
    return line;
}

std::string ConfigProblems;

void NoteProblem(const char* key, const char* expected) noexcept {
    char message[192];
    std::snprintf(message, sizeof(message), "%s must be %s; using the default. ", key, expected);
    ConfigProblems += message;
}

void ApplyConfigLine(std::string_view section, std::string_view key, std::string_view value) noexcept {
    if (section == "bleed") {
        if (key == "enabled") {
            if (!ParseBool(value, Settings.bleedEnabled)) NoteProblem("bleed.enabled", "true or false");
        } else if (key == "min_damage_stat") {
            if (!ParseTableId(value, Settings.minimumStat)) NoteProblem("bleed.min_damage_stat", "0 to 32767");
        } else if (key == "max_damage_stat") {
            if (!ParseTableId(value, Settings.maximumStat)) NoteProblem("bleed.max_damage_stat", "0 to 32767");
        } else if (key == "length_stat") {
            if (!ParseTableId(value, Settings.lengthStat)) NoteProblem("bleed.length_stat", "0 to 32767");
        } else if (key == "state_id") {
            if (!ParseTableId(value, Settings.stateId)) NoteProblem("bleed.state_id", "0 to 32767");
        }
    } else if (section == "tooltip") {
        if (key == "enabled") {
            if (!ParseBool(value, Settings.tooltipEnabled)) NoteProblem("tooltip.enabled", "true or false");
        } else if (key == "string_key") {
            if (!ParseStringKey(value, Settings.stringKey)) {
                NoteProblem("tooltip.string_key", "a quoted key of 1 to 128 printable characters");
            }
        }
    }
}

void ParseConfig(std::string_view text) noexcept {
    std::string_view section;
    std::size_t cursor = 0;
    while (cursor < text.size()) {
        const std::size_t breakAt = text.find('\n', cursor);
        const std::size_t end = breakAt == std::string_view::npos ? text.size() : breakAt;
        std::string_view line = Trim(StripComment(text.substr(cursor, end - cursor)));
        cursor = end + 1;
        if (line.empty()) continue;
        if (line.front() == '[') {
            if (line.back() == ']') section = Trim(line.substr(1, line.size() - 2));
            continue;
        }
        const std::size_t equals = line.find('=');
        if (equals == std::string_view::npos) continue;
        ApplyConfigLine(section, Trim(line.substr(0, equals)), Trim(line.substr(equals + 1)));
    }
}

void ReadConfiguration() noexcept {
    if (!Context->EnsureConfig(DefaultConfigToml)) {
        Context->LogWarn("Bleed: config file could not be created; using defaults.");
    } else {
        std::string buffer(MaximumConfigBytes, '\0');
        if (Context->ReadConfig(buffer.data(), static_cast<std::uint32_t>(buffer.size()), nullptr)) {
            buffer.resize(std::strlen(buffer.c_str()));
            ParseConfig(buffer);
        } else {
            Context->LogWarn("Bleed: config file could not be read; using defaults.");
        }
    }
    if (!ConfigProblems.empty()) {
        D2RL::LogWarnF(Context, "Bleed: %s", ConfigProblems.c_str());
    }
}

// ---------------------------------------------------------------------------
// Memory helpers
// ---------------------------------------------------------------------------

auto ReadPointer(void* base, std::size_t offset) noexcept -> void* {
    void* value = nullptr;
    std::memcpy(&value, static_cast<const std::uint8_t*>(base) + offset, sizeof(value));
    return value;
}

auto ReadInt32(void* base, std::size_t offset) noexcept -> std::int32_t {
    std::int32_t value = 0;
    std::memcpy(&value, static_cast<const std::uint8_t*>(base) + offset, sizeof(value));
    return value;
}

auto AsTableId(std::int32_t id) noexcept -> std::uint64_t {
    return static_cast<std::uint64_t>(static_cast<std::uint32_t>(id));
}

// ---------------------------------------------------------------------------
// Bleed arithmetic, the 2.4 cave from 386F67 to 386FFD
// ---------------------------------------------------------------------------

struct BleedRoll {
    bool          seedAdvanced;
    std::uint32_t seedLow;
    bool          apply;
    std::int32_t  perFrame;
    std::int32_t  averageLength;
};

// Preconditions, checked by the caller as the cave did before this point:
// physical > 0, minimum > 0, length > 0.
auto ComputeBleedRoll(std::int32_t physical, std::int32_t minimum, std::int32_t maximum,
        std::int32_t length, std::int32_t sources, std::uint32_t seedLow,
        std::uint32_t seedHigh) noexcept -> BleedRoll {
    BleedRoll result{};

    // mov ecx,[N] / test ecx,ecx / jg / mov ecx,1 / cdq / idiv ecx
    const std::int32_t divisorSources = sources > 0 ? sources : 1;
    const std::int32_t averageLength = length / divisorSources;
    result.averageLength = averageLength;
    if (averageLength <= 0) return result;

    // imul eax,eax,6AC690C5h / add eax,[seed+4] / mov [seed],eax
    const std::uint32_t next = seedLow * 0x6AC690C5U + seedHigh;
    result.seedAdvanced = true;
    result.seedLow = next;

    // sub ecx / xor edx,edx / inc ecx / jg divide: taken exactly when
    // max - min, as a signed 32-bit value, is not negative.
    const std::uint32_t spread = static_cast<std::uint32_t>(maximum) - static_cast<std::uint32_t>(minimum);
    std::uint32_t roll = static_cast<std::uint32_t>(minimum);
    if (static_cast<std::int32_t>(spread) >= 0) {
        roll += next % (spread + 1U);
    }

    // movsxd rcx,eax / movsxd rbx,[physical] / imul rcx,rbx
    const std::int64_t product = static_cast<std::int64_t>(static_cast<std::int32_t>(roll))
        * static_cast<std::int64_t>(physical);

    // imul eax,eax,64h / movsxd r8,eax
    const std::int64_t divisor = static_cast<std::int32_t>(static_cast<std::uint32_t>(averageLength) * 100U);

    // cqo / idiv r8. The cave has no zero check; averageLength * 100 wraps to
    // zero only for a length of 2^30 frames, and that stays a skip here
    // instead of a divide fault.
    if (divisor == 0) return result;
    std::int64_t perFrame = product / divisor;

    // cmp rdx,7FFFFFFFh / cmovg / cmp rdx,-80000000h / cmovl / test edx,edx
    if (perFrame > INT32_MAX) perFrame = INT32_MAX;
    if (perFrame < INT32_MIN) perFrame = INT32_MIN;
    result.perFrame = static_cast<std::int32_t>(perFrame);
    result.apply = result.perFrame > 0;
    return result;
}

// ---------------------------------------------------------------------------
// Bleed on hit
// ---------------------------------------------------------------------------

void ApplyBleed(void* attacker, void* defender, void* damage) noexcept {
    if (damage == nullptr) return;
    const std::int32_t physical = ReadInt32(damage, DamagePhysicalOffset);
    if (physical <= 0 || attacker == nullptr) return;

    void* const statList = ReadPointer(attacker, UnitStatListOffset);
    if (statList == nullptr) return;

    const std::uint64_t context = GetDataContext(attacker);

    const std::int32_t minimum = ReadEffectiveStat(context, statList, AsTableId(Settings.minimumStat), 0);
    if (minimum <= 0) return;
    const std::int32_t maximum = ReadEffectiveStat(context, statList, AsTableId(Settings.maximumStat), 0);
    const std::int32_t length = ReadEffectiveStat(context, statList, AsTableId(Settings.lengthStat), 0);
    if (length <= 0) return;

    std::int32_t sources = 0;
    for (void* child = ReadPointer(statList, StatListFirstChildOffset); child != nullptr;
            child = ReadPointer(child, StatListNextChildOffset)) {
        if (ReadEffectiveStat(context, child, AsTableId(Settings.lengthStat), 0) > 0) ++sources;
    }

    std::uint32_t* const seed = GetUnitSeed(attacker);
    std::uint32_t seedPair[2]{};
    std::memcpy(seedPair, seed, sizeof(seedPair));

    const BleedRoll roll = ComputeBleedRoll(physical, minimum, maximum, length, sources,
        seedPair[0], seedPair[1]);
    if (roll.seedAdvanced) {
        std::memcpy(seed, &roll.seedLow, sizeof(roll.seedLow));
    }
    if (!roll.apply) return;

    ApplyArgs args{};
    args.source   = attacker;
    args.target   = defender;
    args.skill    = 0;
    args.level    = roll.perFrame;
    args.duration = roll.averageLength;
    args.statId   = HitpointRegenStatId;
    args.value    = -roll.perFrame;
    args.stateId  = Settings.stateId;
    args.callback = nullptr;

    if (ApplyTimedState(&args) != nullptr) {
        BleedsApplied.fetch_add(1, std::memory_order_relaxed);
    } else {
        BleedsRefused.fetch_add(1, std::memory_order_relaxed);
    }
}

// Reached from the near relay in place of the event 16 call right after the
// life total is summed. The arguments are the call's own.
std::int32_t __fastcall HookedLifeTotalEvent(void* game, std::uint64_t eventId,
        void* defender, void* attacker, void* damage) noexcept {
    ApplyBleed(attacker, defender, damage);
    return OriginalUnitStatEvent(game, eventId, defender, attacker, damage);
}

// ---------------------------------------------------------------------------
// Tooltip
// ---------------------------------------------------------------------------

struct TooltipValues {
    const std::int32_t* accumulator;
    std::int32_t        minimum;
    std::int32_t        maximum;
    std::int32_t        length;
    bool                composed;
};

constexpr std::size_t TooltipCacheSize = 8;

thread_local std::array<TooltipValues, TooltipCacheSize> TooltipCache{};
thread_local std::size_t TooltipCacheNext = 0;

auto FindTooltipValues(const std::int32_t* accumulator) noexcept -> const TooltipValues* {
    for (const TooltipValues& entry : TooltipCache) {
        if (entry.accumulator == accumulator) return &entry;
    }
    return nullptr;
}

void StoreTooltipValues(const TooltipValues& values) noexcept {
    for (TooltipValues& entry : TooltipCache) {
        if (entry.accumulator == values.accumulator) {
            entry = values;
            return;
        }
    }
    TooltipCache[TooltipCacheNext] = values;
    TooltipCacheNext = (TooltipCacheNext + 1) % TooltipCacheSize;
}

std::uint64_t __fastcall HookedAccumulate(void* unit, void* statList,
        std::int32_t* accumulator) noexcept {
    const std::uint64_t result = OriginalAccumulate(unit, statList, accumulator);
    if (accumulator == nullptr) return result;

    TooltipValues values{};
    values.accumulator = accumulator;
    if (unit != nullptr) {
        const std::uint64_t context = GetDataContext(unit);
        values.minimum = ReadListStat(context, statList, AsTableId(Settings.minimumStat), 0);
        values.maximum = ReadListStat(context, statList, AsTableId(Settings.maximumStat), 0);
        values.length  = ReadListStat(context, statList, AsTableId(Settings.lengthStat), 0);
        values.composed = values.minimum > 0 && values.maximum > 0;
    }
    StoreTooltipValues(values);
    return result;
}

auto Resolve(const char* key, std::size_t length) noexcept -> const char* {
    const StringKey request{ key, static_cast<std::uint64_t>(length) };
    const char* const text = ResolveStringKey(&request);
    return text != nullptr ? text : "";
}

// ---------------------------------------------------------------------------
// The bleed line
// ---------------------------------------------------------------------------
//   The localized text is filled one placeholder at a time. Placeholder n
//   takes value n: 1 the min percent, 2 the max percent, 3 the seconds. A
//   placeholder is numbered (%3$.2f, as a language may write the text to
//   reorder the values) or takes the next number in order (%.2f). The CRT's
//   snprintf has no numbered placeholders, so it only ever sees one rebuilt
//   placeholder with an argument of the matching type.
//     %d %i %u %o %x %X   whole numbers, the seconds as length / 25 rounded
//                         down
//     %f %F %e %E %g %G   the exact value without the trailing zeros of its
//                         decimals: 3.20 shows as 3.2, 10.00 as 10
//     %%                  a percent sign
//   Any other placeholder (another number, another conversion) is copied as
//   written, and a % that starts no placeholder (a * width) stays a %.

constexpr int         BleedLineValues      = 3;
constexpr std::size_t PlaceholderSpecBytes = 32;
constexpr std::size_t FormattedValueBytes  = 64;

// Appends to a terminated buffer, cutting whatever does not fit.
struct LineBuilder {
    char*       text;
    std::size_t capacity;
    std::size_t used;

    void Append(const char* piece, std::size_t size) noexcept {
        if (used + 1 >= capacity) return;
        const std::size_t room = capacity - 1 - used;
        const std::size_t count = size < room ? size : room;
        std::memcpy(text + used, piece, count);
        used += count;
        text[used] = '\0';
    }
};

auto IsDigit(char character) noexcept -> bool {
    return character >= '0' && character <= '9';
}

auto IsLetter(char character) noexcept -> bool {
    return (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z');
}

// Drops the trailing zeros of a formatted number's decimals, and the decimal
// point with them when no decimal is left: 3.20 -> 3.2, 10.00 -> 10,
// 1.50e+01 -> 1.5e+01. Anything else stays as it is.
void DropTrailingZeros(char* number) noexcept {
    char* cursor = number;
    while (*cursor == ' ' || *cursor == '+' || *cursor == '-') ++cursor;
    const char* const integerDigits = cursor;
    while (IsDigit(*cursor)) ++cursor;
    if (cursor == integerDigits || (*cursor != '.' && *cursor != ',')) return;
    char* const point = cursor++;
    while (IsDigit(*cursor)) ++cursor;
    char* kept = cursor;
    while (kept > point + 1 && kept[-1] == '0') --kept;
    if (kept == point + 1) kept = point;
    std::memmove(kept, cursor, std::strlen(cursor) + 1);
}

void FormatBleedLine(char* line, std::size_t capacity, const char* format,
        const TooltipValues& values) noexcept {
    const std::int32_t whole[BleedLineValues]{
        values.minimum, values.maximum, values.length / FramesPerSecond };
    const double exact[BleedLineValues]{
        static_cast<double>(values.minimum), static_cast<double>(values.maximum),
        static_cast<double>(values.length) / FramesPerSecond };

    LineBuilder out{ line, capacity, 0 };
    if (capacity != 0) line[0] = '\0';
    int inOrder = 0;
    const char* cursor = format;
    while (*cursor != '\0') {
        if (*cursor != '%') {
            const char* const literal = cursor;
            while (*cursor != '\0' && *cursor != '%') ++cursor;
            out.Append(literal, static_cast<std::size_t>(cursor - literal));
            continue;
        }
        const char* const start = cursor++;

        // %n$: the placeholder's own number.
        int number = 0;
        const char* scan = cursor;
        while (IsDigit(*scan)) {
            if (number < 1'000) number = number * 10 + (*scan - '0');
            ++scan;
        }
        const bool numbered = scan != cursor && *scan == '$';
        if (numbered) cursor = scan + 1;

        // Flags, width and precision are kept for snprintf. A length modifier
        // is dropped: the value is always passed as an int or a double.
        char spec[PlaceholderSpecBytes]{};
        std::size_t specLength = 0;
        bool fits = true;
        const auto keep = [&](char character) noexcept {
            if (specLength + 2 < sizeof(spec)) {
                spec[specLength++] = character;
            } else {
                fits = false;
            }
        };
        keep('%');
        while (*cursor != '\0' && std::strchr("-+ #0", *cursor) != nullptr) keep(*cursor++);
        while (IsDigit(*cursor)) keep(*cursor++);
        if (*cursor == '.') {
            keep(*cursor++);
            while (IsDigit(*cursor)) keep(*cursor++);
        }
        while (*cursor != '\0' && std::strchr("hlLjztw", *cursor) != nullptr) ++cursor;
        if (*cursor == 'I') {
            ++cursor;
            if ((cursor[0] == '6' && cursor[1] == '4') || (cursor[0] == '3' && cursor[1] == '2')) {
                cursor += 2;
            }
        }

        const char conversion = *cursor;
        if (conversion != '%' && !IsLetter(conversion)) {
            out.Append(start, 1);
            cursor = start + 1;
            continue;
        }
        ++cursor;
        if (conversion == '%') {
            out.Append("%", 1);
            continue;
        }

        const int index = numbered ? number : ++inOrder;
        const bool integer  = std::strchr("diuoxX", conversion) != nullptr;
        const bool floating = std::strchr("fFeEgG", conversion) != nullptr;
        if (!fits || index < 1 || index > BleedLineValues || (!integer && !floating)) {
            out.Append(start, static_cast<std::size_t>(cursor - start));
            continue;
        }

        spec[specLength++] = conversion;
        spec[specLength] = '\0';
        char value[FormattedValueBytes]{};
        if (floating) {
            std::snprintf(value, sizeof(value), spec, exact[index - 1]);
            DropTrailingZeros(value);
        } else if (conversion == 'd' || conversion == 'i') {
            std::snprintf(value, sizeof(value), spec, whole[index - 1]);
        } else {
            std::snprintf(value, sizeof(value), spec, static_cast<unsigned int>(whole[index - 1]));
        }
        out.Append(value, std::strlen(value));
    }
}

std::uint64_t __fastcall HookedCompose(std::int32_t* accumulator, std::uint64_t statIdArgument,
        char* output, std::uint64_t capacity, std::uint64_t chronicle) noexcept {
    const auto statId = static_cast<std::int32_t>(static_cast<std::uint32_t>(statIdArgument));

    if (static_cast<std::uint32_t>(statId) - static_cast<std::uint32_t>(FirstNativeComposerStat)
            <= static_cast<std::uint32_t>(LastNativeComposerStat - FirstNativeComposerStat)) {
        return OriginalCompose(accumulator, statIdArgument, output, capacity, chronicle);
    }

    const bool isMinimum = statId == Settings.minimumStat;
    const bool isPartner = !isMinimum
        && (statId == Settings.maximumStat || statId == Settings.lengthStat);
    if (!isMinimum && !isPartner) {
        return OriginalCompose(accumulator, statIdArgument, output, capacity, chronicle);
    }

    const TooltipValues* const values = FindTooltipValues(accumulator);
    const bool composed = values != nullptr && values->composed;
    if (isPartner) return composed ? 1 : 0;
    if (!composed) return 0;

    char line[TooltipBufferBytes]{};
    FormatBleedLine(line, sizeof(line),
        Resolve(Settings.stringKey.c_str(), Settings.stringKey.size()), *values);

    if ((chronicle & 0xFF) != 0) {
        AppendText(output, capacity, Resolve(BulletKey, sizeof(BulletKey) - 1));
    }
    AppendText(output, capacity, line);
    AppendText(output, capacity, Resolve(NewlineKey, sizeof(NewlineKey) - 1));

    TooltipLines.fetch_add(1, std::memory_order_relaxed);
    return 1;
}

// ---------------------------------------------------------------------------
// Verification
// ---------------------------------------------------------------------------

auto Verify(std::uint64_t rva, const std::uint8_t* expected, std::size_t size,
        const char* label) noexcept -> bool {
    if (Context->CheckExpectedBytes(rva, expected, static_cast<std::uint32_t>(size))) return true;
    D2RL::LogErrorF(Context,
        "Bleed: %s at 0x%llX does not match the verified D2R image, or another plugin "
        "already owns it.",
        label, static_cast<unsigned long long>(rva));
    return false;
}

auto VerifyThunk(std::uint64_t rva, const std::uint8_t* body, std::size_t bodySize,
        std::uint64_t bodyOffset, bool padded, const char* label) noexcept -> bool {
    if (!Verify(rva, ThunkJumpWitness, sizeof(ThunkJumpWitness), label)) {
        return false;
    }
    if (padded && !Verify(rva + ThunkPaddingOffset, ThunkPaddingWitness,
            sizeof(ThunkPaddingWitness), label)) {
        return false;
    }
    return Verify(rva + bodyOffset, body, bodySize, label);
}

auto VerifySharedContract() noexcept -> bool {
    return Verify(GetDataContextRva, GetDataContextBodyWitness,
                sizeof(GetDataContextBodyWitness), "the unit data context getter");
}

auto VerifyDamageContract() noexcept -> bool {
    return VerifyThunk(ApplyTimedStateRva, ApplyTimedStateBodyWitness,
                sizeof(ApplyTimedStateBodyWitness), ApplyTimedStateBodyOffset, false,
                "the timed state thunk")
        && VerifyThunk(ReadEffectiveStatRva, ReadEffectiveStatBodyWitness,
                sizeof(ReadEffectiveStatBodyWitness), ThunkBodyOffset, true,
                "the effective stat reader thunk")
        && Verify(GetUnitSeedRva, GetUnitSeedBodyWitness, sizeof(GetUnitSeedBodyWitness),
                "the unit seed getter")
        && Verify(GetUnitStatListRva, GetUnitStatListBodyWitness,
                sizeof(GetUnitStatListBodyWitness), "the unit stat list getter")
        && Verify(StatListChainRva, StatListChainWitness, sizeof(StatListChainWitness),
                "the stat list child chain")
        && Verify(DamageRegistersRva, DamageRegistersWitness, sizeof(DamageRegistersWitness),
                "the damage calculation registers")
        && Verify(DamagePhysicalRecordRva, DamagePhysicalRecordWitness,
                sizeof(DamagePhysicalRecordWitness), "the physical damage record")
        && Verify(LifeTotalEventCallRva, LifeTotalEventCallWitness,
                sizeof(LifeTotalEventCallWitness), "the life total event call");
}

auto IsReadable(std::uintptr_t address, std::size_t size) noexcept -> bool {
    MEMORY_BASIC_INFORMATION info{};
    if (VirtualQuery(reinterpret_cast<void*>(address), &info, sizeof(info)) != sizeof(info)) {
        return false;
    }
    constexpr DWORD Readable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY
        | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    if (info.State != MEM_COMMIT || (info.Protect & Readable) == 0
            || (info.Protect & PAGE_GUARD) != 0) {
        return false;
    }
    const std::uintptr_t regionEnd =
        reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize;
    return address + size <= regionEnd;
}

// Follows the E8 at rva (inside a pinned witness) and requires the target to
// be a loader thunk. The resolver thunk sits in D2RLoader's own section past
// the retail image, so it is read directly rather than through the loader's
// byte check.
auto ResolveCallTarget(std::uint64_t rva, std::uintptr_t& target) noexcept -> bool {
    std::int32_t displacement = 0;
    std::memcpy(&displacement, reinterpret_cast<const std::uint8_t*>(Base + rva + 1),
        sizeof(displacement));
    target = static_cast<std::uintptr_t>(static_cast<std::int64_t>(Base + rva + CallSize)
        + displacement);
    if (!IsReadable(target, sizeof(ThunkJumpWitness))) return false;
    return std::memcmp(reinterpret_cast<const void*>(target), ThunkJumpWitness,
        sizeof(ThunkJumpWitness)) == 0;
}

auto VerifyTooltipContract() noexcept -> bool {
    if (!(VerifyThunk(ReadListStatRva, ReadListStatBodyWitness, sizeof(ReadListStatBodyWitness),
                ThunkBodyOffset, true, "the list stat reader thunk")
            && Verify(AccumulatorRva, AccumulatorPrologueWitness,
                sizeof(AccumulatorPrologueWitness), "the tooltip accumulator entry")
            && Verify(AccumulatorSetupRva, AccumulatorSetupWitness,
                sizeof(AccumulatorSetupWitness), "the tooltip accumulator setup")
            && Verify(AccumulatorPoisonLengthRva, AccumulatorPoisonLengthWitness,
                sizeof(AccumulatorPoisonLengthWitness), "the tooltip accumulator stat read")
            && Verify(ComposerRva, ComposerPrologueWitness, sizeof(ComposerPrologueWitness),
                "the tooltip composer entry")
            && Verify(ComposerHeadRva, ComposerHeadWitness, sizeof(ComposerHeadWitness),
                "the tooltip composer switch")
            && Verify(ComposerTailBulletRva, ComposerTailBulletWitness,
                sizeof(ComposerTailBulletWitness), "the tooltip composer bullet")
            && Verify(ComposerTailLineRva, ComposerTailLineWitness,
                sizeof(ComposerTailLineWitness), "the tooltip composer line append")
            && Verify(ComposerTailReturnRva, ComposerTailReturnWitness,
                sizeof(ComposerTailReturnWitness), "the tooltip composer newline")
            && Verify(AppendTextRva, AppendTextPrologueWitness,
                sizeof(AppendTextPrologueWitness), "the tooltip text append"))) {
        return false;
    }

    std::uintptr_t bulletResolver = 0;
    std::uintptr_t newlineResolver = 0;
    if (!ResolveCallTarget(ComposerBulletResolveCallRva, bulletResolver)
            || !ResolveCallTarget(ComposerNewlineResolveCallRva, newlineResolver)
            || bulletResolver != newlineResolver) {
        Context->LogError("Bleed: the tooltip composer's string key resolver could not be located.");
        return false;
    }
    ResolveStringKey = reinterpret_cast<ResolveStringKeyFn>(newlineResolver);
    return true;
}

// ---------------------------------------------------------------------------
// Relay page and the life total call
// ---------------------------------------------------------------------------

constexpr std::size_t RelayPageBytes = 4'096;

// +00  FF 25 00 00 00 00        jmp  qword ptr [rip+0]
// +06  dq                       HookedLifeTotalEvent, or 44D570 after unload
constexpr std::uint8_t RelayStub[]{
    0xFF, 0x25, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00,
};
constexpr std::size_t RelayTargetSlot = 6;

static_assert(sizeof(RelayStub) == 14);

auto CanEncodeRel32(std::uintptr_t from, std::uintptr_t to) noexcept -> bool {
    const std::int64_t delta =
        static_cast<std::int64_t>(to) - static_cast<std::int64_t>(from) - CallSize;
    return delta >= INT32_MIN && delta <= INT32_MAX;
}

auto AllocateNear(std::uintptr_t hint, std::size_t size) noexcept -> void* {
    SYSTEM_INFO systemInfo{};
    GetSystemInfo(&systemInfo);
    const auto granularity = static_cast<std::uintptr_t>(systemInfo.dwAllocationGranularity);
    const auto aligned = hint & ~(granularity - 1U);
    for (std::uintptr_t delta = granularity; delta < 0x7000'0000ULL; delta += granularity) {
        const auto candidate = aligned + delta;
        if (!CanEncodeRel32(hint, candidate + size)) break;
        if (auto* memory = VirtualAlloc(reinterpret_cast<void*>(candidate), size,
                MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)) {
            return memory;
        }
    }
    return nullptr;
}

// The page stays executable while the slot is rewritten: on unload a thread
// may be running the jump.
auto WriteRelayTarget(std::uintptr_t target) noexcept -> bool {
    auto* const page = static_cast<std::uint8_t*>(RelayPage);
    DWORD previous = 0;
    if (!VirtualProtect(page, RelayPageBytes, PAGE_EXECUTE_READWRITE, &previous)) return false;
    const std::uint64_t value = target;
    std::memcpy(page + RelayTargetSlot, &value, sizeof(value));
    DWORD ignored = 0;
    const bool sealed = VirtualProtect(page, RelayPageBytes, PAGE_EXECUTE_READ, &ignored) != FALSE;
    FlushInstructionCache(GetCurrentProcess(), page, RelayPageBytes);
    return sealed;
}

void EncodeCall(std::uintptr_t site, std::uintptr_t target, std::uint8_t* out) noexcept {
    out[0] = 0xE8;
    const auto displacement = static_cast<std::int32_t>(
        static_cast<std::int64_t>(target) - static_cast<std::int64_t>(site + CallSize));
    std::memcpy(out + 1, &displacement, sizeof(displacement));
}

auto InstallDamage() noexcept -> bool {
    const std::uintptr_t site = Base + LifeTotalEventCallRva;

    std::int32_t displacement = 0;
    std::memcpy(&displacement, LifeTotalEventCallWitness + 1, sizeof(displacement));
    const auto nativeTarget = static_cast<std::uintptr_t>(
        static_cast<std::int64_t>(site + CallSize) + displacement);
    if (nativeTarget != Base + ApplyUnitStatEventRva) {
        Context->LogError("Bleed: the life total event call does not reach 0x44D570.");
        return false;
    }
    OriginalUnitStatEvent = reinterpret_cast<UnitStatEventFn>(nativeTarget);

    RelayPage = AllocateNear(site, RelayPageBytes);
    if (RelayPage == nullptr) {
        Context->LogError("Bleed: no relay page was available within rel32 reach.");
        return false;
    }
    std::memset(RelayPage, 0xCC, RelayPageBytes);
    std::memcpy(RelayPage, RelayStub, sizeof(RelayStub));
    if (!WriteRelayTarget(reinterpret_cast<std::uintptr_t>(&HookedLifeTotalEvent))) {
        Context->LogError("Bleed: the relay page protection could not be set.");
        VirtualFree(RelayPage, 0, MEM_RELEASE);
        RelayPage = nullptr;
        return false;
    }

    std::uint8_t call[CallSize]{};
    EncodeCall(site, reinterpret_cast<std::uintptr_t>(RelayPage), call);
    if (!Context->PatchBytes(LifeTotalEventCallRva, LifeTotalEventCallWitness, CallSize,
            call, CallSize)) {
        Context->LogError("Bleed: the life total event call could not be redirected.");
        VirtualFree(RelayPage, 0, MEM_RELEASE);
        RelayPage = nullptr;
        return false;
    }
    LifeTotalCallPatched = true;
    return true;
}

// Points the relay back at the native event call, then restores the call.
// The page itself is kept: a thread may be inside it right now.
void RemoveDamage() noexcept {
    if (RelayPage == nullptr) return;
    WriteRelayTarget(Base + ApplyUnitStatEventRva);
    if (!LifeTotalCallPatched) return;
    std::uint8_t current[CallSize]{};
    EncodeCall(Base + LifeTotalEventCallRva, reinterpret_cast<std::uintptr_t>(RelayPage), current);
    if (Context->PatchBytes(LifeTotalEventCallRva, current, CallSize,
            LifeTotalEventCallWitness, CallSize)) {
        LifeTotalCallPatched = false;
    }
}

// The accumulator hook alone changes nothing visible, so a composer failure
// after it is reported as partially armed rather than unwound.
auto InstallTooltip() noexcept -> PartState {
    if (!Context->InstallInlineHook(AccumulatorRva, AccumulatorPrologueWitness,
            AccumulatorHookSize, &HookedAccumulate, &OriginalAccumulate)
            || OriginalAccumulate == nullptr) {
        Context->LogError("Bleed: the tooltip accumulator hook at 0x2DB370 could not be installed.");
        return PartState::Failed;
    }
    if (!Context->InstallInlineHook(ComposerRva, ComposerPrologueWitness, ComposerHookSize,
            &HookedCompose, &OriginalCompose)
            || OriginalCompose == nullptr) {
        Context->LogError("Bleed: the tooltip composer hook at 0x2DB800 could not be installed.");
        return PartState::PartiallyArmed;
    }
    return PartState::Armed;
}

// ---------------------------------------------------------------------------
// Console command
// ---------------------------------------------------------------------------

auto StateName(PartState state) noexcept -> const char* {
    switch (state) {
    case PartState::DisabledByConfig: return "disabled by config";
    case PartState::Armed:            return "armed";
    case PartState::PartiallyArmed:   return "PARTIALLY armed, see log";
    case PartState::Failed:           return "FAILED, see log";
    default:                          return "not loaded";
    }
}

auto __cdecl StatusCommand(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command,
        void*) noexcept -> D2RL::ConsoleCommandResult {
    if (command == nullptr || command->plugin == nullptr) return D2RL::ConsoleCommandResult::Failed;

    char message[512];
    std::snprintf(message, sizeof(message),
        "Bleed: on hit %s, stats %d/%d/%d, state %d, applied %llu, refused %llu | "
        "tooltip %s, key %s, lines %llu",
        StateName(DamageState), Settings.minimumStat, Settings.maximumStat, Settings.lengthStat,
        Settings.stateId,
        static_cast<unsigned long long>(BleedsApplied.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(BleedsRefused.load(std::memory_order_relaxed)),
        StateName(TooltipState), Settings.stringKey.c_str(),
        static_cast<unsigned long long>(TooltipLines.load(std::memory_order_relaxed)));
    command->plugin->WriteConsoleMessage(message);
    return D2RL::ConsoleCommandResult::Handled;
}

void RegisterStatusCommand() noexcept {
    if (!Context->RegisterConsoleCommand("bleed", &StatusCommand,
            "Reports Bleed status and counters.")) {
        Context->LogWarn("Bleed: the status console command was refused.");
    }
}

void WarnAboutStatChoices() noexcept {
    const auto native = [](std::int32_t id) noexcept {
        return id >= FirstNativeComposerStat && id <= LastNativeComposerStat;
    };
    if (Settings.tooltipEnabled
            && (native(Settings.minimumStat) || native(Settings.maximumStat)
                || native(Settings.lengthStat))) {
        Context->LogWarn("Bleed: a bleed stat id is in 17..59, the game's own damage lines; "
                         "the tooltip never composes a line for it.");
    }
    if (Settings.minimumStat == Settings.maximumStat || Settings.minimumStat == Settings.lengthStat
            || Settings.maximumStat == Settings.lengthStat) {
        Context->LogWarn("Bleed: two bleed stats share the same id.");
    }
}

constexpr D2RL::PluginInfo Info{
    .infoSize = D2RL::PluginInfoSize,
    .abiVersion = D2RL_PLUGIN_ABI_VERSION,
    .id = "celestialrayone.bleed",
    .name = "Bleed",
    .version = "1.0.0",
    .author = "CelestialRayOne",
    .description =
        "Physical hits make the target bleed through a timed state with negative life "
        "regeneration, and item tooltips show the bleed stats as one line.",
    .flags = D2RL::PluginFlags::Shared | D2RL::PluginFlags::NativeHooks,
};

}  // namespace

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* {
    return &Info;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
    if (!D2RL::HasContext(context)) return false;
    Context = context;
    Base = context->exeBase;

    ApplyTimedState   = reinterpret_cast<ApplyTimedStateFn>(Base + ApplyTimedStateRva);
    ReadEffectiveStat = reinterpret_cast<ReadStatFn>(Base + ReadEffectiveStatRva);
    ReadListStat      = reinterpret_cast<ReadStatFn>(Base + ReadListStatRva);
    GetDataContext    = reinterpret_cast<GetDataContextFn>(Base + GetDataContextRva);
    GetUnitSeed       = reinterpret_cast<GetUnitSeedFn>(Base + GetUnitSeedRva);
    AppendText        = reinterpret_cast<AppendTextFn>(Base + AppendTextRva);

    ReadConfiguration();
    WarnAboutStatChoices();

    const bool sharedOk = (!Settings.bleedEnabled && !Settings.tooltipEnabled) || VerifySharedContract();

    if (!Settings.bleedEnabled) {
        DamageState = PartState::DisabledByConfig;
    } else if (sharedOk && VerifyDamageContract() && InstallDamage()) {
        DamageState = PartState::Armed;
    } else {
        DamageState = PartState::Failed;
    }

    if (!Settings.tooltipEnabled) {
        TooltipState = PartState::DisabledByConfig;
    } else if (sharedOk && VerifyTooltipContract()) {
        TooltipState = InstallTooltip();
    } else {
        TooltipState = PartState::Failed;
    }

    const bool anythingLive = DamageState == PartState::Armed
        || TooltipState == PartState::Armed || TooltipState == PartState::PartiallyArmed;
    const bool anythingWanted = Settings.bleedEnabled || Settings.tooltipEnabled;

    D2RL::LogInfoF(Context,
        "Bleed: on hit %s (stats %d/%d/%d, state %d), tooltip %s (key %s).",
        StateName(DamageState), Settings.minimumStat, Settings.maximumStat, Settings.lengthStat,
        Settings.stateId, StateName(TooltipState), Settings.stringKey.c_str());

    if (anythingWanted && !anythingLive) {
        Context->LogError("Bleed: nothing could be installed. Refusing to load.");
        return false;
    }

    RegisterStatusCommand();
    return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
    if (Context == nullptr) return;
    RemoveDamage();
}

}  // namespace CelestialRayOne::Bleed
