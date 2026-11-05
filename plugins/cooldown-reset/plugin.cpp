// ---------------------------------------------------------------------------
// Cooldown Reset
//
// When a player gains the configured state, every skill cooldown he has
// running ends at once, on the server and on his own client.
//
// Read out of the D2RLoader 1.3.1 D2RLoader.exe image and D2RCore.dll. Every
// native used here is checked byte for byte at load; nothing runs when a check
// fails. No game code is patched: the plugin only calls the game's functions.
//
// ---------------------------------------------------------------------------
// 1. Where the game keeps cooldowns (players only: the gate 3404A0 is player-only)
// ---------------------------------------------------------------------------
//   localdelay   state 185 "cooldown" owns a stat list holding stat 359
//                skill_cooldown once per skill: layer = skill id, value = the
//                expiry (a game frame on the server, the client clock in ms on
//                the client). The gate blocks a skill while state 185 is on and
//                its entry is above 0. D2RCore keeps the entries:
//                SetWideSkillCooldown behind 339E60, ExpireWideSkillCooldowns
//                behind 33EA20.
//   globaldelay  state 121 "skilldelay" owns a plain timer list (flag 2),
//                built by 339D70. While state 121 is on, the gate blocks every
//                skill whose globaldelay or localdelay is above 0.
//   The server's lists sit on the server unit, the client's on the client
//   unit, built by its own casts or by the state sync.
//
// ---------------------------------------------------------------------------
// 2. How they end on their own
// ---------------------------------------------------------------------------
//   ExpireWideUnitStats (2F82F0 -> D2RCore 3DAE20) (unit, clock, step) walks
//   the unit's lists. Server (clock = game frame): a list ends once the clock
//   reaches its expiry, the absolute float at +24h. Client (clock 0): the
//   float counts down by step and the list ends at 0. A list with flag 8000h
//   first calls its callback at +88h and ends only when it returns true; a
//   plain timer list just ends, and its remove callback clears its state.
//   In the game image the server reaches it only through the player event 12
//   handler 42EA80 (game, unit), slot 12 of the table at 1D192B0 that the
//   player event dispatcher 42CE30 calls. EVENT_SetEvent 48B720 schedules that
//   event at each expiry (439470 for globaldelay, the cooldown callback 43AC00
//   for the next skill). The client runs it from its unit update 0FA930.
//   The cooldown callbacks, server 43AC00 and client 217FC0, call
//   ExpireWideSkillCooldowns (unit, list, clock): it drops every stat 359
//   entry at or below the clock and returns -1 (and clears state 185) when
//   none is left, else the next expiry, which they re-arm to.
//
// ---------------------------------------------------------------------------
// 3. The reset: the same steps, now
// ---------------------------------------------------------------------------
//   a. ExpireWideSkillCooldowns (unit, list 185, 7FFFFFFFh): every entry
//      goes and state 185 clears, so every localdelay is free at once.
//   b. Both lists' expiry becomes 0 (2F7AB0). At the next expiry pass the
//      cooldown list's callback finds it empty and the list goes; the shared
//      delay list goes and its remove callback clears state 121.
//   c. Server: EVENT_SetEvent (game, unit, 12, 0, 0, 0, 0) runs that pass on
//      the next frame (a frame at or before now becomes now + 1), the way
//      439470 schedules it for a global delay. Client: its pass runs anyway.
//   A cooldown started between b and that pass is kept: its expiry replaces
//   the 0 and the callback re-arms to it.
//
// ---------------------------------------------------------------------------
// 4. When
// ---------------------------------------------------------------------------
//   A server pass (game thread, every update) looks at every player in the
//   game, a client pass (UI thread, every frame) at the local player. Each
//   resets its own side once when the player gains the state. Both run after
//   the cast or effect that gave the state has finished, so the reset also
//   ends that skill's own cooldown, the same way on both sides.
//
// ---------------------------------------------------------------------------
// 5. Other plugins
// ---------------------------------------------------------------------------
//   cooldown-fix (localcooldowns) adopts the client's state-185 list at the
//   setter calls 217BF7 / 439565. The reset uses neither: an adopted list ends
//   through its callback like any other, a list the state sync built is
//   emptied and its state cleared, and the next cast adopts it as usual.
//   crossbow-charges: crossbow skills spend bolts instead of using state 121;
//   the bolt pool is that plugin's and is not refilled here.
// ---------------------------------------------------------------------------

#include <D2RLPlugin/api.h>

