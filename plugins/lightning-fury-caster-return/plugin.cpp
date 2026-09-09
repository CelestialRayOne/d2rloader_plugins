// ---------------------------------------------------------------------------
// Lightning Fury caster priority  -  D2RLoader plugin (D2R 3.x)
//
// Port of the four 2.4 static patches:
//     "LF psrvhitfunc=20 caster-priority hook"  + cave
//     "LF pclthitfunc=25 caster-priority hook"  + cave
//
// Behaviour (identical to the 2.4 caves):
//     When a Lightning Fury style missile hits and
//         * the bolt count for that hit is exactly 1, and
//         * the skill's skills.txt aurafilter has bit 30 (0x40000000) set, and
//         * the caster is within the skill's range of the impact point
//     then the single bolt is fired at the CASTER instead of at a random
//     enemy.  If any of the three conditions fails, the stock enemy search
//     runs untouched, so an in-range monster can still be hit.
//
// How it is implemented on 3.x
//     The 2.4 version replaced the `call <enemy iterator>` instruction inside
//     the srvhit/clthit functions with a jmp into a code cave.  A mid-function
//     hook like that cannot be expressed as a C function (the detour would be
//     entered without a return address on the stack), so this plugin hooks the
//     ENTRY of the two enemy-search functions instead and recognises the
//     Lightning Fury call by the callback pointer the game passes in.  Every
//     other caller of those two functions is forwarded to the trampoline
//     untouched.
//
// Game functions involved (RVAs are offsets into the running main module,
// D2RLoader.exe, taken from the pe-sieve dump rebased to 0x140000000):
//
//   0x4327D0  server "run <callback> for every valid target near (x,y)"
//             wrapper.  Called by the LF server hit function 0x45EC20
//             (psrvhitfunc index 20) as
//                 f(pGame, caster, x, y, range, aurafilter,
//                   callback, wrapper, 1, file, line)
//   0x464FE0  server per-target bolt spawner used only by that call site.
//             Signature: f(iteratorCtx, targetUnit).
//   0x2142D0  client counterpart of 0x4327D0.  Called by the LF client hit
//             function 0x1ADA50 (pclthitfunc index 25) as
//                 f(caster, x, y, range, aurafilter, callback, wrapper, 1)
//   0x1BC840  client per-target bolt spawner for that call site.
//
// Structures read here (all verified against the 3.x image):
//
//   iterator callback context, built by the two iterators on their own stack
//     server: +0x00 pGame  +0x08 caster  +0x10 counter(i32)  +0x18 wrapper
//     client: +0x00 caster +0x08 counter(i32)                +0x10 wrapper
//
//   Lightning Fury wrapper, built by the two hit functions
//     server: +0x00 caster +0x08 missile +0x10 skillId +0x14 skillLvl
//             +0x18 castId +0x1C boltCount +0x20 srvSubMissileId
//     client: +0x00 caster +0x08 missile +0x10 skillId +0x14 skillLvl
//             +0x18 boltCount +0x1C cltSubMissileId +0x20 castId
//     (the two layouts really do differ in 3.x - the count sits at +0x1C on
//      the server and at +0x18 on the client)
//
//   unit:  +0x00 unit type,  +0x38 path pointer
//   path:  types 2/4/5 keep the position as two dwords at +0x10/+0x14,
//          every other type keeps it as two words at +0x02/+0x06
//          (that is exactly what the game does inline at each site)
//
// Runtime settings live in d2rloader/config/celestialrayone.lf-caster-priority.toml.
// The file is created with defaults on first load; the values below are only
// the fallbacks used when a key is missing or the file cannot be read.
//
// The aurafilter is not read from the skill record here: the hit function
// already passes it to the iterator as the "filter" argument (substituting
// 42883 when the column is 0, which never carries bit 30), so testing the
// argument is equivalent to testing skills.txt aurafilter directly.
// ---------------------------------------------------------------------------

#include <D2RLPlugin/api.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace {

// --------------------------------------------------------------------------
// Build-specific constants
// --------------------------------------------------------------------------

constexpr std::uint64_t kSrvTargetIteratorRva = 0x004327D0ULL;
constexpr std::uint64_t kSrvBoltCallbackRva   = 0x00464FE0ULL;
constexpr std::uint64_t kCltTargetIteratorRva = 0x002142D0ULL;
constexpr std::uint64_t kCltBoltCallbackRva   = 0x001BC840ULL;

