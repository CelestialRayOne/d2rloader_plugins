// ---------------------------------------------------------------------------
// monster-drop-cap
//
// Raises the vanilla ceiling of 6 items per monster drop to a configurable
// number. Nothing else about dropping changes: treasure class selection, pick
// counts, NoDrop, quality rolls, Magic Find and the RNG are all untouched.
//
// Target: Diablo II: Resurrected 3.3.93847 (also 3.2.92777 and Steam
// 3.3.93787 -- those builds share an executable identity for patching).
//
// Safety model: the RVAs below are against image base 0x140000000, and nothing
// is installed unless the bytes already there match byte for byte. On a build
// this plugin does not recognise it installs nothing, logs loudly and stays
// loaded so the console command can explain why. An unrecognised build is a
// no-op, never a mis-patch.
//
// ---------------------------------------------------------------------------
// Where the cap lives, and how that was established
// ---------------------------------------------------------------------------
//
// The public server drop entry is TREASURECLASS_GenerateDrops at RVA 0x441300.
// It is a thin wrapper: it resolves the treasure class record from the id and
// tail-calls the real generator sub_1404404F0 (RVA 0x4404F0), forwarding all
// ten arguments unchanged. sub_1404404F0 has exactly two callers, 0x44148C
// inside that wrapper and 0x59333D, so hooking the generator covers strictly
// more ground than hooking the wrapper.
//
// Its tenth argument is the drop cap. The generator opens with:
//
//   0x4405F7  mov  eax, [rbp+948h]        ; a10, the requested cap
//   0x4405FD  mov  [rsp+68h], eax         ; the working cap
//   0x440601  test r14, r14               ; a8, the caller's output array
//   0x440604  jnz  00440614
//   0x440606  test eax, eax
//   0x440608  jg   0044062C               ; a10 > 0 -> use it as given
//   0x44060A  mov  dword ptr [rsp+68h], 6 ; <- THE VANILLA CAP
//   0x440612  jmp  0044062C
//   0x440614  test eax, eax               ; a8 set but a10 zero -> assert
//
// and enforces it at 0x44115F with `if (++*count >= cap) goto done;` after each
// item is spawned.
//
// Two branches, and the difference between them is the whole safety argument
// for this plugin:
//
//   a8 == 0  -- no output array. The caller wants items on the floor and does
//              not collect pointers to them. a10 <= 0 means "use the default",
//              and the default is the hard-coded 6. This is the monster path,
//              and it is the ONLY branch this plugin touches.
//
//   a8 != 0  -- the caller passed an array of a10 QWORDs, filled at 0x44114F
//              with `array[count] = item`. Here a10 is a BUFFER CAPACITY, not
//              a policy knob. Raising it would write past the caller's array.
//              The hook never modifies a10 when a8 is non-null.
//
// The monster call site (0x447E17, reached through the wrapper) sets both to
// zero: `xor ecx,ecx` then `mov [rsp+38h],rcx` for a8 and `mov [rsp+48h],ecx`
// for a10. That is exactly the branch above, so a monster drop is capped at 6
// today.
//
// The unit that is dropping is argument 2. Unit type is the dword at Unit+0x00
// (0 player, 1 monster, 4 item), so the hook can scope itself to monsters
// without calling into the game.
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

constexpr std::uint64_t GenerateDropsRva = 0x004404F0ULL;

// The a8/a10 -> 6 default block. Verified separately from the entry so the
// plugin refuses if the branch this hook mirrors, or the literal 6, ever move.
constexpr std::uint64_t DefaultCapBlockRva = 0x004405F7ULL;

constexpr std::int32_t UnitTypeMonster  = 1;
constexpr std::int32_t VanillaDropCap   = 6;

// Entry of sub_1404404F0 through `sub rsp, 9E8h`. Pins the frame layout the
// argument offsets in the default-cap block depend on.
constexpr std::uint8_t GenerateDropsEntry[] {
	0x4C, 0x8B, 0xDC,                         // mov  r11, rsp
	0x55,                                     // push rbp
	0x53,                                     // push rbx
	0x49, 0x8D, 0xAB, 0x08, 0xF7, 0xFF, 0xFF, // lea  rbp, [r11-8F8h]
	0x48, 0x81, 0xEC, 0xE8, 0x09, 0x00, 0x00, // sub  rsp, 9E8h
};

// The displaced bytes: mov r11,rsp / push rbp / push rbx. Three complete
// instructions, all position independent, no rip-relative operand and no
// branch. `mov r11, rsp` captures the frame base before the pushes, which the
// `lea rbp, [r11-8F8h]` two instructions later consumes -- relocating it into
// the trampoline is correct because the trampoline is entered with RSP exactly
// as the real entry would see it.
constexpr std::uint8_t GenerateDropsPrologue[] {
	0x4C, 0x8B, 0xDC,
	0x55,
	0x53,
};

