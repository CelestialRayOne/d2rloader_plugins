// ---------------------------------------------------------------------------
// Soaring Strike Conversion  -  D2RLoader plugin (D2R 3.3)
//
// Adds a new missiles.txt pSrvDmgFunc (default 18): a line-for-line port of
// D2RLoader's DamageSoaringStrike (pSrvDmgFunc 16) in which the return-trip
// pull roll is replaced by a call to the game's own pSrvDmgFunc 1 (Fire Arrow
// style conversion of physical damage to the missile's EType).
//
// See celestialrayone.soaring-strike-convert.toml for the behaviour write-up.
//
// ---------------------------------------------------------------------------
// WHY THIS IS A PORT AND NOT A COPY OF THE GAME FUNCTION
//
//   pSrvDmgFunc slot 16 in the running game does not point at game code. It
//   points at the forwarding stub j_DamageSoaringStrike (RVA 3E2A79A,
//   `jmp qword ptr [3E2A0E8]`), which lands in D2RCore.dll's export
//   DamageSoaringStrike. The game's own original is still in the image at
//   RVA 454790 with no remaining references, and it behaves differently: it
//   move-assigns an empty damage struct over the hit (sub_140449760), forces
//   ResultFlags 0x20 and has no event, no durability and no txt flags.
//   The version that actually runs, and that the data guide describes, is
//   D2RCore's, so that is what is ported here.
//
//   Source: D2RCore.dll (sha256 2130a98d0b879696116a7ddde5c11ae8c91942b5
//   4b8276db43e02074a715bbc8), export DamageSoaringStrike, RVA 6A3180-6A3361.
//   D2RCore calls game code through records of the form
//   { resolved pointer, game RVA, copy }, bound at start-up as
//   `module base + RVA`. The six records this function uses hold:
//
//     record 5AA678  -> RVA 166040  missiles.txt row for (data context, class)
//     record 5A8D28  -> RVA 48FE80  server unit lookup (game, type, id)
//     record 5B0400  -> RVA 44D570  fire unit event (game, id, source, target, damage)
//     record 5B0418  -> RVA 4242B0  owner's weapon (unit), thunk to 34B000
//     record 5B0430  -> RVA 441B10  missile weapon durability roll (game, unit, item)
//     record 5B03E8  -> RVA 3B4A90  missiles.txt formula evaluator (pull roll only)
//
//   Every RVA was checked against the 3.3 image (module 140000000.D2RLoader.exe):
//   prologues, register use and the callee bodies all match those roles.
//
// ---------------------------------------------------------------------------
// D2RCORE INSTRUCTIONS -> THIS PORT
//
//   6A319D-6A31B6  bail with 0 unless game, missile and damage are all
//                  non-null. The target is never checked.
//   6A31BF         missile unit type [unit+0x00] must be 3.
//   6A31C8-6A31CF  class id [unit+0x04], bail if negative.
//   6A31DB-6A31F2  row = 166040(byte [game+0x106], class), bail if null.
//   6A31F8-6A321D  owner = 48FE80(game, [unit+0xE8], [unit+0xEC]) only when
//                  byte [unit+0x129] bit 0x04 is set, otherwise null. That byte
//                  bit is dword [unit+0x128] bit 0x400, the same test the game's
//                  own owner getter 490300 makes through 34FB00(unit, 1024).
//   6A3220-6A3231  dword [damage+0x00] |= dword [row+0xD0]   (HitFlags)
//                  word  [damage+0x04] |= word  [row+0xD4]   (ResultFlags)
//   6A3235-6A323C  missile data = [unit+0x10], bail with 0 if null.
//   6A3242-6A327A  if dword [data+0x1C] bit 0x04 is clear: when the owner
//                  exists, fire event 15 (game, 15, owner, target, damage) and
//                  re-read [data+0x1C]; then set bit 0x04 either way.
//   6A327F-6A32D0  if dword [row+0x9C] (dParam1) != 0, [data+0x1C] bit 0x10
//                  clear, owner exists, dword [data+0x40] bit 0x40000 clear
//                  (Mirrored Blades copy) and owner type [owner+0x00] == 0
//                  (player): weapon = 4242B0(owner); if non-null, call
//                  441B10(game, owner, weapon) and OR byte [data+0x1C] with 0x10.
//   6A32D3-6A334C  REPLACED. Stock: when dword [data+0x30] bit 0x04 (return
//                  trip) is set and the DmgCalc1 pool offset [row+0xB4] is
//                  not 0, evaluate DmgCalc1 through 3B4A90; when the result
//                  is positive, advance the unit seed at [unit+0x28] and set
//                  ResultFlags 0x40 (pull) when seed % 100 < result.
//                  Here: call pSrvDmgFunc 1, RVA 453490, with the same four
//                  arguments. It reads DmgCalc1 from the same row (+0xB4),
//                  caps the result at 100, moves that percent of physical
//                  [damage+0x18] and adds it through 465160 to the component
//                  picked by the row's EType byte (+0x10C).
//   return 1       every path past the missile-data check returns 1, as stock.
//
// ---------------------------------------------------------------------------
// TXT COLUMN OFFSETS (read from the missiles.txt loader sub_1403C2C70)
//
//   pSrvDmgFunc  word  +0x30     descriptor [rbp+0x210] name 141D0B558
//   dParam1      dword +0x9C     descriptor [rbp+0x4B0] name 141D0B658
//   DmgCalc1     calc  +0xB4     descriptor [rbp+0x4F0] name 141D0B668
//   HitFlags     dword +0xD0     descriptor [rbp+0x7B0] name 141CF9888
//   ResultFlags  word  +0xD4     descriptor [rbp+0x790] name 141CF9878
//   EType        byte  +0x10C    descriptor [rbp+0xA90] name 141CF9DFC
//
// ---------------------------------------------------------------------------
// THE TABLE
//
//   pSrvDmgFunc table at RVA 2391270, slot N at base + 8N, count dword at
//   RVA 2391408 (31 on 3.3, so indices 1..30 are dispatched). Slots 18..30
//   are empty on 3.3. Both dispatchers (45BEAA in sub_14045BC40 and 463EF4 in
//   sub_1404639A0) gate on 0 < index < count and call the slot with no null
//   check. The table is swapped the same way the chain-limit plugin swaps
//   pSrvDoFunc 45: VirtualProtect the one slot, write, restore.
// ---------------------------------------------------------------------------