#include <windows.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace CelestialRayOne::CooldownReset {
namespace {

constexpr char PluginVersion[] = "1.0.0";

// [config-begin]
constexpr const char DefaultConfigToml[] = R"TOML(# Cooldown Reset
#
# When a player gains the state below, every skill cooldown he has running
# ends at once:
#   - each skill's own cooldown (skills.txt localdelay), which the game keeps
#     per skill in stat 359 (skill_cooldown) under state 185 (cooldown)
#   - the shared delay (skills.txt globaldelay), state 121 (skilldelay)
# It happens once each time he gains the state, on the server and on his own
# client, so his skill buttons clear too. It runs right after the cast or
# effect that gave the state, so a skill that gives the state comes off its
# own cooldown as well. Cooldowns started after the reset run normally.
#
# The state must reach the client: leave its states.txt nosend empty.
# Not touched: Crossbow Charges' bolt pool, and cooldowns other plugins keep
# themselves (the Elixir of Reincarnation, artefacts).
#
# state: the states.txt row id (the *ID column). 0 turns it off.
# The console command "cooldownreset" shows the state and counters.

enabled = true
state = 0
)TOML";

struct Config {
    bool         enabled{ true };
    std::int32_t state{ 0 };
};

auto Trim(std::string_view s) -> std::string_view {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
    return s;
}

// enabled = bool, state = states.txt id. Problems are appended to `problems`.
auto ParseConfig(std::string_view text, std::string& problems) -> Config {
    Config parsed{};
    std::size_t pos = 0;
    while (pos <= text.size()) {
        std::size_t end = text.find('\n', pos);
        if (end == std::string_view::npos) end = text.size();
        std::string_view line = text.substr(pos, end - pos);
        pos = end + 1;
        if (const std::size_t hash = line.find('#'); hash != std::string_view::npos) line = line.substr(0, hash);
        line = Trim(line);
        if (line.empty()) continue;
        const std::size_t eq = line.find('=');
        if (eq == std::string_view::npos) { problems += " a line without '=';"; continue; }
        const std::string key(Trim(line.substr(0, eq)));
        const std::string value(Trim(line.substr(eq + 1)));
        if (key == "enabled") {
            if (value == "true") parsed.enabled = true;
            else if (value == "false") parsed.enabled = false;
            else problems += " enabled expects true or false;";
        } else if (key == "state") {
            char* stop = nullptr;
            const long id = std::strtol(value.c_str(), &stop, 10);
            if (value.empty() || stop == value.c_str() || *stop != '\0' || id < 0 || id > 65535) {
                problems += " state expects a states.txt id (0 turns it off);";
            } else {
                parsed.state = static_cast<std::int32_t>(id);
            }
        } else {
            problems += " unknown key " + key + ";";
        }
    }
    return parsed;
}
// [config-end]

// ---------------------------------------------------------------------------
// Natives (D2RLoader 1.3.1)
// ---------------------------------------------------------------------------
constexpr std::int32_t  StateCooldown   = 185;         // states.txt "cooldown": stat 359 per skill
constexpr std::int32_t  StateSkillDelay = 121;         // states.txt "skilldelay": the shared delay
constexpr std::int32_t  EveryCooldown   = 0x7FFFFFFF;  // a clock past every expiry
constexpr std::int32_t  EventStatExpiry = 12;          // player event 12: the server expiry pass
constexpr std::uint32_t UnitPlayer      = 0;
constexpr std::size_t   UnitId          = 0x08;

