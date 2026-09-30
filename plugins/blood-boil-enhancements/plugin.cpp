// Blood Boil
//
// Two server-side changes to skills.txt srvdofunc 160 (WarDoBloodBoil). Both
// live inside Blood Boil's per-demon step and nowhere else.
//
//   1. sksrc() in the calcs Blood Boil evaluates on a demon reads the caster.
//      calc1/calc2/calc3 (the sacrifice percent) and aurarangecalc (the radius)
//      are evaluated with the demon as the formula unit. stat(), skill(),
//      rand() and every other function in them keep reading the demon. Only
//      sksrc() is pointed at the Warlock who cast Blood Boil, read at the moment
//      the skill hits.
//
//   2. Lethal sacrifice. Vanilla skips a demon completely (no sacrifice, no area
//      damage) when the sacrifice would kill it. When the caster has a positive
//      value in a configurable stat (default 523) that skip is removed: the
//      demon takes the whole sacrifice, dies if it runs out of life, and its
//      area damage still goes off.
//
// Everything below was read out of the live D2R 3.3 D2RLoader image (base
// 0x140000000) and disassembled before anything here relies on it.
//
// ---------------------------------------------------------------------------
// Blood Boil, 3.3
// ---------------------------------------------------------------------------
//   srvdofunc table 0x238EA00, slot 160 at 0x238EF00 -> 0x520F80. The handler
//   returns unless the caster is a player, counts the demons (of pettype when
//   that column is set, pooled pets included), picks calc1/calc2/calc3 for
//   1/2/3 demons and calc1 otherwise, and walks the demons with the per-demon
//   callback 0x521320 and the block {skills.txt record, skill id, level, calc
//   index}.
//
//   0x521320(game, caster, demon, block) keeps game in r15, the caster in r12,
//   the demon in rdi and the block in rbx for its whole body:
//
//     52138D  call 2F4D20              demon max life -> rsi
//     52139F  call 2F5020              demon life, stat 6 -> r14d
//     5213C1  mov  r8d,[rdx+r8*4+190h] calc1/2/3 pool offset
//     5213C9  mov  rdx,rdi             the demon is the formula unit
//     5213CC  call 3B5160              site A: the sacrifice percent
//             sacrifice = max life * percent / 100 (three overflow branches)
//     521495  lea  eax,[rdx+1]
//     521498  cmp  r14d,eax
//     52149B  jle  52171F              site C: "would die", skip everything
//     5214AA  call 51EAD0              the sacrifice, r9 = percent
//     52154C  call 3B5160, rdx = r12   calc4 (next hit delay), on the caster
//     521606  mov  rdx,rdi
//     521614  mov  r8d,[rcx+84h]       aurarangecalc
//     521623  call 3B5160              site B: the radius, on the demon
//     521694  call 4327D0              area damage around the demon
//
//   The percent is the only thing calc1-3 produce. The life it is applied to
//   is always read from the demon by 2F4D20, whatever unit the calc runs on.
//
// ---------------------------------------------------------------------------
// Why sksrc() read the demon
// ---------------------------------------------------------------------------
//   Formulas are compiled bytecode run by the interpreter 0xA234B0 against a
//   context with seven unit slots. Which slot each function reads, from the
//   nine callbacks at 0x1D09C50 and the token resolver 0x3B61D0:
//
//     +08 rand() seed   +10 stat()    +18 skill(), sklvl(), lvl/clc tokens
//     +20 callback 8    +28 sksrc()   +30 miss()    +38 callback 8
//
//   The skills.txt evaluator 0x3B5160 writes the one unit it is given into all
//   seven, so in a skills.txt calc sksrc() always reads the same unit as
//   skill(). Blood Boil hands it the demon. The only evaluator that takes a
//   second, caller-supplied unit is 0x3B4D10, and it puts that unit in +28
//   (and +38): that is the "source" the sksrc documentation describes for
//   monster-scope formulas. Blood Boil simply never uses it.
//
// ---------------------------------------------------------------------------
// Sites A and B: the call is redirected, the evaluator is reproduced
// ---------------------------------------------------------------------------
//   Both 5-byte calls to 3B5160 become calls into a relay on a page this
//   plugin allocates within rel32 reach:
//
//     +00  4C 89 64 24 30       mov  [rsp+30h],r12   ; 6th argument = caster
//     +05  0F 1F 44 00 00       nop
//     +0A  FF 25 00 00 00 00    jmp  [rip+0]
//     +10  dq                   EvaluateWithCasterSource
//
//   [rsp+30h] at the relay is the callback's own [rsp+28h], the sixth-argument
//   slot of its outgoing area (the callback passes eleven arguments at
//   521694), which holds nothing across a call. The tail jump leaves the
//   callback's return address on top, so EvaluateWithCasterSource runs as if
//   the callback had called it with one more argument.
//
//   EvaluateWithCasterSource reproduces 3B5160 line for line, with the single
//   difference that slot +28 holds the caster. It calls the same interpreter
//   with the same resolver, callback table, count and context vtable, so
//   every function and token, and any plugin hook on them (playermode-
//   exposing hooks the stat callback 0x3B33F0), behaves as in a native call:
//
//     3B5188  call 300A90              GetDataTables(dataContext)
//     3B5192  cmp  rbx,[rax+140h]      offset >= pool size -> return 0
//     3B51A2  lea  rax,[1D0AEB8]       context vtable at +00
//     3B51A9  mov  byte [rsp+0A4h],0   +44
//     3B51BD  mov  [rsp+0ACh],eax      +4C skill level
//     3B51C4  mov  [rsp+0A0h],eax      +40 skill level
//     3B51CB  mov  [rsp+0A8h],r14d     +48 skill id
//     3B51D3  [rsp+68h..98h] = rdi     +08..+38 the unit, seven times
//     3B5202  length = pool size - offset, asserts unless 0..INT32_MAX
//     3B526C  add  rdx,[rdi]           code = pool base (+138h) + offset
//     3B5296  call A234B0              (ctx, code, length, 3B61D0, 1D09C50, 9, &context)
//
// ---------------------------------------------------------------------------
// Site C: the lethal sacrifice
// ---------------------------------------------------------------------------
//   52149B, 6 bytes, 0F 8E 7E 02 00 00 -> 0F 8E <rel32>: the same jle, aimed at
//   a relay, so the relay only runs when vanilla would skip the demon. It asks
//   LetSacrificeKill(game, caster, demon) and continues at 5214A1 (sacrifice,
//   then area damage) or at 52171F (the vanilla skip):
//
//     +00  41 51                push r9              ; percent, live into 5214AA
//     +02  48 83 EC 28          sub  rsp,28h         ; rsp is 16-aligned at 52149B
//     +06  4C 89 F9             mov  rcx,r15         ; game
//     +09  4C 89 E2             mov  rdx,r12         ; caster
//     +0C  49 89 F8             mov  r8,rdi          ; demon
//     +0F  48 B8 imm64          mov  rax,LetSacrificeKill
//     +19  FF D0                call rax
//     +1B  48 83 C4 28          add  rsp,28h
//     +1F  41 59                pop  r9
//     +21  84 C0                test al,al
//     +23  74 0E                je   +33h
//     +25  FF 25 00 00 00 00    jmp  [rip+0]         ; 5214A1
//     +33  FF 25 00 00 00 00    jmp  [rip+0]         ; 52171F
//
//   rax, rcx, rdx, r8, r10 and r11 are dead at both targets; r9 is the only
//   live volatile register and is restored. No branch in the callback lands
//   inside any of the three patched windows.
//
//   Why a demon may die inside the walk:
//     - The sacrifice 0x51EAD0 is an ordinary damage packet (HitFlags 0x1000,
//       ResultFlags 0x21, life damage at +12Ch) run through 0x44CE80 and
//       0x44A9B0, the same two calls the holy auras use for their damage, so a
//       lethal sacrifice kills through the normal death path.
//     - Both demon walks (inline in 0x4FFA70, and 0x5014C0) load the next demon
//       before they call the callback, so the current demon dying cannot break
//       the walk. Both loops are fingerprinted below.
//     - A demon that is already dead (0x34C2C0) keeps the vanilla skip, so a
//       corpse still in the pet list never fires the area damage.
//
//   The stat is read on the caster with 0x2F5020 (STATLIST_GetUnitStat, the
//   loader thunk to ReadWideUnitStat), which is what stat('x'.accr) reads, and
//   is bounded by the itemstatcost row count at DataTables+0x1260, the same
//   bound the stat() callback applies at 0x3B344B.
//
// Neighbours: no other CelestialRayOne plugin patches 0x520F80..0x521750 or
// 0x3B5160.

