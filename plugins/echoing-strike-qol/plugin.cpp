// Soaring Strike (pSrvHitFunc 34) return-trip changes.
//
// See celestialrayone.soaring-strike.toml for the behaviour write-up. In short:
//
//   1. RE-HIT ON THE WAY BACK
//      Slot 34 gives the missile a fresh cast id when the return trip starts,
//      which resets next-hit delays, but it never touches the LastCollide slot
//      ([pMissile+0x10], type at +0x20, id at +0x24). That slot holds ONE pair,
//      the most recent target, so only the last monster struck outbound stays
//      blocked on the way home. This hook re-stamps it when the turn happens.
//
//   2. UNCAPPED RETURN PATH
//      sub_1403BA560 computes the return lifetime from min(distance, 50). The 50
//      is a literal `mov edi, 32h` at RVA 3BA6B1. The value is fed through
//      `shl edi, 10h`, so 0x7FFF is a hard ceiling: anything larger goes negative
//      and the missile expires on the spot.
//
// Built against D2R 3.3 (module 140000000.D2RLoader.exe,
// md5 baf085b077a4f9605bfddd977cfd9207) and PluginSDK v4.

#include <D2RLPlugin/api.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

// ---------------------------------------------------------------------------
// RVAs, all read out of the 3.3 image
// ---------------------------------------------------------------------------

// pSrvHitFunc 34 (HitSoaringStrike, server), table qword_142391030 slot 34.
// Returns 4, and only 4, when the return trip has just been started.
constexpr std::uint64_t HitSoaringStrikeRva = 0x00460180;

// MISSILE_SetLastCollide(pMissile, pTarget). Writes the target type to [pMD+0x20]
// and the target id to [pMD+0x24]; with a null target it writes -1 into the id
// instead. Internally gated on the missiles.txt LastCollide bit (missile record
// byte 36), so it is a no-op for missiles that do not use that column.
constexpr std::uint64_t SetLastCollideRva = 0x003BD580;

// MISSILE_GetOwner(pGame, pMissile). May return null if the owner is already gone.
constexpr std::uint64_t GetMissileOwnerRva = 0x00490300;

// `mov edi, 32h` inside sub_1403BA560, the return-path distance clamp.
constexpr std::uint64_t ReturnPathCapRva          = 0x003BA6B1;
constexpr std::uint64_t ReturnPathCapImmediateRva = 0x003BA6B2;

// The five pushes opening HitSoaringStrikeRva. Ten bytes, no rip-relative
// operands, clean instruction boundary, so the trampoline can relocate them.
//   40 55 push rbp / 41 54 push r12 / 41 55 push r13 / 41 56 push r14 / 41 57 push r15
constexpr std::uint8_t ExpectedHitPrologue[] { 0x40, 0x55, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57 };

// Full instruction, then just its immediate. Checking the whole instruction first
// proves the site really is `mov edi, 32h` and not an unrelated 0x32 byte.
constexpr std::uint8_t ExpectedCapInstruction[] { 0xBF, 0x32, 0x00, 0x00, 0x00 };
constexpr std::uint8_t ExpectedCapImmediate[] { 0x32, 0x00, 0x00, 0x00 };

constexpr std::int32_t StockReturnPathCap = 50;
constexpr std::int32_t MaxReturnPathCap   = 0x7FFF;  // shl edi, 10h must stay positive

// Return code from slot 34 meaning "the return trip just started".
constexpr std::int64_t ReturnTripStarted = 4;

// ---------------------------------------------------------------------------
// Game function types
// ---------------------------------------------------------------------------

using HitSoaringStrikeFn = std::int64_t(__fastcall*)(void* game, void* missile, void* target) noexcept;
using SetLastCollideFn   = void(__fastcall*)(void* missile, void* target) noexcept;
using GetMissileOwnerFn  = void*(__fastcall*)(void* game, void* missile) noexcept;

