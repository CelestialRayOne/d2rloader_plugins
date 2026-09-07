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
// ---------------------------------------------------------------------------

#include <D2RLPlugin/api.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace {

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

// --- compile time cross-checks on the two byte arrays ----------------------

static_assert(sizeof(UnitHashLookupBody) == 41, "Verified body length changed.");
static_assert(sizeof(UnitHashLookupPrologue) == 7, "Verified prologue length changed.");
static_assert(sizeof(UnitHashLookupPrologue) >= 5, "An inline hook needs at least 5 displaced bytes for a rel32 jump.");

constexpr auto PrologueIsPrefixOfBody() noexcept -> bool {
	for (std::size_t index = 0; index < sizeof(UnitHashLookupPrologue); ++index) {
		if (UnitHashLookupPrologue[index] != UnitHashLookupBody[index]) {
			return false;
		}
	}
	return true;
}

static_assert(PrologueIsPrefixOfBody(), "The displaced bytes must be the leading bytes of the verified body.");

// ---------------------------------------------------------------------------

using UnitHashLookupFn = void*(__fastcall*)(
	void*         bucketArray,
	std::int32_t  bucketIndex,
	std::int32_t  unitId,
	std::int32_t  unitType) noexcept;

UnitHashLookupFn OriginalUnitHashLookup = nullptr;

// Diagnostics only. The hot path (any live unit id) touches nothing; the
// counter is only written on the rare guarded path.
std::atomic<std::uint64_t> SuppressedTombstoneLookups { 0 };

std::atomic<bool> DeadUnitGuardInstalled { false };

auto __fastcall HookUnitHashLookup(
	void*        bucketArray,
	std::int32_t bucketIndex,
	std::int32_t unitId,
	std::int32_t unitType) noexcept -> void* {
	if (unitId == TombstoneUnitId) {
		SuppressedTombstoneLookups.fetch_add(1, std::memory_order_relaxed);
		return nullptr;
	}

	const UnitHashLookupFn original = OriginalUnitHashLookup;
	return original != nullptr ? original(bucketArray, bucketIndex, unitId, unitType) : nullptr;
}

// ---------------------------------------------------------------------------
// Plumbing
// ---------------------------------------------------------------------------

template <std::size_t Size>
constexpr auto ByteCount(const std::uint8_t (&)[Size]) noexcept -> std::uint32_t {
	return static_cast<std::uint32_t>(Size);
}

auto InstallDeadUnitLookupGuard(const D2RL::PluginContext* context) noexcept -> bool {
	if (context == nullptr) {
		return false;
	}

	if (!context->CheckExpectedBytes(UnitHashLookupRva, UnitHashLookupBody, ByteCount(UnitHashLookupBody))) {
		context->LogError(
			"Dead-unit lookup guard NOT installed: the client unit hash lookup at RVA 0009F270 "
			"does not match the verified 3.3.93847 body. This build is not supported; nothing was patched.");
		return false;
	}

	if (!context->InstallInlineHook(
			UnitHashLookupRva,
			UnitHashLookupPrologue,
			ByteCount(UnitHashLookupPrologue),
			HookUnitHashLookup,
			&OriginalUnitHashLookup)) {
		context->LogError("Dead-unit lookup guard NOT installed: InstallInlineHook failed at RVA 0009F270.");
		return false;
	}

	if (OriginalUnitHashLookup == nullptr) {
		context->LogError("Dead-unit lookup guard NOT installed: the loader returned no trampoline.");
		return false;
	}

	DeadUnitGuardInstalled.store(true, std::memory_order_release);
	context->LogInfo("Dead-unit lookup guard installed at RVA 0009F270.");
	return true;
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

	if (DeadUnitGuardInstalled.load(std::memory_order_acquire)) {
		char message[160] {};
		std::snprintf(
			message,
			sizeof(message),
			"dead-unit lookup guard: active. tombstoned lookups suppressed so far: %llu",
			static_cast<unsigned long long>(SuppressedTombstoneLookups.load(std::memory_order_relaxed)));
		context->WriteConsoleMessage(message);
	} else {
		context->WriteConsoleError(
			"dead-unit lookup guard: NOT ACTIVE. This game build is not recognised and nothing was patched.");
	}

	return D2RL::ConsoleCommandResult::Handled;
}

constexpr D2RL::PluginInfo EngineStabilityInfo {
	.infoSize    = D2RL::PluginInfoSize,
	.apiVersion  = D2RL_PLUGIN_API_VERSION,
	.id          = "celestialrayone.engine-stability",
	.name        = "Engine Stability",
	.version     = "0.1.0",
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

	if (!InstallDeadUnitLookupGuard(context)) {
		context->LogError("engine-stability loaded with NO guards active.");
		return true;
	}

	context->LogInfo("engine-stability loaded.");
	return true;
}

// An installed inline hook cannot be withdrawn, and the trampoline lives in
// this module, so there is deliberately nothing to undo here. The loader is
// expected to keep the DLL resident for the life of the process.
D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {}