constexpr std::uint8_t DefaultCapBlock[] {
	0x8B, 0x85, 0x48, 0x09, 0x00, 0x00,       // mov  eax, [rbp+948h]      ; a10
	0x89, 0x44, 0x24, 0x68,                   // mov  [rsp+68h], eax
	0x4D, 0x85, 0xF6,                         // test r14, r14             ; a8
	0x75, 0x0E,                               // jnz  00440614
	0x85, 0xC0,                               // test eax, eax
	0x7F, 0x22,                               // jg   0044062C
	0xC7, 0x44, 0x24, 0x68, 0x06, 0x00, 0x00, 0x00, // mov [rsp+68h], 6
	0xEB, 0x18,                               // jmp  0044062C
};

static_assert(sizeof(GenerateDropsEntry) == 19, "Verified entry length changed.");
static_assert(sizeof(GenerateDropsPrologue) == 5, "Verified prologue length changed.");
static_assert(sizeof(GenerateDropsPrologue) >= 5, "An inline hook needs at least 5 displaced bytes for a rel32 jump.");
static_assert(sizeof(DefaultCapBlock) == 29, "Verified default-cap block length changed.");

constexpr auto PrologueIsPrefixOfEntry() noexcept -> bool {
	for (std::size_t index = 0; index < sizeof(GenerateDropsPrologue); ++index) {
		if (GenerateDropsPrologue[index] != GenerateDropsEntry[index]) {
			return false;
		}
	}
	return true;
}

static_assert(PrologueIsPrefixOfEntry(), "The displaced bytes must be the leading bytes of the verified entry.");

// The generator's own ABI. Arguments the hook does not inspect are carried at
// full width so the pass-through writes each stack slot back byte for byte.
using GenerateDropsFn = std::int64_t(__fastcall*)(
	std::uint64_t game,
	void*         spawner,      // the unit that is dropping
	void*         looter,       // the unit credited with the kill
	std::uint64_t treasureClass,
	std::uint64_t arg5,
	std::uint64_t arg6,
	std::uint64_t arg7,
	std::uint64_t outputArray,  // non-null means arg10 is that array's capacity
	std::uint64_t outputCount,
	std::int32_t  maxDrops) noexcept;

GenerateDropsFn OriginalGenerateDrops = nullptr;

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

constexpr std::int32_t DefaultDropCap = 50;
constexpr std::int32_t MinimumDropCap = 1;
constexpr std::int32_t MaximumDropCap = 999;

// Kept identical to the shipped celestialrayone.monster-drop-cap.toml. This is
// what EnsureConfig writes when the file does not exist yet.
constexpr const char* DefaultConfigToml =
R"toml(# Monster Drop Cap - raise the number of items one monster can drop
#
# Vanilla hard-codes a monster to a maximum of 6 items per drop, no matter how
# many picks its treasure class rolls. This raises that number and nothing
# else: the treasure class, the pick count, NoDrop, quality rolls, Magic Find
# and the RNG are all untouched. Monsters whose TC never rolls more than 6
# items keep dropping exactly what they dropped before.
#
# Scope: monsters only. Chests, urns, other objects and player corpses share
# the same drop code but keep the vanilla limit of 6, because the plugin checks
# the unit type of whatever is dropping and only steps in for monsters.
#
# Not affected, and worth knowing if the drops still look small: a treasure
# class only ever produces as many items as its Picks column allows, and the
# NoDrop weight still gets a roll for every pick. Raising this number does not
# make anything drop more; it only removes the ceiling once something already
# would have.
#
# This file lives at <scope>\d2rloader\config\celestialrayone.monster-drop-cap.toml
# and is created with these defaults on first run. Delete it to get them back.
# It is read once at plugin load, so restart the game after editing.
#
# Type `monster-drop-cap` in the console to see whether the hook is live.


[monster-drop-cap]

# Master switch.
#   true  - monsters may drop up to drop_cap items.
#   false - the DLL stays loaded and the console command still answers, but no
#           hook is installed and monsters keep the vanilla limit of 6.
# Default: true
enabled = true

# Maximum number of items a single monster drop may produce.
#
# 6 reproduces vanilla exactly. Anything higher only matters for treasure
# classes that can actually roll that many picks.
#
# Every item spawned has to find a free floor tile, so very large values will
# scatter loot a long way from the corpse and cost time on every kill. 50 is a
# comfortable ceiling for a mod with large Picks values; treat anything past a
# few hundred as a stress test rather than a setting.
#
# Must be between 1 and 999. Anything outside that range is refused and the
# plugin falls back to 50.
# Default: 50
drop_cap = 50
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