// STATES_CheckState (unit, state) -> int
constexpr std::uint64_t CheckStateRva = 0x3351B0;
constexpr std::uint8_t  CheckStateEntry[]{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x8B,
    0xDA, 0x48, 0x8B, 0xF1,
};
// (unit, state) -> the stat list the state owns, or null
constexpr std::uint64_t StateListRva = 0x2F5940;
constexpr std::uint8_t  StateListEntry[]{
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x8B, 0xDA, 0x48, 0x8B, 0xF9, 0x48,
    0x85, 0xC9, 0x75, 0x13,
};
// (list, expiry), the whole function: flag 2 at +1Ch when expiry > 0, float +24h = expiry
constexpr std::uint64_t SetExpiryRva = 0x2F7AB0;
constexpr std::uint8_t  SetExpiryBody[]{
    0x48, 0x85, 0xC9, 0x74, 0x14, 0x85, 0xD2, 0x7E, 0x04, 0x83, 0x49, 0x1C, 0x02, 0x66, 0x0F, 0x6E,
    0xC2, 0x0F, 0x5B, 0xC0, 0xF3, 0x0F, 0x11, 0x41, 0x24, 0xC3,
};
// jmp [D2RCore!ExpireWideSkillCooldowns] / nop, then the old prologue. The
// jump's disp32 names the loader's import slot and moves between loader
// builds, so only the opcode and the tail are checked.
constexpr std::uint64_t ExpireCooldownsRva     = 0x33EA20;
constexpr std::uint8_t  ExpireCooldownsJump[]{ 0xFF, 0x25 };
constexpr std::uint64_t ExpireCooldownsTailRva = 0x33EA26;
constexpr std::uint8_t  ExpireCooldownsTail[]{ 0x90, 0x41, 0x55, 0x41, 0x57 };
// 43AC14..43AC33 in the server cooldown callback 43AC00 (unit, state, list):
// mov rsi,r8 / mov rdi,rcx / call 48FF00 (the unit's game) / mov rdx,rsi /
// mov rcx,rdi / mov rbp,rax / mov r8d,[rax+170h] (the frame) / call 33EA20.
// It proves the sweep's arguments: (unit, list, clock).
constexpr std::uint64_t ServerCallbackRva = 0x43AC14;
constexpr std::uint8_t  ServerCallback[]{
    0x49, 0x8B, 0xF0, 0x48, 0x8B, 0xF9, 0xE8, 0xE1, 0x52, 0x05, 0x00, 0x48, 0x8B, 0xD6, 0x48, 0x8B,
    0xCF, 0x48, 0x8B, 0xE8, 0x44, 0x8B, 0x80, 0x70, 0x01, 0x00, 0x00, 0xE8, 0xEC, 0x3D, 0xF0, 0xFF,
};
// The player event 12 handler (game, unit), the whole function:
// movss xmm2,[step] / mov rax,rdx / mov edx,[rcx+170h] / mov rcx,rax / jmp 2F82F0
constexpr std::uint64_t ServerExpiryRva = 0x42EA80;
constexpr std::uint8_t  ServerExpiryBody[]{
    0xF3, 0x0F, 0x10, 0x15, 0x7C, 0xBE, 0x88, 0x01, 0x48, 0x8B, 0xC2, 0x8B, 0x91, 0x70, 0x01, 0x00,
    0x00, 0x48, 0x8B, 0xC8, 0xE9, 0x57, 0x98, 0xEC, 0xFF,
};
constexpr std::uint64_t PlayerEventsRva = 0x1D192B0;   // the 15 player event handlers 42CE30 calls
// EVENT_SetEvent (game, unit, type, frame, customId, customParam, arg7) -> 48BE80
constexpr std::uint64_t SetEventRva = 0x48B720;
constexpr std::uint8_t  SetEventEntry[]{
    0x48, 0x83, 0xEC, 0x48, 0x8B, 0x84, 0x24, 0x80, 0x00, 0x00, 0x00, 0x89, 0x44, 0x24, 0x38, 0x8B,
    0x44, 0x24, 0x78, 0x89, 0x44, 0x24, 0x30, 0x8B, 0x44, 0x24, 0x70, 0x89, 0x44, 0x24, 0x28, 0x48,
    0xC7, 0x44, 0x24, 0x20, 0x00, 0x00, 0x00, 0x00, 0xE8, 0x33, 0x07, 0x00, 0x00,
};
// UNITS_GetUnitType (unit)
constexpr std::uint64_t UnitTypeRva = 0x34B9D0;
constexpr std::uint8_t  UnitTypeEntry[]{
    0x48, 0x83, 0xEC, 0x28, 0x48, 0x85, 0xC9, 0x75, 0x1D, 0x88, 0x4C, 0x24, 0x30, 0x48, 0x8D, 0x4C,
    0x24, 0x30,
};