#include <D2RLPlugin/api.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

namespace CelestialRayOne::BloodBoil {
namespace {

// ---------------------------------------------------------------------------
// Native anchors
// ---------------------------------------------------------------------------

constexpr std::uint64_t SrvDoTableRva         = 0x238EA00;
constexpr std::uint64_t BloodBoilSrvDoFunc    = 160;
constexpr std::uint64_t SrvDoSlotRva          = SrvDoTableRva + BloodBoilSrvDoFunc * 8;
constexpr std::uint64_t BloodBoilHandlerRva   = 0x520F80;
static_assert(SrvDoSlotRva == 0x238EF00);

constexpr std::uint64_t CallbackRva           = 0x521320;  // per-demon callback
constexpr std::uint64_t PercentCallRva        = 0x5213CC;  // site A
constexpr std::uint64_t GateWindowRva         = 0x5213D1;
constexpr std::uint64_t GateJumpRva           = 0x52149B;  // site C
constexpr std::uint64_t ProceedRva            = 0x5214A1;  // sacrifice, then area damage
constexpr std::uint64_t SkipRva               = 0x52171F;  // vanilla skip
constexpr std::uint64_t RadiusWindowRva       = 0x521603;
constexpr std::uint64_t RadiusCallRva         = 0x521623;  // site B

constexpr std::uint64_t EvaluateSkillCalcRva  = 0x3B5160;
constexpr std::uint64_t GetDataTablesRva      = 0x300A90;
constexpr std::uint64_t BbeInterpreterRva     = 0xA234B0;
constexpr std::uint64_t BbeResolverRva        = 0x3B61D0;
constexpr std::uint64_t BbeCallbackTableRva   = 0x1D09C50;
constexpr std::uint64_t SkillScopeVtableRva   = 0x1D0AEB8;
constexpr std::int32_t  BbeCallbackCount      = 9;

constexpr std::uint64_t StatRowCountReadRva   = 0x3B3446;
constexpr std::uint64_t GetUnitStatRva        = 0x2F5020;
constexpr std::uint64_t UnitIsDeadRva         = 0x34C2C0;
constexpr std::uint64_t TypedDemonLoopRva     = 0x4FFAD1;
constexpr std::uint64_t PooledDemonLoopRva    = 0x5014F6;

constexpr std::size_t   SkillPoolBaseOffset   = 0x138;
constexpr std::size_t   SkillPoolSizeOffset   = 0x140;
constexpr std::size_t   StatRowCountOffset    = 0x1260;
constexpr std::size_t   GameDataContextOffset = 0x106;

constexpr std::uint32_t CallSize = 5;
constexpr std::uint32_t JleSize  = 6;

// ---------------------------------------------------------------------------
// Witnesses, byte for byte from the live image
// ---------------------------------------------------------------------------

// 0x521320..0x5213D1: callback prologue, the register assignment (rbx block,
// rdi demon, r12 caster, r15 game), both life reads and site A.
constexpr std::uint8_t CallbackHeadWitness[]{
    0x4D, 0x85, 0xC0, 0x0F, 0x84, 0x23, 0x04, 0x00, 0x00, 0x4C, 0x8B, 0xDC,
    0x55, 0x53, 0x57, 0x41, 0x54, 0x41, 0x57, 0x49, 0x8D, 0xAB, 0xB8, 0xFE,
    0xFF, 0xFF, 0x48, 0x81, 0xEC, 0x20, 0x02, 0x00, 0x00, 0x48, 0x8B, 0x05,
    0x80, 0x9F, 0x4A, 0x02, 0x48, 0x33, 0xC4, 0x48, 0x89, 0x85, 0x00, 0x01,
    0x00, 0x00, 0x49, 0x8B, 0xD9, 0x49, 0x8B, 0xF8, 0x4C, 0x8B, 0xE2, 0x4C,
    0x8B, 0xF9, 0x48, 0x85, 0xDB, 0x0F, 0x84, 0xC8, 0x03, 0x00, 0x00, 0x41,
    0x83, 0x79, 0x10, 0x02, 0x49, 0x89, 0x73, 0xD0, 0x4D, 0x89, 0x73, 0xC8,
    0x76, 0x14, 0x48, 0x8D, 0x4C, 0x24, 0x60, 0xC6, 0x44, 0x24, 0x60, 0x00,
    0xE8, 0xAB, 0xC6, 0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x01, 0xCC, 0x48, 0x8B,
    0xCF, 0xE8, 0x8E, 0x39, 0xDD, 0xFF, 0x45, 0x33, 0xC0, 0x48, 0x63, 0xF0,
    0x48, 0x8B, 0xCF, 0x41, 0x8D, 0x50, 0x06, 0xE8, 0x7C, 0x3C, 0xDD, 0xFF,
    0x48, 0x8B, 0x13, 0x44, 0x8B, 0xF0, 0x8B, 0x4B, 0x0C, 0x4C, 0x63, 0x43,
    0x10, 0x44, 0x8B, 0x4B, 0x08, 0x89, 0x4C, 0x24, 0x20, 0x41, 0x0F, 0xB6,
    0x8F, 0x06, 0x01, 0x00, 0x00, 0x46, 0x8B, 0x84, 0x82, 0x90, 0x01, 0x00,
    0x00, 0x48, 0x8B, 0xD7, 0xE8, 0x8F, 0x3D, 0xE9, 0xFF,
};
constexpr std::size_t PercentCallOffset = PercentCallRva - CallbackRva;

// 0x5213D1..0x5214AF: the percent math, the gate and the sacrifice call.
constexpr std::uint8_t GateWindowWitness[]{
    0x4C, 0x63, 0xC8, 0x81, 0xFE, 0x00, 0x00, 0x10, 0x00, 0x7E, 0x4E, 0x8B,
    0xCE, 0x83, 0xE1, 0xF0, 0x81, 0xF9, 0x40, 0x06, 0x00, 0x00, 0x7D, 0x2A,
    0x4D, 0x8B, 0xC1, 0x48, 0xB8, 0x0B, 0xD7, 0xA3, 0x70, 0x3D, 0x0A, 0xD7,
    0xA3, 0x4C, 0x0F, 0xAF, 0xC6, 0x49, 0xF7, 0xE8, 0x49, 0x03, 0xD0, 0x48,
    0xC1, 0xFA, 0x06, 0x48, 0x8B, 0xC2, 0x48, 0xC1, 0xE8, 0x3F, 0x48, 0x03,
    0xD0, 0xE9, 0x82, 0x00, 0x00, 0x00, 0xB8, 0x1F, 0x85, 0xEB, 0x51, 0xF7,
    0xEE, 0xC1, 0xFA, 0x05, 0x8B, 0xC2, 0xC1, 0xE8, 0x1F, 0x03, 0xD0, 0x41,
    0x0F, 0xAF, 0xD1, 0xEB, 0x6B, 0x41, 0x81, 0xF9, 0x00, 0x00, 0x01, 0x00,
    0x7E, 0x4B, 0x41, 0x8B, 0xC1, 0x83, 0xE0, 0xF0, 0x3D, 0x40, 0x06, 0x00,
    0x00, 0x7D, 0x27, 0x49, 0x8B, 0xC9, 0x48, 0xB8, 0x0B, 0xD7, 0xA3, 0x70,
    0x3D, 0x0A, 0xD7, 0xA3, 0x48, 0x0F, 0xAF, 0xCE, 0x48, 0xF7, 0xE9, 0x48,
    0x03, 0xD1, 0x48, 0xC1, 0xFA, 0x06, 0x48, 0x8B, 0xC2, 0x48, 0xC1, 0xE8,
    0x3F, 0x48, 0x03, 0xD0, 0xEB, 0x2E, 0xB8, 0x1F, 0x85, 0xEB, 0x51, 0x41,
    0xF7, 0xE9, 0xC1, 0xFA, 0x05, 0x8B, 0xC2, 0xC1, 0xE8, 0x1F, 0x03, 0xD0,
    0x0F, 0xAF, 0xD6, 0xEB, 0x17, 0x41, 0x8B, 0xC9, 0xB8, 0x1F, 0x85, 0xEB,
    0x51, 0x0F, 0xAF, 0xCE, 0xF7, 0xE9, 0xC1, 0xFA, 0x05, 0x8B, 0xC2, 0xC1,
    0xE8, 0x1F, 0x03, 0xD0, 0x8D, 0x42, 0x01, 0x44, 0x3B, 0xF0, 0x0F, 0x8E,
    0x7E, 0x02, 0x00, 0x00, 0x4C, 0x8B, 0xC7, 0x49, 0x8B, 0xD4, 0x49, 0x8B,
    0xCF, 0xE8, 0x21, 0xD6, 0xFF, 0xFF,
};
constexpr std::size_t GateJumpOffset = GateJumpRva - GateWindowRva;

// 0x521603..0x521643: aurarangecalc on the demon (site B), then 0x432C40 with
// rdx = r12, proving r12 still holds the caster there.
constexpr std::uint8_t RadiusWindowWitness[]{
    0x48, 0x8B, 0x0B, 0x48, 0x8B, 0xD7, 0x8B, 0x43, 0x0C, 0x44, 0x8B, 0x4B,
    0x08, 0x89, 0x44, 0x24, 0x20, 0x44, 0x8B, 0x81, 0x84, 0x00, 0x00, 0x00,
    0x41, 0x0F, 0xB6, 0x8F, 0x06, 0x01, 0x00, 0x00, 0xE8, 0x38, 0x3B, 0xE9,
    0xFF, 0x8B, 0x4B, 0x0C, 0x49, 0x8B, 0xD4, 0x44, 0x8B, 0x4B, 0x08, 0x8B,
    0xF8, 0x4C, 0x8B, 0x03, 0x89, 0x4C, 0x24, 0x20, 0x49, 0x8B, 0xCF, 0xE8,
    0xFD, 0x15, 0xF1, 0xFF,
};
constexpr std::size_t RadiusCallOffset = RadiusCallRva - RadiusWindowRva;

// 0x3B5160..0x3B52B8: the whole skills.txt evaluator this plugin reproduces.
// Its rel32 and RIP-relative operands pin 0x300A90, 0x1D0AEB8, 0x3B61D0,
// 0x1D09C50 and 0xA234B0.
constexpr std::uint8_t EvaluateSkillCalcWitness[]{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89,
    0x74, 0x24, 0x20, 0x57, 0x41, 0x56, 0x41, 0x57, 0x48, 0x81, 0xEC, 0xB0,
    0x00, 0x00, 0x00, 0x45, 0x8B, 0xF1, 0x41, 0x8B, 0xD8, 0x48, 0x8B, 0xFA,
    0x44, 0x0F, 0xB6, 0xF9, 0xE8, 0x03, 0xB9, 0xF4, 0xFF, 0x48, 0x8B, 0xE8,
    0x8B, 0xF3, 0x48, 0x3B, 0x98, 0x40, 0x01, 0x00, 0x00, 0x72, 0x07, 0x33,
    0xC0, 0xE9, 0xF9, 0x00, 0x00, 0x00, 0x48, 0x8D, 0x05, 0x0F, 0x5D, 0x95,
    0x01, 0xC6, 0x84, 0x24, 0xA4, 0x00, 0x00, 0x00, 0x00, 0x48, 0x89, 0x44,
    0x24, 0x60, 0x8B, 0x84, 0x24, 0xF0, 0x00, 0x00, 0x00, 0x89, 0x84, 0x24,
    0xAC, 0x00, 0x00, 0x00, 0x89, 0x84, 0x24, 0xA0, 0x00, 0x00, 0x00, 0x44,
    0x89, 0xB4, 0x24, 0xA8, 0x00, 0x00, 0x00, 0x48, 0x89, 0x7C, 0x24, 0x68,
    0x48, 0x89, 0x7C, 0x24, 0x78, 0x48, 0x89, 0xBC, 0x24, 0x88, 0x00, 0x00,
    0x00, 0x48, 0x89, 0xBC, 0x24, 0x90, 0x00, 0x00, 0x00, 0x48, 0x89, 0x7C,
    0x24, 0x70, 0x48, 0x89, 0xBC, 0x24, 0x80, 0x00, 0x00, 0x00, 0x48, 0x89,
    0xBC, 0x24, 0x98, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x9D, 0x40, 0x01, 0x00,
    0x00, 0x48, 0x2B, 0xDE, 0x48, 0x63, 0xC3, 0x48, 0x3B, 0xC3, 0x75, 0x04,
    0x85, 0xDB, 0x79, 0x1A, 0x48, 0x8D, 0x8C, 0x24, 0xE0, 0x00, 0x00, 0x00,
    0xC6, 0x84, 0x24, 0xE0, 0x00, 0x00, 0x00, 0x00, 0xE8, 0x33, 0x17, 0xCD,
    0xFF, 0x84, 0xC0, 0x74, 0x01, 0xCC, 0x48, 0x8D, 0xBD, 0x38, 0x01, 0x00,
    0x00, 0x48, 0x89, 0x74, 0x24, 0x40, 0x48, 0x3B, 0x77, 0x08, 0x72, 0x1E,
    0x48, 0x8D, 0x44, 0x24, 0x40, 0x48, 0x89, 0x7C, 0x24, 0x50, 0x48, 0x8D,
    0x4C, 0x24, 0x48, 0x48, 0x89, 0x44, 0x24, 0x48, 0xE8, 0xE3, 0xF0, 0xFF,
    0xFF, 0x84, 0xC0, 0x74, 0x01, 0xCC, 0x48, 0x8B, 0x54, 0x24, 0x40, 0x48,
    0x8D, 0x44, 0x24, 0x60, 0x48, 0x03, 0x17, 0x4C, 0x8D, 0x0D, 0x5A, 0x0F,
    0x00, 0x00, 0x48, 0x89, 0x44, 0x24, 0x30, 0x44, 0x8B, 0xC3, 0x48, 0x8D,
    0x05, 0xCB, 0x49, 0x95, 0x01, 0xC7, 0x44, 0x24, 0x28, 0x09, 0x00, 0x00,
    0x00, 0x41, 0x0F, 0xB6, 0xCF, 0x48, 0x89, 0x44, 0x24, 0x20, 0xE8, 0x15,
    0xE2, 0x66, 0x00, 0x4C, 0x8D, 0x9C, 0x24, 0xB0, 0x00, 0x00, 0x00, 0x49,
    0x8B, 0x5B, 0x20, 0x49, 0x8B, 0x6B, 0x28, 0x49, 0x8B, 0x73, 0x38, 0x49,
    0x8B, 0xE3, 0x41, 0x5F, 0x41, 0x5E, 0x5F, 0xC3,
};

// 0x3B3446: call GetDataTables / mov rbp,[rax+1260h], the itemstatcost row
// count read inside the stat() callback.
constexpr std::uint8_t StatRowCountWitness[]{
    0xE8, 0x45, 0xD6, 0xF4, 0xFF, 0x48, 0x8B, 0xA8, 0x60, 0x12, 0x00, 0x00,
};

// 0x4FFAD1 and 0x5014F6: both demon walks, each loading the next node
// (mov rbx,[node+18h]) before it calls the callback.
constexpr std::uint8_t TypedDemonLoopWitness[]{
    0xF6, 0x01, 0x01, 0x48, 0x8B, 0x59, 0x18, 0x75, 0x25, 0x44, 0x8B, 0x41,
    0x04, 0xBA, 0x01, 0x00, 0x00, 0x00, 0x48, 0x8B, 0xCE, 0xE8, 0x95, 0x03,
    0xF9, 0xFF, 0x48, 0x85, 0xC0, 0x74, 0x0F, 0x4D, 0x8B, 0xCE, 0x4C, 0x8B,
    0xC0, 0x48, 0x8B, 0xD5, 0x48, 0x8B, 0xCE, 0x41, 0xFF, 0xD7, 0x48, 0x8B,
    0xCB, 0x48, 0x85, 0xDB, 0x75, 0xCA,
};
constexpr std::uint8_t PooledDemonLoopWitness[]{
    0x48, 0x8B, 0x58, 0x18, 0x40, 0x84, 0xF6, 0x75, 0x05, 0xF6, 0x00, 0x01,
    0x75, 0x25, 0x44, 0x8B, 0x40, 0x04, 0xBA, 0x01, 0x00, 0x00, 0x00, 0x48,
    0x8B, 0xCF, 0xE8, 0x6B, 0xE9, 0xF8, 0xFF, 0x48, 0x85, 0xC0, 0x74, 0x0F,
    0x4C, 0x8B, 0xCD, 0x4C, 0x8B, 0xC0, 0x49, 0x8B, 0xD7, 0x48, 0x8B, 0xCF,
    0x41, 0xFF, 0xD6, 0x48, 0x8B, 0xC3, 0x48, 0x85, 0xDB, 0x75, 0xC5,
};

// 0x34C2C0: SUNIT_IsDead entry, 32 bytes.
constexpr std::uint8_t UnitIsDeadWitness[]{
    0x48, 0x83, 0xEC, 0x28, 0x48, 0x85, 0xC9, 0x75, 0x1D, 0x88, 0x4C, 0x24,
    0x30, 0x48, 0x8D, 0x4C, 0x24, 0x30, 0xE8, 0x59, 0x94, 0xFF, 0xFF, 0x84,
    0xC0, 0x74, 0x4D, 0xCC, 0xB8, 0x01, 0x00, 0x00,
};

// 0x2F5020: loader thunk to D2RCore!ReadWideUnitStat (jmp [rip+disp32]).
constexpr std::uint8_t GetUnitStatThunkWitness[]{ 0xFF, 0x25 };

constexpr std::uint8_t PercentCallOriginal[]{ 0xE8, 0x8F, 0x3D, 0xE9, 0xFF };
constexpr std::uint8_t RadiusCallOriginal[]{ 0xE8, 0x38, 0x3B, 0xE9, 0xFF };
constexpr std::uint8_t GateJumpOriginal[]{ 0x0F, 0x8E, 0x7E, 0x02, 0x00, 0x00 };

static_assert(PercentCallOffset + CallSize == sizeof(CallbackHeadWitness));
static_assert(RadiusCallOffset + CallSize <= sizeof(RadiusWindowWitness));
static_assert(GateJumpOffset + JleSize <= sizeof(GateWindowWitness));

constexpr auto SameBytes(const std::uint8_t* a, const std::uint8_t* b, std::size_t n) noexcept -> bool {
    for (std::size_t i = 0; i < n; ++i) {
        if (a[i] != b[i]) return false;
    }
    return true;
}
static_assert(SameBytes(CallbackHeadWitness + PercentCallOffset, PercentCallOriginal, CallSize));
static_assert(SameBytes(RadiusWindowWitness + RadiusCallOffset, RadiusCallOriginal, CallSize));
static_assert(SameBytes(GateWindowWitness + GateJumpOffset, GateJumpOriginal, JleSize));

// ---------------------------------------------------------------------------
// Relays
// ---------------------------------------------------------------------------

constexpr std::uint8_t SourceStub[]{
    0x4C, 0x89, 0x64, 0x24, 0x30,                   // mov  [rsp+30h], r12
    0x0F, 0x1F, 0x44, 0x00, 0x00,                   // nop
    0xFF, 0x25, 0x00, 0x00, 0x00, 0x00,             // jmp  [rip+0]
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // dq   target
};
constexpr std::size_t SourceTargetSlot = 0x10;
static_assert(sizeof(SourceStub) == 0x18);
static_assert(SourceStub[SourceTargetSlot - 6] == 0xFF && SourceStub[SourceTargetSlot - 5] == 0x25);
static_assert(SourceTargetSlot % 8 == 0);

constexpr std::uint8_t GateStub[]{
    0x41, 0x51, 0x48, 0x83, 0xEC, 0x28, 0x4C, 0x89, 0xF9, 0x4C, 0x89, 0xE2,
    0x49, 0x89, 0xF8, 0x48, 0xB8, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22,
    0x11, 0xFF, 0xD0, 0x48, 0x83, 0xC4, 0x28, 0x41, 0x59, 0x84, 0xC0, 0x74,
    0x0E, 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00,
};
constexpr std::size_t GatePredicateImm64 = 0x11;
constexpr std::size_t GateProceedSlot    = 0x2B;
constexpr std::size_t GateSkipOffset     = 0x33;
constexpr std::size_t GateSkipSlot       = 0x39;
static_assert(sizeof(GateStub) == 0x41);
static_assert(GateStub[GatePredicateImm64 - 2] == 0x48 && GateStub[GatePredicateImm64 - 1] == 0xB8);
static_assert(GateStub[GateProceedSlot - 6] == 0xFF && GateStub[GateProceedSlot - 5] == 0x25);
static_assert(GateStub[GateSkipSlot - 6] == 0xFF && GateStub[GateSkipSlot - 5] == 0x25);
static_assert(GateSkipOffset == GateSkipSlot - 6);
// je +0Eh at +23h lands on the skip jump.
static_assert(GateStub[0x23] == 0x74 && 0x25 + GateStub[0x24] == GateSkipOffset);

// Turns the gate relay into the vanilla skip alone: jmp short to +33h.
constexpr std::uint8_t GateToVanilla[]{ 0xEB, 0x31 };
static_assert(2 + GateToVanilla[1] == GateSkipOffset);

constexpr std::size_t HookPageBytes    = 4'096;
constexpr std::size_t SourceStubOffset = 0x00;
constexpr std::size_t GateStubOffset   = 0x40;
static_assert(SourceStubOffset + sizeof(SourceStub) <= GateStubOffset);
static_assert(GateStubOffset + sizeof(GateStub) <= HookPageBytes);
static_assert(GateStubOffset % 16 == 0);
static_assert(sizeof(GateToVanilla) <= sizeof(std::uint64_t));

// ---------------------------------------------------------------------------
// The skills.txt formula context, as 0x3B5160 builds it at rsp+60h
// ---------------------------------------------------------------------------

struct alignas(16) SkillScopeContext {
    std::uintptr_t vtable;       // +00 0x1D0AEB8
    void*          units[7];     // +08..+38
    std::int32_t   skillLevel;   // +40
    std::uint8_t   scopeFlag;    // +44, 0 here (the item evaluator writes 1)
    std::uint8_t   padding[3];   // +45
    std::int32_t   skillId;      // +48
    std::int32_t   skillLevel2;  // +4C
};
static_assert(sizeof(SkillScopeContext) == 0x50);
static_assert(offsetof(SkillScopeContext, units) == 0x08);
static_assert(offsetof(SkillScopeContext, skillLevel) == 0x40);
static_assert(offsetof(SkillScopeContext, scopeFlag) == 0x44);
static_assert(offsetof(SkillScopeContext, skillId) == 0x48);
static_assert(offsetof(SkillScopeContext, skillLevel2) == 0x4C);

constexpr std::size_t SourceSlot = 4;  // units[4] = +28, the slot sksrc() reads
static_assert(offsetof(SkillScopeContext, units) + SourceSlot * sizeof(void*) == 0x28);

constexpr std::uint32_t MaximumConfigBytes = 32'768;
constexpr std::uint64_t FirstEvaluationsLogged = 4;

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

struct Config {
    bool         enabled        = true;
    bool         casterIsSource = true;
    std::int32_t lethalStat     = 523;
};

constexpr char DefaultConfigToml[] = R"toml(# Blood Boil
#
# Server-side changes to skills.txt srvdofunc 160 (WarDoBloodBoil). They only
# touch Blood Boil's per-demon step, 0x521320 in the D2R 3.3 D2RLoader image.
#
# 1. sksrc() reads the caster
#    Blood Boil evaluates calc1, calc2 and calc3 (the percent of the demon's
#    maximum life it sacrifices) and aurarangecalc (the radius) on the DEMON.
#    stat(), skill() and every other function in those four calcs read the
#    demon, and keep doing so. With this on, sksrc() in them reads the Warlock
#    who cast Blood Boil instead:
#
#      20+(sksrc('The Grand Sacrifice Passive'.lvl)>0)*60
#
#    The caster is read at the moment Blood Boil hits, so a passive gained or
#    lost after the demon was summoned counts immediately. The percent is
#    always applied to the demon's own maximum life. calc4 (next hit delay) is
#    already evaluated on the caster and is not touched.
#
# 2. Lethal sacrifice
#    Vanilla skips a demon completely, no sacrifice and no area damage, when
#    the sacrifice would kill it. When the CASTER has a value above 0 in the
#    stat below, that skip is removed: the demon takes the full sacrifice,
#    dies if it runs out of life, and its area damage still goes off. A demon
#    that is already dead keeps the vanilla skip.
#
# Console command: bloodboil (status and counters)
# Log: d2rloader\logs\celestialrayone.blood-boil.log

[blood_boil]

# Master switch. false leaves the game completely vanilla.
enabled = true

# sksrc() in calc1, calc2, calc3 and aurarangecalc reads the caster.
caster_is_sksrc_source = true

# itemstatcost.txt row read on the caster. Above 0 lets the sacrifice kill the
# demon. -1 turns the lethal sacrifice off.
lethal_sacrifice_stat = 523
)toml";

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

enum class HookState : std::uint8_t {
    NotLoaded,
    DisabledByConfig,
    Armed,
    PartiallyArmed,
};

using GetDataTablesFn     = const std::uint8_t*(__fastcall*)(std::uint8_t dataContext) noexcept;
using BbeInterpreterFn    = std::int32_t(__fastcall*)(std::uint8_t dataContext, const std::uint8_t* code,
                                std::int32_t length, std::uintptr_t resolver, std::uintptr_t callbacks,
                                std::int32_t callbackCount, void* context) noexcept;
using EvaluateSkillCalcFn = std::int32_t(__fastcall*)(std::uint8_t dataContext, void* unit,
                                std::uint32_t poolOffset, std::int32_t skillId, std::int32_t skillLevel) noexcept;
using GetUnitStatFn       = std::int32_t(__fastcall*)(void* unit, std::int32_t statId, std::int32_t layer) noexcept;
using UnitIsDeadFn        = std::int32_t(__fastcall*)(void* unit) noexcept;

const D2RL::PluginContext* Context{};
std::uintptr_t             Base{};
GetDataTablesFn            GetDataTables{};
BbeInterpreterFn           BbeInterpreter{};
EvaluateSkillCalcFn        EvaluateSkillCalc{};
GetUnitStatFn              GetUnitStat{};
UnitIsDeadFn               UnitIsDead{};
Config                     Settings{};
HookState                  State{ HookState::NotLoaded };
void*                      HookPage{};
bool                       PercentPatched{};
bool                       RadiusPatched{};
bool                       GatePatched{};

std::atomic<std::uint64_t> CasterEvaluations{};
std::atomic<std::uint64_t> LethalSacrifices{};
std::atomic<std::uint64_t> KeptAlive{};
std::atomic<bool>          ReportedLethal{};
std::atomic<bool>          ReportedStatRange{};

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

void ParseBool(std::string_view value, bool& out) noexcept {
    if (value == "true" || value == "1") {
        out = true;
    } else if (value == "false" || value == "0") {
        out = false;
    }
}

void ParseInt(std::string_view value, std::int32_t& out) noexcept {
    if (value.empty()) return;
    bool negative = false;
    std::size_t index = 0;
    if (value.front() == '-' || value.front() == '+') {
        negative = value.front() == '-';
        index = 1;
    }
    if (index >= value.size()) return;
    std::int64_t result = 0;
    for (; index < value.size(); ++index) {
        const char digit = value[index];
        if (digit < '0' || digit > '9') return;
        result = result * 10 + (digit - '0');
        if (result > 0x7FFFFFFF) return;
    }
    out = static_cast<std::int32_t>(negative ? -result : result);
}

void ApplyConfigLine(std::string_view key, std::string_view value) noexcept {
    if (key == "enabled") {
        ParseBool(value, Settings.enabled);
    } else if (key == "caster_is_sksrc_source") {
        ParseBool(value, Settings.casterIsSource);
    } else if (key == "lethal_sacrifice_stat") {
        ParseInt(value, Settings.lethalStat);
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
        Context->LogWarn("BloodBoil: config file could not be created; using defaults.");
        return;
    }
    std::string buffer(MaximumConfigBytes, '\0');
    if (!Context->ReadConfig(buffer.data(), static_cast<std::uint32_t>(buffer.size()), nullptr)) {
        Context->LogWarn("BloodBoil: config file could not be read; using defaults.");
        return;
    }
    buffer.resize(std::strlen(buffer.c_str()));
    ParseConfig(buffer);
}

// ---------------------------------------------------------------------------
// Site A and B target: 0x3B5160 with the caster in the sksrc() slot
// ---------------------------------------------------------------------------

auto ReadQword(const std::uint8_t* at) noexcept -> std::uint64_t {
    std::uint64_t value = 0;
    std::memcpy(&value, at, sizeof(value));
    return value;
}

auto __fastcall EvaluateWithCasterSource(std::uint8_t dataContext, void* unit, std::uint32_t poolOffset,
        std::int32_t skillId, std::int32_t skillLevel, void* caster) noexcept -> std::int32_t {
    const std::uint8_t* tables = GetDataTables(dataContext);
    if (caster == nullptr || caster == unit || tables == nullptr) {
        return EvaluateSkillCalc(dataContext, unit, poolOffset, skillId, skillLevel);
    }

    const std::uint64_t poolSize = ReadQword(tables + SkillPoolSizeOffset);
    if (poolOffset >= poolSize) return 0;

    const std::uint64_t length = poolSize - poolOffset;
    if (length > 0x7FFF'FFFFULL) {
        // 3B5160 asserts here. Let the native evaluator do exactly that.
        return EvaluateSkillCalc(dataContext, unit, poolOffset, skillId, skillLevel);
    }

    const auto* code = reinterpret_cast<const std::uint8_t*>(ReadQword(tables + SkillPoolBaseOffset))
        + poolOffset;

    SkillScopeContext context{};
    context.vtable = Base + SkillScopeVtableRva;
    for (auto& slot : context.units) slot = unit;
    context.units[SourceSlot] = caster;
    context.skillLevel  = skillLevel;
    context.scopeFlag   = 0;
    context.skillId     = skillId;
    context.skillLevel2 = skillLevel;

    const std::int32_t result = BbeInterpreter(dataContext, code, static_cast<std::int32_t>(length),
        Base + BbeResolverRva, Base + BbeCallbackTableRva, BbeCallbackCount, &context);

    const std::uint64_t count = CasterEvaluations.fetch_add(1, std::memory_order_relaxed);
    if (count < FirstEvaluationsLogged) {
        D2RL::LogInfoF(Context,
            "BloodBoil: skill %d level %d, calc at pool offset %u evaluated on the demon with the "
            "caster as sksrc() source -> %d.",
            skillId, skillLevel, poolOffset, result);
    }
    return result;
}

// ---------------------------------------------------------------------------
// Site C predicate
// ---------------------------------------------------------------------------

auto __fastcall LetSacrificeKill(void* game, void* caster, void* demon) noexcept -> bool {
    const std::int32_t statId = Settings.lethalStat;
    if (game == nullptr || caster == nullptr || demon == nullptr || statId < 0) {
        KeptAlive.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    // A demon that is already dying or dead keeps the vanilla skip.
    if (UnitIsDead(demon) != 0) {
        KeptAlive.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    const std::uint8_t dataContext = static_cast<const std::uint8_t*>(game)[GameDataContextOffset];
    const std::uint8_t* tables = GetDataTables(dataContext);
    if (tables == nullptr) {
        KeptAlive.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    const std::uint64_t rows = ReadQword(tables + StatRowCountOffset);
    if (static_cast<std::uint64_t>(statId) >= rows) {
        if (!ReportedStatRange.exchange(true, std::memory_order_relaxed)) {
            D2RL::LogErrorF(Context,
                "BloodBoil: lethal_sacrifice_stat %d is not an itemstatcost.txt row (the table has "
                "%llu rows). The sacrifice stays non-lethal.",
                statId, static_cast<unsigned long long>(rows));
        }
        KeptAlive.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    const std::int32_t value = GetUnitStat(caster, statId, 0);
    if (value <= 0) {
        KeptAlive.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    LethalSacrifices.fetch_add(1, std::memory_order_relaxed);
    if (!ReportedLethal.exchange(true, std::memory_order_relaxed)) {
        D2RL::LogInfoF(Context,
            "BloodBoil: first lethal sacrifice. Caster stat %d = %d, the demon takes the full "
            "sacrifice and its area damage still fires.",
            statId, value);
    }
    return true;
}

// ---------------------------------------------------------------------------
// Hook page and sites
// ---------------------------------------------------------------------------

auto CanEncodeRel32(std::uintptr_t from, std::uint32_t length, std::uintptr_t to) noexcept -> bool {
    const std::int64_t delta =
        static_cast<std::int64_t>(to) - static_cast<std::int64_t>(from + length);
    return delta >= INT32_MIN && delta <= INT32_MAX;
}

auto AllocateNear(std::uintptr_t hint, std::size_t size) noexcept -> void* {
    SYSTEM_INFO systemInfo{};
    GetSystemInfo(&systemInfo);
    const auto granularity = static_cast<std::uintptr_t>(systemInfo.dwAllocationGranularity);
    const auto aligned = hint & ~(granularity - 1U);
    for (std::uintptr_t delta = granularity; delta < 0x7000'0000ULL; delta += granularity) {
        const auto candidate = aligned + delta;
        if (!CanEncodeRel32(hint, 0, candidate + size)) break;
        if (auto* memory = VirtualAlloc(reinterpret_cast<void*>(candidate), size,
                MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)) {
            return memory;
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

// Rebuilds the instruction at a site with its rel32 aimed at target.
void EncodeSite(std::uint64_t rva, const std::uint8_t* original, std::uint32_t size,
        std::uintptr_t target, std::uint8_t* out) noexcept {
    std::memcpy(out, original, size);
    const auto displacement = static_cast<std::int32_t>(
        static_cast<std::int64_t>(target) - static_cast<std::int64_t>(Base + rva + size));
    std::memcpy(out + size - 4, &displacement, sizeof(displacement));
}

auto BuildHookPage() noexcept -> bool {
    HookPage = AllocateNear(Base + CallbackRva, HookPageBytes);
    if (HookPage == nullptr) {
        Context->LogError("BloodBoil: no hook page was available within rel32 reach.");
        return false;
    }

    auto* page = static_cast<std::uint8_t*>(HookPage);
    std::memset(page, 0xCC, HookPageBytes);
    std::memcpy(page + SourceStubOffset, SourceStub, sizeof(SourceStub));
    WriteQword(page + SourceStubOffset + SourceTargetSlot,
        reinterpret_cast<std::uint64_t>(&EvaluateWithCasterSource));
    std::memcpy(page + GateStubOffset, GateStub, sizeof(GateStub));
    WriteQword(page + GateStubOffset + GatePredicateImm64,
        reinterpret_cast<std::uint64_t>(&LetSacrificeKill));
    WriteQword(page + GateStubOffset + GateProceedSlot, Base + ProceedRva);
    WriteQword(page + GateStubOffset + GateSkipSlot, Base + SkipRva);

    DWORD previous = 0;
    if (!VirtualProtect(page, HookPageBytes, PAGE_EXECUTE_READ, &previous)) {
        Context->LogError("BloodBoil: hook page protection could not be finalized.");
        VirtualFree(HookPage, 0, MEM_RELEASE);
        HookPage = nullptr;
        return false;
    }
    FlushInstructionCache(GetCurrentProcess(), page, HookPageBytes);

    if (!CanEncodeRel32(Base + PercentCallRva, CallSize, PageAddress(SourceStubOffset))
            || !CanEncodeRel32(Base + RadiusCallRva, CallSize, PageAddress(SourceStubOffset))
            || !CanEncodeRel32(Base + GateJumpRva, JleSize, PageAddress(GateStubOffset))) {
        Context->LogError("BloodBoil: hook page displacement validation failed.");
        VirtualFree(HookPage, 0, MEM_RELEASE);
        HookPage = nullptr;
        return false;
    }
    return true;
}

// Leaves both relays running native code only, so nothing on the page can reach
// this DLL any more. Both writes are single aligned stores.
auto RetargetRelaysToVanilla() noexcept -> bool {
    if (HookPage == nullptr) return true;
    auto* page = static_cast<std::uint8_t*>(HookPage);
    DWORD previous = 0;
    if (!VirtualProtect(page, HookPageBytes, PAGE_EXECUTE_READWRITE, &previous)) return false;
    InterlockedExchange64(
        reinterpret_cast<volatile LONG64*>(page + SourceStubOffset + SourceTargetSlot),
        static_cast<LONG64>(Base + EvaluateSkillCalcRva));
    // The gate relay starts on an 8-byte boundary: rewrite its first qword in
    // one store, with only the leading push r9 replaced by jmp short +33h.
    std::uint64_t gateHead = 0;
    std::memcpy(&gateHead, page + GateStubOffset, sizeof(gateHead));
    std::memcpy(&gateHead, GateToVanilla, sizeof(GateToVanilla));
    InterlockedExchange64(reinterpret_cast<volatile LONG64*>(page + GateStubOffset),
        static_cast<LONG64>(gateHead));
    DWORD ignored = 0;
    VirtualProtect(page, HookPageBytes, previous, &ignored);
    FlushInstructionCache(GetCurrentProcess(), page, HookPageBytes);
    return true;
}

auto PatchSite(std::uint64_t rva, const std::uint8_t* original, std::uint32_t size,
        std::uintptr_t target, bool& patched, const char* label) noexcept -> bool {
    std::uint8_t bytes[8]{};
    EncodeSite(rva, original, size, target, bytes);
    if (!Context->PatchBytes(rva, original, size, bytes, size)
            || !Context->CheckExpectedBytes(rva, bytes, size)) {
        D2RL::LogErrorF(Context, "BloodBoil: %s at 0x%llX could not be redirected.",
            label, static_cast<unsigned long long>(rva));
        return false;
    }
    patched = true;
    return true;
}

auto RestoreSite(std::uint64_t rva, const std::uint8_t* original, std::uint32_t size,
        std::uintptr_t target, bool& patched) noexcept -> bool {
    if (!patched) return true;
    std::uint8_t current[8]{};
    EncodeSite(rva, original, size, target, current);
    if (!Context->PatchBytes(rva, current, size, original, size)) return false;
    patched = false;
    return true;
}

auto RestoreSites() noexcept -> bool {
    bool restored = true;
    restored &= RestoreSite(RadiusCallRva, RadiusCallOriginal, CallSize,
        PageAddress(SourceStubOffset), RadiusPatched);
    restored &= RestoreSite(PercentCallRva, PercentCallOriginal, CallSize,
        PageAddress(SourceStubOffset), PercentPatched);
    restored &= RestoreSite(GateJumpRva, GateJumpOriginal, JleSize,
        PageAddress(GateStubOffset), GatePatched);
    return restored;
}

// ---------------------------------------------------------------------------
// Verification and installation
// ---------------------------------------------------------------------------

auto Verify(std::uint64_t rva, const void* expected, std::size_t size, const char* label) noexcept -> bool {
    if (Context->CheckExpectedBytes(rva, expected, static_cast<std::uint32_t>(size))) return true;
    D2RL::LogErrorF(Context,
        "BloodBoil: %s at 0x%llX does not match the verified D2R image, or another plugin "
        "already owns it. Refusing to load.",
        label, static_cast<unsigned long long>(rva));
    return false;
}

auto VerifyNativeContract(bool source, bool lethal) noexcept -> bool {
    const std::uint64_t handler = Base + BloodBoilHandlerRva;
    if (!Verify(SrvDoSlotRva, &handler, sizeof(handler), "srvdofunc 160 (WarDoBloodBoil)")
            || !Verify(CallbackRva, CallbackHeadWitness, sizeof(CallbackHeadWitness),
                "the Blood Boil per-demon callback")) {
        return false;
    }
    if (source
            && (!Verify(RadiusWindowRva, RadiusWindowWitness, sizeof(RadiusWindowWitness),
                    "the Blood Boil radius evaluation")
                || !Verify(EvaluateSkillCalcRva, EvaluateSkillCalcWitness,
                    sizeof(EvaluateSkillCalcWitness), "the skills.txt formula evaluator"))) {
        return false;
    }
    if (lethal
            && (!Verify(GateWindowRva, GateWindowWitness, sizeof(GateWindowWitness),
                    "the Blood Boil sacrifice gate")
                || !Verify(StatRowCountReadRva, StatRowCountWitness, sizeof(StatRowCountWitness),
                    "the itemstatcost row count read")
                || !Verify(GetUnitStatRva, GetUnitStatThunkWitness, sizeof(GetUnitStatThunkWitness),
                    "the unit stat getter thunk")
                || !Verify(UnitIsDeadRva, UnitIsDeadWitness, sizeof(UnitIsDeadWitness),
                    "SUNIT_IsDead")
                || !Verify(TypedDemonLoopRva, TypedDemonLoopWitness, sizeof(TypedDemonLoopWitness),
                    "the pettype demon walk")
                || !Verify(PooledDemonLoopRva, PooledDemonLoopWitness, sizeof(PooledDemonLoopWitness),
                    "the all-demons walk"))) {
        return false;
    }
    return true;
}

// Returns false only when nothing in the image can reach this DLL any more, so
// the loader may safely unload it.
auto InstallHooks(bool source, bool lethal) noexcept -> bool {
    if (!BuildHookPage()) return false;

    bool failed = false;
    if (lethal) {
        failed = !PatchSite(GateJumpRva, GateJumpOriginal, JleSize, PageAddress(GateStubOffset),
            GatePatched, "the sacrifice gate");
    }
    if (!failed && source) {
        failed = !PatchSite(PercentCallRva, PercentCallOriginal, CallSize,
            PageAddress(SourceStubOffset), PercentPatched, "the calc1-3 evaluation")
            || !PatchSite(RadiusCallRva, RadiusCallOriginal, CallSize,
                PageAddress(SourceStubOffset), RadiusPatched, "the aurarangecalc evaluation");
    }

    if (!failed) {
        State = HookState::Armed;
        return true;
    }

    // The page is kept on every failure path: a thread may already be inside a
    // relay, and after the retarget nothing on it points into this DLL.
    const bool retargeted = RetargetRelaysToVanilla();
    const bool restored = RestoreSites();
    if (restored && retargeted) return false;
    if (retargeted) {
        Context->LogError(
            "BloodBoil: a site could not be restored; it now runs native code through the hook "
            "page, with no behaviour change.");
        return false;
    }
    Context->LogError(
        "BloodBoil: rollback failed, staying loaded so every patched site keeps a valid target.");
    State = HookState::PartiallyArmed;
    return true;
}

// ---------------------------------------------------------------------------
// Console command
// ---------------------------------------------------------------------------

auto StateName() noexcept -> const char* {
    switch (State) {
    case HookState::DisabledByConfig: return "disabled by config";
    case HookState::Armed:            return "armed";
    case HookState::PartiallyArmed:   return "PARTIALLY armed, see log";
    default:                          return "not loaded";
    }
}

auto __cdecl StatusCommand(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command,
        void*) noexcept -> D2RL::ConsoleCommandResult {
    if (command == nullptr || command->plugin == nullptr) return D2RL::ConsoleCommandResult::Failed;

    char message[480];
    std::snprintf(message, sizeof(message),
        "Blood Boil: %s | sksrc reads caster %s | lethal sacrifice %s, stat %d | caster-source "
        "evaluations %llu | lethal sacrifices %llu | demons kept alive %llu",
        StateName(),
        (PercentPatched && RadiusPatched) ? "on" : "off",
        GatePatched ? "on" : "off",
        Settings.lethalStat,
        static_cast<unsigned long long>(CasterEvaluations.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(LethalSacrifices.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(KeptAlive.load(std::memory_order_relaxed)));
    command->plugin->WriteConsoleMessage(message);
    return D2RL::ConsoleCommandResult::Handled;
}

void RegisterStatusCommand() noexcept {
    if (!Context->RegisterConsoleCommand("bloodboil", &StatusCommand,
            "Reports Blood Boil status and counters.")) {
        Context->LogWarn("BloodBoil: the status console command was refused.");
    }
}

constexpr D2RL::PluginInfo Info{
    .infoSize    = D2RL::PluginInfoSize,
#if defined(D2RL_PLUGIN_ABI_VERSION)
    .abiVersion  = D2RL_PLUGIN_ABI_VERSION,
#else
    .apiVersion  = D2RL_PLUGIN_API_VERSION,
#endif
    .id          = "celestialrayone.blood-boil",
    .name        = "Blood Boil",
    .version     = "1.0.0",
    .author      = "CelestialRayOne",
    .description = "Blood Boil (srvdofunc 160): sksrc() in the calcs evaluated on a demon reads the "
                   "caster, and a caster stat lets the sacrifice kill the demon.",
    .flags       = D2RL::PluginFlags::Server | D2RL::PluginFlags::NativeHooks,
};

}  // namespace

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* {
    return &Info;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
    if (!D2RL::HasContext(context)) return false;
    Context = context;
    Base = context->exeBase;
    GetDataTables     = reinterpret_cast<GetDataTablesFn>(Base + GetDataTablesRva);
    BbeInterpreter    = reinterpret_cast<BbeInterpreterFn>(Base + BbeInterpreterRva);
    EvaluateSkillCalc = reinterpret_cast<EvaluateSkillCalcFn>(Base + EvaluateSkillCalcRva);
    GetUnitStat       = reinterpret_cast<GetUnitStatFn>(Base + GetUnitStatRva);
    UnitIsDead        = reinterpret_cast<UnitIsDeadFn>(Base + UnitIsDeadRva);

    ReadConfiguration();

    const bool source = Settings.enabled && Settings.casterIsSource;
    const bool lethal = Settings.enabled && Settings.lethalStat >= 0;
    if (!source && !lethal) {
        State = HookState::DisabledByConfig;
        Context->LogInfo("BloodBoil: disabled by configuration.");
        RegisterStatusCommand();
        return true;
    }

    if (!VerifyNativeContract(source, lethal)) return false;
    if (!InstallHooks(source, lethal)) return false;

    D2RL::LogInfoF(Context,
        "BloodBoil: %s. sksrc reads caster %s (5213CC, 521623), lethal sacrifice %s (52149B, "
        "stat %d).",
        StateName(), (PercentPatched && RadiusPatched) ? "on" : "off",
        GatePatched ? "on" : "off", Settings.lethalStat);

    RegisterStatusCommand();
    return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
    if (Context == nullptr) return;
    RetargetRelaysToVanilla();
    RestoreSites();
    // The hook page is deliberately kept: a thread may be inside a relay right
    // now, and a site that could not be restored still needs it.
}

}  // namespace CelestialRayOne::BloodBoil
