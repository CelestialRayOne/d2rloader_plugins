// Tooltip Color Scope
//
// A color code inside one item stat's text no longer recolors the other stat
// lines of the tooltip. The block's own color is put back right after every
// stat that uses color codes, so each stat's codes only affect its own text.
//
// Everything below was read out of the live D2RLoader 1.3.1 image of D2R 3.3
// (process dump md5 8af7a47c...) and D2RCore.dll (sha256 2a868d01...) and
// disassembled before being relied on.
//
// ---------------------------------------------------------------------------
// Why the game does it
// ---------------------------------------------------------------------------
//   The item builders (main 2BD480, set 2BF1B0, third 2BE560) build the
//   tooltip bottom to top: the first line in the string is the bottom line
//   on screen. Each block of lines is built first and then colored ONCE by
//   the block colorizer CD6E0, which puts a single color code in front of the
//   whole block:
//     CD70D  lea  rax, "colorcode" / mov [rsp+30h], 9   ; key of the escape
//     CD727  call j_ResolveNamespacedStringKey
//     CD73E  add  dil, 30h                              ; '0' + color
//     CD778  strlcat(v8, 400h, block)                   ; code + block
//     CD78A  strlcpy(block, 400h, v8)
//   The renderer keeps the current color from one line to the next, so a
//   color code inside one stat stays active for every line after it in the
//   string, which is every line above it on screen.
//
// ---------------------------------------------------------------------------
// Which function fills which block, and the color the builder gives it
// ---------------------------------------------------------------------------
//   2DA0E0 (item, out, capacity, newlineMode, prefix, a6, a7)  magic properties
//     2BD480: 2BE14B lea rdx,[rbp+9F0h] ... call 2DA0E0
//             2BE1E5 mov ebx,3 ... 2BE249 mov edx,ebx / lea rcx,[rbp+9F0h] /
//             call CD6E0. ebx becomes 1 only at 2BE244, right after the block
//             was overwritten with the "brokensocketin" text (no stat lines).
//     2BF1B0: 2BFD33 lea rdx,[rbp+3610h] ... call 2DA0E0
//             2C00B3 mov edx,3 / lea rcx,[rbp+3610h] / call CD6E0
//     -> color 3
//   2DA310 (player, item, out, capacity)  partial set bonuses, 2BF1B0 only
//             2BFD5B lea r8,[rbp+3210h] ... call 2DA310
//             2C0096 mov edx,2 / lea rcx,[rbp+3210h] / call CD6E0
//     -> color 2
//   2D98D0 (player, item, out, capacity)  full set bonuses, 2BF1B0 only
//             2BFD80 lea r8,[rbp+2E10h] ... call 2D98D0
//             2C0064 mov edx,4 / lea rcx,[rbp+2E10h] / call CD6E0
//     -> color 4
//   2DA510 / 2DC3D0 (item, a2, a3, out, capacity, prefix)  rune and gem
//   "Weapons:/Armor:" lines. Both render through 2DA0E0 (2DA510 into a local
//   buffer it appends to out, 2DC3D0 straight into out, comma mode) and are
//   only called by 2D9A70 <- 2C2800, whose output is the block
//             2BD480: 2BDC2A lea rdx,[rbp+3DF0h] ... call 2C2800
//                     2BE38A xor edx,edx / lea rcx,[rbp+3DF0h] / call CD6E0
//             2BE560: 2BEC19 lea rdx,[rbp+2E20h] ... call 2C2800
//                     2BEFE3 xor edx,edx / lea rcx,[rbp+2E20h] / call CD6E0
//     -> color 0
//   These are the only callers of each function. Every window above is
//   verified at load.
//
//   Inside those functions every stat reaches D2RCore's renderer (3E7AC0),
//   which for every stat entry calls through its import slots:
//     3E7FD0  call [7043A0] -> 2DB800   composer, damage stats 17..59
//     3E80AC  call [704400] -> 2D6330   ranged writer, line buffer = arg 7
//     3E80F7  call [704400] -> 2D6330
//     3E82E4  call [7043E8] -> 2D6520   single writer, line buffer = arg 6
//   The line buffer is 256 bytes on the renderer's stack (zeroed at 3E7FE7,
//   16 x 16 bytes). After a writer returns non-zero, the renderer appends the
//   newline to it (3E8232: strcat_s(line, 100h, newline)) and then appends
//   the line to the block (3E7E5D). So the writers always hand over one whole
//   stat, internal line breaks included, before its newline.
//   Composer lines and lines composed by other plugins on the composer (the
//   bleed line) go straight into the block and never pass the writers.
//
//   D2RLoader's Ctrl stat-ranges view calls the main builder (D2RCore slot
//   704430), so its tooltip gets the fix. For its roll data it renders raw
//   property text through 2DC4B0 directly (D2RCore 379EE0); those renders are
//   outside the functions above and stay byte-identical.
//
// ---------------------------------------------------------------------------
// What this plugin does
// ---------------------------------------------------------------------------
//   1. 2DA0E0 / 2DA310 / 2D98D0 / 2DA510 / 2DC3D0 are wrapped: while one runs,
//      this thread is "inside a stat block" whose color is known (above).
//   2. 2D6520 / 2D6330 are wrapped: inside a stat block, a stat whose text has
//      a color code becomes start marker + text + end marker. The whole stat
//      is wrapped, so a stat spanning several lines keeps its own color flow.
//   3. When a block function returns, the text it added is scanned: a line
//      not written by the stat writers that contains a color code gets an end
//      marker where it ends. When the OUTERMOST block function returns, every
//      end marker becomes the block's color code, obtained by asking the
//      game's colorizer CD6E0 to color a one-character probe with that color,
//      and the start markers are deleted. A marker is exactly as long as a
//      color code, so the block does not grow.
//   Markers never leave these hooks. Stats without color codes, and every
//   other text in the game, are left byte for byte as before.
//
//   CD6E0 is only CALLED, never patched, and it is verified by windows in its
//   body, not at its entry. The monster display calls it and checks its entry
//   bytes at load; patching it (version 1.0.0 did) made that check fail
//   whenever this plugin loaded first.
//
// Hooks, all on whole-instruction prologues:
//   2D6520  push rbp..r15 / lea rbp,[rsp-988h]
//   2D6330  push rsi,rdi,r12,r15 / sub rsp,88h
//   2DA0E0  push rbx,rbp,rsi,rdi,r13,r14,r15 / sub rsp,480h
//   2D98D0  push rbx,rbp,r12,r15 / sub rsp,78h
//   2DA510  push rbx,rsi,rdi / sub rsp,460h
//   2DC3D0  mov [rsp+8],rbx / mov [rsp+10h],rsi / push rdi / sub rsp,50h
//   2DA310  test rdx,rdx / je 2DA509 / mov [rsp+20h],r9d / mov [rsp+18h],r8
//   The first six go in one loader transaction: all of them or none, so the
//   gem and rune lines can never be resolved by 2DA0E0 alone (it would pick
//   the properties color). 2DA310 opens with a conditional jump the hook
//   engine has to relocate, so it is a second transaction; if only it fails,
//   partial set bonus lines keep the old behavior and nothing else changes.

