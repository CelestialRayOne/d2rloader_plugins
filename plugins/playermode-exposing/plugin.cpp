// ---------------------------------------------------------------------------
// playermode-stat
//
// Exposes a unit's animation mode to softcode formulas as one virtual stat id
// (410 by default, configurable). Every other stat id is untouched and goes
// down the stock path.
//
// Target: Diablo II: Resurrected 3.3.93847 (also 3.2.92777 and Steam
// 3.3.93787 -- those builds share an executable identity for patching).
//
// Safety model: the RVA below is against image base 0x140000000, and nothing
// is installed unless the bytes already there match byte for byte. On a build
// this plugin does not recognise it installs nothing, logs loudly and stays
// loaded so the console command can explain why. An unrecognised build is a
// no-op, never a mis-patch.
//
// ---------------------------------------------------------------------------
// Where the hook goes, and how that was established
// ---------------------------------------------------------------------------
//
// .txt formulas are compiled to bytecode by the shared expression engine
// (D2Common\src\DataTbls\SharedBBE.cpp) and evaluated by a generic evaluator
// that is wired with a per-keyword callback table. The table lives at RVA
// 0x1D09C50 and is NINE 16-byte records of {callback VA : qword, arity : dword,
// padding : dword} -- not a plain array of function pointers:
//
//   record 0  0x3B3200  arity 2
//   record 1  0x3B3210  arity 2
//   record 2  0x3B3220  arity 2
//   record 3  0x3B32F0  arity 2
//   record 4  0x3B33A0  arity 2
//   record 5  0x3B33F0  arity 2   <- stat(), this is the one
//   record 6  0x3B3500  arity 3
//   record 7  0x3B3590  arity 2
//   record 8  0x3B3640  arity -1
//
// Read straight out of the image and byte-identical to the runtime capture
// published in the D2RLoader Suite research ledger (known-rvas.json,
// DATATBLS_BBEEvaluatorCallbackTable), which also pins the keyword string
// "stat" for record 5.
//
// sub_1403B33F0 disassembled in full (0x3B33F0..0x3B34F8, size 0x109):
//
//   RCX = game version byte, EDX = stat id, R8D = field code,
//   R9  = unused here,       [rsp+0x28] = evaluation context
//
//   0x3B33FF  mov rsi,[rsp+0x50]      ; = [entry rsp + 0x28] = 5th arg, context
//   0x3B3434  mov rsi,[rsi+0x10]      ; CONTEXT + 0x10 = the unit
//   0x3B343D  test edi,edi / js       ; negative stat id -> 0
//   0x3B3446  call 0x300A90           ; GetGameData(version)
//   0x3B344B  mov rbp,[rax+0x1260]    ; itemstatcost row count
//   0x3B3472  cmp edi,ebp / jge       ; id past the last row -> 0
//   0x3B347B  cmp edi,0x13 / jne      ; stat 19 (tohit) -> tail 0x3483C0
//   field 0 -> tail 0x2F5020  STATLIST_GetUnitStat      (.accr)
//   field 1 -> tail 0x2F48C0  STATLIST_GetUnitBaseStat  (.base)
//   field 2 -> tail 0x2F5C60  STATLIST_UnitGetStatValue (.mod)
//
// Two things follow from that and both matter here.
//
// First, the unit is at CONTEXT + 0x10 on this build. The legacy ESR byte
// patch for this feature read it at CONTEXT + 0 -- that was correct for the
// build it was written against and is wrong here. This is not a detail that
// can be carried over on faith.
//
// Second, hooking the entry puts the answer BEFORE the row-count guard at
// 0x3B3472 and before the field dispatch, so the virtual id does not need a
// real itemstatcost row and .base / .mod / .accr all return the same value.
//
// The mode itself is the 32-bit field at Unit + 0x0C. Confirmed against
// UNITS_GetUnitMode (RVA 0x34AB60), which is exactly
// `return unit ? *(uint32*)(unit + 12) : 0`.
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
// Native contract
// ---------------------------------------------------------------------------