// Prologue bytes, used as a build check. A mismatch means this plugin is
// running on a build it was not compiled for, and it refuses to load.
constexpr std::uint8_t kSrvTargetIteratorBytes[]{
	0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18,
	0x48, 0x89, 0x74, 0x24, 0x20, 0x57, 0x48, 0x83, 0xEC, 0x60,
};
constexpr std::uint8_t kSrvBoltCallbackBytes[]{
	0x48, 0x89, 0x5C, 0x24, 0x20, 0x56, 0x48, 0x81, 0xEC, 0xE0, 0x00, 0x00, 0x00,
};
constexpr std::uint8_t kCltTargetIteratorBytes[]{
	0x40, 0x55, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57,
	0x48, 0x8D, 0x6C, 0x24, 0xF1, 0x48, 0x81, 0xEC, 0xA8, 0x00, 0x00, 0x00,
};
constexpr std::uint8_t kCltBoltCallbackBytes[]{
	0x40, 0x53, 0x48, 0x81, 0xEC, 0xE0, 0x00, 0x00, 0x00,
};

// Defaults, overridden by the plugin's TOML config.
// kDefaultFilterBit is the skills.txt aurafilter bit that turns a Lightning
// Fury missile into the caster-priority variant: the same bit the 2.4 caves
// used. kDefaultBoltCount is the bolt count a hit must have for its single
// bolt to be redirected at the caster.
constexpr std::int32_t kDefaultFilterBit  = 0x40000000;
constexpr std::int32_t kDefaultBoltCount  = 1;

constexpr std::size_t kSrvWrapperBoltCountOffset = 0x1C;
constexpr std::size_t kCltWrapperBoltCountOffset = 0x18;

constexpr std::size_t kUnitPathOffset    = 0x38;
constexpr std::size_t kPathDwordXOffset  = 0x10;
constexpr std::size_t kPathDwordYOffset  = 0x14;
constexpr std::size_t kPathWordXOffset   = 0x02;
constexpr std::size_t kPathWordYOffset   = 0x06;

// --------------------------------------------------------------------------
// Game function types
// --------------------------------------------------------------------------

using SrvTargetIteratorFn = std::int64_t(__fastcall*)(
	void*         pGame,
	void*         caster,
	std::int32_t  x,
	std::int32_t  y,
	std::int32_t  range,
	std::int32_t  filter,
	void*         callback,
	void*         wrapper,
	std::int32_t  flag,
	const char*   file,
	std::int32_t  line) noexcept;

using CltTargetIteratorFn = std::int64_t(__fastcall*)(
	void*         caster,
	std::uint32_t x,
	std::uint32_t y,
	std::int32_t  range,
	std::int32_t  filter,
	void*         callback,
	void*         wrapper,
	std::int32_t  flag) noexcept;

using BoltCallbackFn = std::uint32_t(__fastcall*)(void* iteratorCtx, void* targetUnit) noexcept;

struct SrvIteratorCtx {
	void*        pGame;
	void*        caster;
	std::int32_t counter;
	std::int32_t padding;
	void*        wrapper;
};

struct CltIteratorCtx {
	void*        caster;
	std::int32_t counter;
	std::int32_t padding;
	void*        wrapper;
};

static_assert(sizeof(SrvIteratorCtx) == 0x20, "server iterator context layout");
static_assert(sizeof(CltIteratorCtx) == 0x18, "client iterator context layout");
static_assert(offsetof(SrvIteratorCtx, wrapper) == 0x18, "server wrapper slot");
static_assert(offsetof(CltIteratorCtx, wrapper) == 0x10, "client wrapper slot");

// --------------------------------------------------------------------------
// State
// --------------------------------------------------------------------------

SrvTargetIteratorFn g_originalSrvIterator = nullptr;
CltTargetIteratorFn g_originalCltIterator = nullptr;

BoltCallbackFn g_srvBoltCallback = nullptr;
BoltCallbackFn g_cltBoltCallback = nullptr;

struct Settings {
	bool         enabled              = true;
	bool         hookClient           = true;
	std::int32_t filterBit            = kDefaultFilterBit;
	std::int32_t boltCount            = kDefaultBoltCount;
	bool         requireCasterInRange = true;
};

Settings g_settings{};

volatile LONG g_srvRedirects = 0;
volatile LONG g_cltRedirects = 0;
bool          g_srvHooked    = false;
bool          g_cltHooked    = false;

// --------------------------------------------------------------------------
// Helpers
// --------------------------------------------------------------------------

auto ReadInt32(const void* base, std::size_t offset) noexcept -> std::int32_t {
	return *reinterpret_cast<const std::int32_t*>(static_cast<const std::uint8_t*>(base) + offset);
}

