// Herald Bosses
//
// Monsters that become heralds count as bosses and prime evils, as if their
// monstats.txt row had boss = 1 and primeevil = 1. What that changes in game
// is written up in celestialrayone.herald-bosses.toml.
//
// Read out of the D2RLoader 1.3.1 process image and D2RCore.dll 1.3.1
// (SHA-256 2a868d01...89b948fdeb5a2). Every site is checked byte for byte at
// load; a part whose check fails is not installed and the log names it.
//
// 1. HERALDS
//   MonsterData (unit+10h) +1Ah holds the rank flags. A herald carries 200h,
//   the flag the game's Aura Enchanted routine tests (0x495F69) and D2RCore's
//   herald code tests as byte +1Bh bit 1. It is tested when the game asks, so
//   a monster counts from the moment it becomes a herald.
//
// 2. WHERE THE GAME ASKS
//   monstats.txt boss and primeevil are bits 6 and 7 of the flags dword at
//   record +3Ch (parser 0x39DB30, bitfield columns). The game asks through
//   three helpers:
//     0x3AEFF0  MONSTERS_IsBoss (record, unit), 25 callers: stun 0x451C30,
//               freeze 0x451570, Crushing Blow, SUNITDMG_CalculateTotalDamage
//               0x44DF10, skills, AI, the affix rules in 0x498510..0x49AEB0
//     0x3AF080  the same, false for monstats rows 546 to 550
//     0x3B0250  MONSTERS_IsPrimeEvil (unit), 7 callers: 0x44DF10 (the
//               difficultylevels.txt PrimeEvil damage columns), aura filters
//               0x439C10, AI 0x595270, 0x5D0F10, 0x5D6870
//   Each ends with  movzx eax, byte [record+3Ch] / and eax, [mask]  where the
//   mask is gdwBitMasks[6] (0x1D996E8, 40h) or [7] (0x1D996EC, 80h). Those
//   ten bytes jump to a stub that does the same and, when the result is 0
//   and the unit is a herald, returns the mask. The entries stay untouched
//   (crushing-blow-modifier checks the first 16 bytes of 0x3AEFF0).
//   One game routine tests the boss bit itself:
//     0x34C7EF  AIUTIL_CanUnitSwitchAi, its "no bosses" gate (rbx = unit,
//               rdi = record), used by Conversion, Revive and the other AI
//               switches when they ask for it
//   D2RCore tests the prime evil bit itself once:
//     ApplyResistanceComponentWithPrimeEvilThreat +6Dh (D2RCore+0x82678D),
//     the prime evil threat bookkeeping after a player's hit (rax = record,
//     [r14+18h] = the monster that was hit)
//   Both get the same kind of stub: the original test first, then the
//   herald flag.
//
// 3. LEFT ALONE
//   The boss bit reads that set a monster's level at spawn (0x543B50, done
//   before any monster can be a herald) and the level a treasure class is
//   upgraded by (TREASURECLASS_GenerateDrops 0x441300, and 0x43DF20, same
//   noratio + boss test): herald drops keep scaling with the herald's level.
//   D2RCore's herald code (0x40CF60, 0x40D1D0, 0x40E7A0), which tests the
//   herald flag and the record's boss bit together to tell boss heralds from
//   replaced monsters. Every other read of these two masks in game code
//   tests another bit (killable, flying, placespawn, npc) or another table.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <D2RLPlugin/api.h>

#include <Windows.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>