// Every player of every game on this host: the server's client registry, as
// Archaeology walks it. 256 buckets at 2AAECA0, ready flag 2AAF4A0.
// 485290..4852D4 (68 bytes): the registry reset
// 485320..485346 (38 bytes): the client lookup, next client at +630h
// 484CDB..484CF4 (25 bytes): unit id +270h, unit type +26Ch, game +2B0h
constexpr std::uint8_t RegistryReset[]{
    0x33, 0xD2, 0x48, 0x8D, 0x05, 0x07, 0x9A, 0x62, 0x02, 0x8D, 0x4A, 0x20, 0x0F, 0x1F, 0x40, 0x00,
    0x48, 0x89, 0x10, 0x48, 0x89, 0x50, 0x08, 0x48, 0x89, 0x50, 0x10, 0x48, 0x8D, 0x40, 0x40, 0x48,
    0x89, 0x50, 0xD8, 0x48, 0x89, 0x50, 0xE0, 0x48, 0x89, 0x50, 0xE8, 0x48, 0x89, 0x50, 0xF0, 0x48,
    0x89, 0x50, 0xF8, 0x48, 0x83, 0xE9, 0x01, 0x75, 0xD7, 0xC7, 0x05, 0xCD, 0xA1, 0x62, 0x02, 0x01,
    0x00, 0x00, 0x00, 0xC3,
};
constexpr std::uint8_t ClientLookup[]{
    0x0F, 0xB6, 0xC1, 0x48, 0x8D, 0x15, 0x76, 0x99, 0x62, 0x02, 0x48, 0x8B, 0x04, 0xC2, 0x48, 0x85,
    0xC0, 0x74, 0x10, 0x39, 0x08, 0x74, 0x0E, 0x48, 0x8B, 0x80, 0x30, 0x06, 0x00, 0x00, 0x48, 0x85,
    0xC0, 0x75, 0xF0, 0x33, 0xC0, 0xC3,
};
constexpr std::uint8_t ClientPlayer[]{
    0x44, 0x8B, 0x83, 0x70, 0x02, 0x00, 0x00, 0x8B, 0x93, 0x6C, 0x02, 0x00, 0x00, 0x48, 0x8B, 0x8B,
    0xB0, 0x02, 0x00, 0x00, 0xE8, 0x8C, 0xB1, 0x00, 0x00,
};
constexpr std::uint8_t ServerUnitEntry[]{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x41,
    0x8B, 0xD8, 0x8B, 0xF2, 0x48, 0x8B, 0xF9, 0x48, 0x85, 0xC9,
};
constexpr std::uint8_t LocalPlayerIndexEntry[]{
    0x8B, 0x05, 0x2E, 0x84, 0x99, 0x02, 0xC3,
};
constexpr std::uint8_t PlayerFromIndexEntry[]{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x83, 0xF9, 0x08,
};
constexpr std::uint64_t RegistryResetRva    = 0x485290;
constexpr std::uint64_t ClientLookupRva     = 0x485320;
constexpr std::uint64_t ClientPlayerRva     = 0x484CDB;
constexpr std::uint64_t ServerUnitRva       = 0x48FE80;  // SUNIT_GetServerUnit (game, type, id)
constexpr std::uint64_t LocalPlayerIndexRva = 0x08B2D0;
constexpr std::uint64_t PlayerFromIndexRva  = 0x09A480;  // CLIENT_GetLocalPlayer (index)
constexpr std::uint64_t RegistryRva         = 0x2AAECA0;
constexpr std::uint64_t RegistryReadyRva    = 0x2AAF4A0;
constexpr std::size_t   RegistryBuckets     = 256;
constexpr std::size_t   RegistryNext        = 0x630;
constexpr std::size_t   RegistryUnitType    = 0x26C;
constexpr std::size_t   RegistryUnitId      = 0x270;
constexpr std::size_t   RegistryGame        = 0x2B0;
constexpr std::size_t   MaxPlayers          = 16;

using CheckStateFn      = std::int32_t (__fastcall*)(void* unit, std::int32_t state);
using StateListFn       = void* (__fastcall*)(void* unit, std::int32_t state);
using SetExpiryFn       = void (__fastcall*)(void* list, std::int32_t expiry);
using ExpireCooldownsFn = std::int32_t (__fastcall*)(void* unit, void* list, std::int32_t clock);
using SetEventFn        = void (__fastcall*)(void* game, void* unit, std::int32_t type, std::int32_t frame, std::int32_t id,
                                             std::int32_t param, std::int32_t arg7);
using UnitTypeFn        = std::uint32_t (__fastcall*)(void* unit);
using ServerUnitFn      = void* (__fastcall*)(void* game, std::uint32_t type, std::uint32_t id);
using LocalIndexFn      = std::int32_t (__fastcall*)();
using PlayerFromIndexFn = void* (__fastcall*)(std::int32_t index);

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
const D2RL::PluginContext*  Context{};
std::uintptr_t              Base{};
const D2RL::ThreadService*    ThreadSvc{};
const D2RL::LifecycleService* LifecycleSvc{};
Config                      Settings;
std::string                 ConfigProblems;
std::string                 Problem;
bool                        Ready{};

CheckStateFn      CheckState{};
StateListFn       StateList{};
SetExpiryFn       SetExpiry{};
ExpireCooldownsFn ExpireCooldowns{};
SetEventFn        SetEvent{};
UnitTypeFn        UnitType{};
ServerUnitFn      ServerUnit{};
LocalIndexFn      LocalPlayerIndex{};
PlayerFromIndexFn PlayerFromIndex{};