// Reproduces the position read the game performs inline at every one of these
// call sites: dword coordinates for unit types 2, 4 and 5, word coordinates
// for everything else.
auto GetUnitPosition(const void* unit, std::int32_t* outX, std::int32_t* outY) noexcept -> bool {
	if (unit == nullptr) {
		return false;
	}

	const std::uint32_t type = *reinterpret_cast<const std::uint32_t*>(unit);
	const std::uint8_t* path = *reinterpret_cast<const std::uint8_t* const*>(
		static_cast<const std::uint8_t*>(unit) + kUnitPathOffset);
	if (path == nullptr) {
		return false;
	}

	if (type == 2U || (type - 4U) < 2U) {
		*outX = *reinterpret_cast<const std::int32_t*>(path + kPathDwordXOffset);
		*outY = *reinterpret_cast<const std::int32_t*>(path + kPathDwordYOffset);
	} else {
		*outX = static_cast<std::int32_t>(*reinterpret_cast<const std::uint16_t*>(path + kPathWordXOffset));
		*outY = static_cast<std::int32_t>(*reinterpret_cast<const std::uint16_t*>(path + kPathWordYOffset));
	}
	return true;
}

// Same gate the enemy search itself applies to a candidate: range^2 >= dist^2.
auto CasterIsInRange(const void* caster, std::int32_t x, std::int32_t y, std::int32_t range) noexcept -> bool {
	if (range <= 0) {
		return false;
	}

	// The iterators treat a zero coordinate as "use the caster's own position",
	// in which case the caster is trivially at the impact point.
	if (x == 0 || y == 0) {
		return true;
	}

	if (!g_settings.requireCasterInRange) {
		return true;
	}

	std::int32_t casterX = 0;
	std::int32_t casterY = 0;
	if (!GetUnitPosition(caster, &casterX, &casterY)) {
		return false;
	}

	const std::int64_t dx = static_cast<std::int64_t>(casterX) - x;
	const std::int64_t dy = static_cast<std::int64_t>(casterY) - y;
	return static_cast<std::int64_t>(range) * range >= dx * dx + dy * dy;
}

auto ShouldRedirect(const void* wrapper, std::size_t boltCountOffset, std::int32_t filter) noexcept -> bool {
	return wrapper != nullptr
		&& (filter & g_settings.filterBit) != 0
		&& ReadInt32(wrapper, boltCountOffset) == g_settings.boltCount;
}

// --------------------------------------------------------------------------
// Config file
// --------------------------------------------------------------------------

constexpr const char* kDefaultConfigToml =
	"# Lightning Fury Caster Priority\n"
	"#\n"
	"# A Lightning Fury style hit sends its bolt at the caster instead of at a\n"
	"# random enemy when the hit fires exactly bolt_count bolts, the skill's\n"
	"# skills.txt aurafilter has aurafilter_bit set, and the caster is inside the\n"
	"# skill's range. Otherwise the stock enemy search runs untouched.\n"
	"\n"
	"[lf-caster-priority]\n"
	"\n"
	"# Master switch. false leaves both hooks installed but inert.\n"
	"enabled = true\n"
	"\n"
	"# Mirror the server behaviour on the client so the visual bolt matches.\n"
	"# Only turn this off to isolate a client side problem.\n"
	"hook_client = true\n"
	"\n"
	"# skills.txt aurafilter bit that marks a skill as caster-priority.\n"
	"# Decimal or 0x hex. Default is bit 30, the bit the 2.4 patches used.\n"
	"aurafilter_bit = 0x40000000\n"
	"\n"
	"# Bolt count the hit must have for its bolt to be redirected.\n"
	"# 1 means only single-bolt hits are affected, which is the 2.4 behaviour.\n"
	"bolt_count = 1\n"
	"\n"
	"# Require the caster to be within the skill's range of the impact point.\n"
	"# false redirects the bolt no matter how far away the caster is.\n"
	"require_caster_in_range = true\n";

