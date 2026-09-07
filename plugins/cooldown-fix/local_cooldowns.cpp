// ---------------------------------------------------------------------------
// local-cooldowns
//
// Makes D2R's skills.txt `localdelay` behave as a TRUE per-skill cooldown.
//
// WHAT IS ACTUALLY WRONG  (measured in game, not deduced)
// -------------------------------------------------------
// The SERVER is correct. Its state-185 statlist carries flags 0x8002 and the
// expire callback sub_14043AC00, its expire tracks the true minimum, and the
// sweep sub_14033EA20 fires per skill:
//
//     list @1B63E6D80 flags=8002 cb=server expire=3770.0
//          stat=359 layer=683 value=3770
//          stat=359 layer=693 value=3865
//     SWEEP now=3770 entries=2 -> prunes 683, re-arms to 3865
//     SWEEP now=3865 entries=1 -> prunes 693
//
// The CLIENT's state-185 statlist is a different object on a different unit:
//
//     list @1B63DC430 flags=0 cb=NONE expire=0.0
//          stat=359 layer=683 value=150760
//          stat=359 layer=693 value=154560
//
// flags 0, no expire callback, expire 0.0. sub_1402F82F0 only ticks a sublist
// when flag 0x8000 (callback path) or flag 0x2 (plain timer) is set, so that
// list is NEVER ticked, NEVER swept and NEVER pruned.
//
// sub_140339E60 cannot have built it: it allocates with
// sub_1402F7300(0x8000, ...) and installs the callback with sub_1402F7A60.
// The state-sync path created it first - the probe caught it holding the
// SERVER's frame value 3770 for layer 683 while the client clock was at
// ~150760 ms. From then on sub_1402F5940(unit, 185) hands the client's
// cooldown code that list, so sub_140339E60 always takes its "already exists"
// branch and never installs the callback or the timer flag.
//
// Consequence, matching both reported symptoms exactly: nothing on the client
// releases a skill at its own expiry. The client is released in one shot when
// the SERVER's list finally empties and clears state 185, which is when the
// LAST cooldown expires.
//
// THE FIX
// -------
// Adopt the list. When sub_140339E60 has used an existing state-185 statlist
// that carries no expire callback, install the callback it was handed and set
// the 0x8000 timer flag. The next client tick sees expire 0.0, runs the
// callback, and the vanilla machinery takes it from there: sub_140217FC0
// prunes against the client clock via sub_14033EA20, re-arms to the next
// surviving expiry, and clears state 185 only when the list is truly empty.
//
// Stale frame-scale values left by the sync sit far below the millisecond
// clock, so the first sweep drops them as expired, which is correct for
// cooldowns that ended long ago. If that empties the list the sweep tears it
// down and the next cast builds a clean one through the create path.
//
// The server list already has a callback, so the adoption is a no-op there.
//
// Verified against D2R.exe md5 baf085b077a4f9605bfddd977cfd9207.
// Built against PluginSDK v4 (D2RL_PLUGIN_API_VERSION 4).
// ---------------------------------------------------------------------------

#include <D2RLPlugin/api.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace {

// ---------------------------------------------------------------------------
// D2R.exe RVAs
// ---------------------------------------------------------------------------

// sub_140339E60 - SKILLS_SetLocalCooldown(unit, absoluteExpiry, skillId, cb).
// Creates the state-185 statlist if missing, then writes stat 359 at
// layer = skillId. Returns 2 if it created the list, 1 if the new cooldown is
// the earliest, 0 otherwise.
constexpr std::uint64_t kRvaSetLocalCooldown = 0x339E60;

// sub_1402F5940 - returns the statlist owned by a state, or null.
constexpr std::uint64_t kRvaStatlistGetStateList = 0x2F5940;

constexpr std::int32_t kStateLocalCooldown = 185;   // 0xB9

constexpr std::size_t kStatlistFlags          = 0x1C;  // 28
constexpr std::size_t kStatlistExpireCallback = 0x88;  // 136

// sub_1402F82F0 only ticks a sublist when this flag (or flag 2) is set. With
// 0x8000 it takes the callback path, the one that prunes per skill.
constexpr std::int32_t kStatlistFlagHasExpireCallback = 0x8000;

// First 13 bytes of sub_140339E60, on whole-instruction boundaries:
//   40 53 push rbx / 55 push rbp / 56 push rsi / 57 push rdi
//   41 54 push r12 / 41 55 push r13 / 41 56 push r14 / 41 57 push r15
constexpr unsigned char kSetterExpectedBytes[] = {
    0x40, 0x53, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57,
};

// x64 has a single calling convention, so no __fastcall is needed here.
using SetLocalCooldownFn = std::int64_t (*)(void* unit,
                                            std::int32_t expiry,
                                            std::int16_t skillId,
                                            void* expireCallback);
using GetStateStatlistFn = void* (*)(void* unit, std::int32_t stateId);

SetLocalCooldownFn g_originalSetter   = nullptr;
GetStateStatlistFn g_getStateStatlist = nullptr;

std::atomic<std::uint32_t> g_setterCalls{0};
std::atomic<std::uint32_t> g_adoptions{0};
std::atomic<std::uint32_t> g_alreadyOwned{0};

