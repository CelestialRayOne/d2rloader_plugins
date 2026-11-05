// ---------------------------------------------------------------------------
// celestialrayone.blood-magic
//
// While a player has state 114 (blood_mana), skill and aura costs are paid
// from life only. A cost above current life is refused, mana is never used,
// and a payment never ends the state. See celestialrayone.blood-magic.toml.
//
// Target: D2RLoader 1.3.1 (Diablo II: Resurrected 3.3.93847), PluginSDK 0.3.0.
// Game addresses are RVAs in the D2RLoader.exe image (base 0x140000000).
// D2RCore.dll addresses are RVAs in that DLL as shipped with 1.3.1
// (SHA-256 2A868D013D2E0830BD2D9E04B918B19E46A73CF726C833E70D089B948FDEB5A2).
//
// The engine, for a player with state 114:
//   cast gate   00436750  loader jump into D2RCore CanAffordWideUsedSkill:
//                         life >= cost. Correct, left alone.
//   skill cost  00436830  loader jump into D2RCore ConsumeWideSkillResource,
//                         whose body (003DD6E0) jumps to D2RCore 003DDB50.
//   aura cost   00436A80  native periodic payment, called by four aura skill
//                         functions (00563650, 00563CE1, 00564224, 00564578).
//                         For state 114 it jumps to native 00584E80.
// Both life payments (003DDB50 and 00584E80) still run the vanilla Blood Mana
// curse rules:
//   life < cost  expire and free the state 114 stat list (its remove callback
//                turns the state off), set life to 256 (1 life), return 0.
//   paid         take cost from life; if life is now below the granting
//                skill's dword [record+1C8h] << 8, expire and free the list.
// With the state off, the gate and both payments charge mana again: life
// below the cost, the skill still casts, mana is taken.
// 00584E80 also faults when state 114 is set but its stat list is gone: it
// hands the null lookup to 002F56A0, which reads [rbx+28h] at 002F56C1. A
// granting skill id without a Skills.txt record faults at 00584EF7. Neither
// was ever dead code: the aura payment still reaches it.
//
// The fix replaces both life payments with one rule:
//   life < cost  return 0. Nothing is taken, the state stays.
//   otherwise    take cost from life, return 1. The state stays.
// The callers are unchanged, so non-players, item charges, players without
// state 114 and the cast gate run exactly as before.
//
// Sites:
//   D2RCore 003DDB50  reached from the export ConsumeWideSkillResource: stub
//                     call -> payment body -> the body's tail jump for state
//                     114. Stub, body and routine are compared byte for byte,
//                     leaving out rip-relative displacements and rel32s to
//                     other functions; the D2RCore slots they call through
//                     must name the expected game functions. The first 14
//                     bytes become an absolute jump into this DLL and are
//                     restored on unload.
//   game 00584E80     inline hook through the loader, after the whole routine
//                     and the aura payment's jump to it are compared.
// ---------------------------------------------------------------------------

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