#include <D2RLPlugin/api.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

// --------------------------------------------------------------------------
// Build-specific constants
// --------------------------------------------------------------------------

constexpr std::uint64_t kDmgFuncTableRva = 0x02391270ULL;
constexpr std::uint64_t kDmgFuncCountRva = 0x02391408ULL;

constexpr std::uint64_t kGetMissilesTxtRecordRva = 0x00166040ULL;
constexpr std::uint64_t kGetServerUnitRva        = 0x0048FE80ULL;
constexpr std::uint64_t kFireUnitEventRva        = 0x0044D570ULL;
constexpr std::uint64_t kGetOwnerWeaponRva       = 0x004242B0ULL;
constexpr std::uint64_t kMissileWeaponWearRva    = 0x00441B10ULL;
constexpr std::uint64_t kDamageFireArrowRva      = 0x00453490ULL;

// The dispatcher in sub_14045BC40, from `movsx rcx, word ptr [rax+30h]` to
// `call qword ptr [r10+rax*8]`. Its two rip-relative operands encode the table
// (lea r10 -> 2391270) and the count (cmp ecx -> 2391408), so a byte-exact match
// proves both RVAs for this build.
constexpr std::uint64_t kDispatchWitnessRva = 0x0045BEAAULL;
constexpr std::uint8_t  kDispatchWitnessBytes[]{
	0x48, 0x0F, 0xBF, 0x48, 0x30,                   // movsx rcx, word ptr [rax+30h]
	0x66, 0x85, 0xC9,                               // test cx, cx
	0x7E, 0x25,                                     // jle
	0x3B, 0x0D, 0x4E, 0x55, 0xF3, 0x01,             // cmp ecx, [rip+1F3554Eh]   -> 2391408
	0x7D, 0x1D,                                     // jge
	0x4C, 0x8B, 0x44, 0x24, 0x58,                   // mov r8, [rsp+58h]
	0x4C, 0x8D, 0x15, 0xA8, 0x53, 0xF3, 0x01,       // lea r10, [rip+1F353A8h]   -> 2391270
	0x48, 0x8B, 0xC1,                               // mov rax, rcx
	0x4C, 0x8D, 0x4D, 0x90,                         // lea r9, [rbp-70h]
	0x48, 0x8B, 0xD3,                               // mov rdx, rbx
	0x49, 0x8B, 0xCE,                               // mov rcx, r14
	0x41, 0xFF, 0x14, 0xC2,                         // call qword ptr [r10+rax*8]
};