std::atomic<std::uint64_t> ServerResets{};
std::atomic<std::uint64_t> ServerResetsRunning{};
std::atomic<std::uint64_t> ClientResets{};
std::atomic<std::uint64_t> ClientResetsRunning{};
std::atomic<std::uint64_t> ServerPasses{};
std::atomic<std::uint64_t> ClientPasses{};

std::atomic<std::uint64_t> ServerChain{};
std::atomic<std::uint64_t> ClientChain{};
std::atomic<std::uint64_t> ChainCounter{};
std::atomic<std::uint64_t> Generation{};

// Who had the state at the last pass. Server: game thread only. Client: UI
// thread only. A game joined or left (Generation) forgets both.
struct Seen {
    void*         game;
    std::uint32_t id;
    bool          had;
};
std::vector<Seen> ServerSeen;
std::uint64_t     ServerSeenGeneration{};
struct LocalSeen {
    std::uint32_t id{};
    bool          known{};
    bool          had{};
};
LocalSeen     ClientSeen;
std::uint64_t ClientSeenGeneration{};

template <typename T> auto At(std::uint64_t rva) noexcept -> T { return reinterpret_cast<T>(Base + rva); }
template <typename T> auto Read(const void* p, std::size_t off) noexcept -> T {
    T v{};
    std::memcpy(&v, static_cast<const std::uint8_t*>(p) + off, sizeof(T));
    return v;
}

// ---------------------------------------------------------------------------
// The reset
// ---------------------------------------------------------------------------
// Ends every cooldown on one side's unit (section 3 a and b). True when there
// was a list to end.
auto EndCooldowns(void* unit) noexcept -> bool {
    bool running = false;
    if (void* const list = StateList(unit, StateCooldown); list != nullptr) {
        ExpireCooldowns(unit, list, EveryCooldown);
        SetExpiry(list, 0);
        running = true;
    }
    if (void* const list = StateList(unit, StateSkillDelay); list != nullptr) {
        SetExpiry(list, 0);
        running = true;
    }
    return running;
}

void ResetServerPlayer(void* game, void* unit) noexcept {
    ServerResets.fetch_add(1, std::memory_order_relaxed);
    if (!EndCooldowns(unit)) return;
    // Section 3 c: the expiry pass on the next frame takes both lists down.
    SetEvent(game, unit, EventStatExpiry, 0, 0, 0, 0);
    ServerResetsRunning.fetch_add(1, std::memory_order_relaxed);
}

void ResetClientPlayer(void* unit) noexcept {
    ClientResets.fetch_add(1, std::memory_order_relaxed);
    if (EndCooldowns(unit)) ClientResetsRunning.fetch_add(1, std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------
// The passes
// ---------------------------------------------------------------------------
void ServerPass() noexcept {
    const std::uint64_t generation = Generation.load();
    if (ServerSeenGeneration != generation) {
        ServerSeen.clear();
        ServerSeenGeneration = generation;
    }
    thread_local std::vector<Seen> next;
    next.clear();
    if (Read<std::int32_t>(reinterpret_cast<const void*>(Base + RegistryReadyRva), 0) != 0) {
        const auto* const buckets = reinterpret_cast<const std::uint8_t* const*>(Base + RegistryRva);
        std::size_t players = 0;
        std::size_t visited = 0;
        for (std::size_t bucket = 0; bucket < RegistryBuckets && visited <= 256; ++bucket) {
            for (const std::uint8_t* client = buckets[bucket]; client != nullptr && ++visited <= 256;
                    client = Read<const std::uint8_t*>(client, RegistryNext)) {
                void* const game = Read<void*>(client, RegistryGame);
                if (game == nullptr || Read<std::uint32_t>(client, RegistryUnitType) != UnitPlayer) continue;
                const std::uint32_t id = Read<std::uint32_t>(client, RegistryUnitId);
                void* const unit = ServerUnit(game, UnitPlayer, id);
                if (unit == nullptr || UnitType(unit) != UnitPlayer || ++players > MaxPlayers) continue;
                const bool has = CheckState(unit, Settings.state) != 0;
                bool had = false;
                for (const Seen& seen : ServerSeen) {
                    if (seen.game == game && seen.id == id) {
                        had = seen.had;
                        break;
                    }
                }
                if (has && !had) ResetServerPlayer(game, unit);
                next.push_back({ game, id, has });
            }
        }
    }
    ServerSeen.swap(next);
    ServerPasses.fetch_add(1, std::memory_order_relaxed);
}

void ClientPass() noexcept {
    const std::uint64_t generation = Generation.load();
    if (ClientSeenGeneration != generation) {
        ClientSeen = {};
        ClientSeenGeneration = generation;
    }
    void* const unit = PlayerFromIndex(LocalPlayerIndex());
    if (unit == nullptr || UnitType(unit) != UnitPlayer) {
        ClientSeen = {};
        return;
    }
    const std::uint32_t id = Read<std::uint32_t>(unit, UnitId);
    if (!ClientSeen.known || ClientSeen.id != id) ClientSeen = { id, true, false };
    const bool has = CheckState(unit, Settings.state) != 0;
    if (has && !ClientSeen.had) ResetClientPlayer(unit);
    ClientSeen.had = has;
    ClientPasses.fetch_add(1, std::memory_order_relaxed);
}

auto Active() noexcept -> bool {
    return Ready && Settings.enabled && Settings.state > 0;
}

void __cdecl ServerTick(const D2RL::PluginContext* context, void* userData) noexcept {
    const auto id = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(userData));
    if (context == nullptr || ServerChain.load() != id) return;
    if (Active()) ServerPass();
    if (ThreadSvc->runOnGameThread(context, &ServerTick, userData) != D2RL::Threads::Result::Success) {
        std::uint64_t expected = id;
        (void)ServerChain.compare_exchange_strong(expected, 0);
    }
}

void __cdecl ClientTick(const D2RL::PluginContext* context, void* userData) noexcept {
    const auto id = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(userData));
    if (context == nullptr || ClientChain.load() != id) return;
    if (Active()) ClientPass();
    if (ThreadSvc->runOnUiThread(context, &ClientTick, userData) != D2RL::Threads::Result::Success) {
        std::uint64_t expected = id;
        (void)ClientChain.compare_exchange_strong(expected, 0);
    }
}

