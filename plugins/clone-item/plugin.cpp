// Cube Clone Item
//
// Adds a cubemain.txt output code, cloneitem. The output is an exact duplicate
// of the item matched by input 1: same base item, quality, affixes, rolled
// values, sockets and socketed items. Nothing is rolled.
//
// Port of the ESR D2R 2.4 cloneitem memory patches (parser hook 2618FC,
// generator dispatch 307645, clone hook 307796, output store 3077A5, cave
// 3632F6) to D2R 3.3. Everything below was read out of the live D2RLoader.exe
// image and disassembled before being relied on. The RuffnecKk corpus
// documents this image as identical for 3.2.92777 and 3.3.93847.
//
// ---------------------------------------------------------------------------
// The copy: sub_14043D660 (game, sourceItem) -> item
// ---------------------------------------------------------------------------
//   The game's own exact item copy, successor of 2.4 sub_1402F83D0. It
//   serializes the source into a 1024-byte buffer through
//   ITEMS_SerializeItemToBitstream 0x375EE0, reads the header through
//   ITEMS_ReadItemBitstreamMetadata 0x374FF0, decodes a new item through
//   sub_14043D900 and repeats that for every socketed child. The stored stat
//   lists are rebuilt verbatim, so nothing depends on a roll or on the drop
//   context. Its only native caller is 0x53E477 (rcx game, rdx item). r8 is
//   overwritten at 43D6B2 before any read, so it takes two arguments.
//
//     43D689  48 8B E9                mov   rbp, rcx         ; game
//     43D68C  48 89 54 24 50          mov   [rsp+50h], rdx   ; source item
//     43D6B2  41 B8 00 04 00 00       mov   r8d, 400h        ; buffer size
//     43D6D5  E8 06 88 F3 FF          call  375EE0           ; serialize
//
// ---------------------------------------------------------------------------
// 1. Parser: sub_1403E5E60 (dataContext, output, cell, row)
// ---------------------------------------------------------------------------
//   Parses one output, output b or output c cell into a 0x59-byte output
//   record. The TXT field callback sub_1403E5E20 tail-jumps here with
//   output = recipe + 4Ch + index * 59h. The output type is the byte at +0Dh:
//   usetype FFh, useitem FEh, itemtype FDh, item FCh, portals 1 to 4. FBh is
//   unused, so cloneitem keeps its 2.4 value.
//
//     3E5FB0  0F B6 04 0B             movzx eax, byte ptr [rbx+rcx]  ; inlined
//     ...                                                     ; "useitem" compare
//     3E5FC7  41 C6 47 0D FE          mov   byte ptr [r15+0Dh], 0FEh
//     3E5FD1  E9 C2 01 00 00          jmp   3E6198                  ; modifiers
//
//   The 2.4 strcmp call site is gone: 3.3 inlines every code compare. An
//   inline hook on the entry recognises cloneitem, hands the native parser a
//   private copy of the cell with the code spelled useitem, then sets the type
//   to FBh. Every modifier after the first comma is parsed by native code,
//   exactly as for useitem. The 2.4 cave did the same: match, type FBh, then
//   the shared modifier tail.
//
// ---------------------------------------------------------------------------
// 2. Input binding: sub_140529D50, run per input by the matcher sub_140529090
// ---------------------------------------------------------------------------
//   For input 1 only, a three-pass loop fills the 16-byte bound entry of
//   every output: item, class id, item level. The item is stored only when
//   output (A) carries mod or is usetype or useitem. r13 is the recipe and
//   never advances, so all three passes test output (A)'s type.
//
//     52A79D  41 BF 03 00 00 00       mov   r15d, 3
//     52A7B2  41 F6 45 50 01          test  byte ptr [r13+50h], 1     ; mod
//     52A7B7  74 04                   je    52A7BD
//     52A7B9  4C 89 77 F8             mov   [rdi-8], r14            ; bind
//     52A7BD  41 0F B6 45 59          movzx eax, byte ptr [r13+59h]  ; <- hook
//     52A7C2  3C FF                   cmp   al, 0FFh                ; usetype
//     52A7C4  75 2E                   jne   52A7F4
//     ...
//     52A7F4  3C FE                   cmp   al, 0FEh                ; useitem
//     52A7F6  75 6E                   jne   52A866                  ; unbound
//     52A7F8  4C 89 77 F8             mov   [rdi-8], r14            ; bind
//     52A7FC  C7 07 FF FF FF FF       mov   dword ptr [rdi], -1
//     52A802  EB DD                   jmp   52A7E1
//
//   Without this hook a cloneitem in output (A) never receives an item. The
//   five-byte type load becomes a jump to a relay that replays it and reports
//   FBh as FEh to these two compares only, so cloneitem binds exactly like
//   useitem. eax is reloaded at 52A7E1 before anything else reads it.
//
// ---------------------------------------------------------------------------
// 3. Output generator: sub_1405269C0 (game, player, recipe, boundEntries)
// ---------------------------------------------------------------------------
//   Runs output (A), b and c in turn. Its frame sets rbp = rsp + 100h and
//   keeps game at [rbp-80h], player at [rsp+60h], the bound entries at
//   [rsp+68h], the output index at [rsp+54h], the current output base at
//   [rsp+78h] (type at +59h) and the unique row at [rbp-70h]. A type that is
//   not usetype, item or itemtype leaves the switch with esi = 0 and skips
//   the output:
//
//     52745A  45 0F B6 67 56          movzx r12d, byte ptr [r15+56h]
//     527466  8B F1                   mov   esi, ecx                ; 0
//     527468  C7 45 90 FF FF FF FF    mov   dword ptr [rbp-70h], -1
//     527472  3C FF                   cmp   al, 0FFh
//     ...
//     527654  4C 8B 45 80             mov   r8, [rbp-80h]           ; default
//     527658  48 8B 5C 24 78          mov   rbx, [rsp+78h]
//     52765D  E9 BC FE FF FF          jmp   52751E
//     52751C  33 C9                   xor   ecx, ecx
//     52751E  85 F6                   test  esi, esi
//     527520  0F 84 65 03 00 00       je    52788B                  ; <- hook
//     527526  48 8B 74 24 60          mov   rsi, [rsp+60h]          ; build
//
//   The native build stores the new item and carries on:
//
//     527828  44 8B 65 90             mov   r12d, [rbp-70h]
//     527837  E8 14 55 F1 FF          call  43CD50                  ; build
//     52783C  48 63 4C 24 54          movsxd rcx, dword ptr [rsp+54h] ; resume
//     527841  48 8B 5C 24 60          mov   rbx, [rsp+60h]
//     527846  48 89 84 CD 20 01 00 00 mov   [rbp+rcx*8+120h], rax   ; store
//     52784E  4C 8B 7C 24 78          mov   r15, [rsp+78h]
//     527853  44 8B 74 24 58          mov   r14d, [rsp+58h]
//     527858  45 85 E4                test  r12d, r12d
//     52785B  78 3C                   js    527899                  ; r12 = game
//
//   2.4 dispatched on type with cmp al,0FDh / jnz. 3.3 compiled a switch, so
//   the je above is the one place every unrecognised type passes. Only its
//   rel32 changes, to a relay. For type FBh with a bound item the relay calls
//   the copy, loads rsi and r12d exactly as 527526 and 527828 do, and resumes
//   at 52783C. The copy then goes through the native store and every native
//   step after it: uns, rem, the mod columns, rep, rch, sock=, qty=,
//   placement in the Cube and the client packets. r12d is [rbp-70h] = -1,
//   so the unique-row bookkeeping at 52785D stays skipped, as for any output
//   that is not a unique. Any other type, or no bound item, takes the
//   original je target. A null copy is stored as null and skipped natively.
//
// ---------------------------------------------------------------------------
// Relays
// ---------------------------------------------------------------------------
//   Both relays are position independent and live on one page allocated
//   within rel32 reach of the image. Their absolute targets sit in qword
//   slots written at install. Assembled with keystone and checked with
//   capstone: every branch lands on an instruction start inside the relay
//   and every rip-relative load reads its own slot.
//
//   If the plugin unloads, or a later step fails, the copy slot is first
//   pointed straight at the native copy, so nothing that still reaches a
//   relay can land in an unloaded DLL. Then both sites are restored.

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
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