struct DropCapConfig {
	bool         enabled { true };
	std::int32_t dropCap { DefaultDropCap };
};

DropCapConfig Config {};
bool          ConfigFileWasRead = false;

// ---------------------------------------------------------------------------
// The hook
// ---------------------------------------------------------------------------

// Read once at load, never written afterwards.
std::atomic<std::int32_t> ActiveDropCap { DefaultDropCap };

// Diagnostics only. Written on the monster path, never on any other.
std::atomic<std::uint64_t> DropsRaised { 0 };

auto __fastcall HookGenerateDrops(
	std::uint64_t game,
	void*         spawner,
	void*         looter,
	std::uint64_t treasureClass,
	std::uint64_t arg5,
	std::uint64_t arg6,
	std::uint64_t arg7,
	std::uint64_t outputArray,
	std::uint64_t outputCount,
	std::int32_t  maxDrops) noexcept {

	// outputArray == 0 and maxDrops <= 0 is precisely the branch that would
	// otherwise fall through to the hard-coded 6. Any other combination is
	// left exactly as the caller set it up -- in particular a non-null
	// outputArray, where maxDrops is that array's capacity and raising it
	// would write past the end of the caller's buffer.
	if (outputArray == 0 && maxDrops <= 0 && spawner != nullptr
	    && *static_cast<const std::int32_t*>(spawner) == UnitTypeMonster) {
		maxDrops = ActiveDropCap.load(std::memory_order_relaxed);
		DropsRaised.fetch_add(1, std::memory_order_relaxed);
	}

	const GenerateDropsFn original = OriginalGenerateDrops;
	return original != nullptr
		? original(game, spawner, looter, treasureClass, arg5, arg6, arg7, outputArray, outputCount, maxDrops)
		: 0;
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

std::atomic<HookState> DropCapHookState { HookState::NotAttempted };

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
			"Could not create or open celestialrayone.monster-drop-cap.toml. "
			"Running with defaults: enabled, cap 50.");
		return;
	}

	std::array<char, 8192> buffer {};
	std::uint32_t          requiredSize = 0;

	if (!context->ReadConfig(buffer.data(), static_cast<std::uint32_t>(buffer.size() - 1), &requiredSize)) {
		context->LogWarn(
			"Could not read celestialrayone.monster-drop-cap.toml. "
			"Running with defaults: enabled, cap 50.");
		return;
	}

	if (requiredSize >= buffer.size()) {
		D2RL::LogWarnF(
			context,
			"celestialrayone.monster-drop-cap.toml is %u bytes, larger than the %zu byte read buffer. "
			"Running with defaults: enabled, cap 50.",
			requiredSize,
			buffer.size());
		return;
	}

	buffer[buffer.size() - 1] = '\0';
	ConfigFileWasRead         = true;

	(void)ReadConfigBool(buffer.data(), "monster-drop-cap", "enabled", Config.enabled);

	std::int32_t requestedCap = Config.dropCap;
	if (ReadConfigInt(buffer.data(), "monster-drop-cap", "drop_cap", requestedCap)) {
		if (requestedCap < MinimumDropCap || requestedCap > MaximumDropCap) {
			D2RL::LogWarnF(
				context,
				"drop_cap %d is outside the allowed range %d..%d. Falling back to %d.",
				requestedCap,
				MinimumDropCap,
				MaximumDropCap,
				DefaultDropCap);
			Config.dropCap = DefaultDropCap;
		} else {
			Config.dropCap = requestedCap;
		}
	}

	ActiveDropCap.store(Config.dropCap, std::memory_order_relaxed);

	D2RL::LogInfoF(
		context,
		"config: enabled=%s, drop_cap=%d (vanilla is %d).",
		Config.enabled ? "true" : "false",
		Config.dropCap,
		VanillaDropCap);
}