void StartChains(const D2RL::PluginContext* context) noexcept {
    if (!Ready || ThreadSvc == nullptr) return;
    if (ServerChain.load() == 0) {
        const std::uint64_t id = ChainCounter.fetch_add(1) + 1;
        ServerChain.store(id);
        // A remote TCP/IP client has no game thread here; only its UI pass runs.
        if (ThreadSvc->runOnGameThread(context, &ServerTick, reinterpret_cast<void*>(static_cast<std::uintptr_t>(id)))
                != D2RL::Threads::Result::Success) {
            std::uint64_t expected = id;
            (void)ServerChain.compare_exchange_strong(expected, 0);
        }
    }
    if (ClientChain.load() == 0) {
        const std::uint64_t id = ChainCounter.fetch_add(1) + 1;
        ClientChain.store(id);
        if (ThreadSvc->runOnUiThread(context, &ClientTick, reinterpret_cast<void*>(static_cast<std::uintptr_t>(id)))
                != D2RL::Threads::Result::Success) {
            std::uint64_t expected = id;
            (void)ClientChain.compare_exchange_strong(expected, 0);
        }
    }
}

void __cdecl OnGameJoined(const D2RL::PluginContext*, const D2RL::Lifecycle::GameplayEvent*, void*) noexcept {
    Generation.fetch_add(1);
}

void __cdecl OnLocalPlayerReady(const D2RL::PluginContext* context, const D2RL::Lifecycle::GameplayEvent*, void*) noexcept {
    if (context != nullptr) StartChains(context);
}

void __cdecl OnGameLeft(const D2RL::PluginContext*, const D2RL::Lifecycle::GameplayEvent*, void*) noexcept {
    Generation.fetch_add(1);
    ServerChain.store(0);
    ClientChain.store(0);
}

// ---------------------------------------------------------------------------
// Installation
// ---------------------------------------------------------------------------
struct Witness {
    std::uint64_t       rva;
    const std::uint8_t* bytes;
    std::uint32_t       size;
    const char*         name;
};

template <std::size_t N>
constexpr auto W(std::uint64_t rva, const std::uint8_t (&bytes)[N], const char* name) noexcept -> Witness {
    return { rva, bytes, static_cast<std::uint32_t>(N), name };
}

// An entry another plugin hooked starts with a jump; the hook keeps its meaning.
auto EntryHooked(std::uint64_t rva) noexcept -> bool {
    const auto* code = reinterpret_cast<const std::uint8_t*>(Base + rva);
    return code[0] == 0xE9 || (code[0] == 0xFF && code[1] == 0x25);
}

