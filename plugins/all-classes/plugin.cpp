// Skill Line Merge
//
// When an item tooltip block shows the plain line and the class-only line for
// the same skill, the two become one line:
//   +3 to Leap
//   +27 to Leap (Barbarian Only)
// shows as
//   +30 to Leap (All Classes)
// Any class that has the skill gets both bonuses, so the merged line shows the
// sum. The text comes from the string key "allclasses".
//
// Everything below was read out of the live D2RLoader 1.3.1 image of D2R 3.3
// (process dump md5 8af7a47c...) and disassembled before being relied on.
//
// ---------------------------------------------------------------------------
// The two lines
// ---------------------------------------------------------------------------
//   plain line       the row in oskill_line_stat, descfunc 28  "%+d to %s"
//   class-only line  item_singleskill, row 107, descfunc 27    "%+d to %s %s"
//   ESR shows oskills through item_nonclassskill_display (row 370): its oskill
//   property sets that row next to item_nonclassskill (97), which has no
//   description in ESR, and oskill-hidden sets 97 alone. So 370 is ESR's plain
//   line, and a hidden oskill has no line to merge. The base game shows
//   item_nonclassskill (97) itself. All of them are descpriority 81, so the
//   lines sit together, and the layer of each is the skill id.
//
// ---------------------------------------------------------------------------
// Where a stat line is written
// ---------------------------------------------------------------------------
//   D2RLoader replaced the stat list renderer with D2RCore's
//   RenderWideItemStatList. It still calls the game's single-value writer
//   2D6520 once per stat entry through its import slot. When the writer returns
//   zero, no line and no line break is added for that entry.
//
//   2D6520 (unit, itemstatcost record, value, layer, group, line, mode):
//     2D655B  mov ebx,r8d          ; value
//     2D6563  mov r13,rdx          ; itemstatcost record
//     2D6566  mov [rbp-68h],rcx    ; unit, the item being described
//     2D6576  movsxd r14,r9d       ; layer, the skill id here
//     2D6606  mov rdi,rax          ; descstrpos text, the line's format
//     2D668B..2D66AA  switch on descfunc through the 31-entry table at 2D7794
//   Nothing writes ebx, r13, r14, rdi or [rbp-68h] between there and the
//   switch. The whole stretch 2D654D..2D66AC is verified at load.
//
//   descfunc 27, 2D751F: skill name, the class "Only" text, then
//     2D75B1  format(line, 100h, rdi, ebx, skill name, class text)
//   descfunc 28, 2D7655: skill name, then
//     2D7426  format(line, 100h, rdi, ebx, skill name)   ; mov al,1 / return
//     It prints ebx as it is. The own-class cap of 3 it computes at 2D76C3
//     (r15d) only goes into a scratch buffer and never into the line.
//   2D6A6A  xor al,al, then the epilogue: the writer returns zero, no line.
//
//   Tooltip Color Scope hooks the entry of 2D6520 and the loader refuses a
//   second hook on the same bytes. This plugin changes neither the entry nor
//   any instruction of the writer.
//
// ---------------------------------------------------------------------------
// What this plugin does
// ---------------------------------------------------------------------------
//   1. The descfunc 27 and 28 entries of the writer's jump table (2D77FC and
//      2D7800) point at two small relays on a page right after the game image.
//      A relay asks the plugin about the line, then continues at the case the
//      table pointed at, or at the descfunc 28 case with the merged number and
//      the "allclasses" text, or at 2D6A6A for no line. The writer's own code
//      writes every line.
//   2. The values come from the tooltip block being drawn. 2DC4B0 builds a
//      temporary stat list per block (the item's list for that block, the
//      socketed items merged in) and hands it to the renderer, which calls the
//      tooltip accumulator 2DAED0(unit, list, ...) before any line of that
//      block. A hook on 2DAED0 notes the unit and the list for the calling
//      thread. The relays read both stats for the line's skill from that list
//      with D2RCore's ReadWideListStat (2F5DF0), the reader the accumulator
//      itself uses on this list.
//   3. A pair merges when both values are non-zero for the same skill in the
//      block, both itemstatcost rows have no valshift and an op outside 2..5
//      (so the tooltip number is the stat value), and the "allclasses" text
//      has an integer conversion followed by a string conversion. Both relays
//      apply this same test, so the two lines always agree:
//        class-only line  -> the merged line, through the descfunc 28 code
//        plain line       -> no line
//        a sum of 0       -> no line for either
//      Anything else keeps both native lines.
//
//   4. Views that show property ranges (the Chronicle, for one) are left
//      alone. There a line whose property has a range goes through the ranged
//      writer 2D6330 / 2D7850 instead of 2D6520, so one of the two lines could
//      miss the relays. The accumulator receives that range data and tests it
//      itself:
//        2DAF0D  test rsi,rsi / je   ; argument 4, the range array
//        2DAF16  cmp byte [r13+8],0  ; argument 5, the range list is engaged
//      A block with range data is not captured, so both native lines stay.
//
//   Only the unit the block was captured for is answered, so the writer's
//   other caller (2D97E0, which formats for the local player) never merges.
//
// Hook on a whole-instruction prologue:
//   2DAED0  push rbx,rsi,rdi,r13 / sub rsp,68h / mov r13,[rsp+0B0h]
// The hook and both table entries go in one loader transaction.

#include <D2RLPlugin/api.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <atomic>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

