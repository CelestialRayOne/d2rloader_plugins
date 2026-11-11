// Op 1 Item Fix (celestialrayone.op1-item-fix)
//
// An itemstatcost.txt op 1 stat on an item stops reaching the character as
// soon as it changes while the item also carries the stat it raises. In
// Eastern Sun Resurrected that is passive_summ_mastery_total (op 1, op stat1
// passive_summ_mastery): a Jeweler perk bonus on it, or a socketed item's part
// of it, never reaches the character's summon damage.
//
// Every address and byte below was read out of D2RCore.dll 1.3.1 (sha256
// 2a868d01...): the witnesses are the ones the enhanced-damage-fix plugin
// verified there, copied byte for byte, and the op 1 table entry is checked at
// load against the value the switch's own bounds check proves for the
// default arm.
//
// ---------------------------------------------------------------------------
// 1. The engine rule that loses it
// ---------------------------------------------------------------------------
//   When a stat with an op changes on a statlist, the engine re-evaluates the
//   stats it raises (its op stat1..3) and then decides whether the stat's own
//   total may be committed to that statlist, which is what carries it up to
//   the socket host and then to the character. That is the evaluate-and-update
//   routine (D2MOO sub_6FDB64A0): for each target whose re-evaluated total is
//   not 0 it switches on the stat's `op`, and op 1 falls into the default arm,
//   which withholds the commit on every statlist.
//
//   op 1 itself is applied by the evaluator (D2MOO sub_6FDB5830 case 1, D2RCore
//   3DC2D0): on a statlist owned by an item it multiplies the ITEM'S OWN BASE
//   value of the target (3DC834: "ops 1 and 13 on an item owner take the base
//   Stats path"), on a character or monster the accumulated total. So on an
//   item without a base value of the target op 1 multiplied nothing, yet the
//   commit is withheld because the target's total, affixes included, is not 0.
//   The item's total of the op 1 stat keeps whatever was committed before, the
//   character's op 1 reads that stale total, and the change is lost. The
//   character's own statlist is right to withhold: op 1 applied there.
//
// ---------------------------------------------------------------------------
// 2. Where it runs under D2RLoader 1.3.1: D2RCore.dll
// ---------------------------------------------------------------------------
//     D2RCore 3DBEB0  evaluate and update (ctx, statList, key, record, unit)
//       3DBF0E  mov [rsp+40h],r15 / mov al,1 / mov [rsp+34h],eax  ; commit = 1
//       3DBF25  and al,[rsp+34h] / mov [rsp+34h],eax              ; <- resume
//       3DBF30  next target, at most 3
//       3DBF3D  movzx r12d, word [rsi+r15*2+54h]                  ; op stat N
//       3DBF8E  call 3DBEB0                                        ; re-evaluate it
//       3DBFAF  call 3DCE90                                        ; commit it
//       3DBFB4  test ebp,ebp / jz next                             ; total 0: no switch
//       3DBFBC  movzx eax, byte [rsi+50h]                          ; the stat's `op`
//       3DBFC0  cmp rax,0Dh / ja 3DC123                            ; default arm
//       3DBFE0  lea rcx,[rip+254445h]                              ; table 63042C
//       3DBFE7  movsxd rax,dword [rcx+rax*4] / add rax,rcx / jmp rax
//
//   Entry N of the table at 63042C is the arm for op N as an offset from the
//   table. Entry 13 is 3DC117 (the op 13 arm the enhanced-damage-fix plugin
//   repairs). The bounds check sends every op above 13 to 3DC123, the default
//   arm; op 1 has no case of its own, so its entry holds 3DC123 too. That is
//   checked at load (F7 BC DA FF), and nothing is installed otherwise.
//
// ---------------------------------------------------------------------------
// 3. The fix
// ---------------------------------------------------------------------------
//   Entry 1 is pointed at a 60-byte stub in a page this plugin allocates next
//   to D2RCore. The stub asks AllowCommit for al and resumes at 3DBF25, so the
//   engine's own and/mov records the answer exactly as for any other arm:
//
//     native  al = 0 (withhold), for every statlist
//     fixed   a statlist owned by an item (statlist +08h is 4): withhold only
//               when the owning item (statlist +A0h) has a base value of the
//               target at layer 0 (D2RCore ReadWideUnitBaseStat, the read op 1
//               itself makes there); with none, or no owning item, commit
//             every other statlist: native
//
//   Registers at the arm: rdi = statList, rsi = the op 1 stat's record (stat
//   id at +0), r15 = target index 0..2, ebp = the target's non-zero total, the
//   commit flag at [rsp+34h]; rsp is 16-aligned there (8 pushes + sub rsp,48h
//   at 3DBEB0), so the stub takes 20h of shadow space, calls, and gives it
//   back. Nothing volatile is live at 3DBF25: the next iteration and the exit
//   path reload rcx, rdx and r8..r11 before use.
//
//   Only op 1 goes through the stub: the default arm and every other entry are
//   untouched, and so is the enhanced-damage-fix plugin's op 13 arm.
//
// ---------------------------------------------------------------------------
// 4. Verified against
// ---------------------------------------------------------------------------
//   D2MOO D2StatList.cpp (sub_6FDB5830 case 1, sub_6FDB64A0 commit switch,
//   sub_6FDB6C10 propagation, PostStatToStatList); the enhanced-damage-fix
//   plugin's D2RCore 1.3.1 witnesses. The stub was assembled with keystone,
//   decoded with capstone and run under unicorn through D2RCore's real dispatch
//   bytes (3DBFBC..3DBFF0) with the table entry pointing at it: arguments
//   (statList, target, op 1 stat), 16-byte alignment at the call, every
//   non-volatile register kept, the commit flag 1 or 0 as answered, the next
//   target reached at 3DBF3D, and after unload the stub's native tail landing
//   on the default arm at 3DC123.
//
//   No other plugin in this repo touches the op switch table at 63042C.

#include <D2RLPlugin/api.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <atomic>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