namespace CelestialRayOne::CubeCloneItem {
namespace {

// ---------------------------------------------------------------------------
// Native anchors
// ---------------------------------------------------------------------------

constexpr std::uint64_t CubeOutputParserRva       = 0x3E5E60;
constexpr std::uint64_t ParserUseItemBranchRva    = 0x3E5FA6;
constexpr std::uint64_t ItemCloneRva              = 0x43D660;
constexpr std::uint64_t GeneratorFrameRva         = 0x5269C0;
constexpr std::uint64_t GeneratorUseItemBranchRva = 0x527104;
constexpr std::uint64_t GeneratorSwitchHeadRva    = 0x52745A;
constexpr std::uint64_t GeneratorNoBuildBranchRva = 0x52751C;
constexpr std::uint64_t GeneratorNoBuildJumpRva   = 0x527520;
constexpr std::uint64_t GeneratorDefaultCaseRva   = 0x527654;
constexpr std::uint64_t GeneratorBuildAndStoreRva = 0x527828;
constexpr std::uint64_t GeneratorStoreResumeRva   = 0x52783C;
constexpr std::uint64_t GeneratorNoBuildTargetRva = 0x52788B;
constexpr std::uint64_t BinderLoopRva             = 0x52A799;
constexpr std::uint64_t BinderTypeLoadRva         = 0x52A7BD;
constexpr std::uint64_t BinderTypeCompareRva      = 0x52A7C2;

constexpr std::uint32_t NoBuildJumpSize  = 6;    // 0F 84 rel32
constexpr std::uint32_t BinderJumpSize   = 5;    // E9 rel32
constexpr std::uint32_t ParserHookSize   = 5;    // displaced prologue

constexpr std::uint8_t  CloneItemType    = 0xFB;
constexpr std::uint8_t  UseItemType      = 0xFE;
constexpr std::size_t   OutputTypeOffset = 0x0D;

constexpr std::string_view CloneItemCode = "cloneitem";
constexpr std::string_view UseItemCode   = "useitem";

// ---------------------------------------------------------------------------
// Witnesses, each instruction-aligned and matched in full before any write
// ---------------------------------------------------------------------------

// parser entry, 32 bytes at 0x3E5E60
constexpr std::uint8_t ParserEntryWitness[]{
    0x40, 0x53, 0x56, 0x41, 0x54, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x38, 0x44,
    0x89, 0x0A, 0x4D, 0x8D, 0x60, 0x01, 0x41, 0x80, 0x38, 0x22, 0x4C, 0x8B,
    0xFA, 0x0F, 0xB6, 0xF1, 0x4D, 0x0F, 0x45, 0xE0,
};

// parser useitem branch, 48 bytes at 0x3E5FA6
constexpr std::uint8_t ParserUseItemBranchWitness[]{
    0x33, 0xC9, 0x48, 0x8D, 0x15, 0xC1, 0x85, 0x92, 0x01, 0x90, 0x0F, 0xB6,
    0x04, 0x0B, 0x48, 0xFF, 0xC1, 0x3A, 0x44, 0x0A, 0xFF, 0x75, 0x19, 0x48,
    0x83, 0xF9, 0x08, 0x75, 0xED, 0x40, 0x0F, 0xB6, 0xCE, 0x41, 0xC6, 0x47,
    0x0D, 0xFE, 0xE8, 0xBF, 0xAA, 0xF1, 0xFF, 0xE9, 0xC2, 0x01, 0x00, 0x00,
};

// item copy, 122 bytes at 0x43D660
constexpr std::uint8_t ItemCloneWitness[]{
    0x48, 0x89, 0x5C, 0x24, 0x18, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55,
    0x41, 0x56, 0x41, 0x57, 0x48, 0x81, 0xEC, 0x80, 0x04, 0x00, 0x00, 0x48,
    0x8B, 0x05, 0x4A, 0xDC, 0x58, 0x02, 0x48, 0x33, 0xC4, 0x48, 0x89, 0x84,
    0x24, 0x70, 0x04, 0x00, 0x00, 0x48, 0x8B, 0xE9, 0x48, 0x89, 0x54, 0x24,
    0x50, 0x48, 0x8B, 0xCA, 0x48, 0x8B, 0xFA, 0xE8, 0xA4, 0xDD, 0xF0, 0xFF,
    0x48, 0x8B, 0xD8, 0x48, 0x8D, 0x54, 0x24, 0x70, 0x48, 0x8D, 0x05, 0x65,
    0x91, 0x8D, 0x01, 0x33, 0xF6, 0x48, 0x89, 0x44, 0x24, 0x48, 0x41, 0xB8,
    0x00, 0x04, 0x00, 0x00, 0x48, 0x8D, 0x44, 0x24, 0x48, 0x48, 0x8B, 0xCF,
    0x48, 0x89, 0x44, 0x24, 0x30, 0x89, 0x74, 0x24, 0x28, 0x44, 0x8D, 0x4E,
    0x01, 0xC7, 0x44, 0x24, 0x20, 0x01, 0x00, 0x00, 0x00, 0xE8, 0x06, 0x88,
    0xF3, 0xFF,
};

// generator frame, 88 bytes at 0x5269C0
constexpr std::uint8_t GeneratorFrameWitness[]{
    0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41,
    0x57, 0x48, 0x8D, 0xAC, 0x24, 0x08, 0xEA, 0xFF, 0xFF, 0xB8, 0xF8, 0x16,
    0x00, 0x00, 0xE8, 0x01, 0xA7, 0xDA, 0x00, 0x48, 0x2B, 0xE0, 0x48, 0x8B,
    0x05, 0xDF, 0x48, 0x4A, 0x02, 0x48, 0x33, 0xC4, 0x48, 0x89, 0x85, 0xE0,
    0x15, 0x00, 0x00, 0x33, 0xC0, 0x4C, 0x89, 0x45, 0xB8, 0x45, 0x33, 0xED,
    0x48, 0x89, 0x54, 0x24, 0x60, 0x0F, 0x57, 0xC0, 0x44, 0x89, 0x6C, 0x24,
    0x70, 0x4D, 0x8B, 0xF8, 0x48, 0x89, 0x4D, 0x80, 0x48, 0x8B, 0xF2, 0x4C,
    0x89, 0x4C, 0x24, 0x68,
};

// generator useitem branch, 39 bytes at 0x527104
constexpr std::uint8_t GeneratorUseItemBranchWitness[]{
    0x41, 0x0F, 0xB6, 0x47, 0x59, 0x3C, 0xFE, 0x0F, 0x85, 0x49, 0x03, 0x00,
    0x00, 0x48, 0x63, 0x4C, 0x24, 0x54, 0x48, 0x8B, 0x44, 0x24, 0x68, 0x48,
    0x03, 0xC9, 0x48, 0x8B, 0x0C, 0xC8, 0x48, 0x85, 0xC9, 0x0F, 0x84, 0x1D,
    0xFF, 0xFF, 0xFF,
};

// generator type switch, 32 bytes at 0x52745A
constexpr std::uint8_t GeneratorSwitchHeadWitness[]{
    0x45, 0x0F, 0xB6, 0x67, 0x56, 0x33, 0xC9, 0x45, 0x0F, 0xB7, 0x7F, 0x54,
    0x8B, 0xF1, 0xC7, 0x45, 0x90, 0xFF, 0xFF, 0xFF, 0xFF, 0x44, 0x8B, 0xF1,
    0x3C, 0xFF, 0x0F, 0x85, 0x8A, 0x01, 0x00, 0x00,
};

// generator no-build branch, 15 bytes at 0x52751C
constexpr std::uint8_t GeneratorNoBuildBranchWitness[]{
    0x33, 0xC9, 0x85, 0xF6, 0x0F, 0x84, 0x65, 0x03, 0x00, 0x00, 0x48, 0x8B,
    0x74, 0x24, 0x60,
};

// generator default case, 14 bytes at 0x527654
constexpr std::uint8_t GeneratorDefaultCaseWitness[]{
    0x4C, 0x8B, 0x45, 0x80, 0x48, 0x8B, 0x5C, 0x24, 0x78, 0xE9, 0xBC, 0xFE,
    0xFF, 0xFF,
};

// generator build and store, 53 bytes at 0x527828
constexpr std::uint8_t GeneratorBuildAndStoreWitness[]{
    0x44, 0x8B, 0x65, 0x90, 0x48, 0x8B, 0x4D, 0x80, 0x48, 0x8D, 0x55, 0x20,
    0x45, 0x33, 0xC0, 0xE8, 0x14, 0x55, 0xF1, 0xFF, 0x48, 0x63, 0x4C, 0x24,
    0x54, 0x48, 0x8B, 0x5C, 0x24, 0x60, 0x48, 0x89, 0x84, 0xCD, 0x20, 0x01,
    0x00, 0x00, 0x4C, 0x8B, 0x7C, 0x24, 0x78, 0x44, 0x8B, 0x74, 0x24, 0x58,
    0x45, 0x85, 0xE4, 0x78, 0x3C,
};

// generator no-build target, 14 bytes at 0x52788B
constexpr std::uint8_t GeneratorNoBuildTargetWitness[]{
    0x4C, 0x8B, 0x65, 0x80, 0x4C, 0x8B, 0x7C, 0x24, 0x78, 0xE9, 0xAF, 0xF7,
    0xFF, 0xFF,
};

// input binder loop, 107 bytes at 0x52A799
constexpr std::uint8_t BinderLoopWitness[]{
    0x48, 0x8B, 0x7D, 0x48, 0x41, 0xBF, 0x03, 0x00, 0x00, 0x00, 0x48, 0x83,
    0xC7, 0x08, 0x49, 0x8B, 0xCE, 0xE8, 0x41, 0x25, 0xE4, 0xFF, 0x89, 0x47,
    0x04, 0x41, 0xF6, 0x45, 0x50, 0x01, 0x74, 0x04, 0x4C, 0x89, 0x77, 0xF8,
    0x41, 0x0F, 0xB6, 0x45, 0x59, 0x3C, 0xFF, 0x75, 0x2E, 0x41, 0xB8, 0x13,
    0x02, 0x00, 0x00, 0x4C, 0x89, 0x77, 0xF8, 0x48, 0x8D, 0x15, 0x99, 0x17,
    0x81, 0x01, 0x49, 0x8B, 0xCE, 0xE8, 0x81, 0xF0, 0xE1, 0xFF, 0x89, 0x07,
    0x41, 0x0F, 0xB7, 0x45, 0x50, 0x84, 0xC0, 0x79, 0x1A, 0x41, 0x8B, 0x9C,
    0x24, 0x88, 0x00, 0x00, 0x00, 0xEB, 0x1F, 0x3C, 0xFE, 0x75, 0x6E, 0x4C,
    0x89, 0x77, 0xF8, 0xC7, 0x07, 0xFF, 0xFF, 0xFF, 0xFF, 0xEB, 0xDD,
};

// push rbx / push rsi / push r12, the first five bytes of ParserEntryWitness.
constexpr std::uint8_t ParserPrologue[]{ 0x40, 0x53, 0x56, 0x41, 0x54 };

struct Witness {
    const char*         name;
    std::uint64_t       rva;
    const std::uint8_t* bytes;
    std::uint32_t       size;
};

constexpr std::array<Witness, 11> Witnesses{{
    { "parser entry", CubeOutputParserRva, ParserEntryWitness, static_cast<std::uint32_t>(sizeof(ParserEntryWitness)) },
    { "parser useitem branch", ParserUseItemBranchRva, ParserUseItemBranchWitness, static_cast<std::uint32_t>(sizeof(ParserUseItemBranchWitness)) },
    { "item copy", ItemCloneRva, ItemCloneWitness, static_cast<std::uint32_t>(sizeof(ItemCloneWitness)) },
    { "generator frame", GeneratorFrameRva, GeneratorFrameWitness, static_cast<std::uint32_t>(sizeof(GeneratorFrameWitness)) },
    { "generator useitem branch", GeneratorUseItemBranchRva, GeneratorUseItemBranchWitness, static_cast<std::uint32_t>(sizeof(GeneratorUseItemBranchWitness)) },
    { "generator type switch", GeneratorSwitchHeadRva, GeneratorSwitchHeadWitness, static_cast<std::uint32_t>(sizeof(GeneratorSwitchHeadWitness)) },
    { "generator no-build branch", GeneratorNoBuildBranchRva, GeneratorNoBuildBranchWitness, static_cast<std::uint32_t>(sizeof(GeneratorNoBuildBranchWitness)) },
    { "generator default case", GeneratorDefaultCaseRva, GeneratorDefaultCaseWitness, static_cast<std::uint32_t>(sizeof(GeneratorDefaultCaseWitness)) },
    { "generator build and store", GeneratorBuildAndStoreRva, GeneratorBuildAndStoreWitness, static_cast<std::uint32_t>(sizeof(GeneratorBuildAndStoreWitness)) },
    { "generator no-build target", GeneratorNoBuildTargetRva, GeneratorNoBuildTargetWitness, static_cast<std::uint32_t>(sizeof(GeneratorNoBuildTargetWitness)) },
    { "input binder loop", BinderLoopRva, BinderLoopWitness, static_cast<std::uint32_t>(sizeof(BinderLoopWitness)) },
}};

// Original bytes of both patched sites, taken from inside their witnesses.
constexpr const std::uint8_t* NoBuildJumpOriginal =
    GeneratorNoBuildBranchWitness + (GeneratorNoBuildJumpRva - GeneratorNoBuildBranchRva);
constexpr const std::uint8_t* BinderTypeLoadOriginal =
    BinderLoopWitness + (BinderTypeLoadRva - BinderLoopRva);

// ---------------------------------------------------------------------------
// Relays
// ---------------------------------------------------------------------------

// Generator relay, entered from the je at 0x527520 (taken only when no
// native build applies). rsp and rbp are the generator's own frame.
constexpr std::uint8_t GeneratorRelay[]{
    0x48, 0x8B, 0x44, 0x24, 0x78,                              // +00 mov rax, qword ptr [rsp + 0x78]
    0x80, 0x78, 0x59, 0xFB,                                    // +05 cmp byte ptr [rax + 0x59], 0xfb
    0x75, 0x2F,                                                // +09 jne 0x3a
    0x48, 0x63, 0x4C, 0x24, 0x54,                              // +0B movsxd rcx, dword ptr [rsp + 0x54]
    0x48, 0xC1, 0xE1, 0x04,                                    // +10 shl rcx, 4
    0x48, 0x03, 0x4C, 0x24, 0x68,                              // +14 add rcx, qword ptr [rsp + 0x68]
    0x48, 0x8B, 0x11,                                          // +19 mov rdx, qword ptr [rcx]
    0x48, 0x85, 0xD2,                                          // +1C test rdx, rdx
    0x74, 0x19,                                                // +1F je 0x3a
    0x48, 0x8B, 0x4D, 0x80,                                    // +21 mov rcx, qword ptr [rbp - 0x80]
    0xFF, 0x15, 0x18, 0x00, 0x00, 0x00,                        // +25 call qword ptr [rip + 0x18]
    0x48, 0x8B, 0x74, 0x24, 0x60,                              // +2B mov rsi, qword ptr [rsp + 0x60]
    0x44, 0x8B, 0x65, 0x90,                                    // +30 mov r12d, dword ptr [rbp - 0x70]
    0xFF, 0x25, 0x11, 0x00, 0x00, 0x00,                        // +34 jmp qword ptr [rip + 0x11]
    0xFF, 0x25, 0x13, 0x00, 0x00, 0x00,                        // +3A jmp qword ptr [rip + 0x13]
    0xCC, 0xCC, 0xCC,                                          // padding
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,            // +43 copy slot: CopyForCubeOutput
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,            // +4B store slot: 0x52783C
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,            // +53 no-build slot: 0x52788B
};

constexpr std::size_t GeneratorCopySlot    = 0x43;
constexpr std::size_t GeneratorStoreSlot   = 0x4B;
constexpr std::size_t GeneratorNoBuildSlot = 0x53;
static_assert(sizeof(GeneratorRelay) == 0x5B, "Verified generator relay length changed.");

// Binder relay, entered from the jmp at 0x52A7BD. Replays the type load and
// reports cloneitem (FBh) as useitem (FEh) to the compares at 0x52A7C2.
constexpr std::uint8_t BinderRelay[]{
    0x41, 0x0F, 0xB6, 0x45, 0x59,                              // +00 movzx eax, byte ptr [r13 + 0x59]
    0x3C, 0xFB,                                                // +05 cmp al, 0xfb
    0x75, 0x02,                                                // +07 jne 0xb
    0xB0, 0xFE,                                                // +09 mov al, 0xfe
    0xFF, 0x25, 0x02, 0x00, 0x00, 0x00,                        // +0B jmp qword ptr [rip + 2]
    0xCC, 0xCC,                                                // padding
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,            // +13 resume slot: 0x52A7C2
};

constexpr std::size_t BinderResumeSlot = 0x13;
static_assert(sizeof(BinderRelay) == 0x1B, "Verified binder relay length changed.");

constexpr std::size_t RelayPageBytes       = 4'096;
constexpr std::size_t GeneratorRelayOffset = 0x00;
constexpr std::size_t BinderRelayOffset    = 0x60;

static_assert(GeneratorRelayOffset + sizeof(GeneratorRelay) <= BinderRelayOffset,
    "Generator relay overlaps the binder relay.");
static_assert(BinderRelayOffset + sizeof(BinderRelay) <= RelayPageBytes,
    "Binder relay does not fit the relay page.");

constexpr std::size_t MaximumConfigBytes = 32'768;
constexpr std::size_t AliasBufferBytes   = 1'024;

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

struct Config {
    bool enabled = true;
};

constexpr char DefaultConfigToml[] =
    "# Cube Clone Item\n"
    "#\n"
    "# Adds a cubemain.txt output code, cloneitem. The output is an exact duplicate\n"
    "# of the item matched by input 1: same base item, quality, affixes, rolled\n"
    "# values, sockets and socketed items. Nothing is rolled.\n"
    "#\n"
    "# Write it in the output, output b or output c column, in lowercase. Modifiers\n"
    "# follow a comma, as with useitem:\n"
    "#\n"
    "#   cloneitem\n"
    "#   cloneitem,rem\n"
    "#\n"
    "# Which item is copied\n"
    "#   Always the item matched by input 1. The game binds input 1 to the outputs\n"
    "#   only when output (A) is cloneitem, useitem or usetype, or carries mod. A\n"
    "#   cloneitem in output b or output c therefore needs one of those in output\n"
    "#   (A); otherwise that output produces nothing.\n"
    "#\n"
    "# The source item\n"
    "#   Consumed like any other input. To keep the original and get a copy, put\n"
    "#   useitem in output (A) and cloneitem in output b.\n"
    "#\n"
    "# Modifiers\n"
    "#   Applied to the copy after it is made: uns, rem, rep, rch, sock=, qty= and\n"
    "#   the mod 1 to mod 5 columns.\n"
    "#   No effect, because nothing is rolled: pre=, suf=, lvl=, plvl=, ilvl=,\n"
    "#   low, nor, hiq, mag, set, rar, uni, crf, tmp, eth, exc and eli.\n"
    "#   reg turns the output into usetype, exactly as it does for useitem.\n"
    "#   Do not combine with mod: mod rewrites the input 1 item in place and does\n"
    "#   not know cloneitem.\n"
    "#\n"
    "# Console command: cloneitem reports whether the hooks are armed, how many\n"
    "# cloneitem cells were parsed and how many copies were made.\n"
    "\n"
    "[cube_cloneitem]\n"
    "\n"
    "# Master switch. When false nothing is hooked and cloneitem is not a\n"
    "# recognised output code, as in the unmodified game.\n"
    "enabled = true\n";

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

enum class HookState : std::uint8_t {
    NotLoaded,
    DisabledByConfig,
    Armed,
};

using CubeOutputParserFn =
    std::int64_t(__fastcall*)(std::uint8_t dataContext, std::uint8_t* output,
                              char* cell, std::int32_t row);
using ItemCloneFn = void*(__fastcall*)(void* game, void* sourceItem);

const D2RL::PluginContext* Context{};
std::uintptr_t             Base{};
ItemCloneFn                NativeItemClone{};
CubeOutputParserFn         OriginalParser{};
Config                     Settings{};
HookState                  State{ HookState::NotLoaded };
void*                      RelayPage{};
bool                       NoBuildJumpPatched{};
bool                       BinderPatched{};
std::atomic<std::uint64_t> CellsRecognised{};
std::atomic<std::uint64_t> CellsTooLong{};
std::atomic<std::uint64_t> CopiesMade{};
std::atomic<std::uint64_t> CopiesFailed{};

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

void ApplyConfigLine(std::string_view key, std::string_view value) noexcept {
    if (key == "enabled") ParseBool(value, Settings.enabled);
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
        Context->LogWarn("CubeCloneItem: config file could not be created; using defaults.");
        return;
    }
    std::string buffer(MaximumConfigBytes, '\0');
    if (Context->ReadConfig(buffer.data(), static_cast<std::uint32_t>(buffer.size()), nullptr)) {
        buffer.resize(std::strlen(buffer.c_str()));
        ParseConfig(buffer);
    } else {
        Context->LogWarn("CubeCloneItem: config file could not be read; using defaults.");
    }
}

// ---------------------------------------------------------------------------
// 1. Parser hook
// ---------------------------------------------------------------------------

auto IsCloneItemCode(const char* code) noexcept -> bool {
    if (std::strncmp(code, CloneItemCode.data(), CloneItemCode.size()) != 0) return false;
    // The native parser ends the code at the first comma, after turning every
    // quote into a terminator.
    const char next = code[CloneItemCode.size()];
    return next == '\0' || next == ',' || next == '"';
}

std::int64_t __fastcall ParseCubeOutput(std::uint8_t dataContext, std::uint8_t* output,
                                        char* cell, std::int32_t row) noexcept {
    // The field callback only calls with a non-empty cell and a record.
    if (output == nullptr || cell == nullptr) {
        return OriginalParser(dataContext, output, cell, row);
    }

    const std::size_t quote = cell[0] == '"' ? 1U : 0U;
    if (!IsCloneItemCode(cell + quote)) {
        return OriginalParser(dataContext, output, cell, row);
    }

    const std::size_t cellLength = std::strlen(cell);
    const std::size_t tailLength = cellLength - quote - CloneItemCode.size();
    if (quote + UseItemCode.size() + tailLength + 1 > AliasBufferBytes) {
        CellsTooLong.fetch_add(1, std::memory_order_relaxed);
        return OriginalParser(dataContext, output, cell, row);
    }

    // Same cell, code spelled useitem: the native parser then handles every
    // modifier after the comma exactly as it does for useitem.
    char alias[AliasBufferBytes];
    std::size_t length = 0;
    if (quote != 0) alias[length++] = '"';
    std::memcpy(alias + length, UseItemCode.data(), UseItemCode.size());
    length += UseItemCode.size();
    std::memcpy(alias + length, cell + quote + CloneItemCode.size(), tailLength + 1);

    const std::int64_t result = OriginalParser(dataContext, output, alias, row);

    // reg rewrites the type to usetype (FFh) for useitem too; that is left
    // native, so only a cell that still reads useitem becomes cloneitem.
    if (output[OutputTypeOffset] == UseItemType) {
        output[OutputTypeOffset] = CloneItemType;
        CellsRecognised.fetch_add(1, std::memory_order_relaxed);
    }
    return result;
}

// ---------------------------------------------------------------------------
// 3. The copy, reached from the generator relay with the native copy's ABI
// ---------------------------------------------------------------------------

void* __fastcall CopyForCubeOutput(void* game, void* sourceItem) noexcept {
    void* copy = NativeItemClone(game, sourceItem);
    (copy != nullptr ? CopiesMade : CopiesFailed).fetch_add(1, std::memory_order_relaxed);
    return copy;
}

// ---------------------------------------------------------------------------
// Relay page and site patching
// ---------------------------------------------------------------------------

auto Rel32(std::uintptr_t instructionEnd, std::uintptr_t target, std::int32_t& out) noexcept
        -> bool {
    const std::int64_t delta =
        static_cast<std::int64_t>(target) - static_cast<std::int64_t>(instructionEnd);
    if (delta < INT32_MIN || delta > INT32_MAX) return false;
    out = static_cast<std::int32_t>(delta);
    return true;
}

auto GeneratorRelayAddress() noexcept -> std::uintptr_t {
    return reinterpret_cast<std::uintptr_t>(RelayPage) + GeneratorRelayOffset;
}

auto BinderRelayAddress() noexcept -> std::uintptr_t {
    return reinterpret_cast<std::uintptr_t>(RelayPage) + BinderRelayOffset;
}

auto EncodeNoBuildJump(std::array<std::uint8_t, NoBuildJumpSize>& bytes) noexcept -> bool {
    std::int32_t displacement = 0;
    if (!Rel32(Base + GeneratorNoBuildJumpRva + NoBuildJumpSize, GeneratorRelayAddress(),
            displacement)) {
        return false;
    }
    bytes[0] = 0x0F;
    bytes[1] = 0x84;
    std::memcpy(bytes.data() + 2, &displacement, sizeof(displacement));
    return true;
}

auto EncodeBinderJump(std::array<std::uint8_t, BinderJumpSize>& bytes) noexcept -> bool {
    std::int32_t displacement = 0;
    if (!Rel32(Base + BinderTypeLoadRva + BinderJumpSize, BinderRelayAddress(), displacement)) {
        return false;
    }
    bytes[0] = 0xE9;
    std::memcpy(bytes.data() + 1, &displacement, sizeof(displacement));
    return true;
}

// Searches upward from the lowest patched site, so the higher site is closer
// to the page too. Both displacements are re-checked by the encoders.
auto AllocateNear(std::uintptr_t hint, std::size_t size) noexcept -> void* {
    SYSTEM_INFO systemInfo{};
    GetSystemInfo(&systemInfo);
    const auto granularity = static_cast<std::uintptr_t>(systemInfo.dwAllocationGranularity);
    const auto aligned = hint & ~(granularity - 1U);
    for (std::uintptr_t delta = granularity; delta < 0x7000'0000ULL; delta += granularity) {
        const auto candidate = aligned + delta;
        std::int32_t unused = 0;
        if (!Rel32(hint, candidate + size, unused)) break;
        if (auto* memory = VirtualAlloc(reinterpret_cast<void*>(candidate), size,
                MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)) {
            return memory;
        }
    }
    return nullptr;
}

void WriteQword(std::uint8_t* at, std::uint64_t value) noexcept {
    std::memcpy(at, &value, sizeof(value));
}

auto BuildRelayPage() noexcept -> bool {
    RelayPage = AllocateNear(Base + GeneratorNoBuildJumpRva, RelayPageBytes);
    if (RelayPage == nullptr) {
        Context->LogError("CubeCloneItem: no relay page was available within rel32 reach.");
        return false;
    }

    auto* page = static_cast<std::uint8_t*>(RelayPage);
    std::memset(page, 0xCC, RelayPageBytes);

    auto* generator = page + GeneratorRelayOffset;
    std::memcpy(generator, GeneratorRelay, sizeof(GeneratorRelay));
    WriteQword(generator + GeneratorCopySlot, reinterpret_cast<std::uint64_t>(&CopyForCubeOutput));
    WriteQword(generator + GeneratorStoreSlot, Base + GeneratorStoreResumeRva);
    WriteQword(generator + GeneratorNoBuildSlot, Base + GeneratorNoBuildTargetRva);

    auto* binder = page + BinderRelayOffset;
    std::memcpy(binder, BinderRelay, sizeof(BinderRelay));
    WriteQword(binder + BinderResumeSlot, Base + BinderTypeCompareRva);

    DWORD previous = 0;
    if (!VirtualProtect(page, RelayPageBytes, PAGE_EXECUTE_READ, &previous)) {
        Context->LogError("CubeCloneItem: relay page protection could not be finalized.");
        VirtualFree(RelayPage, 0, MEM_RELEASE);
        RelayPage = nullptr;
        return false;
    }
    FlushInstructionCache(GetCurrentProcess(), page, RelayPageBytes);
    return true;
}

// Points the copy slot at the native copy. Anything still reaching the
// generator relay then runs native code only.
auto RetargetCopySlotToNative() noexcept -> bool {
    if (RelayPage == nullptr) return true;
    auto* page = static_cast<std::uint8_t*>(RelayPage);
    DWORD previous = 0;
    if (!VirtualProtect(page, RelayPageBytes, PAGE_EXECUTE_READWRITE, &previous)) return false;
    WriteQword(page + GeneratorRelayOffset + GeneratorCopySlot, Base + ItemCloneRva);
    DWORD ignored = 0;
    VirtualProtect(page, RelayPageBytes, previous, &ignored);
    FlushInstructionCache(GetCurrentProcess(), page, RelayPageBytes);
    return true;
}

auto RestoreSites() noexcept -> bool {
    bool restored = true;
    if (BinderPatched) {
        std::array<std::uint8_t, BinderJumpSize> current{};
        if (EncodeBinderJump(current)
                && Context->PatchBytes(BinderTypeLoadRva, current.data(), BinderJumpSize,
                    BinderTypeLoadOriginal, BinderJumpSize)) {
            BinderPatched = false;
        } else {
            restored = false;
        }
    }
    if (NoBuildJumpPatched) {
        std::array<std::uint8_t, NoBuildJumpSize> current{};
        if (EncodeNoBuildJump(current)
                && Context->PatchBytes(GeneratorNoBuildJumpRva, current.data(), NoBuildJumpSize,
                    NoBuildJumpOriginal, NoBuildJumpSize)) {
            NoBuildJumpPatched = false;
        } else {
            restored = false;
        }
    }
    return restored;
}

// Undo after a failed install. Returns false; the loader then unloads.
auto RollBack() noexcept -> bool {
    RetargetCopySlotToNative();
    if (RestoreSites()) {
        VirtualFree(RelayPage, 0, MEM_RELEASE);
        RelayPage = nullptr;
    } else {
        // Keep the page: a site that could not be restored still jumps into it,
        // and its copy slot now reaches native code only.
        Context->LogError("CubeCloneItem: rollback could not restore every site; the relay "
                          "page stays allocated.");
    }
    return false;
}

// ---------------------------------------------------------------------------
// Verification and installation
// ---------------------------------------------------------------------------

auto VerifyNativeContract() noexcept -> bool {
    for (const auto& witness : Witnesses) {
        if (!Context->CheckExpectedBytes(witness.rva, witness.bytes, witness.size)) {
            char message[256];
            std::snprintf(message, sizeof(message),
                "CubeCloneItem: the %s at 0x%llX does not match the verified D2R image, "
                "or another plugin already owns it. Refusing to load.",
                witness.name, static_cast<unsigned long long>(witness.rva));
            Context->LogError(message);
            return false;
        }
    }
    return true;
}

auto InstallHooks() noexcept -> bool {
    if (!BuildRelayPage()) return false;

    std::array<std::uint8_t, NoBuildJumpSize> noBuildJump{};
    std::array<std::uint8_t, BinderJumpSize>  binderJump{};
    if (!EncodeNoBuildJump(noBuildJump) || !EncodeBinderJump(binderJump)) {
        Context->LogError("CubeCloneItem: relay displacement validation failed.");
        return RollBack();
    }

    // Runtime sites first. Until the parser hook is in, no output can carry
    // type FBh, so both relays stay on their native paths.
    if (!Context->PatchBytes(GeneratorNoBuildJumpRva, NoBuildJumpOriginal, NoBuildJumpSize,
            noBuildJump.data(), NoBuildJumpSize)) {
        Context->LogError("CubeCloneItem: the generator branch at 0x527520 could not be redirected.");
        return RollBack();
    }
    NoBuildJumpPatched = true;

    if (!Context->PatchBytes(BinderTypeLoadRva, BinderTypeLoadOriginal, BinderJumpSize,
            binderJump.data(), BinderJumpSize)) {
        Context->LogError("CubeCloneItem: the input binder at 0x52A7BD could not be redirected.");
        return RollBack();
    }
    BinderPatched = true;

    if (!Context->InstallInlineHook(CubeOutputParserRva, ParserPrologue, ParserHookSize,
            reinterpret_cast<void*>(&ParseCubeOutput),
            reinterpret_cast<void**>(&OriginalParser))
            || OriginalParser == nullptr) {
        Context->LogError("CubeCloneItem: the cubemain output parser hook at 0x3E5E60 could not "
                          "be installed.");
        return RollBack();
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
    default:                          return "not loaded";
    }
}

auto __cdecl StatusCommand(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command,
        void*) noexcept -> D2RL::ConsoleCommandResult {
    if (command == nullptr || command->plugin == nullptr) {
        return D2RL::ConsoleCommandResult::Failed;
    }
    char message[320];
    std::snprintf(message, sizeof(message),
        "Cube Clone Item: %s | cloneitem cells parsed %llu, too long %llu | "
        "copies made %llu, failed %llu",
        StateName(),
        static_cast<unsigned long long>(CellsRecognised.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(CellsTooLong.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(CopiesMade.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(CopiesFailed.load(std::memory_order_relaxed)));
    command->plugin->WriteConsoleMessage(message);
    return D2RL::ConsoleCommandResult::Handled;
}

void RegisterStatusCommand() noexcept {
    if (!Context->RegisterConsoleCommand("cloneitem", &StatusCommand,
            "Reports Cube Clone Item status and counters.")) {
        Context->LogWarn("CubeCloneItem: the status console command was refused.");
    }
}

constexpr D2RL::PluginInfo Info{
    .infoSize = D2RL::PluginInfoSize,
    .apiVersion = D2RL_PLUGIN_API_VERSION,
    .id = "celestialrayone.cube-cloneitem",
    .name = "Cube Clone Item",
    .version = "1.0.0",
    .author = "CelestialRayOne",
    .description =
        "Adds the cloneitem cubemain output code: an exact duplicate of the item "
        "matched by input 1, with nothing rolled.",
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
    NativeItemClone = reinterpret_cast<ItemCloneFn>(Base + ItemCloneRva);

    ReadConfiguration();

    if (!Settings.enabled) {
        State = HookState::DisabledByConfig;
        Context->LogInfo("CubeCloneItem: disabled by configuration; nothing hooked.");
        RegisterStatusCommand();
        return true;
    }

    if (!VerifyNativeContract()) return false;
    if (!InstallHooks()) return false;

    Context->LogInfo("CubeCloneItem: armed. cubemain output code cloneitem is available.");
    RegisterStatusCommand();
    return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
    if (Context == nullptr || RelayPage == nullptr) return;
    RetargetCopySlotToNative();
    RestoreSites();
    // The relay page is deliberately kept: a thread may be inside a relay right
    // now, and a site that could not be restored still needs it.
}

}  // namespace CelestialRayOne::CubeCloneItem