// Prologues of every game function called, used as a build check.
constexpr std::uint8_t kGetMissilesTxtRecordBytes[]{
	0x40, 0x53, 0x48, 0x83, 0xEC, 0x30, 0x48, 0x63, 0xDA,
};
constexpr std::uint8_t kGetServerUnitBytes[]{
	0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x20,
	0x41, 0x8B, 0xD8, 0x8B, 0xF2, 0x48, 0x8B, 0xF9,
};
constexpr std::uint8_t kFireUnitEventBytes[]{
	0x40, 0x53, 0x55, 0x56, 0x57, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x60,
};
constexpr std::uint8_t kGetOwnerWeaponBytes[]{
	0xE9, 0x4B, 0x6D, 0xF2, 0xFF,  // jmp 34B000
};
constexpr std::uint8_t kMissileWeaponWearBytes[]{
	0x48, 0x89, 0x6C, 0x24, 0x10, 0x56, 0x57, 0x41, 0x54, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x30,
	0x4C, 0x8B, 0xF2, 0x48, 0x8B, 0xE9,
};
constexpr std::uint8_t kDamageFireArrowBytes[]{
	0x48, 0x89, 0x6C, 0x24, 0x18, 0x56, 0x57, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x30,
	0x49, 0x8B, 0xF1, 0x4D, 0x8B, 0xF0, 0x48, 0x8B, 0xFA,
};

// Slots that must hold known values before the table is touched.
constexpr std::int32_t kFireArrowSlot       = 1;
constexpr std::int32_t kSoaringStrikeSlot   = 16;
constexpr std::int32_t kDefaultSlot         = 18;
constexpr std::int32_t kMaxSaneCount        = 256;

// Unit layout.
constexpr std::size_t   kUnitTypeOffset        = 0x00;
constexpr std::size_t   kUnitClassOffset       = 0x04;
constexpr std::size_t   kUnitDataOffset        = 0x10;
constexpr std::size_t   kUnitOwnerTypeOffset   = 0xE8;
constexpr std::size_t   kUnitOwnerIdOffset     = 0xEC;
constexpr std::size_t   kUnitOwnerFlagOffset   = 0x129;
constexpr std::uint8_t  kUnitOwnerFlagBit      = 0x04;
constexpr std::uint32_t kUnitTypePlayer        = 0;
constexpr std::uint32_t kUnitTypeMissile       = 3;

// pGame layout.
constexpr std::size_t kGameDataContextOffset = 0x106;

// Missile data layout ([unit+0x10]).
constexpr std::size_t   kDataOnceFlagsOffset  = 0x1C;
constexpr std::uint32_t kOnceEventFired       = 0x04;
constexpr std::uint8_t  kOnceWeaponWorn       = 0x10;
constexpr std::size_t   kDataMirrorFlagsOffset = 0x40;
constexpr std::uint32_t kMirroredCopyFlag     = 0x40000;

// missiles.txt row layout.
constexpr std::size_t kRowDParam1Offset     = 0x9C;
constexpr std::size_t kRowHitFlagsOffset    = 0xD0;
constexpr std::size_t kRowResultFlagsOffset = 0xD4;