constexpr std::uint64_t StatFieldResolverRva = 0x003B33F0ULL;

// Offset of the unit inside the evaluation context struct (0x3B3434).
constexpr std::uint64_t ContextUnitOffset = 0x10ULL;

// Offset of the 32-bit animation mode inside a unit (UNITS_GetUnitMode).
constexpr std::uint64_t UnitModeOffset = 0x0CULL;

// Verified before anything is installed. This covers the entry, the null
// context path, and the CONTEXT+0x10 unit read this plugin depends on, so the
// plugin also refuses on a build where the context layout moved rather than
// silently reading the wrong field. 77 bytes, 0x3B33F0..0x3B343C.
constexpr std::uint8_t StatFieldResolverBody[] {
	0x48, 0x89, 0x5C, 0x24, 0x10,             // mov  [rsp+10h], rbx
	0x48, 0x89, 0x74, 0x24, 0x18,             // mov  [rsp+18h], rsi
	0x57,                                     // push rdi
	0x48, 0x83, 0xEC, 0x20,                   // sub  rsp, 20h
	0x48, 0x8B, 0x74, 0x24, 0x50,             // mov  rsi, [rsp+50h]      ; context
	0x41, 0x8B, 0xD8,                         // mov  ebx, r8d            ; field code
	0x8B, 0xFA,                               // mov  edi, edx            ; stat id
	0x48, 0x85, 0xF6,                         // test rsi, rsi
	0x75, 0x26,                               // jnz  0003B3434
	0x48, 0x8D, 0x4C, 0x24, 0x50,             // lea  rcx, [rsp+50h]
	0x40, 0x88, 0x74, 0x24, 0x50,             // mov  [rsp+50h], sil
	0xE8, 0xF3, 0x0E, 0x00, 0x00,             // call 0003B4310
	0x84, 0xC0,                               // test al, al
	0x74, 0x01,                               // jz   0003B3422
	0xCC,                                     // int3
	0x33, 0xC0,                               // xor  eax, eax
	0x48, 0x8B, 0x5C, 0x24, 0x38,             // mov  rbx, [rsp+38h]
	0x48, 0x8B, 0x74, 0x24, 0x40,             // mov  rsi, [rsp+40h]
	0x48, 0x83, 0xC4, 0x20,                   // add  rsp, 20h
	0x5F,                                     // pop  rdi
	0xC3,                                     // retn
	0x48, 0x8B, 0x76, 0x10,                   // mov  rsi, [rsi+10h]      ; unit
	0x48, 0x85, 0xF6,                         // test rsi, rsi
	0x74, 0xE5,                               // jz   0003B3422
};

// The bytes the inline hook displaces: mov [rsp+10h], rbx. One instruction,
// position independent, no rip-relative operand and no branch. Every branch
// inside the function was decoded and none targets 0x3B33F0..0x3B33F4, so
// nothing can land in the middle of the displaced region.
constexpr std::uint8_t StatFieldResolverPrologue[] {
	0x48, 0x89, 0x5C, 0x24, 0x10,
};

static_assert(sizeof(StatFieldResolverBody) == 77, "Verified body length changed.");
static_assert(sizeof(StatFieldResolverPrologue) == 5, "Verified prologue length changed.");
static_assert(sizeof(StatFieldResolverPrologue) >= 5, "An inline hook needs at least 5 displaced bytes for a rel32 jump.");

constexpr auto PrologueIsPrefixOfBody() noexcept -> bool {
	for (std::size_t index = 0; index < sizeof(StatFieldResolverPrologue); ++index) {
		if (StatFieldResolverPrologue[index] != StatFieldResolverBody[index]) {
			return false;
		}
	}
	return true;
}

static_assert(PrologueIsPrefixOfBody(), "The displaced bytes must be the leading bytes of the verified body.");

