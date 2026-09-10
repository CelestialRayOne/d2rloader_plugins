// ---------------------------------------------------------------------------
// engine-stability
//
// Crash guards only. This plugin changes no gameplay, no balance and no
// presentation. Everything it does is either a no-op or the difference between
// a hard crash and a normal frame.
//
// Target: Diablo II: Resurrected 3.3.93847 (also 3.2.92777, Steam 3.3.93787 --
// those builds share an executable identity for patching purposes).
//
// Safety model: every address below is an RVA against image base 0x140000000,
// and nothing is installed unless the bytes already at that RVA match byte for
// byte. On any build the plugin does not recognise it installs nothing, logs
// loudly and stays loaded so the console command can explain why. An
// unrecognised build is therefore a no-op, never a mis-patch.
//
// Configuration: every guard has an on/off switch in
// <scope>\d2rloader\config\celestialrayone.engine-stability.toml, all true by
// default. The file is created with documented defaults on first run. The
// config is read once at load, so edits need a game restart. A guard that the
// config turned off and a guard the build refused are reported differently by
// the console command -- they are not the same thing and must never look it.
// ---------------------------------------------------------------------------

#include <D2RLPlugin/api.h>

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

// Number of fixed-size elements in a [begin, end) pair handed to a command
// builder. Deliberately total: a null or inverted range counts as empty, which
// makes every guard below fall through to the original function rather than
// invent a reason to block.
constexpr auto ElementCount(const void* begin, const void* end, std::size_t stride) noexcept -> std::uint64_t {
	if (begin == nullptr || end == nullptr || stride == 0) {
		return 0;
	}

	const auto first = reinterpret_cast<std::uintptr_t>(begin);
	const auto last  = reinterpret_cast<std::uintptr_t>(end);
	if (last <= first) {
		return 0;
	}

	return static_cast<std::uint64_t>(last - first) / stride;
}

// ---------------------------------------------------------------------------
// Guard identity
// ---------------------------------------------------------------------------

