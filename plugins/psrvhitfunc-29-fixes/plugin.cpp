// ---------------------------------------------------------------------------
// psrvhitfunc-29-fixes
//
// Engine fixes for Missiles.txt pSrvHitFunc 29, the burst Frozen Orb and
// everything built on it uses, each behind its own switch:
//
//   frozen_orb_burst_on_wall      a missile that dies on a wall bursts
//   burst_missile_survives_miss   a missile that misses its to-hit roll flies
//                                 on instead of being deleted
//
// They correct engine bugs that are not crashes, so they do change gameplay:
// they make the engine apply what its own data already says.
//
// Target: D2RLoader 1.3.1, which runs Diablo II: Resurrected 3.3 inside
// D2RLoader.exe and moves part of the engine into D2RCore.dll.
//
// Split out of engine-stability 0.12.0, which also carried a skill-freeze AI
// resume fix. That one is not carried over: D2RLoader handles it.
//
// Safety model: every game address below is an RVA against image base
// 0x140000000, and nothing is installed unless the bytes already at that RVA
// match byte for byte. On any build the plugin does not recognise it installs
// nothing, logs loudly and stays loaded so the console command can explain why.
// An unrecognised build is therefore a no-op, never a mis-patch.
//
// Configuration: every fix has an on/off switch in
// <scope>\d2rloader\config\celestialrayone.psrvhitfunc-29-fixes.toml, all true
// by default. The file is created with documented defaults on first run. The
// config is read once at load, so edits need a game restart. A fix that the
// config turned off and a fix the build refused are reported differently by
// the console command -- they are not the same thing and must never look it.
// ---------------------------------------------------------------------------

#include <D2RLPlugin/api.h>

// Windows.h defines min/max as macros unless NOMINMAX is set.
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