namespace {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

template <std::size_t Size>
constexpr auto ByteCount(const std::uint8_t (&)[Size]) noexcept -> std::uint32_t {
	return static_cast<std::uint32_t>(Size);
}

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

// rva of the target of the E8/E9 rel32 at `offset` inside a verified window.
template <std::size_t Size>
constexpr auto Rel32TargetRva(const std::uint8_t (&window)[Size], std::uint64_t windowRva, std::size_t offset) noexcept -> std::uint64_t {
	const auto rel = static_cast<std::int32_t>(
		static_cast<std::uint32_t>(window[offset + 1])
		| (static_cast<std::uint32_t>(window[offset + 2]) << 8)
		| (static_cast<std::uint32_t>(window[offset + 3]) << 16)
		| (static_cast<std::uint32_t>(window[offset + 4]) << 24));
	return static_cast<std::uint64_t>(static_cast<std::int64_t>(windowRva + offset + 5) + rel);
}

// A byte range a comparison leaves out.
struct ByteGap {
	std::size_t offset;
	std::size_t size;
};

template <std::size_t GapCount>
constexpr auto HasGapAt(const ByteGap (&gaps)[GapCount], std::size_t offset) noexcept -> bool {
	for (const ByteGap& gap : gaps) {
		if (gap.offset == offset && gap.size == 4) {
			return true;
		}
	}
	return false;
}

template <std::size_t Size, std::size_t GapCount>
constexpr auto GapsFit(const std::uint8_t (&)[Size], const ByteGap (&gaps)[GapCount]) noexcept -> bool {
	for (const ByteGap& gap : gaps) {
		if (gap.offset + gap.size > Size) {
			return false;
		}
	}
	return true;
}

// ---------------------------------------------------------------------------
// Game functions the payments and the slot checks refer to
// ---------------------------------------------------------------------------

constexpr std::uint64_t GetUnitStatRva       = 0x002F5020ULL;  // (unit, stat, layer) -> value
constexpr std::uint64_t AddUnitStatRva       = 0x002F34F0ULL;  // (unit, stat, value, layer)
constexpr std::uint64_t SetUnitStatRva       = 0x002F7D10ULL;  // (unit, stat, value, layer)
constexpr std::uint64_t FindStateListRva     = 0x002F5940ULL;  // (unit, state) -> stat list
constexpr std::uint64_t StateListSkillIdRva  = 0x002F56A0ULL;  // (stat list) -> granting skill id
constexpr std::uint64_t CheckStateRva        = 0x003351B0ULL;  // (unit, state) -> bool
constexpr std::uint64_t GetManaCostRva       = 0x0033AA00ULL;  // (data bank, skill id, level) -> cost
constexpr std::uint64_t SkillsRecordRva      = 0x00097790ULL;  // (data bank, skill id) -> Skills.txt record

constexpr std::int32_t StatHitpoints  = 6;
constexpr std::int32_t StateBloodMana = 114;

// ---------------------------------------------------------------------------
// Game image: the aura route
// ---------------------------------------------------------------------------

// Vanilla blood mana life payment, (unit, cost) -> 1 paid / 0 refused.
//   +014h mov edx, 72h / +019h call 002F5940     state 114 stat list, no null test
//   +021h call 002F56A0                          granting skill id (faults on null)
//   +032h call 002F5020                          life
//   +03Ch jl  +0CCh                              life < cost
//   +04Eh call 002F34F0                          life -= cost
//   +077h mov ecx, [rbx+1C8h] / shl ecx, 8       threshold from the granting skill
//   +09Fh call 002F7920 / +0B2h call 002F4180    expire and free the stat list
//   +0FFh mov r8d, 100h / +10Ch call 002F7D10    life = 256 on the refuse path
constexpr std::uint64_t NativeBloodPayRva = 0x00584E80ULL;
constexpr std::uint8_t ExpectedNativeBloodPay[] {
	0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48,
	0x83, 0xEC, 0x20, 0x8B, 0xDA, 0x48, 0x8B, 0xF9, 0xBA, 0x72, 0x00, 0x00,
	0x00, 0xE8, 0xA2, 0x0A, 0xD7, 0xFF, 0x48, 0x8B, 0xC8, 0xE8, 0xFA, 0x07,
	0xD7, 0xFF, 0x45, 0x33, 0xC0, 0x48, 0x8B, 0xCF, 0x8B, 0xF0, 0x41, 0x8D,
	0x50, 0x06, 0xE8, 0x69, 0x01, 0xD7, 0xFF, 0x48, 0x8B, 0xCF, 0x3B, 0xC3,
	0x0F, 0x8C, 0x8A, 0x00, 0x00, 0x00, 0x45, 0x33, 0xC9, 0xF7, 0xDB, 0x44,
	0x8B, 0xC3, 0x41, 0x8D, 0x51, 0x06, 0xE8, 0x1D, 0xE6, 0xD6, 0xFF, 0x48,
	0x8B, 0xCF, 0xE8, 0x05, 0x52, 0xDC, 0xFF, 0x0F, 0xB6, 0xC8, 0x8B, 0xD6,
	0xE8, 0xAB, 0x28, 0xB1, 0xFF, 0x45, 0x33, 0xC0, 0x48, 0x8B, 0xCF, 0x48,
	0x8B, 0xD8, 0x41, 0x8D, 0x50, 0x06, 0xE8, 0x29, 0x01, 0xD7, 0xFF, 0x8B,
	0x8B, 0xC8, 0x01, 0x00, 0x00, 0xC1, 0xE1, 0x08, 0x3B, 0xC1, 0x7D, 0x33,
	0xBA, 0x72, 0x00, 0x00, 0x00, 0x48, 0x8B, 0xCF, 0xE8, 0x2F, 0x0A, 0xD7,
	0xFF, 0x48, 0x8B, 0xD8, 0x48, 0x85, 0xC0, 0x74, 0x1E, 0x48, 0x8B, 0xD0,
	0x48, 0x8B, 0xCF, 0xE8, 0xFC, 0x29, 0xD7, 0xFF, 0x48, 0x8B, 0xCF, 0xE8,
	0xB4, 0x51, 0xDC, 0xFF, 0x0F, 0xB6, 0xC8, 0x48, 0x8B, 0xD3, 0xE8, 0x49,
	0xF2, 0xD6, 0xFF, 0xB8, 0x01, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x5C, 0x24,
	0x30, 0x48, 0x8B, 0x74, 0x24, 0x38, 0x48, 0x83, 0xC4, 0x20, 0x5F, 0xC3,
	0xBA, 0x72, 0x00, 0x00, 0x00, 0xE8, 0xEA, 0x09, 0xD7, 0xFF, 0x48, 0x8B,
	0xD8, 0x48, 0x85, 0xC0, 0x74, 0x1E, 0x48, 0x8B, 0xD0, 0x48, 0x8B, 0xCF,
	0xE8, 0xB7, 0x29, 0xD7, 0xFF, 0x48, 0x8B, 0xCF, 0xE8, 0x6F, 0x51, 0xDC,
	0xFF, 0x0F, 0xB6, 0xC8, 0x48, 0x8B, 0xD3, 0xE8, 0x04, 0xF2, 0xD6, 0xFF,
	0x45, 0x33, 0xC9, 0x41, 0xB8, 0x00, 0x01, 0x00, 0x00, 0x48, 0x8B, 0xCF,
	0x41, 0x8D, 0x51, 0x06, 0xE8, 0x7F, 0x2D, 0xD7, 0xFF, 0x48, 0x8B, 0x5C,
	0x24, 0x30, 0x33, 0xC0, 0x48, 0x8B, 0x74, 0x24, 0x38, 0x48, 0x83, 0xC4,
	0x20, 0x5F, 0xC3,
};
// mov [rsp+8], rbx / mov [rsp+10h], rsi / push rdi / sub rsp, 20h. Whole
// instructions, taken by the loader's inline hook.
constexpr std::uint8_t NativeBloodPayPrologue[] {
	0x48, 0x89, 0x5C, 0x24, 0x08,
	0x48, 0x89, 0x74, 0x24, 0x10,
	0x57,
	0x48, 0x83, 0xEC, 0x20,
};

static_assert(sizeof(ExpectedNativeBloodPay) == 291, "Verified native blood mana payment length changed.");
static_assert(ExpectedNativeBloodPay[sizeof(ExpectedNativeBloodPay) - 1] == 0xC3, "The verified body must end on its ret.");
static_assert(IsLeadingBytesOf(NativeBloodPayPrologue, ExpectedNativeBloodPay), "The hooked prologue must be the routine's first bytes.");
static_assert(ExpectedNativeBloodPay[0x14] == 0xBA && ExpectedNativeBloodPay[0x15] == StateBloodMana, "+14h must be mov edx, 72h.");
static_assert(Rel32TargetRva(ExpectedNativeBloodPay, NativeBloodPayRva, 0x19) == FindStateListRva, "+19h must look up the state's stat list.");
static_assert(Rel32TargetRva(ExpectedNativeBloodPay, NativeBloodPayRva, 0x21) == StateListSkillIdRva, "+21h must read the granting skill id.");
static_assert(Rel32TargetRva(ExpectedNativeBloodPay, NativeBloodPayRva, 0x32) == GetUnitStatRva, "+32h must read life.");
static_assert(Rel32TargetRva(ExpectedNativeBloodPay, NativeBloodPayRva, 0x4E) == AddUnitStatRva, "+4Eh must take the cost from life.");
static_assert(ExpectedNativeBloodPay[0x77] == 0x8B && ExpectedNativeBloodPay[0x78] == 0x8B && ExpectedNativeBloodPay[0x79] == 0xC8 && ExpectedNativeBloodPay[0x7A] == 0x01, "+77h must read the threshold at record+1C8h.");
static_assert(ExpectedNativeBloodPay[0xFF] == 0x41 && ExpectedNativeBloodPay[0x100] == 0xB8 && ExpectedNativeBloodPay[0x101] == 0x00 && ExpectedNativeBloodPay[0x102] == 0x01, "+FFh must be mov r8d, 100h.");
static_assert(Rel32TargetRva(ExpectedNativeBloodPay, NativeBloodPayRva, 0x10C) == SetUnitStatRva, "+10Ch must set life.");

// Periodic payment, (unit, cost) -> int. Its only route into the routine
// above: state 114 -> jmp 00584E80.
//   +01Eh call 003351B0 with edx = 72h (lea edx, [rax+72h] at +018h)
//   +036h jmp 00584E80
constexpr std::uint64_t AuraPayRva = 0x00436A80ULL;
constexpr std::uint8_t ExpectedAuraPay[] {
	0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x8B, 0xFA,
	0x48, 0x8B, 0xD9, 0xE8, 0x3C, 0x4F, 0xF1, 0xFF, 0x85, 0xC0, 0x75, 0x5D,
	0x8D, 0x50, 0x72, 0x48, 0x8B, 0xCB, 0xE8, 0x0D, 0xE7, 0xEF, 0xFF, 0x48,
	0x8B, 0xCB, 0x85, 0xC0, 0x74, 0x11, 0x8B, 0xD7, 0x48, 0x8B, 0x5C, 0x24,
	0x30, 0x48, 0x83, 0xC4, 0x20, 0x5F, 0xE9, 0xC5, 0xE3, 0x14, 0x00, 0x45,
	0x33, 0xC0, 0x41, 0x8D, 0x50, 0x08, 0xE8, 0x59, 0xE5, 0xEB, 0xFF, 0x3B,
	0xC7, 0x7D, 0x0D, 0x33, 0xC0, 0x48, 0x8B, 0x5C, 0x24, 0x30, 0x48, 0x83,
	0xC4, 0x20, 0x5F, 0xC3, 0x80, 0x3D, 0x8B, 0xFF, 0x66, 0x02, 0x00, 0x75,
	0x14, 0x45, 0x33, 0xC9, 0xF7, 0xDF, 0x44, 0x8B, 0xC7, 0x48, 0x8B, 0xCB,
	0x41, 0x8D, 0x51, 0x08, 0xE8, 0xFB, 0xC9, 0xEB, 0xFF, 0x48, 0x8B, 0x5C,
	0x24, 0x30, 0xB8, 0x01, 0x00, 0x00, 0x00, 0x48, 0x83, 0xC4, 0x20, 0x5F,
	0xC3,
};
constexpr std::size_t AuraPayBloodJumpOffset = 0x36;

static_assert(sizeof(ExpectedAuraPay) == 133, "Verified aura payment length changed.");
static_assert(ExpectedAuraPay[0x18] == 0x8D && ExpectedAuraPay[0x19] == 0x50 && ExpectedAuraPay[0x1A] == StateBloodMana, "+18h must load state 114.");
static_assert(Rel32TargetRva(ExpectedAuraPay, AuraPayRva, 0x1E) == CheckStateRva, "+1Eh must check the state.");
static_assert(ExpectedAuraPay[AuraPayBloodJumpOffset] == 0xE9 && Rel32TargetRva(ExpectedAuraPay, AuraPayRva, AuraPayBloodJumpOffset) == NativeBloodPayRva, "+36h must jump into the native blood mana payment.");

// ---------------------------------------------------------------------------
// Game image: loader jumps the payments call or come through
// ---------------------------------------------------------------------------

// jmp qword ptr [rip+disp32]. disp32 and the slot belong to the loader.
constexpr std::uint8_t LoaderJump[] { 0xFF, 0x25 };
constexpr std::size_t  LoaderJumpBytes = 6;

// 002F5020 and 002F34F0: the game's stat read and stat add, now loader jumps
// into D2RCore. The bytes after each jump are the untouched native bodies.
constexpr std::uint8_t ExpectedGetUnitStatTail[] {
	0x90, 0x90, 0x90, 0x90, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57, 0x48, 0x83,
	0xEC, 0x20, 0x41, 0x0F, 0xB7, 0xE8, 0x8B, 0xFA, 0x48, 0x8B, 0xD9, 0x48,
	0x85, 0xC9, 0x75, 0x2A, 0x88, 0x4C, 0x24, 0x30, 0x48, 0x8D, 0x4C, 0x24,
	0x30, 0xE8, 0xA0, 0xD5, 0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x01, 0xCC, 0x33,
	0xC0, 0x48, 0x8B, 0x5C, 0x24, 0x38, 0x48, 0x8B, 0x6C, 0x24,
};
constexpr std::uint8_t ExpectedAddUnitStatTail[] {
	0x57, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x40, 0x45, 0x0F, 0xB7, 0xF1, 0x41,
	0x8B, 0xF8, 0x8B, 0xEA, 0x48, 0x8B, 0xF1, 0x48, 0x85, 0xC9, 0x75, 0x13,
	0x88, 0x4C, 0x24, 0x70, 0x48, 0x8D, 0x4C, 0x24, 0x70, 0xE8, 0x54, 0xEE,
	0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x01, 0xCC, 0x85, 0xFF, 0x74, 0x5B, 0x48,
	0x8B, 0xCE, 0x48, 0x89, 0x5C, 0x24, 0x60, 0xE8, 0x3E, 0x83,
};

// 00436830: the game's skill payment entry, a loader jump into D2RCore
// ConsumeWideSkillResource. Only checked, to confirm skill casts reach the
// D2RCore routine this plugin replaces.
constexpr std::uint64_t SkillPayRva = 0x00436830ULL;
constexpr std::uint8_t ExpectedSkillPayTail[] {
	0x90, 0x90, 0x90, 0x90, 0x56, 0x57, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x40,
	0x48, 0x8B, 0xF1, 0x45, 0x8B, 0xF1,
};

// ---------------------------------------------------------------------------
// D2RCore.dll 1.3.1: the skill route
// ---------------------------------------------------------------------------

constexpr wchar_t CoreModuleName[]     = L"D2RCore.dll";
constexpr char    CoreSkillPayExport[] = "ConsumeWideSkillResource";

// ConsumeWideSkillResource, RVA 00833F90, 94 bytes.
//   +02Eh call payment body (003DD6E0)
constexpr std::uint8_t CoreSkillPayStub[] {
	0x48, 0x83, 0xEC, 0x38, 0x48, 0x8B, 0x05, 0xE5, 0x75, 0xEC, 0xFF, 0x48,
	0x31, 0xE0, 0x48, 0x89, 0x44, 0x24, 0x30, 0x48, 0x85, 0xC9, 0x74, 0x1D,
	0x48, 0x89, 0x4C, 0x24, 0x20, 0x48, 0x8D, 0x05, 0x5C, 0xFE, 0xB9, 0xFF,
	0x48, 0x89, 0x44, 0x24, 0x28, 0x48, 0x8D, 0x4C, 0x24, 0x20, 0xE8, 0x1D,
	0x97, 0xBA, 0xFF, 0xEB, 0x02, 0x31, 0xC0, 0x48, 0x8B, 0x4C, 0x24, 0x30,
	0x48, 0x31, 0xE1, 0x48, 0x8B, 0x15, 0xAA, 0x75, 0xEC, 0xFF, 0x48, 0x39,
	0xCA, 0x75, 0x05, 0x48, 0x83, 0xC4, 0x38, 0xC3, 0x48, 0x8B, 0x4C, 0x24,
	0x30, 0x48, 0x31, 0xE1, 0xE8, 0x43, 0x35, 0xD0, 0xFF, 0xCC,
};
constexpr ByteGap CoreSkillPayStubGaps[] {
	{ 0x007, 4 },
	{ 0x020, 4 },
	{ 0x02F, 4 },
	{ 0x042, 4 },
	{ 0x059, 4 },
};
constexpr std::size_t CoreStubBodyCallOffset = 0x2E;

// Payment body, RVA 003DD6E0, 527 bytes. (context, unit, skill id, level).
//   +140h call [slot 0033AA00]                   the skill's cost
//   +161h mov edx, 72h / +166h call [slot 003351B0]
//   +199h jmp 003DDB50                           state 114: life payment
constexpr std::uint8_t CoreSkillPayBody[] {
	0x41, 0x57, 0x41, 0x56, 0x56, 0x57, 0x55, 0x53, 0x48, 0x83, 0xEC, 0x48,
	0x48, 0x8B, 0x05, 0x8D, 0xDE, 0x31, 0x00, 0x48, 0x31, 0xE0, 0x48, 0x89,
	0x44, 0x24, 0x40, 0x31, 0xC0, 0x48, 0x85, 0xD2, 0x74, 0x0A, 0x83, 0x3A,
	0x00, 0x74, 0x2A, 0xB8, 0x01, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x4C, 0x24,
	0x40, 0x48, 0x31, 0xE1, 0x48, 0x8B, 0x15, 0x65, 0xDE, 0x31, 0x00, 0x48,
	0x39, 0xCA, 0x0F, 0x85, 0xBD, 0x01, 0x00, 0x00, 0x48, 0x83, 0xC4, 0x48,
	0x5B, 0x5D, 0x5F, 0x5E, 0x41, 0x5E, 0x41, 0x5F, 0xC3, 0x48, 0x89, 0xCE,
	0x44, 0x89, 0xCD, 0x44, 0x89, 0xC3, 0x48, 0x89, 0xD7, 0x48, 0x89, 0xD1,
	0xFF, 0x15, 0xAA, 0x17, 0x33, 0x00, 0x48, 0x85, 0xC0, 0x0F, 0x84, 0xC2,
	0x00, 0x00, 0x00, 0x49, 0x89, 0xC7, 0x48, 0x89, 0xC1, 0xFF, 0x15, 0x3D,
	0x18, 0x33, 0x00, 0x39, 0xD8, 0x0F, 0x85, 0xAE, 0x00, 0x00, 0x00, 0x4C,
	0x89, 0xF9, 0xFF, 0x15, 0xCC, 0x17, 0x33, 0x00, 0x83, 0xF8, 0xFF, 0x0F,
	0x84, 0x9C, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x0E, 0xBA, 0x04, 0x00, 0x00,
	0x00, 0x41, 0x89, 0xC0, 0xFF, 0x15, 0x6A, 0x2D, 0x32, 0x00, 0x49, 0x89,
	0xC6, 0x48, 0x85, 0xC0, 0xB8, 0x00, 0x00, 0x00, 0x00, 0x0F, 0x84, 0x75,
	0xFF, 0xFF, 0xFF, 0x41, 0x8B, 0x6F, 0x40, 0x0F, 0xB6, 0x8F, 0xBD, 0x01,
	0x00, 0x00, 0x31, 0xD2, 0xFF, 0x15, 0xD6, 0x44, 0x32, 0x00, 0x89, 0xC1,
	0x31, 0xC0, 0x85, 0xED, 0x0F, 0x4E, 0xE8, 0x39, 0xE9, 0x0F, 0x4C, 0xE9,
	0x48, 0x83, 0x7E, 0x08, 0x00, 0x0F, 0x84, 0x49, 0xFF, 0xFF, 0xFF, 0x48,
	0x89, 0xF9, 0xFF, 0x15, 0xFC, 0x2D, 0x32, 0x00, 0x48, 0x89, 0xC1, 0x31,
	0xC0, 0x48, 0x8B, 0x56, 0x08, 0x48, 0x85, 0xD2, 0x0F, 0x84, 0x2E, 0xFF,
	0xFF, 0xFF, 0x48, 0x8B, 0x06, 0x0F, 0xB6, 0x80, 0x06, 0x01, 0x00, 0x00,
	0x88, 0x44, 0x24, 0x30, 0x48, 0x89, 0x54, 0x24, 0x38, 0x48, 0x89, 0x4C,
	0x24, 0x20, 0x48, 0x8D, 0x4C, 0x24, 0x30, 0x4C, 0x89, 0xF2, 0x41, 0x89,
	0xD8, 0x41, 0x89, 0xE9, 0xE8, 0x57, 0x1B, 0x00, 0x00, 0x0F, 0xB6, 0xC0,
	0xE9, 0xFB, 0xFE, 0xFF, 0xFF, 0x48, 0x8B, 0x06, 0x0F, 0xB6, 0x88, 0x06,
	0x01, 0x00, 0x00, 0x89, 0xDA, 0x41, 0x89, 0xE8, 0xFF, 0x15, 0x8A, 0x17,
	0x33, 0x00, 0x89, 0xC3, 0x85, 0xC0, 0xB8, 0x00, 0x00, 0x00, 0x00, 0x0F,
	0x8E, 0xD7, 0xFE, 0xFF, 0xFF, 0x83, 0x3F, 0x00, 0x0F, 0x85, 0xC9, 0xFE,
	0xFF, 0xFF, 0x48, 0x89, 0xF9, 0xBA, 0x72, 0x00, 0x00, 0x00, 0xFF, 0x15,
	0x4C, 0x26, 0x32, 0x00, 0x85, 0xC0, 0x74, 0x2E, 0x48, 0x8B, 0x44, 0x24,
	0x40, 0x48, 0x31, 0xE0, 0x48, 0x8B, 0x0D, 0x21, 0xDD, 0x31, 0x00, 0x48,
	0x39, 0xC1, 0x75, 0x7D, 0x48, 0x89, 0xF1, 0x48, 0x89, 0xFA, 0x41, 0x89,
	0xD8, 0x48, 0x83, 0xC4, 0x48, 0x5B, 0x5D, 0x5F, 0x5E, 0x41, 0x5E, 0x41,
	0x5F, 0xE9, 0xD2, 0x02, 0x00, 0x00, 0x48, 0x89, 0xF9, 0xBA, 0x08, 0x00,
	0x00, 0x00, 0x45, 0x31, 0xC0, 0xE8, 0x42, 0xB3, 0xFF, 0xFF, 0x89, 0xC1,
	0x31, 0xC0, 0x39, 0xD9, 0x0F, 0x8C, 0x72, 0xFE, 0xFF, 0xFF, 0x48, 0x8B,
	0x05, 0xFF, 0x71, 0x32, 0x00, 0x80, 0x38, 0x00, 0x0F, 0x85, 0x5D, 0xFE,
	0xFF, 0xFF, 0xF7, 0xDB, 0x0F, 0xB6, 0x87, 0xBD, 0x01, 0x00, 0x00, 0x88,
	0x44, 0x24, 0x30, 0xFF, 0x15, 0xCB, 0x78, 0x32, 0x00, 0x48, 0x89, 0x44,
	0x24, 0x38, 0x48, 0x8D, 0x4C, 0x24, 0x30, 0x49, 0xB8, 0x00, 0x00, 0x00,
	0x00, 0x08, 0x00, 0x00, 0x00, 0x48, 0x89, 0xFA, 0x41, 0x89, 0xD9, 0xE8,
	0x04, 0xC6, 0xFF, 0xFF, 0xE9, 0x26, 0xFE, 0xFF, 0xFF, 0x48, 0x8B, 0x4C,
	0x24, 0x40, 0x48, 0x31, 0xE1, 0xE8, 0x42, 0x9C, 0x15, 0x00, 0xCC,
};
constexpr ByteGap CoreSkillPayBodyGaps[] {
	{ 0x00F, 4 },
	{ 0x037, 4 },
	{ 0x062, 4 },
	{ 0x077, 4 },
	{ 0x088, 4 },
	{ 0x0A2, 4 },
	{ 0x0C6, 4 },
	{ 0x0E8, 4 },
	{ 0x125, 4 },
	{ 0x142, 4 },
	{ 0x168, 4 },
	{ 0x17B, 4 },
	{ 0x19A, 4 },
	{ 0x1AA, 4 },
	{ 0x1BD, 4 },
	{ 0x1D9, 4 },
	{ 0x1F8, 4 },
	{ 0x20A, 4 },
};
constexpr std::size_t CoreBodyCostCallOffset  = 0x140;
constexpr std::size_t CoreBodyStateArgOffset  = 0x161;
constexpr std::size_t CoreBodyStateCallOffset = 0x166;
constexpr std::size_t CoreBodyBloodJumpOffset = 0x199;

// Blood mana life payment, RVA 003DDB50, 473 bytes. (context, unit, cost).
// The context is never read.
//   +01Fh mov edx, 72h / +024h call [slot 002F5940]
//   +036h call [slot 002F56A0]                   granting skill id
//   +0CCh mov r9d, 100h                          life = 256 on the refuse path
//   +13Dh call [slot 00097790]                   granting skill's record
//   +148h mov edi, [rax+1C8h] / shl edi, 8       threshold
constexpr std::uint8_t CoreBloodPay[] {
	0x56, 0x57, 0x53, 0x48, 0x83, 0xEC, 0x40, 0x44, 0x89, 0xC7, 0x48, 0x89,
	0xD6, 0x48, 0x8B, 0x05, 0x1C, 0xDA, 0x31, 0x00, 0x48, 0x31, 0xE0, 0x48,
	0x89, 0x44, 0x24, 0x38, 0x48, 0x89, 0xD1, 0xBA, 0x72, 0x00, 0x00, 0x00,
	0xFF, 0x15, 0x8E, 0x10, 0x32, 0x00, 0x48, 0x85, 0xC0, 0x0F, 0x84, 0xA4,
	0x00, 0x00, 0x00, 0x48, 0x89, 0xC1, 0xFF, 0x15, 0x54, 0x11, 0x32, 0x00,
	0x89, 0xC3, 0x48, 0x89, 0xF1, 0xBA, 0x06, 0x00, 0x00, 0x00, 0x45, 0x31,
	0xC0, 0xE8, 0x32, 0xB0, 0xFF, 0xFF, 0x39, 0xF8, 0x0F, 0x8D, 0xA3, 0x00,
	0x00, 0x00, 0x48, 0x89, 0xF1, 0xBA, 0x72, 0x00, 0x00, 0x00, 0xFF, 0x15,
	0x54, 0x10, 0x32, 0x00, 0x0F, 0xB6, 0x9E, 0xBD, 0x01, 0x00, 0x00, 0x48,
	0x85, 0xC0, 0x74, 0x3B, 0x48, 0x89, 0xC7, 0x83, 0x78, 0x1C, 0x00, 0x78,
	0x32, 0x88, 0x5C, 0x24, 0x28, 0xFF, 0x15, 0xB5, 0x75, 0x32, 0x00, 0x48,
	0x89, 0x44, 0x24, 0x30, 0x48, 0x8D, 0x4C, 0x24, 0x28, 0x48, 0x89, 0xFA,
	0xE8, 0xFB, 0xCA, 0xFF, 0xFF, 0x80, 0x4F, 0x1D, 0x20, 0x89, 0xD9, 0x48,
	0x89, 0xFA, 0xFF, 0x15, 0xBC, 0x10, 0x32, 0x00, 0x0F, 0xB6, 0x9E, 0xBD,
	0x01, 0x00, 0x00, 0x88, 0x5C, 0x24, 0x28, 0xFF, 0x15, 0x83, 0x75, 0x32,
	0x00, 0x48, 0x89, 0x44, 0x24, 0x30, 0x48, 0x8D, 0x4C, 0x24, 0x28, 0x49,
	0xB8, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x48, 0x89, 0xF2,
	0x41, 0xB9, 0x00, 0x01, 0x00, 0x00, 0xE8, 0xC9, 0xBF, 0xFF, 0xFF, 0x31,
	0xC0, 0x48, 0x8B, 0x4C, 0x24, 0x38, 0x48, 0x31, 0xE1, 0x48, 0x8B, 0x15,
	0x48, 0xD9, 0x31, 0x00, 0x48, 0x39, 0xCA, 0x0F, 0x85, 0xDB, 0x00, 0x00,
	0x00, 0x48, 0x83, 0xC4, 0x40, 0x5B, 0x5F, 0x5E, 0xC3, 0x48, 0x85, 0xF6,
	0x74, 0x36, 0x85, 0xFF, 0x74, 0x32, 0xF7, 0xDF, 0x0F, 0xB6, 0x86, 0xBD,
	0x01, 0x00, 0x00, 0x88, 0x44, 0x24, 0x28, 0xFF, 0x15, 0x23, 0x75, 0x32,
	0x00, 0x48, 0x89, 0x44, 0x24, 0x30, 0x48, 0x8D, 0x4C, 0x24, 0x28, 0x49,
	0xB8, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x48, 0x89, 0xF2,
	0x41, 0x89, 0xF9, 0xE8, 0x5C, 0xC2, 0xFF, 0xFF, 0x0F, 0xB6, 0x8E, 0xBD,
	0x01, 0x00, 0x00, 0x89, 0xDA, 0xFF, 0x15, 0x05, 0x14, 0x33, 0x00, 0x48,
	0x85, 0xC0, 0x74, 0x0B, 0x8B, 0xB8, 0xC8, 0x01, 0x00, 0x00, 0xC1, 0xE7,
	0x08, 0xEB, 0x02, 0x31, 0xFF, 0x48, 0x89, 0xF1, 0xBA, 0x06, 0x00, 0x00,
	0x00, 0x45, 0x31, 0xC0, 0xE8, 0x1B, 0xAF, 0xFF, 0xFF, 0x89, 0xC1, 0xB8,
	0x01, 0x00, 0x00, 0x00, 0x39, 0xF9, 0x0F, 0x8D, 0x65, 0xFF, 0xFF, 0xFF,
	0x48, 0x89, 0xF1, 0xBA, 0x72, 0x00, 0x00, 0x00, 0xFF, 0x15, 0x36, 0x0F,
	0x32, 0x00, 0x48, 0x85, 0xC0, 0x74, 0x3B, 0x48, 0x89, 0xC7, 0x83, 0x78,
	0x1C, 0x00, 0x78, 0x32, 0x0F, 0xB6, 0x9E, 0xBD, 0x01, 0x00, 0x00, 0x88,
	0x5C, 0x24, 0x28, 0xFF, 0x15, 0x97, 0x74, 0x32, 0x00, 0x48, 0x89, 0x44,
	0x24, 0x30, 0x48, 0x8D, 0x4C, 0x24, 0x28, 0x48, 0x89, 0xFA, 0xE8, 0xDD,
	0xC9, 0xFF, 0xFF, 0x80, 0x4F, 0x1D, 0x20, 0x89, 0xD9, 0x48, 0x89, 0xFA,
	0xFF, 0x15, 0x9E, 0x0F, 0x32, 0x00, 0xB8, 0x01, 0x00, 0x00, 0x00, 0xE9,
	0x0D, 0xFF, 0xFF, 0xFF, 0x48, 0x8B, 0x4C, 0x24, 0x38, 0x48, 0x31, 0xE1,
	0xE8, 0x07, 0x98, 0x15, 0x00,
};
constexpr ByteGap CoreBloodPayGaps[] {
	{ 0x010, 4 },
	{ 0x026, 4 },
	{ 0x038, 4 },
	{ 0x04A, 4 },
	{ 0x060, 4 },
	{ 0x07F, 4 },
	{ 0x091, 4 },
	{ 0x0A0, 4 },
	{ 0x0B1, 4 },
	{ 0x0D3, 4 },
	{ 0x0E4, 4 },
	{ 0x111, 4 },
	{ 0x130, 4 },
	{ 0x13F, 4 },
	{ 0x161, 4 },
	{ 0x17E, 4 },
	{ 0x19D, 4 },
	{ 0x1AF, 4 },
	{ 0x1BE, 4 },
	{ 0x1D5, 4 },
};
constexpr std::size_t CoreBloodStateArgOffset   = 0x1F;
constexpr std::size_t CoreBloodFindListOffset   = 0x24;
constexpr std::size_t CoreBloodListSkillOffset  = 0x36;
constexpr std::size_t CoreBloodLifeFloorOffset  = 0xCC;
constexpr std::size_t CoreBloodRecordCallOffset = 0x13D;
constexpr std::size_t CoreBloodThresholdOffset  = 0x148;

// jmp qword ptr [rip+0] / dq target
constexpr std::size_t CoreJumpBytes        = 14;
constexpr std::size_t CoreJumpTargetOffset = 6;

static_assert(sizeof(CoreSkillPayStub) == 94 && GapsFit(CoreSkillPayStub, CoreSkillPayStubGaps), "Stub window or gaps changed.");
static_assert(CoreSkillPayStub[CoreStubBodyCallOffset] == 0xE8 && HasGapAt(CoreSkillPayStubGaps, CoreStubBodyCallOffset + 1), "The stub must call the payment body through a skipped rel32.");
static_assert(sizeof(CoreSkillPayBody) == 527 && GapsFit(CoreSkillPayBody, CoreSkillPayBodyGaps), "Body window or gaps changed.");
static_assert(CoreSkillPayBody[CoreBodyCostCallOffset] == 0xFF && CoreSkillPayBody[CoreBodyCostCallOffset + 1] == 0x15 && HasGapAt(CoreSkillPayBodyGaps, CoreBodyCostCallOffset + 2), "+140h must be call qword ptr [rip+disp32].");
static_assert(CoreSkillPayBody[CoreBodyStateArgOffset] == 0xBA && CoreSkillPayBody[CoreBodyStateArgOffset + 1] == StateBloodMana, "+161h must be mov edx, 72h.");
static_assert(CoreSkillPayBody[CoreBodyStateCallOffset] == 0xFF && CoreSkillPayBody[CoreBodyStateCallOffset + 1] == 0x15 && HasGapAt(CoreSkillPayBodyGaps, CoreBodyStateCallOffset + 2), "+166h must be call qword ptr [rip+disp32].");
static_assert(CoreSkillPayBody[CoreBodyBloodJumpOffset] == 0xE9 && HasGapAt(CoreSkillPayBodyGaps, CoreBodyBloodJumpOffset + 1), "+199h must jump to the life payment through a skipped rel32.");
static_assert(sizeof(CoreBloodPay) == 473 && GapsFit(CoreBloodPay, CoreBloodPayGaps), "Life payment window or gaps changed.");
static_assert(CoreBloodPay[CoreBloodStateArgOffset] == 0xBA && CoreBloodPay[CoreBloodStateArgOffset + 1] == StateBloodMana, "+1Fh must be mov edx, 72h.");
static_assert(CoreBloodPay[CoreBloodFindListOffset] == 0xFF && CoreBloodPay[CoreBloodFindListOffset + 1] == 0x15 && HasGapAt(CoreBloodPayGaps, CoreBloodFindListOffset + 2), "+24h must be call qword ptr [rip+disp32].");
static_assert(CoreBloodPay[CoreBloodListSkillOffset] == 0xFF && CoreBloodPay[CoreBloodListSkillOffset + 1] == 0x15 && HasGapAt(CoreBloodPayGaps, CoreBloodListSkillOffset + 2), "+36h must be call qword ptr [rip+disp32].");
static_assert(CoreBloodPay[CoreBloodLifeFloorOffset] == 0x41 && CoreBloodPay[CoreBloodLifeFloorOffset + 1] == 0xB9 && CoreBloodPay[CoreBloodLifeFloorOffset + 2] == 0x00 && CoreBloodPay[CoreBloodLifeFloorOffset + 3] == 0x01, "+CCh must be mov r9d, 100h.");
static_assert(CoreBloodPay[CoreBloodRecordCallOffset] == 0xFF && CoreBloodPay[CoreBloodRecordCallOffset + 1] == 0x15 && HasGapAt(CoreBloodPayGaps, CoreBloodRecordCallOffset + 2), "+13Dh must be call qword ptr [rip+disp32].");
static_assert(CoreBloodPay[CoreBloodThresholdOffset] == 0x8B && CoreBloodPay[CoreBloodThresholdOffset + 1] == 0xB8 && CoreBloodPay[CoreBloodThresholdOffset + 2] == 0xC8 && CoreBloodPay[CoreBloodThresholdOffset + 3] == 0x01, "+148h must read the threshold at record+1C8h.");
// Nothing in the routine branches back into the bytes the jump replaces.
static_assert(CoreBloodPay[0] == 0x56 && CoreBloodPay[1] == 0x57 && CoreBloodPay[2] == 0x53, "The routine must start push rsi / push rdi / push rbx.");

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

using GetUnitStatFn = std::int32_t(__fastcall*)(void* unit, std::int32_t stat, std::int32_t layer);
using AddUnitStatFn = void(__fastcall*)(void* unit, std::int32_t stat, std::int32_t value, std::int32_t layer);

const D2RL::PluginContext* Context     = nullptr;
std::uintptr_t             ExeBase     = 0;
GetUnitStatFn              GetUnitStat = nullptr;
AddUnitStatFn              AddUnitStat = nullptr;

HMODULE                                  CoreModule     = nullptr;
std::uint8_t*                            CoreBloodEntry = nullptr;
std::array<std::uint8_t, CoreJumpBytes>  CoreBloodOriginal {};
std::array<std::uint8_t, CoreJumpBytes>  CoreBloodWritten {};

const char* SkillStatus = "not installed";
const char* AuraStatus  = "not installed";

std::atomic<std::uint64_t> SkillPaid {0};
std::atomic<std::uint64_t> SkillRefused {0};
std::atomic<std::uint64_t> AuraPaid {0};
std::atomic<std::uint64_t> AuraRefused {0};
std::atomic<bool>          SkillRefusalLogged {false};
std::atomic<bool>          AuraRefusalLogged {false};

// ---------------------------------------------------------------------------
// The rule
// ---------------------------------------------------------------------------

// Same contract as both routines it replaces: 1 when the cost was taken from
// life, 0 when it was refused. Never touches the state or forces life.
auto PayInLife(
	void*                       unit,
	std::int32_t                cost,
	std::atomic<std::uint64_t>& paid,
	std::atomic<std::uint64_t>& refused,
	std::atomic<bool>&          refusalLogged,
	const char*                 route) noexcept -> std::int32_t {
	if (unit == nullptr) {
		refused.fetch_add(1, std::memory_order_relaxed);
		return 0;
	}
	const std::int32_t life = GetUnitStat(unit, StatHitpoints, 0);
	if (life < cost) {
		refused.fetch_add(1, std::memory_order_relaxed);
		if (!refusalLogged.exchange(true, std::memory_order_relaxed) && Context != nullptr) {
			char message[160] {};
			std::snprintf(message, sizeof(message),
			              "First %s cost refused this session: life %d below cost %d (both in 1/256). Nothing was taken.",
			              route, life, cost);
			Context->LogInfo(message);
		}
		return 0;
	}
	if (cost != 0) {
		// neg, as both originals do it.
		AddUnitStat(unit, StatHitpoints, static_cast<std::int32_t>(0U - static_cast<std::uint32_t>(cost)), 0);
	}
	paid.fetch_add(1, std::memory_order_relaxed);
	return 1;
}

// D2RCore 003DDB50, entered by the payment body's tail jump: rcx context
// (unused), rdx unit, r8d cost. Returns to the caller of the export.
auto __fastcall SkillBloodPay(void* context, void* unit, std::int32_t cost) noexcept -> std::int32_t {
	(void)context;
	return PayInLife(unit, cost, SkillPaid, SkillRefused, SkillRefusalLogged, "skill");
}

// Game 00584E80, entered from the aura payment's tail jump: rcx unit, edx cost.
auto __fastcall AuraBloodPay(void* unit, std::int32_t cost) noexcept -> std::int32_t {
	return PayInLife(unit, cost, AuraPaid, AuraRefused, AuraRefusalLogged, "aura");
}

// ---------------------------------------------------------------------------
// D2RCore access
// ---------------------------------------------------------------------------

// Committed, accessible memory, executable when `code` is set. With
// `inCore`, it must also belong to D2RCore.dll.
auto IsMapped(const std::uint8_t* address, std::size_t size, bool code, bool inCore) noexcept -> bool {
	if (address == nullptr) {
		return false;
	}
	MEMORY_BASIC_INFORMATION info {};
	if (VirtualQuery(address, &info, sizeof(info)) != sizeof(info)) {
		return false;
	}
	if (info.State != MEM_COMMIT || (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) {
		return false;
	}
	if (inCore && (CoreModule == nullptr || info.AllocationBase != static_cast<void*>(CoreModule))) {
		return false;
	}
	const auto* regionEnd = static_cast<const std::uint8_t*>(info.BaseAddress) + info.RegionSize;
	if (address + size > regionEnd) {
		return false;
	}
	if (!code) {
		return true;
	}
	const DWORD executable = PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
	return (info.Protect & executable) != 0;
}

template <std::size_t Size, std::size_t GapCount>
auto CoreMatches(const std::uint8_t* at, const std::uint8_t (&expected)[Size], const ByteGap (&gaps)[GapCount]) noexcept -> bool {
	if (!IsMapped(at, Size, true, true)) {
		return false;
	}
	for (std::size_t index = 0; index < Size; ++index) {
		bool skipped = false;
		for (const ByteGap& gap : gaps) {
			if (index >= gap.offset && index < gap.offset + gap.size) {
				skipped = true;
				break;
			}
		}
		if (!skipped && at[index] != expected[index]) {
			return false;
		}
	}
	return true;
}

// Target of a verified instruction whose rip-relative operand ends it.
auto Rel32Target(const std::uint8_t* instruction, std::size_t displacementOffset, std::size_t length) noexcept -> const std::uint8_t* {
	std::int32_t displacement = 0;
	std::memcpy(&displacement, instruction + displacementOffset, sizeof(displacement));
	return reinterpret_cast<const std::uint8_t*>(
		reinterpret_cast<std::uintptr_t>(instruction) + length
		+ static_cast<std::uintptr_t>(static_cast<std::intptr_t>(displacement)));
}

// A call qword ptr [rip+disp32] whose slot is D2RCore's entry for the game
// function at `rva`. Each entry is { address, rva, address }; the address is
// zero until D2RCore resolves it.
auto CoreSlotNames(const std::uint8_t* call, std::uint64_t rva) noexcept -> bool {
	const auto* slot = Rel32Target(call, 2, 6);
	if (!IsMapped(slot, 2 * sizeof(std::uint64_t), false, true)) {
		return false;
	}
	std::uint64_t address = 0;
	std::uint64_t slotRva = 0;
	std::memcpy(&address, slot, sizeof(address));
	std::memcpy(&slotRva, slot + sizeof(address), sizeof(slotRva));
	return slotRva == rva && (address == 0 || address == ExeBase + rva);
}

auto LocateCoreBloodPay(const std::uint8_t*& stubOut, const char*& why) noexcept -> std::uint8_t* {
	CoreModule = GetModuleHandleW(CoreModuleName);
	if (CoreModule == nullptr) {
		why = "D2RCore.dll is not loaded";
		return nullptr;
	}
	const auto* stub = reinterpret_cast<const std::uint8_t*>(GetProcAddress(CoreModule, CoreSkillPayExport));
	if (stub == nullptr) {
		why = "D2RCore.dll has no ConsumeWideSkillResource export";
		return nullptr;
	}
	if (!CoreMatches(stub, CoreSkillPayStub, CoreSkillPayStubGaps)) {
		why = "ConsumeWideSkillResource does not match the 1.3.1 export";
		return nullptr;
	}
	const auto* body = Rel32Target(stub + CoreStubBodyCallOffset, 1, 5);
	if (!CoreMatches(body, CoreSkillPayBody, CoreSkillPayBodyGaps)) {
		why = "the skill payment body does not match 1.3.1";
		return nullptr;
	}
	if (!CoreSlotNames(body + CoreBodyCostCallOffset, GetManaCostRva)
		|| !CoreSlotNames(body + CoreBodyStateCallOffset, CheckStateRva)) {
		why = "the skill payment body does not call the game's cost and state checks";
		return nullptr;
	}
	const auto* blood = Rel32Target(body + CoreBodyBloodJumpOffset, 1, 5);
	if (!CoreMatches(blood, CoreBloodPay, CoreBloodPayGaps)) {
		why = "the life payment does not match 1.3.1";
		return nullptr;
	}
	if (!CoreSlotNames(blood + CoreBloodFindListOffset, FindStateListRva)
		|| !CoreSlotNames(blood + CoreBloodListSkillOffset, StateListSkillIdRva)
		|| !CoreSlotNames(blood + CoreBloodRecordCallOffset, SkillsRecordRva)) {
		why = "the life payment does not call the game's state and skill lookups";
		return nullptr;
	}
	stubOut = stub;
	return const_cast<std::uint8_t*>(blood);
}

auto WriteCode(std::uint8_t* at, const std::uint8_t* bytes, std::size_t size) noexcept -> bool {
	DWORD previous = 0;
	if (!VirtualProtect(at, size, PAGE_EXECUTE_READWRITE, &previous)) {
		return false;
	}
	std::memcpy(at, bytes, size);
	DWORD ignored = 0;
	VirtualProtect(at, size, previous, &ignored);
	FlushInstructionCache(GetCurrentProcess(), at, size);
	return std::memcmp(at, bytes, size) == 0;
}

// The game's skill payment entry must jump into the export located above.
auto SkillEntryReaches(const D2RL::PluginContext* context, const std::uint8_t* stub) noexcept -> bool {
	if (!context->CheckExpectedBytes(SkillPayRva, LoaderJump, ByteCount(LoaderJump))
		|| !context->CheckExpectedBytes(SkillPayRva + LoaderJumpBytes, ExpectedSkillPayTail, ByteCount(ExpectedSkillPayTail))) {
		return false;
	}
	const auto* entry = reinterpret_cast<const std::uint8_t*>(ExeBase + SkillPayRva);
	const auto* slot  = Rel32Target(entry, 2, LoaderJumpBytes);
	if (!IsMapped(slot, sizeof(std::uint64_t), false, false)) {
		return false;
	}
	std::uint64_t target = 0;
	std::memcpy(&target, slot, sizeof(target));
	return target == reinterpret_cast<std::uint64_t>(stub);
}

// ---------------------------------------------------------------------------
// Install
// ---------------------------------------------------------------------------

auto BindStatFunctions(const D2RL::PluginContext* context) noexcept -> bool {
	if (!context->CheckExpectedBytes(GetUnitStatRva, LoaderJump, ByteCount(LoaderJump))
		|| !context->CheckExpectedBytes(GetUnitStatRva + LoaderJumpBytes, ExpectedGetUnitStatTail, ByteCount(ExpectedGetUnitStatTail))) {
		context->LogError("0x2F5020 is not the stat read the 1.3.1 loader jumps into D2RCore; wrong build. Nothing was hooked.");
		return false;
	}
	if (!context->CheckExpectedBytes(AddUnitStatRva, LoaderJump, ByteCount(LoaderJump))
		|| !context->CheckExpectedBytes(AddUnitStatRva + LoaderJumpBytes, ExpectedAddUnitStatTail, ByteCount(ExpectedAddUnitStatTail))) {
		context->LogError("0x2F34F0 is not the stat add the 1.3.1 loader jumps into D2RCore; wrong build. Nothing was hooked.");
		return false;
	}
	GetUnitStat = reinterpret_cast<GetUnitStatFn>(ExeBase + GetUnitStatRva);
	AddUnitStat = reinterpret_cast<AddUnitStatFn>(ExeBase + AddUnitStatRva);
	return true;
}

void InstallSkillPart(const D2RL::PluginContext* context) noexcept {
	const char*         why  = nullptr;
	const std::uint8_t* stub = nullptr;
	std::uint8_t*       entry = LocateCoreBloodPay(stub, why);
	if (entry == nullptr) {
		SkillStatus = "not installed: D2RCore.dll is not the 1.3.1 build this was made for";
		D2RL::LogErrorF(context, "Skill costs NOT covered: %s. D2RCore.dll was not touched.", why);
		return;
	}
	if (!SkillEntryReaches(context, stub)) {
		context->LogWarn("The game's skill payment entry (0x436830) does not jump into ConsumeWideSkillResource "
		                 "right now. The D2RCore routine is still replaced, but skill costs may not go through it.");
	}

	std::memcpy(CoreBloodOriginal.data(), entry, CoreJumpBytes);
	std::array<std::uint8_t, CoreJumpBytes> jump { 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00 };
	const auto target = reinterpret_cast<std::uint64_t>(&SkillBloodPay);
	std::memcpy(jump.data() + CoreJumpTargetOffset, &target, sizeof(target));
	if (!WriteCode(entry, jump.data(), jump.size())) {
		WriteCode(entry, CoreBloodOriginal.data(), CoreBloodOriginal.size());
		SkillStatus = "not installed: the D2RCore routine could not be written";
		context->LogError("Skill costs NOT covered: writing the jump into D2RCore failed; the original bytes were put back.");
		return;
	}
	CoreBloodWritten = jump;
	CoreBloodEntry   = entry;
	SkillStatus      = "installed";
	D2RL::LogInfoF(context, "Skill costs covered: D2RCore.dll+0x%llX now pays state 114 costs in life only.",
	               static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(entry) - reinterpret_cast<std::uintptr_t>(CoreModule)));
}

void InstallAuraPart(const D2RL::PluginContext* context) noexcept {
	if (!context->CheckExpectedBytes(AuraPayRva, ExpectedAuraPay, ByteCount(ExpectedAuraPay))) {
		AuraStatus = "not installed: 0x436A80 is not the 1.3.1 aura payment";
		context->LogError("Aura costs NOT covered: 0x436A80 is not the aura payment this was made for. Nothing was hooked there.");
		return;
	}
	if (!context->CheckExpectedBytes(NativeBloodPayRva, ExpectedNativeBloodPay, ByteCount(ExpectedNativeBloodPay))) {
		AuraStatus = "not installed: 0x584E80 is not the vanilla life payment";
		context->LogError("Aura costs NOT covered: 0x584E80 is not the vanilla blood mana payment (another plugin may "
		                  "own it). Nothing was hooked there.");
		return;
	}
	if (!context->InstallInlineHook(NativeBloodPayRva,
	                                NativeBloodPayPrologue,
	                                ByteCount(NativeBloodPayPrologue),
	                                &AuraBloodPay)) {
		AuraStatus = "not installed: the loader refused the inline hook";
		context->LogError("Aura costs NOT covered: the inline hook at 0x584E80 failed.");
		return;
	}
	AuraStatus = "installed";
	context->LogInfo("Aura costs covered: 0x584E80 now pays state 114 costs in life only.");
}

// ---------------------------------------------------------------------------
// Config
// ---------------------------------------------------------------------------

auto IsBlank(char value) noexcept -> bool {
	return value == ' ' || value == '\t' || value == '\r';
}

auto FindConfigValue(const char* text, const char* key, const char*& value, std::size_t& length) noexcept -> bool {
	if (text == nullptr || key == nullptr) {
		return false;
	}
	const std::size_t keyLength = std::strlen(key);
	for (const char* line = text; *line != '\0';) {
		const char* cursor = line;
		while (IsBlank(*cursor)) {
			++cursor;
		}
		if (std::strncmp(cursor, key, keyLength) == 0) {
			const char* after = cursor + keyLength;
			while (IsBlank(*after)) {
				++after;
			}
			if (*after == '=') {
				++after;
				while (IsBlank(*after)) {
					++after;
				}
				const char* end = after;
				while (*end != '\0' && *end != '\n' && *end != '#') {
					++end;
				}
				while (end > after && IsBlank(end[-1])) {
					--end;
				}
				value  = after;
				length = static_cast<std::size_t>(end - after);
				return true;
			}
		}
		while (*line != '\0' && *line != '\n') {
			++line;
		}
		if (*line == '\n') {
			++line;
		}
	}
	return false;
}

auto ReadBool(const char* text, const char* key, bool fallback) noexcept -> bool {
	const char* value  = nullptr;
	std::size_t length = 0;
	if (!FindConfigValue(text, key, value, length)) {
		return fallback;
	}
	if (length == 4 && std::strncmp(value, "true", 4) == 0) {
		return true;
	}
	if (length == 5 && std::strncmp(value, "false", 5) == 0) {
		return false;
	}
	return fallback;
}

// Written by EnsureConfig when the file does not exist yet. It is the plugin's
// documentation as shipped.
constexpr char DefaultConfigToml[] = R"toml(# celestialrayone.blood-magic
#
# Blood Magic
#
# Built for D2RLoader 1.3.1 (Diablo II: Resurrected 3.3). Read when the plugin
# loads; restart the game after editing.
#
# What goes wrong without it
#   State 114 (blood_mana) makes skills cost life instead of mana. The engine
#   still runs it as the old Blood Mana monster curse, which ends itself:
#     - a cost higher than current life: state 114 is removed, life is set
#       to 1, and the cost is not paid;
#     - a payment that leaves life below a threshold taken from the skill
#       that granted state 114: state 114 is removed.
#   With state 114 gone, skills cost mana again. A player whose life is below
#   a skill's cost can then keep casting it, paying mana.
#   Skill casts and auras that cost something every tick both work this way.
#   The aura one also crashes the game when state 114 is still set but its
#   stat list is already gone.
#
# What the plugin does
#   While a player has state 114, every skill and aura cost is paid from life:
#     - life below the cost: refused, nothing is taken, no mana either;
#     - otherwise the cost is taken from life.
#   A payment never removes state 114 and never sets life to 1. The state
#   still ends when whatever gave it removes it.
#
# Not changed
#   The cast check, which already refuses a cast when life is below the cost
#   while state 114 is on. Players without state 114, monsters, and skills
#   cast from item charges.
#
# Note
#   State 114 is also the Blood Mana curse cast by Succubi and Baal. It now
#   lasts its full duration instead of ending at low life.
#
# Console
#   bloodmagic   which parts are installed, and how many costs were paid in
#                life or refused this session.
#
# Log: d2rloader\logs\celestialrayone.blood-magic.log

# Turn the plugin on or off. Off leaves the game exactly as stock D2RLoader
# runs it.
enabled = true
)toml";

