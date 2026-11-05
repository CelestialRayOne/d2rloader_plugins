// Bound Baal AI
//
// Baal's AI was written for a Baal who always fights the players. While he
// fights on their side (bound with Bind Demon, or converted), three of his
// behaviours still treat the player as the enemy. This plugin fixes all three
// with rules the game already applies elsewhere. A normal, hostile Baal runs
// every original instruction untouched: each change below only runs while
// Baal's alignment is not evil.
//
// Written for D2R 3.3 on D2RLoader 1.3.1. Every address and byte below was read
// out of the live D2RLoader.exe process image (md5 8af7a47ce9077de0378c69af2930854d)
// and disassembled before being relied on.
//
// ---------------------------------------------------------------------------
// 1. Targeting
// ---------------------------------------------------------------------------
//   Most monster AIs get their target from the shared target picker 0x595750
//   (the AI tick's category 2 target, called at 0x4A2C99). It checks alignment
//   first: an evil unit searches the players' side, any other unit scans the
//   nearby rooms for hostile units (enumerator 0x596340 mode 4, test callback
//   0x597E00 -> valid-target test 0x597840, range 35 at 0x595D61).
//
//   Baal skips it. The AI rows BaalCrab (135, think 0x5A8B80) and BaalCrabClone
//   (140, think 0x5A8CF0) use target mode 0 and call the Baal-only routine
//   0x5A9A10 (game, baal, int* bestScore, int* count, cull) instead. Its only
//   callers are the Baal thinks (0x5A8BB8, 0x5A8D81, 0x5A9031). It walks the
//   ten target lists at game+0x21E0: the eight player slots (each player
//   followed by his pets, hirelings and bound or converted monsters), then the
//   allied and the neutral monsters, and keeps the best score of the threat
//   routine called at 0x5A9B6A, 0x5A9BA6, 0x5A9C06 and 0x5A9C5A (D2RLoader
//   routes those to D2RCore's ScoreBaalTargetWithPrimeEvilThreat). Nothing in
//   it reads Baal's alignment, so a bound Baal attacks the player or one of his
//   allies and never considers a hostile monster.
//
//   The hook on 0x5A9A10: an evil Baal runs the original. Any other Baal gets
//   his target from 0x595750, with the arguments the AI tick passes it: the AI
//   control at monster data +30h (0x4A2AD0) and the difficulty byte from
//   0x48FF40 (0x4A2AEB, read back as the byte at 0x4A2C7A). bestScore becomes
//   Baal's own threat score for that target, from the routine the original
//   calls, and count the number of units within the picker's range that pass
//   the picker's own valid-target test 0x597840 and distance 0x596720 (as at
//   0x597E23/0x597E36), at least 1 when there is a target. Baal's skill choice
//   0x5A95E0 and skill handler 0x5AA860 then run unchanged on that target.
//
// ---------------------------------------------------------------------------
// 2. Clone
// ---------------------------------------------------------------------------
//   0x5AAE60, reached only from the skill handler's case 15 (call at 0x5AAD9B),
//   refuses while Baal has living minions or a living owner. A bound Baal whose
//   owner is dead or gone clones: the clone spawns as a hostile Baal, and the
//   routine rewrites the bound Baal's own owner to himself (0x4A5800), which
//   breaks the bind. The hook returns 0, the routine's own "no clone" answer,
//   which the caller ignores before idling, for every Baal that is not evil:
//   the rule spawning AIs such as the Vile Mother's follow, spawning only
//   while evil.
//
// ---------------------------------------------------------------------------
// 3. Festering Appendages
// ---------------------------------------------------------------------------
//   srvdofunc 140 (0x5859E0) spawns each tentacle with 0x492CF0, then only
//   stores the caster as its owner (0x34EF50, called at 0x585CA7). A tentacle
//   never gets an alignment, so it is hostile to the players. That call is
//   relayed: after the owner is stored, a tentacle whose owner is a monster
//   that is not evil takes the owner's alignment through 0x48E600, called the
//   way the Bind Demon conversion calls it at 0x51F1E1. That is the rule the
//   Hydra skill applies to every hydra a monster casts. An evil caster's
//   tentacles are not touched.
//
// ---------------------------------------------------------------------------
// Unit fields read here
// ---------------------------------------------------------------------------
//   +0x00 unit type, 1 = monster (0x34B9D0 returns [unit]).
//   +0x10 monster data; its +0x30 is the AI control (0x4A2AB0..0x4A2AD8).
//   Alignment through the game's own getter 0x2F4190 (0 evil, 1 neutral,
//   2 good), as the AI think event calls it at 0x4A2A38.

#include <D2RLPlugin/api.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