namespace {

// ---------------------------------------------------------------------------
// Shared helpers
// ---------------------------------------------------------------------------

template <std::size_t Size>
constexpr auto ByteCount(const std::uint8_t (&)[Size]) noexcept -> std::uint32_t {
	return static_cast<std::uint32_t>(Size);
}

// True when `prefix` is exactly the leading bytes of `body`. Used to prove at
// compile time that the bytes an inline hook displaces really are the first
// bytes of the body the plugin verified.
template <std::size_t PrefixSize, std::size_t BodySize>
constexpr auto IsLeadingBytesOf(
	const std::uint8_t (&prefix)[PrefixSize],
	const std::uint8_t (&body)[BodySize]) noexcept -> bool {
	if (PrefixSize > BodySize) {
		return false;
	}
	for (std::size_t index = 0; index < PrefixSize; ++index) {
		if (prefix[index] != body[index]) {
			return false;
		}
	}
	return true;
}

// True when `part` appears in `whole` starting at `offset`.
template <std::size_t PartSize, std::size_t WholeSize>
constexpr auto IsSubrangeOf(
	const std::uint8_t (&part)[PartSize],
	const std::uint8_t (&whole)[WholeSize],
	std::size_t offset) noexcept -> bool {
	if (offset + PartSize > WholeSize) {
		return false;
	}
	for (std::size_t index = 0; index < PartSize; ++index) {
		if (part[index] != whole[offset + index]) {
			return false;
		}
	}
	return true;
}

// rva of the callee of the E8 call at `offset` inside a verified window.
template <std::size_t Size>
constexpr auto CallTargetRva(const std::uint8_t (&window)[Size], std::uint64_t windowRva, std::size_t offset) noexcept -> std::uint64_t {
	const auto rel = static_cast<std::int32_t>(
		static_cast<std::uint32_t>(window[offset + 1])
		| (static_cast<std::uint32_t>(window[offset + 2]) << 8)
		| (static_cast<std::uint32_t>(window[offset + 3]) << 16)
		| (static_cast<std::uint32_t>(window[offset + 4]) << 24));
	return static_cast<std::uint64_t>(static_cast<std::int64_t>(windowRva + offset + 5) + rel);
}

// True when the eight byte slot at `offset` is the target of a
// jmp qword ptr [rip+0] that ends right before it.
template <std::size_t Size>
constexpr auto HasAbsoluteJumpBefore(const std::uint8_t (&code)[Size], std::size_t offset) noexcept -> bool {
	return offset >= 6
		&& offset + 8 <= Size
		&& code[offset - 6] == 0xFF
		&& code[offset - 5] == 0x25
		&& code[offset - 4] == 0x00
		&& code[offset - 3] == 0x00
		&& code[offset - 2] == 0x00
		&& code[offset - 1] == 0x00;
}

// ---------------------------------------------------------------------------
// Guard identity
// ---------------------------------------------------------------------------

enum class Guard : std::size_t {
	FrozenOrbBurstOnWall,
	BurstMissileSurvivesMiss,
	Count,
};

constexpr std::size_t GuardCount = static_cast<std::size_t>(Guard::Count);

constexpr auto Index(Guard guard) noexcept -> std::size_t {
	return static_cast<std::size_t>(guard);
}

// Why a guard is not running. "You turned it off" and "this build is not
// supported" must never be reported as the same thing.
enum class GuardState : std::uint8_t {
	NotAttempted,
	Installed,
	DisabledByConfig,
	UnsupportedBuild,
	InstallFailed,
};

std::array<std::atomic<GuardState>, GuardCount> GuardStates {};

void SetGuardState(Guard guard, GuardState state) noexcept {
	GuardStates[Index(guard)].store(state, std::memory_order_release);
}

auto GetGuardState(Guard guard) noexcept -> GuardState {
	return GuardStates[Index(guard)].load(std::memory_order_acquire);
}

// ---------------------------------------------------------------------------
// Fix 1: the burst hit function never runs when a missile dies on a wall
//        (port of the 2.4 fix at 3D1DCC)
// ---------------------------------------------------------------------------
//
//   sub_14045FB50 is missiles.txt pSrvHitFunc 29, the burst Frozen Orb and
//   everything built on it uses: it spawns a ring of the missile's
//   HitSubMissile around the missile. Being a hit function, the engine calls it
//   for every collision the missile has, and the burst is only meant to happen
//   once, at the end of the missile's life. It tells those apart by the frame
//   counter:
//
//     0045FB91  call 00166040             missiles.txt record
//     0045FBA2  cmp  word ptr [rax+46h], 0  HitSubMissile
//     0045FBA7  jl   0045FD00             none: nothing to spawn, return 1
//     0045FBAD  mov  rcx, rbx             the missile
//     0045FBB0  call 003BB1E0             frames left = ceil(countdown / 40)
//     0045FBB5  test eax, eax
//     0045FBB7  je   0045FBC3             none left: burst
//     0045FBB9  mov  eax, 2               still flying: refuse
//     0045FBBE  jmp  0045FD05             and leave
//
//   The missile step 00463100 decrements that counter every update and only
//   resolves the hit once it has run out, so a missile that lives out its Range
//   arrives here with zero frames left and bursts. A missile stopped by a wall
//   does not: the step sees the collision and resolves the hit with frames
//   still on the clock, so the counter test refuses. The missile dies silently
//   against the wall and the whole ring, with all the damage it carries, is
//   lost.
//
//   The counter is not the right question. The step already says what happened,
//   in the third argument it hands to the hit path: a unit for a collision with
//   a unit, null for a wall and for running out of range.
//
//     004631F0  ResolveHit(game, missile, 0, 1)      out of frames
//     004633D5  ResolveHit(game, missile, 0, 1)      wall
//     004633F2  ResolveHit(game, missile, unit, 0)   unit found in the cell
//
//   ResolveHit 004639A0 passes that argument straight through to the hit
//   function in r8, at both of its dispatch sites and at the third one in
//   00464AC0. So what the frame counter is really being asked is "is this a
//   passing hit on a unit", and the argument answers it exactly.
//
//   The fix: refuse the burst only when there are frames left AND the hit is
//   against a unit. A mid-flight unit collision behaves exactly as before, the
//   end of Range behaves exactly as before, and a wall now bursts.
//
//   r8 does not survive to the counter test: 0045FB7D loads it with an assert
//   line number. So it is spilled first, into [rsp+20h]. That slot is the head
//   of the missile creation block this function builds later; nothing reads or
//   writes it until 0045FBEC zeroes it, which is past both patched sites, and
//   the three calls in between can only touch the shadow space at
//   [rsp+00h..1Fh].
//
//   The spill site is rewritten in place, 6 bytes:
//
//     0045FB6D  E9 <rel32>                jmp  relay page + B0h
//     0045FB72  90                        never executed
//
//   and its relay stub does the two displaced moves, the spill, and rejoins:
//
//     +00  48 8B DA                       mov  rbx, rdx            ; stock
//     +03  48 8B E9                       mov  rbp, rcx            ; stock
//     +06  4C 89 44 24 20                 mov  [rsp+20h], r8       ; the hit unit
//     +0B  FF 25 00 00 00 00 <abs64>      jmp  0045FB73
//
//   The counter test is rewritten in place, 14 bytes:
//
//     0045FBB5  E9 <rel32>                jmp  relay page + D0h
//     0045FBBA  90 x9                     never executed
//
//   and its relay stub adds the second test:
//
//     +00  85 C0                          test eax, eax
//     +02  74 1B                          je   +1Fh                ; no frames left: burst
//     +04  48 83 7C 24 20 00              cmp  qword ptr [rsp+20h], 0
//     +0A  74 13                          je   +1Fh                ; no unit: wall or expiry, burst
//     +0C  B8 02 00 00 00                 mov  eax, 2              ; stock refuse value
//     +11  FF 25 00 00 00 00 <abs64>      jmp  0045FD05            ; stock refuse exit
//     +1F  FF 25 00 00 00 00 <abs64>      jmp  0045FBC3            ; burst
//
//   The refuse path jumps to the function's own epilogue rather than to
//   0045FBB9, because 0045FBB9 is inside the replaced region and is a NOP after
//   the patch; landing there would slide through the NOPs into the burst. Stock
//   0045FBB9 is mov eax,2 / jmp 0045FD05, so setting eax in the stub and going
//   straight to 0045FD05 is the same thing.
//
//   eax is dead on the burst path: stock reaches 0045FBC3 with eax zero from
//   the counter, the fix can reach it with the counter still set, and 0045FBC3
//   is mov rdx,rbx / mov rcx,rbp / call, which rewrites it. The flags are dead
//   at both destinations. Neither stub touches rsp, so the call inside the
//   function is made on exactly the alignment it had before.
//
//   The whole function, 0045FB50..0045FD1F, is verified before patching, which
//   also proves the counter call, both stubs' destinations and the refuse
//   value. Decoded end to end: the only branch targets inside it are 0045FBC3,
//   0045FC90, 0045FCDD, 0045FD00 and 0045FD05, so nothing lands in either
//   replaced region, and 0045FBC3's only source is the je being replaced.

constexpr std::uint64_t FrozenOrbHitRva          = 0x0045FB50ULL;  // missiles.txt pSrvHitFunc 29
constexpr std::uint64_t FrozenOrbSpillSiteRva    = 0x0045FB6DULL;
constexpr std::uint64_t FrozenOrbSpillResumeRva  = 0x0045FB73ULL;
constexpr std::uint64_t FrozenOrbGateSiteRva     = 0x0045FBB5ULL;
constexpr std::uint64_t FrozenOrbBurstRva        = 0x0045FBC3ULL;
constexpr std::uint64_t FrozenOrbEpilogueRva     = 0x0045FD05ULL;
constexpr std::uint64_t MissileCurrentFrameRva   = 0x003BB1E0ULL;  // call at 0045FBB0

// Where the hit unit is parked between the two sites. Third home slot of the
// shadow space this function reserves for its own calls, and the first dword of
// the missile creation block it fills from 0045FBEC.
constexpr std::uint8_t FrozenOrbUnitSlotDisp = 0x20;

// RVA 0x45FB50, 464 bytes. pSrvHitFunc 29, D2Game\src\Missiles\MissMode.cpp.
constexpr std::uint8_t FrozenOrbHitBody[] {
	0x40, 0x53, 0x55, 0x57, 0x48, 0x81, 0xEC, 0xD0, 0x00, 0x00, 0x00, 0x48,
	0x8B, 0x05, 0x66, 0xB7, 0x56, 0x02, 0x48, 0x33, 0xC4, 0x48, 0x89, 0x84,
	0x24, 0xC0, 0x00, 0x00, 0x00, 0x48, 0x8B, 0xDA, 0x48, 0x8B, 0xE9, 0x48,
	0x8B, 0xCB, 0x48, 0x8D, 0x15, 0xB3, 0xB1, 0x8B, 0x01, 0x41, 0xB8, 0x50,
	0x13, 0x00, 0x00, 0xE8, 0xD8, 0x9C, 0xEE, 0xFF, 0x0F, 0xB6, 0x8D, 0x06,
	0x01, 0x00, 0x00, 0x8B, 0xD0, 0xE8, 0xAA, 0x64, 0xD0, 0xFF, 0x48, 0x8B,
	0xF8, 0x48, 0x85, 0xC0, 0x0F, 0x84, 0x5E, 0x01, 0x00, 0x00, 0x66, 0x83,
	0x78, 0x46, 0x00, 0x0F, 0x8C, 0x53, 0x01, 0x00, 0x00, 0x48, 0x8B, 0xCB,
	0xE8, 0x2B, 0xB6, 0xF5, 0xFF, 0x85, 0xC0, 0x74, 0x0A, 0xB8, 0x02, 0x00,
	0x00, 0x00, 0xE9, 0x42, 0x01, 0x00, 0x00, 0x48, 0x8B, 0xD3, 0x48, 0x8B,
	0xCD, 0xE8, 0x32, 0x07, 0x03, 0x00, 0x48, 0x85, 0xC0, 0x0F, 0x84, 0x29,
	0x01, 0x00, 0x00, 0x0F, 0x57, 0xC0, 0x48, 0x89, 0xB4, 0x24, 0x00, 0x01,
	0x00, 0x00, 0x33, 0xC9, 0x4C, 0x89, 0xB4, 0x24, 0x08, 0x01, 0x00, 0x00,
	0x0F, 0x11, 0x44, 0x24, 0x20, 0x48, 0x89, 0x8C, 0x24, 0xB0, 0x00, 0x00,
	0x00, 0x48, 0x8B, 0xCB, 0x0F, 0x11, 0x44, 0x24, 0x30, 0xC7, 0x44, 0x24,
	0x20, 0x02, 0x00, 0x00, 0x00, 0x0F, 0x11, 0x44, 0x24, 0x40, 0x48, 0x89,
	0x44, 0x24, 0x28, 0x0F, 0x11, 0x44, 0x24, 0x50, 0x48, 0x89, 0x5C, 0x24,
	0x30, 0x0F, 0x11, 0x44, 0x24, 0x60, 0x0F, 0x11, 0x44, 0x24, 0x70, 0x0F,
	0x11, 0x84, 0x24, 0x80, 0x00, 0x00, 0x00, 0x0F, 0x11, 0x84, 0x24, 0x90,
	0x00, 0x00, 0x00, 0x0F, 0x11, 0x84, 0x24, 0xA0, 0x00, 0x00, 0x00, 0xE8,
	0x0C, 0xC8, 0xF5, 0xFF, 0x48, 0x8B, 0xCB, 0x89, 0x44, 0x24, 0x5C, 0xE8,
	0x20, 0xB8, 0xF5, 0xFF, 0x48, 0x8B, 0xCB, 0x89, 0x44, 0x24, 0x60, 0xE8,
	0x64, 0xBA, 0xEE, 0xFF, 0x89, 0x84, 0x24, 0x88, 0x00, 0x00, 0x00, 0x4C,
	0x8D, 0x35, 0x96, 0x03, 0xBA, 0xFF, 0x0F, 0xBF, 0x47, 0x46, 0xB9, 0x01,
	0x00, 0x00, 0x00, 0x89, 0x44, 0x24, 0x40, 0x8B, 0x47, 0x70, 0x3B, 0xC1,
	0x0F, 0x4C, 0xC1, 0x8B, 0xF0, 0x48, 0xC1, 0xE6, 0x02, 0x33, 0xDB, 0x66,
	0x0F, 0x1F, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00, 0x42, 0x8B, 0x84, 0x33,
	0x50, 0xB4, 0xD1, 0x01, 0x48, 0x8D, 0x54, 0x24, 0x20, 0x89, 0x44, 0x24,
	0x4C, 0x48, 0x8B, 0xCD, 0x42, 0x8B, 0x84, 0x33, 0x50, 0xB5, 0xD1, 0x01,
	0x89, 0x44, 0x24, 0x50, 0xE8, 0xEB, 0x74, 0x0D, 0x00, 0x48, 0x8B, 0xF8,
	0x48, 0x85, 0xC0, 0x74, 0x20, 0x42, 0x8B, 0x94, 0x33, 0x50, 0xB4, 0xD1,
	0x01, 0x48, 0x8B, 0xC8, 0xE8, 0xD3, 0xDB, 0xF5, 0xFF, 0x42, 0x8B, 0x94,
	0x33, 0x50, 0xB5, 0xD1, 0x01, 0x48, 0x8B, 0xCF, 0xE8, 0x43, 0xDD, 0xF5,
	0xFF, 0x48, 0x03, 0xDE, 0x48, 0x81, 0xFB, 0x00, 0x01, 0x00, 0x00, 0x7C,
	0xA7, 0x4C, 0x8B, 0xB4, 0x24, 0x08, 0x01, 0x00, 0x00, 0xB8, 0x03, 0x00,
	0x00, 0x00, 0x48, 0x8B, 0xB4, 0x24, 0x00, 0x01, 0x00, 0x00, 0xEB, 0x05,
	0xB8, 0x01, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x8C, 0x24, 0xC0, 0x00, 0x00,
	0x00, 0x48, 0x33, 0xCC, 0xE8, 0x3B, 0x14, 0xE7, 0x00, 0x48, 0x81, 0xC4,
	0xD0, 0x00, 0x00, 0x00, 0x5F, 0x5D, 0x5B, 0xC3,
};

// mov rbx, rdx / mov rbp, rcx
constexpr std::uint8_t FrozenOrbSpillOriginal[] {
	0x48, 0x8B, 0xDA,
	0x48, 0x8B, 0xE9,
};

// jmp rel32 to the relay stub, rel32 filled at install / one never-executed NOP.
constexpr std::uint8_t FrozenOrbSpillTemplate[] {
	0xE9, 0x00, 0x00, 0x00, 0x00,
	0x90,
};

constexpr std::uint32_t FrozenOrbSpillSiteRel32Offset = 1;

// test eax, eax / je 0045FBC3 / mov eax, 2 / jmp 0045FD05
constexpr std::uint8_t FrozenOrbGateOriginal[] {
	0x85, 0xC0,
	0x74, 0x0A,
	0xB8, 0x02, 0x00, 0x00, 0x00,
	0xE9, 0x42, 0x01, 0x00, 0x00,
};

// jmp rel32 to the relay stub, rel32 filled at install / nine never-executed NOPs.
constexpr std::uint8_t FrozenOrbGateTemplate[] {
	0xE9, 0x00, 0x00, 0x00, 0x00,
	0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
};

constexpr std::uint32_t FrozenOrbGateSiteRel32Offset = 1;

// Native relay stubs. The absolute targets are filled at install.
constexpr std::uint8_t FrozenOrbSpillStub[] {
	0x48, 0x8B, 0xDA,
	0x48, 0x8B, 0xE9,
	0x4C, 0x89, 0x44, 0x24, 0x20,
	0xFF, 0x25, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

constexpr std::size_t FrozenOrbSpillResumeTargetOffset = 0x11;

constexpr std::uint8_t FrozenOrbGateStub[] {
	0x85, 0xC0,
	0x74, 0x1B,
	0x48, 0x83, 0x7C, 0x24, 0x20, 0x00,
	0x74, 0x13,
	0xB8, 0x02, 0x00, 0x00, 0x00,
	0xFF, 0x25, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0xFF, 0x25, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

constexpr std::size_t FrozenOrbGateRefuseTargetOffset = 0x17;
constexpr std::size_t FrozenOrbGateBurstTargetOffset  = 0x25;

static_assert(sizeof(FrozenOrbHitBody) == 0x1D0, "Verified pSrvHitFunc 29 length changed.");
static_assert(FrozenOrbHitBody[sizeof(FrozenOrbHitBody) - 1] == 0xC3, "The verified body must end on the function's ret.");
static_assert(sizeof(FrozenOrbSpillOriginal) == sizeof(FrozenOrbSpillTemplate), "A byte patch must not change the length of the region it replaces.");
static_assert(sizeof(FrozenOrbGateOriginal) == sizeof(FrozenOrbGateTemplate), "A byte patch must not change the length of the region it replaces.");
static_assert(IsSubrangeOf(FrozenOrbSpillOriginal, FrozenOrbHitBody, FrozenOrbSpillSiteRva - FrozenOrbHitRva), "The replaced bytes must sit inside the verified body at the stated offset.");
static_assert(IsSubrangeOf(FrozenOrbGateOriginal, FrozenOrbHitBody, FrozenOrbGateSiteRva - FrozenOrbHitRva), "The replaced bytes must sit inside the verified body at the stated offset.");
static_assert(FrozenOrbSpillResumeRva == FrozenOrbSpillSiteRva + sizeof(FrozenOrbSpillOriginal), "The spill stub must rejoin at the instruction after the site.");
static_assert(FrozenOrbBurstRva == FrozenOrbGateSiteRva + sizeof(FrozenOrbGateOriginal), "The burst exit is the instruction after the site.");
static_assert(FrozenOrbHitBody[0x45FBB0 - 0x45FB50] == 0xE8 && CallTargetRva(FrozenOrbHitBody, FrozenOrbHitRva, 0x45FBB0 - 0x45FB50) == MissileCurrentFrameRva, "The frame counter is not the callee witnessed at 0045FBB0.");
static_assert(FrozenOrbHitBody[0x45FBBE - 0x45FB50] == 0xE9 && CallTargetRva(FrozenOrbHitBody, FrozenOrbHitRva, 0x45FBBE - 0x45FB50) == FrozenOrbEpilogueRva, "The stock refuse path does not leave through the epilogue at 0045FD05.");
static_assert(FrozenOrbGateOriginal[4] == 0xB8 && FrozenOrbGateOriginal[5] == 0x02, "The stock refuse value is not 2.");
static_assert(FrozenOrbGateStub[12] == FrozenOrbGateOriginal[4] && FrozenOrbGateStub[13] == FrozenOrbGateOriginal[5], "The stub must refuse with the stock value.");
static_assert(IsLeadingBytesOf(FrozenOrbSpillOriginal, FrozenOrbSpillStub), "The spill stub must start with the bytes it displaced.");
static_assert(FrozenOrbSpillStub[6] == 0x4C && FrozenOrbSpillStub[7] == 0x89 && FrozenOrbSpillStub[8] == 0x44 && FrozenOrbSpillStub[9] == 0x24 && FrozenOrbSpillStub[10] == FrozenOrbUnitSlotDisp, "The spill must be mov [rsp+20h], r8.");
static_assert(FrozenOrbGateStub[0] == FrozenOrbGateOriginal[0] && FrozenOrbGateStub[1] == FrozenOrbGateOriginal[1], "The gate stub must start with the stock counter test.");
static_assert(FrozenOrbGateStub[8] == FrozenOrbUnitSlotDisp, "The gate must read the slot the spill wrote.");
static_assert(FrozenOrbGateStub[3] == FrozenOrbGateBurstTargetOffset - 6 - 0x04 && FrozenOrbGateStub[11] == FrozenOrbGateBurstTargetOffset - 6 - 0x0C, "Both stub branches must land on the burst jump.");
static_assert(HasAbsoluteJumpBefore(FrozenOrbSpillStub, FrozenOrbSpillResumeTargetOffset), "Spill rejoin target must follow its jmp.");
static_assert(HasAbsoluteJumpBefore(FrozenOrbGateStub, FrozenOrbGateRefuseTargetOffset), "Refuse target must follow its jmp.");
static_assert(HasAbsoluteJumpBefore(FrozenOrbGateStub, FrozenOrbGateBurstTargetOffset), "Burst target must follow its jmp.");
static_assert(sizeof(FrozenOrbSpillStub) == 25 && sizeof(FrozenOrbGateStub) == 45, "Frozen orb stub length changed.");


// ---------------------------------------------------------------------------
// Fix 2: a pSrvHitFunc 29 missile is deleted when its to-hit roll misses
// ---------------------------------------------------------------------------
//
//   Missile hit resolution 004639A0 rolls to hit before anything else for a
//   row with ToHit. On a miss it never reads CollideKill: the missile is
//   spent, the way a missed arrow is spent, unless the row has AlwaysExplode
//   and the hit function, run once for the miss, answers bit 4 or bit 8:
//
//     00463C2C  cmp  byte ptr [r13+1B3h], dil   ToHit
//     00463C5F  call 0044BA60                   the to-hit roll
//     00463C66  jne  00463D8F                   hit: the normal path
//     00463D1C  cmp  byte ptr [r13+1B4h], dil   miss: AlwaysExplode
//     00463D52  call r9                         the row's pSrvHitFunc
//     00463D55  test al, 0Ch                    4 or 8: keep the missile
//     00463D59  mov  dword ptr [rsp+48h], 2     else return 2
//
//   and the missile dispatcher 00466CE0 deletes a missile whose step returns
//   2. A hit reads the same answer bit by bit instead:
//
//     00463E4E  test al, 4      absorbed: no damage, keep the missile
//     00463E8A  test dil, 2     deal the damage
//     0046400F  test dil, 1     kill the missile
//
//   Bit 8 is read by the miss branch only, so it means "keep me after a miss"
//   and nothing else.
//
//   pSrvHitFunc 29 answers a mid-flight contact with a unit with 2: no burst
//   yet, deal the damage, fly on. A hit reads that as fly on; a miss finds
//   neither 4 nor 8 and deletes the missile. So a burst missile that misses
//   dies on the spot with no damage and no burst, while the same missile
//   hitting flies on and bursts at the end of its Range. Frozen Orb never
//   rolls to hit, which is why vanilla never showed it.
//
//   The fix: the mid-flight refuse answers 0Ah (2 plus 8) instead of 2. A hit
//   never reads bit 8, so it is exactly as before. A miss now keeps the
//   missile, and it bursts where it would have anyway. The burst answer (3)
//   and the nothing-to-spawn answer (1) are untouched, so a miss on the
//   missile's last frame still bursts and ends it. The hit resolution itself
//   is not touched, and the miss has already stamped LastCollide (00463ABD)
//   before the roll, so with LastCollide set the missed unit is not rolled
//   again by the same missile.
//
//   Where the refuse lives decides where the value is written:
//     burst-on-wall fix installed      the relay gate stub, +0Ch mov eax, 2,
//                                      written when the relay page is built
//     burst-on-wall fix not installed  stock 0045FBB9 B8 02 00 00 00
//   0045FBB9 sits inside the 14 bytes the burst-on-wall fix replaces, so the
//   two are never written at the same time.

constexpr std::uint64_t BurstRefuseSiteRva                = 0x0045FBB9ULL;  // stock mov eax, 2
constexpr std::uint8_t  BurstRefuseKeepValue               = 0x0A;           // 2 (fly on) + 8 (keep after a miss)
constexpr std::size_t   FrozenOrbGateStubRefuseImmOffset   = 13;             // immediate of the stub's mov eax, 2

// mov eax, 2
constexpr std::uint8_t BurstRefuseStock[] {
	0xB8, 0x02, 0x00, 0x00, 0x00,
};

// mov eax, 0Ah
constexpr std::uint8_t BurstRefuseKeep[] {
	0xB8, BurstRefuseKeepValue, 0x00, 0x00, 0x00,
};

static_assert(IsSubrangeOf(BurstRefuseStock, FrozenOrbHitBody, BurstRefuseSiteRva - FrozenOrbHitRva), "The stock refuse must sit inside the verified body at 0045FBB9.");
static_assert(IsSubrangeOf(BurstRefuseStock, FrozenOrbGateOriginal, BurstRefuseSiteRva - FrozenOrbGateSiteRva), "The stock refuse must be the one the burst-on-wall gate replaces.");
static_assert(FrozenOrbGateStub[FrozenOrbGateStubRefuseImmOffset - 1] == BurstRefuseStock[0] && FrozenOrbGateStub[FrozenOrbGateStubRefuseImmOffset] == BurstRefuseStock[1], "The relay stub's refuse immediate is not where the miss fix writes it.");
static_assert(FrozenOrbGateStubRefuseImmOffset + sizeof(std::uint32_t) <= FrozenOrbGateRefuseTargetOffset - 6, "The refuse immediate must end before the refuse jump.");
static_assert(sizeof(BurstRefuseStock) == sizeof(BurstRefuseKeep), "A byte patch must not change the length of the region it replaces.");
static_assert(BurstRefuseKeep[0] == BurstRefuseStock[0] && BurstRefuseKeep[2] == BurstRefuseStock[2] && BurstRefuseKeep[3] == BurstRefuseStock[3] && BurstRefuseKeep[4] == BurstRefuseStock[4], "Only the immediate may change.");
static_assert(BurstRefuseKeepValue == (BurstRefuseStock[1] | 0x08), "The keep value must be the stock refuse plus bit 8 and nothing else.");
static_assert((BurstRefuseKeepValue & 0x0C) != 0, "A miss must keep the missile (00463D55 test al, 0Ch).");
static_assert((BurstRefuseKeepValue & 0x04) == 0, "A hit must not be absorbed (00463E4E test al, 4).");
static_assert((BurstRefuseKeepValue & 0x02) != 0, "A hit must still deal its damage (00463E8A test dil, 2).");
static_assert((BurstRefuseKeepValue & 0x01) == 0, "A hit must still fly on (0046400F test dil, 1).");


// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------
//
// The loader hands the plugin the raw text of its own TOML file; there is no
// parser in the SDK. Rather than pull one in for a handful of booleans, this is
// a small line scanner that understands exactly what this file needs: [section]
// headers, `key = true` / `key = false`, `#` comments (whole line or trailing),
// and both LF and CRLF. Anything it does not understand it ignores, which is
// the right failure mode for a config file a player edits by hand.
//
// The defaults are the safe direction: if the file is missing, unreadable or
// truncated, every guard stays ON and the plugin says so in the log. Losing a
// crash guard because a config file could not be read would be the worse of
// the two outcomes.

// Kept identical to the shipped celestialrayone.psrvhitfunc-29-fixes.toml. This is
// what EnsureConfig writes when the file does not exist yet.
constexpr const char* DefaultConfigToml =
R"toml(# =============================================================================
# pSrvHitFunc 29 Fixes 1.0.0
# Engine fixes for Missiles.txt pSrvHitFunc 29 in Diablo II: Resurrected.
#
# Built against D2RLoader 1.3.1: Diablo II: Resurrected 3.3 inside
# D2RLoader.exe.
# =============================================================================
#
# WHAT THIS IS
#
#   pSrvHitFunc 29 is the burst used by Frozen Orb and everything built on it:
#   at the end of the missile's life it spawns a ring of the missile's
#   HitSubMissile. The two fixes below correct engine bugs in how a missile
#   reaches that burst. They are not crash fixes, so they DO change gameplay:
#   they make the engine apply what its own data already says. Turning one off
#   restores stock behaviour.
#
#   Split out of engine-stability 0.12.0. Its skill_freeze_ai_resume fix is
#   not carried over, and celestialrayone.engine-stability.toml is not read.
#
# SAFETY MODEL
#
#   Every site is verified byte for byte before anything is written. On a build
#   the plugin does not recognise it installs nothing, no matter what this file
#   says, logs the reason and stays loaded so it can still answer for itself.
#   An unrecognised build is a no-op, never a mis-patch.
#
#   "Turned off in the config" and "this build was not recognised" are reported
#   as different things, in the log and in the console command. They are not
#   the same problem and never look the same.
#
# USING THIS FILE
#
#   Location: <scope>\d2rloader\config\celestialrayone.psrvhitfunc-29-fixes.toml
#             where <scope> is the game root for a global install, or
#             mods\<mod>\ for a mod-scoped one. Never both.
#
#   Read once at plugin load, so restart the game after editing.
#   Delete the file to have it recreated with these defaults.
#   A missing key keeps its default. An unknown key is ignored.
#   Values are true or false. Anything else is ignored and the default stands.
#
#   Type `psrvhitfunc-29-fixes` in the console to see what is actually live,
#   which is not the same question as what this file asks for.


[psrvhitfunc-29-fixes]

# Master switch for the whole plugin.
#   true   The fixes are installed as configured below.
#   false  The DLL stays loaded and the console command still answers, but
#          nothing is patched. Use this to rule the plugin out while chasing a
#          problem, without moving the DLL out of the folder.
# Default: true
enabled = true

# =============================================================================
[fixes]
# =============================================================================
# Missile burst skipped when the missile dies on a wall.
#   Site:     RVA 0045FB6D and RVA 0045FBB5, in Missiles.txt pSrvHitFunc 29.
#   Bug:      pSrvHitFunc 29 is the burst used by Frozen Orb and everything
#             built on it: at the end of the missile's life it spawns a ring of
#             the missile's HitSubMissile. It is a hit function, so the engine
#             calls it on every collision, and it decides whether this is the
#             end of life by checking how many frames the missile has left. A
#             missile stopped by a wall still has frames left, so the test
#             refuses and the ring is never spawned. The missile dies silently
#             against the wall and every sub-missile it should have released,
#             with all the damage they carry, is lost. Only a missile that
#             lives out its full Range ever bursts.
#   Fix:      The burst is refused only when the missile still has frames left
#             AND the hit is against a unit, which is the case the frame test
#             was really there to catch. The engine already tells the hit
#             function which of the two it is. Hitting a monster in mid-flight
#             behaves as before, running out of Range behaves as before, dying
#             on a wall now bursts.
#   Scope:    This is a pSrvHitFunc 29 change, not a Frozen Orb change. Every
#             missile in your Missiles.txt using pSrvHitFunc 29 starts bursting
#             on walls. Server side only; pCltHitFunc is untouched.
#   Cost:     One compare per pSrvHitFunc 29 collision.
# Default: true
frozen_orb_burst_on_wall = true
# A burst missile is deleted when its to-hit roll misses.
#   Site:     RVA 0045FBB9 in Missiles.txt pSrvHitFunc 29, or the same
#             instruction inside the frozen_orb_burst_on_wall relay when that
#             fix is on. Works with that fix on or off.
#   Bug:      For a Missiles.txt row with ToHit, the engine rolls to hit before
#             anything else. A miss never looks at CollideKill: the missile is
#             deleted on the spot, the way a missed arrow is, unless the row has
#             AlwaysExplode and the hit function, run once for the miss, asks to
#             keep it. pSrvHitFunc 29 answers a mid-flight touch on a unit with
#             "no burst yet, keep flying", but in a form only a hit reads as
#             keep. So a pSrvHitFunc 29 missile that misses dies there, with no
#             damage and no burst, while the same missile hitting flies on and
#             bursts at the end of its Range. Frozen Orb never rolls to hit,
#             which is why vanilla never showed it.
#   Fix:      That mid-flight answer also carries the engine's own "keep me
#             after a miss" flag, which a hit never reads. A hit behaves exactly
#             as before. A miss now flies on and bursts where it would have.
#   Scope:    Every Missiles.txt row with pSrvHitFunc 29 and ToHit. A row
#             without ToHit never misses and is unaffected. Server side only.
#   Note:     The miss still stamps LastCollide. With LastCollide set, the unit
#             it missed is not rolled again by the same missile. Without it,
#             the missile rolls again every frame it overlaps that unit, the
#             same way a hit without LastCollide strikes again.
#   Cost:     Nothing per hit: one value in the function changes.
# Default: true
burst_missile_survives_miss = true
)toml";

struct Slice {
	const char* begin;
	const char* end;
};

constexpr auto IsBlank(char value) noexcept -> bool {
	return value == ' ' || value == '\t' || value == '\r';
}

constexpr auto Trim(Slice slice) noexcept -> Slice {
	while (slice.begin < slice.end && IsBlank(*slice.begin)) {
		++slice.begin;
	}
	while (slice.end > slice.begin && IsBlank(slice.end[-1])) {
		--slice.end;
	}
	return slice;
}

auto SliceEquals(Slice slice, const char* literal) noexcept -> bool {
	const char* text = literal;
	const char* cursor = slice.begin;
	while (cursor < slice.end && *text != '\0') {
		if (*cursor != *text) {
			return false;
		}
		++cursor;
		++text;
	}
	return cursor == slice.end && *text == '\0';
}

// Reads section.key as a boolean. Returns true only when the key was present
// and spelled true or false; `value` is left untouched otherwise.
auto ReadConfigBool(const char* toml, const char* section, const char* key, bool& value) noexcept -> bool {
	if (toml == nullptr) {
		return false;
	}

	std::array<char, 64> currentSection {};
	bool                 found = false;

	for (const char* line = toml; *line != '\0';) {
		const char* lineEnd = line;
		while (*lineEnd != '\0' && *lineEnd != '\n') {
			++lineEnd;
		}

		const Slice trimmed = Trim({ line, lineEnd });

		if (trimmed.begin < trimmed.end && *trimmed.begin != '#') {
			if (*trimmed.begin == '[') {
				const char* close = trimmed.begin;
				while (close < trimmed.end && *close != ']') {
					++close;
				}

				const Slice  name = Trim({ trimmed.begin + 1, close });
				std::size_t  used = 0;
				for (const char* cursor = name.begin; cursor < name.end && used + 1 < currentSection.size(); ++cursor) {
					currentSection[used++] = *cursor;
				}
				currentSection[used] = '\0';
			} else {
				const char* equals = trimmed.begin;
				while (equals < trimmed.end && *equals != '=') {
					++equals;
				}

				if (equals < trimmed.end) {
					const Slice name = Trim({ trimmed.begin, equals });
					Slice       raw  = Trim({ equals + 1, trimmed.end });

					for (const char* cursor = raw.begin; cursor < raw.end; ++cursor) {
						if (*cursor == '#') {
							raw.end = cursor;
							break;
						}
					}
					raw = Trim(raw);

					if (std::strcmp(currentSection.data(), section) == 0 && SliceEquals(name, key)) {
						if (SliceEquals(raw, "true")) {
							value = true;
							found = true;
						} else if (SliceEquals(raw, "false")) {
							value = false;
							found = true;
						}
					}
				}
			}
		}

		line = (*lineEnd == '\0') ? lineEnd : lineEnd + 1;
	}

	return found;
}

// Reads section.key as a whole number inside [0, high]. Anything that is not a
// plain run of digits, and anything out of range, leaves `value` untouched, so
// a typo falls back to the default rather than to zero.
[[maybe_unused]] auto ReadConfigInt(
	const char*    toml,
	const char*    section,
	const char*    key,
	std::uint32_t  high,
	std::uint32_t& value,
	bool&          sawBadValue) noexcept -> bool {
	if (toml == nullptr) {
		return false;
	}

	std::array<char, 64> currentSection {};
	bool                 found = false;

	for (const char* line = toml; *line != '\0';) {
		const char* lineEnd = line;
		while (*lineEnd != '\0' && *lineEnd != '\n') {
			++lineEnd;
		}

		const Slice trimmed = Trim({ line, lineEnd });

		if (trimmed.begin < trimmed.end && *trimmed.begin != '#') {
			if (*trimmed.begin == '[') {
				const char* close = trimmed.begin;
				while (close < trimmed.end && *close != ']') {
					++close;
				}

				const Slice  name = Trim({ trimmed.begin + 1, close });
				std::size_t  used = 0;
				for (const char* cursor = name.begin; cursor < name.end && used + 1 < currentSection.size(); ++cursor) {
					currentSection[used++] = *cursor;
				}
				currentSection[used] = '\0';
			} else {
				const char* equals = trimmed.begin;
				while (equals < trimmed.end && *equals != '=') {
					++equals;
				}

				if (equals < trimmed.end) {
					const Slice name = Trim({ trimmed.begin, equals });
					Slice       raw  = Trim({ equals + 1, trimmed.end });

					for (const char* cursor = raw.begin; cursor < raw.end; ++cursor) {
						if (*cursor == '#') {
							raw.end = cursor;
							break;
						}
					}
					raw = Trim(raw);

					if (std::strcmp(currentSection.data(), section) == 0 && SliceEquals(name, key)) {
						if (raw.begin >= raw.end) {
							sawBadValue = true;
						} else {
							std::uint64_t parsed  = 0;
							bool          isDigits = true;
							for (const char* cursor = raw.begin; cursor < raw.end; ++cursor) {
								if (*cursor < '0' || *cursor > '9') {
									isDigits = false;
									break;
								}
								parsed = parsed * 10 + static_cast<std::uint64_t>(*cursor - '0');
								if (parsed > high) {
									isDigits = false;
									break;
								}
							}

							if (isDigits) {
								value       = static_cast<std::uint32_t>(parsed);
								sawBadValue = false;
								found       = true;
							} else {
								sawBadValue = true;
							}
						}
					}
				}
			}
		}

		line = (*lineEnd == '\0') ? lineEnd : lineEnd + 1;
	}

	return found;
}

struct FixesConfig {
	bool pluginEnabled            { true };
	bool frozenOrbBurstOnWall     { true };
	bool burstMissileSurvivesMiss { true };
};

FixesConfig Config {};

// Only meaningful for the log line; the console command reads the guard states.
bool ConfigFileWasRead = false;

void LoadConfiguration(const D2RL::PluginContext* context) noexcept {
	if (context == nullptr) {
		return;
	}

	if (!context->EnsureConfig(DefaultConfigToml)) {
		context->LogWarn(
			"Could not create or open celestialrayone.psrvhitfunc-29-fixes.toml. "
			"Running with defaults: every fix ON.");
		return;
	}

	std::array<char, 32768> buffer {};
	std::uint32_t           requiredSize = 0;

	if (!context->ReadConfig(buffer.data(), static_cast<std::uint32_t>(buffer.size() - 1), &requiredSize)) {
		context->LogWarn(
			"Could not read celestialrayone.psrvhitfunc-29-fixes.toml. "
			"Running with defaults: every fix ON.");
		return;
	}

	if (requiredSize >= buffer.size()) {
		D2RL::LogWarnF(
			context,
			"celestialrayone.psrvhitfunc-29-fixes.toml is %u bytes, larger than the %zu byte read buffer. "
			"Running with defaults: every fix ON.",
			requiredSize,
			buffer.size());
		return;
	}

	buffer[buffer.size() - 1] = '\0';
	ConfigFileWasRead         = true;

	(void)ReadConfigBool(buffer.data(), "psrvhitfunc-29-fixes", "enabled", Config.pluginEnabled);
	(void)ReadConfigBool(buffer.data(), "fixes", "frozen_orb_burst_on_wall", Config.frozenOrbBurstOnWall);
	(void)ReadConfigBool(buffer.data(), "fixes", "burst_missile_survives_miss", Config.burstMissileSurvivesMiss);

	D2RL::LogInfoF(
		context,
		"config: enabled=%s, frozen_orb_burst_on_wall=%s, burst_missile_survives_miss=%s.",
		Config.pluginEnabled ? "true" : "false",
		Config.frozenOrbBurstOnWall ? "true" : "false",
		Config.burstMissileSurvivesMiss ? "true" : "false");
}

// ---------------------------------------------------------------------------
// Relay page: the frozen orb fix
// ---------------------------------------------------------------------------
//
// Both frozen orb sites sit in the middle of pSrvHitFunc 29, where the loader's
// inline hook cannot go: the bytes they replace include a branch or call, which
// does not relocate. Each site is rewritten in place to reach one page
// allocated within rel32 reach of the image:
//
//   +00h  frozen orb hit-unit spill stub, native code only
//   +40h  frozen orb burst gate stub, native code only
//
// The page is never freed. A thread may be jumping through it at any time, and
// the native stubs need nothing from this DLL.

constexpr std::size_t RelayPageBytes           = 4096;
constexpr std::size_t FrozenOrbSpillStubOffset = 0x00;
constexpr std::size_t FrozenOrbGateStubOffset  = 0x40;

static_assert(FrozenOrbSpillStubOffset + sizeof(FrozenOrbSpillStub) <= FrozenOrbGateStubOffset, "Frozen orb spill stub overlaps the frozen orb gate stub.");
static_assert(FrozenOrbGateStubOffset + sizeof(FrozenOrbGateStub) <= RelayPageBytes, "Frozen orb gate stub does not fit the relay page.");

constexpr const char* FrozenOrbFixLabel = "frozen orb burst-on-wall fix";

const D2RL::PluginContext* LoadedContext    = nullptr;
void*                      RelayPage        = nullptr;
bool                       RelayPageRefused = false;

bool FrozenOrbSpillSitePatched = false;
bool FrozenOrbGateSitePatched  = false;
bool GateStubKeepsMissedMissile = false;  // the gate stub refuses with BurstRefuseKeepValue
std::array<std::uint8_t, sizeof(FrozenOrbSpillTemplate)> FrozenOrbSpillSiteWritten {};
std::array<std::uint8_t, sizeof(FrozenOrbGateTemplate)>  FrozenOrbGateSiteWritten {};

auto WithinRel32(std::uintptr_t next, std::uintptr_t target) noexcept -> bool {
	const std::int64_t delta = static_cast<std::int64_t>(target) - static_cast<std::int64_t>(next);
	return delta >= INT32_MIN && delta <= INT32_MAX;
}

auto AllocateNear(std::uintptr_t hint, std::size_t size) noexcept -> void* {
	SYSTEM_INFO systemInfo {};
	GetSystemInfo(&systemInfo);
	const auto granularity = static_cast<std::uintptr_t>(systemInfo.dwAllocationGranularity);
	const auto aligned     = hint & ~(granularity - 1U);
	for (std::uintptr_t delta = granularity; delta < 0x70000000ULL; delta += granularity) {
		const auto candidate = aligned + delta;
		if (!WithinRel32(hint, candidate + size)) {
			break;
		}
		if (auto* memory = VirtualAlloc(reinterpret_cast<void*>(candidate), size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)) {
			return memory;
		}
	}
	return nullptr;
}

// Allocates and fills the page once, then makes it execute-only. A failure is
// remembered so the second relay user does not retry and log twice.
auto PrepareRelayPage(const D2RL::PluginContext* context) noexcept -> bool {
	if (RelayPage != nullptr) {
		return true;
	}
	if (RelayPageRefused) {
		return false;
	}
	RelayPageRefused = true;

	const auto imageBase = static_cast<std::uintptr_t>(context->exeBase);
	void*      page      = AllocateNear(imageBase + FrozenOrbHitRva, RelayPageBytes);
	if (page == nullptr) {
		context->LogError("No relay page could be allocated within rel32 reach of the game image.");
		return false;
	}

	auto* bytes = static_cast<std::uint8_t*>(page);
	std::memset(bytes, 0xCC, RelayPageBytes);

	const std::uint64_t spillResume = imageBase + FrozenOrbSpillResumeRva;
	std::memcpy(bytes + FrozenOrbSpillStubOffset, FrozenOrbSpillStub, sizeof(FrozenOrbSpillStub));
	std::memcpy(bytes + FrozenOrbSpillStubOffset + FrozenOrbSpillResumeTargetOffset, &spillResume, sizeof(spillResume));

	const std::uint64_t burstRefuse = imageBase + FrozenOrbEpilogueRva;
	const std::uint64_t burstSpawn  = imageBase + FrozenOrbBurstRva;
	std::memcpy(bytes + FrozenOrbGateStubOffset, FrozenOrbGateStub, sizeof(FrozenOrbGateStub));
	std::memcpy(bytes + FrozenOrbGateStubOffset + FrozenOrbGateRefuseTargetOffset, &burstRefuse, sizeof(burstRefuse));
	std::memcpy(bytes + FrozenOrbGateStubOffset + FrozenOrbGateBurstTargetOffset, &burstSpawn, sizeof(burstSpawn));

	// Fix 2 owns the value the stub refuses with. With it off the stub refuses
	// with the stock 2.
	if (Config.pluginEnabled && Config.burstMissileSurvivesMiss) {
		bytes[FrozenOrbGateStubOffset + FrozenOrbGateStubRefuseImmOffset] = BurstRefuseKeepValue;
		GateStubKeepsMissedMissile = true;
	}

	DWORD previous = 0;
	if (!VirtualProtect(page, RelayPageBytes, PAGE_EXECUTE_READ, &previous)) {
		context->LogError("The relay page could not be made executable.");
		VirtualFree(page, 0, MEM_RELEASE);
		return false;
	}
	FlushInstructionCache(GetCurrentProcess(), page, RelayPageBytes);

	RelayPage        = page;
	RelayPageRefused = false;
	return true;
}

auto InstallFrozenOrbBurstOnWallFix(const D2RL::PluginContext* context) noexcept -> bool {
	if (context == nullptr) {
		return false;
	}

	if (!Config.pluginEnabled || !Config.frozenOrbBurstOnWall) {
		SetGuardState(Guard::FrozenOrbBurstOnWall, GuardState::DisabledByConfig);
		D2RL::LogInfoF(context, "%s not installed: turned off in the config file.", FrozenOrbFixLabel);
		return false;
	}

	if (!context->CheckExpectedBytes(FrozenOrbHitRva, FrozenOrbHitBody, ByteCount(FrozenOrbHitBody))) {
		SetGuardState(Guard::FrozenOrbBurstOnWall, GuardState::UnsupportedBuild);
		D2RL::LogErrorF(
			context,
			"%s NOT installed: the bytes at RVA 0045FB50 do not match the verified 3.3.93847 pSrvHitFunc 29. "
			"This build is not supported, or another patch already owns the site; nothing was patched.",
			FrozenOrbFixLabel);
		return false;
	}

	if (!PrepareRelayPage(context)) {
		SetGuardState(Guard::FrozenOrbBurstOnWall, GuardState::InstallFailed);
		D2RL::LogErrorF(context, "%s NOT installed: no relay page.", FrozenOrbFixLabel);
		return false;
	}

	const auto imageBase = static_cast<std::uintptr_t>(context->exeBase);
	const auto relayPage = reinterpret_cast<std::uintptr_t>(RelayPage);

	const auto spillNext   = imageBase + FrozenOrbSpillSiteRva + FrozenOrbSpillSiteRel32Offset + sizeof(std::int32_t);
	const auto spillTarget = relayPage + FrozenOrbSpillStubOffset;
	const auto gateNext    = imageBase + FrozenOrbGateSiteRva + FrozenOrbGateSiteRel32Offset + sizeof(std::int32_t);
	const auto gateTarget  = relayPage + FrozenOrbGateStubOffset;
	if (!WithinRel32(spillNext, spillTarget) || !WithinRel32(gateNext, gateTarget)) {
		SetGuardState(Guard::FrozenOrbBurstOnWall, GuardState::InstallFailed);
		D2RL::LogErrorF(context, "%s NOT installed: the relay page is out of rel32 reach of RVA 0045FB50.", FrozenOrbFixLabel);
		return false;
	}

	const auto spillDisplacement = static_cast<std::int32_t>(static_cast<std::int64_t>(spillTarget) - static_cast<std::int64_t>(spillNext));
	std::memcpy(FrozenOrbSpillSiteWritten.data(), FrozenOrbSpillTemplate, sizeof(FrozenOrbSpillTemplate));
	std::memcpy(FrozenOrbSpillSiteWritten.data() + FrozenOrbSpillSiteRel32Offset, &spillDisplacement, sizeof(spillDisplacement));

	const auto gateDisplacement = static_cast<std::int32_t>(static_cast<std::int64_t>(gateTarget) - static_cast<std::int64_t>(gateNext));
	std::memcpy(FrozenOrbGateSiteWritten.data(), FrozenOrbGateTemplate, sizeof(FrozenOrbGateTemplate));
	std::memcpy(FrozenOrbGateSiteWritten.data() + FrozenOrbGateSiteRel32Offset, &gateDisplacement, sizeof(gateDisplacement));

	// The spill goes in first because on its own it is inert: it writes the hit
	// unit into a stack slot the function overwrites later and nothing reads it
	// until the gate is in. The gate on its own would read an uninitialised
	// slot, so if it cannot be written the spill is taken back out again.
	if (!context->PatchBytes(
			FrozenOrbSpillSiteRva,
			FrozenOrbSpillOriginal,
			ByteCount(FrozenOrbSpillOriginal),
			FrozenOrbSpillSiteWritten.data(),
			static_cast<std::uint32_t>(FrozenOrbSpillSiteWritten.size()))) {
		SetGuardState(Guard::FrozenOrbBurstOnWall, GuardState::InstallFailed);
		D2RL::LogErrorF(context, "%s NOT installed: PatchBytes failed at RVA 0045FB6D.", FrozenOrbFixLabel);
		return false;
	}
	FrozenOrbSpillSitePatched = true;

	if (!context->PatchBytes(
			FrozenOrbGateSiteRva,
			FrozenOrbGateOriginal,
			ByteCount(FrozenOrbGateOriginal),
			FrozenOrbGateSiteWritten.data(),
			static_cast<std::uint32_t>(FrozenOrbGateSiteWritten.size()))) {
		if (context->PatchBytes(
				FrozenOrbSpillSiteRva,
				FrozenOrbSpillSiteWritten.data(),
				static_cast<std::uint32_t>(FrozenOrbSpillSiteWritten.size()),
				FrozenOrbSpillOriginal,
				ByteCount(FrozenOrbSpillOriginal))) {
			FrozenOrbSpillSitePatched = false;
		}
		SetGuardState(Guard::FrozenOrbBurstOnWall, GuardState::InstallFailed);
		D2RL::LogErrorF(context, "%s NOT installed: PatchBytes failed at RVA 0045FBB5.", FrozenOrbFixLabel);
		return false;
	}
	FrozenOrbGateSitePatched = true;

	SetGuardState(Guard::FrozenOrbBurstOnWall, GuardState::Installed);
	D2RL::LogInfoF(context, "%s installed at RVA 0045FB6D and RVA 0045FBB5.", FrozenOrbFixLabel);
	return true;
}

// ---------------------------------------------------------------------------
// Fix 2 install
// ---------------------------------------------------------------------------
//
// Runs after the burst-on-wall fix, because which of the two places holds the
// refuse depends on whether that fix went in.

constexpr const char* BurstMissFixLabel = "burst missile survives-miss fix";

bool BurstRefuseSitePatched = false;

auto InstallBurstMissileSurvivesMissFix(const D2RL::PluginContext* context) noexcept -> bool {
	if (context == nullptr) {
		return false;
	}

	if (!Config.pluginEnabled || !Config.burstMissileSurvivesMiss) {
		SetGuardState(Guard::BurstMissileSurvivesMiss, GuardState::DisabledByConfig);
		D2RL::LogInfoF(context, "%s not installed: turned off in the config file.", BurstMissFixLabel);
		return false;
	}

	// The burst-on-wall fix is live: the refuse runs in its relay stub, which
	// the relay page was built with.
	if (FrozenOrbGateSitePatched) {
		if (!GateStubKeepsMissedMissile) {
			SetGuardState(Guard::BurstMissileSurvivesMiss, GuardState::InstallFailed);
			D2RL::LogErrorF(context, "%s NOT installed: the burst-on-wall relay stub was built without it.", BurstMissFixLabel);
			return false;
		}
		SetGuardState(Guard::BurstMissileSurvivesMiss, GuardState::Installed);
		D2RL::LogInfoF(context, "%s installed in the burst-on-wall relay stub that replaces RVA 0045FBB5.", BurstMissFixLabel);
		return true;
	}

	// Otherwise the refuse is the stock instruction. The whole function is
	// verified first, the same proof the burst-on-wall fix uses.
	if (!context->CheckExpectedBytes(FrozenOrbHitRva, FrozenOrbHitBody, ByteCount(FrozenOrbHitBody))) {
		SetGuardState(Guard::BurstMissileSurvivesMiss, GuardState::UnsupportedBuild);
		D2RL::LogErrorF(
			context,
			"%s NOT installed: the bytes at RVA 0045FB50 do not match the verified 3.3.93847 pSrvHitFunc 29. "
			"This build is not supported, or another patch already owns the site; nothing was patched.",
			BurstMissFixLabel);
		return false;
	}

	if (!context->PatchBytes(
			BurstRefuseSiteRva,
			BurstRefuseStock,
			ByteCount(BurstRefuseStock),
			BurstRefuseKeep,
			ByteCount(BurstRefuseKeep))) {
		SetGuardState(Guard::BurstMissileSurvivesMiss, GuardState::InstallFailed);
		D2RL::LogErrorF(context, "%s NOT installed: PatchBytes failed at RVA 0045FBB9.", BurstMissFixLabel);
		return false;
	}
	BurstRefuseSitePatched = true;

	SetGuardState(Guard::BurstMissileSurvivesMiss, GuardState::Installed);
	D2RL::LogInfoF(context, "%s installed at RVA 0045FBB9.", BurstMissFixLabel);
	return true;
}

// On unload every site gets its original bytes back, gate first. The page
// itself is kept: a thread may still be inside a stub.
void WithdrawBurstRefuseSite() noexcept {
	const D2RL::PluginContext* context = LoadedContext;
	if (context == nullptr || !BurstRefuseSitePatched) {
		return;
	}

	if (context->PatchBytes(
			BurstRefuseSiteRva,
			BurstRefuseKeep,
			ByteCount(BurstRefuseKeep),
			BurstRefuseStock,
			ByteCount(BurstRefuseStock))) {
		BurstRefuseSitePatched = false;
	}
}

void WithdrawRelaySites() noexcept {
	const D2RL::PluginContext* context = LoadedContext;
	if (context == nullptr || RelayPage == nullptr) {
		return;
	}

	// Gate first: with only the spill left the function is stock behaviour plus
	// one dead store, which is the safe order to be caught halfway through.
	if (FrozenOrbGateSitePatched
		&& context->PatchBytes(
			FrozenOrbGateSiteRva,
			FrozenOrbGateSiteWritten.data(),
			static_cast<std::uint32_t>(FrozenOrbGateSiteWritten.size()),
			FrozenOrbGateOriginal,
			ByteCount(FrozenOrbGateOriginal))) {
		FrozenOrbGateSitePatched = false;
	}

	if (FrozenOrbSpillSitePatched
		&& context->PatchBytes(
			FrozenOrbSpillSiteRva,
			FrozenOrbSpillSiteWritten.data(),
			static_cast<std::uint32_t>(FrozenOrbSpillSiteWritten.size()),
			FrozenOrbSpillOriginal,
			ByteCount(FrozenOrbSpillOriginal))) {
		FrozenOrbSpillSitePatched = false;
	}
}


// ---------------------------------------------------------------------------
// Console command
// ---------------------------------------------------------------------------

void ReportGuard(
	const D2RL::PluginContext* context,
	Guard                      guard,
	const char*                label) noexcept {
	char message[256] {};

	switch (GetGuardState(guard)) {
	case GuardState::Installed:
		std::snprintf(message, sizeof(message), "%s: active.", label);
		context->WriteConsoleMessage(message);
		break;

	case GuardState::DisabledByConfig:
		std::snprintf(
			message,
			sizeof(message),
			"%s: OFF because the config file turns it off. The game build is fine.",
			label);
		context->WriteConsoleWarning(message);
		break;

	case GuardState::UnsupportedBuild:
		std::snprintf(
			message,
			sizeof(message),
			"%s: NOT ACTIVE. This game build is not recognised and nothing was patched.",
			label);
		context->WriteConsoleError(message);
		break;

	case GuardState::InstallFailed:
		std::snprintf(
			message,
			sizeof(message),
			"%s: NOT ACTIVE. The bytes matched but it could not be installed. See the plugin log.",
			label);
		context->WriteConsoleError(message);
		break;

	case GuardState::NotAttempted:
	default:
		std::snprintf(message, sizeof(message), "%s: NOT ACTIVE. Installation was never attempted.", label);
		context->WriteConsoleError(message);
		break;
	}
}

auto FixesCommand(
	D2R::Game::Client*                  client,
	const D2RL::ConsoleCommandContext*  command,
	void*                               userData) noexcept -> D2RL::ConsoleCommandResult {
	(void)client;
	(void)userData;

	if (command == nullptr || command->plugin == nullptr) {
		return D2RL::ConsoleCommandResult::Failed;
	}

	const D2RL::PluginContext* context = command->plugin;

	char header[160] {};
	std::snprintf(
		header,
		sizeof(header),
		"psrvhitfunc-29-fixes: config %s, plugin %s.",
		ConfigFileWasRead ? "loaded" : "NOT READ (defaults in use)",
		Config.pluginEnabled ? "enabled" : "DISABLED in config");
	context->WriteConsoleMessage(header);

	ReportGuard(context, Guard::FrozenOrbBurstOnWall, FrozenOrbFixLabel);
	ReportGuard(context, Guard::BurstMissileSurvivesMiss, BurstMissFixLabel);

	return D2RL::ConsoleCommandResult::Handled;
}

constexpr D2RL::PluginInfo FixesInfo {
	.infoSize    = D2RL::PluginInfoSize,
	.abiVersion  = D2RL_PLUGIN_ABI_VERSION,
	.id          = "celestialrayone.psrvhitfunc-29-fixes",
	.name        = "pSrvHitFunc 29 Fixes",
	.version     = "1.0.0",
	.author      = "CelestialRayOne",
	.description = "Missiles.txt pSrvHitFunc 29 fixes: missiles burst on walls and keep flying after a miss.",
	.flags       = D2RL::PluginFlags::Client | D2RL::PluginFlags::NativeHooks,
};

static_assert(D2RL::HasValidPluginRole(FixesInfo.flags), "Exactly one execution role must be set.");
static_assert(D2RL::HasOnlyKnownPluginFlags(FixesInfo.flags), "Unknown plugin flag set.");
static_assert(D2RL::HasFlag(FixesInfo.flags, D2RL::PluginFlags::NativeHooks), "The relay stubs are native code the game jumps into.");

} // namespace

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* {
	return &FixesInfo;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
	if (context == nullptr) {
		return false;
	}

	LoadedContext = context;

	// Registered before anything is installed on purpose. Returning false from
	// this function unloads the DLL and takes the console command with it,
	// which would leave a user on an unsupported build with no in-game signal
	// at all. This plugin would rather stay loaded and be able to say that it
	// did nothing.
	if (!context->RegisterConsoleCommand(
			"psrvhitfunc-29-fixes",
			FixesCommand,
			"Report which pSrvHitFunc 29 fixes are active.")) {
		context->LogWarn("The psrvhitfunc-29-fixes console command was not registered.");
	}

	if (const char* build = D2RL::GetBuildVersion(context); build != nullptr) {
		D2RL::LogInfoF(context, "psrvhitfunc-29-fixes loading against build %s.", build);
	}

	LoadConfiguration(context);

	if (!Config.pluginEnabled) {
		SetGuardState(Guard::FrozenOrbBurstOnWall, GuardState::DisabledByConfig);
		SetGuardState(Guard::BurstMissileSurvivesMiss, GuardState::DisabledByConfig);
		context->LogWarn("psrvhitfunc-29-fixes is disabled in its config file. Nothing was installed.");
		return true;
	}

	// Each fix is installed independently. One refusing, whether because the
	// config turned it off or because its site did not verify, must never take
	// the other down with it.
	unsigned installed = 0;

	if (InstallFrozenOrbBurstOnWallFix(context)) {
		++installed;
	}

	// After the burst-on-wall fix: it decides where the refuse lives.
	if (InstallBurstMissileSurvivesMissFix(context)) {
		++installed;
	}

	if (installed == 0) {
		context->LogError("psrvhitfunc-29-fixes loaded with NO fixes active.");
		return true;
	}

	D2RL::LogInfoF(context, "psrvhitfunc-29-fixes loaded with %u of %zu fixes active.", installed, GuardCount);
	return true;
}

// Every site this plugin writes can be withdrawn, so on unload they all get
// their original bytes back. The relay page itself is kept.
D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
	WithdrawRelaySites();
	WithdrawBurstRefuseSite();
}