auto IsSpace(char c) noexcept -> bool {
	return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

// Finds "key" at the start of a line and returns the text after its '='.
// Small hand rolled scan: the config has a handful of scalar keys and pulling
// in a TOML parser for that would be silly.
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

auto ReadBool(const char* text, const char* key, bool fallback) noexcept -> bool {
	const char* value = FindValue(text, key);
	if (value == nullptr) {
		return fallback;
	}
	if (value[0] == 't' || value[0] == 'T' || value[0] == '1') {
		return true;
	}
	if (value[0] == 'f' || value[0] == 'F' || value[0] == '0') {
		return false;
	}
	return fallback;
}

auto ReadInt(const char* text, const char* key, std::int32_t fallback) noexcept -> std::int32_t {
	const char* value = FindValue(text, key);
	if (value == nullptr) {
		return fallback;
	}

	bool negative = false;
	if (*value == '-') {
		negative = true;
		++value;
	}

	std::int64_t parsed = 0;
	bool         digits = false;

	if (value[0] == '0' && (value[1] == 'x' || value[1] == 'X')) {
		value += 2;
		while (true) {
			std::int32_t digit = -1;
			if (*value >= '0' && *value <= '9') {
				digit = *value - '0';
			} else if (*value >= 'a' && *value <= 'f') {
				digit = *value - 'a' + 10;
			} else if (*value >= 'A' && *value <= 'F') {
				digit = *value - 'A' + 10;
			} else if (*value == '_') {
				++value;
				continue;
			}
			if (digit < 0) {
				break;
			}
			parsed = parsed * 16 + digit;
			digits = true;
			++value;
			if (parsed > 0xFFFFFFFFLL) {
				return fallback;
			}
		}
	} else {
		while (*value >= '0' && *value <= '9') {
			parsed = parsed * 10 + (*value - '0');
			digits = true;
			++value;
			if (parsed > 0xFFFFFFFFLL) {
				return fallback;
			}
		}
	}

	if (!digits || !(IsSpace(*value) || *value == '\0' || *value == '#')) {
		return fallback;
	}

	const std::int32_t result = static_cast<std::int32_t>(static_cast<std::uint32_t>(parsed));
	return negative ? -result : result;
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

	Settings settings{};
	settings.enabled              = ReadBool(toml, "enabled", settings.enabled);
	settings.hookClient           = ReadBool(toml, "hook_client", settings.hookClient);
	settings.filterBit            = ReadInt(toml, "aurafilter_bit", settings.filterBit);
	settings.boltCount            = ReadInt(toml, "bolt_count", settings.boltCount);
	settings.requireCasterInRange = ReadBool(toml, "require_caster_in_range", settings.requireCasterInRange);

	if (settings.filterBit == 0) {
		context->LogWarn("aurafilter_bit is 0, which would match every skill. Keeping the default bit.");
		settings.filterBit = kDefaultFilterBit;
	}
	if (settings.boltCount < 1) {
		context->LogWarn("bolt_count below 1 can never match. Keeping the default.");
		settings.boltCount = kDefaultBoltCount;
	}

	g_settings = settings;
}

// --------------------------------------------------------------------------
// Hooks
// --------------------------------------------------------------------------

auto __fastcall HookSrvTargetIterator(
	void*         pGame,
	void*         caster,
	std::int32_t  x,
	std::int32_t  y,
	std::int32_t  range,
	std::int32_t  filter,
	void*         callback,
	void*         wrapper,
	std::int32_t  flag,
	const char*   file,
	std::int32_t  line) noexcept -> std::int64_t {

	if (g_settings.enabled
		&& callback == reinterpret_cast<void*>(g_srvBoltCallback)
		&& caster != nullptr
		&& ShouldRedirect(wrapper, kSrvWrapperBoltCountOffset, filter)
		&& CasterIsInRange(caster, x, y, range)) {

		SrvIteratorCtx ctx{};
		ctx.pGame   = pGame;
		ctx.caster  = caster;
		ctx.counter = 0;
		ctx.padding = 0;
		ctx.wrapper = wrapper;

		g_srvBoltCallback(&ctx, caster);
		::InterlockedIncrement(&g_srvRedirects);
		return 0;
	}

	const SrvTargetIteratorFn original = g_originalSrvIterator;
	if (original == nullptr) {
		return 0;
	}
	return original(pGame, caster, x, y, range, filter, callback, wrapper, flag, file, line);
}

auto __fastcall HookCltTargetIterator(
	void*         caster,
	std::uint32_t x,
	std::uint32_t y,
	std::int32_t  range,
	std::int32_t  filter,
	void*         callback,
	void*         wrapper,
	std::int32_t  flag) noexcept -> std::int64_t {

	if (g_settings.enabled
		&& callback == reinterpret_cast<void*>(g_cltBoltCallback)
		&& caster != nullptr
		&& ShouldRedirect(wrapper, kCltWrapperBoltCountOffset, filter)
		&& CasterIsInRange(caster, static_cast<std::int32_t>(x), static_cast<std::int32_t>(y), range)) {

		CltIteratorCtx ctx{};
		ctx.caster  = caster;
		ctx.counter = 0;
		ctx.padding = 0;
		ctx.wrapper = wrapper;

		g_cltBoltCallback(&ctx, caster);
		::InterlockedIncrement(&g_cltRedirects);
		return 0;
	}

	const CltTargetIteratorFn original = g_originalCltIterator;
	if (original == nullptr) {
		return 0;
	}
	return original(caster, x, y, range, filter, callback, wrapper, flag);
}

// --------------------------------------------------------------------------
// Plugin plumbing
// --------------------------------------------------------------------------

constexpr D2RL::PluginInfo kPluginInfo{
	.infoSize    = D2RL::PluginInfoSize,
	.apiVersion  = D2RL_PLUGIN_API_VERSION,
	.id          = "celestialrayone.lf-caster-priority",
	.name        = "Lightning Fury Caster Priority",
	.version     = "1.0.0",
	.author      = "CelestialRayOne",
	.description = "Single-bolt Lightning Fury hits target the caster when skills.txt aurafilter bit 0x40000000 is set and the caster is in range.",
	.flags       = D2RL::PluginFlags::Shared | D2RL::PluginFlags::NativeHooks,
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
	              "lf-caster-priority: %s, server hook %s, client hook %s, redirects %ld/%ld (server/client).",
	              g_settings.enabled ? "enabled" : "DISABLED",
	              g_srvHooked ? "on" : "OFF",
	              g_cltHooked ? "on" : "OFF",
	              static_cast<long>(g_srvRedirects),
	              static_cast<long>(g_cltRedirects));
	command->plugin->WriteConsoleMessage(message);

	std::snprintf(message, sizeof(message),
	              "lf-caster-priority: aurafilter_bit 0x%08X, bolt_count %d, require_caster_in_range %s.",
	              static_cast<unsigned int>(g_settings.filterBit),
	              static_cast<int>(g_settings.boltCount),
	              g_settings.requireCasterInRange ? "true" : "false");
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

	// Verify all four sites before touching anything.
	if (!CheckSite(context, kSrvTargetIteratorRva, kSrvTargetIteratorBytes, ByteCount(kSrvTargetIteratorBytes), "Server target search")
		|| !CheckSite(context, kSrvBoltCallbackRva, kSrvBoltCallbackBytes, ByteCount(kSrvBoltCallbackBytes), "Server Lightning Fury bolt callback")
		|| !CheckSite(context, kCltTargetIteratorRva, kCltTargetIteratorBytes, ByteCount(kCltTargetIteratorBytes), "Client target search")
		|| !CheckSite(context, kCltBoltCallbackRva, kCltBoltCallbackBytes, ByteCount(kCltBoltCallbackBytes), "Client Lightning Fury bolt callback")) {
		return false;
	}

	g_srvBoltCallback = reinterpret_cast<BoltCallbackFn>(moduleBase + kSrvBoltCallbackRva);
	g_cltBoltCallback = reinterpret_cast<BoltCallbackFn>(moduleBase + kCltBoltCallbackRva);

	if (!context->InstallInlineHook(kSrvTargetIteratorRva,
	                                kSrvTargetIteratorBytes,
	                                ByteCount(kSrvTargetIteratorBytes),
	                                &HookSrvTargetIterator,
	                                &g_originalSrvIterator)) {
		context->LogError("Failed to install the server target-search hook.");
		return false;
	}
	g_srvHooked = true;

	// From here on the plugin never returns false: the server detour is live,
	// and returning false makes the loader unload this DLL with a hook still
	// pointing into it.
	if (!g_settings.hookClient) {
		context->LogWarn("hook_client is false: the client visual will not follow the server bolt.");
	} else if (context->InstallInlineHook(kCltTargetIteratorRva,
	                                      kCltTargetIteratorBytes,
	                                      ByteCount(kCltTargetIteratorBytes),
	                                      &HookCltTargetIterator,
	                                      &g_originalCltIterator)) {
		g_cltHooked = true;
	} else {
		context->LogError("Failed to install the client target-search hook. The server side stays active, so the bolt still hits the caster, but the local visual will not match.");
	}

	if (!context->RegisterConsoleCommand("lf-caster-priority", StatusCommand, "Report Lightning Fury caster-priority hook state.")) {
		context->LogWarn("Console command was not registered.");
	}

	char summary[192]{};
	std::snprintf(summary, sizeof(summary),
	              "Lightning Fury caster priority loaded: %s, aurafilter bit 0x%08X, bolt count %d.",
	              g_settings.enabled ? "enabled" : "disabled in config",
	              static_cast<unsigned int>(g_settings.filterBit),
	              static_cast<int>(g_settings.boltCount));
	context->LogInfo(summary);
	return true;
}