// Damage struct layout.
constexpr std::size_t kDamageHitFlagsOffset    = 0x00;
constexpr std::size_t kDamageResultFlagsOffset = 0x04;
constexpr std::size_t kDamagePhysicalOffset    = 0x18;

// Unit event 15, named hextrigger in the D2RLoader data guide.
constexpr std::int32_t kHexTriggerEvent = 15;

// --------------------------------------------------------------------------
// Game function types
// --------------------------------------------------------------------------

using GetMissilesTxtRecordFn = void*(__fastcall*)(std::uint32_t dataContext, std::int32_t missileClass) noexcept;
using GetServerUnitFn        = void*(__fastcall*)(void* game, std::uint32_t unitType, std::uint32_t unitId) noexcept;
using FireUnitEventFn        = std::int32_t(__fastcall*)(void* game, std::int32_t eventId, void* source, void* target, void* damage) noexcept;
using GetOwnerWeaponFn       = void*(__fastcall*)(void* unit) noexcept;
using MissileWeaponWearFn    = void(__fastcall*)(void* game, void* unit, void* item) noexcept;
using DamageFuncFn           = std::int64_t(__fastcall*)(void* game, void* missile, void* target, void* damage) noexcept;

GetMissilesTxtRecordFn g_getMissilesTxtRecord = nullptr;
GetServerUnitFn        g_getServerUnit        = nullptr;
FireUnitEventFn        g_fireUnitEvent        = nullptr;
GetOwnerWeaponFn       g_getOwnerWeapon       = nullptr;
MissileWeaponWearFn    g_missileWeaponWear    = nullptr;
DamageFuncFn           g_damageFireArrow      = nullptr;

// --------------------------------------------------------------------------
// State
// --------------------------------------------------------------------------

struct Settings {
	std::int32_t slot = kDefaultSlot;
};

Settings     g_settings{};
void**       g_slot          = nullptr;
std::int32_t g_installedSlot = 0;

volatile LONG g_hits        = 0;
volatile LONG g_events      = 0;
volatile LONG g_weaponRolls = 0;
volatile LONG g_conversions = 0;

// --------------------------------------------------------------------------
// Memory helpers
// --------------------------------------------------------------------------

template <typename T>
auto Read(const void* base, std::size_t offset) noexcept -> T {
	T value{};
	std::memcpy(&value, static_cast<const std::uint8_t*>(base) + offset, sizeof(T));
	return value;
}

template <typename T>
void Write(void* base, std::size_t offset, T value) noexcept {
	std::memcpy(static_cast<std::uint8_t*>(base) + offset, &value, sizeof(T));
}

// --------------------------------------------------------------------------
// The damage function
// --------------------------------------------------------------------------