auto LoadEnabled(const D2RL::PluginContext* context) noexcept -> bool {
	if (!context->EnsureConfig(DefaultConfigToml)) {
		context->LogWarn("Could not create celestialrayone.blood-magic.toml; using defaults.");
		return true;
	}
	std::array<char, 8'192> buffer {};
	if (!context->ReadConfig(buffer.data(), static_cast<std::uint32_t>(buffer.size()))) {
		context->LogWarn("Could not read celestialrayone.blood-magic.toml; using defaults.");
		return true;
	}
	buffer.back() = '\0';
	return ReadBool(buffer.data(), "enabled", true);
}

// ---------------------------------------------------------------------------
// Console
// ---------------------------------------------------------------------------

auto __cdecl BloodMagicCommand(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command, void*) noexcept
	-> D2RL::ConsoleCommandResult {
	char line[200] {};
	command->plugin->WriteConsoleMessage("Blood Magic 1.0.0, state 114 costs paid in life only.");
	std::snprintf(line, sizeof(line), "  Skill costs (D2RCore life payment): %s.", SkillStatus);
	command->plugin->WriteConsoleMessage(line);
	std::snprintf(line, sizeof(line), "  Aura costs (0x584E80): %s.", AuraStatus);
	command->plugin->WriteConsoleMessage(line);
	std::snprintf(line, sizeof(line), "  This session: skills %llu paid in life, %llu refused; auras %llu paid in life, %llu refused.",
	              static_cast<unsigned long long>(SkillPaid.load(std::memory_order_relaxed)),
	              static_cast<unsigned long long>(SkillRefused.load(std::memory_order_relaxed)),
	              static_cast<unsigned long long>(AuraPaid.load(std::memory_order_relaxed)),
	              static_cast<unsigned long long>(AuraRefused.load(std::memory_order_relaxed)));
	command->plugin->WriteConsoleMessage(line);
	return D2RL::ConsoleCommandResult::Handled;
}