// Opaque parameters are carried at full width so the pass-through hands the
// original exactly the registers the evaluator set up.
using StatFieldResolverFn = std::int64_t(__fastcall*)(
	std::uint64_t gameVersion,
	std::int32_t  statId,
	std::int32_t  fieldCode,
	std::uint64_t reserved,
	std::uint64_t context) noexcept;

StatFieldResolverFn OriginalStatFieldResolver = nullptr;

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

constexpr std::int32_t DefaultStatId = 410;
constexpr std::int32_t MinimumStatId = 0;
constexpr std::int32_t MaximumStatId = 32767;

// Kept identical to the shipped celestialrayone.playermode-stat.toml. This is
// what EnsureConfig writes when the file does not exist yet.
constexpr const char* DefaultConfigToml =
R"toml(# Playermode Stat - expose a unit's animation mode to softcode formulas
#
# Adds one virtual stat id that any .txt formula can read with stat('...'),
# and that resolves to the current animation mode of the unit the formula is
# being evaluated against. Nothing else changes: every other stat id resolves
# exactly as it did before, through the stock code path.
#
# Use it to branch a calc on what the unit is doing right now, for example
# only granting a bonus while running, or only while in the cast animation.
#
# How to use it from a .txt: give the id below a row name in itemstatcost.txt
# (or just reference it by an existing name that maps to it) and read it like
# any other stat, e.g.  (stat('playermode'.accr)==3)*someBonus
# The field spelling does not matter. The hook answers before the engine gets
# as far as the .base / .mod / .accr dispatch, so .accr, .base and .mod all
# return the same mode value.
#
# PLAYER modes (unit type 0):
#    0 death       1 neutral      2 walk        3 run
#    4 gethit      5 townneutral  6 townwalk    7 attack1
#    8 attack2     9 block       10 cast       11 throw
#   12 kick       13 skill1      14 skill2     15 skill3
#   16 skill4     17 dead        18 sequence   19 knockback
#
# MONSTER modes (unit type 1) use a different table:
#    0 death       1 neutral      2 walk        3 gethit
#    4 attack1     5 attack2      6 block       7 cast
#    8 skill1      9 skill2      10 skill3     11 skill4
#   12 dead       13 knockback   14 sequence   15 run
#
# The value returned is the mode of whichever unit the formula is being
# evaluated for, so check the unit type first if a calc can run on both.
#
# This file lives at <scope>\d2rloader\config\celestialrayone.playermode-stat.toml
# and is created with these defaults on first run. Delete it to get them back.
# It is read once at plugin load, so restart the game after editing.
#
# Type `playermode-stat` in the console to see whether the hook is live.


[playermode-stat]

# Master switch.
#   true  - the virtual stat is installed and resolves as described above.
#   false - the DLL stays loaded and the console command still answers, but no
#           hook is installed and stat_id resolves the stock way (which, for an
#           id past the end of itemstatcost.txt, means it returns 0).
# Default: true
enabled = true

# The stat id the virtual playermode stat answers on.
#
# Pick an id that is PAST the last row of your itemstatcost.txt. The hook is
# checked before the engine's row-count guard, so the id does not need a real
# itemstatcost row to work - but if you point it at an id that DOES have one,
# that real stat becomes unreadable from every formula in the mod, because this
# hook answers first. There is no way for the plugin to detect that for you.
#
# Must be between 0 and 32767. Anything outside that range is refused and the
# plugin falls back to 410.
# Default: 410
stat_id = 410
)toml";

// The loader hands the plugin the raw text of its own TOML file; there is no
// parser in the SDK. This is a small line scanner that understands exactly
// what this file needs: [section] headers, `key = value`, `#` comments (whole
// line or trailing), and both LF and CRLF. Anything it does not understand it
// ignores, which is the right failure mode for a file edited by hand.

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
	const char* text   = literal;
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