auto __fastcall DamageSoaringStrikeConvert(void* game, void* missile, void* target, void* damage) noexcept -> std::int32_t {
	::InterlockedIncrement(&g_hits);

	if (game == nullptr || missile == nullptr || damage == nullptr) {
		return 0;
	}
	if (Read<std::uint32_t>(missile, kUnitTypeOffset) != kUnitTypeMissile) {
		return 0;
	}

	const std::int32_t missileClass = Read<std::int32_t>(missile, kUnitClassOffset);
	if (missileClass < 0) {
		return 0;
	}

	const void* const row = g_getMissilesTxtRecord(Read<std::uint8_t>(game, kGameDataContextOffset), missileClass);
	if (row == nullptr) {
		return 0;
	}

	void* owner = nullptr;
	if ((Read<std::uint8_t>(missile, kUnitOwnerFlagOffset) & kUnitOwnerFlagBit) != 0) {
		owner = g_getServerUnit(game,
		                        Read<std::uint32_t>(missile, kUnitOwnerTypeOffset),
		                        Read<std::uint32_t>(missile, kUnitOwnerIdOffset));
	}

	Write<std::uint32_t>(damage, kDamageHitFlagsOffset,
	                     Read<std::uint32_t>(damage, kDamageHitFlagsOffset) | Read<std::uint32_t>(row, kRowHitFlagsOffset));
	Write<std::uint16_t>(damage, kDamageResultFlagsOffset,
	                     static_cast<std::uint16_t>(Read<std::uint16_t>(damage, kDamageResultFlagsOffset) | Read<std::uint16_t>(row, kRowResultFlagsOffset)));

	const auto missileData = Read<void*>(missile, kUnitDataOffset);
	if (missileData == nullptr) {
		return 0;
	}

	std::uint32_t onceFlags = Read<std::uint32_t>(missileData, kDataOnceFlagsOffset);
	if ((onceFlags & kOnceEventFired) == 0) {
		if (owner != nullptr) {
			g_fireUnitEvent(game, kHexTriggerEvent, owner, target, damage);
			::InterlockedIncrement(&g_events);
			onceFlags = Read<std::uint32_t>(missileData, kDataOnceFlagsOffset);
		}
		onceFlags |= kOnceEventFired;
		Write<std::uint32_t>(missileData, kDataOnceFlagsOffset, onceFlags);
	}

	if (Read<std::int32_t>(row, kRowDParam1Offset) != 0
		&& (onceFlags & kOnceWeaponWorn) == 0
		&& owner != nullptr
		&& (Read<std::uint32_t>(missileData, kDataMirrorFlagsOffset) & kMirroredCopyFlag) == 0
		&& Read<std::uint32_t>(owner, kUnitTypeOffset) == kUnitTypePlayer) {

		const auto weapon = g_getOwnerWeapon(owner);
		if (weapon != nullptr) {
			g_missileWeaponWear(game, owner, weapon);
			Write<std::uint8_t>(missileData, kDataOnceFlagsOffset,
			                    static_cast<std::uint8_t>(Read<std::uint8_t>(missileData, kDataOnceFlagsOffset) | kOnceWeaponWorn));
			::InterlockedIncrement(&g_weaponRolls);
		}
	}

	// pSrvDmgFunc 16 rolls its return-trip pull here. This function converts
	// damage instead, by running pSrvDmgFunc 1 itself on the same hit.
	const std::int32_t physicalBefore = Read<std::int32_t>(damage, kDamagePhysicalOffset);
	g_damageFireArrow(game, missile, target, damage);
	if (Read<std::int32_t>(damage, kDamagePhysicalOffset) != physicalBefore) {
		::InterlockedIncrement(&g_conversions);
	}

	return 1;
}

// --------------------------------------------------------------------------
// Config file
// --------------------------------------------------------------------------

constexpr const char* kDefaultConfigToml =
	"# Soaring Strike Conversion\n"
	"#\n"
	"# Adds a new missiles.txt pSrvDmgFunc. It is D2RLoader's DamageSoaringStrike\n"
	"# (pSrvDmgFunc 16) with one change: the return-trip pull is gone, and\n"
	"# DmgCalc1 converts physical damage to elemental exactly like pSrvDmgFunc 1.\n"
	"#\n"
	"# What the function does every time the missile damages a unit, in order:\n"
	"#\n"
	"#   1. ORs the missile's HitFlags and ResultFlags columns into the hit.\n"
	"#\n"
	"#   2. First hit of the missile only: fires unit event 15 (hextrigger) with\n"
	"#      the missile's owner as the source and the struck unit as the target.\n"
	"#      The once-per-missile mark is set on that first hit even when the\n"
	"#      owner no longer exists, so the event can never fire later.\n"
	"#\n"
	"#   3. Only when dParam1 is not 0: once per missile, a player owner's weapon\n"
	"#      gets the game's missile durability roll, the same call pSrvDmgFunc 16\n"
	"#      makes. Skipped for copies created by Mirrored Blades. If the owner has\n"
	"#      no weapon at that moment, it is tried again on the next hit.\n"
	"#\n"
	"#   4. Every hit, outbound and return: DmgCalc1 percent of the physical\n"
	"#      damage is moved to the element named in the missile's EType column.\n"
	"#      This runs the game's own pSrvDmgFunc 1 code, so the math is identical.\n"
	"#      Blank, 0 or negative converts nothing; above 100 converts all of it.\n"
	"#\n"
	"# Removed compared to pSrvDmgFunc 16: the return-trip pull roll.\n"
	"\n"
	"[soaring-strike-convert]\n"
	"\n"
	"# pSrvDmgFunc number the function is installed under. Put the same number in\n"
	"# the pSrvDmgFunc column of every missile that should use it. Must be a free\n"
	"# slot from 1 to 30; the plugin refuses to load if the slot is already taken.\n"
	"slot = 18\n";