HitSoaringStrikeFn OriginalHitSoaringStrike = nullptr;
SetLastCollideFn   SetLastCollide           = nullptr;
GetMissileOwnerFn  GetMissileOwner          = nullptr;

bool ReseedWithOwner = true;

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------

struct Settings {
	bool         returnTripReHit = true;
	bool         reseedWithOwner = true;
	std::int32_t returnPathCap   = MaxReturnPathCap;
};

constexpr auto ByteSize(std::size_t size) noexcept -> std::uint32_t {
	return static_cast<std::uint32_t>(size);
}

template <std::size_t Size>
constexpr auto ByteCount(const std::uint8_t (&)[Size]) noexcept -> std::uint32_t {
	return ByteSize(Size);
}

constexpr auto IsBlank(char c) noexcept -> bool {
	return c == ' ' || c == '\t' || c == '\r';
}

// Finds `key = value` at the start of a line. The config is a single table, so
// section headers and comments are simply skipped rather than tracked.
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

			// A longer key such as return_path_cap_extra fails here, because the
			// character after the prefix is neither blank nor '='.
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

auto ReadInt(const char* text, const char* key, std::int32_t fallback) noexcept -> std::int32_t {
	const char* value  = nullptr;
	std::size_t length = 0;
	if (!FindConfigValue(text, key, value, length) || length == 0) {
		return fallback;
	}

	std::int64_t result = 0;
	for (std::size_t i = 0; i < length; ++i) {
		const char c = value[i];
		if (c == '_') {
			continue;  // TOML digit separator
		}
		if (c < '0' || c > '9') {
			return fallback;
		}
		result = result * 10 + (c - '0');
		if (result > MaxReturnPathCap * 64) {
			return fallback;
		}
	}

	return static_cast<std::int32_t>(result);
}

auto LoadSettings(const D2RL::PluginContext* context) noexcept -> Settings {
	Settings settings {};

	std::array<char, 8'192> buffer {};
	if (!context->ReadConfig(buffer.data(), ByteSize(buffer.size()))) {
		context->LogWarn("Could not read celestialrayone.soaring-strike.toml; using defaults.");
		return settings;
	}
	buffer.back() = '\0';

	settings.returnTripReHit = ReadBool(buffer.data(), "return_trip_rehit", settings.returnTripReHit);
	settings.reseedWithOwner = ReadBool(buffer.data(), "reseed_with_owner", settings.reseedWithOwner);
	settings.returnPathCap   = ReadInt(buffer.data(), "return_path_cap", settings.returnPathCap);
	return settings;
}

// ---------------------------------------------------------------------------
// The hook
// ---------------------------------------------------------------------------

// game, missile, target. target is null on an expiry or a wall hit.
auto __fastcall HookHitSoaringStrike(void* game, void* missile, void* target) noexcept -> std::int64_t {
	const HitSoaringStrikeFn original = OriginalHitSoaringStrike;
	if (original == nullptr) {
		return 0;
	}

	const std::int64_t result = original(game, missile, target);

	if (result == ReturnTripStarted && missile != nullptr && SetLastCollide != nullptr) {
		void* owner = nullptr;
		if (ReseedWithOwner && GetMissileOwner != nullptr) {
			owner = GetMissileOwner(game, missile);
		}
		// A null owner falls through to the engine's own clear path (id = -1),
		// which is still correct: nothing can match -1, so nothing stays blocked.
		SetLastCollide(missile, owner);
	}

	return result;
}