constexpr D2RL::PluginInfo BloodMagicInfo {
	.infoSize    = D2RL::PluginInfoSize,
	.abiVersion  = D2RL_PLUGIN_ABI_VERSION,
	.id          = "celestialrayone.blood-magic",
	.name        = "Blood Magic",
	.version     = "1.0.0",
	.author      = "CelestialRayOne",
	.description = "While state 114 (blood_mana) is on, skill and aura costs are paid from life only: a cost above "
	               "current life is refused, mana is never used, and a payment never ends the state.",
	.flags       = D2RL::PluginFlags::Server | D2RL::PluginFlags::NativeHooks,
};

}  // namespace

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* {
	return &BloodMagicInfo;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
	if (context == nullptr) {
		return false;
	}
	if (!context->RegisterConsoleCommand("bloodmagic", &BloodMagicCommand,
	                                     "Shows which Blood Magic parts are installed and how many state 114 costs "
	                                     "were paid in life or refused this session.")) {
		context->LogWarn("The bloodmagic console command was refused.");
	}
	if (!LoadEnabled(context)) {
		SkillStatus = "off in the config file";
		AuraStatus  = "off in the config file";
		context->LogInfo("enabled is false; nothing hooked.");
		return true;
	}
	ExeBase = context->exeBase;
	if (ExeBase == 0) {
		context->LogError("exeBase is zero; cannot resolve game addresses.");
		return true;
	}
	if (!BindStatFunctions(context)) {
		SkillStatus = "not installed: wrong game build";
		AuraStatus  = "not installed: wrong game build";
		return true;
	}
	Context = context;
	InstallSkillPart(context);
	InstallAuraPart(context);
	return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
	if (CoreBloodEntry != nullptr
		&& std::memcmp(CoreBloodEntry, CoreBloodWritten.data(), CoreBloodWritten.size()) == 0) {
		WriteCode(CoreBloodEntry, CoreBloodOriginal.data(), CoreBloodOriginal.size());
	}
	CoreBloodEntry = nullptr;
	Context        = nullptr;
}