// Finds "key" at the start of a line and returns the text after its '='.
auto FindValue(const char* text, const char* key) noexcept -> const char* {
	if (text == nullptr || key == nullptr) {
		return nullptr;
	}

	std::size_t keyLength = 0;
	while (key[keyLength] != '\0') {
		++keyLength;
	}

	const char* line = text;
	while (*line != '\0') {
		const char* cursor = line;
		while (*cursor == ' ' || *cursor == '\t') {
			++cursor;
		}

		if (*cursor != '#') {
			std::size_t index = 0;
			while (index < keyLength && cursor[index] == key[index]) {
				++index;
			}
			if (index == keyLength) {
				const char* after = cursor + keyLength;
				while (*after == ' ' || *after == '\t') {
					++after;
				}
				if (*after == '=') {
					++after;
					while (*after == ' ' || *after == '\t') {
						++after;
					}
					return after;
				}
			}
		}

		while (*line != '\0' && *line != '\n') {
			++line;
		}
		if (*line == '\n') {
			++line;
		}
	}
	return nullptr;
}

enum class ValueRead {
	Missing,
	Parsed,
	Malformed,
};

// Plain non-negative decimal integer, followed by whitespace, a comment or the
// end of the line.
auto TryReadInt(const char* text, const char* key, std::int32_t* out) noexcept -> ValueRead {
	const char* value = FindValue(text, key);
	if (value == nullptr) {
		return ValueRead::Missing;
	}

	std::int64_t parsed = 0;
	bool         digits = false;
	while (*value >= '0' && *value <= '9') {
		parsed = parsed * 10 + (*value - '0');
		digits = true;
		++value;
		if (parsed > 0x7FFFFFFFLL) {
			return ValueRead::Malformed;
		}
	}

	const bool terminated = *value == '\0' || *value == '\n' || *value == '\r' || *value == ' ' || *value == '\t' || *value == '#';
	if (!digits || !terminated) {
		return ValueRead::Malformed;
	}

	*out = static_cast<std::int32_t>(parsed);
	return ValueRead::Parsed;
}

void LoadSettings(const D2RL::PluginContext* context) noexcept {
	if (!context->EnsureConfig(kDefaultConfigToml)) {
		context->LogWarn("Could not create the config file, using built in defaults.");
		return;
	}

	char          toml[4096]{};
	std::uint32_t requiredSize = 0;
	if (!context->ReadConfig(toml, static_cast<std::uint32_t>(sizeof(toml)), &requiredSize)) {
		context->LogWarn("Could not read the config file, using built in defaults.");
		return;
	}
	toml[sizeof(toml) - 1] = '\0';

	Settings     settings{};
	std::int32_t slot = settings.slot;
	if (TryReadInt(toml, "slot", &slot) == ValueRead::Malformed) {
		char message[128]{};
		std::snprintf(message, sizeof(message), "slot is not a plain whole number, using %d.", static_cast<int>(kDefaultSlot));
		context->LogWarn(message);
	} else {
		settings.slot = slot;
	}
	g_settings = settings;
}

// --------------------------------------------------------------------------
// Table slot
// --------------------------------------------------------------------------

// Writes one table slot. Returns the value the slot held before the write.
// When `expected` is not what the slot holds, nothing is written.
auto SwapSlot(void** slot, void* expected, void* value, void** previousOut) noexcept -> bool {
	DWORD oldProtection = 0;
	if (::VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &oldProtection) == FALSE) {
		return false;
	}

	const auto previous = static_cast<void*>(::InterlockedCompareExchangePointer(slot, value, expected));

	DWORD ignored = 0;
	::VirtualProtect(slot, sizeof(void*), oldProtection, &ignored);

	if (previousOut != nullptr) {
		*previousOut = previous;
	}
	return previous == expected && *slot == value;
}