#include <D2RLPlugin/api.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "color_scope.h"

namespace CelestialRayOne::TooltipColorScope {
namespace {

// ---------------------------------------------------------------------------
// Addresses and witnesses
// ---------------------------------------------------------------------------

// Hooked
constexpr std::uint64_t WriteSingleRva     = 0x2D6520;
constexpr std::uint64_t WriteRangeRva      = 0x2D6330;
constexpr std::uint64_t PropertiesBlockRva = 0x2DA0E0;
constexpr std::uint64_t FullSetBlockRva    = 0x2D98D0;
constexpr std::uint64_t PartialSetBlockRva = 0x2DA310;
constexpr std::uint64_t RuneLinesRva       = 0x2DA510;
constexpr std::uint64_t GemLinesRva        = 0x2DC3D0;
// Called
constexpr std::uint64_t ColorizeRva        = 0x0CD6E0;

// Block colors given by the builders (see the header).
constexpr char PropertiesColor = 3;
constexpr char PartialSetColor = 2;
constexpr char FullSetColor    = 4;
constexpr char GemRuneColor    = 0;

// --- CD6E0, the block colorizer. Called, never hooked: its entry is not
// checked, so a plugin that hooks it does not stop this one.
constexpr std::uint64_t ColorizeEmptyCheckRva = 0x0CD6FF;
// cmp byte [rcx],0 / mov edi,edx / mov rbx,rcx / je CD78F: an empty block is returned uncolored.
constexpr std::uint8_t ColorizeEmptyCheck[]{
    0x80, 0x39, 0x00, 0x8B, 0xFA, 0x48, 0x8B, 0xD9, 0x0F, 0x84, 0x82, 0x00, 0x00, 0x00,
};
constexpr std::uint64_t ColorizeKeyRva = 0x0CD70D;
// lea rax,"colorcode" / mov qword [rsp+30h],9: the escape comes from the "colorcode" key.
constexpr std::uint8_t ColorizeKey[]{
    0x48, 0x8D, 0x05, 0x9C, 0x53, 0xBF, 0x01, 0x48, 0xC7, 0x44, 0x24, 0x30, 0x09, 0x00, 0x00, 0x00,
};
constexpr std::uint64_t ColorizeDigitRva = 0x0CD73E;
// add dil,30h: the color character is '0' + color.
constexpr std::uint8_t ColorizeDigit[]{
    0x40, 0x80, 0xC7, 0x30,
};
constexpr std::uint64_t ColorizeCopyBackRva = 0x0CD77D;
// strlcpy(block, 400h, code + block): the block is a 1024-byte buffer.
constexpr std::uint8_t ColorizeCopyBack[]{
    0x4C, 0x8D, 0x44, 0x24, 0x40, 0xBA, 0x00, 0x04, 0x00, 0x00, 0x48, 0x8B, 0xCB, 0xE8, 0xB1, 0xA0,
    0x15, 0x01,
};

// --- Stat writers. Hooked.
// 2D6520: push rbp,rbx,rsi,rdi,r12,r13,r14,r15 / lea rbp,[rsp-988h].
constexpr std::uint8_t WriteSinglePrologue[]{
    0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8D, 0xAC,
    0x24, 0x78, 0xF6, 0xFF, 0xFF,
};
constexpr std::uint64_t WriteSingleFrameRva = 0x2D6535;
// sub rsp,0A88h: frame of the single writer.
constexpr std::uint8_t WriteSingleFrame[]{
    0x48, 0x81, 0xEC, 0x88, 0x0A, 0x00, 0x00,
};
// 2D6330: push rsi,rdi,r12,r15 / sub rsp,88h.
constexpr std::uint8_t WriteRangePrologue[]{
    0x40, 0x56, 0x57, 0x41, 0x54, 0x41, 0x57, 0x48, 0x81, 0xEC, 0x88, 0x00, 0x00, 0x00,
};
constexpr std::uint64_t WriteRangeLineArgRva = 0x2D634D;
// mov r15,[rsp+0E0h]: r15 = argument 7, the line buffer.
constexpr std::uint8_t WriteRangeLineArg[]{
    0x4C, 0x8B, 0xBC, 0x24, 0xE0, 0x00, 0x00, 0x00,
};

// --- Block functions. Hooked.
// 2DA0E0: push rbx,rbp,rsi,rdi,r13,r14,r15 / sub rsp,480h.
constexpr std::uint8_t PropertiesBlockPrologue[]{
    0x40, 0x53, 0x55, 0x56, 0x57, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x81, 0xEC, 0x80, 0x04,
    0x00, 0x00,
};
constexpr std::uint64_t PropertiesBlockArgsRva = 0x2DA104;
// mov r13,[rsp+4E0h] / mov rsi,rdx / mov r15,[rsp+4E8h] / mov rdi,rcx: rsi = out, rdi = item.
constexpr std::uint8_t PropertiesBlockArgs[]{
    0x4C, 0x8B, 0xAC, 0x24, 0xE0, 0x04, 0x00, 0x00, 0x48, 0x8B, 0xF2, 0x4C, 0x8B, 0xBC, 0x24, 0xE8,
    0x04, 0x00, 0x00, 0x48, 0x8B, 0xF9,
};
// 2D98D0: push rbx,rbp,r12,r15 / sub rsp,78h.
constexpr std::uint8_t FullSetBlockPrologue[]{
    0x40, 0x53, 0x55, 0x41, 0x54, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x78,
};
constexpr std::uint64_t FullSetBlockArgsRva = 0x2D98DB;
// mov rbp,rcx / mov r15d,r9d / mov rcx,rdx / mov r12,r8 / mov rbx,rdx: r12 = out, r15d = capacity.
constexpr std::uint8_t FullSetBlockArgs[]{
    0x48, 0x8B, 0xE9, 0x45, 0x8B, 0xF9, 0x48, 0x8B, 0xCA, 0x4D, 0x8B, 0xE0, 0x48, 0x8B, 0xDA,
};
// 2DA310: test rdx,rdx / je 2DA509 / mov [rsp+20h],r9d / mov [rsp+18h],r8.
constexpr std::uint8_t PartialSetBlockPrologue[]{
    0x48, 0x85, 0xD2, 0x0F, 0x84, 0xF0, 0x01, 0x00, 0x00, 0x44, 0x89, 0x4C, 0x24, 0x20, 0x4C, 0x89,
    0x44, 0x24, 0x18,
};
constexpr std::uint64_t PartialSetBlockArgsRva = 0x2DA323;
// push rbx,rsi,r12,r14 / sub rsp,78h / mov rbx,rcx / mov r14d,r9d / mov rcx,rdx / mov r12,r8 / mov rsi,rdx: r12 = out, r14d = capacity.
constexpr std::uint8_t PartialSetBlockArgs[]{
    0x53, 0x56, 0x41, 0x54, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x78, 0x48, 0x8B, 0xD9, 0x45, 0x8B, 0xF1,
    0x48, 0x8B, 0xCA, 0x4D, 0x8B, 0xE0, 0x48, 0x8B, 0xF2,
};
// 2DA510: push rbx,rsi,rdi / sub rsp,460h.
constexpr std::uint8_t RuneLinesPrologue[]{
    0x40, 0x53, 0x56, 0x57, 0x48, 0x81, 0xEC, 0x60, 0x04, 0x00, 0x00,
};
constexpr std::uint64_t RuneLinesArgsRva = 0x2DA52D;
// mov rbx,[rsp+4A8h] / mov rdi,r9: rbx = argument 6 (prefix), rdi = argument 4 (out).
constexpr std::uint8_t RuneLinesArgs[]{
    0x48, 0x8B, 0x9C, 0x24, 0xA8, 0x04, 0x00, 0x00, 0x49, 0x8B, 0xF9,
};
// 2DC3D0: mov [rsp+8],rbx / mov [rsp+10h],rsi / push rdi / sub rsp,50h.
constexpr std::uint8_t GemLinesPrologue[]{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x50,
};
constexpr std::uint64_t GemLinesArgsRva = 0x2DC3DF;
// xor esi,esi / mov rbx,r9 / mov rdi,rcx: rbx = argument 4 (out), rdi = item.
constexpr std::uint8_t GemLinesArgs[]{
    0x33, 0xF6, 0x49, 0x8B, 0xD9, 0x48, 0x8B, 0xF9,
};
constexpr std::uint64_t GemLinesCapacityRva = 0x2DC400;
// mov r8d,[rsp+80h]: argument 5, the capacity, passed on to 2DA0E0.
constexpr std::uint8_t GemLinesCapacity[]{
    0x44, 0x8B, 0x84, 0x24, 0x80, 0x00, 0x00, 0x00,
};

// --- Builders: which buffer each block function fills and which color the
// builder then gives that buffer.
constexpr std::uint64_t MainPropertiesCallRva = 0x2BE14B;
// 2BD480: lea rdx,[rbp+9F0h] ... mov r8d,400h / mov rcx,rbx ... call 2DA0E0.
constexpr std::uint8_t MainPropertiesCall[]{
    0x48, 0x8D, 0x95, 0xF0, 0x09, 0x00, 0x00, 0x48, 0x8B, 0x45, 0x98, 0x41, 0xB1, 0x01, 0x48, 0x89,
    0x44, 0x24, 0x28, 0x41, 0xB8, 0x00, 0x04, 0x00, 0x00, 0x48, 0x8B, 0xCB, 0x4C, 0x89, 0x64, 0x24,
    0x20, 0xE8, 0x6F, 0xBF, 0x01,
};
constexpr std::uint64_t MainPropertiesColorRva = 0x2BE1E5;
// 2BD480: mov ebx,3.
constexpr std::uint8_t MainPropertiesColor[]{
    0xBB, 0x03, 0x00, 0x00, 0x00,
};
constexpr std::uint64_t MainPropertiesColorCallRva = 0x2BE244;
// 2BD480: mov ebx,1 (after "brokensocketin") / mov edx,ebx / lea rcx,[rbp+9F0h] / call CD6E0.
constexpr std::uint8_t MainPropertiesColorCall[]{
    0xBB, 0x01, 0x00, 0x00, 0x00, 0x8B, 0xD3, 0x48, 0x8D, 0x8D, 0xF0, 0x09, 0x00, 0x00, 0xE8, 0x89,
    0xF4, 0xE0, 0xFF,
};
constexpr std::uint64_t MainGemRuneCallRva = 0x2BDC2A;
// 2BD480: lea rdx,[rbp+3DF0h] / mov rcx,rbx / call 2C2800.
constexpr std::uint8_t MainGemRuneCall[]{
    0x48, 0x8D, 0x95, 0xF0, 0x3D, 0x00, 0x00, 0x48, 0x8B, 0xCB, 0xE8, 0xC7, 0x4B, 0x00,
};
constexpr std::uint64_t MainGemRuneColorRva = 0x2BE38A;
// 2BD480: xor edx,edx / lea rcx,[rbp+3DF0h] / call CD6E0.
constexpr std::uint8_t MainGemRuneColor[]{
    0x33, 0xD2, 0x48, 0x8D, 0x8D, 0xF0, 0x3D, 0x00, 0x00, 0xE8, 0x48, 0xF3, 0xE0, 0xFF,
};
constexpr std::uint64_t SetBlockCallsRva = 0x2BFD33;
// 2BF1B0: 2DA0E0 into [rbp+3610h], 2DA310 into [rbp+3210h], 2D98D0 into [rbp+2E10h], all 400h.
constexpr std::uint8_t SetBlockCalls[]{
    0x48, 0x8D, 0x95, 0x10, 0x36, 0x00, 0x00, 0x48, 0x89, 0x5C, 0x24, 0x28, 0x41, 0xB1, 0x01, 0x41,
    0xB8, 0x00, 0x04, 0x00, 0x00, 0x4C, 0x89, 0x74, 0x24, 0x20, 0x48, 0x8B, 0xCF, 0xE8, 0x8B, 0xA3,
    0x01, 0x00, 0x41, 0xB9, 0x00, 0x04, 0x00, 0x00, 0x4C, 0x8D, 0x85, 0x10, 0x32, 0x00, 0x00, 0x48,
    0x8B, 0xD7, 0x49, 0x8B, 0xCC, 0xE8, 0xA3, 0xA5, 0x01, 0x00, 0x48, 0x8B, 0xCF, 0xE8, 0xEB, 0xAD,
    0x08, 0x00, 0x83, 0xF8, 0x01, 0x75, 0x18, 0x41, 0xB9, 0x00, 0x04, 0x00, 0x00, 0x4C, 0x8D, 0x85,
    0x10, 0x2E, 0x00, 0x00, 0x48, 0x8B, 0xD7, 0x49, 0x8B, 0xCC, 0xE8, 0x3E, 0x9B, 0x01, 0x00,
};
constexpr std::uint64_t SetBlockColorsRva = 0x2C0064;
// 2BF1B0: [rbp+2E10h] color 4, [rbp+3210h] color 2, [rbp+3610h] color 3.
constexpr std::uint8_t SetBlockColors[]{
    0xBA, 0x04, 0x00, 0x00, 0x00, 0x48, 0x8D, 0x8D, 0x10, 0x2E, 0x00, 0x00, 0xE8, 0x6B, 0xD6, 0xE0,
    0xFF, 0x48, 0x8B, 0xD0, 0x48, 0x8D, 0x4D, 0xC0, 0xE8, 0xCF, 0x8A, 0xF2, 0xFF, 0x48, 0x8B, 0xD6,
    0x48, 0x8D, 0x4D, 0xC0, 0xE8, 0xC3, 0x8A, 0xF2, 0xFF, 0x80, 0xBD, 0x10, 0x32, 0x00, 0x00, 0x00,
    0x74, 0x1D, 0xBA, 0x02, 0x00, 0x00, 0x00, 0x48, 0x8D, 0x8D, 0x10, 0x32, 0x00, 0x00, 0xE8, 0x39,
    0xD6, 0xE0, 0xFF, 0x48, 0x8B, 0xD0, 0x48, 0x8D, 0x4D, 0xC0, 0xE8, 0x9D, 0x8A, 0xF2, 0xFF, 0xBA,
    0x03, 0x00, 0x00, 0x00, 0x48, 0x8D, 0x8D, 0x10, 0x36, 0x00, 0x00, 0xE8, 0x1C, 0xD6, 0xE0, 0xFF,
};
constexpr std::uint64_t OtherGemRuneCallRva = 0x2BEC19;
// 2BE560: lea rdx,[rbp+2E20h] / mov rcx,rsi / call 2C2800.
constexpr std::uint8_t OtherGemRuneCall[]{
    0x48, 0x8D, 0x95, 0x20, 0x2E, 0x00, 0x00, 0x48, 0x8B, 0xCE, 0xE8, 0xD8, 0x3B, 0x00, 0x00,
};
constexpr std::uint64_t OtherGemRuneColorRva = 0x2BEFE3;
// 2BE560: xor edx,edx / lea rcx,[rbp+2E20h] / call CD6E0.
constexpr std::uint8_t OtherGemRuneColor[]{
    0x33, 0xD2, 0x48, 0x8D, 0x8D, 0x20, 0x2E, 0x00, 0x00, 0xE8, 0xEF, 0xE6, 0xE0, 0xFF,
};

// ---------------------------------------------------------------------------
// Sizes
// ---------------------------------------------------------------------------

constexpr std::size_t   LineCapacity     = 0x100;  // renderer line buffer, see 3E7FE7 / 3E8232
constexpr std::size_t   LineReserve      = 16;     // left free for the renderer's newline
constexpr std::size_t   ProbeCapacity    = 0x400;  // CD6E0 copies back with 400h
constexpr std::size_t   MaxBlockCode     = 16;
constexpr std::uint32_t MaxScopeCapacity = 0x10000;
constexpr char          ProbeCharacter   = 'x';
constexpr std::size_t   MaxConfigBytes   = 16 * 1024;

// ---------------------------------------------------------------------------
// Game function types. Every argument is passed as a full 64-bit value so the
// hooks hand the original exactly what they received.
// ---------------------------------------------------------------------------

using ColorizeFn = char*(__fastcall*)(char* block, std::uint64_t color) noexcept;
using WriteSingleFn = std::uint64_t(__fastcall*)(void* unit, void* record, std::uint64_t value,
    std::uint64_t layer, std::uint64_t useGroup, char* line, std::uint64_t mode) noexcept;
using WriteRangeFn = std::uint64_t(__fastcall*)(void* unit, void* statList, std::uint64_t statId,
    std::uint64_t layer, std::uint64_t valueA, std::uint64_t valueB, char* line,
    std::uint64_t mode) noexcept;
using PropertiesBlockFn = std::uint64_t(__fastcall*)(void* item, char* out, std::uint64_t capacity,
    std::uint64_t newlineMode, const char* prefix, void* argument6, void* argument7) noexcept;
using SetBlockFn = std::uint64_t(__fastcall*)(void* player, void* item, char* out,
    std::uint64_t capacity) noexcept;
using SocketLinesFn = std::uint64_t(__fastcall*)(void* item, std::uint64_t argument2,
    std::uint64_t argument3, char* out, std::uint64_t capacity, const char* prefix) noexcept;

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

const D2RL::PluginContext* Context = nullptr;
std::uintptr_t             Base    = 0;

ColorizeFn        Colorize                = nullptr;
WriteSingleFn     OriginalWriteSingle     = nullptr;
WriteRangeFn      OriginalWriteRange      = nullptr;
PropertiesBlockFn OriginalPropertiesBlock = nullptr;
SetBlockFn        OriginalFullSetBlock    = nullptr;
SetBlockFn        OriginalPartialSetBlock = nullptr;
SocketLinesFn     OriginalRuneLines       = nullptr;
SocketLinesFn     OriginalGemLines        = nullptr;

// Per thread: how many block functions are running, how many of them could
// not take markers (their buffer was not usable), and whether markers were
// written since the outermost one started. Markers are only ever written
// while BlockDepth > 0 and UnusableBlocks == 0, so the outermost block
// function is always able to resolve them.
thread_local std::uint32_t BlockDepth     = 0;
thread_local std::uint32_t UnusableBlocks = 0;
thread_local bool          MarkersWritten = false;

enum class PartState { NotLoaded, DisabledByConfig, Armed, Failed };
PartState CoreState       = PartState::NotLoaded;
PartState PartialSetState = PartState::NotLoaded;

std::atomic<std::uint64_t> StatsScoped{0};
std::atomic<std::uint64_t> LinesClosed{0};
std::atomic<std::uint64_t> BlocksResolved{0};
std::atomic<std::uint64_t> ColorsRestored{0};
std::atomic<std::uint64_t> NoRoomSkips{0};
std::atomic<std::uint64_t> UnknownCodeBlocks{0};

struct Configuration {
    bool enabled = true;
};
Configuration Settings{};
std::string   ConfigProblems;

// ---------------------------------------------------------------------------
// Default configuration, written on first run
// ---------------------------------------------------------------------------

constexpr char DefaultConfigToml[] =
    "# Tooltip Color Scope\n"
    "#\n"
    "# A color code inside one item stat's text no longer recolors the other stat\n"
    "# lines of the tooltip.\n"
    "#\n"
    "# Why it happens in the unmodified game:\n"
    "#   The tooltip is built bottom to top, and each block of stat lines is\n"
    "#   colored once, by a single color code in front of the whole block: blue\n"
    "#   for magic properties, green for partial set bonuses, gold for full set\n"
    "#   bonuses, white for gem and rune effect lines. The text renderer keeps the\n"
    "#   current color from one line to the next, so a color code inside one stat\n"
    "#   stays active for every stat line above it on screen.\n"
    "#   No single closing code in a string can be right for every block, because\n"
    "#   the same stat can land in blocks of different colors.\n"
    "#\n"
    "# What this plugin does:\n"
    "#   Right after every stat whose text contains a color code, the block's own\n"
    "#   color is put back. Each stat's color codes now only affect that stat's own\n"
    "#   text, whatever color its block has.\n"
    "#   - A stat whose text spans several lines keeps its color across its own lines.\n"
    "#   - Stats without color codes are left exactly as the game writes them.\n"
    "#   - A string no longer needs a closing color code at its end; a leftover\n"
    "#     one is harmless.\n"
    "#   - The damage-range lines and lines other plugins add to the stat list\n"
    "#     (the bleed line) are covered too.\n"
    "#   - The game's text colouring function is only called, never patched, so\n"
    "#     plugins that call or check it (the monster display does both) are\n"
    "#     unaffected whatever order the plugins load in.\n"
    "#\n"
    "# Covered blocks: magic properties (socketed items' properties included),\n"
    "# partial set bonuses, full set bonuses, and the gem and rune\n"
    "# \"Weapons:/Armor:/Helms:/Shields:\" lines. D2RLoader's Ctrl ranges view gets\n"
    "# the fix in its tooltip; the raw property text it reads for its roll data is\n"
    "# left exactly as before.\n"
    "#\n"
    "# Console: type colorscope for the status and counters.\n"
    "\n"
    "[color_scope]\n"
    "\n"
    "# Master switch. false installs nothing and leaves every tooltip exactly as\n"
    "# the game builds it.\n"
    "enabled = true\n";

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

auto Trim(std::string text) -> std::string {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

void NoteProblem(const std::string& problem) {
    if (!ConfigProblems.empty()) ConfigProblems += "; ";
    ConfigProblems += problem;
}

void ParseConfig(const std::string& toml) {
    std::string section;
    std::size_t position = 0;
    while (position <= toml.size()) {
        const std::size_t end = toml.find('\n', position);
        std::string line = toml.substr(position, end == std::string::npos ? std::string::npos : end - position);
        position = end == std::string::npos ? toml.size() + 1 : end + 1;

        const std::size_t hash = line.find('#');
        if (hash != std::string::npos) line.erase(hash);
        line = Trim(line);
        if (line.empty()) continue;

        if (line.front() == '[') {
            const std::size_t close = line.find(']');
            section = close == std::string::npos ? std::string{} : Trim(line.substr(1, close - 1));
            continue;
        }
        const std::size_t equals = line.find('=');
        if (equals == std::string::npos) continue;
        const std::string key   = Trim(line.substr(0, equals));
        const std::string value = Trim(line.substr(equals + 1));

        if (section == "color_scope" && key == "enabled") {
            if (value == "true") Settings.enabled = true;
            else if (value == "false") Settings.enabled = false;
            else NoteProblem("color_scope.enabled must be true or false, keeping true");
        }
    }
}

void ReadConfiguration() {
    if (!Context->EnsureConfig(DefaultConfigToml)) {
        Context->LogWarn("Tooltip Color Scope: config file could not be created; using defaults.");
    } else {
        std::string buffer(MaxConfigBytes, '\0');
        if (Context->ReadConfig(buffer.data(), static_cast<std::uint32_t>(buffer.size()), nullptr)) {
            buffer.resize(std::strlen(buffer.c_str()));
            ParseConfig(buffer);
        } else {
            Context->LogWarn("Tooltip Color Scope: config file could not be read; using defaults.");
        }
    }
    if (!ConfigProblems.empty()) {
        D2RL::LogWarnF(Context, "Tooltip Color Scope: %s", ConfigProblems.c_str());
    }
}

// ---------------------------------------------------------------------------
// Block scope
// ---------------------------------------------------------------------------

auto MarkersAllowed() noexcept -> bool {
    return BlockDepth != 0 && UnusableBlocks == 0;
}

// Replaces the markers in text[start..] with the code CD6E0 gives `color`.
void ResolveBlock(char* text, std::size_t capacity, std::size_t start, char color) noexcept {
    char probe[ProbeCapacity]{ProbeCharacter};
    Colorize(probe, static_cast<std::uint64_t>(static_cast<unsigned char>(color)));
    const std::size_t codeLength =
        Text::ExtractBlockCode(probe, sizeof(probe), ProbeCharacter, MaxBlockCode);
    if (codeLength == 0) UnknownCodeBlocks.fetch_add(1, std::memory_order_relaxed);

    const Text::ResolveResult result = Text::ResolveMarkers(text, capacity, start, probe, codeLength);
    BlocksResolved.fetch_add(1, std::memory_order_relaxed);
    if (result.restored != 0) ColorsRestored.fetch_add(result.restored, std::memory_order_relaxed);
    if (result.dropped != 0) NoRoomSkips.fetch_add(result.dropped, std::memory_order_relaxed);
}

// One running block function. On the way out it closes every colored line the
// function added that the stat writers did not produce; the outermost one
// then resolves every marker in the text it added.
class BlockScope {
public:
    BlockScope(char* out, std::uint64_t capacity, char color) noexcept : out_(out), color_(color) {
        const auto bytes = static_cast<std::uint32_t>(capacity);
        if (out != nullptr && bytes != 0 && bytes <= MaxScopeCapacity) {
            const std::size_t length = Text::BoundedLength(out, bytes);
            if (length < bytes) {
                capacity_ = bytes;
                start_    = length;
            }
        }
        if (capacity_ == 0) ++UnusableBlocks;
        if (BlockDepth == 0) MarkersWritten = false;
        ++BlockDepth;
    }

    ~BlockScope() noexcept {
        const bool allowed = MarkersAllowed();
        --BlockDepth;
        if (capacity_ == 0) {
            --UnusableBlocks;
            return;
        }
        if (!allowed) return;

        const Text::CloseResult closed = Text::CloseLines(out_, capacity_, start_);
        if (closed.closed != 0) {
            MarkersWritten = true;
            LinesClosed.fetch_add(closed.closed, std::memory_order_relaxed);
        }
        if (closed.noRoom != 0) NoRoomSkips.fetch_add(closed.noRoom, std::memory_order_relaxed);

        if (BlockDepth != 0 || !MarkersWritten) return;  // an enclosing block function resolves
        ResolveBlock(out_, capacity_, start_, color_);
        MarkersWritten = false;
    }

    BlockScope(const BlockScope&) = delete;
    auto operator=(const BlockScope&) -> BlockScope& = delete;

private:
    char*       out_      = nullptr;
    std::size_t capacity_ = 0;
    std::size_t start_    = 0;
    char        color_    = 0;
};

// ---------------------------------------------------------------------------
// Hooks
// ---------------------------------------------------------------------------

void ScopeStatLine(char* line) noexcept {
    if (line == nullptr || !MarkersAllowed()) return;
    switch (Text::WrapStat(line, LineCapacity, LineReserve)) {
    case Text::WrapResult::Wrapped:
        MarkersWritten = true;
        StatsScoped.fetch_add(1, std::memory_order_relaxed);
        break;
    case Text::WrapResult::NoRoom:
        NoRoomSkips.fetch_add(1, std::memory_order_relaxed);
        break;
    case Text::WrapResult::Untouched:
        break;
    }
}

auto __fastcall HookWriteSingle(void* unit, void* record, std::uint64_t value, std::uint64_t layer,
        std::uint64_t useGroup, char* line, std::uint64_t mode) noexcept -> std::uint64_t {
    if (OriginalWriteSingle == nullptr) return 0;
    const std::uint64_t written = OriginalWriteSingle(unit, record, value, layer, useGroup, line, mode);
    // The renderer tests al (3E82EA).
    if ((written & 0xFF) != 0) ScopeStatLine(line);
    return written;
}

auto __fastcall HookWriteRange(void* unit, void* statList, std::uint64_t statId, std::uint64_t layer,
        std::uint64_t valueA, std::uint64_t valueB, char* line, std::uint64_t mode) noexcept
        -> std::uint64_t {
    if (OriginalWriteRange == nullptr) return 0;
    const std::uint64_t written =
        OriginalWriteRange(unit, statList, statId, layer, valueA, valueB, line, mode);
    // The renderer tests eax (3E80B2).
    if (static_cast<std::uint32_t>(written) != 0) ScopeStatLine(line);
    return written;
}

auto __fastcall HookPropertiesBlock(void* item, char* out, std::uint64_t capacity,
        std::uint64_t newlineMode, const char* prefix, void* argument6, void* argument7) noexcept
        -> std::uint64_t {
    if (OriginalPropertiesBlock == nullptr) return 0;
    BlockScope scope(out, capacity, PropertiesColor);
    return OriginalPropertiesBlock(item, out, capacity, newlineMode, prefix, argument6, argument7);
}

auto __fastcall HookFullSetBlock(void* player, void* item, char* out, std::uint64_t capacity) noexcept
        -> std::uint64_t {
    if (OriginalFullSetBlock == nullptr) return 0;
    BlockScope scope(out, capacity, FullSetColor);
    return OriginalFullSetBlock(player, item, out, capacity);
}

auto __fastcall HookPartialSetBlock(void* player, void* item, char* out, std::uint64_t capacity) noexcept
        -> std::uint64_t {
    if (OriginalPartialSetBlock == nullptr) return 0;
    BlockScope scope(out, capacity, PartialSetColor);
    return OriginalPartialSetBlock(player, item, out, capacity);
}

auto __fastcall HookRuneLines(void* item, std::uint64_t argument2, std::uint64_t argument3, char* out,
        std::uint64_t capacity, const char* prefix) noexcept -> std::uint64_t {
    if (OriginalRuneLines == nullptr) return 0;
    BlockScope scope(out, capacity, GemRuneColor);
    return OriginalRuneLines(item, argument2, argument3, out, capacity, prefix);
}

auto __fastcall HookGemLines(void* item, std::uint64_t argument2, std::uint64_t argument3, char* out,
        std::uint64_t capacity, const char* prefix) noexcept -> std::uint64_t {
    if (OriginalGemLines == nullptr) return 0;
    BlockScope scope(out, capacity, GemRuneColor);
    return OriginalGemLines(item, argument2, argument3, out, capacity, prefix);
}

// ---------------------------------------------------------------------------
// Verification and installation
// ---------------------------------------------------------------------------

auto Verify(std::uint64_t rva, const std::uint8_t* expected, std::size_t size, const char* label) noexcept
        -> bool {
    if (Context->CheckExpectedBytes(rva, expected, static_cast<std::uint32_t>(size))) return true;
    D2RL::LogErrorF(Context,
        "Tooltip Color Scope: %s at 0x%llX does not match the verified D2R image, or another "
        "plugin already owns it.",
        label, static_cast<unsigned long long>(rva));
    return false;
}

#define TCS_VERIFY(rva, bytes, label) Verify(rva, bytes, sizeof(bytes), label)

auto VerifyCoreContract() noexcept -> bool {
    return TCS_VERIFY(ColorizeEmptyCheckRva, ColorizeEmptyCheck, "the colorizer empty-block check")
        && TCS_VERIFY(ColorizeKeyRva, ColorizeKey, "the colorizer escape key")
        && TCS_VERIFY(ColorizeDigitRva, ColorizeDigit, "the colorizer color digit")
        && TCS_VERIFY(ColorizeCopyBackRva, ColorizeCopyBack, "the colorizer copy-back")
        && TCS_VERIFY(WriteSingleRva, WriteSinglePrologue, "the single-value stat writer")
        && TCS_VERIFY(WriteSingleFrameRva, WriteSingleFrame, "the single-value stat writer frame")
        && TCS_VERIFY(WriteRangeRva, WriteRangePrologue, "the ranged stat writer")
        && TCS_VERIFY(WriteRangeLineArgRva, WriteRangeLineArg, "the ranged stat writer line argument")
        && TCS_VERIFY(PropertiesBlockRva, PropertiesBlockPrologue, "the properties block function")
        && TCS_VERIFY(PropertiesBlockArgsRva, PropertiesBlockArgs, "the properties block arguments")
        && TCS_VERIFY(FullSetBlockRva, FullSetBlockPrologue, "the full set bonus function")
        && TCS_VERIFY(FullSetBlockArgsRva, FullSetBlockArgs, "the full set bonus arguments")
        && TCS_VERIFY(RuneLinesRva, RuneLinesPrologue, "the rune lines function")
        && TCS_VERIFY(RuneLinesArgsRva, RuneLinesArgs, "the rune lines arguments")
        && TCS_VERIFY(GemLinesRva, GemLinesPrologue, "the gem lines function")
        && TCS_VERIFY(GemLinesArgsRva, GemLinesArgs, "the gem lines arguments")
        && TCS_VERIFY(GemLinesCapacityRva, GemLinesCapacity, "the gem lines capacity")
        && TCS_VERIFY(MainPropertiesCallRva, MainPropertiesCall, "the main builder's properties call")
        && TCS_VERIFY(MainPropertiesColorRva, MainPropertiesColor, "the main builder's properties color")
        && TCS_VERIFY(MainPropertiesColorCallRva, MainPropertiesColorCall,
               "the main builder's properties coloring")
        && TCS_VERIFY(MainGemRuneCallRva, MainGemRuneCall, "the main builder's gem and rune call")
        && TCS_VERIFY(MainGemRuneColorRva, MainGemRuneColor, "the main builder's gem and rune coloring")
        && TCS_VERIFY(SetBlockCallsRva, SetBlockCalls, "the set builder's block calls")
        && TCS_VERIFY(SetBlockColorsRva, SetBlockColors, "the set builder's block colors")
        && TCS_VERIFY(OtherGemRuneCallRva, OtherGemRuneCall, "the third builder's gem and rune call")
        && TCS_VERIFY(OtherGemRuneColorRva, OtherGemRuneColor,
               "the third builder's gem and rune coloring");
}

auto VerifyPartialSetContract() noexcept -> bool {
    return TCS_VERIFY(PartialSetBlockRva, PartialSetBlockPrologue, "the partial set bonus function")
        && TCS_VERIFY(PartialSetBlockArgsRva, PartialSetBlockArgs, "the partial set bonus arguments");
}

#undef TCS_VERIFY

struct HookSpec {
    const char*         label;
    std::uint64_t       rva;
    const std::uint8_t* expected;
    std::uint32_t       expectedSize;
    void*               hook;
    void**              original;
};

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

enum class InstallResult {
    Installed,         // every hook live with its call-through
    NotApplied,        // the loader applied nothing from this group
    MissingCallThrough // applied, but a hook has no call-through: unload the plugin
};

// Installs every hook of `specs` in one loader transaction: all of them or
// none. The originals are published only after the commit succeeded.
auto InstallTransaction(const HookSpec* specs, std::size_t count, const char* group) noexcept
        -> InstallResult {
    constexpr std::size_t MaxHooks = 8;
    if (count == 0 || count > MaxHooks) return InstallResult::NotApplied;

    const D2RL::MutationService* mutations = nullptr;
    if (Context->QueryService(&mutations) != D2RL::ServiceQueryResult::Success
            || !D2RL::HasMutationServiceField(mutations, D2RL::MutationServiceRequiredSize)) {
        D2RL::LogErrorF(Context, "Tooltip Color Scope: %s: the loader's mutation service is unavailable.",
            group);
        return InstallResult::NotApplied;
    }

    D2RL::Mutations::TransactionHandle transaction = D2RL::Mutations::InvalidTransactionHandle;
    D2RL::Mutations::Result            status      = mutations->beginTransaction(Context, &transaction);
    if (status != D2RL::Mutations::Result::Success) {
        D2RL::LogErrorF(Context, "Tooltip Color Scope: %s: could not start a transaction (%s).", group,
            MutationResultName(status));
        return InstallResult::NotApplied;
    }

    D2RL::Mutations::OperationHandle operations[MaxHooks]{};
    for (std::size_t i = 0; i < count; ++i) {
        const D2RL::Mutations::InlineHookRequest request{
            .structSize   = D2RL::Mutations::InlineHookRequestSize,
            .flags        = 0,
            .rva          = specs[i].rva,
            .expected     = specs[i].expected,
            .expectedSize = specs[i].expectedSize,
            .reserved     = 0,
            .target       = specs[i].hook,
            .reserved2    = 0,
        };
        status = mutations->stageInlineHook(Context, transaction, &request, &operations[i]);
        if (status != D2RL::Mutations::Result::Success) {
            D2RL::LogErrorF(Context, "Tooltip Color Scope: %s: staging the hook on %s at 0x%llX failed (%s).",
                group, specs[i].label, static_cast<unsigned long long>(specs[i].rva),
                MutationResultName(status));
            mutations->cancelTransaction(Context, transaction);
            return InstallResult::NotApplied;
        }
    }

    D2RL::Mutations::CommitResult commit{};
    commit.structSize = D2RL::Mutations::CommitResultSize;
    status = mutations->commit(Context, transaction, &commit);
    if (status != D2RL::Mutations::Result::Success || commit.result != D2RL::Mutations::Result::Success) {
        const char* failed = "an unknown hook";
        for (std::size_t i = 0; i < count; ++i) {
            if (operations[i] == commit.operation) failed = specs[i].label;
        }
        D2RL::LogErrorF(Context,
            "Tooltip Color Scope: %s: the loader refused the hooks at %s (%s, %s). Nothing from this "
            "group was applied.",
            group, failed, MutationResultName(commit.result), FailureReasonName(commit.failureReason));
        mutations->cancelTransaction(Context, transaction);
        return InstallResult::NotApplied;
    }

    InstallResult result = InstallResult::Installed;
    for (std::size_t i = 0; i < count; ++i) {
        void* original = nullptr;
        status = mutations->getInlineHookOriginal(Context, transaction, operations[i], &original);
        if (status != D2RL::Mutations::Result::Success || original == nullptr) {
            D2RL::LogErrorF(Context, "Tooltip Color Scope: %s: the loader gave no call-through for %s (%s).",
                group, specs[i].label, MutationResultName(status));
            result = InstallResult::MissingCallThrough;
            continue;
        }
        *specs[i].original = original;
    }
    return result;
}

auto InstallCore() noexcept -> InstallResult {
    const HookSpec specs[]{
        {"the single-value stat writer", WriteSingleRva, WriteSinglePrologue,
            sizeof(WriteSinglePrologue), reinterpret_cast<void*>(&HookWriteSingle),
            reinterpret_cast<void**>(&OriginalWriteSingle)},
        {"the ranged stat writer", WriteRangeRva, WriteRangePrologue, sizeof(WriteRangePrologue),
            reinterpret_cast<void*>(&HookWriteRange), reinterpret_cast<void**>(&OriginalWriteRange)},
        {"the properties block function", PropertiesBlockRva, PropertiesBlockPrologue,
            sizeof(PropertiesBlockPrologue), reinterpret_cast<void*>(&HookPropertiesBlock),
            reinterpret_cast<void**>(&OriginalPropertiesBlock)},
        {"the full set bonus function", FullSetBlockRva, FullSetBlockPrologue,
            sizeof(FullSetBlockPrologue), reinterpret_cast<void*>(&HookFullSetBlock),
            reinterpret_cast<void**>(&OriginalFullSetBlock)},
        {"the rune lines function", RuneLinesRva, RuneLinesPrologue, sizeof(RuneLinesPrologue),
            reinterpret_cast<void*>(&HookRuneLines), reinterpret_cast<void**>(&OriginalRuneLines)},
        {"the gem lines function", GemLinesRva, GemLinesPrologue, sizeof(GemLinesPrologue),
            reinterpret_cast<void*>(&HookGemLines), reinterpret_cast<void**>(&OriginalGemLines)},
    };
    return InstallTransaction(specs, sizeof(specs) / sizeof(specs[0]), "core hooks");
}

auto InstallPartialSet() noexcept -> InstallResult {
    const HookSpec specs[]{
        {"the partial set bonus function", PartialSetBlockRva, PartialSetBlockPrologue,
            sizeof(PartialSetBlockPrologue), reinterpret_cast<void*>(&HookPartialSetBlock),
            reinterpret_cast<void**>(&OriginalPartialSetBlock)},
    };
    return InstallTransaction(specs, 1, "partial set bonus hook");
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

auto __cdecl StatusCommand(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command, void*) noexcept
        -> D2RL::ConsoleCommandResult {
    if (command == nullptr || command->plugin == nullptr) return D2RL::ConsoleCommandResult::Failed;

    char message[512];
    std::snprintf(message, sizeof(message),
        "Tooltip Color Scope 1.1.0: blocks %s, partial set bonuses %s | stats scoped %llu, lines "
        "closed %llu, blocks resolved %llu, colors restored %llu, skipped for room %llu, unknown "
        "block code %llu",
        StateName(CoreState), StateName(PartialSetState),
        static_cast<unsigned long long>(StatsScoped.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(LinesClosed.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(BlocksResolved.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(ColorsRestored.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(NoRoomSkips.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(UnknownCodeBlocks.load(std::memory_order_relaxed)));
    command->plugin->WriteConsoleMessage(message);
    return D2RL::ConsoleCommandResult::Handled;
}

void RegisterStatusCommand() noexcept {
    if (!Context->RegisterConsoleCommand("colorscope", &StatusCommand,
            "Reports Tooltip Color Scope status and counters.")) {
        Context->LogWarn("Tooltip Color Scope: the status console command was refused.");
    }
}

constexpr D2RL::PluginInfo Info{
    .infoSize    = D2RL::PluginInfoSize,
    .abiVersion  = D2RL_PLUGIN_ABI_VERSION,
    .id          = "celestialrayone.tooltip-color-scope",
    .name        = "Tooltip Color Scope",
    .version     = "1.1.0",
    .author      = "CelestialRayOne",
    .description =
        "A color code inside one item stat's text no longer recolors the other stat lines of "
        "the tooltip; the block's own color is put back after every colored stat.",
    .flags = D2RL::PluginFlags::Client | D2RL::PluginFlags::NativeHooks,
};

}  // namespace

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* {
    return &Info;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
    if (!D2RL::HasContext(context)) return false;
    Context = context;
    Base    = context->exeBase;
    if (Base == 0) return false;
    Colorize = reinterpret_cast<ColorizeFn>(Base + ColorizeRva);

    ReadConfiguration();

    if (!Settings.enabled) {
        CoreState       = PartState::DisabledByConfig;
        PartialSetState = PartState::DisabledByConfig;
        Context->LogInfo("Tooltip Color Scope: disabled by config; nothing installed.");
        RegisterStatusCommand();
        return true;
    }

    // Returning false makes D2RLoader unload the plugin, which also removes
    // every hook it already applied.
    if (!VerifyCoreContract() || InstallCore() != InstallResult::Installed) {
        CoreState       = PartState::Failed;
        PartialSetState = PartState::Failed;
        Context->LogError("Tooltip Color Scope: the core hooks could not be installed. Refusing to load.");
        return false;
    }
    CoreState = PartState::Armed;

    // The partial set bonus function resolves its own markers, so it does not
    // depend on the core group; it is kept separate because its prologue opens
    // with a conditional jump the hook engine has to relocate.
    const InstallResult partial =
        VerifyPartialSetContract() ? InstallPartialSet() : InstallResult::NotApplied;
    if (partial == InstallResult::MissingCallThrough) {
        Context->LogError("Tooltip Color Scope: the partial set bonus hook has no call-through. "
                          "Refusing to load.");
        return false;
    }
    PartialSetState = partial == InstallResult::Installed ? PartState::Armed : PartState::Failed;
    if (PartialSetState == PartState::Failed) {
        Context->LogWarn("Tooltip Color Scope: partial set bonus lines keep the game's behavior; "
                         "every other block is fixed.");
    }

    D2RL::LogInfoF(Context, "Tooltip Color Scope: blocks %s, partial set bonuses %s.",
        StateName(CoreState), StateName(PartialSetState));
    RegisterStatusCommand();
    return true;
}

}  // namespace CelestialRayOne::TooltipColorScope