auto Verify() -> bool {
    char text[256];
    // Entries the plugin calls: another plugin may hook them.
    for (const Witness& w : { W(CheckStateRva, CheckStateEntry, "STATES_CheckState"),
                              W(StateListRva, StateListEntry, "the state stat list lookup"),
                              W(SetEventRva, SetEventEntry, "EVENT_SetEvent"),
                              W(UnitTypeRva, UnitTypeEntry, "UNITS_GetUnitType"),
                              W(ServerUnitRva, ServerUnitEntry, "SUNIT_GetServerUnit"),
                              W(LocalPlayerIndexRva, LocalPlayerIndexEntry, "the local player index"),
                              W(PlayerFromIndexRva, PlayerFromIndexEntry, "CLIENT_GetLocalPlayer") }) {
        if (!Context->CheckExpectedBytes(w.rva, w.bytes, w.size) && !EntryHooked(w.rva)) {
            std::snprintf(text, sizeof(text), "%s at 0x%llX does not match D2RLoader 1.3.1", w.name,
                static_cast<unsigned long long>(w.rva));
            Problem = text;
            return false;
        }
    }
    // Code whose exact bytes the reset relies on.
    for (const Witness& w : { W(SetExpiryRva, SetExpiryBody, "the stat list expiry setter"),
                              W(ExpireCooldownsRva, ExpireCooldownsJump, "the ExpireWideSkillCooldowns thunk"),
                              W(ExpireCooldownsTailRva, ExpireCooldownsTail, "the ExpireWideSkillCooldowns thunk"),
                              W(ServerCallbackRva, ServerCallback, "the server cooldown callback"),
                              W(ServerExpiryRva, ServerExpiryBody, "the player event 12 handler"),
                              W(RegistryResetRva, RegistryReset, "the server client registry reset"),
                              W(ClientLookupRva, ClientLookup, "the server client lookup"),
                              W(ClientPlayerRva, ClientPlayer, "the server client fields") }) {
        if (!Context->CheckExpectedBytes(w.rva, w.bytes, w.size)) {
            std::snprintf(text, sizeof(text), "%s at 0x%llX does not match D2RLoader 1.3.1", w.name,
                static_cast<unsigned long long>(w.rva));
            Problem = text;
            return false;
        }
    }
    // Event 12 must still be the expiry pass for players.
    const auto slot = Read<std::uintptr_t>(reinterpret_cast<const void*>(Base + PlayerEventsRva), EventStatExpiry * 8);
    if (slot != Base + ServerExpiryRva) {
        Problem = "player event 12 is no longer the stat expiry pass 0x42EA80";
        return false;
    }
    return true;
}

void BindNatives() noexcept {
    CheckState       = At<CheckStateFn>(CheckStateRva);
    StateList        = At<StateListFn>(StateListRva);
    SetExpiry        = At<SetExpiryFn>(SetExpiryRva);
    ExpireCooldowns  = At<ExpireCooldownsFn>(ExpireCooldownsRva);
    SetEvent         = At<SetEventFn>(SetEventRva);
    UnitType         = At<UnitTypeFn>(UnitTypeRva);
    ServerUnit       = At<ServerUnitFn>(ServerUnitRva);
    LocalPlayerIndex = At<LocalIndexFn>(LocalPlayerIndexRva);
    PlayerFromIndex  = At<PlayerFromIndexFn>(PlayerFromIndexRva);
}

auto Install() -> bool {
    if (!Verify()) return false;
    if (Context->QueryService(&ThreadSvc) != D2RL::ServiceQueryResult::Success
            || !D2RL::HasThreadServiceField(ThreadSvc, D2RL::ThreadServiceRequiredSize)) {
        ThreadSvc = nullptr;
    }
    if (Context->QueryService(&LifecycleSvc) != D2RL::ServiceQueryResult::Success
            || !D2RL::HasLifecycleServiceField(LifecycleSvc, D2RL::LifecycleServiceRequiredSize)) {
        LifecycleSvc = nullptr;
    }
    if (ThreadSvc == nullptr || LifecycleSvc == nullptr) {
        Problem = "D2RLoader's thread or lifecycle service is unavailable";
        return false;
    }
    BindNatives();
    struct Listener {
        D2RL::Lifecycle::GameplayEventKind     kind;
        D2RL::Lifecycle::GameplayEventCallback callback;
    };
    const std::array<Listener, 3> listeners{ {
        { D2RL::Lifecycle::GameplayEventKind::GameJoined, &OnGameJoined },
        { D2RL::Lifecycle::GameplayEventKind::LocalPlayerReady, &OnLocalPlayerReady },
        { D2RL::Lifecycle::GameplayEventKind::GameLeft, &OnGameLeft },
    } };
    for (const Listener& listener : listeners) {
        const D2RL::Lifecycle::GameplayEventListener registration{
            .structSize = D2RL::Lifecycle::GameplayEventListenerSize,
            .kind       = listener.kind,
            .callback   = listener.callback,
        };
        D2RL::Lifecycle::ListenerHandle handle = D2RL::Lifecycle::InvalidHandle;
        if (LifecycleSvc->registerGameplayEventListener(Context, &registration, &handle) != D2RL::Lifecycle::Result::Success) {
            Problem = "a game join/leave listener was refused";
            return false;
        }
    }
    Ready = true;
    return true;
}