namespace CelestialRayOne::SkillLineMerge {
namespace {

// ---------------------------------------------------------------------------
// Native anchors
// ---------------------------------------------------------------------------

constexpr std::uint64_t WriterFrameRva       = 0x2D6535;
constexpr std::uint64_t WriterBodyRva        = 0x2D654D;
constexpr std::uint64_t ResolverCallRvas[]{ 0x2D663D, 0x2D665E, 0x2D667F };
constexpr std::uint64_t ClassSkillEntryRva   = 0x2D77FC;  // jump table 2D7794, descfunc 27
constexpr std::uint64_t OSkillEntryRva       = 0x2D7800;  // jump table 2D7794, descfunc 28
constexpr std::uint64_t ClassSkillCaseRva    = 0x2D751F;
constexpr std::uint64_t OSkillCaseRva        = 0x2D7655;
constexpr std::uint64_t OSkillFormatTailRva  = 0x2D7426;
constexpr std::uint64_t NoLineExitRva        = 0x2D6A6A;
constexpr std::uint64_t AccumulatorRva       = 0x2DAED0;
constexpr std::uint64_t AccumulatorSetupRva  = 0x2DAEE2;
constexpr std::uint64_t AccumulatorRangeRva  = 0x2DAF0D;
constexpr std::uint64_t GetDataTablesRva     = 0x300A90;
constexpr std::uint64_t GetDataContextRva    = 0x34A0E0;
constexpr std::uint64_t ReadListStatRva      = 0x2F5DF0;
constexpr std::uint64_t GetStatRecordRva     = 0x2D9730;

// The loader thunks keep an FF 25 disp32 jump into D2RLoader's import slot.
// The slot moves between loader builds, so only the opcode, the padding and
// the untouched remainder of the old body are pinned.
constexpr std::uint64_t ThunkPaddingOffset = 6;
constexpr std::uint64_t ThunkBodyOffset    = 10;

// ---------------------------------------------------------------------------
// Witnesses, generated from the live image
// ---------------------------------------------------------------------------

constexpr std::uint8_t ThunkJumpWitness[]{ 0xFF, 0x25 };
constexpr std::uint8_t ThunkPaddingWitness[]{ 0x90, 0x90, 0x90, 0x90 };

// 2D6535: sub rsp,0A88h, the writer's frame. Checked only; Tooltip Color
// Scope pins the same bytes.
constexpr std::uint8_t WriterFrameWitness[]{ 0x48, 0x81, 0xEC, 0x88, 0x0A, 0x00, 0x00 };

// From the register setup at 2D654D through the descfunc switch jump at 2D66AA.
constexpr std::uint8_t WriterBodyWitness[]{
    0x0F, 0xB6, 0x7A, 0x32, 0x48, 0x8B, 0xC1, 0x48, 0x8B, 0xB5, 0xF8, 0x09, 0x00, 0x00, 0x41, 0x8B,
    0xD8, 0x44, 0x0F, 0xB6, 0x7A, 0x33, 0x4C, 0x8B, 0xEA, 0x48, 0x89, 0x4D, 0x98, 0x0F, 0xB7, 0x4A,
    0x38, 0x66, 0x89, 0x4C, 0x24, 0x50, 0x48, 0x8B, 0xC8, 0x4D, 0x63, 0xF1, 0x89, 0x7C, 0x24, 0x58,
    0xE8, 0x5E, 0x3B, 0x07, 0x00, 0x8B, 0x95, 0x00, 0x0A, 0x00, 0x00, 0x88, 0x44, 0x24, 0x52, 0x83,
    0xEA, 0x02, 0x74, 0x0F, 0x83, 0xFA, 0x01, 0x75, 0x12, 0xC7, 0x44, 0x24, 0x58, 0x1F, 0x00, 0x00,
    0x00, 0xEB, 0x08, 0xC7, 0x44, 0x24, 0x58, 0x1E, 0x00, 0x00, 0x00, 0x80, 0xBD, 0xF0, 0x09, 0x00,
    0x00, 0x00, 0x74, 0x2A, 0x41, 0x0F, 0xB6, 0x45, 0x3C, 0x45, 0x0F, 0xB6, 0x7D, 0x3D, 0x89, 0x44,
    0x24, 0x58, 0x41, 0x0F, 0xB7, 0x45, 0x42, 0x66, 0x89, 0x44, 0x24, 0x50, 0x85, 0xDB, 0x78, 0x07,
    0x41, 0x0F, 0xB7, 0x4D, 0x3E, 0xEB, 0x17, 0x41, 0x0F, 0xB7, 0x4D, 0x40, 0xEB, 0x10, 0x85, 0xDB,
    0x78, 0x07, 0x41, 0x0F, 0xB7, 0x4D, 0x34, 0xEB, 0x05, 0x41, 0x0F, 0xB7, 0x4D, 0x36, 0xE8, 0x40,
    0x51, 0xB5, 0x03, 0x44, 0x8B, 0xCB, 0x4C, 0x8D, 0x05, 0xD6, 0xC8, 0x9E, 0x01, 0xBA, 0x0A, 0x00,
    0x00, 0x00, 0x48, 0x8D, 0x8D, 0x60, 0x01, 0x00, 0x00, 0x48, 0x8B, 0xF8, 0xE8, 0x42, 0x7A, 0xF6,
    0x00, 0x4C, 0x8D, 0x85, 0x60, 0x01, 0x00, 0x00, 0xBA, 0x0A, 0x00, 0x00, 0x00, 0x48, 0x8D, 0x8D,
    0x50, 0x01, 0x00, 0x00, 0xE8, 0x1A, 0x12, 0xF5, 0x00, 0x48, 0x8D, 0x05, 0xA3, 0x91, 0xA1, 0x01,
    0x48, 0xC7, 0x45, 0xD0, 0x07, 0x00, 0x00, 0x00, 0x48, 0x8D, 0x4D, 0xC8, 0x48, 0x89, 0x45, 0xC8,
    0xE8, 0xF4, 0x50, 0xB5, 0x03, 0x48, 0x89, 0x44, 0x24, 0x70, 0x48, 0x8D, 0x4D, 0xA8, 0x48, 0x8D,
    0x05, 0xD6, 0xC4, 0x9E, 0x01, 0x48, 0xC7, 0x45, 0xB0, 0x05, 0x00, 0x00, 0x00, 0x48, 0x89, 0x45,
    0xA8, 0xE8, 0xD3, 0x50, 0xB5, 0x03, 0x48, 0x89, 0x44, 0x24, 0x60, 0x48, 0x8D, 0x4D, 0xB8, 0x48,
    0x8D, 0x05, 0xB5, 0xF1, 0xA1, 0x01, 0x48, 0xC7, 0x45, 0xC0, 0x04, 0x00, 0x00, 0x00, 0x48, 0x89,
    0x45, 0xB8, 0xE8, 0xB2, 0x50, 0xB5, 0x03, 0x8B, 0x4C, 0x24, 0x58, 0x4C, 0x8B, 0xE0, 0xFF, 0xC9,
    0x83, 0xF9, 0x1E, 0x0F, 0x87, 0xD4, 0x03, 0x00, 0x00, 0x48, 0x8D, 0x05, 0x63, 0x99, 0xD2, 0xFF,
    0x48, 0x63, 0xC9, 0x8B, 0x94, 0x88, 0x94, 0x77, 0x2D, 0x00, 0x48, 0x03, 0xD0, 0xFF, 0xE2,
};

// 2D751F: the descfunc 27 case, test ebx,ebx .. call (skill name id).
constexpr std::uint8_t ClassSkillCaseWitness[]{
    0x85, 0xDB, 0x0F, 0x84, 0x1C, 0xF2, 0xFF, 0xFF, 0x44, 0x0F, 0xB6, 0x64, 0x24, 0x52, 0x45, 0x33,
    0xC0, 0x41, 0x0F, 0xB6, 0xCC, 0x41, 0x8B, 0xD6, 0xE8, 0xB4, 0x47, 0xFB, 0xFF,
};

// 2D7655: the whole descfunc 28 case up to its jump into the format tail.
constexpr std::uint8_t OSkillCaseWitness[]{
    0x44, 0x8B, 0xFB, 0x85, 0xDB, 0x0F, 0x84, 0xE3, 0xF0, 0xFF, 0xFF, 0x44, 0x0F, 0xB6, 0x6C, 0x24,
    0x52, 0x41, 0x8B, 0xD6, 0x41, 0x0F, 0xB6, 0xCD, 0xE8, 0x1E, 0x01, 0xDC, 0xFF, 0x4C, 0x8B, 0xE0,
    0x48, 0x85, 0xC0, 0x0F, 0x84, 0xC5, 0xF0, 0xFF, 0xFF, 0x48, 0x8B, 0x4D, 0x98, 0xE8, 0x49, 0x43,
    0x07, 0x00, 0x85, 0xC0, 0x74, 0x0E, 0xE8, 0x40, 0x3C, 0xDB, 0xFF, 0x8B, 0xC8, 0xE8, 0xE9, 0x2D,
    0xDC, 0xFF, 0xEB, 0x04, 0x48, 0x8B, 0x45, 0x98, 0x41, 0xB8, 0x2B, 0x08, 0x00, 0x00, 0x48, 0x8D,
    0x15, 0x36, 0xE1, 0xA1, 0x01, 0x48, 0x8B, 0xC8, 0xE8, 0xAE, 0x21, 0x07, 0x00, 0x41, 0x0F, 0xBE,
    0x4C, 0x24, 0x2C, 0x3B, 0xC8, 0x75, 0x0B, 0xB8, 0x03, 0x00, 0x00, 0x00, 0x3B, 0xD8, 0x44, 0x0F,
    0x4F, 0xF8, 0x45, 0x33, 0xC0, 0x41, 0x8B, 0xD6, 0x41, 0x0F, 0xB6, 0xCD, 0xE8, 0x1A, 0x46, 0xFB,
    0xFF, 0x0F, 0xB7, 0xC8, 0xE8, 0x52, 0x40, 0xB5, 0x03, 0x4C, 0x8B, 0xF0, 0x48, 0x85, 0xC0, 0x0F,
    0x84, 0x59, 0xF0, 0xFF, 0xFF, 0x45, 0x8B, 0xCF, 0x4C, 0x8D, 0x05, 0xDC, 0xB7, 0x9E, 0x01, 0xBA,
    0x0A, 0x00, 0x00, 0x00, 0x48, 0x8D, 0x8D, 0x60, 0x01, 0x00, 0x00, 0xE8, 0x4B, 0x69, 0xF6, 0x00,
    0x4C, 0x8D, 0x85, 0x60, 0x01, 0x00, 0x00, 0xBA, 0x0A, 0x00, 0x00, 0x00, 0x48, 0x8D, 0x8D, 0x50,
    0x01, 0x00, 0x00, 0xE8, 0x23, 0x01, 0xF5, 0x00, 0x4C, 0x89, 0x74, 0x24, 0x20, 0xE9, 0xFF, 0xFC,
    0xFF, 0xFF,
};

// 2D7426: format(line, 100h, rdi, ebx, skill name) / mov al,1 / jmp epilogue.
constexpr std::uint8_t OSkillFormatTailWitness[]{
    0x44, 0x8B, 0xCB, 0x4C, 0x8B, 0xC7, 0xBA, 0x00, 0x01, 0x00, 0x00, 0x48, 0x8B, 0xCE, 0xE8, 0x77,
    0x9C, 0x5B, 0x00, 0xB0, 0x01, 0xE9, 0x2C, 0xF6, 0xFF, 0xFF,
};

// 2D6A6A: xor al,al and the writer's epilogue.
constexpr std::uint8_t NoLineExitWitness[]{
    0x32, 0xC0, 0x48, 0x8B, 0x8D, 0x70, 0x09, 0x00, 0x00, 0x48, 0x33, 0xCC, 0xE8, 0xD5, 0xA6, 0xFF,
    0x00, 0x48, 0x81, 0xC4, 0x88, 0x0A, 0x00, 0x00, 0x41, 0x5F, 0x41, 0x5E, 0x41, 0x5D, 0x41, 0x5C,
    0x5F, 0x5E, 0x5B, 0x5D, 0xC3,
};

// The two jump table entries: 2D751F and 2D7655 as offsets from the image base.
constexpr std::uint8_t ClassSkillEntryWitness[]{
    0x1F, 0x75, 0x2D, 0x00,
};
constexpr std::uint8_t OSkillEntryWitness[]{
    0x55, 0x76, 0x2D, 0x00,
};

// 2DAED0: push rbx,rsi,rdi,r13 / sub rsp,68h / mov r13,[rsp+0B0h]. Hooked.
constexpr std::uint8_t AccumulatorPrologueWitness[]{
    0x40, 0x53, 0x56, 0x57, 0x41, 0x55, 0x48, 0x83, 0xEC, 0x68, 0x4C, 0x8B, 0xAC, 0x24, 0xB0, 0x00,
    0x00, 0x00,
};

// 2DAEE2: mov rsi,r9 / mov rdi,r8 / mov rbx,rcx (unit) / call 2DB370.
constexpr std::uint8_t AccumulatorSetupWitness[]{
    0x49, 0x8B, 0xF1, 0x49, 0x8B, 0xF8, 0x48, 0x8B, 0xD9, 0xE8, 0x80, 0x04, 0x00, 0x00,
};

// 2DAF0D: test rsi,rsi / je / cmp byte [r13+8],0 / je: range data is
// argument 4 (array) and argument 5 (list, engaged flag at +8).
constexpr std::uint8_t AccumulatorRangeWitness[]{
    0x48, 0x85, 0xF6, 0x0F, 0x84, 0x45, 0x04, 0x00, 0x00, 0x41, 0x80, 0x7D, 0x08, 0x00, 0x0F, 0x84,
    0x3A, 0x04, 0x00, 0x00,
};

// 300A90: the data tables of a data context, qword [table + context * 16].
constexpr std::uint8_t GetDataTablesBodyWitness[]{
    0x48, 0x83, 0xEC, 0x28, 0x0F, 0xB6, 0xC1, 0x48, 0x89, 0x44, 0x24, 0x38, 0x48, 0x83, 0xF8, 0x04,
    0x72, 0x19, 0x48, 0x8D, 0x44, 0x24, 0x38, 0x48, 0x8D, 0x4C, 0x24, 0x40, 0x48, 0x89, 0x44, 0x24,
    0x40, 0xE8, 0x7A, 0xD3, 0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x01, 0xCC, 0x48, 0x8B, 0x44, 0x24, 0x38,
    0x48, 0x8D, 0x0D, 0xB9, 0x9A, 0x79, 0x02, 0x48, 0x03, 0xC0, 0x48, 0x8B, 0x04, 0xC1, 0x48, 0x83,
    0xC4, 0x28, 0xC3,
};

// 34A0E0: returns byte [unit+1BDh], the data context.
constexpr std::uint8_t GetDataContextBodyWitness[]{
    0x48, 0x83, 0xEC, 0x28, 0x48, 0x85, 0xC9, 0x75, 0x1A, 0x88, 0x4C, 0x24, 0x30, 0x48, 0x8D, 0x4C,
    0x24, 0x30, 0xE8, 0x49, 0xC7, 0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x01, 0xCC, 0x32, 0xC0, 0x48, 0x83,
    0xC4, 0x28, 0xC3, 0x0F, 0xB6, 0x81, 0xBD, 0x01, 0x00, 0x00, 0x48, 0x83, 0xC4, 0x28, 0xC3,
};

// 2F5DFA: the old list stat reader body (dataContext, list, stat, layer).
constexpr std::uint8_t ReadListStatBodyWitness[]{
    0x57, 0x48, 0x83, 0xEC, 0x20, 0x41, 0x0F, 0xB7, 0xF1, 0x41, 0x8B, 0xD8, 0x48, 0x8B, 0xFA, 0x48,
    0x85, 0xD2, 0x75, 0x12,
};

// 2D9730: itemstatcost record of a stat id, 144h bytes per row from the
// table at [dataTables+1258h], count at [dataTables+1260h], zero past the end.
constexpr std::uint8_t GetStatRecordBodyWitness[]{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x48,
    0x63, 0xF2, 0xE8, 0x49, 0x73, 0x02, 0x00, 0x48, 0x8B, 0xF8, 0x48, 0x8B, 0xDE, 0x85, 0xF6, 0x78,
    0x09, 0x48, 0x3B, 0x98, 0x60, 0x12, 0x00, 0x00, 0x72, 0x18, 0x48, 0x8D, 0x4C, 0x24, 0x48, 0xC6,
    0x44, 0x24, 0x48, 0x00, 0xE8, 0x57, 0x7A, 0xE5, 0xFF, 0x84, 0xC0, 0x74, 0x01, 0xCC, 0x85, 0xF6,
    0x78, 0x55, 0x48, 0x3B, 0x9F, 0x60, 0x12, 0x00, 0x00, 0x73, 0x4C, 0x48, 0x81, 0xC7, 0x58, 0x12,
    0x00, 0x00, 0x48, 0x89, 0x5C, 0x24, 0x50, 0x48, 0x3B, 0x5F, 0x08, 0x72, 0x1E, 0x48, 0x8D, 0x44,
    0x24, 0x50, 0x48, 0x89, 0x7C, 0x24, 0x28, 0x48, 0x8D, 0x4C, 0x24, 0x20, 0x48, 0x89, 0x44, 0x24,
    0x20, 0xE8, 0xBA, 0x76, 0xE5, 0xFF, 0x84, 0xC0, 0x74, 0x01, 0xCC, 0x48, 0x69, 0x44, 0x24, 0x50,
    0x44, 0x01, 0x00, 0x00, 0x48, 0x03, 0x07, 0x48, 0x8B, 0x5C, 0x24, 0x40, 0x48, 0x8B, 0x74, 0x24,
    0x58, 0x48, 0x83, 0xC4, 0x30, 0x5F, 0xC3,
};

// ---------------------------------------------------------------------------
// Relays
// ---------------------------------------------------------------------------
//
// Entered by the writer's switch jump, inside the writer's frame: rsp is
// 16-byte aligned, no volatile register is live at either case entry, and
// ebx / r13 / r14 / rdi / [rbp-68h] hold the value, the record, the layer, the
// format and the unit. Every jump goes through an absolute slot on the page.
//
// Class-skill relay (descfunc 27), page +000:
//   sub  rsp,30h
//   lea  rax,[rsp+28h] / mov [rsp+20h],rax     ; argument 5: merged value out
//   mov  rcx,[rbp-68h]                          ; unit
//   mov  edx,ebx / mov r8,r14 / mov r9,r13      ; value, layer, record
//   call [DecideClassSkillLine]
//   cmp  rax,1 / je hide / jb native
//   mov  ebx,[rsp+28h] / mov rdi,rax            ; merged value, "allclasses"
//   add  rsp,30h / jmp [2D7655]                 ; the descfunc 28 code
//   native: add rsp,30h / jmp [2D751F]
//   hide:   add rsp,30h / jmp [2D6A6A]
//
// Plain-line relay (descfunc 28), page +080:
//   sub  rsp,20h
//   mov  rcx,[rbp-68h] / mov edx,ebx / mov r8,r14 / mov r9,r13
//   call [DecideOSkillLine]
//   add  rsp,20h / test eax,eax / jne hide
//   jmp  [2D7655]
//   hide: jmp [2D6A6A]
//
// The decision functions keep every non-volatile register, as any x64
// function does. After unload both decision slots point at "xor eax,eax / ret"
// on the page, so the relays only ever take the native cases.

constexpr std::uint8_t ClassSkillRelay[]{
    0x48, 0x83, 0xEC, 0x30, 0x48, 0x8D, 0x44, 0x24, 0x28, 0x48, 0x89, 0x44, 0x24, 0x20, 0x48, 0x8B,
    0x4D, 0x98, 0x89, 0xDA, 0x4D, 0x89, 0xF0, 0x4D, 0x89, 0xE9, 0xFF, 0x15, 0xE0, 0x00, 0x00, 0x00,
    0x48, 0x83, 0xF8, 0x01, 0x74, 0x1D, 0x72, 0x11, 0x8B, 0x5C, 0x24, 0x28, 0x48, 0x89, 0xC7, 0x48,
    0x83, 0xC4, 0x30, 0xFF, 0x25, 0xDF, 0x00, 0x00, 0x00, 0x48, 0x83, 0xC4, 0x30, 0xFF, 0x25, 0xCD,
    0x00, 0x00, 0x00, 0x48, 0x83, 0xC4, 0x30, 0xFF, 0x25, 0xD3, 0x00, 0x00, 0x00,
};

constexpr std::uint8_t OSkillRelay[]{
    0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0x4D, 0x98, 0x89, 0xDA, 0x4D, 0x89, 0xF0, 0x4D, 0x89, 0xE9,
    0xFF, 0x15, 0x72, 0x00, 0x00, 0x00, 0x48, 0x83, 0xC4, 0x20, 0x85, 0xC0, 0x75, 0x06, 0xFF, 0x25,
    0x74, 0x00, 0x00, 0x00, 0xFF, 0x25, 0x76, 0x00, 0x00, 0x00,
};

constexpr std::uint8_t NativeDecisionStub[]{
    0x31, 0xC0, 0xC3,
};

constexpr std::size_t RelayPageBytes          = 4'096;
constexpr std::size_t ClassSkillRelayOffset   = 0x000;
constexpr std::size_t OSkillRelayOffset       = 0x080;
constexpr std::size_t NativeStubOffset        = 0x0C0;
constexpr std::size_t DecideClassSkillSlot    = 0x100;
constexpr std::size_t DecideOSkillSlot        = 0x108;
constexpr std::size_t ClassSkillCaseSlot      = 0x110;
constexpr std::size_t OSkillCaseSlot          = 0x118;
constexpr std::size_t NoLineSlot              = 0x120;

static_assert(sizeof(ClassSkillRelay) <= OSkillRelayOffset - ClassSkillRelayOffset);
static_assert(sizeof(OSkillRelay) <= NativeStubOffset - OSkillRelayOffset);
static_assert(NativeStubOffset + sizeof(NativeDecisionStub) <= DecideClassSkillSlot);

// ---------------------------------------------------------------------------
// Native layout
// ---------------------------------------------------------------------------

constexpr std::int32_t  ClassSkillStatId  = 107;  // item_singleskill
constexpr std::uint8_t  PlainLineDescFunc = 28;   // "+3 to Leap"
constexpr std::uint8_t  ClassLineDescFunc = 27;   // "+27 to Leap (Barbarian Only)"
constexpr std::size_t   RecordStatOffset     = 0x00;  // word, the row's stat id
constexpr std::size_t   RecordValShiftOffset = 0x14;  // byte, valshift
constexpr std::size_t   RecordDescFuncOffset = 0x32;  // byte, descfunc (2D654D)
constexpr std::size_t   RecordOpOffset       = 0x50;  // byte, op
constexpr std::size_t   DataTablesStatRowsOffset = 0x1260;  // qword, itemstatcost rows (2D9751)
constexpr std::size_t   RangeEngagedOffset   = 8;     // byte in argument 5 of 2DAED0 (2DAF16)
constexpr std::uint32_t CallSize = 5;
constexpr std::int64_t  MaximumLayer = 0xFFFF;

constexpr char FormatKey[] = "allclasses";

// The string resolver takes a pointer to { text, length }.
struct StringKey {
    const char*   text;
    std::uint64_t length;
};

static_assert(sizeof(StringKey) == 16);

// Every narrow argument is passed as a zero-extended 64-bit value: D2RCore
// builds its lookup keys from full registers.
using ResolveStringKeyFn = const char*(__fastcall*)(const StringKey* key);
using GetDataContextFn   = std::uint8_t(__fastcall*)(void* unit);
using ReadStatFn         = std::int32_t(__fastcall*)(std::uint64_t dataContext, void* statList,
                               std::uint64_t statId, std::uint64_t layer);
using GetStatRecordFn    = const std::uint8_t*(__fastcall*)(std::uint64_t dataContext,
                               std::uint64_t statId);
using GetDataTablesFn    = const std::uint8_t*(__fastcall*)(std::uint64_t dataContext);
using AccumulateFn       = std::uint64_t(__fastcall*)(void* unit, void* statList,
                               void* accumulator, void* fourth, void* fifth) noexcept;

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

constexpr std::size_t  MaximumConfigBytes = 32'768;
constexpr std::int32_t MaximumTableId     = 0x7FFF;

struct Config {
    bool         enabled        = true;
    std::int32_t oskillLineStat = 370;  // item_nonclassskill_display in ESR
};

constexpr char DefaultConfigToml[] =
    "# Skill Line Merge\n"
    "#\n"
    "# Item tooltips show the plain line and the class-only line of the same\n"
    "# skill as one line with their sum:\n"
    "#   +3 to Leap\n"
    "#   +27 to Leap (Barbarian Only)\n"
    "# shows as\n"
    "#   +30 to Leap (All Classes)\n"
    "# Any class that has the skill gets both bonuses, so the sum is what the\n"
    "# character gets.\n"
    "#\n"
    "# Which lines\n"
    "#   - The plain line is the itemstatcost.txt row in oskill_line_stat below.\n"
    "#     The class-only line is item_singleskill (row 107). Only these two\n"
    "#     merge, and only for the same skill: +3 to Leap and +27 to Bash stay\n"
    "#     two lines.\n"
    "#   - Each tooltip block merges on its own: the item's properties (with its\n"
    "#     socketed gems, jewels and runes, which the game already adds in), the\n"
    "#     green partial set bonuses and the gold full set bonuses. A pair split\n"
    "#     across two blocks stays two lines.\n"
    "#   - The merged line takes the place of the class-only line, in the block's\n"
    "#     own color, with the game's skill name. The plain line disappears.\n"
    "#   - If the two values add up to 0, neither line shows.\n"
    "#   - Views that show stat ranges (the Chronicle, for example) keep both\n"
    "#     lines with their ranges.\n"
    "#\n"
    "# The text\n"
    "#   - The merged line uses the string key allclasses: one whole number\n"
    "#     written as %+d (or %d), then the skill name written as %s, in that\n"
    "#     order, for example \"%+d to %s (All Classes)\".\n"
    "#   - If the key is missing, or its text in the current language does not\n"
    "#     have that shape, both lines stay as they are and the log says why.\n"
    "#\n"
    "# The rows\n"
    "#   - The plain line's row must use descfunc 28 and item_singleskill must\n"
    "#     use descfunc 27, as in the base game.\n"
    "#   - Both rows must leave valshift empty and use no op from 2 to 5, as in\n"
    "#     the base game. Otherwise the number in the tooltip is not the stat\n"
    "#     value, and both lines stay as they are.\n"
    "#\n"
    "# Console command: skillmerge (status and counters)\n"
    "\n"
    "[skill-line-merge]\n"
    "\n"
    "# Master switch.\n"
    "enabled = true\n"
    "\n"
    "# itemstatcost.txt row whose line reads \"+3 to Leap\".\n"
    "# ESR shows oskills through item_nonclassskill_display (370): the oskill\n"
    "# property sets it next to item_nonclassskill (97), which has no description\n"
    "# of its own, while oskill-hidden sets 97 alone. So a hidden oskill has no\n"
    "# line and never merges.\n"
    "# A mod that shows item_nonclassskill directly uses 97 here.\n"
    "oskill_line_stat = 370\n";

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

enum class PartState : std::uint8_t {
    NotLoaded,
    DisabledByConfig,
    Armed,
    Failed,
};

const D2RL::PluginContext* Context{};
std::uintptr_t             Base{};
Config                     Settings{};
PartState                  State{ PartState::NotLoaded };

ResolveStringKeyFn ResolveStringKey{};
GetDataContextFn   GetDataContext{};
ReadStatFn         ReadListStat{};
GetStatRecordFn    GetStatRecord{};
GetDataTablesFn    GetDataTables{};
AccumulateFn       OriginalAccumulate{};

std::uint8_t* RelayPage{};

std::atomic<std::uint64_t> BlocksRead{};
std::atomic<std::uint64_t> LinesMerged{};
std::atomic<std::uint64_t> PlainLinesRemoved{};
std::atomic<std::uint64_t> PairsHidden{};
std::atomic<std::uint64_t> FormatRefusals{};
std::atomic<std::uint64_t> ShiftedRowRefusals{};
std::atomic<std::uint64_t> ValueMismatches{};
std::atomic<std::uint64_t> RangeBlocksLeft{};
std::atomic<std::uint64_t> RowRefusals{};
std::atomic<bool>          FormatProblemLogged{};
std::atomic<bool>          ShiftedRowsLogged{};
std::atomic<bool>          RowProblemLogged{};

// The tooltip block the calling thread is drawing, noted by the accumulator
// hook before the block's first line.
struct BlockCapture {
    void*               unit;
    void*               statList;
    std::uint64_t       dataContext;
    const std::uint8_t* oskillRecord;
    const std::uint8_t* classSkillRecord;
    bool                plainValues;
    bool                valid;
};

thread_local BlockCapture Block{};

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

void ApplyConfigLine(std::string_view section, std::string_view key, std::string_view value) noexcept {
    if (section != "skill-line-merge") return;
    if (key == "enabled") {
        if (!ParseBool(value, Settings.enabled)) {
            ConfigProblems += "skill-line-merge.enabled must be true or false; using the default. ";
        }
    } else if (key == "oskill_line_stat") {
        if (!ParseTableId(value, Settings.oskillLineStat)) {
            ConfigProblems += "skill-line-merge.oskill_line_stat must be 0 to 32767; using the "
                              "default. ";
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
        Context->LogWarn("Skill Line Merge: config file could not be created; using defaults.");
    } else {
        std::string buffer(MaximumConfigBytes, '\0');
        if (Context->ReadConfig(buffer.data(), static_cast<std::uint32_t>(buffer.size()), nullptr)) {
            buffer.resize(std::strlen(buffer.c_str()));
            ParseConfig(buffer);
        } else {
            Context->LogWarn("Skill Line Merge: config file could not be read; using defaults.");
        }
    }
    if (!ConfigProblems.empty()) {
        D2RL::LogWarnF(Context, "Skill Line Merge: %s", ConfigProblems.c_str());
    }
}

// ---------------------------------------------------------------------------
// The merged line's text
// ---------------------------------------------------------------------------

enum class Conversion : std::uint8_t { Invalid, Literal, Integer, Text };

// Reads one printf conversion starting at text[index] == '%' and moves index
// past it. Flags, width and precision are allowed; '*' and length modifiers
// are not.
auto ReadConversion(const char* text, std::size_t& index) noexcept -> Conversion {
    ++index;
    if (text[index] == '%') { ++index; return Conversion::Literal; }
    while (text[index] == '-' || text[index] == '+' || text[index] == ' '
            || text[index] == '#' || text[index] == '0') {
        ++index;
    }
    while (text[index] >= '0' && text[index] <= '9') ++index;
    if (text[index] == '.') {
        ++index;
        while (text[index] >= '0' && text[index] <= '9') ++index;
    }
    const char kind = text[index];
    if (kind == '\0') return Conversion::Invalid;
    ++index;
    if (kind == 'd' || kind == 'i') return Conversion::Integer;
    if (kind == 's') return Conversion::Text;
    return Conversion::Invalid;
}

// The formatter receives (number, skill name), so the text must convert
// exactly one integer and then exactly one string.
auto IsMergedFormat(const char* text) noexcept -> bool {
    if (text == nullptr || text[0] == '\0') return false;
    Conversion seen[2]{};
    std::size_t count = 0;
    std::size_t index = 0;
    while (text[index] != '\0') {
        if (text[index] != '%') { ++index; continue; }
        const Conversion conversion = ReadConversion(text, index);
        if (conversion == Conversion::Literal) continue;
        if (conversion == Conversion::Invalid || count == 2) return false;
        seen[count++] = conversion;
    }
    return count == 2 && seen[0] == Conversion::Integer && seen[1] == Conversion::Text;
}

void NoteFormatProblem(const char* text) noexcept {
    FormatRefusals.fetch_add(1, std::memory_order_relaxed);
    if (FormatProblemLogged.exchange(true, std::memory_order_relaxed)) return;
    char shown[96]{};
    if (text != nullptr) std::snprintf(shown, sizeof(shown), "%s", text);
    D2RL::LogWarnF(Context,
        "Skill Line Merge: the \"%s\" text in the current language is \"%s\". It needs one "
        "whole number (%%d or %%+d) followed by one %%s, like \"%%+d to %%s (All Classes)\". "
        "The two skill lines stay as they are until it does.",
        FormatKey, shown);
}

auto MergedFormat() noexcept -> const char* {
    const StringKey key{ FormatKey, sizeof(FormatKey) - 1 };
    const char* const text = ResolveStringKey(&key);
    if (!IsMergedFormat(text)) {
        NoteFormatProblem(text);
        return nullptr;
    }
    return text;
}

// ---------------------------------------------------------------------------
// Decisions
// ---------------------------------------------------------------------------

// The tooltip shows the stat value unchanged only without valshift and
// without op 2..5 (those scale the shown number by another stat).
auto HasPlainValue(const std::uint8_t* record) noexcept -> bool {
    const std::uint8_t valShift = record[RecordValShiftOffset];
    const std::uint8_t op = record[RecordOpOffset];
    return valShift == 0 && static_cast<std::uint8_t>(op - 2) > 3;
}

auto IsStat(const std::uint8_t* record, std::int32_t statId, const std::uint8_t* known) noexcept
        -> bool {
    if (record == nullptr) return false;
    if (record == known) return true;
    std::uint16_t id = 0;
    std::memcpy(&id, record + RecordStatOffset, sizeof(id));
    return id == static_cast<std::uint16_t>(statId);
}

void NoteShiftedRows() noexcept {
    ShiftedRowRefusals.fetch_add(1, std::memory_order_relaxed);
    if (ShiftedRowsLogged.exchange(true, std::memory_order_relaxed)) return;
    D2RL::LogWarnF(Context,
        "Skill Line Merge: itemstatcost.txt row %d (oskill_line_stat) or row %d "
        "(item_singleskill) uses valshift or op 2 to 5, so the tooltip number is not the stat "
        "value. Those two lines stay as they are.",
        Settings.oskillLineStat, ClassSkillStatId);
}

enum class Merge : std::uint8_t { None, Hidden, Merged };

struct PairValues {
    std::int32_t oskill;
    std::int32_t classSkill;
    std::int32_t total;
    const char*  format;
};

// The one test both relays apply, so the plain line and the class-only line
// of a skill always get matching answers.
auto Evaluate(void* unit, std::int64_t layer, PairValues& pair) noexcept -> Merge {
    if (!Block.valid || unit == nullptr || unit != Block.unit) return Merge::None;
    if (layer < 0 || layer > MaximumLayer) return Merge::None;
    const auto layerArgument = static_cast<std::uint64_t>(layer);

    pair.oskill = ReadListStat(Block.dataContext, Block.statList,
        static_cast<std::uint64_t>(Settings.oskillLineStat), layerArgument);
    if (pair.oskill == 0) return Merge::None;
    pair.classSkill = ReadListStat(Block.dataContext, Block.statList,
        static_cast<std::uint64_t>(ClassSkillStatId), layerArgument);
    if (pair.classSkill == 0) return Merge::None;

    if (!Block.plainValues) {
        NoteShiftedRows();
        return Merge::None;
    }
    pair.format = MergedFormat();
    if (pair.format == nullptr) return Merge::None;

    const std::int64_t sum = static_cast<std::int64_t>(pair.oskill) + pair.classSkill;
    pair.total = sum > INT32_MAX ? INT32_MAX
               : sum < INT32_MIN ? INT32_MIN
               : static_cast<std::int32_t>(sum);
    return pair.total == 0 ? Merge::Hidden : Merge::Merged;
}

// Called by the plain-line relay. 0 keeps the native line, 1 writes no line.
std::uint64_t DecideOSkillLine(void* unit, std::uint64_t value, std::int64_t layer,
        const std::uint8_t* record) noexcept {
    if (!IsStat(record, Settings.oskillLineStat, Block.oskillRecord)) return 0;
    PairValues pair{};
    if (Evaluate(unit, layer, pair) == Merge::None) return 0;
    if (static_cast<std::int32_t>(value) != pair.oskill) {
        ValueMismatches.fetch_add(1, std::memory_order_relaxed);
    }
    PlainLinesRemoved.fetch_add(1, std::memory_order_relaxed);
    return 1;
}

// Called by the class-skill relay. 0 keeps the native line, 1 writes no line,
// anything else is the merged line's format, with its number in *merged.
std::uint64_t DecideClassSkillLine(void* unit, std::uint64_t value, std::int64_t layer,
        const std::uint8_t* record, std::int32_t* merged) noexcept {
    if (merged == nullptr || !IsStat(record, ClassSkillStatId, Block.classSkillRecord)) return 0;
    PairValues pair{};
    const Merge merge = Evaluate(unit, layer, pair);
    if (merge == Merge::None) return 0;
    if (static_cast<std::int32_t>(value) != pair.classSkill) {
        ValueMismatches.fetch_add(1, std::memory_order_relaxed);
    }
    if (merge == Merge::Hidden) {
        PairsHidden.fetch_add(1, std::memory_order_relaxed);
        return 1;
    }
    *merged = pair.total;
    LinesMerged.fetch_add(1, std::memory_order_relaxed);
    return reinterpret_cast<std::uint64_t>(pair.format);
}

// ---------------------------------------------------------------------------
// Accumulator hook
// ---------------------------------------------------------------------------

void NoteRowProblem(const char* format, ...) noexcept {
    RowRefusals.fetch_add(1, std::memory_order_relaxed);
    if (RowProblemLogged.exchange(true, std::memory_order_relaxed)) return;
    char message[320];
    va_list arguments;
    va_start(arguments, format);
    std::vsnprintf(message, sizeof(message), format, arguments);
    va_end(arguments);
    Context->LogWarn(message);
}

struct LineRows {
    const std::uint8_t* oskill;
    const std::uint8_t* classSkill;
};

// The itemstatcost rows of both lines in this data context, or nothing when a
// row is missing or does not write the line this plugin merges.
auto FindLineRows(std::uint64_t dataContext) noexcept -> LineRows {
    const std::uint8_t* const tables = GetDataTables(dataContext);
    if (tables == nullptr) return {};
    std::uint64_t rows = 0;
    std::memcpy(&rows, tables + DataTablesStatRowsOffset, sizeof(rows));
    if (static_cast<std::uint64_t>(Settings.oskillLineStat) >= rows
            || static_cast<std::uint64_t>(ClassSkillStatId) >= rows) {
        NoteRowProblem("Skill Line Merge: oskill_line_stat (%d) or item_singleskill (%d) is past "
                       "the end of itemstatcost.txt (%llu rows). Nothing merges until that is fixed.",
            Settings.oskillLineStat, ClassSkillStatId, static_cast<unsigned long long>(rows));
        return {};
    }
    const std::uint8_t* const oskill = GetStatRecord(dataContext,
        static_cast<std::uint64_t>(Settings.oskillLineStat));
    const std::uint8_t* const classSkill = GetStatRecord(dataContext,
        static_cast<std::uint64_t>(ClassSkillStatId));
    if (oskill == nullptr || classSkill == nullptr) return {};
    if (oskill[RecordDescFuncOffset] != PlainLineDescFunc) {
        NoteRowProblem("Skill Line Merge: oskill_line_stat row %d uses descfunc %u, but the "
                       "\"+3 to Leap\" line needs descfunc 28. Nothing merges until that is fixed.",
            Settings.oskillLineStat, static_cast<unsigned>(oskill[RecordDescFuncOffset]));
        return {};
    }
    if (classSkill[RecordDescFuncOffset] != ClassLineDescFunc) {
        NoteRowProblem("Skill Line Merge: item_singleskill (row %d) uses descfunc %u, but the "
                       "class-only line needs descfunc 27. Nothing merges until that is fixed.",
            ClassSkillStatId, static_cast<unsigned>(classSkill[RecordDescFuncOffset]));
        return {};
    }
    return { oskill, classSkill };
}

std::uint64_t __fastcall HookedAccumulate(void* unit, void* statList, void* accumulator,
        void* fourth, void* fifth) noexcept {
    Block = BlockCapture{};
    if (fourth != nullptr && fifth != nullptr
            && static_cast<const std::uint8_t*>(fifth)[RangeEngagedOffset] != 0) {
        // A range view: lines with a range go through the ranged writer.
        RangeBlocksLeft.fetch_add(1, std::memory_order_relaxed);
    } else if (unit != nullptr && statList != nullptr) {
        const std::uint64_t dataContext = GetDataContext(unit);
        const LineRows rows = FindLineRows(dataContext);
        if (rows.oskill != nullptr) {
            Block.unit             = unit;
            Block.statList         = statList;
            Block.dataContext      = dataContext;
            Block.oskillRecord     = rows.oskill;
            Block.classSkillRecord = rows.classSkill;
            Block.plainValues      = HasPlainValue(rows.oskill) && HasPlainValue(rows.classSkill);
            Block.valid            = true;
            BlocksRead.fetch_add(1, std::memory_order_relaxed);
        }
    }
    // The loader publishes the call-through right after the commit; until then
    // (or if it never does) the native accumulator cannot be reached.
    const AccumulateFn original = OriginalAccumulate;
    if (original == nullptr) return 0;
    return original(unit, statList, accumulator, fourth, fifth);
}

// ---------------------------------------------------------------------------
// Verification
// ---------------------------------------------------------------------------

auto Verify(std::uint64_t rva, const std::uint8_t* expected, std::size_t size,
        const char* label) noexcept -> bool {
    if (Context->CheckExpectedBytes(rva, expected, static_cast<std::uint32_t>(size))) return true;
    D2RL::LogErrorF(Context,
        "Skill Line Merge: %s at 0x%llX does not match the verified D2R image, or another "
        "plugin already owns it.",
        label, static_cast<unsigned long long>(rva));
    return false;
}

auto VerifyThunk(std::uint64_t rva, const std::uint8_t* body, std::size_t bodySize,
        const char* label) noexcept -> bool {
    return Verify(rva, ThunkJumpWitness, sizeof(ThunkJumpWitness), label)
        && Verify(rva + ThunkPaddingOffset, ThunkPaddingWitness, sizeof(ThunkPaddingWitness), label)
        && Verify(rva + ThunkBodyOffset, body, bodySize, label);
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

auto VerifyContract() noexcept -> bool {
    if (!(Verify(WriterFrameRva, WriterFrameWitness, sizeof(WriterFrameWitness),
                "the stat line writer's frame")
            && Verify(WriterBodyRva, WriterBodyWitness, sizeof(WriterBodyWitness),
                "the stat line writer's setup and descfunc switch")
            && Verify(ClassSkillCaseRva, ClassSkillCaseWitness, sizeof(ClassSkillCaseWitness),
                "the descfunc 27 case")
            && Verify(OSkillCaseRva, OSkillCaseWitness, sizeof(OSkillCaseWitness),
                "the descfunc 28 case")
            && Verify(OSkillFormatTailRva, OSkillFormatTailWitness,
                sizeof(OSkillFormatTailWitness), "the descfunc 28 line format")
            && Verify(NoLineExitRva, NoLineExitWitness, sizeof(NoLineExitWitness),
                "the writer's no-line exit")
            && Verify(ClassSkillEntryRva, ClassSkillEntryWitness, sizeof(ClassSkillEntryWitness),
                "the descfunc 27 jump table entry")
            && Verify(OSkillEntryRva, OSkillEntryWitness, sizeof(OSkillEntryWitness),
                "the descfunc 28 jump table entry")
            && Verify(AccumulatorRva, AccumulatorPrologueWitness,
                sizeof(AccumulatorPrologueWitness), "the tooltip accumulator entry")
            && Verify(AccumulatorSetupRva, AccumulatorSetupWitness,
                sizeof(AccumulatorSetupWitness), "the tooltip accumulator setup")
            && Verify(AccumulatorRangeRva, AccumulatorRangeWitness,
                sizeof(AccumulatorRangeWitness), "the tooltip accumulator's range data test")
            && Verify(GetDataTablesRva, GetDataTablesBodyWitness,
                sizeof(GetDataTablesBodyWitness), "the data tables getter")
            && Verify(GetDataContextRva, GetDataContextBodyWitness,
                sizeof(GetDataContextBodyWitness), "the unit data context getter")
            && VerifyThunk(ReadListStatRva, ReadListStatBodyWitness,
                sizeof(ReadListStatBodyWitness), "the list stat reader thunk")
            && Verify(GetStatRecordRva, GetStatRecordBodyWitness,
                sizeof(GetStatRecordBodyWitness), "the itemstatcost record getter"))) {
        return false;
    }

    std::uintptr_t resolvers[sizeof(ResolverCallRvas) / sizeof(ResolverCallRvas[0])]{};
    for (std::size_t index = 0; index < sizeof(ResolverCallRvas) / sizeof(ResolverCallRvas[0]);
            ++index) {
        if (!ResolveCallTarget(ResolverCallRvas[index], resolvers[index])
                || resolvers[index] != resolvers[0]) {
            Context->LogError("Skill Line Merge: the writer's string key resolver could not be "
                              "located.");
            return false;
        }
    }
    ResolveStringKey = reinterpret_cast<ResolveStringKeyFn>(resolvers[0]);
    return true;
}

// ---------------------------------------------------------------------------
// Relay page
// ---------------------------------------------------------------------------

// The jump table holds unsigned 32-bit offsets from the image base, so the
// page has to sit above the image base and below base + 4 GB.
auto AllocateRelayPage() noexcept -> std::uint8_t* {
    SYSTEM_INFO systemInfo{};
    GetSystemInfo(&systemInfo);
    const auto granularity = static_cast<std::uintptr_t>(systemInfo.dwAllocationGranularity);
    const std::uintptr_t limit = Base + 0xFFFF'0000ULL;
    std::uintptr_t cursor = Base;
    while (cursor < limit) {
        MEMORY_BASIC_INFORMATION region{};
        if (VirtualQuery(reinterpret_cast<void*>(cursor), &region, sizeof(region))
                != sizeof(region)) {
            break;
        }
        const auto regionStart = reinterpret_cast<std::uintptr_t>(region.BaseAddress);
        const std::uintptr_t regionEnd = regionStart + region.RegionSize;
        if (region.State == MEM_FREE) {
            const std::uintptr_t from = cursor > regionStart ? cursor : regionStart;
            const std::uintptr_t candidate = (from + granularity - 1) & ~(granularity - 1);
            if (candidate + RelayPageBytes <= regionEnd && candidate + RelayPageBytes <= limit) {
                if (void* memory = VirtualAlloc(reinterpret_cast<void*>(candidate), RelayPageBytes,
                        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)) {
                    return static_cast<std::uint8_t*>(memory);
                }
            }
        }
        if (regionEnd <= cursor) break;
        cursor = regionEnd;
    }
    return nullptr;
}

void WriteSlot(std::uint8_t* page, std::size_t offset, std::uint64_t value) noexcept {
    std::memcpy(page + offset, &value, sizeof(value));
}

auto BuildRelayPage(std::uint8_t* page) noexcept -> bool {
    std::memset(page, 0xCC, RelayPageBytes);
    std::memcpy(page + ClassSkillRelayOffset, ClassSkillRelay, sizeof(ClassSkillRelay));
    std::memcpy(page + OSkillRelayOffset, OSkillRelay, sizeof(OSkillRelay));
    std::memcpy(page + NativeStubOffset, NativeDecisionStub, sizeof(NativeDecisionStub));
    WriteSlot(page, DecideClassSkillSlot, reinterpret_cast<std::uint64_t>(&DecideClassSkillLine));
    WriteSlot(page, DecideOSkillSlot, reinterpret_cast<std::uint64_t>(&DecideOSkillLine));
    WriteSlot(page, ClassSkillCaseSlot, Base + ClassSkillCaseRva);
    WriteSlot(page, OSkillCaseSlot, Base + OSkillCaseRva);
    WriteSlot(page, NoLineSlot, Base + NoLineExitRva);
    DWORD previous = 0;
    const bool sealed = VirtualProtect(page, RelayPageBytes, PAGE_EXECUTE_READ, &previous) != FALSE;
    FlushInstructionCache(GetCurrentProcess(), page, RelayPageBytes);
    return sealed;
}

// Both decision slots point at "xor eax,eax / ret" on the page: the relays
// only take the native cases from then on. The page itself is kept, a thread
// may be inside it right now.
void ParkRelays() noexcept {
    if (RelayPage == nullptr) return;
    DWORD previous = 0;
    if (!VirtualProtect(RelayPage, RelayPageBytes, PAGE_EXECUTE_READWRITE, &previous)) return;
    const auto stub = reinterpret_cast<std::uint64_t>(RelayPage + NativeStubOffset);
    WriteSlot(RelayPage, DecideClassSkillSlot, stub);
    WriteSlot(RelayPage, DecideOSkillSlot, stub);
    DWORD ignored = 0;
    VirtualProtect(RelayPage, RelayPageBytes, PAGE_EXECUTE_READ, &ignored);
    FlushInstructionCache(GetCurrentProcess(), RelayPage, RelayPageBytes);
}

// ---------------------------------------------------------------------------
// Installation
// ---------------------------------------------------------------------------

auto MutationResultName(D2RL::Mutations::Result result) noexcept -> const char* {
    using D2RL::Mutations::Result;
    switch (result) {
    case Result::Success:               return "success";
    case Result::InvalidArgument:       return "invalid argument";
    case Result::Unsupported:           return "unsupported";
    case Result::OwnerInactive:         return "owner inactive";
    case Result::NotFound:              return "not found";
    case Result::InvalidState:          return "invalid state";
    case Result::Conflict:              return "conflict with another patch";
    case Result::ExpectedBytesMismatch: return "expected bytes mismatch";
    case Result::Unavailable:           return "unavailable";
    case Result::CommitFailed:          return "commit failed";
    case Result::RollbackFailed:        return "rollback failed";
    case Result::CallbackFault:         return "callback fault";
    }
    return "unknown";
}

auto FailureReasonName(D2RL::Mutations::FailureReason reason) noexcept -> const char* {
    using D2RL::Mutations::FailureReason;
    switch (reason) {
    case FailureReason::None:                  return "none";
    case FailureReason::EmptyTransaction:      return "empty transaction";
    case FailureReason::InvalidRange:          return "invalid range";
    case FailureReason::Overlap:               return "overlaps another patch";
    case FailureReason::ExpectedBytesMismatch: return "expected bytes mismatch";
    case FailureReason::NativeHooksRequired:   return "NativeHooks flag required";
    case FailureReason::HookPreparationFailed: return "hook preparation failed";
    case FailureReason::MemoryWriteFailed:     return "memory write failed";
    case FailureReason::HookEnableFailed:      return "hook enable failed";
    case FailureReason::TrackingFailed:        return "tracking failed";
    case FailureReason::RollbackFailed:        return "rollback failed";
    }
    return "unknown";
}

void ReleaseRelayPage() noexcept {
    if (RelayPage == nullptr) return;
    VirtualFree(RelayPage, 0, MEM_RELEASE);
    RelayPage = nullptr;
}

// The accumulator hook and both table entries in one loader transaction: all
// of them or none. Until a commit is attempted nothing can reach the relays,
// so the page is freed. After a failed commit it is parked and kept instead:
// if the loader could not restore a table entry, the page must stay valid.
auto Install() noexcept -> bool {
    RelayPage = AllocateRelayPage();
    if (RelayPage == nullptr) {
        Context->LogError("Skill Line Merge: no relay page was available within 4 GB above the "
                          "game image.");
        return false;
    }
    if (!BuildRelayPage(RelayPage)) {
        Context->LogError("Skill Line Merge: the relay page protection could not be set.");
        ReleaseRelayPage();
        return false;
    }
    const auto pageOffset = reinterpret_cast<std::uintptr_t>(RelayPage) - Base;
    const auto classSkillEntry = static_cast<std::uint32_t>(pageOffset + ClassSkillRelayOffset);
    const auto oskillEntry = static_cast<std::uint32_t>(pageOffset + OSkillRelayOffset);

    const D2RL::MutationService* mutations = nullptr;
    if (Context->QueryService(&mutations) != D2RL::ServiceQueryResult::Success
            || !D2RL::HasMutationServiceField(mutations, D2RL::MutationServiceRequiredSize)) {
        Context->LogError("Skill Line Merge: the loader's mutation service is unavailable.");
        ReleaseRelayPage();
        return false;
    }

    D2RL::Mutations::TransactionHandle transaction = D2RL::Mutations::InvalidTransactionHandle;
    D2RL::Mutations::Result status = mutations->beginTransaction(Context, &transaction);
    if (status != D2RL::Mutations::Result::Success) {
        D2RL::LogErrorF(Context, "Skill Line Merge: could not start a transaction (%s).",
            MutationResultName(status));
        ReleaseRelayPage();
        return false;
    }

    const D2RL::Mutations::InlineHookRequest hook{
        .structSize   = D2RL::Mutations::InlineHookRequestSize,
        .flags        = 0,
        .rva          = AccumulatorRva,
        .expected     = AccumulatorPrologueWitness,
        .expectedSize = sizeof(AccumulatorPrologueWitness),
        .reserved     = 0,
        .target       = reinterpret_cast<void*>(&HookedAccumulate),
        .reserved2    = 0,
    };
    const D2RL::Mutations::BytePatchRequest classSkillPatch{
        .structSize   = D2RL::Mutations::BytePatchRequestSize,
        .flags        = 0,
        .rva          = ClassSkillEntryRva,
        .expected     = ClassSkillEntryWitness,
        .expectedSize = sizeof(ClassSkillEntryWitness),
        .reserved     = 0,
        .bytes        = &classSkillEntry,
        .size         = sizeof(classSkillEntry),
        .reserved2    = 0,
    };
    const D2RL::Mutations::BytePatchRequest oskillPatch{
        .structSize   = D2RL::Mutations::BytePatchRequestSize,
        .flags        = 0,
        .rva          = OSkillEntryRva,
        .expected     = OSkillEntryWitness,
        .expectedSize = sizeof(OSkillEntryWitness),
        .reserved     = 0,
        .bytes        = &oskillEntry,
        .size         = sizeof(oskillEntry),
        .reserved2    = 0,
    };

    D2RL::Mutations::OperationHandle hookOperation = D2RL::Mutations::InvalidOperationHandle;
    D2RL::Mutations::OperationHandle classSkillOperation = D2RL::Mutations::InvalidOperationHandle;
    D2RL::Mutations::OperationHandle oskillOperation = D2RL::Mutations::InvalidOperationHandle;
    const char* staging = "the tooltip accumulator hook";
    status = mutations->stageInlineHook(Context, transaction, &hook, &hookOperation);
    if (status == D2RL::Mutations::Result::Success) {
        staging = "the descfunc 27 jump table entry";
        status = mutations->stageBytePatch(Context, transaction, &classSkillPatch,
            &classSkillOperation);
    }
    if (status == D2RL::Mutations::Result::Success) {
        staging = "the descfunc 28 jump table entry";
        status = mutations->stageBytePatch(Context, transaction, &oskillPatch, &oskillOperation);
    }
    if (status != D2RL::Mutations::Result::Success) {
        D2RL::LogErrorF(Context, "Skill Line Merge: staging %s failed (%s).", staging,
            MutationResultName(status));
        mutations->cancelTransaction(Context, transaction);
        ReleaseRelayPage();
        return false;
    }

    D2RL::Mutations::CommitResult commit{};
    commit.structSize = D2RL::Mutations::CommitResultSize;
    status = mutations->commit(Context, transaction, &commit);
    if (status != D2RL::Mutations::Result::Success
            || commit.result != D2RL::Mutations::Result::Success) {
        const char* failed = "an unknown change";
        if (commit.operation == hookOperation) failed = "the tooltip accumulator hook";
        if (commit.operation == classSkillOperation) failed = "the descfunc 27 jump table entry";
        if (commit.operation == oskillOperation) failed = "the descfunc 28 jump table entry";
        D2RL::LogErrorF(Context,
            "Skill Line Merge: the loader refused %s (%s, %s). Nothing was applied.",
            failed, MutationResultName(commit.result), FailureReasonName(commit.failureReason));
        mutations->cancelTransaction(Context, transaction);
        ParkRelays();
        return false;
    }

    void* original = nullptr;
    status = mutations->getInlineHookOriginal(Context, transaction, hookOperation, &original);
    if (status != D2RL::Mutations::Result::Success || original == nullptr) {
        D2RL::LogErrorF(Context,
            "Skill Line Merge: the loader gave no call-through for the tooltip accumulator (%s).",
            MutationResultName(status));
        ParkRelays();
        return false;
    }
    OriginalAccumulate = reinterpret_cast<AccumulateFn>(original);
    return true;
}

// ---------------------------------------------------------------------------
// Console command
// ---------------------------------------------------------------------------

auto StateName(PartState state) noexcept -> const char* {
    switch (state) {
    case PartState::DisabledByConfig: return "disabled by config";
    case PartState::Armed:            return "armed";
    case PartState::Failed:           return "FAILED, see log";
    default:                          return "not loaded";
    }
}

auto __cdecl StatusCommand(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command,
        void*) noexcept -> D2RL::ConsoleCommandResult {
    if (command == nullptr || command->plugin == nullptr) return D2RL::ConsoleCommandResult::Failed;

    char message[768];
    std::snprintf(message, sizeof(message),
        "Skill Line Merge: %s, rows %d + %d | tooltip blocks read %llu, lines merged %llu, plain "
        "lines removed %llu, pairs adding up to 0 %llu | left unmerged: range view blocks %llu, "
        "unusable rows %llu, \"%s\" text unusable %llu, rows with valshift or op %llu | value "
        "mismatches %llu",
        StateName(State), Settings.oskillLineStat, ClassSkillStatId,
        static_cast<unsigned long long>(BlocksRead.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(LinesMerged.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(PlainLinesRemoved.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(PairsHidden.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(RangeBlocksLeft.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(RowRefusals.load(std::memory_order_relaxed)),
        FormatKey,
        static_cast<unsigned long long>(FormatRefusals.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(ShiftedRowRefusals.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(ValueMismatches.load(std::memory_order_relaxed)));
    command->plugin->WriteConsoleMessage(message);
    return D2RL::ConsoleCommandResult::Handled;
}

void RegisterStatusCommand() noexcept {
    if (!Context->RegisterConsoleCommand("skillmerge", &StatusCommand,
            "Reports Skill Line Merge status and counters.")) {
        Context->LogWarn("Skill Line Merge: the status console command was refused.");
    }
}

constexpr D2RL::PluginInfo Info{
    .infoSize = D2RL::PluginInfoSize,
    .abiVersion = D2RL_PLUGIN_ABI_VERSION,
    .id = "celestialrayone.skill-line-merge",
    .name = "Skill Line Merge",
    .version = "1.1.0",
    .author = "CelestialRayOne",
    .description =
        "Item tooltips show the plain and the class-only line of the same skill as one "
        "\"(All Classes)\" line with the summed value.",
    .flags = D2RL::PluginFlags::Client | D2RL::PluginFlags::NativeHooks,
};

}  // namespace

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* {
    return &Info;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
    if (!D2RL::HasContext(context)) return false;
    Context = context;
    Base = context->exeBase;

    GetDataContext = reinterpret_cast<GetDataContextFn>(Base + GetDataContextRva);
    ReadListStat   = reinterpret_cast<ReadStatFn>(Base + ReadListStatRva);
    GetStatRecord  = reinterpret_cast<GetStatRecordFn>(Base + GetStatRecordRva);
    GetDataTables  = reinterpret_cast<GetDataTablesFn>(Base + GetDataTablesRva);

    ReadConfiguration();

    if (!Settings.enabled) {
        State = PartState::DisabledByConfig;
    } else if (VerifyContract() && Install()) {
        State = PartState::Armed;
    } else {
        State = PartState::Failed;
    }

    D2RL::LogInfoF(Context, "Skill Line Merge: %s (plain line row %d, class-only line row %d).",
        StateName(State), Settings.oskillLineStat, ClassSkillStatId);
    if (State == PartState::Failed) {
        Context->LogError("Skill Line Merge: nothing could be installed. Refusing to load.");
        return false;
    }

    RegisterStatusCommand();
    return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
    if (Context == nullptr) return;
    ParkRelays();
}

}  // namespace CelestialRayOne::SkillLineMerge