// Finds the raw right-hand side of section.key. Last assignment wins.
auto FindConfigValue(const char* toml, const char* section, const char* key, Slice& value) noexcept -> bool {
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

				const Slice name = Trim({ trimmed.begin + 1, close });
				std::size_t used = 0;
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
						value = raw;
						found = true;
					}
				}
			}
		}

		line = (*lineEnd == '\0') ? lineEnd : lineEnd + 1;
	}

	return found;
}

// Both readers leave the caller's value untouched unless the key was present
// AND parsed cleanly.
auto ReadConfigBool(const char* toml, const char* section, const char* key, bool& value) noexcept -> bool {
	Slice raw {};
	if (!FindConfigValue(toml, section, key, raw)) {
		return false;
	}

	if (SliceEquals(raw, "true")) {
		value = true;
		return true;
	}
	if (SliceEquals(raw, "false")) {
		value = false;
		return true;
	}
	return false;
}

auto ReadConfigInt(const char* toml, const char* section, const char* key, std::int32_t& value) noexcept -> bool {
	Slice raw {};
	if (!FindConfigValue(toml, section, key, raw)) {
		return false;
	}

	const char* cursor   = raw.begin;
	bool        negative = false;
	if (cursor < raw.end && (*cursor == '+' || *cursor == '-')) {
		negative = (*cursor == '-');
		++cursor;
	}

	if (cursor >= raw.end) {
		return false;
	}

	std::int64_t parsed = 0;
	for (; cursor < raw.end; ++cursor) {
		if (*cursor == '_') { // TOML digit separator
			continue;
		}
		if (*cursor < '0' || *cursor > '9') {
			return false;
		}
		parsed = parsed * 10 + (*cursor - '0');
		if (parsed > 0x7FFFFFFFLL) {
			return false;
		}
	}

	value = static_cast<std::int32_t>(negative ? -parsed : parsed);
	return true;
}

struct PlayermodeConfig {
	bool         enabled { true };
	std::int32_t statId { DefaultStatId };
};

PlayermodeConfig Config {};
bool             ConfigFileWasRead = false;

// ---------------------------------------------------------------------------
// The hook
// ---------------------------------------------------------------------------

// Read once at load, never written afterwards, so the hook needs no ordering
// beyond a relaxed load. Kept separate from Config so the hot path touches one
// cache line and no struct offsets.
std::atomic<std::int32_t> ActiveStatId { DefaultStatId };

// Diagnostics only. Written on the rare matching path, never on pass-through.
std::atomic<std::uint64_t> ResolutionsServed { 0 };

auto __fastcall HookStatFieldResolver(
	std::uint64_t gameVersion,
	std::int32_t  statId,
	std::int32_t  fieldCode,
	std::uint64_t reserved,
	std::uint64_t context) noexcept -> std::int64_t {

	if (statId == ActiveStatId.load(std::memory_order_relaxed) && context != 0) {
		const auto unit = *reinterpret_cast<void* const*>(context + ContextUnitOffset);
		if (unit != nullptr) {
			ResolutionsServed.fetch_add(1, std::memory_order_relaxed);
			const auto mode = *reinterpret_cast<const std::uint32_t*>(
				static_cast<const char*>(unit) + UnitModeOffset);
			return static_cast<std::int64_t>(mode);
		}
	}

	// Everything else, including a null context or a context with no unit,
	// goes down the stock path untouched. The stock function has its own
	// diagnostics for those two cases and swallowing them here would hide a
	// real bug.
	const StatFieldResolverFn original = OriginalStatFieldResolver;
	return original != nullptr ? original(gameVersion, statId, fieldCode, reserved, context) : 0;
}

// ---------------------------------------------------------------------------
// Plumbing
// ---------------------------------------------------------------------------

enum class HookState : std::uint8_t {
	NotAttempted,
	Installed,
	DisabledByConfig,
	UnsupportedBuild,
	InstallFailed,
};

std::atomic<HookState> ResolverHookState { HookState::NotAttempted };