template <typename T>
T* AtOffset(void* p, std::size_t offset) {
    return reinterpret_cast<T*>(static_cast<std::uint8_t*>(p) + offset);
}

std::int64_t HookedSetLocalCooldown(void* unit,
                                    std::int32_t expiry,
                                    std::int16_t skillId,
                                    void* expireCallback) {
    const std::int64_t result =
        g_originalSetter(unit, expiry, skillId, expireCallback);

    g_setterCalls.fetch_add(1, std::memory_order_relaxed);

    if (unit == nullptr || expireCallback == nullptr) {
        return result;
    }

    void* statlist = g_getStateStatlist(unit, kStateLocalCooldown);
    if (statlist == nullptr) {
        return result;
    }

    void** slot = AtOffset<void*>(statlist, kStatlistExpireCallback);
    if (*slot != nullptr) {
        // Already a real cooldown list: the server's case, and the client's
        // once adopted. Nothing to do.
        g_alreadyOwned.fetch_add(1, std::memory_order_relaxed);
        return result;
    }

    // A state-185 statlist the sync path built. Give it the callback the
    // cooldown code was handed, plus the flag that makes the tick visit it.
    *slot = expireCallback;
    *AtOffset<std::int32_t>(statlist, kStatlistFlags) |= kStatlistFlagHasExpireCallback;
    g_adoptions.fetch_add(1, std::memory_order_relaxed);

    return result;
}

auto StatusCommand(D2R::Game::Client* client,
                   const D2RL::ConsoleCommandContext* command,
                   void* userData) noexcept -> D2RL::ConsoleCommandResult {
    (void)client;
    (void)userData;

    if (command == nullptr || command->plugin == nullptr) {
        return D2RL::ConsoleCommandResult::Failed;
    }

    char line[256];

    std::snprintf(line, sizeof(line),
                  "local-cooldowns: hook %s at RVA 0x%llX",
                  g_originalSetter != nullptr ? "INSTALLED" : "NOT installed",
                  static_cast<unsigned long long>(kRvaSetLocalCooldown));
    command->plugin->WriteConsoleMessage(line);

    std::snprintf(line, sizeof(line),
                  "  setter calls %u, lists adopted %u, already owned %u",
                  g_setterCalls.load(std::memory_order_relaxed),
                  g_adoptions.load(std::memory_order_relaxed),
                  g_alreadyOwned.load(std::memory_order_relaxed));
    command->plugin->WriteConsoleMessage(line);

    if (g_adoptions.load(std::memory_order_relaxed) == 0 &&
        g_setterCalls.load(std::memory_order_relaxed) > 0) {
        command->plugin->WriteConsoleMessage(
            "  no list needed adopting yet - cast a skill with a localdelay");
    }

    return D2RL::ConsoleCommandResult::Handled;
}

constexpr D2RL::PluginInfo kPluginInfo{
    .infoSize    = D2RL::PluginInfoSize,
    .apiVersion  = D2RL_PLUGIN_API_VERSION,
    .id          = "celestialrayone.local-cooldowns",
    .name        = "True Local Cooldowns",
    .version     = "1.0.0",
    .author      = "CelestialRayOne",
    .description = "Gives the client's local-cooldown statlist the expire "
                   "callback it is missing, so each skill comes off cooldown "
                   "at its own expiry instead of when the last one does.",
    .flags       = D2RL::PluginFlags::Shared | D2RL::PluginFlags::NativeHooks,
};

}  // namespace

// ---------------------------------------------------------------------------
// Exports. Note the shape: GetPluginInfo takes no arguments and returns a
// pointer to a static PluginInfo.
// ---------------------------------------------------------------------------

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* {
    return &kPluginInfo;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
    if (context == nullptr) {
        return false;
    }

    context->LogInfo("local-cooldowns: load entered");

    const std::uintptr_t base = context->exeBase;
    if (base == 0) {
        context->LogError("local-cooldowns: context exeBase is 0");
        return false;
    }

    g_getStateStatlist =
        reinterpret_cast<GetStateStatlistFn>(base + kRvaStatlistGetStateList);

    if (!context->CheckExpectedBytes(kRvaSetLocalCooldown,
                                     kSetterExpectedBytes,
                                     sizeof(kSetterExpectedBytes))) {
        context->LogError("local-cooldowns: bytes at RVA 0x339E60 are not what "
                          "this build expects, refusing to hook");
        return false;
    }

    if (!context->InstallInlineHook(kRvaSetLocalCooldown,
                                    kSetterExpectedBytes,
                                    sizeof(kSetterExpectedBytes),
                                    &HookedSetLocalCooldown,
                                    &g_originalSetter)) {
        context->LogError("local-cooldowns: InstallInlineHook failed at RVA 0x339E60");
        return false;
    }

    context->LogInfo("local-cooldowns: hooked SKILLS_SetLocalCooldown at RVA 0x339E60");

    if (!context->RegisterConsoleCommand("localcooldowns", StatusCommand,
                                         "Report local-cooldown hook status and "
                                         "adoption counts.")) {
        context->LogWarn("local-cooldowns: console command registration failed");
    }

    return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {}