enum class Guard : std::size_t {
	DeadUnitLookup,
	TransmuteCommand,
	DisplaceCommand,
	QuickDisplaceCommand,
	LifeDrainWhileDead,
	AutomapBlobLength,
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

// Diagnostics only. The hot paths touch nothing; a counter is only written on
// the rare guarded path.
std::array<std::atomic<std::uint64_t>, GuardCount> SuppressedEvents {};

void SetGuardState(Guard guard, GuardState state) noexcept {
	GuardStates[Index(guard)].store(state, std::memory_order_release);
}

auto GetGuardState(Guard guard) noexcept -> GuardState {
	return GuardStates[Index(guard)].load(std::memory_order_acquire);
}

void CountSuppressed(Guard guard) noexcept {
	SuppressedEvents[Index(guard)].fetch_add(1, std::memory_order_relaxed);
}

auto SuppressedCount(Guard guard) noexcept -> std::uint64_t {
	return SuppressedEvents[Index(guard)].load(std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------
// Guard 1: client unit-by-id hash lookup, tombstoned unit id
// ---------------------------------------------------------------------------
//
// sub_14009F270 is the shared bottom of the client's unit-by-id lookup. Its
// public wrapper sub_14009A5D0(unitId, unitType) resolves the per-type bucket
// array out of the client unit hash table at 0x2A23910 and forwards here:
//
//     sub_14009F270(&hashTable[128 * unitType], unitId & 0x7F, unitId, unitType)
//
// The function walks a chain and compares [node+8] against the requested id and
// [node] against the requested type. It never validates the id it was handed.
//
// The failure it has to be guarded against: a unit that dies while something it
// spawned is still alive. Freeing a unit stamps a tombstone on the block (unit
// id -1) and returns it to the pool, so every later resolution of that owner
// arrives here with id -1. That masks to bucket 0x7F and the walk follows
// whatever that slot holds, which is where the dangling chain node is.
//
// The guard: an id of -1 is never a live unit, so answer "not found" without
// walking. Returning null is this function's own not-found result -- it already
// ends in xor eax,eax / retn when the chain runs out -- so every one of its
// call sites already handles it. That is what makes this safe to do in the
// callee rather than in any particular caller, which matters because the
// captured 2.4 dumps arrived through more than one caller.
//
// Verified body at RVA 0x09F270 (41 bytes):
//
//   0009F270  48 63 C2                 movsxd rax, edx
//   0009F273  48 8B 04 C1              mov    rax, [rcx+rax*8]
//   0009F277  48 85 C0                 test   rax, rax
//   0009F27A  74 1A                    jz     0009F296
//   0009F27C  44 39 40 08              cmp    [rax+8], r8d
//   0009F280  75 05                    jnz    0009F287
//   0009F282  44 39 08                 cmp    [rax], r9d
//   0009F285  74 11                    jz     0009F298
//   0009F287  48 8B 88 58 01 00 00     mov    rcx, [rax+158h]
//   0009F28E  48 8B C1                 mov    rax, rcx
//   0009F291  48 85 C9                 test   rcx, rcx
//   0009F294  EB E4                    jmp    0009F27A
//   0009F296  33 C0                    xor    eax, eax
//   0009F298  C3                       retn
//
// The hook displaces the first 7 bytes (two instructions). Both are position
// independent -- no rip-relative operand, no branch -- so they relocate into
// the trampoline cleanly. No branch inside the function targets any address in
// 0x09F270..0x09F276, so nothing can land in the middle of the displaced
// region.

constexpr std::uint64_t UnitHashLookupRva = 0x0009F270ULL;

// Checked in full before anything is installed. Verifying the whole body, not
// just the bytes being displaced, means the plugin also refuses on a build
// where the not-found tail this guard relies on has changed.
constexpr std::uint8_t UnitHashLookupBody[] {
	0x48, 0x63, 0xC2,
	0x48, 0x8B, 0x04, 0xC1,
	0x48, 0x85, 0xC0,
	0x74, 0x1A,
	0x44, 0x39, 0x40, 0x08,
	0x75, 0x05,
	0x44, 0x39, 0x08,
	0x74, 0x11,
	0x48, 0x8B, 0x88, 0x58, 0x01, 0x00, 0x00,
	0x48, 0x8B, 0xC1,
	0x48, 0x85, 0xC9,
	0xEB, 0xE4,
	0x33, 0xC0,
	0xC3,
};

// The bytes the inline hook displaces: movsxd rax, edx / mov rax, [rcx+rax*8].
constexpr std::uint8_t UnitHashLookupPrologue[] {
	0x48, 0x63, 0xC2,
	0x48, 0x8B, 0x04, 0xC1,
};

constexpr std::int32_t TombstoneUnitId = -1;

static_assert(sizeof(UnitHashLookupBody) == 41, "Verified body length changed.");
static_assert(sizeof(UnitHashLookupPrologue) == 7, "Verified prologue length changed.");
static_assert(sizeof(UnitHashLookupPrologue) >= 5, "An inline hook needs at least 5 displaced bytes for a rel32 jump.");
static_assert(IsLeadingBytesOf(UnitHashLookupPrologue, UnitHashLookupBody), "The displaced bytes must be the leading bytes of the verified body.");

using UnitHashLookupFn = void*(__fastcall*)(
	void*         bucketArray,
	std::int32_t  bucketIndex,
	std::int32_t  unitId,
	std::int32_t  unitType) noexcept;

UnitHashLookupFn OriginalUnitHashLookup = nullptr;

auto __fastcall HookUnitHashLookup(
	void*        bucketArray,
	std::int32_t bucketIndex,
	std::int32_t unitId,
	std::int32_t unitType) noexcept -> void* {
	if (unitId == TombstoneUnitId) {
		CountSuppressed(Guard::DeadUnitLookup);
		return nullptr;
	}

	const UnitHashLookupFn original = OriginalUnitHashLookup;
	return original != nullptr ? original(bucketArray, bucketIndex, unitId, unitType) : nullptr;
}

// ---------------------------------------------------------------------------
// Guards 2-4: oversized client gameplay commands
// ---------------------------------------------------------------------------
//
// Every client gameplay command goes through the same sender, sub_1400EE2A0,
// reached from the builders through the import thunk SendClientGameplayPacket.
// The sender takes (buffer, length) and does exactly this with it:
//
//     memcmp(&LastCommandSent, buffer, length)    // 512 byte global at 2A41380
//     memcpy(&LastCommandSent, buffer, length)    // followed at +200h by a dword
//     memcpy(localBuffer, buffer, length)         // BYTE localBuffer[512]
//     sub_1400EE360(localBuffer, length)          // memcpy into another BYTE[512]
//
// Not one of those four is bounds checked. Any command longer than 512 bytes
// runs off the end of a global, off the end of two stack frames, or all three.
// Nothing faults at the point of damage, which is why this crash lands in
// unrelated code, most often while the process is tearing down.
//
// Three builders can produce a command that long, and all three do it the same
// way: a fixed header, then one fixed-size record per item, with the item count
// coming from a [begin, end) pointer pair the caller assembled. The item count
// also travels in the command as a single byte, so past 256 items a builder
// additionally overruns its own stack frame while filling it in.
//
// The guard for all three is the same: compute the length the builder is about
// to produce, and if it will not fit in the 512 byte buffers, do not call the
// builder at all. The command is simply not sent. Cancelling at the builder is
// what makes this correct rather than a containment hack: by the time the
// sender sees the command the builder has already filled its own frame, and for
// large enough item counts that frame is already smashed.
//
// Refusing is also the only representable answer. The count field in each of
// these commands is one byte and the server validates
// len == offsetof(items) + (nPlace + 1) * sizeof(items[0]), so a command
// carrying more items than the byte can express does not exist in the protocol.
// There is nothing to split or clamp into.

// The size of every buffer the sender copies a command into.
constexpr std::uint64_t CommandBufferBytes = 512;

// Largest item count whose command still fits. Written as the builder's own
// arithmetic so it stays checkable against the disassembly.
constexpr auto MaxItemsThatFit(std::uint64_t headerBytes, std::uint64_t bytesPerItem) noexcept -> std::uint64_t {
	return (CommandBufferBytes - headerBytes) / bytesPerItem;
}

// --- Guard 2: CCMD_TRANSMUTE (32), sub_1400ED120 ---------------------------
//
// Reached from the Horadric cube panel: sub_1402CD720 walks the player's items,
// keeps the ones on page 3, finds the cube itself by item code 'box', and hands
// the resulting id array to this builder.
//
//   000ED120  48 81 EC 48 05 00 00     sub    rsp, 548h
//   000ED127  48 8B 05 9A E1 8D 02     mov    rax, cs:__security_cookie
//   ...
//   000ED151  C6 44 24 20 20           mov    byte ptr [rsp+20h], 20h   ; id 32
//   000ED166  49 2B C1                 sub    rax, r9                   ; end - begin
//   000ED16D  48 C1 F8 03              sar    rax, 3                    ; / 8
//   000ED171  FE C8                    dec    al                        ; count - 1
//   ...
//   000ED1BA  8D 14 85 0D 00 00 00     lea    edx, [rax*4+0Dh]
//   000ED1C1  03 D0                    add    edx, eax                  ; 5*(n-1)+13
//
// So the command is an 8 byte header plus 5 bytes per item, and the item array
// is a [begin, end) pair of 8 byte entries in r9 and [rsp+28h].
//
// The hook displaces the first 7 bytes. That single instruction is position
// independent; the next one is rip-relative and must not be displaced. Neither
// branch in the function (74 36 -> 000ED1B2, 75 D3 -> 000ED180) targets any
// address inside 000ED120..000ED126.

constexpr std::uint64_t TransmuteCommandRva      = 0x000ED120ULL;
constexpr std::uint64_t TransmuteHeaderBytes     = 8;
constexpr std::uint64_t TransmuteBytesPerItem    = 5;
constexpr std::uint64_t TransmuteItemStride      = 8;
constexpr std::uint64_t MaxTransmuteItems        = MaxItemsThatFit(TransmuteHeaderBytes, TransmuteBytesPerItem);

static_assert(MaxTransmuteItems == 100, "Transmute item ceiling changed; re-derive it before shipping.");

constexpr std::uint8_t TransmuteCommandBody[] {
	0x48, 0x81, 0xEC, 0x48, 0x05, 0x00, 0x00,
	0x48, 0x8B, 0x05, 0x9A, 0xE1, 0x8D, 0x02,
	0x48, 0x33, 0xC4,
	0x48, 0x89, 0x84, 0x24, 0x30, 0x05, 0x00, 0x00,
	0x4C, 0x8B, 0x94, 0x24, 0x70, 0x05, 0x00, 0x00,
	0x41, 0x8B, 0xC0,
	0xC1, 0xE8, 0x10,
	0x41, 0x80, 0xE0, 0x0F,
	0xC0, 0xE0, 0x04,
	0x41, 0x02, 0xC0,
	0xC6, 0x44, 0x24, 0x20, 0x20,
	0x88, 0x44, 0x24, 0x26,
	0x4C, 0x8D, 0x44, 0x24, 0x28,
	0x49, 0x8B, 0xC2,
	0x89, 0x4C, 0x24, 0x21,
	0x49, 0x2B, 0xC1,
	0x88, 0x54, 0x24, 0x25,
	0x48, 0xC1, 0xF8, 0x03,
	0xFE, 0xC8,
	0x88, 0x44, 0x24, 0x27,
	0x4D, 0x3B, 0xCA,
	0x74, 0x36,
	0x0F, 0x1F, 0x40, 0x00,
	0x49, 0x8B, 0x01,
	0x49, 0x83, 0xC1, 0x08,
	0x41, 0x89, 0x00,
	0x4D, 0x8D, 0x40, 0x05,
	0x48, 0x8B, 0xD0,
	0x48, 0x8B, 0xC8,
	0x48, 0xC1, 0xEA, 0x30,
	0x48, 0xC1, 0xE9, 0x20,
	0xC0, 0xE2, 0x04,
	0x80, 0xE1, 0x0F,
	0x02, 0xD1,
	0x41, 0x88, 0x50, 0xFF,
	0x4D, 0x3B, 0xCA,
	0x75, 0xD3,
	0x0F, 0xB6, 0x44, 0x24, 0x27,
	0x0F, 0xB6, 0xC0,
	0x48, 0x8D, 0x4C, 0x24, 0x20,
	0x8D, 0x14, 0x85, 0x0D, 0x00, 0x00, 0x00,
	0x03, 0xD0,
	0xE8, 0x3E, 0xD6, 0xD3, 0x03,
	0x48, 0x8B, 0x8C, 0x24, 0x30, 0x05, 0x00, 0x00,
	0x48, 0x33, 0xCC,
	0xE8, 0x78, 0x3F, 0x1E, 0x01,
	0x48, 0x81, 0xC4, 0x48, 0x05, 0x00, 0x00,
	0xC3,
};

// sub rsp, 548h
constexpr std::uint8_t TransmuteCommandPrologue[] {
	0x48, 0x81, 0xEC, 0x48, 0x05, 0x00, 0x00,
};

static_assert(sizeof(TransmuteCommandBody) == 0xC0, "Verified transmute builder length changed.");
static_assert(sizeof(TransmuteCommandPrologue) >= 5, "An inline hook needs at least 5 displaced bytes for a rel32 jump.");
static_assert(IsLeadingBytesOf(TransmuteCommandPrologue, TransmuteCommandBody), "The displaced bytes must be the leading bytes of the verified body.");

// Registers: rcx, dl, r8d, r9 = item array begin, [rsp+28h] = item array end.
// Every argument the guard does not need is carried as a full 64 bit value so
// the forwarded call is bit-identical to the one the caller made.
using TransmuteCommandFn = std::int64_t(__fastcall*)(
	std::uint64_t unitId,
	std::uint64_t page,
	std::uint64_t position,
	const void*   itemsBegin,
	const void*   itemsEnd) noexcept;

TransmuteCommandFn OriginalTransmuteCommand = nullptr;

auto __fastcall HookTransmuteCommand(
	std::uint64_t unitId,
	std::uint64_t page,
	std::uint64_t position,
	const void*   itemsBegin,
	const void*   itemsEnd) noexcept -> std::int64_t {
	if (ElementCount(itemsBegin, itemsEnd, TransmuteItemStride) > MaxTransmuteItems) {
		CountSuppressed(Guard::TransmuteCommand);
		return 0;
	}

	const TransmuteCommandFn original = OriginalTransmuteCommand;
	return original != nullptr ? original(unitId, page, position, itemsBegin, itemsEnd) : 0;
}

// --- Guard 3: CCMD_DISPLACEITEMS (34), sub_1400EC580 -----------------------
//
//   000EC580  40 53                    push   rbx
//   000EC582  48 81 EC 50 07 00 00     sub    rsp, 750h
//   000EC589  48 8B 05 38 ED 8D 02     mov    rax, cs:__security_cookie
//   ...
//   000EC59B  49 8B C1                 mov    rax, r9                   ; end
//   000EC59E  C6 44 24 30 22           mov    byte ptr [rsp+30h], 22h   ; id 34
//   000EC5A3  49 2B C0                 sub    rax, r8                   ; - begin
//   000EC5AA  48 C1 F8 04              sar    rax, 4                    ; / 16
//
// The record loop writes 7 bytes per item starting at [rsp+3Ah], so the header
// is 10 bytes, and the builder ends in 7*(n-1)+17. The item array is a
// [begin, end) pair of 16 byte entries in r8 and r9.
//
// The hook displaces the first 9 bytes (push rbx / sub rsp, 750h). Both are
// position independent; the rip-relative cookie load that follows must not be
// displaced. Nothing branches into 000EC580..000EC588.

constexpr std::uint64_t DisplaceCommandRva      = 0x000EC580ULL;
constexpr std::uint64_t DisplaceHeaderBytes     = 10;
constexpr std::uint64_t DisplaceBytesPerItem    = 7;
constexpr std::uint64_t DisplaceItemStride      = 16;
constexpr std::uint64_t MaxDisplaceItems        = MaxItemsThatFit(DisplaceHeaderBytes, DisplaceBytesPerItem);

static_assert(MaxDisplaceItems == 71, "Displace item ceiling changed; re-derive it before shipping.");

constexpr std::uint8_t DisplaceCommandBody[] {
	0x40, 0x53,
	0x48, 0x81, 0xEC, 0x50, 0x07, 0x00, 0x00,
	0x48, 0x8B, 0x05, 0x38, 0xED, 0x8D, 0x02,
	0x48, 0x33, 0xC4,
	0x48, 0x89, 0x84, 0x24, 0x40, 0x07, 0x00, 0x00,
	0x49, 0x8B, 0xC1,
	0xC6, 0x44, 0x24, 0x30, 0x22,
	0x49, 0x2B, 0xC0,
	0x89, 0x4C, 0x24, 0x31,
	0x48, 0xC1, 0xF8, 0x04,
};

// push rbx / sub rsp, 750h
constexpr std::uint8_t DisplaceCommandPrologue[] {
	0x40, 0x53,
	0x48, 0x81, 0xEC, 0x50, 0x07, 0x00, 0x00,
};

static_assert(sizeof(DisplaceCommandBody) == 46, "Verified displace builder length changed.");
static_assert(sizeof(DisplaceCommandPrologue) >= 5, "An inline hook needs at least 5 displaced bytes for a rel32 jump.");
static_assert(IsLeadingBytesOf(DisplaceCommandPrologue, DisplaceCommandBody), "The displaced bytes must be the leading bytes of the verified body.");

// Registers: rcx, rdx, r8 = item array begin, r9 = item array end.
using DisplaceCommandFn = std::int64_t(__fastcall*)(
	std::uint64_t unitId,
	std::uint64_t target,
	const void*   itemsBegin,
	const void*   itemsEnd) noexcept;

DisplaceCommandFn OriginalDisplaceCommand = nullptr;

auto __fastcall HookDisplaceCommand(
	std::uint64_t unitId,
	std::uint64_t target,
	const void*   itemsBegin,
	const void*   itemsEnd) noexcept -> std::int64_t {
	if (ElementCount(itemsBegin, itemsEnd, DisplaceItemStride) > MaxDisplaceItems) {
		CountSuppressed(Guard::DisplaceCommand);
		return 0;
	}

	const DisplaceCommandFn original = OriginalDisplaceCommand;
	return original != nullptr ? original(unitId, target, itemsBegin, itemsEnd) : 0;
}

// --- Guard 4: CCMD_QUICKDISPLACEITEMS (96), sub_1400ECB40 ------------------
//
//   000ECB40  40 53                    push   rbx
//   000ECB42  48 81 EC 60 07 00 00     sub    rsp, 760h
//   ...
//   000ECB62  4C 8D 54 24 49           lea    r10, [rsp+49h]            ; records
//   000ECB67  48 8B 9C 24 B8 07 00 00  mov    rbx, [rsp+7B8h]           ; arg 10
//   000ECB6F  4C 8B 9C 24 B0 07 00 00  mov    r11, [rsp+7B0h]           ; arg 9
//   000ECB9D  48 8B C3                 mov    rax, rbx                  ; end
//   000ECBA0  49 2B C3                 sub    rax, r11                  ; - begin
//   000ECBA3  C6 44 24 30 60           mov    byte ptr [rsp+30h], 60h   ; id 96
//   000ECBA8  48 C1 F8 04              sar    rax, 4                    ; / 16
//
// Records start at [rsp+49h] against a header at [rsp+30h], so the header is 25
// bytes and the builder ends in 7*(n-1)+32. The frame is push rbx + 760h, so
// [rsp+7B0h] and [rsp+7B8h] are the caller's argument slots 9 and 10, exactly
// where a ten argument __fastcall declaration puts them.
//
// This guard is precautionary: same overflow, same arithmetic, but this path
// has not been seen crashing. It gets its own switch for that reason.

constexpr std::uint64_t QuickDisplaceCommandRva   = 0x000ECB40ULL;
constexpr std::uint64_t QuickDisplaceHeaderBytes  = 25;
constexpr std::uint64_t QuickDisplaceBytesPerItem = 7;
constexpr std::uint64_t QuickDisplaceItemStride   = 16;
constexpr std::uint64_t MaxQuickDisplaceItems     = MaxItemsThatFit(QuickDisplaceHeaderBytes, QuickDisplaceBytesPerItem);

static_assert(MaxQuickDisplaceItems == 69, "Quick-displace item ceiling changed; re-derive it before shipping.");

constexpr std::uint8_t QuickDisplaceCommandBody[] {
	0x40, 0x53,
	0x48, 0x81, 0xEC, 0x60, 0x07, 0x00, 0x00,
	0x48, 0x8B, 0x05, 0x78, 0xE7, 0x8D, 0x02,
	0x48, 0x33, 0xC4,
	0x48, 0x89, 0x84, 0x24, 0x50, 0x07, 0x00, 0x00,
	0x8B, 0x84, 0x24, 0x98, 0x07, 0x00, 0x00,
	0x4C, 0x8D, 0x54, 0x24, 0x49,
	0x48, 0x8B, 0x9C, 0x24, 0xB8, 0x07, 0x00, 0x00,
	0x4C, 0x8B, 0x9C, 0x24, 0xB0, 0x07, 0x00, 0x00,
	0x89, 0x44, 0x24, 0x3D,
	0x0F, 0xB6, 0x84, 0x24, 0xA0, 0x07, 0x00, 0x00,
	0x88, 0x44, 0x24, 0x47,
	0x8B, 0x84, 0x24, 0xA8, 0x07, 0x00, 0x00,
	0x89, 0x44, 0x24, 0x41,
	0x8B, 0x84, 0x24, 0x90, 0x07, 0x00, 0x00,
	0x89, 0x44, 0x24, 0x39,
	0x48, 0x8B, 0xC3,
	0x49, 0x2B, 0xC3,
	0xC6, 0x44, 0x24, 0x30, 0x60,
	0x48, 0xC1, 0xF8, 0x04,
	0xFE, 0xC8,
};

// push rbx / sub rsp, 760h
constexpr std::uint8_t QuickDisplaceCommandPrologue[] {
	0x40, 0x53,
	0x48, 0x81, 0xEC, 0x60, 0x07, 0x00, 0x00,
};

static_assert(sizeof(QuickDisplaceCommandBody) == 110, "Verified quick-displace builder length changed.");
static_assert(sizeof(QuickDisplaceCommandPrologue) >= 5, "An inline hook needs at least 5 displaced bytes for a rel32 jump.");
static_assert(IsLeadingBytesOf(QuickDisplaceCommandPrologue, QuickDisplaceCommandBody), "The displaced bytes must be the leading bytes of the verified body.");

// Ten arguments: rcx, rdx, r8, r9 and six stack slots, of which the last two
// are the item array begin and end.
using QuickDisplaceCommandFn = std::int64_t(__fastcall*)(
	std::uint64_t action,
	std::uint64_t stashId,
	std::uint64_t bodyItem,
	std::uint64_t bodyItemTargetPage,
	std::uint64_t bodyItemTargetPos,
	std::uint64_t gridItem,
	std::uint64_t gridItemPage,
	std::uint64_t gridItemPos,
	const void*   itemsBegin,
	const void*   itemsEnd) noexcept;

QuickDisplaceCommandFn OriginalQuickDisplaceCommand = nullptr;

auto __fastcall HookQuickDisplaceCommand(
	std::uint64_t action,
	std::uint64_t stashId,
	std::uint64_t bodyItem,
	std::uint64_t bodyItemTargetPage,
	std::uint64_t bodyItemTargetPos,
	std::uint64_t gridItem,
	std::uint64_t gridItemPage,
	std::uint64_t gridItemPos,
	const void*   itemsBegin,
	const void*   itemsEnd) noexcept -> std::int64_t {
	if (ElementCount(itemsBegin, itemsEnd, QuickDisplaceItemStride) > MaxQuickDisplaceItems) {
		CountSuppressed(Guard::QuickDisplaceCommand);
		return 0;
	}

	const QuickDisplaceCommandFn original = OriginalQuickDisplaceCommand;
	if (original == nullptr) {
		return 0;
	}

	return original(
		action,
		stashId,
		bodyItem,
		bodyItemTargetPage,
		bodyItemTargetPos,
		gridItem,
		gridItemPage,
		gridItemPos,
		itemsBegin,
		itemsEnd);
}

// ---------------------------------------------------------------------------
// Guard 5: life stolen per hit applied to a player that is already dead
// ---------------------------------------------------------------------------
//
// sub_140583A70 is the life-drain applier: the handler the engine runs to pay
// out "life stolen per hit". It is reached through the ItemEventFunc table at
// .rdata off_14238E5C0, slot 28, which is what both itemstatcost's
// itemeventfunc columns and skills.txt's auraeventfunc columns index. There is
// no call site to redirect, so the guard hooks the handler's own entry.
//
// Its arguments are the ones every event handler gets:
//
//     (pGame, eventId, pUnit, pOther, pDamage, packedStat)
//
// pUnit, in r8, is the leecher: the unit whose life is about to be raised, and
// the unit the drain stat is read from. Body:
//
//   00583A70  48 89 5C 24 08     mov  [rsp+8], rbx
//   00583A7F  49 8B D8           mov  rbx, r8            ; leecher
//   00583AA4  call GetStat(leecher, packedStat)          ; drain amount
//   00583AB2  call GetMaxLife(leecher)
//   00583AC3  call GetStat(leecher, 6)                   ; current life
//   00583ACE  C1 E7 08           shl  edi, 8             ; life is in 256ths
//   00583AEA  call SetStat(leecher, 6, clamped)
//   00583AFA  call ScheduleEvent(leecher, 151)
//
// What goes wrong without the guard: after UNITS_Die has committed life = 0,
// the player spends the whole death animation in modes DT and DD before the
// corpse-spawn pass finishes. A leeching missile that lands in that window
// runs this handler against the corpse, and the SetStat above puts life back.
// The accumulated life is then cashed in at the anim-complete transition and
// the player revives in place: the server has them dead, the client has them
// alive with positive life, and nothing but Save and Exit clears it. ESR
// reaches this constantly because its auras keep spawning leeching missiles
// after the caster dies.
//
// The guard: if the leecher is a player in a death animation, return 0 without
// touching the life stat and without scheduling the event. 0 is the handler's
// own "did nothing" return, used by all five of its early exits, so every
// caller already handles it. Living players, monsters and hirelings are not
// touched, because the gate requires unit type 0 and a death mode.
//
// The two struct offsets and both mode numbers were re-derived on this build
// rather than carried over:
//   +0x00 dwType      sub_14034B9D0 returns *(uint32*)unit
//   +0x04 dwTxtFileNo read at 0034A2D7 to index monstats
//   +0x08 dwUnitId    sub_14034A330 returns [unit+8]
//   +0x0C dwMode      the only head dword left; +0x10 is the unit-data pointer
//   type 0            player (sub_140584320 walks owners until type 0)
//   mode 0 / mode 17  PMODE_DEATH / PMODE_DEAD, 17 slots apart in the engine's
//                     own mode-name table at 141CC8790
//
// The hook displaces the first 5 bytes, one position independent store. The
// three internal branches all target 00583B14, well clear of the displaced
// region.

constexpr std::uint64_t LifeDrainApplyRva = 0x00583A70ULL;

constexpr std::uint8_t LifeDrainApplyBody[] {
	0x48, 0x89, 0x5C, 0x24, 0x08,
	0x48, 0x89, 0x74, 0x24, 0x10,
	0x57,
	0x48, 0x83, 0xEC, 0x20,
	0x49, 0x8B, 0xD8,
	0x4D, 0x85, 0xC0,
	0x0F, 0x84, 0x89, 0x00, 0x00, 0x00,
	0x4D, 0x85, 0xC9,
	0x0F, 0x84, 0x80, 0x00, 0x00, 0x00,
	0x8B, 0x44, 0x24, 0x58,
	0x48, 0x8B, 0xCB,
	0x8B, 0xD0,
	0x44, 0x0F, 0xB7, 0xC0,
	0xC1, 0xEA, 0x10,
	0xE8, 0xB7, 0x21, 0xD7, 0xFF,
	0x8B, 0xF8,
	0x85, 0xC0,
	0x74, 0x65,
	0x48, 0x8B, 0xCB,
	0xE8, 0x69, 0x12, 0xD7, 0xFF,
	0x45, 0x33, 0xC0,
	0x48, 0x8B, 0xCB,
	0x8B, 0xF0,
	0x41, 0x8D, 0x50, 0x06,
	0xE8, 0x58, 0x15, 0xD7, 0xFF,
	0x3B, 0xC6,
	0x7D, 0x48,
	0x33, 0xC9,
	0xC1, 0xE7, 0x08,
	0x03, 0xC7,
	0x85, 0xC0,
	0x0F, 0x4F, 0xC8,
	0x3B, 0xCE,
	0x0F, 0x4C, 0xF1,
	0x45, 0x33, 0xC9,
	0x44, 0x8B, 0xC6,
	0x48, 0x8B, 0xCB,
	0x41, 0x8D, 0x51, 0x06,
	0xE8, 0x21, 0x42, 0xD7, 0xFF,
	0x45, 0x33, 0xC0,
	0xBA, 0x97, 0x00, 0x00, 0x00,
	0x48, 0x8B, 0xCB,
	0xE8, 0x21, 0x55, 0xDC, 0xFF,
	0xB8, 0x01, 0x00, 0x00, 0x00,
	0x48, 0x8B, 0x5C, 0x24, 0x30,
	0x48, 0x8B, 0x74, 0x24, 0x38,
	0x48, 0x83, 0xC4, 0x20,
	0x5F,
	0xC3,
	0x48, 0x8B, 0x5C, 0x24, 0x30,
	0x33, 0xC0,
	0x48, 0x8B, 0x74, 0x24, 0x38,
	0x48, 0x83, 0xC4, 0x20,
	0x5F,
	0xC3,
};

// mov [rsp+8], rbx
constexpr std::uint8_t LifeDrainApplyPrologue[] {
	0x48, 0x89, 0x5C, 0x24, 0x08,
};

static_assert(sizeof(LifeDrainApplyBody) == 182, "Verified life-drain applier length changed.");
static_assert(sizeof(LifeDrainApplyPrologue) >= 5, "An inline hook needs at least 5 displaced bytes for a rel32 jump.");
static_assert(IsLeadingBytesOf(LifeDrainApplyPrologue, LifeDrainApplyBody), "The displaced bytes must be the leading bytes of the verified body.");

constexpr std::size_t UnitTypeOffset     = 0x00;
constexpr std::size_t UnitAnimModeOffset = 0x0C;

constexpr std::int32_t UnitTypePlayer  = 0;
constexpr std::int32_t PlayerModeDying = 0;   // PMODE_DEATH, the death animation
constexpr std::int32_t PlayerModeDead  = 17;  // PMODE_DEAD, the corpse

// Reads the two head fields without assuming anything about alignment or
// aliasing. Null is not a dead player: the handler's own null check should be
// the one that runs, not this guard.
auto IsPlayerInDeathAnimation(const void* unit) noexcept -> bool {
	if (unit == nullptr) {
		return false;
	}

	const auto* head = static_cast<const unsigned char*>(unit);

	std::int32_t unitType = 0;
	std::int32_t animMode = 0;
	std::memcpy(&unitType, head + UnitTypeOffset, sizeof(unitType));
	std::memcpy(&animMode, head + UnitAnimModeOffset, sizeof(animMode));

	return unitType == UnitTypePlayer
		&& (animMode == PlayerModeDying || animMode == PlayerModeDead);
}

// Six arguments: rcx, rdx, r8, r9 and two stack slots. Only the leecher is
// read; everything else is carried through untouched.
using LifeDrainApplyFn = std::int64_t(__fastcall*)(
	std::uint64_t game,
	std::uint64_t eventId,
	const void*   leecher,
	std::uint64_t other,
	std::uint64_t damage,
	std::uint64_t packedStat) noexcept;

LifeDrainApplyFn OriginalLifeDrainApply = nullptr;

auto __fastcall HookLifeDrainApply(
	std::uint64_t game,
	std::uint64_t eventId,
	const void*   leecher,
	std::uint64_t other,
	std::uint64_t damage,
	std::uint64_t packedStat) noexcept -> std::int64_t {
	if (IsPlayerInDeathAnimation(leecher)) {
		CountSuppressed(Guard::LifeDrainWhileDead);
		return 0;
	}

	const LifeDrainApplyFn original = OriginalLifeDrainApply;
	return original != nullptr ? original(game, eventId, leecher, other, damage, packedStat) : 0;
}

// ---------------------------------------------------------------------------
// Guard 6: automap .map blob length, 16-bit truncation
// ---------------------------------------------------------------------------
//
// The automap cell-set serializer sub_1400D7CE0 walks one cell set into a word
// buffer and reports the byte length back to its caller. Its tail:
//
//   000D7E3A  4D 85 FF                 test   r15, r15
//   000D7E3D  74 0D                    jz     000D7E4C
//   000D7E3F  0F B7 4E 08              movzx  ecx, word ptr [rsi+8]   ; cell words
//   000D7E43  66 03 C9                 add    cx, cx                  ; * 2, 16 bit
//   000D7E46  0F BF C9                 movsx  ecx, cx                 ; sign extend
//   000D7E49  41 89 0F                 mov    [r15], ecx              ; 32 bit field
//
// One automap cell is 3 words, so a set past 5461 cells is past 32768 bytes and
// the length written is negative. The record writer sub_1400D5FE0 hands that
// straight to memmove, which reads it as roughly 4GB. The crash lands on the
// next flush: a layer change, or leaving the game.
//
// The fix computes the length as a full 32 bit value:
//
//   000D7E3F  8B 4E 08                 mov    ecx, [rsi+8]
//   000D7E42  03 C9                    add    ecx, ecx
//   000D7E44  41 89 0F                 mov    [r15], ecx
//   000D7E47  90 90 90 90 90           nop x5
//
// This is format compatible in both directions: the loader sub_1400D5710
// already reads that field as an unsigned 32 bit value, so saves written by a
// patched game still load on an unpatched one and the other way round.
//
// A patch rather than a hook, so it costs nothing at runtime. The verified
// window starts three bytes early, at the null check, so the plugin also
// refuses on a build where the surrounding shape moved.

constexpr std::uint64_t AutomapBlobLengthSiteRva = 0x000D7E3AULL;
constexpr std::uint64_t AutomapBlobLengthRva     = 0x000D7E3FULL;

constexpr std::uint8_t AutomapBlobLengthSite[] {
	0x4D, 0x85, 0xFF,
	0x74, 0x0D,
	0x0F, 0xB7, 0x4E, 0x08,
	0x66, 0x03, 0xC9,
	0x0F, 0xBF, 0xC9,
	0x41, 0x89, 0x0F,
	0x48, 0x8B, 0x4C, 0x24, 0x40,
	0x48, 0x33, 0xCC,
};

constexpr std::uint8_t AutomapBlobLengthOriginal[] {
	0x0F, 0xB7, 0x4E, 0x08,
	0x66, 0x03, 0xC9,
	0x0F, 0xBF, 0xC9,
	0x41, 0x89, 0x0F,
};

constexpr std::uint8_t AutomapBlobLengthPatched[] {
	0x8B, 0x4E, 0x08,
	0x03, 0xC9,
	0x41, 0x89, 0x0F,
	0x90, 0x90, 0x90, 0x90, 0x90,
};

static_assert(sizeof(AutomapBlobLengthOriginal) == sizeof(AutomapBlobLengthPatched), "A byte patch must not change the length of the region it replaces.");
static_assert(sizeof(AutomapBlobLengthOriginal) == 13, "Verified automap length region changed.");
static_assert(AutomapBlobLengthRva - AutomapBlobLengthSiteRva == 5, "The patch must start 5 bytes into the verified window.");
static_assert(IsSubrangeOf(AutomapBlobLengthOriginal, AutomapBlobLengthSite, 5), "The replaced bytes must sit inside the verified window at the stated offset.");

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

// Kept identical to the shipped celestialrayone.engine-stability.toml. This is
// what EnsureConfig writes when the file does not exist yet.
constexpr const char* DefaultConfigToml =
R"toml(# Engine Stability - crash guards for Diablo II: Resurrected
#
# Nothing in this plugin changes gameplay, balance or presentation. Every
# switch below turns one guard on or off, and a guard is only ever the
# difference between a hard crash and a normal frame. Turning a guard off
# restores stock (crashing) behaviour.
#
# Safety: the plugin verifies the original bytes at every hook and patch site
# before it installs anything. On a game build it does not recognise it
# installs nothing no matter what this file says, logs loudly, and stays
# loaded so the `engine-stability` console command can tell you why.
#
# This file lives at <scope>\d2rloader\config\celestialrayone.engine-stability.toml
# and is created with these defaults on first run. Delete it to get them back.
# Changes are read at plugin load, so restart the game after editing.
#
# Type `engine-stability` in the console to see which guards are actually live.


[engine-stability]

# Master switch for the whole plugin.
#   true  - guards are installed as configured in [guards] below.
#   false - the DLL stays loaded and the console command still answers, but no
#           hook is installed at all. Use this to rule the plugin out while
#           chasing a crash without having to move the DLL out of the folder.
# Default: true
enabled = true


[guards]

# Client unit-by-id hash lookup, tombstoned unit id.
# Site: RVA 0009F270, the shared bottom of the client's unit-by-id lookup.
#
# What goes wrong without it: freeing a unit stamps a tombstone on the block
# (unit id -1) and returns it to the pool, so every later resolution of that
# owner arrives at the lookup with id -1. That masks to bucket 0x7F and the
# walk follows whatever that slot happens to hold, which is where the dangling
# chain node is. Reliable repro: a summon dies while the missiles from its own
# on-death proc are still in flight.
#
# What the guard does: an id of -1 is never a live unit, so it answers "not
# found" without walking the chain. Returning null is this function's own
# not-found result, so every call site already handles it.
#
# Cost when on: one compare against -1 per lookup. The hot path (any live unit
# id) touches nothing else.
# Default: true
client_unit_lookup_tombstone = true

# Oversized transmute command.
# Site: RVA 000ED120, the client's CCMD_TRANSMUTE (32) command builder.
#
# What goes wrong without it: the builder writes an 8 byte header plus 5 bytes
# per cube item into a command buffer, then hands it to the client command
# sender. The sender copies the command into three fixed 512 byte buffers (a
# global "last command sent" buffer and two stack buffers) with no length
# check anywhere. Transmuting a cube holding more than 100 items overruns all
# three. The item count also travels as a single byte, so above 256 items the
# builder overruns its own stack frame as well. The overrun corrupts adjacent
# memory rather than faulting on the spot, so the crash usually lands
# somewhere unrelated, often during process teardown.
#
# What the guard does: counts the items the builder was handed and drops the
# transmute if the resulting command would not fit in 512 bytes. Nothing is
# sent, the cube is untouched, and the game carries on. No legitimate recipe
# takes anywhere near 100 inputs, so in practice this only ever fires on a
# misclick against a cube used as bulk storage.
#
# Cost when on: one subtraction and one compare per transmute.
# Default: true
transmute_command_overflow = true

# Oversized item-displace command.
# Site: RVA 000EC580, the client's CCMD_DISPLACEITEMS (34) command builder.
#
# What goes wrong without it: the same overflow as the transmute guard above,
# on the path that moves a container and its contents. The command is a 10
# byte header plus 7 bytes per displaced item, so it passes 512 bytes at 72
# items. Dropping a cube full of loot on the ground is the usual way to reach
# it.
#
# What the guard does: counts the items and drops the command if it would not
# fit in 512 bytes. The item stays where it was.
#
# Cost when on: one subtraction and one compare per displace.
# Default: true
item_displace_command_overflow = true

# Oversized quick-move item-displace command.
# Site: RVA 000ECB40, the client's CCMD_QUICKDISPLACEITEMS (96) command
# builder.
#
# This is the same bug and the same command layout as the guard above, on the
# quick-move path (ctrl-click and controller quick-move) instead of the drag
# path. Its header is 25 bytes, so it passes 512 bytes at 70 items.
#
# This guard is precautionary. The overflow is real and the arithmetic is the
# same, but unlike the two above it has not been observed crashing in the
# wild. It is separated from item_displace_command_overflow so it can be
# turned off on its own.
# Default: true
item_quick_displace_command_overflow = true

# Life stolen per hit applied to an already dead player.
# Site: RVA 00583A70, the life-drain handler (ItemEventFunc slot 28, the one
# both itemstatcost's itemeventfunc columns and skills.txt's auraeventfunc
# columns index).
#
# What goes wrong without it: when a player dies the engine commits life to 0,
# then spends the whole death animation in the dying and dead modes before the
# corpse-spawn pass finishes. Any leeching hit that lands in that window runs
# the life-drain handler against the corpse, which writes life straight back
# into the life stat. That life is cashed in at the animation-complete
# transition and the player revives in place: the server has them dead, the
# client has them alive, and only Save and Exit clears it. Auras that keep
# spawning leeching missiles after their caster dies hit this constantly.
#
# What the guard does: if the unit about to be paid the stolen life is a
# player in a death animation, the handler returns without writing life and
# without scheduling its follow-up event. That is the handler's own "did
# nothing" return, which its five other early exits already use. Living
# players, monsters and hirelings leech exactly as before.
#
# Cost when on: two struct reads and up to three compares per leeching hit.
# Default: true
lifesteal_while_dead = true

# Automap .map blob length, 16-bit truncation.
# Site: RVA 000D7E3F, in the automap cell-set serializer.
#
# What goes wrong without it: the serializer computes the blob byte length as
# (int16)(cells * 2) and stores it sign extended into a 32 bit length field.
# One automap cell is 3 words, so any layer holding more than 5461 cells
# writes a negative length, which the record writer's memmove consumes as
# roughly 4GB. The game dies on the next automap flush, which happens on a
# layer change or on leaving the game. Long map running sessions on a single
# layer are what get there.
#
# What the guard does: computes the length as a full 32 bit value. The loader
# already reads that field as unsigned 32 bit, so saves stay readable by an
# unpatched game and by an unpatched loader.
#
# This one is a 13 byte instruction rewrite rather than a hook, so it costs
# nothing at runtime.
# Default: true
automap_blob_length_truncation = true
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

struct StabilityConfig {
	bool pluginEnabled                    { true };
	bool clientUnitLookupTombstone        { true };
	bool transmuteCommandOverflow         { true };
	bool itemDisplaceCommandOverflow      { true };
	bool itemQuickDisplaceCommandOverflow { true };
	bool lifestealWhileDead               { true };
	bool automapBlobLengthTruncation      { true };
};

StabilityConfig Config {};

// Only meaningful for the log line; the console command reads the guard states.
bool ConfigFileWasRead = false;

void LoadConfiguration(const D2RL::PluginContext* context) noexcept {
	if (context == nullptr) {
		return;
	}

	if (!context->EnsureConfig(DefaultConfigToml)) {
		context->LogWarn(
			"Could not create or open celestialrayone.engine-stability.toml. "
			"Running with defaults: every guard ON.");
		return;
	}

	std::array<char, 32768> buffer {};
	std::uint32_t           requiredSize = 0;

	if (!context->ReadConfig(buffer.data(), static_cast<std::uint32_t>(buffer.size() - 1), &requiredSize)) {
		context->LogWarn(
			"Could not read celestialrayone.engine-stability.toml. "
			"Running with defaults: every guard ON.");
		return;
	}

	if (requiredSize >= buffer.size()) {
		D2RL::LogWarnF(
			context,
			"celestialrayone.engine-stability.toml is %u bytes, larger than the %zu byte read buffer. "
			"Running with defaults: every guard ON.",
			requiredSize,
			buffer.size());
		return;
	}

	buffer[buffer.size() - 1] = '\0';
	ConfigFileWasRead         = true;

	(void)ReadConfigBool(buffer.data(), "engine-stability", "enabled", Config.pluginEnabled);
	(void)ReadConfigBool(buffer.data(), "guards", "client_unit_lookup_tombstone", Config.clientUnitLookupTombstone);
	(void)ReadConfigBool(buffer.data(), "guards", "transmute_command_overflow", Config.transmuteCommandOverflow);
	(void)ReadConfigBool(buffer.data(), "guards", "item_displace_command_overflow", Config.itemDisplaceCommandOverflow);
	(void)ReadConfigBool(buffer.data(), "guards", "item_quick_displace_command_overflow", Config.itemQuickDisplaceCommandOverflow);
	(void)ReadConfigBool(buffer.data(), "guards", "lifesteal_while_dead", Config.lifestealWhileDead);
	(void)ReadConfigBool(buffer.data(), "guards", "automap_blob_length_truncation", Config.automapBlobLengthTruncation);

	D2RL::LogInfoF(
		context,
		"config: enabled=%s, client_unit_lookup_tombstone=%s, transmute_command_overflow=%s, "
		"item_displace_command_overflow=%s, item_quick_displace_command_overflow=%s, "
		"lifesteal_while_dead=%s, automap_blob_length_truncation=%s.",
		Config.pluginEnabled ? "true" : "false",
		Config.clientUnitLookupTombstone ? "true" : "false",
		Config.transmuteCommandOverflow ? "true" : "false",
		Config.itemDisplaceCommandOverflow ? "true" : "false",
		Config.itemQuickDisplaceCommandOverflow ? "true" : "false",
		Config.lifestealWhileDead ? "true" : "false",
		Config.automapBlobLengthTruncation ? "true" : "false");
}

// ---------------------------------------------------------------------------
// Installation
// ---------------------------------------------------------------------------

// One row per inline hook. Everything that differs between the four hook sites
// lives here so the install and report paths stay single-copy.
struct InlineGuardSite {
	Guard               guard;
	const char*         label;      // how the guard is named in log and console
	const char*         eventNoun;  // what its suppression counter counts
	const char*         rvaText;    // for messages; %llX of a constant reads badly
	std::uint64_t       rva;
	const std::uint8_t* body;
	std::uint32_t       bodySize;
	const std::uint8_t* prologue;
	std::uint32_t       prologueSize;
	void*               hook;
	void**              original;
	const bool*         enabled;
};

const InlineGuardSite InlineGuardSites[] {
	{
		.guard        = Guard::DeadUnitLookup,
		.label        = "dead-unit lookup guard",
		.eventNoun    = "tombstoned lookups suppressed",
		.rvaText      = "0009F270",
		.rva          = UnitHashLookupRva,
		.body         = UnitHashLookupBody,
		.bodySize     = ByteCount(UnitHashLookupBody),
		.prologue     = UnitHashLookupPrologue,
		.prologueSize = ByteCount(UnitHashLookupPrologue),
		.hook         = reinterpret_cast<void*>(&HookUnitHashLookup),
		.original     = reinterpret_cast<void**>(&OriginalUnitHashLookup),
		.enabled      = &Config.clientUnitLookupTombstone,
	},
	{
		.guard        = Guard::TransmuteCommand,
		.label        = "transmute command size guard",
		.eventNoun    = "oversized transmutes dropped",
		.rvaText      = "000ED120",
		.rva          = TransmuteCommandRva,
		.body         = TransmuteCommandBody,
		.bodySize     = ByteCount(TransmuteCommandBody),
		.prologue     = TransmuteCommandPrologue,
		.prologueSize = ByteCount(TransmuteCommandPrologue),
		.hook         = reinterpret_cast<void*>(&HookTransmuteCommand),
		.original     = reinterpret_cast<void**>(&OriginalTransmuteCommand),
		.enabled      = &Config.transmuteCommandOverflow,
	},
	{
		.guard        = Guard::DisplaceCommand,
		.label        = "item displace command size guard",
		.eventNoun    = "oversized displaces dropped",
		.rvaText      = "000EC580",
		.rva          = DisplaceCommandRva,
		.body         = DisplaceCommandBody,
		.bodySize     = ByteCount(DisplaceCommandBody),
		.prologue     = DisplaceCommandPrologue,
		.prologueSize = ByteCount(DisplaceCommandPrologue),
		.hook         = reinterpret_cast<void*>(&HookDisplaceCommand),
		.original     = reinterpret_cast<void**>(&OriginalDisplaceCommand),
		.enabled      = &Config.itemDisplaceCommandOverflow,
	},
	{
		.guard        = Guard::QuickDisplaceCommand,
		.label        = "item quick-move command size guard",
		.eventNoun    = "oversized quick-moves dropped",
		.rvaText      = "000ECB40",
		.rva          = QuickDisplaceCommandRva,
		.body         = QuickDisplaceCommandBody,
		.bodySize     = ByteCount(QuickDisplaceCommandBody),
		.prologue     = QuickDisplaceCommandPrologue,
		.prologueSize = ByteCount(QuickDisplaceCommandPrologue),
		.hook         = reinterpret_cast<void*>(&HookQuickDisplaceCommand),
		.original     = reinterpret_cast<void**>(&OriginalQuickDisplaceCommand),
		.enabled      = &Config.itemQuickDisplaceCommandOverflow,
	},
	{
		.guard        = Guard::LifeDrainWhileDead,
		.label        = "lifesteal-while-dead guard",
		.eventNoun    = "post-death leeches blocked",
		.rvaText      = "00583A70",
		.rva          = LifeDrainApplyRva,
		.body         = LifeDrainApplyBody,
		.bodySize     = ByteCount(LifeDrainApplyBody),
		.prologue     = LifeDrainApplyPrologue,
		.prologueSize = ByteCount(LifeDrainApplyPrologue),
		.hook         = reinterpret_cast<void*>(&HookLifeDrainApply),
		.original     = reinterpret_cast<void**>(&OriginalLifeDrainApply),
		.enabled      = &Config.lifestealWhileDead,
	},
};

constexpr const char* AutomapGuardLabel = "automap blob length guard";

auto InstallInlineGuard(const D2RL::PluginContext* context, const InlineGuardSite& site) noexcept -> bool {
	if (context == nullptr) {
		return false;
	}

	if (!Config.pluginEnabled || !*site.enabled) {
		SetGuardState(site.guard, GuardState::DisabledByConfig);
		D2RL::LogInfoF(context, "%s not installed: turned off in the config file.", site.label);
		return false;
	}

	if (!context->CheckExpectedBytes(site.rva, site.body, site.bodySize)) {
		SetGuardState(site.guard, GuardState::UnsupportedBuild);
		D2RL::LogErrorF(
			context,
			"%s NOT installed: the bytes at RVA %s do not match the verified 3.3.93847 body. "
			"This build is not supported; nothing was patched.",
			site.label,
			site.rvaText);
		return false;
	}

	if (!context->InstallInlineHook(site.rva, site.prologue, site.prologueSize, site.hook, site.original)) {
		SetGuardState(site.guard, GuardState::InstallFailed);
		D2RL::LogErrorF(context, "%s NOT installed: InstallInlineHook failed at RVA %s.", site.label, site.rvaText);
		return false;
	}

	if (*site.original == nullptr) {
		SetGuardState(site.guard, GuardState::InstallFailed);
		D2RL::LogErrorF(context, "%s NOT installed: the loader returned no trampoline.", site.label);
		return false;
	}

	SetGuardState(site.guard, GuardState::Installed);
	D2RL::LogInfoF(context, "%s installed at RVA %s.", site.label, site.rvaText);
	return true;
}

auto InstallAutomapBlobLengthGuard(const D2RL::PluginContext* context) noexcept -> bool {
	if (context == nullptr) {
		return false;
	}

	if (!Config.pluginEnabled || !Config.automapBlobLengthTruncation) {
		SetGuardState(Guard::AutomapBlobLength, GuardState::DisabledByConfig);
		D2RL::LogInfoF(context, "%s not installed: turned off in the config file.", AutomapGuardLabel);
		return false;
	}

	if (!context->CheckExpectedBytes(AutomapBlobLengthSiteRva, AutomapBlobLengthSite, ByteCount(AutomapBlobLengthSite))) {
		SetGuardState(Guard::AutomapBlobLength, GuardState::UnsupportedBuild);
		D2RL::LogErrorF(
			context,
			"%s NOT installed: the bytes at RVA 000D7E3A do not match the verified 3.3.93847 serializer tail. "
			"This build is not supported; nothing was patched.",
			AutomapGuardLabel);
		return false;
	}

	if (!context->PatchBytes(
			AutomapBlobLengthRva,
			AutomapBlobLengthOriginal,
			ByteCount(AutomapBlobLengthOriginal),
			AutomapBlobLengthPatched,
			ByteCount(AutomapBlobLengthPatched))) {
		SetGuardState(Guard::AutomapBlobLength, GuardState::InstallFailed);
		D2RL::LogErrorF(context, "%s NOT installed: PatchBytes failed at RVA 000D7E3F.", AutomapGuardLabel);
		return false;
	}

	SetGuardState(Guard::AutomapBlobLength, GuardState::Installed);
	D2RL::LogInfoF(context, "%s installed at RVA 000D7E3F.", AutomapGuardLabel);
	return true;
}

// ---------------------------------------------------------------------------
// Console command
// ---------------------------------------------------------------------------

void ReportGuard(
	const D2RL::PluginContext* context,
	Guard                      guard,
	const char*                label,
	const char*                eventNoun) noexcept {
	char message[256] {};

	switch (GetGuardState(guard)) {
	case GuardState::Installed:
		if (eventNoun != nullptr) {
			std::snprintf(
				message,
				sizeof(message),
				"%s: active. %s so far: %llu",
				label,
				eventNoun,
				static_cast<unsigned long long>(SuppressedCount(guard)));
		} else {
			std::snprintf(message, sizeof(message), "%s: active.", label);
		}
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

auto EngineStabilityCommand(
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
		"engine-stability: config %s, plugin %s.",
		ConfigFileWasRead ? "loaded" : "NOT READ (defaults in use)",
		Config.pluginEnabled ? "enabled" : "DISABLED in config");
	context->WriteConsoleMessage(header);

	for (const InlineGuardSite& site : InlineGuardSites) {
		ReportGuard(context, site.guard, site.label, site.eventNoun);
	}

	ReportGuard(context, Guard::AutomapBlobLength, AutomapGuardLabel, nullptr);

	return D2RL::ConsoleCommandResult::Handled;
}

constexpr D2RL::PluginInfo EngineStabilityInfo {
	.infoSize    = D2RL::PluginInfoSize,
	.apiVersion  = D2RL_PLUGIN_API_VERSION,
	.id          = "celestialrayone.engine-stability",
	.name        = "Engine Stability",
	.version     = "0.4.0",
	.author      = "CelestialRayOne",
	.description = "Crash guards for Diablo II: Resurrected. No gameplay changes.",
	.flags       = D2RL::PluginFlags::Client | D2RL::PluginFlags::NativeHooks,
};

static_assert(D2RL::HasValidPluginRole(EngineStabilityInfo.flags), "Exactly one execution role must be set.");
static_assert(D2RL::HasOnlyKnownPluginFlags(EngineStabilityInfo.flags), "Unknown plugin flag set.");
static_assert(D2RL::HasFlag(EngineStabilityInfo.flags, D2RL::PluginFlags::NativeHooks), "Inline hooks require the NativeHooks flag.");

} // namespace

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* {
	return &EngineStabilityInfo;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
	if (context == nullptr) {
		return false;
	}

	// Registered before anything is installed on purpose. Returning false from
	// this function unloads the DLL and takes the console command with it,
	// which would leave a user on an unsupported build with no in-game signal
	// at all. This plugin would rather stay loaded and be able to say that it
	// did nothing.
	if (!context->RegisterConsoleCommand(
			"engine-stability",
			EngineStabilityCommand,
			"Report which engine stability guards are active.")) {
		context->LogWarn("The engine-stability console command was not registered.");
	}

	if (const char* build = D2RL::GetBuildVersion(context); build != nullptr) {
		D2RL::LogInfoF(context, "engine-stability loading against build %s.", build);
	}

	LoadConfiguration(context);

	if (!Config.pluginEnabled) {
		for (const InlineGuardSite& site : InlineGuardSites) {
			SetGuardState(site.guard, GuardState::DisabledByConfig);
		}
		SetGuardState(Guard::AutomapBlobLength, GuardState::DisabledByConfig);
		context->LogWarn("engine-stability is disabled in its config file. No guards were installed.");
		return true;
	}

	// Each guard is installed independently. One refusing, whether because the
	// config turned it off or because its site did not verify, must never take
	// the others down with it.
	unsigned installed = 0;

	for (const InlineGuardSite& site : InlineGuardSites) {
		if (InstallInlineGuard(context, site)) {
			++installed;
		}
	}

	if (InstallAutomapBlobLengthGuard(context)) {
		++installed;
	}

	if (installed == 0) {
		context->LogError("engine-stability loaded with NO guards active.");
		return true;
	}

	D2RL::LogInfoF(context, "engine-stability loaded with %u of %zu guards active.", installed, GuardCount);
	return true;
}

// An installed inline hook cannot be withdrawn, and the trampoline lives in
// this module, so there is deliberately nothing to undo here. The loader is
// expected to keep the DLL resident for the life of the process.
D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {}