// --------------------------------------------------------------------------
// Plugin plumbing
// --------------------------------------------------------------------------

constexpr D2RL::PluginInfo kPluginInfo{
	.infoSize    = D2RL::PluginInfoSize,
	.apiVersion  = D2RL_PLUGIN_API_VERSION,
	.id          = "celestialrayone.soaring-strike-convert",
	.name        = "Soaring Strike Conversion",
	.version     = "1.0.0",
	.author      = "CelestialRayOne",
	.description = "Adds pSrvDmgFunc 18: D2RLoader's DamageSoaringStrike with the return-trip pull replaced by pSrvDmgFunc 1 damage conversion.",
	.flags       = D2RL::PluginFlags::Server,
};

template <std::size_t Size>
constexpr auto ByteCount(const std::uint8_t (&)[Size]) noexcept -> std::uint32_t {
	return static_cast<std::uint32_t>(Size);
}

auto CheckSite(const D2RL::PluginContext* context,
               std::uint64_t             rva,
               const std::uint8_t*       bytes,
               std::uint32_t             size,
               const char*               what) noexcept -> bool {
	if (context->CheckExpectedBytes(rva, bytes, size)) {
		return true;
	}

	char message[192]{};
	std::snprintf(message, sizeof(message),
	              "%s at RVA 0x%08llX does not match the expected bytes - wrong game build, or another plugin got there first.",
	              what, static_cast<unsigned long long>(rva));
	context->LogError(message);
	return false;
}