// ---------------------------------------------------------------------------
// Configuration file and console
// ---------------------------------------------------------------------------
void ReadConfiguration() {
    std::string problems;
    if (!Context->EnsureConfig(DefaultConfigToml)) {
        Context->LogWarn("CooldownReset: the config file could not be created; using the defaults.");
        Settings = ParseConfig(DefaultConfigToml, problems);
        return;
    }
    std::uint32_t required = 0;
    std::string buffer(16384, '\0');
    if (!Context->ReadConfig(buffer.data(), static_cast<std::uint32_t>(buffer.size()), &required)) {
        Context->LogWarn("CooldownReset: the config file could not be read; using the defaults.");
        Settings = ParseConfig(DefaultConfigToml, problems);
        return;
    }
    buffer.resize(std::strlen(buffer.c_str()));
    Settings = ParseConfig(buffer, problems);
    ConfigProblems = problems;
    if (!ConfigProblems.empty()) D2RL::LogWarnF(Context, "CooldownReset: config:%s", ConfigProblems.c_str());
}

auto StateText() -> std::string {
    if (!Settings.enabled) return "off (disabled in the config)";
    if (!Ready) return "off (" + (Problem.empty() ? std::string("not installed") : Problem) + ")";
    if (Settings.state <= 0) return "off (no state set)";
    return "on";
}

auto __cdecl StatusCommand(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command, void*) noexcept
        -> D2RL::ConsoleCommandResult {
    if (command == nullptr || command->plugin == nullptr) return D2RL::ConsoleCommandResult::Failed;
    const D2RL::PluginContext* const plugin = command->plugin;
    char text[512];
    std::snprintf(text, sizeof(text), "Cooldown Reset %s: %s, state %d.", PluginVersion, StateText().c_str(), Settings.state);
    plugin->WriteConsoleMessage(text);
    std::snprintf(text, sizeof(text), "Server: %llu resets (%llu with cooldowns running), %llu passes. Client: %llu resets "
        "(%llu with cooldowns running), %llu passes.", static_cast<unsigned long long>(ServerResets.load()),
        static_cast<unsigned long long>(ServerResetsRunning.load()), static_cast<unsigned long long>(ServerPasses.load()),
        static_cast<unsigned long long>(ClientResets.load()), static_cast<unsigned long long>(ClientResetsRunning.load()),
        static_cast<unsigned long long>(ClientPasses.load()));
    plugin->WriteConsoleMessage(text);
    if (!ConfigProblems.empty()) {
        std::snprintf(text, sizeof(text), "Config:%s", ConfigProblems.c_str());
        plugin->WriteConsoleMessage(text);
    }
    return D2RL::ConsoleCommandResult::Handled;
}

constexpr D2RL::PluginInfo Info{
    .infoSize    = D2RL::PluginInfoSize,
    .abiVersion  = D2RL_PLUGIN_ABI_VERSION,
    .id          = "celestialrayone.cooldown-reset",
    .name        = "Cooldown Reset",
    .version     = PluginVersion,
    .author      = "CelestialRayOne",
    .description = "A player who gains the configured state has every skill cooldown he has running ended at once.",
    .flags       = D2RL::PluginFlags::Shared | D2RL::PluginFlags::NativeHooks,
};

}  // namespace
}  // namespace CelestialRayOne::CooldownReset

using namespace CelestialRayOne::CooldownReset;

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* {
    return &Info;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
    if (!D2RL::HasContext(context)) return false;
    Context = context;
    Base = context->exeBase;
    ReadConfiguration();
    if (!context->RegisterConsoleCommand("cooldownreset", &StatusCommand, "Cooldown Reset: state and counters.")) {
        context->LogWarn("CooldownReset: the console command could not be registered.");
    }
    if (!Settings.enabled) {
        Problem = "disabled in the config";
        return true;
    }
    if (!Install()) {
        D2RL::LogErrorF(context, "CooldownReset: not installed: %s.", Problem.c_str());
        return true;
    }
    if (Settings.state <= 0) context->LogWarn("CooldownReset: no state set; nothing resets until state = <id> is set.");
    D2RL::LogInfoF(context, "CooldownReset %s: installed (state %d).", PluginVersion, Settings.state);
    return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
    ServerChain.store(0);
    ClientChain.store(0);
    Ready = false;
    Context = nullptr;
}