namespace CelestialRayOne::Op1ItemFix {
namespace {

// ---------------------------------------------------------------------------
// D2RCore.dll 1.3.1 anchors (RVAs inside D2RCore, fixed image base C0DE5000000)
// ---------------------------------------------------------------------------

constexpr wchar_t CoreModuleName[]     = L"D2RCore.dll";
constexpr char    ReadBaseStatExport[] = "ReadWideUnitBaseStat";

constexpr std::uintptr_t EvaluateUpdateRva = 0x3DBEB0;  // evaluate and update
constexpr std::uintptr_t CommitLoopRva     = 0x3DBF0E;  // per-target commit loop
constexpr std::uintptr_t CommitResumeRva   = 0x3DBF25;  // and al,[rsp+34h]
constexpr std::uintptr_t BoundsJumpRva     = 0x3DBFC4;  // ja default (0F 87 rel32)
constexpr std::uintptr_t DefaultArmRva     = 0x3DC123;  // the switch's default arm
constexpr std::uintptr_t OpJumpTableRva    = 0x63042C;  // relative entries, one per op 0..13
constexpr std::uintptr_t OpJumpEntry1Rva   = OpJumpTableRva + 1 * 4;   // op 1, the patch site
constexpr std::uintptr_t OpJumpEntry13Rva  = OpJumpTableRva + 13 * 4;  // op 13
constexpr std::uintptr_t OwnerLoadRva      = 0x3DC710;  // evaluator: owner unit, its type
constexpr std::uintptr_t OwnerGatesRva     = 0x3DC834;  // evaluator: op 11 / op 13 owner gates
constexpr std::uintptr_t OwnerBaseRva      = 0x3DC90F;  // evaluator: owner base Stats lookup
constexpr std::uintptr_t ReadBaseExportRva = 0x831DD0;  // ReadWideUnitBaseStat
constexpr std::uintptr_t ReadBaseImplRva   = 0x3D8AD0;  // its body

// Statlist, record and unit layout, as the D2RCore code above reads it.
constexpr std::size_t   StatListOwnerTypeOffset = 0x08;
constexpr std::size_t   StatListOwnerOffset     = 0xA0;
constexpr std::size_t   UnitTypeOffset          = 0x00;
constexpr std::size_t   UnitClassIdOffset       = 0x04;
constexpr std::uint32_t UnitTypeItem            = 4;

constexpr std::size_t EntrySize = 4;

// ---------------------------------------------------------------------------
// D2RCore byte witnesses (the enhanced-damage-fix plugin's, unchanged)
// ---------------------------------------------------------------------------

// 3DBEB0: push r15..rbx (8) / sub rsp,48h. The frame the stub runs in.
constexpr std::uint8_t EvaluatePrologueWitness[]{
    0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x56, 0x57, 0x55, 0x53, 0x48, 0x83, 0xEC, 0x48,
};

// 3DBF0E..3DBFF0: commit flag at [rsp+34h], the resume block at +17h, the loop
// index in r15, the target id read at +2Fh, the recursive re-evaluation, the
// target commit, the zero-total skip, the switch on byte [rsi+50h] and its
// jump through the table at 63042C.
constexpr std::uint8_t CommitLoopWitness[]{
    0x4C, 0x89, 0x7C, 0x24, 0x40, 0xB0, 0x01, 0x89, 0x44, 0x24, 0x34, 0x45, 0x31, 0xFF, 0xEB, 0x1F,
    0x83, 0x7F, 0x08, 0x02, 0x0F, 0x93, 0xC0, 0x22, 0x44, 0x24, 0x34, 0x89, 0x44, 0x24, 0x34, 0x0F,
    0x1F, 0x00, 0x49, 0xFF, 0xC7, 0x49, 0x83, 0xFF, 0x03, 0x0F, 0x84, 0x91, 0x02, 0x00, 0x00, 0x46,
    0x0F, 0xB7, 0x64, 0x7E, 0x54, 0x41, 0x81, 0xFC, 0xFF, 0xFF, 0x00, 0x00, 0x0F, 0x84, 0x7E, 0x02,
    0x00, 0x00, 0x48, 0x8B, 0x87, 0xA0, 0x00, 0x00, 0x00, 0x48, 0x8D, 0x88, 0xBD, 0x01, 0x00, 0x00,
    0x48, 0x85, 0xC0, 0x49, 0x0F, 0x44, 0xCE, 0x0F, 0xB6, 0x09, 0x44, 0x89, 0xE2, 0xFF, 0x15, 0x7F,
    0x32, 0x32, 0x00, 0x48, 0x85, 0xC0, 0x74, 0xBA, 0x49, 0x89, 0xC5, 0x49, 0xC1, 0xE4, 0x20, 0x48,
    0x89, 0x5C, 0x24, 0x20, 0x4C, 0x89, 0xF1, 0x48, 0x89, 0xFA, 0x4D, 0x89, 0xE0, 0x49, 0x89, 0xC1,
    0xE8, 0x1D, 0xFF, 0xFF, 0xFF, 0x89, 0xC5, 0x49, 0x8B, 0x46, 0x08, 0x48, 0x89, 0x44, 0x24, 0x28,
    0x48, 0x89, 0x5C, 0x24, 0x20, 0x48, 0x89, 0xF9, 0x4C, 0x89, 0xE2, 0x41, 0x89, 0xE8, 0x4D, 0x89,
    0xE9, 0xE8, 0xDC, 0x0E, 0x00, 0x00, 0x85, 0xED, 0x0F, 0x84, 0x74, 0xFF, 0xFF, 0xFF, 0x0F, 0xB6,
    0x46, 0x50, 0x48, 0x83, 0xF8, 0x0D, 0x0F, 0x87, 0x59, 0x01, 0x00, 0x00, 0x4B, 0x8D, 0x0C, 0x7F,
    0x48, 0x8D, 0x96, 0xDA, 0x00, 0x00, 0x00, 0x0F, 0xB7, 0x14, 0x4A, 0x49, 0x89, 0xD4, 0x49, 0xC1,
    0xE4, 0x20, 0x48, 0x8D, 0x0D, 0x45, 0x44, 0x25, 0x00, 0x48, 0x63, 0x04, 0x81, 0x48, 0x01, 0xC8,
    0xFF, 0xE0,
};

// 63042C + 13 * 4: the relative entry for op 13, 3DC117 - 63042C.
constexpr std::uint8_t OpJumpEntry13Witness[]{ 0xEB, 0xBC, 0xDA, 0xFF };

// 3DC710: mov rax,[rdi+0A0h] / test / jz / mov ecx,[rax]. The owner unit and its type.
constexpr std::uint8_t OwnerLoadWitness[]{
    0x48, 0x8B, 0x87, 0xA0, 0x00, 0x00, 0x00, 0x48, 0x85, 0xC0, 0x0F, 0x84, 0x0F, 0x01, 0x00, 0x00,
    0x8B, 0x08, 0xE9, 0x0D, 0x01, 0x00, 0x00,
};

// 3DC834: op 11 needs a player or monster owner, op 13 needs an item owner,
// ops 1 and 13 on an item owner take the base Stats path.
constexpr std::uint8_t OwnerGatesWitness[]{
    0x83, 0xFD, 0x0B, 0x0F, 0x94, 0xC2, 0x83, 0xF9, 0x02, 0x41, 0x0F, 0x93, 0xC0, 0x44, 0x84, 0xC2,
    0x0F, 0x85, 0xB6, 0xFD, 0xFF, 0xFF, 0x83, 0xFD, 0x0D, 0x0F, 0x94, 0xC2, 0x83, 0xF9, 0x04, 0x41,
    0x0F, 0x95, 0xC0, 0x44, 0x84, 0xC2, 0x0F, 0x85, 0xA0, 0xFD, 0xFF, 0xFF, 0x83, 0xF9, 0x04, 0x0F,
    0x95, 0xC1, 0x83, 0xFD, 0x0B, 0x0F, 0x94, 0xC2, 0x08, 0xCA,
};

// 3DC90F: owner unit +88h -> its base Stats (+30h pointer, +38h count),
// binary search on 16-byte entries for the target key at layer 0.
constexpr std::uint8_t OwnerBaseWitness[]{
    0xC7, 0x44, 0x24, 0x44, 0x00, 0x00, 0x00, 0x00, 0x48, 0x85, 0xC0, 0x0F, 0x84, 0xE5, 0xFC, 0xFF,
    0xFF, 0x48, 0x8B, 0x88, 0x88, 0x00, 0x00, 0x00, 0x48, 0x85, 0xC9, 0x0F, 0x84, 0xD0, 0xFC, 0xFF,
    0xFF, 0x4C, 0x8B, 0x41, 0x38, 0x4D, 0x85, 0xC0, 0x0F, 0x84, 0x7F, 0x02, 0x00, 0x00, 0x4C, 0x8B,
    0x49, 0x30, 0x31, 0xD2, 0x4D, 0x89, 0xC3, 0x4D, 0x89, 0xC2, 0xEB, 0x11, 0x0F, 0x1F, 0x44, 0x00,
    0x00, 0x4D, 0x89, 0xD3, 0x4D, 0x85, 0xD2, 0x0F, 0x84, 0x62, 0x02, 0x00, 0x00, 0x49, 0xD1, 0xEA,
    0x4D, 0x8D, 0x3C, 0x12, 0x49, 0xC1, 0xE7, 0x04, 0x4B, 0x39, 0x34, 0x39, 0x73, 0xE3, 0x4C, 0x01,
    0xD2, 0x48, 0xFF, 0xC2, 0x49, 0xF7, 0xD2, 0x4D, 0x01, 0xDA, 0xEB, 0xD5,
};

// 831DD0: ReadWideUnitBaseStat, a thunk to 3D8AD0.
constexpr std::uint8_t ReadBaseExportWitness[]{
    0x48, 0x83, 0xEC, 0x28, 0xE8, 0xF7, 0x6C, 0xBA, 0xFF, 0x89, 0xC0, 0x48, 0x83, 0xC4, 0x28, 0xC3,
};

// 3D8AD0: (unit, statId, layer) -> unit+88h base Stats value for key
// statId<<32|layer; 0 for no unit, no list, statId >= 8000h or no record.
constexpr std::uint8_t ReadBaseImplWitness[]{
    0x56, 0x57, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x31, 0xC0, 0x48, 0x85, 0xC9, 0x0F, 0x84, 0xDE, 0x00,
    0x00, 0x00, 0x48, 0x8B, 0xB1, 0x88, 0x00, 0x00, 0x00, 0x48, 0x85, 0xF6, 0x41, 0x0F, 0x94, 0xC1,
    0x81, 0xFA, 0x00, 0x80, 0x00, 0x00, 0x41, 0x0F, 0x93, 0xC2, 0x45, 0x08, 0xCA, 0x0F, 0x85, 0xBD,
    0x00, 0x00, 0x00, 0x44, 0x89, 0xC7, 0x0F, 0xB6, 0x89, 0xBD, 0x01, 0x00, 0x00, 0x48, 0x89, 0xD3,
    0xFF, 0x15, 0xDA, 0x66, 0x32, 0x00, 0x48, 0x89, 0xC1, 0x31, 0xC0, 0x48, 0x85, 0xC9, 0x0F, 0x84,
    0x9C, 0x00, 0x00, 0x00, 0x48, 0xC1, 0xE3, 0x20, 0x89, 0xFA, 0x48, 0x09, 0xDA, 0x4C, 0x8B, 0x4E,
    0x38, 0x31, 0xC0, 0x4D, 0x85, 0xC9, 0x74, 0x3F, 0x4C, 0x8B, 0x56, 0x30, 0x45, 0x31, 0xC0, 0x4C,
    0x89, 0xCF, 0x4D, 0x89, 0xCB, 0xEB, 0x11, 0x66, 0x0F, 0x1F, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x4C, 0x89, 0xDF, 0x4D, 0x85, 0xDB, 0x74, 0x22, 0x49, 0xD1, 0xEB, 0x4B, 0x8D, 0x1C, 0x03, 0x48,
    0xC1, 0xE3, 0x04, 0x49, 0x39, 0x14, 0x1A, 0x73, 0xE7, 0x4D, 0x01, 0xD8, 0x49, 0xFF, 0xC0, 0x49,
    0xF7, 0xD3, 0x49, 0x01, 0xFB, 0xEB, 0xD9, 0x45, 0x31, 0xC0, 0x4D, 0x39, 0xC8, 0x74, 0x41, 0x4C,
    0x8B, 0x4E, 0x30, 0x49, 0xC1, 0xE0, 0x04, 0x4B, 0x39, 0x14, 0x01, 0x75, 0x33, 0x4D, 0x01, 0xC1,
    0x41, 0x8B, 0x41, 0x08, 0xF6, 0x41, 0x05, 0x02, 0x74, 0x26, 0x83, 0x7E, 0x1C, 0x00, 0x79, 0x20,
    0x48, 0x8B, 0x96, 0xA0, 0x00, 0x00, 0x00, 0x48, 0x85, 0xD2, 0x74, 0x14, 0x83, 0x3A, 0x01, 0x77,
    0x0F, 0x8B, 0x51, 0x28, 0x39, 0xD0, 0x7D, 0x08, 0x0F, 0xB6, 0x49, 0x14, 0xD3, 0xE2, 0x89, 0xD0,
    0x48, 0x83, 0xC4, 0x20, 0x5B, 0x5F, 0x5E, 0xC3,
};

// 63042C + 1 * 4: op 1's entry, the default arm 3DC123 - 63042C.
constexpr std::uint8_t OpJumpEntry1Witness[]{ 0xF7, 0xBC, 0xDA, 0xFF };

constexpr std::size_t ResumeOffsetInLoop     = CommitResumeRva - CommitLoopRva;
constexpr std::size_t BoundsJumpOffsetInLoop = BoundsJumpRva - CommitLoopRva;

// The default arm is where the loop's own bounds check sends ops above 13.
static_assert(CommitLoopWitness[BoundsJumpOffsetInLoop] == 0x0F && CommitLoopWitness[BoundsJumpOffsetInLoop + 1] == 0x87);
static_assert(BoundsJumpRva + 6 + (CommitLoopWitness[BoundsJumpOffsetInLoop + 2]
    | (CommitLoopWitness[BoundsJumpOffsetInLoop + 3] << 8)) == DefaultArmRva);
static_assert(CommitLoopWitness[BoundsJumpOffsetInLoop + 4] == 0 && CommitLoopWitness[BoundsJumpOffsetInLoop + 5] == 0);
static_assert(CommitLoopWitness[ResumeOffsetInLoop] == 0x22 && CommitLoopWitness[ResumeOffsetInLoop + 1] == 0x44
    && CommitLoopWitness[ResumeOffsetInLoop + 2] == 0x24 && CommitLoopWitness[ResumeOffsetInLoop + 3] == 0x34);
// Entry 1 holds the default arm, entry 13 the op 13 arm, both relative to the table.
static_assert(static_cast<std::int32_t>(OpJumpEntry1Witness[0] | (OpJumpEntry1Witness[1] << 8) | (OpJumpEntry1Witness[2] << 16)
    | (static_cast<std::uint32_t>(OpJumpEntry1Witness[3]) << 24)) == static_cast<std::int32_t>(DefaultArmRva) - static_cast<std::int32_t>(OpJumpTableRva));
static_assert(static_cast<std::int32_t>(OpJumpEntry13Witness[0] | (OpJumpEntry13Witness[1] << 8) | (OpJumpEntry13Witness[2] << 16)
    | (static_cast<std::uint32_t>(OpJumpEntry13Witness[3]) << 24)) == 0x3DC117 - static_cast<std::int32_t>(OpJumpTableRva));

struct CoreWitness {
    std::uintptr_t      rva;
    const std::uint8_t* bytes;
    std::size_t         size;
    const char*         name;
};

template <std::size_t N>
constexpr auto W(std::uintptr_t rva, const std::uint8_t (&bytes)[N], const char* name) noexcept -> CoreWitness {
    return { rva, bytes, N, name };
}

constexpr CoreWitness CoreWitnesses[]{
    W(EvaluateUpdateRva, EvaluatePrologueWitness, "evaluate-and-update prologue"),
    W(CommitLoopRva, CommitLoopWitness, "per-target commit loop and op switch"),
    W(OpJumpEntry1Rva, OpJumpEntry1Witness, "op 1 switch entry"),
    W(OpJumpEntry13Rva, OpJumpEntry13Witness, "op 13 switch entry"),
    W(OwnerLoadRva, OwnerLoadWitness, "evaluator owner unit read"),
    W(OwnerGatesRva, OwnerGatesWitness, "evaluator owner gates (op 1 on an item takes the base path)"),
    W(OwnerBaseRva, OwnerBaseWitness, "evaluator owner base Stats read"),
    W(ReadBaseExportRva, ReadBaseExportWitness, "ReadWideUnitBaseStat export"),
    W(ReadBaseImplRva, ReadBaseImplWitness, "ReadWideUnitBaseStat body"),
};

// ---------------------------------------------------------------------------
// The stub
// ---------------------------------------------------------------------------
//
// +00  48 83 EC 20              sub   rsp, 20h            ; shadow, stays aligned
// +04  48 89 F9                 mov   rcx, rdi            ; statList
// +07  42 0F B7 54 7E 54        movzx edx, word [rsi+r15*2+54h] ; target stat id
// +0D  44 0F B7 06              movzx r8d, word [rsi]     ; the op 1 stat's id
// +11  48 B8 imm64              mov   rax, AllowCommit
// +1B  FF D0                    call  rax
// +1D  48 83 C4 20              add   rsp, 20h
// +21  0F B6 C0                 movzx eax, al
// +24  FF 25 00 00 00 00        jmp   qword ptr [rip+0]
// +2A  dq                       D2RCore+3DBF25
// +32  FF 25 00 00 00 00        jmp   qword ptr [rip+0]   ; the native arm,
// +38  dq                       D2RCore+3DC123            ; used after unload
constexpr std::uint8_t DecisionStub[]{
    0x48, 0x83, 0xEC, 0x20, 0x48, 0x89, 0xF9, 0x42, 0x0F, 0xB7, 0x54, 0x7E, 0x54, 0x44, 0x0F, 0xB7,
    0x06, 0x48, 0xB8, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, 0xFF, 0xD0, 0x48, 0x83, 0xC4,
    0x20, 0x0F, 0xB6, 0xC0, 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66,
    0x77, 0x88, 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
};

constexpr std::size_t StubPredicateImm64   = 0x13;
constexpr std::size_t StubResumeSlot       = 0x2A;
constexpr std::size_t StubNativeOffset     = 0x32;
constexpr std::size_t StubNativeSlot       = 0x38;

constexpr std::size_t HookPageBytes  = 4'096;
constexpr std::size_t StubPageOffset = 0x00;

constexpr auto MovabsBefore(const std::uint8_t* code, std::size_t slot) noexcept -> bool {
    return slot >= 2 && code[slot - 2] == 0x48 && code[slot - 1] == 0xB8;
}

constexpr auto JumpBefore(const std::uint8_t* code, std::size_t slot) noexcept -> bool {
    return slot >= 6 && code[slot - 6] == 0xFF && code[slot - 5] == 0x25
        && code[slot - 4] == 0x00 && code[slot - 3] == 0x00
        && code[slot - 2] == 0x00 && code[slot - 1] == 0x00;
}

static_assert(sizeof(DecisionStub) == 0x40);
static_assert(MovabsBefore(DecisionStub, StubPredicateImm64));
static_assert(JumpBefore(DecisionStub, StubResumeSlot));
static_assert(JumpBefore(DecisionStub, StubNativeSlot));
static_assert(StubResumeSlot + 8 == StubNativeOffset && StubNativeOffset + 6 == StubNativeSlot);
// The target id read is the loop's own read at 3DBF3D with r12d swapped for edx.
static_assert(DecisionStub[0x07] == 0x42 && DecisionStub[0x08] == 0x0F && DecisionStub[0x09] == 0xB7
    && DecisionStub[0x0A] == 0x54 && DecisionStub[0x0B] == 0x7E && DecisionStub[0x0C] == 0x54);
static_assert(StubPageOffset + sizeof(DecisionStub) <= HookPageBytes);

// Turns the stub into the native arm alone: jmp short +30h, landing on +32h.
constexpr std::uint8_t StubToNative[]{ 0xEB, 0x30 };
static_assert(static_cast<std::size_t>(0x02 + StubToNative[1]) == StubNativeOffset);

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

struct Config {
    bool enabled    = true;
    bool logRepairs = false;
};

constexpr char DefaultConfigToml[] = R"toml(# Op 1 Item Fix (celestialrayone.op1-item-fix)
#
# Fixes itemstatcost.txt op 1 stats on items. An op 1 stat raises the stats in
# its op stat1..3 by its own value in percent; passive_summ_mastery_total
# ("Total Multiplier to Summon Damage") raises passive_summ_mastery this way.
# On a character, op 1 multiplies the character's total of the raised stat. On
# an item it only multiplies the item's own BASE value of it, and items have no
# base value of stats like passive_summ_mastery, so there it multiplies nothing.
#
# What went wrong:
#   When an op 1 stat changes on an item that also carries the stat it raises,
#   the stat engine decides whether the change may go on to the character. It
#   asked "does this item have any of the raised stat at all?", affixes
#   included, instead of "did op 1 multiply this item's base value?". So the
#   change was held back and the item kept handing the character the value it
#   had before. Players saw it as:
#     - a bonus a plugin puts on such a stat (the Jeweler perks) shown on the
#       item but never reaching the character
#     - an item carrying the op 1 stat socketed into an item that has the
#       raised stat: its part never reached the character
#   An item's own value usually survived loading only by luck: item stats are
#   read in id order, and passive_summ_mastery_total (303) comes before
#   passive_summ_mastery (486).
#
# What the plugin changes:
#   That one decision, for op 1 only, now asks op 1's own question: the change
#   stays on the item only when the item has a base value of the raised stat.
#   Otherwise it reaches the character, where op 1 applies it once, to the
#   character's total. Nothing counts twice: op 1 multiplied nothing on the
#   item. Characters, monsters and every other op keep the game's decision.
#   The Enhanced Damage Fix plugin makes the same kind of correction for op 13
#   damage stats; the two work side by side, in either load order.
#
# Existing characters are fixed when they load: item stats are rebuilt from the
# save and pass through the corrected decision.
#
# Technical: under D2RLoader 1.3.1 the stat engine runs in D2RCore.dll. The
# plugin points op 1's entry of the engine's op switch (D2RCore RVA 0x630430)
# at its own decision, after checking D2RCore's bytes, that entry included. On
# any other D2RCore build it installs nothing and says so in the log and the
# console.
#
# Console command: op1fix (install state and counters)

[op1_item_fix]

# Master switch. false leaves the game completely vanilla.
enabled = true

# Write the first 16 repaired decisions to the plugin log: the items.txt row of
# the item, the op 1 stat and the stat it raises. The first repair is always
# logged once. Leave false unless you are checking a specific item.
log_repairs = false
)toml";

constexpr std::uint32_t MaximumConfigBytes = 32'768;

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

enum class InstallState : std::uint8_t {
    NotLoaded,
    DisabledByConfig,
    CoreMissing,
    UnsupportedCore,
    InstallFailed,
    Active,
    PartiallyActive,
};

using ReadUnitBaseStatFn = std::uint32_t(__fastcall*)(void* unit, std::uint32_t statId, std::uint32_t layer) noexcept;

const D2RL::PluginContext* Context{};
std::uintptr_t             CoreBase{};
std::uintptr_t             CoreSize{};
ReadUnitBaseStatFn         ReadUnitBaseStat{};
Config                     Settings{};
InstallState               State{ InstallState::NotLoaded };
void*                      HookPage{};
bool                       EntryPatched{};
std::int32_t               EntryOurs{};

std::atomic<std::uint64_t> Repaired{};
std::atomic<std::uint64_t> ItemBaseDecisions{};
std::atomic<std::uint64_t> OtherListDecisions{};
std::atomic<std::uint64_t> LoggedRepairs{};
std::atomic<std::int32_t>  LastRepairedRow{ -1 };
std::atomic<std::uint32_t> LastRepairedStat{};
std::atomic<std::uint32_t> LastRepairedTarget{};

constexpr std::uint64_t MaximumLoggedRepairs = 16;

template <typename T>
auto Read(const void* base, std::size_t offset) noexcept -> T {
    T value{};
    std::memcpy(&value, static_cast<const std::uint8_t*>(base) + offset, sizeof(T));
    return value;
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

void ParseBool(std::string_view key, std::string_view value, bool& out) noexcept {
    if (value == "true" || value == "1") {
        out = true;
    } else if (value == "false" || value == "0") {
        out = false;
    } else {
        D2RL::LogWarnF(Context, "Op1ItemFix: %.*s = %.*s is not true or false; keeping %s.",
            static_cast<int>(key.size()), key.data(), static_cast<int>(value.size()), value.data(),
            out ? "true" : "false");
    }
}

void ApplyConfigLine(std::string_view key, std::string_view value) noexcept {
    if (key == "enabled") {
        ParseBool(key, value, Settings.enabled);
    } else if (key == "log_repairs") {
        ParseBool(key, value, Settings.logRepairs);
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
        Context->LogWarn("Op1ItemFix: the config file could not be created; using defaults.");
        return;
    }
    std::string buffer(MaximumConfigBytes, '\0');
    if (!Context->ReadConfig(buffer.data(), static_cast<std::uint32_t>(buffer.size()), nullptr)) {
        Context->LogWarn("Op1ItemFix: the config file could not be read; using defaults.");
        return;
    }
    buffer.resize(std::strlen(buffer.c_str()));
    ParseConfig(buffer);
}

// ---------------------------------------------------------------------------
// The corrected decision
// ---------------------------------------------------------------------------
//
// Called from the stub with the engine's own statList (rdi), the op 1 target
// being decided (word [rsi+r15*2+54h]) and the op 1 stat's id (word [rsi]).
// Returns the al the engine expects at 3DBF25: 1 lets the op 1 stat be
// committed to this statlist, and from there reach the socket host and the
// character; 0 withholds it.
auto __fastcall AllowCommit(const void* statList, std::uint32_t targetStat, std::uint32_t sourceStat) noexcept -> std::uint8_t {
    if (Read<std::uint32_t>(statList, StatListOwnerTypeOffset) != UnitTypeItem) {
        OtherListDecisions.fetch_add(1, std::memory_order_relaxed);
        return 0;  // native: op 1 applied to the accumulated total on this list
    }
    auto* owner = Read<void*>(statList, StatListOwnerOffset);
    const bool ownerIsItem = owner != nullptr && Read<std::uint32_t>(owner, UnitTypeOffset) == UnitTypeItem;
    if (ownerIsItem && static_cast<std::int32_t>(ReadUnitBaseStat(owner, targetStat, 0)) != 0) {
        ItemBaseDecisions.fetch_add(1, std::memory_order_relaxed);
        return 0;  // native: op 1 multiplied the item's base value of the target
    }

    Repaired.fetch_add(1, std::memory_order_relaxed);
    const std::int32_t row = ownerIsItem ? Read<std::int32_t>(owner, UnitClassIdOffset) : -1;
    LastRepairedRow.store(row, std::memory_order_relaxed);
    LastRepairedStat.store(sourceStat, std::memory_order_relaxed);
    LastRepairedTarget.store(targetStat, std::memory_order_relaxed);

    const std::uint64_t logged = LoggedRepairs.fetch_add(1, std::memory_order_relaxed);
    if (logged == 0 || (Settings.logRepairs && logged < MaximumLoggedRepairs)) {
        D2RL::LogInfoF(Context,
            "Op1ItemFix: committed op 1 stat %u (raising stat %u) on items.txt row %d; the game would have "
            "withheld it.",
            sourceStat, targetStat, row);
    }
    return 1;
}

// ---------------------------------------------------------------------------
// D2RCore access
// ---------------------------------------------------------------------------

auto LocateCore() noexcept -> bool {
    HMODULE module = GetModuleHandleW(CoreModuleName);
    if (module == nullptr) return false;
    const auto base = reinterpret_cast<std::uintptr_t>(module);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + static_cast<std::uintptr_t>(dos->e_lfanew));
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;
    CoreBase = base;
    CoreSize = nt->OptionalHeader.SizeOfImage;
    return true;
}

auto CoreAddress(std::uintptr_t rva) noexcept -> std::uintptr_t {
    return CoreBase + rva;
}

auto CoreMatches(const CoreWitness& witness) noexcept -> bool {
    if (witness.rva + witness.size > CoreSize) return false;
    return std::memcmp(reinterpret_cast<const void*>(CoreAddress(witness.rva)), witness.bytes, witness.size) == 0;
}

auto VerifyCore() noexcept -> bool {
    bool ok = true;
    for (const CoreWitness& witness : CoreWitnesses) {
        if (!CoreMatches(witness)) {
            D2RL::LogErrorF(Context,
                "Op1ItemFix: D2RCore %s at RVA 0x%llX does not match D2RCore.dll 1.3.1, or another plugin "
                "already changed it.",
                witness.name, static_cast<unsigned long long>(witness.rva));
            ok = false;
        }
    }
    if (!ok) return false;

    const FARPROC exported = GetProcAddress(reinterpret_cast<HMODULE>(CoreBase), ReadBaseStatExport);
    if (reinterpret_cast<std::uintptr_t>(exported) != CoreAddress(ReadBaseExportRva)) {
        D2RL::LogErrorF(Context,
            "Op1ItemFix: D2RCore export %s is not at RVA 0x%llX; this is not D2RCore.dll 1.3.1.",
            ReadBaseStatExport, static_cast<unsigned long long>(ReadBaseExportRva));
        return false;
    }
    ReadUnitBaseStat = reinterpret_cast<ReadUnitBaseStatFn>(CoreAddress(ReadBaseExportRva));
    return true;
}

// Writes bytes and reads them back. D2RCore is not D2R.exe, so the loader's
// patch API (D2R.exe RVAs only) cannot reach it.
auto WriteBytes(std::uintptr_t address, const std::uint8_t* bytes, std::size_t size) noexcept -> bool {
    auto* target = reinterpret_cast<void*>(address);
    DWORD previous = 0;
    if (!VirtualProtect(target, size, PAGE_EXECUTE_READWRITE, &previous)) return false;
    std::memcpy(target, bytes, size);
    DWORD ignored = 0;
    VirtualProtect(target, size, previous, &ignored);
    FlushInstructionCache(GetCurrentProcess(), target, size);
    return std::memcmp(target, bytes, size) == 0;
}

// The table entry is one aligned dword the dispatch reads with a single load,
// so it is swapped with one interlocked store.
auto WriteEntry(std::int32_t value) noexcept -> bool {
    const std::uintptr_t address = CoreAddress(OpJumpEntry1Rva);
    if ((address & 3U) != 0) return false;
    auto* target = reinterpret_cast<void*>(address);
    DWORD previous = 0;
    if (!VirtualProtect(target, EntrySize, PAGE_EXECUTE_READWRITE, &previous)) return false;
    InterlockedExchange(reinterpret_cast<volatile LONG*>(address), static_cast<LONG>(value));
    DWORD ignored = 0;
    VirtualProtect(target, EntrySize, previous, &ignored);
    return Read<std::int32_t>(target, 0) == value;
}

auto NativeEntry() noexcept -> std::int32_t {
    std::int32_t value = 0;
    std::memcpy(&value, OpJumpEntry1Witness, sizeof(value));
    return value;
}

// ---------------------------------------------------------------------------
// Hook page and table entry
// ---------------------------------------------------------------------------

// A table entry is a signed 32-bit offset from the table.
auto CanEncodeEntry(std::uintptr_t target) noexcept -> bool {
    const std::int64_t delta = static_cast<std::int64_t>(target) - static_cast<std::int64_t>(CoreAddress(OpJumpTableRva));
    return delta >= INT32_MIN && delta <= INT32_MAX;
}

// A page within an entry's reach of the table, above D2RCore first, then below.
auto AllocateNear(std::size_t size) noexcept -> void* {
    SYSTEM_INFO systemInfo{};
    GetSystemInfo(&systemInfo);
    const auto granularity = static_cast<std::uintptr_t>(systemInfo.dwAllocationGranularity);
    const std::uintptr_t hint = CoreAddress(OpJumpTableRva);
    const auto aligned = hint & ~(granularity - 1U);
    for (std::uintptr_t delta = granularity; delta < 0x7000'0000ULL; delta += granularity) {
        const std::uintptr_t candidates[2]{ aligned + delta, delta < aligned ? aligned - delta : 0 };
        for (const std::uintptr_t candidate : candidates) {
            if (candidate == 0 || !CanEncodeEntry(candidate)) continue;
            if (auto* memory = VirtualAlloc(reinterpret_cast<void*>(candidate), size,
                    MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)) {
                return memory;
            }
        }
    }
    return nullptr;
}

auto PageAddress(std::size_t offset) noexcept -> std::uintptr_t {
    return reinterpret_cast<std::uintptr_t>(HookPage) + offset;
}

void WriteQword(std::uint8_t* at, std::uint64_t value) noexcept {
    std::memcpy(at, &value, sizeof(value));
}

auto BuildHookPage() noexcept -> bool {
    HookPage = AllocateNear(HookPageBytes);
    if (HookPage == nullptr) {
        Context->LogError("Op1ItemFix: no hook page was available within reach of D2RCore's op switch table.");
        return false;
    }

    auto* page = static_cast<std::uint8_t*>(HookPage);
    std::memset(page, 0xCC, HookPageBytes);
    std::memcpy(page + StubPageOffset, DecisionStub, sizeof(DecisionStub));
    WriteQword(page + StubPageOffset + StubPredicateImm64, reinterpret_cast<std::uint64_t>(&AllowCommit));
    WriteQword(page + StubPageOffset + StubResumeSlot, CoreAddress(CommitResumeRva));
    WriteQword(page + StubPageOffset + StubNativeSlot, CoreAddress(DefaultArmRva));

    DWORD previous = 0;
    if (!VirtualProtect(page, HookPageBytes, PAGE_EXECUTE_READ, &previous)) {
        Context->LogError("Op1ItemFix: hook page protection could not be finalized.");
        VirtualFree(HookPage, 0, MEM_RELEASE);
        HookPage = nullptr;
        return false;
    }
    FlushInstructionCache(GetCurrentProcess(), page, HookPageBytes);
    return true;
}

// Leaves the stub running the native arm only. After this nothing in the page
// reaches this DLL.
auto RetargetStubToNative() noexcept -> bool {
    if (HookPage == nullptr) return true;
    return WriteBytes(PageAddress(StubPageOffset), StubToNative, sizeof(StubToNative));
}

auto PatchEntry() noexcept -> bool {
    const std::uintptr_t stub = PageAddress(StubPageOffset);
    if (!CanEncodeEntry(stub)) {
        Context->LogError("Op1ItemFix: the hook page is out of reach of the op switch table.");
        return false;
    }
    EntryOurs = static_cast<std::int32_t>(static_cast<std::int64_t>(stub) - static_cast<std::int64_t>(CoreAddress(OpJumpTableRva)));
    if (!WriteEntry(EntryOurs)) {
        Context->LogError("Op1ItemFix: op 1's entry in D2RCore's op switch could not be written.");
        return false;
    }
    EntryPatched = true;
    return true;
}

// Puts the native entry back, only while it still holds this plugin's.
auto RestoreEntry() noexcept -> bool {
    if (!EntryPatched) return true;
    if (Read<std::int32_t>(reinterpret_cast<const void*>(CoreAddress(OpJumpEntry1Rva)), 0) != EntryOurs) {
        Context->LogError("Op1ItemFix: op 1's entry no longer holds this plugin's; left as found.");
        return false;
    }
    if (!WriteEntry(NativeEntry())) return false;
    EntryPatched = false;
    return true;
}

// Returns false only when nothing can reach this DLL, so the loader may unload it.
auto Install() noexcept -> bool {
    if (!BuildHookPage()) {
        State = InstallState::InstallFailed;
        return false;
    }
    if (PatchEntry()) {
        State = InstallState::Active;
        return true;
    }
    // Only an entry holding this plugin's offset can reach the page. Point the
    // page at the native arm first, then make sure the native entry is back.
    RetargetStubToNative();
    const bool untouched = CoreMatches(W(OpJumpEntry1Rva, OpJumpEntry1Witness, "op 1 switch entry"));
    if (untouched || WriteEntry(NativeEntry())) {
        State = InstallState::InstallFailed;
        return false;
    }
    Context->LogError("Op1ItemFix: the entry could not be restored; staying loaded so it keeps a valid target.");
    State = InstallState::PartiallyActive;
    return true;
}

// ---------------------------------------------------------------------------
// Console command
// ---------------------------------------------------------------------------

auto StateName() noexcept -> const char* {
    switch (State) {
    case InstallState::DisabledByConfig: return "disabled by config";
    case InstallState::CoreMissing:      return "D2RCore.dll not found, nothing installed";
    case InstallState::UnsupportedCore:  return "unsupported D2RCore build, nothing installed (see log)";
    case InstallState::InstallFailed:    return "install failed, nothing active (see log)";
    case InstallState::Active:           return "active";
    case InstallState::PartiallyActive:  return "PARTIALLY active, see log";
    default:                             return "not loaded";
    }
}

auto __cdecl StatusCommand(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command, void*) noexcept
        -> D2RL::ConsoleCommandResult {
    if (command == nullptr || command->plugin == nullptr) return D2RL::ConsoleCommandResult::Failed;

    char line[384];
    std::snprintf(line, sizeof(line), "Op 1 Item Fix: %s%s", StateName(),
        EntryPatched ? " (D2RCore op switch entry 1 at RVA 0x630430)." : ".");
    command->plugin->WriteConsoleMessage(line);

    std::snprintf(line, sizeof(line),
        "  repaired: %llu | items with a base value of the raised stat, left to the game %llu | "
        "character and monster lists, left to the game %llu",
        static_cast<unsigned long long>(Repaired.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(ItemBaseDecisions.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(OtherListDecisions.load(std::memory_order_relaxed)));
    command->plugin->WriteConsoleMessage(line);

    const std::int32_t row = LastRepairedRow.load(std::memory_order_relaxed);
    if (Repaired.load(std::memory_order_relaxed) != 0) {
        std::snprintf(line, sizeof(line), "  last repair: op 1 stat %u raising stat %u, items.txt row %d.",
            LastRepairedStat.load(std::memory_order_relaxed), LastRepairedTarget.load(std::memory_order_relaxed), row);
        command->plugin->WriteConsoleMessage(line);
    }
    return D2RL::ConsoleCommandResult::Handled;
}

void RegisterStatusCommand() noexcept {
    if (!Context->RegisterConsoleCommand("op1fix", &StatusCommand,
            "Shows Op 1 Item Fix install state and how many op 1 commits it repaired.")) {
        Context->LogWarn("Op1ItemFix: the console command was refused.");
    }
}

constexpr D2RL::PluginInfo Info{
    .infoSize    = D2RL::PluginInfoSize,
    .abiVersion  = D2RL_PLUGIN_ABI_VERSION,
    .id          = "celestialrayone.op1-item-fix",
    .name        = "Op 1 Item Fix",
    .version     = "1.0.0",
    .author      = "CelestialRayOne",
    .description = "itemstatcost.txt op 1 stats on items reach the character even when the item also carries the "
                   "stat they raise.",
    .flags       = D2RL::PluginFlags::Shared | D2RL::PluginFlags::NativeHooks,
};

}  // namespace

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* {
    return &Info;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
    if (!D2RL::HasContext(context)) return false;
    Context = context;

    ReadConfiguration();
    RegisterStatusCommand();

    if (!Settings.enabled) {
        State = InstallState::DisabledByConfig;
        Context->LogInfo("Op1ItemFix: disabled by config.");
        return true;
    }
    if (!LocateCore()) {
        State = InstallState::CoreMissing;
        Context->LogError("Op1ItemFix: D2RCore.dll is not loaded; nothing was installed.");
        return true;
    }
    if (!VerifyCore()) {
        State = InstallState::UnsupportedCore;
        Context->LogError("Op1ItemFix: this D2RCore build does not match 1.3.1; nothing was installed.");
        return true;
    }
    if (!Install()) {
        Context->LogError("Op1ItemFix: install failed; the game is unchanged.");
        return false;
    }

    D2RL::LogInfoF(Context,
        "Op1ItemFix: %s. D2RCore's op switch now commits op 1 stats on items without a base value of the stat "
        "they raise; characters, monsters and every other op keep the game's decision.",
        StateName());
    return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
    if (Context == nullptr) return;
    RetargetStubToNative();
    RestoreEntry();
    // The hook page is deliberately kept: a thread may be inside the stub now.
}

}  // namespace CelestialRayOne::Op1ItemFix