auto InstallDropCapHook(const D2RL::PluginContext* context) noexcept -> bool {
	if (context == nullptr) {
		return false;
	}

	if (!Config.enabled) {
		DropCapHookState.store(HookState::DisabledByConfig, std::memory_order_release);
		context->LogInfo("Monster drop cap not installed: turned off in the config file.");
		return false;
	}

	if (!context->CheckExpectedBytes(GenerateDropsRva, GenerateDropsEntry, ByteCount(GenerateDropsEntry))) {
		DropCapHookState.store(HookState::UnsupportedBuild, std::memory_order_release);
		context->LogError(
			"Monster drop cap NOT installed: the treasure class drop generator at RVA 004404F0 does not "
			"match the verified 3.3.93847 entry. This build is not supported; nothing was patched.");
		return false;
	}

	if (!context->CheckExpectedBytes(DefaultCapBlockRva, DefaultCapBlock, ByteCount(DefaultCapBlock))) {
		DropCapHookState.store(HookState::UnsupportedBuild, std::memory_order_release);
		context->LogError(
			"Monster drop cap NOT installed: the default-cap block at RVA 004405F7 does not match the "
			"verified 3.3.93847 bytes, so the argument this plugin overrides cannot be trusted to still "
			"mean what it meant. Nothing was patched.");
		return false;
	}

	if (!context->InstallInlineHook(
			GenerateDropsRva,
			GenerateDropsPrologue,
			ByteCount(GenerateDropsPrologue),
			HookGenerateDrops,
			&OriginalGenerateDrops)) {
		DropCapHookState.store(HookState::InstallFailed, std::memory_order_release);
		context->LogError("Monster drop cap NOT installed: InstallInlineHook failed at RVA 004404F0.");
		return false;
	}

	if (OriginalGenerateDrops == nullptr) {
		DropCapHookState.store(HookState::InstallFailed, std::memory_order_release);
		context->LogError("Monster drop cap NOT installed: the loader returned no trampoline.");
		return false;
	}

	DropCapHookState.store(HookState::Installed, std::memory_order_release);
	D2RL::LogInfoF(context, "Monster drop cap installed at RVA 004404F0, cap %d.", Config.dropCap);
	return true;
}

auto MonsterDropCapCommand(
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
		"monster-drop-cap: config %s, cap %d (vanilla %d).",
		ConfigFileWasRead ? "loaded" : "NOT READ (defaults in use)",
		Config.dropCap,
		VanillaDropCap);
	context->WriteConsoleMessage(header);

	char message[208] {};
	switch (DropCapHookState.load(std::memory_order_acquire)) {
	case HookState::Installed:
		std::snprintf(
			message,
			sizeof(message),
			"drop generator hook: active. monster drops raised so far: %llu",
			static_cast<unsigned long long>(DropsRaised.load(std::memory_order_relaxed)));
		context->WriteConsoleMessage(message);
		break;

	case HookState::DisabledByConfig:
		context->WriteConsoleWarning(
			"drop generator hook: OFF because the config file turns it off. The game build is fine.");
		break;

	case HookState::UnsupportedBuild:
		context->WriteConsoleError(
			"drop generator hook: NOT ACTIVE. This game build is not recognised and nothing was patched.");
		break;

	case HookState::InstallFailed:
		context->WriteConsoleError(
			"drop generator hook: NOT ACTIVE. The bytes matched but the hook could not be installed. "
			"See the plugin log.");
		break;

	case HookState::NotAttempted:
	default:
		context->WriteConsoleError("drop generator hook: NOT ACTIVE. Installation was never attempted.");
		break;
	}

	return D2RL::ConsoleCommandResult::Handled;
}

constexpr D2RL::PluginInfo MonsterDropCapInfo {
	.infoSize    = D2RL::PluginInfoSize,
	.apiVersion  = D2RL_PLUGIN_API_VERSION,
	.id          = "celestialrayone.monster-drop-cap",
	.name        = "Monster Drop Cap",
	.version     = "0.1.0",
	.author      = "CelestialRayOne",
	.description = "Raises the vanilla limit of 6 items per monster drop to a configurable number.",
	// Server: treasure class drop generation is server side only.
	.flags       = D2RL::PluginFlags::Server | D2RL::PluginFlags::NativeHooks,
};

static_assert(D2RL::HasValidPluginRole(MonsterDropCapInfo.flags), "Exactly one execution role must be set.");
static_assert(D2RL::HasOnlyKnownPluginFlags(MonsterDropCapInfo.flags), "Unknown plugin flag set.");
static_assert(D2RL::HasFlag(MonsterDropCapInfo.flags, D2RL::PluginFlags::NativeHooks), "Inline hooks require the NativeHooks flag.");

} // namespace

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* {
	return &MonsterDropCapInfo;
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
			"monster-drop-cap",
			MonsterDropCapCommand,
			"Report whether the raised monster drop cap is active.")) {
		context->LogWarn("The monster-drop-cap console command was not registered.");
	}

	if (const char* build = D2RL::GetBuildVersion(context); build != nullptr) {
		D2RL::LogInfoF(context, "monster-drop-cap loading against build %s.", build);
	}

	LoadConfiguration(context);

	if (!InstallDropCapHook(context)) {
		context->LogError("monster-drop-cap loaded with the raised cap NOT active.");
		return true;
	}

	context->LogInfo("monster-drop-cap loaded.");
	return true;
}

// An installed inline hook cannot be withdrawn, and the trampoline lives in
// this module, so there is deliberately nothing to undo here. The loader is
// expected to keep the DLL resident for the life of the process.
D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {}