template <std::size_t Size>
constexpr auto ByteCount(const std::uint8_t (&)[Size]) noexcept -> std::uint32_t {
	return static_cast<std::uint32_t>(Size);
}

void LoadConfiguration(const D2RL::PluginContext* context) noexcept {
	if (context == nullptr) {
		return;
	}

	if (!context->EnsureConfig(DefaultConfigToml)) {
		context->LogWarn(
			"Could not create or open celestialrayone.playermode-stat.toml. "
			"Running with defaults: enabled, stat id 410.");
		return;
	}

	std::array<char, 8192> buffer {};
	std::uint32_t          requiredSize = 0;

	if (!context->ReadConfig(buffer.data(), static_cast<std::uint32_t>(buffer.size() - 1), &requiredSize)) {
		context->LogWarn(
			"Could not read celestialrayone.playermode-stat.toml. "
			"Running with defaults: enabled, stat id 410.");
		return;
	}

	if (requiredSize >= buffer.size()) {
		D2RL::LogWarnF(
			context,
			"celestialrayone.playermode-stat.toml is %u bytes, larger than the %zu byte read buffer. "
			"Running with defaults: enabled, stat id 410.",
			requiredSize,
			buffer.size());
		return;
	}

	buffer[buffer.size() - 1] = '\0';
	ConfigFileWasRead         = true;

	(void)ReadConfigBool(buffer.data(), "playermode-stat", "enabled", Config.enabled);

	std::int32_t requestedStatId = Config.statId;
	if (ReadConfigInt(buffer.data(), "playermode-stat", "stat_id", requestedStatId)) {
		if (requestedStatId < MinimumStatId || requestedStatId > MaximumStatId) {
			D2RL::LogWarnF(
				context,
				"stat_id %d is outside the allowed range %d..%d. Falling back to %d.",
				requestedStatId,
				MinimumStatId,
				MaximumStatId,
				DefaultStatId);
			Config.statId = DefaultStatId;
		} else {
			Config.statId = requestedStatId;
		}
	}

	ActiveStatId.store(Config.statId, std::memory_order_relaxed);

	D2RL::LogInfoF(
		context,
		"config: enabled=%s, stat_id=%d.",
		Config.enabled ? "true" : "false",
		Config.statId);
}

auto InstallStatFieldResolverHook(const D2RL::PluginContext* context) noexcept -> bool {
	if (context == nullptr) {
		return false;
	}

	if (!Config.enabled) {
		ResolverHookState.store(HookState::DisabledByConfig, std::memory_order_release);
		context->LogInfo("Playermode stat not installed: turned off in the config file.");
		return false;
	}

	if (!context->CheckExpectedBytes(StatFieldResolverRva, StatFieldResolverBody, ByteCount(StatFieldResolverBody))) {
		ResolverHookState.store(HookState::UnsupportedBuild, std::memory_order_release);
		context->LogError(
			"Playermode stat NOT installed: the formula stat() resolver at RVA 003B33F0 does not match "
			"the verified 3.3.93847 body. This build is not supported; nothing was patched.");
		return false;
	}

	if (!context->InstallInlineHook(
			StatFieldResolverRva,
			StatFieldResolverPrologue,
			ByteCount(StatFieldResolverPrologue),
			HookStatFieldResolver,
			&OriginalStatFieldResolver)) {
		ResolverHookState.store(HookState::InstallFailed, std::memory_order_release);
		context->LogError("Playermode stat NOT installed: InstallInlineHook failed at RVA 003B33F0.");
		return false;
	}

	if (OriginalStatFieldResolver == nullptr) {
		ResolverHookState.store(HookState::InstallFailed, std::memory_order_release);
		context->LogError("Playermode stat NOT installed: the loader returned no trampoline.");
		return false;
	}

	ResolverHookState.store(HookState::Installed, std::memory_order_release);
	D2RL::LogInfoF(context, "Playermode stat installed at RVA 003B33F0, answering on stat id %d.", Config.statId);
	return true;
}