auto __cdecl StatusCommand(D2R::Game::Client*                 client,
                           const D2RL::ConsoleCommandContext* command,
                           void*                              userData) noexcept -> D2RL::ConsoleCommandResult {
	(void)client;
	(void)userData;

	if (command == nullptr || command->plugin == nullptr) {
		return D2RL::ConsoleCommandResult::Failed;
	}

	char message[224]{};
	std::snprintf(message, sizeof(message),
	              "soaring-strike-convert: pSrvDmgFunc %d %s, hits %ld, hextrigger events %ld, weapon rolls %ld, converted hits %ld.",
	              static_cast<int>(g_installedSlot),
	              g_slot != nullptr ? "installed" : "NOT installed",
	              static_cast<long>(g_hits),
	              static_cast<long>(g_events),
	              static_cast<long>(g_weaponRolls),
	              static_cast<long>(g_conversions));
	command->plugin->WriteConsoleMessage(message);
	return D2RL::ConsoleCommandResult::Handled;
}

} // namespace

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* {
	return &kPluginInfo;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
	if (context == nullptr) {
		return false;
	}

	LoadSettings(context);

	const auto moduleBase = reinterpret_cast<std::uintptr_t>(::GetModuleHandleW(nullptr));
	if (moduleBase == 0) {
		context->LogError("Could not resolve the main module base.");
		return false;
	}

	if (!CheckSite(context, kDispatchWitnessRva, kDispatchWitnessBytes, ByteCount(kDispatchWitnessBytes), "pSrvDmgFunc dispatcher")
		|| !CheckSite(context, kGetMissilesTxtRecordRva, kGetMissilesTxtRecordBytes, ByteCount(kGetMissilesTxtRecordBytes), "missiles.txt row getter")
		|| !CheckSite(context, kGetServerUnitRva, kGetServerUnitBytes, ByteCount(kGetServerUnitBytes), "Server unit lookup")
		|| !CheckSite(context, kFireUnitEventRva, kFireUnitEventBytes, ByteCount(kFireUnitEventBytes), "Unit event dispatcher")
		|| !CheckSite(context, kGetOwnerWeaponRva, kGetOwnerWeaponBytes, ByteCount(kGetOwnerWeaponBytes), "Owner weapon getter")
		|| !CheckSite(context, kMissileWeaponWearRva, kMissileWeaponWearBytes, ByteCount(kMissileWeaponWearBytes), "Missile weapon durability roll")
		|| !CheckSite(context, kDamageFireArrowRva, kDamageFireArrowBytes, ByteCount(kDamageFireArrowBytes), "pSrvDmgFunc 1")) {
		return false;
	}

	const auto table = reinterpret_cast<void**>(moduleBase + kDmgFuncTableRva);
	const auto count = Read<std::int32_t>(reinterpret_cast<void*>(moduleBase + kDmgFuncCountRva), 0);

	char message[224]{};
	if (count <= kSoaringStrikeSlot || count > kMaxSaneCount) {
		std::snprintf(message, sizeof(message), "pSrvDmgFunc count is %d, expected %d to %d. Refusing to load.",
		              static_cast<int>(count), static_cast<int>(kSoaringStrikeSlot + 1), static_cast<int>(kMaxSaneCount));
		context->LogError(message);
		return false;
	}
	if (table[kFireArrowSlot] != reinterpret_cast<void*>(moduleBase + kDamageFireArrowRva)) {
		context->LogError("pSrvDmgFunc slot 1 is not the game's Fire Arrow function. Refusing to load.");
		return false;
	}
	if (table[kSoaringStrikeSlot] == nullptr) {
		context->LogError("pSrvDmgFunc slot 16 (DamageSoaringStrike) is empty on this build. Refusing to load.");
		return false;
	}

	const std::int32_t slotIndex = g_settings.slot;
	if (slotIndex < 1 || slotIndex >= count) {
		std::snprintf(message, sizeof(message), "slot = %d is outside 1..%d. Refusing to load.",
		              static_cast<int>(slotIndex), static_cast<int>(count - 1));
		context->LogError(message);
		return false;
	}

	g_getMissilesTxtRecord = reinterpret_cast<GetMissilesTxtRecordFn>(moduleBase + kGetMissilesTxtRecordRva);
	g_getServerUnit        = reinterpret_cast<GetServerUnitFn>(moduleBase + kGetServerUnitRva);
	g_fireUnitEvent        = reinterpret_cast<FireUnitEventFn>(moduleBase + kFireUnitEventRva);
	g_getOwnerWeapon       = reinterpret_cast<GetOwnerWeaponFn>(moduleBase + kGetOwnerWeaponRva);
	g_missileWeaponWear    = reinterpret_cast<MissileWeaponWearFn>(moduleBase + kMissileWeaponWearRva);
	g_damageFireArrow      = reinterpret_cast<DamageFuncFn>(moduleBase + kDamageFireArrowRva);

	const auto slot     = &table[slotIndex];
	void*      previous = nullptr;
	if (!SwapSlot(slot, nullptr, reinterpret_cast<void*>(&DamageSoaringStrikeConvert), &previous)) {
		if (previous != nullptr) {
			std::snprintf(message, sizeof(message),
			              "pSrvDmgFunc slot %d is already taken (holds %p). Pick a free slot in the config. Refusing to load.",
			              static_cast<int>(slotIndex), previous);
		} else {
			std::snprintf(message, sizeof(message), "Could not write pSrvDmgFunc slot %d. Refusing to load.",
			              static_cast<int>(slotIndex));
		}
		context->LogError(message);
		return false;
	}

	g_slot          = slot;
	g_installedSlot = slotIndex;

	if (!context->RegisterConsoleCommand("soaring-strike-convert", StatusCommand, "Report the Soaring Strike conversion damage function state.")) {
		context->LogWarn("Console command was not registered.");
	}

	std::snprintf(message, sizeof(message),
	              "Soaring Strike conversion installed as pSrvDmgFunc %d (table RVA 0x%08llX).",
	              static_cast<int>(slotIndex),
	              static_cast<unsigned long long>(kDmgFuncTableRva + 8ULL * static_cast<std::uint64_t>(slotIndex)));
	context->LogInfo(message);
	return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
	if (g_slot != nullptr) {
		SwapSlot(g_slot, reinterpret_cast<void*>(&DamageSoaringStrikeConvert), nullptr, nullptr);
		g_slot = nullptr;
	}
}