namespace {

const D2RL::PluginContext* Context = nullptr;
std::uintptr_t             Base    = 0;

void LogInfo(const char* message) noexcept {
	if (Context != nullptr) Context->LogInfo(message);
}

void LogWarn(const char* message) noexcept {
	if (Context != nullptr) Context->LogWarn(message);
}

void LogError(const char* message) noexcept {
	if (Context != nullptr) Context->LogError(message);
}

// ---------------------------------------------------------------------------
// Stubs. Position independent: the masks and the absolute return addresses
// are filled in at load.
//   StubBossRbx    record rbx, unit rdi (0x3AEFF0 and 0x3AF080)
//   StubPrimeRax   record rax, unit rdi (0x3B0250)
//   StubSwitchAi   record rdi, unit rbx (0x34C7EF)
//   StubCoreThreat record rax, monster [r14+18h] (D2RCore)
// Each keeps the original test, then: unit+10h MonsterData, byte +1Bh bit 1
// (rank flag 200h). Only rax/eax, rcx and the flags change, all of them
// dead at every place the stubs return to.
// ---------------------------------------------------------------------------

constexpr std::uint8_t StubBossRbx[] {
	0x0F, 0xB6, 0x43, 0x3C, 0x25, 0x11, 0x11, 0x11, 0x11, 0x75, 0x14, 0x48, 0x8B, 0x4F, 0x10, 0x48,
	0x85, 0xC9, 0x74, 0x0B, 0xF6, 0x41, 0x1B, 0x02, 0x74, 0x05, 0xB8, 0x11, 0x11, 0x11, 0x11, 0xFF,
	0x25, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

constexpr std::uint8_t StubPrimeRax[] {
	0x0F, 0xB6, 0x40, 0x3C, 0x25, 0x11, 0x11, 0x11, 0x11, 0x75, 0x14, 0x48, 0x8B, 0x4F, 0x10, 0x48,
	0x85, 0xC9, 0x74, 0x0B, 0xF6, 0x41, 0x1B, 0x02, 0x74, 0x05, 0xB8, 0x11, 0x11, 0x11, 0x11, 0xFF,
	0x25, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

constexpr std::uint8_t StubSwitchAi[] {
	0x0F, 0xB6, 0x47, 0x3C, 0xA8, 0x11, 0x75, 0x22, 0x83, 0x3B, 0x01, 0x75, 0x0F, 0x48, 0x8B, 0x43,
	0x10, 0x48, 0x85, 0xC0, 0x74, 0x06, 0xF6, 0x40, 0x1B, 0x02, 0x75, 0x0E, 0xFF, 0x25, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

constexpr std::uint8_t StubCoreThreat[] {
	0xF6, 0x40, 0x3C, 0x80, 0x75, 0x21, 0x49, 0x8B, 0x4E, 0x18, 0x48, 0x8B, 0x49, 0x10, 0x48, 0x85,
	0xC9, 0x74, 0x06, 0xF6, 0x41, 0x1B, 0x02, 0x75, 0x0E, 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00,
};

constexpr std::uint8_t CoreThreatWindow[] {
	0x49, 0x8B, 0x46, 0x18, 0x48, 0x85, 0xC0, 0x74, 0x2D, 0x49, 0x8B, 0x0E, 0x48, 0x85, 0xC9, 0x74,
	0x25, 0x83, 0x38, 0x01, 0x75, 0x20, 0x8B, 0x50, 0x04, 0x85, 0xD2, 0x78, 0x19, 0x0F, 0xB6, 0x89,
	0x06, 0x01, 0x00, 0x00, 0xFF, 0x15, 0xE1, 0xB8, 0xED, 0xFF, 0x90, 0x48, 0x85, 0xC0, 0x74, 0x06,
	0xF6, 0x40, 0x3C, 0x80, 0x75, 0x14, 0x48, 0x89, 0xF0, 0x48, 0x83, 0xC4, 0x48, 0x5B, 0x5F, 0x5E,
	0x41, 0x5C, 0x41, 0x5D, 0x41, 0x5E, 0x41, 0x5F, 0x5D, 0xC3, 0x49, 0x8B, 0x5E, 0x10, 0x48, 0x85,
	0xDB, 0x74, 0xE3,
};

constexpr std::uint8_t CoreThreatEntry[] {
	0x55, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x56, 0x57, 0x53, 0x48, 0x83, 0xEC, 0x48,
	0x48, 0x8D, 0x6C, 0x24, 0x40, 0x48, 0xC7, 0x45, 0x00, 0xFE, 0xFF, 0xFF, 0xFF, 0x49, 0x89, 0xD7,
};

constexpr std::size_t StubMaskAt     = 5;    // and eax, imm32 (or test al, imm8 in StubSwitchAi)
constexpr std::size_t StubMaskAgain  = 27;   // mov eax, imm32
constexpr std::size_t StubBackAt     = 37;
constexpr std::size_t SwitchContAt   = 34;
constexpr std::size_t SwitchFailAt   = 48;
constexpr std::size_t CoreNotAt      = 31;
constexpr std::size_t CorePrimeAt    = 45;

// ---------------------------------------------------------------------------
// Game sites (RVAs in the D2RLoader 1.3.1 image)
// ---------------------------------------------------------------------------

// gdwBitMasks, 1 << n, the table the helpers read their mask from.
constexpr std::uint64_t BitMasksRva  = 0x01D996D0;
constexpr std::uint8_t  BitMasksBytes[] {
	0x01, 0, 0, 0, 0x02, 0, 0, 0, 0x04, 0, 0, 0, 0x08, 0, 0, 0,
	0x10, 0, 0, 0, 0x20, 0, 0, 0, 0x40, 0, 0, 0, 0x80, 0, 0, 0 };
constexpr std::uint64_t BossMaskRva  = 0x01D996E8;
constexpr std::uint64_t PrimeMaskRva = 0x01D996EC;

struct Witness {
	std::uint64_t       rva;
	const std::uint8_t* bytes;
	std::uint32_t       size;
};

// MONSTERS_IsBoss 0x3AEFF0: unit type check, record from the class when none
// was passed, then the tail.
constexpr std::uint8_t IsBossEntry[]  { 0x48, 0x8B, 0xCF, 0xE8, 0xA3, 0xC9, 0xF9, 0xFF, 0x83, 0xF8, 0x01, 0x75, 0xE6, 0x48, 0x85, 0xDB, 0x75, 0x31 };
constexpr std::uint8_t IsBossTail[]   { 0x0F, 0xB6, 0x43, 0x3C, 0x23, 0x05, 0x76, 0xA6, 0x9E, 0x01 };
constexpr std::uint8_t IsBossReturn[] { 0x48, 0x8B, 0x5C, 0x24, 0x30, 0x48, 0x83, 0xC4, 0x20, 0x5F, 0xC3 };
constexpr Witness      IsBossChecks[] { { 0x003AF025, IsBossEntry, sizeof(IsBossEntry) }, { 0x003AF072, IsBossReturn, sizeof(IsBossReturn) } };

// 0x3AF080: the same with rows 546..550 refused before the tail.
constexpr std::uint8_t TwinEntry[]  { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xFA, 0x48, 0x8B, 0xD9 };
constexpr std::uint8_t TwinFilter[] { 0xB9, 0x22, 0x02, 0x00, 0x00, 0x66, 0x2B, 0xC1, 0x66, 0x83, 0xF8, 0x04 };
constexpr std::uint8_t TwinTail[]   { 0x0F, 0xB6, 0x43, 0x3C, 0x23, 0x05, 0xB9, 0xA5, 0x9E, 0x01 };
constexpr std::uint8_t TwinReturn[] { 0x85, 0xC0, 0x0F, 0x95, 0xC0, 0x48, 0x8B, 0x5C, 0x24, 0x30, 0x48, 0x83, 0xC4, 0x20, 0x5F, 0xC3 };
constexpr Witness      TwinChecks[] { { 0x003AF080, TwinEntry, sizeof(TwinEntry) }, { 0x003AF0A5, TwinFilter, sizeof(TwinFilter) }, { 0x003AF12F, TwinReturn, sizeof(TwinReturn) } };

// MONSTERS_IsPrimeEvil 0x3B0250.
constexpr std::uint8_t PrimeEntry[]  { 0x40, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xF9 };
constexpr std::uint8_t PrimeBranch[] { 0x48, 0x8B, 0x5C, 0x24, 0x30, 0x48, 0x85, 0xC0, 0x75, 0x1B };
constexpr std::uint8_t PrimeTail[]   { 0x0F, 0xB6, 0x40, 0x3C, 0x23, 0x05, 0x2C, 0x94, 0x9E, 0x01 };
constexpr std::uint8_t PrimeReturn[] { 0x48, 0x83, 0xC4, 0x20, 0x5F, 0xC3 };
constexpr Witness      PrimeChecks[] { { 0x003B0250, PrimeEntry, sizeof(PrimeEntry) }, { 0x003B0291, PrimeBranch, sizeof(PrimeBranch) }, { 0x003B02C0, PrimeReturn, sizeof(PrimeReturn) } };

// AIUTIL_CanUnitSwitchAi 0x34C730: rbx = unit, rdi = its record, the boss
// gate only when its checkBoss argument (r14b) is set.
constexpr std::uint8_t SwitchUnit[]   { 0x0F, 0xB6, 0xB3, 0xBD, 0x01, 0x00, 0x00, 0x8B, 0x6B, 0x04 };
constexpr std::uint8_t SwitchRecord[] { 0xE8, 0x4B, 0xAF, 0xD4, 0xFF, 0x48, 0x8B, 0xF8 };
constexpr std::uint8_t SwitchGate[]   { 0x45, 0x84, 0xF6, 0x74, 0x0C };
constexpr std::uint8_t SwitchSite[]   { 0x0F, 0xB6, 0x47, 0x3C, 0x84, 0x05, 0xEF, 0xCE, 0xA4, 0x01, 0x75, 0xC5 };
constexpr std::uint8_t SwitchNext[]   { 0x80, 0x7C, 0x24, 0x60, 0x00 };
constexpr std::uint8_t SwitchRefuse[] { 0x32, 0xC0, 0x48, 0x8B, 0x5C, 0x24, 0x48 };
constexpr Witness      SwitchChecks[] {
	{ 0x0034C780, SwitchUnit, sizeof(SwitchUnit) }, { 0x0034C790, SwitchRecord, sizeof(SwitchRecord) },
	{ 0x0034C7EA, SwitchGate, sizeof(SwitchGate) }, { 0x0034C7FB, SwitchNext, sizeof(SwitchNext) },
	{ 0x0034C7C0, SwitchRefuse, sizeof(SwitchRefuse) } };

struct ExeSite {
	const char*                  name;
	bool                         boss;          // herald_boss, else herald_prime_evil
	std::uint64_t                rva;           // the bytes that now jump to the stub
	const std::uint8_t*          original;
	std::uint32_t                size;
	const Witness*               checks;
	std::uint32_t                checkCount;
	const std::uint8_t*          stub;
	std::uint32_t                stubSize;
	std::uint64_t                maskRva;
	bool                         maskLowByte;   // test al, imm8
	std::array<std::size_t, 2>   maskAt;        // 0 = unused
	std::array<std::size_t, 2>   targetAt;      // 0 = unused
	std::array<std::uint64_t, 2> targetRva;
	bool                         installed;
	std::array<std::uint8_t, 16> patch;
};

ExeSite Sites[] {
	{ "MONSTERS_IsBoss 0x3AEFF0", true, 0x003AF068, IsBossTail, sizeof(IsBossTail), IsBossChecks, 2, StubBossRbx,
	  sizeof(StubBossRbx), BossMaskRva, false, { StubMaskAt, StubMaskAgain }, { StubBackAt, 0 }, { 0x003AF072, 0 }, false, {} },
	{ "MONSTERS_IsBoss twin 0x3AF080", true, 0x003AF125, TwinTail, sizeof(TwinTail), TwinChecks, 3, StubBossRbx,
	  sizeof(StubBossRbx), BossMaskRva, false, { StubMaskAt, StubMaskAgain }, { StubBackAt, 0 }, { 0x003AF12F, 0 }, false, {} },
	{ "AIUTIL_CanUnitSwitchAi boss gate 0x34C7EF", true, 0x0034C7EF, SwitchSite, sizeof(SwitchSite), SwitchChecks, 5,
	  StubSwitchAi, sizeof(StubSwitchAi), BossMaskRva, true, { StubMaskAt, 0 }, { SwitchContAt, SwitchFailAt },
	  { 0x0034C7FB, 0x0034C7C0 }, false, {} },
	{ "MONSTERS_IsPrimeEvil 0x3B0250", false, 0x003B02B6, PrimeTail, sizeof(PrimeTail), PrimeChecks, 3, StubPrimeRax,
	  sizeof(StubPrimeRax), PrimeMaskRva, false, { StubMaskAt, StubMaskAgain }, { StubBackAt, 0 }, { 0x003B02C0, 0 }, false, {} },
};
constexpr std::size_t SiteStubSpacing = 0x40;

// D2RCore: ApplyResistanceComponentWithPrimeEvilThreat, export at +0x826720.
constexpr wchar_t       CoreModuleName[] = L"D2RCore.dll";
constexpr char          CoreExportName[] = "ApplyResistanceComponentWithPrimeEvilThreat";
constexpr std::uint64_t CoreExportRva    = 0x00826720;
constexpr std::uint64_t CoreWindowRva    = 0x0082675D;   // [r14+18h] .. the test and both of its exits
constexpr std::uint64_t CoreSiteRva      = 0x0082678D;   // test byte [rax+3Ch],80h / jne +14h
constexpr std::uint64_t CoreNotPrimeRva  = 0x00826793;
constexpr std::uint64_t CorePrimeRva     = 0x008267A7;
constexpr std::uint8_t  CoreSite[]       { 0xF6, 0x40, 0x3C, 0x80, 0x75, 0x14 };
static_assert(CoreSiteRva - CoreWindowRva == 0x30, "the site sits 30h into the window");

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------

struct Settings {
	bool boss      = true;
	bool primeEvil = true;
};

Settings Config {};
bool     ConfigFileRead = false;

constexpr char DefaultConfigToml[] = R"toml(# celestialrayone.herald-bosses
#
# Monsters that become heralds count as bosses and as prime evils: every
# game rule that asks "is this a boss" or "is this a prime evil" answers yes
# for them, as if their monstats.txt row had boss = 1 and primeevil = 1.
# A monster counts from the moment it becomes a herald.
# Changes are read when the plugin loads. Restart the game after editing.
# Built for D2RLoader 1.3.1 (Diablo II: Resurrected 3.3) and its D2RCore.dll.
# Console command: heraldbosses (which parts are installed).

# herald_boss
#   Heralds follow the game's boss rules, for example: they cannot be
#   stunned, they get chilled instead of frozen, Crushing Blow and
#   difficultylevels.txt PetDamagePercentVSBoss treat them as bosses, skills
#   and aura filters that skip bosses skip them, and Conversion, Revive and
#   the other AI switches leave them alone.
herald_boss = true

# herald_prime_evil
#   Heralds follow the prime evil rules: the difficultylevels.txt columns
#   PlayerDamagePercentVSPrimeEvil, PrimeEvilDamagePercentVSPlayer,
#   PrimeEvilDamagePercentVSMercenary and PrimeEvilDamagePercentVSPet, the
#   aura filters and AI rules for prime evils, and D2RCore's prime evil
#   threat on the players who hit them.
herald_prime_evil = true

# Not changed by this plugin:
#   - A herald's treasure class is still upgraded by its level like any
#     other monster's (a boss's never is), and its spawn level stays the
#     one it got before it became a herald.
#   - D2RCore's own herald choices (boss heralds versus replaced monsters).
#   - Plugins that read the monstats.txt flags themselves instead of asking
#     the game, for example Monster Display's prime evil bar.
)toml";

constexpr auto IsBlank(char c) noexcept -> bool {
	return c == ' ' || c == '\t' || c == '\r';
}

// Finds `key = value` at the start of a line; comments and section headers
// are skipped.
auto FindConfigValue(const char* text, const char* key, const char*& value, std::size_t& length) noexcept -> bool {
	const std::size_t keyLength = std::strlen(key);
	for (const char* line = text; *line != '\0';) {
		const char* cursor = line;
		while (IsBlank(*cursor)) ++cursor;
		if (std::strncmp(cursor, key, keyLength) == 0) {
			const char* after = cursor + keyLength;
			while (IsBlank(*after)) ++after;
			if (*after == '=') {
				++after;
				while (IsBlank(*after)) ++after;
				const char* end = after;
				while (*end != '\0' && *end != '\n' && *end != '#') ++end;
				while (end > after && IsBlank(end[-1])) --end;
				value  = after;
				length = static_cast<std::size_t>(end - after);
				return true;
			}
		}
		while (*line != '\0' && *line != '\n') ++line;
		if (*line == '\n') ++line;
	}
	return false;
}

auto ReadBool(const char* text, const char* key, bool fallback) noexcept -> bool {
	const char* value  = nullptr;
	std::size_t length = 0;
	if (!FindConfigValue(text, key, value, length) || length == 0) return fallback;
	if (length == 4 && std::strncmp(value, "true", 4) == 0) return true;
	if (length == 5 && std::strncmp(value, "false", 5) == 0) return false;
	char message[160] {};
	std::snprintf(message, sizeof(message), "%s must be true or false; using %s.", key, fallback ? "true" : "false");
	LogWarn(message);
	return fallback;
}

void LoadSettings() noexcept {
	if (!Context->EnsureConfig(DefaultConfigToml)) {
		LogWarn("Could not create celestialrayone.herald-bosses.toml; built-in defaults are used.");
		return;
	}
	std::array<char, 8'192> buffer {};
	if (!Context->ReadConfig(buffer.data(), static_cast<std::uint32_t>(buffer.size()))) {
		LogWarn("Could not read celestialrayone.herald-bosses.toml; built-in defaults are used.");
		return;
	}
	buffer.back()    = '\0';
	ConfigFileRead   = true;
	Config.boss      = ReadBool(buffer.data(), "herald_boss", Config.boss);
	Config.primeEvil = ReadBool(buffer.data(), "herald_prime_evil", Config.primeEvil);
}

// ---------------------------------------------------------------------------
// Relay pages
// ---------------------------------------------------------------------------

constexpr std::size_t PageBytes = 4'096;
constexpr std::size_t JumpSize  = 5;

auto CanReach(std::uintptr_t from, std::uintptr_t to) noexcept -> bool {
	const std::int64_t delta = static_cast<std::int64_t>(to) - static_cast<std::int64_t>(from + JumpSize);
	return delta >= std::numeric_limits<std::int32_t>::min() && delta <= std::numeric_limits<std::int32_t>::max();
}

// A page a rel32 jump at `site` can reach.
auto AllocateNear(std::uintptr_t site) noexcept -> std::uint8_t* {
	SYSTEM_INFO system {};
	GetSystemInfo(&system);
	const auto granularity = static_cast<std::uintptr_t>(system.dwAllocationGranularity);
	const auto aligned     = site & ~(granularity - 1U);
	for (std::uintptr_t delta = granularity; delta < 0x7000'0000ULL; delta += granularity) {
		const std::uintptr_t candidate = aligned + delta;
		if (!CanReach(site, candidate + PageBytes)) break;
		if (void* memory = VirtualAlloc(reinterpret_cast<void*>(candidate), PageBytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)) {
			return static_cast<std::uint8_t*>(memory);
		}
	}
	return nullptr;
}

auto Seal(std::uint8_t* page) noexcept -> bool {
	DWORD previous = 0;
	const bool ok  = VirtualProtect(page, PageBytes, PAGE_EXECUTE_READ, &previous) != FALSE;
	FlushInstructionCache(GetCurrentProcess(), page, PageBytes);
	return ok;
}

// jmp rel32 to `target`, padded with nops to `size`.
void EncodeJump(std::uintptr_t site, std::uintptr_t target, std::uint32_t size, std::uint8_t* out) noexcept {
	out[0] = 0xE9;
	const auto displacement = static_cast<std::int32_t>(static_cast<std::int64_t>(target) - static_cast<std::int64_t>(site + JumpSize));
	std::memcpy(out + 1, &displacement, sizeof(displacement));
	for (std::uint32_t i = JumpSize; i < size; ++i) out[i] = 0x90;
}

std::uint8_t* ExePage  = nullptr;
std::uint8_t* CorePage = nullptr;

// ---------------------------------------------------------------------------
// Game sites
// ---------------------------------------------------------------------------

auto Wanted(const ExeSite& site) noexcept -> bool {
	return site.boss ? Config.boss : Config.primeEvil;
}

auto CheckSite(const ExeSite& site) noexcept -> bool {
	char message[256] {};
	for (std::uint32_t i = 0; i < site.checkCount; ++i) {
		const Witness& check = site.checks[i];
		if (!Context->CheckExpectedBytes(check.rva, check.bytes, check.size)) {
			std::snprintf(message, sizeof(message), "%s: the bytes at 0x%llX do not match this build; not installed.", site.name,
			              static_cast<unsigned long long>(check.rva));
			LogError(message);
			return false;
		}
	}
	if (!Context->CheckExpectedBytes(site.rva, site.original, site.size)) {
		std::snprintf(message, sizeof(message),
		              "%s: the bytes at 0x%llX do not match (wrong build, or another plugin changed them); not installed.",
		              site.name, static_cast<unsigned long long>(site.rva));
		LogError(message);
		return false;
	}
	return true;
}

// Writes the site's stub into the page and prepares the jump to it.
void BuildStub(ExeSite& site, std::uint8_t* stub) noexcept {
	std::memcpy(stub, site.stub, site.stubSize);
	std::uint32_t mask = 0;
	std::memcpy(&mask, reinterpret_cast<const void*>(Base + site.maskRva), sizeof(mask));
	for (const std::size_t at : site.maskAt) {
		if (at == 0) continue;
		if (site.maskLowByte) {
			stub[at] = static_cast<std::uint8_t>(mask);
		} else {
			std::memcpy(stub + at, &mask, sizeof(mask));
		}
	}
	for (std::size_t i = 0; i < site.targetAt.size(); ++i) {
		if (site.targetAt[i] == 0) continue;
		const std::uint64_t target = Base + site.targetRva[i];
		std::memcpy(stub + site.targetAt[i], &target, sizeof(target));
	}
	EncodeJump(Base + site.rva, reinterpret_cast<std::uintptr_t>(stub), site.size, site.patch.data());
}

void InstallExeSites() noexcept {
	bool any = false;
	for (const ExeSite& site : Sites) any = any || Wanted(site);
	if (!any) return;

	if (!Context->CheckExpectedBytes(BitMasksRva, BitMasksBytes, sizeof(BitMasksBytes))) {
		LogError("The monstats flag mask table at 0x1D996D0 does not match this build; nothing installed in game code.");
		return;
	}
	ExePage = AllocateNear(Base + Sites[0].rva);
	if (ExePage == nullptr) {
		LogError("No page within reach of the game code; nothing installed in game code.");
		return;
	}
	std::memset(ExePage, 0xCC, PageBytes);

	bool ready[std::size(Sites)] {};
	for (std::size_t i = 0; i < std::size(Sites); ++i) {
		ExeSite& site = Sites[i];
		if (!Wanted(site) || !CheckSite(site)) continue;
		std::uint8_t* stub = ExePage + i * SiteStubSpacing;
		if (!CanReach(Base + site.rva, reinterpret_cast<std::uintptr_t>(stub))) continue;
		BuildStub(site, stub);
		ready[i] = true;
	}
	if (!Seal(ExePage)) {
		LogError("The stub page could not be made executable; nothing installed in game code.");
		return;
	}
	for (std::size_t i = 0; i < std::size(Sites); ++i) {
		ExeSite& site = Sites[i];
		if (!ready[i]) continue;
		site.installed = Context->PatchBytes(site.rva, site.original, site.size, site.patch.data(), site.size);
		char message[224] {};
		std::snprintf(message, sizeof(message), site.installed ? "%s: heralds count (0x%llX)." : "%s: patch at 0x%llX refused.",
		              site.name, static_cast<unsigned long long>(site.rva));
		site.installed ? LogInfo(message) : LogError(message);
	}
}

void RestoreExeSites() noexcept {
	for (ExeSite& site : Sites) {
		if (!site.installed) continue;
		if (Context->PatchBytes(site.rva, site.patch.data(), site.size, site.original, site.size)) site.installed = false;
	}
	// The page stays: a thread may be inside a stub right now.
}

// ---------------------------------------------------------------------------
// D2RCore site
// ---------------------------------------------------------------------------

std::uint8_t* CoreSiteAddress = nullptr;
std::uint8_t  CorePatch[sizeof(CoreSite)] {};
bool          CoreInstalled   = false;

auto WriteCode(std::uint8_t* at, const std::uint8_t* bytes, std::size_t size) noexcept -> bool {
	DWORD previous = 0;
	if (VirtualProtect(at, size, PAGE_EXECUTE_READWRITE, &previous) == FALSE) return false;
	std::memcpy(at, bytes, size);
	DWORD unused = 0;
	VirtualProtect(at, size, previous, &unused);
	FlushInstructionCache(GetCurrentProcess(), at, size);
	return std::memcmp(at, bytes, size) == 0;
}

void InstallCoreSite() noexcept {
	if (!Config.primeEvil) return;
	HMODULE module = GetModuleHandleW(CoreModuleName);
	if (module == nullptr) {
		LogError("D2RCore.dll is not loaded; its prime evil threat check is left as it is.");
		return;
	}
	auto* const core = reinterpret_cast<std::uint8_t*>(module);
	if (reinterpret_cast<std::uint8_t*>(GetProcAddress(module, CoreExportName)) != core + CoreExportRva
	    || std::memcmp(core + CoreExportRva, CoreThreatEntry, sizeof(CoreThreatEntry)) != 0
	    || std::memcmp(core + CoreWindowRva, CoreThreatWindow, sizeof(CoreThreatWindow)) != 0) {
		LogError("D2RCore's ApplyResistanceComponentWithPrimeEvilThreat is not the 1.3.1 one; its prime evil threat check is "
		         "left as it is.");
		return;
	}
	CoreSiteAddress = core + CoreSiteRva;
	CorePage        = AllocateNear(reinterpret_cast<std::uintptr_t>(CoreSiteAddress));
	if (CorePage == nullptr) {
		LogError("No page within reach of D2RCore; its prime evil threat check is left as it is.");
		return;
	}
	std::memset(CorePage, 0xCC, PageBytes);
	std::memcpy(CorePage, StubCoreThreat, sizeof(StubCoreThreat));
	const std::uint64_t notPrime = reinterpret_cast<std::uint64_t>(core + CoreNotPrimeRva);
	const std::uint64_t prime    = reinterpret_cast<std::uint64_t>(core + CorePrimeRva);
	std::memcpy(CorePage + CoreNotAt, &notPrime, sizeof(notPrime));
	std::memcpy(CorePage + CorePrimeAt, &prime, sizeof(prime));
	if (!Seal(CorePage)) {
		LogError("The D2RCore stub page could not be made executable; its prime evil threat check is left as it is.");
		return;
	}
	EncodeJump(reinterpret_cast<std::uintptr_t>(CoreSiteAddress), reinterpret_cast<std::uintptr_t>(CorePage), sizeof(CoreSite),
	           CorePatch);
	CoreInstalled = WriteCode(CoreSiteAddress, CorePatch, sizeof(CorePatch));
	if (CoreInstalled) {
		LogInfo("D2RCore prime evil threat: heralds count.");
	} else {
		WriteCode(CoreSiteAddress, CoreSite, sizeof(CoreSite));
		LogError("D2RCore prime evil threat: the patch could not be written; restored.");
	}
}

void RestoreCoreSite() noexcept {
	if (!CoreInstalled || CoreSiteAddress == nullptr) return;
	if (std::memcmp(CoreSiteAddress, CorePatch, sizeof(CorePatch)) == 0 && WriteCode(CoreSiteAddress, CoreSite, sizeof(CoreSite))) {
		CoreInstalled = false;
	}
}

// ---------------------------------------------------------------------------
// Console
// ---------------------------------------------------------------------------

auto __cdecl StatusCommand(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command, void*) noexcept
	-> D2RL::ConsoleCommandResult {
	if (command == nullptr || command->plugin == nullptr) return D2RL::ConsoleCommandResult::Failed;
	char line[256] {};
	std::snprintf(line, sizeof(line), "Herald Bosses 1.0.0: config %s | herald_boss %s, herald_prime_evil %s",
	              ConfigFileRead ? "loaded" : "NOT READ (defaults)", Config.boss ? "true" : "false",
	              Config.primeEvil ? "true" : "false");
	command->plugin->WriteConsoleMessage(line);
	for (const ExeSite& site : Sites) {
		std::snprintf(line, sizeof(line), "%s: %s", site.name, site.installed ? "on" : (Wanted(site) ? "OFF (see the log)" : "off"));
		command->plugin->WriteConsoleMessage(line);
	}
	std::snprintf(line, sizeof(line), "D2RCore prime evil threat: %s",
	              CoreInstalled ? "on" : (Config.primeEvil ? "OFF (see the log)" : "off"));
	command->plugin->WriteConsoleMessage(line);
	return D2RL::ConsoleCommandResult::Handled;
}

constexpr D2RL::PluginInfo HeraldBossesInfo {
	.infoSize    = D2RL::PluginInfoSize,
	.abiVersion  = D2RL_PLUGIN_ABI_VERSION,
	.id          = "celestialrayone.herald-bosses",
	.name        = "Herald Bosses",
	.version     = "1.0.0",
	.author      = "CelestialRayOne",
	.description = "Monsters that become heralds count as bosses and prime evils for every game rule that asks.",
	.flags       = D2RL::PluginFlags::Shared | D2RL::PluginFlags::NativeHooks,
};

}  // namespace

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* {
	return &HeraldBossesInfo;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
	if (context == nullptr) return false;
	Context = context;
	Base    = context->exeBase;
	if (Base == 0) {
		LogError("exeBase is zero; nothing installed.");
		return false;
	}
	LoadSettings();
	InstallExeSites();
	InstallCoreSite();
	if (!Context->RegisterConsoleCommand("heraldbosses", &StatusCommand, "Herald Bosses status: which parts are installed.")) {
		LogWarn("Could not register the heraldbosses console command.");
	}
	bool any = CoreInstalled;
	for (const ExeSite& site : Sites) any = any || site.installed;
	if (!any) {
		LogError((Config.boss || Config.primeEvil) ? "Nothing could be installed; unloading."
		                                           : "herald_boss and herald_prime_evil are both false; unloading.");
		return false;
	}
	return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
	if (Context == nullptr) return;
	RestoreExeSites();
	RestoreCoreSite();
	Context = nullptr;
}