namespace CelestialRayOne::BoundBaalAi {
namespace {

// ---------------------------------------------------------------------------
// Addresses (RVAs into D2RLoader.exe)
// ---------------------------------------------------------------------------

constexpr std::uint64_t BaalTargetRva        = 0x5A9A10;
constexpr std::uint64_t BaalCloneRva         = 0x5AAE60;
constexpr std::uint64_t TentacleOwnerCallRva = 0x585CA7;
constexpr std::uint64_t StoreOwnerRva        = 0x34EF50;
constexpr std::uint64_t GetAlignmentRva      = 0x2F4190;
constexpr std::uint64_t SetAlignmentRva      = 0x48E600;
constexpr std::uint64_t DifficultyRva        = 0x48FF40;
constexpr std::uint64_t SelectTargetRva      = 0x595750;
constexpr std::uint64_t EnumerateRva         = 0x596340;
constexpr std::uint64_t ValidTargetRva       = 0x597840;
constexpr std::uint64_t DistanceRva          = 0x596720;
constexpr std::uint64_t AllyRangeImmRva      = 0x595D63;  // imm32 of mov r14d,35 at 0x595D61

// The four calls of Baal's threat routine inside 0x5A9A10.
constexpr std::array<std::uint64_t, 4> ScoreCallRvas{ 0x5A9B6A, 0x5A9BA6, 0x5A9C06, 0x5A9C5A };

// ---------------------------------------------------------------------------
// Verified byte windows. Hooked windows hold whole instructions with no
// branch and no RIP-relative operand. The others are only read, to prove an
// address and its arguments at a native call site before the plugin calls it.
// ---------------------------------------------------------------------------

// 0x5A9A10 (hooked) mov rax,rsp / mov [rax+20h],r9 / mov [rax+18h],r8 /
// push rbx / push rbp / push rsi / push rdi / push r12 / push r13 / push r14 /
// push r15 / sub rsp,48h
constexpr std::uint8_t BaalTargetEntry[]{
    0x48, 0x8B, 0xC4, 0x4C, 0x89, 0x48, 0x20, 0x4C, 0x89, 0x40, 0x18, 0x53, 0x55, 0x56,
    0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x48 };

// 0x5AAE60 (hooked) mov [rsp+8],rbx / mov [rsp+10h],rsi / mov [rsp+18h],rdi /
// push rbp / push r12 / push r13 / push r14 / push r15
constexpr std::uint8_t BaalCloneEntry[]{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x48, 0x89, 0x7C, 0x24,
    0x18, 0x55, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57 };

// 0x585C96 (tentacle loop of srvdofunc 140) mov rdx,rdi / mov rcx,r15 /
// call 0x4A6E50 (idle 15 frames) / mov rdx,rbx / mov rcx,rdi /
// call 0x34EF50 (store the caster as owner, relayed) / sub rbp,1 / jnz loop
constexpr std::uint64_t TentacleWindowRva = 0x585C96;
constexpr std::uint8_t  TentacleWindow[]{
    0x48, 0x8B, 0xD7, 0x49, 0x8B, 0xCF, 0xE8, 0xAF, 0x11, 0xF2, 0xFF, 0x48, 0x8B, 0xD3,
    0x48, 0x8B, 0xCF, 0xE8, 0xA4, 0x92, 0xDC, 0xFF, 0x48, 0x83, 0xED, 0x01, 0x0F, 0x85,
    0xBA, 0xFE, 0xFF, 0xFF };
constexpr std::size_t   TentacleCallOffset = 17;

// 0x4A2A35 (AI think event) mov rcx,rdx / call 0x2F4190 (alignment) / cmp eax,2
constexpr std::uint64_t AlignmentWindowRva = 0x4A2A35;
constexpr std::uint8_t  AlignmentWindow[]{
    0x48, 0x8B, 0xCA, 0xE8, 0x53, 0x17, 0xE5, 0xFF, 0x83, 0xF8, 0x02 };

// 0x4A2AB0 (AI think event) mov rax,[r14+10h] / test rax,rax / je /
// cmp [rax+30h],rbx / jne / (assert) / mov rax,[r14+10h] / mov rax,[rax+30h]
constexpr std::uint64_t AiControlWindowRva = 0x4A2AB0;
constexpr std::uint8_t  AiControlWindow[]{
    0x49, 0x8B, 0x46, 0x10, 0x48, 0x85, 0xC0, 0x74, 0x21, 0x48, 0x39, 0x58, 0x30, 0x75,
    0x11, 0x48, 0x8D, 0x4D, 0x9F, 0x88, 0x5D, 0x9F, 0xE8, 0xE5, 0xDE, 0xF5, 0xFF, 0x84,
    0xC0, 0x74, 0x01, 0xCC, 0x49, 0x8B, 0x46, 0x10, 0x48, 0x8B, 0x40, 0x30 };

// 0x4A2ADD (AI think event) mov r8,r14 (unit) / mov [rbp-51h],rax (AI control) /
// mov rdx,r15 (game) / lea rcx,[rbp+0Fh] / call 0x48FF40 / ... /
// movsd xmm0,[rax] / movsd [rbp-19h],xmm0 (the context the tick keeps)
constexpr std::uint64_t DifficultyWindowRva = 0x4A2ADD;
constexpr std::uint8_t  DifficultyWindow[]{
    0x4D, 0x8B, 0xC6, 0x48, 0x89, 0x45, 0xAF, 0x49, 0x8B, 0xD7, 0x48, 0x8D, 0x4D, 0x0F,
    0xE8, 0x50, 0xD4, 0xFE, 0xFF, 0x41, 0xB8, 0xE4, 0x03, 0x00, 0x00, 0x48, 0x8D, 0x15,
    0x03, 0xBE, 0x87, 0x01, 0x49, 0x8B, 0xCE, 0xF2, 0x0F, 0x10, 0x00, 0xF2, 0x0F, 0x11,
    0x45, 0xE7 };

// 0x4A2C7A (AI think event, category 2) movzx eax,byte [rbp-19h] (context
// byte 0) / lea r9,[rbp-31h] (distance) / mov r8,[rbp-51h] (AI control) /
// mov rdx,r14 / mov [rsp+28h],al / mov rcx,r15 / lea rax,[rbp-2Dh] (combat) /
// mov [rsp+20h],rax / call 0x595750
constexpr std::uint64_t SelectTargetWindowRva = 0x4A2C7A;
constexpr std::uint8_t  SelectTargetWindow[]{
    0x0F, 0xB6, 0x45, 0xE7, 0x4C, 0x8D, 0x4D, 0xCF, 0x4C, 0x8B, 0x45, 0xAF, 0x49, 0x8B,
    0xD6, 0x88, 0x44, 0x24, 0x28, 0x49, 0x8B, 0xCF, 0x48, 0x8D, 0x45, 0xD3, 0x48, 0x89,
    0x44, 0x24, 0x20, 0xE8, 0xB2, 0x2A, 0x0F, 0x00 };

// 0x5A9058 (Uber Baal think) lea r9,[callback] / ... / lea r8,[context] / ... /
// mov rcx,rsi (game) / mov dword [rsp+20h],1 (mode 1: caller's callback) /
// mov r14,rax / call 0x596340
constexpr std::uint64_t EnumerateWindowRva = 0x5A9058;
constexpr std::uint8_t  EnumerateWindow[]{
    0x4C, 0x8D, 0x0D, 0xB1, 0xD1, 0xFE, 0xFF, 0x4C, 0x89, 0x7D, 0x8C, 0x4C, 0x8D, 0x45,
    0x80, 0xC7, 0x45, 0x94, 0xFF, 0xFF, 0xFF, 0x7F, 0x48, 0x8B, 0xCE, 0xC7, 0x44, 0x24,
    0x20, 0x01, 0x00, 0x00, 0x00, 0x4C, 0x8B, 0xF0, 0xE8, 0xBF, 0xD2, 0xFE, 0xFF };

// 0x597E1A (allied hostile-target callback 0x597E00) mov rsi,r9 / mov rdi,r8 /
// mov r14,rdx / call 0x597840 (game, searcher, candidate) / mov rcx,rdi /
// test eax,eax / je / mov rdx,r14 / call 0x596720 (candidate, searcher) /
// mov ebp,eax / cmp eax,[rsi+20h] (range)
constexpr std::uint64_t HostileTestWindowRva = 0x597E1A;
constexpr std::uint8_t  HostileTestWindow[]{
    0x49, 0x8B, 0xF1, 0x49, 0x8B, 0xF8, 0x4C, 0x8B, 0xF2, 0xE8, 0x18, 0xFA, 0xFF, 0xFF,
    0x48, 0x8B, 0xCF, 0x85, 0xC0, 0x0F, 0x84, 0xC6, 0x00, 0x00, 0x00, 0x49, 0x8B, 0xD6,
    0xE8, 0xE5, 0xE8, 0xFF, 0xFF, 0x8B, 0xE8, 0x3B, 0x46, 0x20 };

// 0x595D56 (non-evil branch of 0x595750) lea r8,[rbp-50h] / xorps xmm0,xmm0 /
// mov [rbp-38h],r12d / mov r14d, <range> ...
constexpr std::uint64_t AllyRangeHeadRva = 0x595D56;
constexpr std::uint8_t  AllyRangeHead[]{
    0x4C, 0x8D, 0x45, 0xB0, 0x0F, 0x57, 0xC0, 0x44, 0x89, 0x65, 0xC8, 0x41, 0xBE };
// 0x595D67 ... movdqa [rbp-50h],xmm0 / mov rcx,rsi / mov [rbp-34h],r12d /
// xor r9d,r9d / mov [rbp-30h],r14d (context +20h = range) / mov rdx,r15 /
// mov [rbp-28h],eax / mov [rbp-20h],r13b / mov dword [rsp+20h],4 / call 0x596340
constexpr std::uint64_t AllyRangeTailRva = 0x595D67;
constexpr std::uint8_t  AllyRangeTail[]{
    0x66, 0x0F, 0x7F, 0x45, 0xB0, 0x48, 0x8B, 0xCE, 0x44, 0x89, 0x65, 0xCC, 0x45, 0x33,
    0xC9, 0x44, 0x89, 0x75, 0xD0, 0x49, 0x8B, 0xD7, 0x89, 0x45, 0xD8, 0x44, 0x88, 0x6D,
    0xE0, 0xC7, 0x44, 0x24, 0x20, 0x04, 0x00, 0x00, 0x00, 0xE8, 0xAF, 0x05, 0x00, 0x00 };

// 0x51F1D6 (Bind Demon conversion) mov r8d,1 / mov dl,2 / mov rcx,rbx /
// call 0x48E600 (alignment: unit, alignment, update region)
constexpr std::uint64_t SetAlignmentWindowRva = 0x51F1D6;
constexpr std::uint8_t  SetAlignmentWindow[]{
    0x41, 0xB8, 0x01, 0x00, 0x00, 0x00, 0xB2, 0x02, 0x48, 0x8B, 0xCB, 0xE8, 0x1A, 0xF4,
    0xF6, 0xFF };

// Around each threat call: the two argument moves (rdx = target, rcx = Baal),
// the call opcode, and the instruction after it. The rel32 is D2RLoader's and
// is decoded live instead.
struct ScoreSite {
    std::uint64_t headRva;
    std::uint8_t  head[7];
    std::uint64_t tailRva;
    std::uint8_t  tail[2];
};
constexpr std::array<ScoreSite, 4> ScoreSites{ {
    { 0x5A9B64, { 0x48, 0x8B, 0xD7, 0x49, 0x8B, 0xCE, 0xE8 }, 0x5A9B6F, { 0x8B, 0xC8 } },
    { 0x5A9BA0, { 0x49, 0x8B, 0xCE, 0x48, 0x8B, 0xD7, 0xE8 }, 0x5A9BAB, { 0x8B, 0xC8 } },
    { 0x5A9C00, { 0x48, 0x8B, 0xD7, 0x49, 0x8B, 0xCE, 0xE8 }, 0x5A9C0B, { 0x3B, 0xE8 } },
    { 0x5A9C54, { 0x48, 0x8B, 0xD7, 0x49, 0x8B, 0xCE, 0xE8 }, 0x5A9C5F, { 0x3B, 0xF0 } },
} };

// Decodes the target of an E8 rel32 call at `offset` inside a window.
constexpr auto CallTarget(const std::uint8_t* window, std::uint64_t windowRva, std::size_t offset) noexcept
        -> std::uint64_t {
    const std::uint32_t raw = static_cast<std::uint32_t>(window[offset + 1])
        | (static_cast<std::uint32_t>(window[offset + 2]) << 8)
        | (static_cast<std::uint32_t>(window[offset + 3]) << 16)
        | (static_cast<std::uint32_t>(window[offset + 4]) << 24);
    const std::int64_t rel = static_cast<std::int32_t>(raw);
    return static_cast<std::uint64_t>(static_cast<std::int64_t>(windowRva + offset + 5) + rel);
}

static_assert(TentacleWindow[TentacleCallOffset] == 0xE8);
static_assert(TentacleWindowRva + TentacleCallOffset == TentacleOwnerCallRva);
static_assert(CallTarget(TentacleWindow, TentacleWindowRva, TentacleCallOffset) == StoreOwnerRva);
static_assert(CallTarget(AlignmentWindow, AlignmentWindowRva, 3) == GetAlignmentRva);
static_assert(CallTarget(DifficultyWindow, DifficultyWindowRva, 14) == DifficultyRva);
static_assert(CallTarget(SelectTargetWindow, SelectTargetWindowRva, 31) == SelectTargetRva);
static_assert(CallTarget(EnumerateWindow, EnumerateWindowRva, 36) == EnumerateRva);
static_assert(CallTarget(HostileTestWindow, HostileTestWindowRva, 9) == ValidTargetRva);
static_assert(CallTarget(HostileTestWindow, HostileTestWindowRva, 28) == DistanceRva);
static_assert(CallTarget(AllyRangeTail, AllyRangeTailRva, 37) == EnumerateRva);
static_assert(AllyRangeHeadRva + sizeof(AllyRangeHead) == AllyRangeImmRva);
static_assert(AllyRangeImmRva + 4 == AllyRangeTailRva);
static_assert(CallTarget(SetAlignmentWindow, SetAlignmentWindowRva, 11) == SetAlignmentRva);

// ---------------------------------------------------------------------------
// Unit layout and constants
// ---------------------------------------------------------------------------

constexpr std::size_t   UnitTypeOffset    = 0x00;
constexpr std::size_t   MonsterDataOffset = 0x10;
constexpr std::size_t   AiControlOffset   = 0x30;
constexpr std::uint32_t UnitTypeMonster   = 1;
constexpr std::int32_t  AlignmentEvil     = 0;
constexpr std::int32_t  AlignmentCount    = 3;
// Enumerator mode 1: table 0x1D3F910 entry 1 = adjacent rooms, every unit, the
// caller's own callback.
constexpr std::int32_t  EnumerateWithCallback = 1;
// The picker's range when the live instruction cannot be read.
constexpr std::int32_t  DefaultAllyRange = 35;

// ---------------------------------------------------------------------------
// Native signatures (Windows x64 ABI)
// ---------------------------------------------------------------------------

using BaalTargetFn     = void* (*)(void* game, void* baal, std::int32_t* bestScore, std::int32_t* count,
                                   void* cull) noexcept;
using BaalCloneFn      = std::uint8_t (*)(void* game, void* baal, void* aiControl) noexcept;
using StoreOwnerFn     = void (*)(void* unit, void* owner) noexcept;
using GetAlignmentFn   = std::int32_t (*)(void* unit) noexcept;
using SetAlignmentFn   = void (*)(void* unit, std::uint8_t alignment, std::int32_t updateRegion) noexcept;
using DifficultyFn     = std::uint8_t* (*)(void* context, void* game, void* unit) noexcept;
using SelectTargetFn   = void* (*)(void* game, void* unit, void* aiControl, std::int32_t* distance,
                                   std::int32_t* combat, std::uint8_t difficulty) noexcept;
using SearchCallbackFn = void* (*)(void* game, void* searcher, void* candidate, void* context) noexcept;
using EnumerateFn      = void* (*)(void* game, void* searcher, void* context, SearchCallbackFn callback,
                                   std::int32_t mode) noexcept;
using ValidTargetFn    = std::int32_t (*)(void* game, void* searcher, void* candidate) noexcept;
using DistanceFn       = std::int32_t (*)(void* unit, void* other) noexcept;
using ScoreFn          = std::int32_t (*)(void* baal, void* target) noexcept;

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

constexpr char        ConfigSection[]    = "bound_baal_ai";
constexpr std::size_t InitialConfigBytes = 16'384;
constexpr std::size_t MaximumConfigBytes = 1'048'576;

constexpr char DefaultConfigToml[] =
    "# Bound Baal AI\n"
    "#\n"
    "# Baal's AI was written for a Baal who always fights the players. While he\n"
    "# fights on their side (bound with Bind Demon, or converted), three of his\n"
    "# behaviours still treat you as the enemy. This plugin fixes all three. A\n"
    "# normal, hostile Baal is never touched: every change below only applies\n"
    "# while Baal is on the players' side.\n"
    "#\n"
    "#   targeting  Baal picks targets with his own routine, which only ever looks\n"
    "#              at players, their pets, hirelings and other allies. A bound Baal\n"
    "#              therefore attacks you or your allies and never a hostile\n"
    "#              monster. With this on he picks his target the same way every\n"
    "#              other bound demon does: the nearest enemy, never in town. He\n"
    "#              then fights it with his full normal skill logic, and his threat\n"
    "#              score and the number of enemies around him still decide which\n"
    "#              skill he uses.\n"
    "#\n"
    "#   clone      Baal's clone is meant for a Baal nobody owns. A bound Baal still\n"
    "#              clones once his owner is dead or gone: the clone spawns as a\n"
    "#              hostile Baal, and the bound Baal stops belonging to you. With\n"
    "#              this on a bound Baal never clones, the same way the game's own\n"
    "#              spawning monsters, like the Vile Mother, only spawn while evil.\n"
    "#\n"
    "#   tentacles  Festering Appendages spawns its tentacles as ordinary hostile\n"
    "#              monsters, so a bound Baal's tentacles attack you. With this on\n"
    "#              they take the side of the Baal that cast them, the rule the game\n"
    "#              already applies to Hydras cast by monsters, and fight for you.\n"
    "#\n"
    "# Server side only. In single player the game hosts its own server, so it\n"
    "# applies there too.\n"
    "#\n"
    "# Console command: boundbaal (status and counters)\n"
    "\n"
    "[bound_baal_ai]\n"
    "\n"
    "# Master switch. false installs nothing at all.\n"
    "enabled = true\n"
    "\n"
    "# Each fix can be turned off on its own (true or false).\n"
    "targeting = true\n"
    "clone = true\n"
    "tentacles = true\n";

struct Settings {
    bool enabled   = true;
    bool targeting = true;
    bool clone     = true;
    bool tentacles = true;
};

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

enum class FixState : std::uint8_t {
    NotAttempted,
    Off,
    Refused,
    Active,
};

enum class FixId : std::size_t { Targeting, Clone, Tentacles, Count };
constexpr std::size_t FixCount = static_cast<std::size_t>(FixId::Count);

const D2RL::PluginContext*            Context{};
std::uintptr_t                        Base{};
Settings                              Config{};
bool                                  ConfigFileWasRead{};
std::array<FixState, FixCount>        Fixes{};

std::atomic<std::uint64_t> AlliedPicks{};
std::atomic<std::uint64_t> AlliedPicksWithoutEnemy{};
std::atomic<std::uint64_t> ClonesRefused{};
std::atomic<std::uint64_t> TentaclesJoined{};

BaalTargetFn   OriginalBaalTarget{};
BaalCloneFn    OriginalBaalClone{};
StoreOwnerFn   StoreOwner{};
GetAlignmentFn GetAlignment{};
SetAlignmentFn SetAlignment{};
DifficultyFn   DifficultyContext{};
SelectTargetFn SelectTarget{};
EnumerateFn    Enumerate{};
ValidTargetFn  ValidTarget{};
DistanceFn     Distance{};
ScoreFn        Score{};
std::int32_t   AllyRange = DefaultAllyRange;

constexpr std::size_t        RelayPageBytes = 0x1000;
std::uint8_t*                RelayPage{};
std::array<std::uint8_t, 5>  TentacleStock{};
std::array<std::uint8_t, 5>  TentacleWritten{};
bool                         TentaclePatched{};

auto ReadU32(const void* base, std::size_t offset) noexcept -> std::uint32_t {
    std::uint32_t value = 0;
    std::memcpy(&value, static_cast<const std::uint8_t*>(base) + offset, sizeof(value));
    return value;
}

auto ReadPointer(const void* base, std::size_t offset) noexcept -> void* {
    void* value = nullptr;
    std::memcpy(&value, static_cast<const std::uint8_t*>(base) + offset, sizeof(value));
    return value;
}

auto IsMonster(void* unit) noexcept -> bool {
    return unit != nullptr && ReadU32(unit, UnitTypeOffset) == UnitTypeMonster;
}

// A monster that is not evil: bound, converted or otherwise on the players'
// side. The same split 0x595750 makes before it picks a target.
auto IsSidingWithPlayers(void* unit) noexcept -> bool {
    const GetAlignmentFn alignment = GetAlignment;
    if (alignment == nullptr || !IsMonster(unit)) return false;
    return alignment(unit) != AlignmentEvil;
}

auto AiControlOf(void* unit) noexcept -> void* {
    if (!IsMonster(unit)) return nullptr;
    void* monsterData = ReadPointer(unit, MonsterDataOffset);
    return monsterData != nullptr ? ReadPointer(monsterData, AiControlOffset) : nullptr;
}

// ---------------------------------------------------------------------------
// 1. Targeting
// ---------------------------------------------------------------------------

struct EnemyTally {
    std::int32_t count;
    std::int32_t range;
};

// Counts what the allied picker would accept: a valid target within its range.
// Returning null keeps the enumeration going.
auto CountEnemy(void* game, void* searcher, void* candidate, void* context) noexcept -> void* {
    auto* tally = static_cast<EnemyTally*>(context);
    if (tally == nullptr || candidate == nullptr) return nullptr;
    if (ValidTarget(game, searcher, candidate) == 0) return nullptr;
    if (Distance(candidate, searcher) > tally->range) return nullptr;
    ++tally->count;
    return nullptr;
}

auto CountEnemies(void* game, void* baal) noexcept -> std::int32_t {
    EnemyTally tally{ 0, AllyRange };
    Enumerate(game, baal, &tally, &CountEnemy, EnumerateWithCallback);
    return tally.count;
}

// Target, threat score and enemy count for a Baal on the players' side.
auto PickAlliedTarget(void* game, void* baal, std::int32_t& score, std::int32_t& enemies) noexcept -> void* {
    score   = 0;
    enemies = 0;
    void* aiControl = AiControlOf(baal);
    if (aiControl == nullptr) return nullptr;

    alignas(8) std::uint8_t context[16]{};
    const std::uint8_t* filled = DifficultyContext(context, game, baal);
    const std::uint8_t difficulty = filled != nullptr ? filled[0] : context[0];

    std::int32_t distance = 0;
    std::int32_t combat   = 0;
    void* target = SelectTarget(game, baal, aiControl, &distance, &combat, difficulty);
    if (target == nullptr) return nullptr;

    score   = Score(baal, target);
    enemies = CountEnemies(game, baal);
    if (enemies < 1) enemies = 1;
    return target;
}

auto HookBaalTarget(void* game, void* baal, std::int32_t* bestScore, std::int32_t* count, void* cull) noexcept
        -> void* {
    if (game == nullptr || !IsSidingWithPlayers(baal)) {
        const BaalTargetFn original = OriginalBaalTarget;
        return original != nullptr ? original(game, baal, bestScore, count, cull) : nullptr;
    }

    std::int32_t score   = 0;
    std::int32_t enemies = 0;
    void* target = PickAlliedTarget(game, baal, score, enemies);
    AlliedPicks.fetch_add(1, std::memory_order_relaxed);
    if (target == nullptr) AlliedPicksWithoutEnemy.fetch_add(1, std::memory_order_relaxed);
    if (bestScore != nullptr) *bestScore = score;
    if (count != nullptr) *count = enemies;
    return target;
}

// ---------------------------------------------------------------------------
// 2. Clone
// ---------------------------------------------------------------------------

auto HookBaalClone(void* game, void* baal, void* aiControl) noexcept -> std::uint8_t {
    if (IsSidingWithPlayers(baal)) {
        ClonesRefused.fetch_add(1, std::memory_order_relaxed);
        return 0;
    }
    const BaalCloneFn original = OriginalBaalClone;
    return original != nullptr ? original(game, baal, aiControl) : 0;
}

// ---------------------------------------------------------------------------
// 3. Festering Appendages
// ---------------------------------------------------------------------------

// Replaces the call at 0x585CA7: same arguments, same callee first.
void RelayTentacleOwner(void* tentacle, void* owner) noexcept {
    StoreOwner(tentacle, owner);
    if (tentacle == nullptr || !IsSidingWithPlayers(owner)) return;
    const std::int32_t alignment = GetAlignment(owner);
    if (alignment <= AlignmentEvil || alignment >= AlignmentCount) return;
    SetAlignment(tentacle, static_cast<std::uint8_t>(alignment), 1);
    TentaclesJoined.fetch_add(1, std::memory_order_relaxed);
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
    if (value == "true") { out = true; return true; }
    if (value == "false") { out = false; return true; }
    return false;
}

auto SettingFor(std::string_view key) noexcept -> bool* {
    if (key == "enabled") return &Config.enabled;
    if (key == "targeting") return &Config.targeting;
    if (key == "clone") return &Config.clone;
    if (key == "tentacles") return &Config.tentacles;
    return nullptr;
}

void ParseConfig(std::string_view text) noexcept {
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
            section = Trim(line.substr(1, close == std::string_view::npos ? close : close - 1));
            continue;
        }
        if (section != ConfigSection) continue;