auto PlayermodeStatCommand(
	D2R::Game::Client*                 client,
	const D2RL::ConsoleCommandContext* command,
	void*                              userData) noexcept -> D2RL::ConsoleCommandResult {
	(void)client;
	(void)userData;

	if (command == nullptr || command->plugin == nullptr) {
		return D2RL::ConsoleCommandResult::Failed;
	}

	const D2RL::PluginContext* context = command->plugin;

	char header[176] {};
	std::snprintf(
		header,
		sizeof(header),
		"playermode-stat: config %s, stat id %d.",
		ConfigFileWasRead ? "loaded" : "NOT READ (defaults in use)",
		Config.statId);
	context->WriteConsoleMessage(header);

	char message[208] {};
	switch (ResolverHookState.load(std::memory_order_acquire)) {
	case HookState::Installed:
		std::snprintf(
			message,
			sizeof(message),
			"stat() resolver hook: active. playermode reads served so far: %llu",
			static_cast<unsigned long long>(ResolutionsServed.load(std::memory_order_relaxed)));
		context->WriteConsoleMessage(message);
		break;

	case HookState::DisabledByConfig:
		context->WriteConsoleWarning(
			"stat() resolver hook: OFF because the config file turns it off. The game build is fine.");
		break;

	case HookState::UnsupportedBuild:
		context->WriteConsoleError(
			"stat() resolver hook: NOT ACTIVE. This game build is not recognised and nothing was patched.");
		break;

	case HookState::InstallFailed:
		context->WriteConsoleError(
			"stat() resolver hook: NOT ACTIVE. The bytes matched but the hook could not be installed. "
			"See the plugin log.");
		break;

	case HookState::NotAttempted:
	default:
		context->WriteConsoleError("stat() resolver hook: NOT ACTIVE. Installation was never attempted.");
		break;
	}

	return D2RL::ConsoleCommandResult::Handled;
}

constexpr D2RL::PluginInfo PlayermodeStatInfo {
	.infoSize    = D2RL::PluginInfoSize,
	.apiVersion  = D2RL_PLUGIN_API_VERSION,
	.id          = "celestialrayone.playermode-stat",
	.name        = "Playermode Stat",
	.version     = "0.1.0",
	.author      = "CelestialRayOne",
	.description = "Exposes a unit's animation mode to .txt formulas as a configurable virtual stat id.",
	// Shared, not Client or Server: .txt formulas are evaluated on both sides
	// and the two must agree on what the virtual stat returns.
	.flags       = D2RL::PluginFlags::Shared | D2RL::PluginFlags::NativeHooks,
};

static_assert(D2RL::HasValidPluginRole(PlayermodeStatInfo.flags), "Exactly one execution role must be set.");
static_assert(D2RL::HasOnlyKnownPluginFlags(PlayermodeStatInfo.flags), "Unknown plugin flag set.");
static_assert(D2RL::HasFlag(PlayermodeStatInfo.flags, D2RL::PluginFlags::NativeHooks), "Inline hooks require the NativeHooks flag.");

} // namespace

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* {
	return &PlayermodeStatInfo;
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
			"playermode-stat",
			PlayermodeStatCommand,
			"Report whether the playermode virtual stat is active.")) {
		context->LogWarn("The playermode-stat console command was not registered.");
	}

	if (const char* build = D2RL::GetBuildVersion(context); build != nullptr) {
		D2RL::LogInfoF(context, "playermode-stat loading against build %s.", build);
	}

	LoadConfiguration(context);

	if (!InstallStatFieldResolverHook(context)) {
		context->LogError("playermode-stat loaded with the virtual stat NOT active.");
		return true;
	}

	context->LogInfo("playermode-stat loaded.");
	return true;
}

// An installed inline hook cannot be withdrawn, and the trampoline lives in
// this module, so there is deliberately nothing to undo here. The loader is
// expected to keep the DLL resident for the life of the process.
D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {}