auto InstallReturnTripReHit(const D2RL::PluginContext* context, const Settings& settings) noexcept -> bool {
	ReseedWithOwner = settings.reseedWithOwner;

	const std::uintptr_t base = context->exeBase;
	if (base == 0) {
		context->LogError("exeBase is zero; cannot resolve game functions.");
		return false;
	}

	SetLastCollide  = reinterpret_cast<SetLastCollideFn>(base + SetLastCollideRva);
	GetMissileOwner = reinterpret_cast<GetMissileOwnerFn>(base + GetMissileOwnerRva);

	if (!context->InstallInlineHook(HitSoaringStrikeRva,
	                                ExpectedHitPrologue,
	                                ByteCount(ExpectedHitPrologue),
	                                HookHitSoaringStrike,
	                                &OriginalHitSoaringStrike)) {
		context->LogError("Inline hook at 0x460180 (pSrvHitFunc 34) failed; wrong game build, "
		                  "or the prologue does not match.");
		return false;
	}

	context->LogInfo(settings.reseedWithOwner
	                     ? "pSrvHitFunc 34 hooked; LastCollide is re-seeded with the owner on the return trip."
	                     : "pSrvHitFunc 34 hooked; LastCollide is cleared outright on the return trip.");
	return true;
}

auto ApplyReturnPathCap(const D2RL::PluginContext* context, const Settings& settings) noexcept -> bool {
	const std::int32_t cap = settings.returnPathCap;

	if (cap < 1 || cap > MaxReturnPathCap) {
		char message[160] {};
		std::snprintf(message,
		              sizeof(message),
		              "return_path_cap = %d is outside 1..%d; leaving the stock clamp of %d in place.",
		              cap,
		              MaxReturnPathCap,
		              StockReturnPathCap);
		context->LogError(message);
		return false;
	}

	if (cap == StockReturnPathCap) {
		context->LogInfo("return_path_cap is the stock 50; leaving the clamp untouched.");
		return true;
	}

	if (!context->CheckExpectedBytes(ReturnPathCapRva, ExpectedCapInstruction, ByteCount(ExpectedCapInstruction))) {
		context->LogError("The site at 0x3BA6B1 is not `mov edi, 32h`; wrong game build, or "
		                  "something else already owns it. Leaving the clamp untouched.");
		return false;
	}

	if (!context->PatchWriteU32(ReturnPathCapImmediateRva,
	                            ExpectedCapImmediate,
	                            ByteCount(ExpectedCapImmediate),
	                            static_cast<std::uint32_t>(cap))) {
		context->LogError("Failed to write the return-path clamp at 0x3BA6B2.");
		return false;
	}

	char message[128] {};
	std::snprintf(message, sizeof(message), "Return-path clamp raised from %d to %d.", StockReturnPathCap, cap);
	context->LogInfo(message);
	return true;
}

constexpr D2RL::PluginInfo SoaringStrikeInfo {
	.infoSize    = D2RL::PluginInfoSize,
	.apiVersion  = D2RL_PLUGIN_API_VERSION,
	.id          = "celestialrayone.soaring-strike",
	.name        = "Soaring Strike Return Trip",
	.version     = "1.0.0",
	.author      = "CelestialRayOne",
	.description = "Boomerang missiles (pSrvHitFunc 34) can strike their last outbound target "
	               "again on the way back, and the return path is no longer clamped to 50.",
	.flags       = D2RL::PluginFlags::Server | D2RL::PluginFlags::NativeHooks,
};

}  // namespace

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* {
	return &SoaringStrikeInfo;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
	if (context == nullptr) {
		return false;
	}

	const Settings settings = LoadSettings(context);

	bool reHit = true;
	if (settings.returnTripReHit) {
		reHit = InstallReturnTripReHit(context, settings);
	} else {
		context->LogInfo("return_trip_rehit is false; pSrvHitFunc 34 is left unhooked.");
	}

	const bool cap = ApplyReturnPathCap(context, settings);

	// The two changes are independent, so only a total failure unloads the plugin.
	if (!reHit && !cap) {
		context->LogError("Neither change could be applied; unloading.");
		return false;
	}

	return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
	OriginalHitSoaringStrike = nullptr;
	SetLastCollide           = nullptr;
	GetMissileOwner          = nullptr;
}