        const std::size_t equals = line.find('=');
        if (equals == std::string_view::npos) continue;
        const std::string_view key   = Trim(line.substr(0, equals));
        const std::string_view value = Trim(line.substr(equals + 1));
        const std::string      keyText(key);

        bool* setting = SettingFor(key);
        if (setting == nullptr) {
            D2RL::LogWarnF(Context, "BoundBaalAi: unknown setting \"%s\" in [%s] is ignored.",
                keyText.c_str(), ConfigSection);
            continue;
        }
        if (!ParseBool(value, *setting)) {
            D2RL::LogWarnF(Context, "BoundBaalAi: %s must be true or false; keeping %s.",
                keyText.c_str(), *setting ? "true" : "false");
        }
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

void ReadConfiguration() noexcept {
    std::string text;
    if (!Context->EnsureConfig(DefaultConfigToml)) {
        D2RL::LogWarnF(Context, "%s", "BoundBaalAi: the config file could not be created; using the embedded defaults.");
        text = DefaultConfigToml;
    } else if (!ReadConfigText(text)) {
        D2RL::LogWarnF(Context, "%s", "BoundBaalAi: the config file could not be read; using the embedded defaults.");
        text = DefaultConfigToml;
    } else {
        ConfigFileWasRead = true;
    }
    ParseConfig(text);
}

// ---------------------------------------------------------------------------
// Verification and native resolution
// ---------------------------------------------------------------------------

auto Check(std::uint64_t rva, const std::uint8_t* bytes, std::size_t size, const char* what) noexcept -> bool {
    if (Context->CheckExpectedBytes(rva, bytes, static_cast<std::uint32_t>(size))) return true;
    D2RL::LogErrorF(Context, "BoundBaalAi: %s at 0x%llX does not match the verified D2R image, or another "
        "plugin already patches it.", what, static_cast<unsigned long long>(rva));
    return false;
}

template <typename Function>
auto NativeAt(std::uint64_t rva) noexcept -> Function {
    return reinterpret_cast<Function>(Base + static_cast<std::uintptr_t>(rva));
}

auto ImageEnd() noexcept -> std::uintptr_t {
    const auto* image = reinterpret_cast<const std::uint8_t*>(Base);
    const std::uint32_t ntOffset  = ReadU32(image, 0x3C);
    const std::uint32_t imageSize = ReadU32(image, static_cast<std::size_t>(ntOffset) + 0x50);
    return Base + imageSize;
}

// Every threat call must still be the same E8 call, and all four must reach
// the same function inside the process image.
auto ResolveScore() noexcept -> ScoreFn {
    std::uintptr_t found = 0;
    for (const ScoreSite& site : ScoreSites) {
        if (!Check(site.headRva, site.head, sizeof(site.head), "a Baal threat call")
                || !Check(site.tailRva, site.tail, sizeof(site.tail), "a Baal threat call")) {
            return nullptr;
        }
        const std::uint64_t callRva = site.headRva + sizeof(site.head) - 1;
        std::int32_t rel = 0;
        std::memcpy(&rel, reinterpret_cast<const void*>(Base + static_cast<std::uintptr_t>(callRva) + 1), sizeof(rel));
        const std::uintptr_t next   = Base + static_cast<std::uintptr_t>(callRva) + 5;
        const std::uintptr_t target = static_cast<std::uintptr_t>(static_cast<std::intptr_t>(next) + rel);
        if (found == 0) {
            found = target;
        } else if (found != target) {
            D2RL::LogErrorF(Context, "%s", "BoundBaalAi: the four Baal threat calls no longer reach the same function.");
            return nullptr;
        }
    }
    if (found < Base || found >= ImageEnd()) {
        D2RL::LogErrorF(Context, "%s", "BoundBaalAi: the Baal threat call no longer reaches the process image.");
        return nullptr;
    }
    return reinterpret_cast<ScoreFn>(found);
}

auto ReadAllyRange() noexcept -> std::int32_t {
    std::int32_t range = 0;
    std::memcpy(&range, reinterpret_cast<const void*>(Base + static_cast<std::uintptr_t>(AllyRangeImmRva)),
        sizeof(range));
    if (range <= 0 || range > 1000) {
        D2RL::LogWarnF(Context, "BoundBaalAi: the allied search range reads %d; counting enemies within %d.",
            range, DefaultAllyRange);
        return DefaultAllyRange;
    }
    return range;
}

auto VerifyCommon() noexcept -> bool {
    if (!Check(AlignmentWindowRva, AlignmentWindow, sizeof(AlignmentWindow), "the alignment call")) return false;
    GetAlignment = NativeAt<GetAlignmentFn>(GetAlignmentRva);
    return true;
}

// ---------------------------------------------------------------------------
// Installation
// ---------------------------------------------------------------------------

auto InstallTargeting() noexcept -> bool {
    const bool verified =
        Check(BaalTargetRva, BaalTargetEntry, sizeof(BaalTargetEntry), "the Baal target routine")
        && Check(AiControlWindowRva, AiControlWindow, sizeof(AiControlWindow), "the AI control read")
        && Check(DifficultyWindowRva, DifficultyWindow, sizeof(DifficultyWindow), "the AI context call")
        && Check(SelectTargetWindowRva, SelectTargetWindow, sizeof(SelectTargetWindow), "the target picker call")
        && Check(EnumerateWindowRva, EnumerateWindow, sizeof(EnumerateWindow), "the unit enumerator call")
        && Check(HostileTestWindowRva, HostileTestWindow, sizeof(HostileTestWindow), "the allied target test")
        && Check(AllyRangeHeadRva, AllyRangeHead, sizeof(AllyRangeHead), "the allied search range")
        && Check(AllyRangeTailRva, AllyRangeTail, sizeof(AllyRangeTail), "the allied search range");
    if (!verified) return false;

    const ScoreFn score = ResolveScore();
    if (score == nullptr) return false;

    // Everything the hook calls is resolved before it goes live.
    Score             = score;
    AllyRange         = ReadAllyRange();
    DifficultyContext = NativeAt<DifficultyFn>(DifficultyRva);
    SelectTarget      = NativeAt<SelectTargetFn>(SelectTargetRva);
    Enumerate         = NativeAt<EnumerateFn>(EnumerateRva);
    ValidTarget       = NativeAt<ValidTargetFn>(ValidTargetRva);
    Distance          = NativeAt<DistanceFn>(DistanceRva);

    if (!Context->InstallInlineHook(BaalTargetRva, BaalTargetEntry, static_cast<std::uint32_t>(sizeof(BaalTargetEntry)),
            &HookBaalTarget, &OriginalBaalTarget) || OriginalBaalTarget == nullptr) {
        D2RL::LogErrorF(Context, "%s", "BoundBaalAi: the Baal target routine could not be hooked.");
        return false;
    }
    return true;
}

auto InstallClone() noexcept -> bool {
    if (!Check(BaalCloneRva, BaalCloneEntry, sizeof(BaalCloneEntry), "the Baal clone routine")) return false;
    if (!Context->InstallInlineHook(BaalCloneRva, BaalCloneEntry, static_cast<std::uint32_t>(sizeof(BaalCloneEntry)),
            &HookBaalClone, &OriginalBaalClone) || OriginalBaalClone == nullptr) {
        D2RL::LogErrorF(Context, "%s", "BoundBaalAi: the Baal clone routine could not be hooked.");
        return false;
    }
    return true;
}

void WriteAbsoluteJump(std::uint8_t* at, const void* destination) noexcept {
    static constexpr std::uint8_t JumpIndirect[6]{ 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00 };  // jmp [rip+0]
    std::memcpy(at, JumpIndirect, sizeof(JumpIndirect));
    const auto address = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(destination));
    std::memcpy(at + sizeof(JumpIndirect), &address, sizeof(address));
}

// A free page above the image, within rel32 reach of the relayed call.
auto AllocateRelayPage() noexcept -> std::uint8_t* {
    SYSTEM_INFO systemInfo{};
    GetSystemInfo(&systemInfo);
    const std::uintptr_t granularity =
        systemInfo.dwAllocationGranularity != 0 ? systemInfo.dwAllocationGranularity : 0x10000;
    const auto alignUp = [granularity](std::uintptr_t value) noexcept {
        return (value + granularity - 1) & ~(granularity - 1);
    };
    const std::uintptr_t limit   = Base + static_cast<std::uintptr_t>(TentacleOwnerCallRva) + 0x7FF00000ULL;
    std::uintptr_t       address = alignUp(ImageEnd());
    while (address + RelayPageBytes < limit) {
        MEMORY_BASIC_INFORMATION info{};
        if (VirtualQuery(reinterpret_cast<LPCVOID>(address), &info, sizeof(info)) == 0) break;
        const auto regionEnd = reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize;
        if (info.State == MEM_FREE && address + RelayPageBytes <= regionEnd) {
            if (void* page = VirtualAlloc(reinterpret_cast<LPVOID>(address), RelayPageBytes,
                    MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE)) {
                return static_cast<std::uint8_t*>(page);
            }
        }
        const std::uintptr_t next = alignUp(regionEnd);
        if (next <= address) break;
        address = next;
    }
    return nullptr;
}

void ReleaseRelayPage() noexcept {
    if (RelayPage == nullptr) return;
    VirtualFree(RelayPage, 0, MEM_RELEASE);
    RelayPage = nullptr;
}

auto InstallTentacles() noexcept -> bool {
    if (!Check(TentacleWindowRva, TentacleWindow, sizeof(TentacleWindow), "the tentacle owner call")
            || !Check(SetAlignmentWindowRva, SetAlignmentWindow, sizeof(SetAlignmentWindow), "the alignment setter call")) {
        return false;
    }
    StoreOwner   = NativeAt<StoreOwnerFn>(StoreOwnerRva);
    SetAlignment = NativeAt<SetAlignmentFn>(SetAlignmentRva);
    std::memcpy(TentacleStock.data(), TentacleWindow + TentacleCallOffset, TentacleStock.size());

    RelayPage = AllocateRelayPage();
    if (RelayPage == nullptr) {
        D2RL::LogErrorF(Context, "%s", "BoundBaalAi: no memory within reach of the tentacle owner call.");
        return false;
    }
    std::memset(RelayPage, 0xCC, RelayPageBytes);
    WriteAbsoluteJump(RelayPage, reinterpret_cast<const void*>(&RelayTentacleOwner));
    DWORD previous = 0;
    if (!VirtualProtect(RelayPage, RelayPageBytes, PAGE_EXECUTE_READ, &previous)) {
        ReleaseRelayPage();
        return false;
    }
    FlushInstructionCache(GetCurrentProcess(), RelayPage, RelayPageBytes);

    const std::uintptr_t from = Base + static_cast<std::uintptr_t>(TentacleOwnerCallRva) + 5;
    const std::uintptr_t to   = reinterpret_cast<std::uintptr_t>(RelayPage);
    const std::int64_t   rel  = static_cast<std::int64_t>(to) - static_cast<std::int64_t>(from);
    if (rel < INT32_MIN || rel > INT32_MAX) {
        D2RL::LogErrorF(Context, "%s", "BoundBaalAi: the relay page is out of reach of the tentacle owner call.");
        ReleaseRelayPage();
        return false;
    }
    const auto rel32 = static_cast<std::int32_t>(rel);
    TentacleWritten[0] = 0xE8;
    std::memcpy(TentacleWritten.data() + 1, &rel32, sizeof(rel32));
    if (!Context->PatchBytes(TentacleOwnerCallRva, TentacleStock.data(), static_cast<std::uint32_t>(TentacleStock.size()),
            TentacleWritten.data(), static_cast<std::uint32_t>(TentacleWritten.size()))) {
        D2RL::LogErrorF(Context, "%s", "BoundBaalAi: D2RLoader refused the tentacle owner call (another plugin may own it).");
        ReleaseRelayPage();
        return false;
    }
    TentaclePatched = true;
    if (std::memcmp(reinterpret_cast<const void*>(Base + static_cast<std::uintptr_t>(TentacleOwnerCallRva)),
            TentacleWritten.data(), TentacleWritten.size()) != 0) {
        D2RL::LogErrorF(Context, "%s", "BoundBaalAi: the tentacle owner call does not hold the written bytes.");
        if (Context->PatchBytes(TentacleOwnerCallRva, TentacleWritten.data(), static_cast<std::uint32_t>(TentacleWritten.size()),
                TentacleStock.data(), static_cast<std::uint32_t>(TentacleStock.size()))) {
            TentaclePatched = false;
            ReleaseRelayPage();
        }
        return false;
    }
    return true;
}

void RestoreTentacles() noexcept {
    if (!TentaclePatched) return;
    if (Context->PatchBytes(TentacleOwnerCallRva, TentacleWritten.data(), static_cast<std::uint32_t>(TentacleWritten.size()),
            TentacleStock.data(), static_cast<std::uint32_t>(TentacleStock.size()))) {
        TentaclePatched = false;
        ReleaseRelayPage();
    }
}

auto FixName(FixId fix) noexcept -> const char* {
    switch (fix) {
    case FixId::Targeting: return "targeting";
    case FixId::Clone:     return "clone";
    case FixId::Tentacles: return "tentacles";
    case FixId::Count:
    default:               return "?";
    }
}

void SetFix(FixId fix, FixState state) noexcept {
    Fixes[static_cast<std::size_t>(fix)] = state;
}

void Install() noexcept {
    if (!Config.enabled) {
        Fixes.fill(FixState::Off);
        D2RL::LogInfoF(Context, "%s", "BoundBaalAi: disabled in the config file; nothing installed.");
        return;
    }
    if (!VerifyCommon()) {
        Fixes.fill(FixState::Refused);
        D2RL::LogErrorF(Context, "%s", "BoundBaalAi: nothing installed; Baal's AI runs stock.");
        return;
    }

    struct Step { FixId id; bool wanted; bool (*install)() noexcept; };
    const std::array<Step, FixCount> steps{ {
        { FixId::Targeting, Config.targeting, &InstallTargeting },
        { FixId::Clone,     Config.clone,     &InstallClone },
        { FixId::Tentacles, Config.tentacles, &InstallTentacles },
    } };

    for (const Step& step : steps) {
        if (!step.wanted) {
            SetFix(step.id, FixState::Off);
            continue;
        }
        if (step.install()) {
            SetFix(step.id, FixState::Active);
        } else {
            SetFix(step.id, FixState::Refused);
            D2RL::LogErrorF(Context, "BoundBaalAi: the %s fix is NOT active; that part of Baal's AI runs stock.",
                FixName(step.id));
        }
    }

    D2RL::LogInfoF(Context, "BoundBaalAi: targeting %s, clone %s, tentacles %s.",
        Fixes[0] == FixState::Active ? "active" : "off",
        Fixes[1] == FixState::Active ? "active" : "off",
        Fixes[2] == FixState::Active ? "active" : "off");
}

// ---------------------------------------------------------------------------
// Console
// ---------------------------------------------------------------------------

auto StateText(FixState state) noexcept -> const char* {
    switch (state) {
    case FixState::Active:       return "active";
    case FixState::Off:          return "off in the config file";
    case FixState::Refused:      return "NOT ACTIVE, see the log";
    case FixState::NotAttempted:
    default:                     return "NOT ACTIVE, never attempted";
    }
}

auto __cdecl StatusCommand(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command, void*) noexcept
        -> D2RL::ConsoleCommandResult {
    if (command == nullptr || command->plugin == nullptr) return D2RL::ConsoleCommandResult::Failed;
    const D2RL::PluginContext* plugin = command->plugin;

    char line[256]{};
    std::snprintf(line, sizeof(line), "Bound Baal AI: %s | config %s",
        Config.enabled ? "enabled" : "disabled",
        ConfigFileWasRead ? "loaded" : "NOT READ (embedded defaults)");
    plugin->WriteConsoleMessage(line);

    std::snprintf(line, sizeof(line), "targeting: %s, %llu allied target picks, %llu found no enemy",
        StateText(Fixes[0]),
        static_cast<unsigned long long>(AlliedPicks.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(AlliedPicksWithoutEnemy.load(std::memory_order_relaxed)));
    plugin->WriteConsoleMessage(line);

    std::snprintf(line, sizeof(line), "clone: %s, %llu clones refused",
        StateText(Fixes[1]), static_cast<unsigned long long>(ClonesRefused.load(std::memory_order_relaxed)));
    plugin->WriteConsoleMessage(line);

    std::snprintf(line, sizeof(line), "tentacles: %s, %llu tentacles joined their Baal's side",
        StateText(Fixes[2]), static_cast<unsigned long long>(TentaclesJoined.load(std::memory_order_relaxed)));
    plugin->WriteConsoleMessage(line);
    return D2RL::ConsoleCommandResult::Handled;
}

constexpr D2RL::PluginInfo Info{
    .infoSize    = D2RL::PluginInfoSize,
    .abiVersion  = D2RL_PLUGIN_ABI_VERSION,
    .id          = "celestialrayone.bound-baal-ai",
    .name        = "Bound Baal AI",
    .version     = "1.0.0",
    .author      = "CelestialRayOne",
    .description = "A bound or converted Baal fights the player's enemies instead of the player: allied "
                   "targeting, no hostile clone, and tentacles that join his side.",
    // Server: monster AI and skills only run on the server.
    .flags       = D2RL::PluginFlags::Server | D2RL::PluginFlags::NativeHooks,
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
    Base    = context->exeBase;

    // Registered first, and the load always succeeds: returning false would
    // unload the DLL and take the command with it.
    if (!Context->RegisterConsoleCommand("boundbaal", &StatusCommand,
            "Reports Bound Baal AI status and counters.")) {
        D2RL::LogWarnF(Context, "%s", "BoundBaalAi: the status console command was refused.");
    }

    ReadConfiguration();
    Install();
    return true;
}

// The loader removes its inline hooks itself; the relayed call is put back here.
D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
    RestoreTentacles();
}

}  // namespace CelestialRayOne::BoundBaalAi
