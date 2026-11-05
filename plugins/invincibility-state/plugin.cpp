// Invincible State
//
// While a player, mercenary, monster or summon has one of the configured
// states, it cannot lose life by any means and cannot die from damage.
// Healing still works.
//
// Everything below was read out of the D2RLoader 1.3.1 process dump (md5
// 8af7a47ce9077de0378c69af2930854d) with IDA. Every game address the plugin
// uses is guarded by a byte witness at load.
//
// ---------------------------------------------------------------------------
// The one funnel every life write goes through
// ---------------------------------------------------------------------------
//   D2RLoader moved the stat store into D2RCore: the game's STATLIST_SetUnitStat
//   0x2F7D10 is a 10-byte import thunk, jmp [rip+3B32532h] / nop x4, whose
//   pointer at 0x3E2A248 leads to D2RCore!SetWideUnitStat (unit, stat, value,
//   layer). Every life change the game makes is a call of that thunk:
//     0x44D093  the damage commit (SUNITDMG_ExecuteEvents): every hit, thorns
//               and reflected damage, corpse explosions, everything a missile,
//               melee or skill deals; Alchemy's relay there calls it too
//     0x42E6D1  the player regen tick: poison, burning and every other
//               negative hpregen on a player (the tick never goes below 1 life)
//     0x448D4C  the monster regen tick: the same for monsters and mercenaries
//   The plugin swaps that one pointer for its own function, which calls what
//   was there before (D2RCore, or the next plugin that swapped it): a write of
//   stat 6 (hitpoints, layer 0) that would lower the life of a unit with one
//   of the states writes its current life instead. Other plugins swap other
//   pointers of the same table the same way (Monster Regen Start, Charm
//   Dedup, Loot Filter); none uses this one, and the swap chains in any order.
//
// ---------------------------------------------------------------------------
// The two places that decide death themselves
// ---------------------------------------------------------------------------
//   The damage commit reads life back after the write and marks the unit as
//   dying only when it is 0, so a kept life means no death. The player regen
//   tick never lets life drop under 256 (1 life). The monster regen tick,
//   0x448C00, decides death on its own value:
//     448D35  cmp esi,1 / mov r14d,r15d / mov rcx,rdi / cmovge r14d,esi
//     448D42  xor r9d,r9d / mov r8d,r14d / lea edx,[r9+6] / call 2F7D10
//     ...
//     448D9D  test r14d,r14d / jne 448F87   ; 0 -> the poison or burn kill
//   So 448D45 (mov r8d,r14d / lea edx,[r9+6], 7 bytes) becomes a call to a
//   relay that asks the plugin for the life to use and puts it in both r14d
//   (the value the death check reads) and r8d, then replays lea edx. rdi is
//   the monster. Monster Life Cap patches 448D19..448D1F, before this.
//
// ---------------------------------------------------------------------------
// Not damage, left alone
// ---------------------------------------------------------------------------
//   Life spent as a skill cost goes through the stat add 0x2F34F0 (Blood Magic)
//   or inside D2RCore, not through the writer above, so costs still apply.
//   Mana burn, stamina drain, freezing, knockback and hit recovery are not
//   life loss and still happen; the attacker's life and mana leech, worked out
//   from the damage of the hit, still happen too.
//
// ---------------------------------------------------------------------------
// ABIs (x64)
// ---------------------------------------------------------------------------
//   SetWideUnitStat     (unit, stat, value, layer)
//   0x2F5020            STATLIST_GetUnitStat (unit, stat, layer) -> int32 (thunk)
//   0x3351B0            STATES_CheckState (unit, state) -> int32; asserts on an
//                       id at or past the states count, which is
//                       [0x300A90(0x34A0E0(unit)) + 298h]

#include <D2RLPlugin/api.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace CelestialRayOne::InvincibleState {
namespace {

constexpr char PluginVersion[] = "1.0.0";

constexpr const char DefaultConfigToml[] = R"TOML(# Invincible State
#
# While a unit (player, mercenary, monster or summon) has one of the states
# below, it cannot lose life by any means and cannot die from damage. Every
# hit, thorns and reflected damage, poison, burning, open wounds and any other
# life the game takes away is refused, because all of it goes through the one
# stat writer this plugin guards. Healing still works.
#
# Not damage, so still applied: life spent as a skill cost (Blood Magic), mana
# burn, stamina drain, freezing, knockback, hit recovery, and the attacker's
# leech.
#
# states: states.txt row ids (the *ID column). Empty: nothing happens.
# The console command "invincible" shows the state and counters.

enabled = true
states = []
)TOML";

// ---------------------------------------------------------------------------
// Native anchors (D2RLoader 1.3.1)
// ---------------------------------------------------------------------------
constexpr std::uint64_t SetStatThunkRva = 0x2F7D10;  // jmp [rip+3B32532h] / nop x4
constexpr std::uint8_t  SetStatThunk[]{ 0xFF, 0x25, 0x32, 0x25, 0xB3, 0x03, 0x90, 0x90, 0x90, 0x90 };
constexpr std::uint64_t SetStatSlotRva  = 0x3E2A248;
static_assert(SetStatThunkRva + 6 + 0x03B32532 == SetStatSlotRva);

constexpr std::uint64_t ReadStatThunkRva = 0x2F5020;  // jmp [rip+3B351F2h] / nop x4 -> ReadWideUnitStat
constexpr std::uint8_t  ReadStatThunk[]{ 0xFF, 0x25, 0xF2, 0x51, 0xB3, 0x03, 0x90, 0x90, 0x90, 0x90 };

constexpr std::uint64_t CheckStateRva      = 0x3351B0;
constexpr std::uint64_t CheckStateCountRva = 0x3351DF;  // mov rcx,rsi / call 34A0E0 / movzx ecx,al / call 300A90 / mov rdi,[rax+298h]
constexpr std::uint8_t  CheckStateCount[]{
    0x48, 0x8B, 0xCE, 0xE8, 0xF9, 0x4E, 0x01, 0x00, 0x0F, 0xB6, 0xC8, 0xE8,
    0xA1, 0xB8, 0xFC, 0xFF, 0x48, 0x8B, 0xB8, 0x98, 0x02, 0x00, 0x00,
};
constexpr std::uint64_t DataContextRva    = 0x34A0E0;
constexpr std::uint64_t DataTablesRva     = 0x300A90;
constexpr std::size_t   StatesCountOffset = 0x298;

// The monster regen tick, 448D35..448DA6 (113 bytes).
constexpr std::uint8_t RegenWindow[]{
    0x83, 0xFE, 0x01, 0x45, 0x8B, 0xF7, 0x48, 0x8B, 0xCF, 0x44, 0x0F, 0x4D, 0xF6, 0x45, 0x33, 0xC9,
    0x45, 0x8B, 0xC6, 0x41, 0x8D, 0x51, 0x06, 0xE8, 0xBF, 0xEF, 0xEA, 0xFF, 0x45, 0x33, 0xC0, 0xBA,
    0x60, 0x01, 0x00, 0x00, 0x48, 0x8B, 0xCF, 0xE8, 0xBF, 0xC2, 0xEA, 0xFF, 0x48, 0x8B, 0xCF, 0x8B,
    0xD8, 0xE8, 0x75, 0xFF, 0x13, 0x00, 0x0F, 0xB6, 0xC8, 0x0F, 0xB6, 0xC3, 0x8B, 0xF1, 0x2B, 0xC1,
    0x99, 0x33, 0xC2, 0x2B, 0xC2, 0x83, 0xF8, 0x04, 0x7E, 0x1E, 0x0F, 0xB6, 0xD1, 0x48, 0x8B, 0xCF,
    0xE8, 0xE6, 0x04, 0x0F, 0x00, 0x45, 0x33, 0xC9, 0x44, 0x8B, 0xC6, 0xBA, 0x60, 0x01, 0x00, 0x00,
    0x48, 0x8B, 0xCF, 0xE8, 0x73, 0xEF, 0xEA, 0xFF, 0x45, 0x85, 0xF6, 0x0F, 0x85, 0xE1, 0x01, 0x00,
    0x00,
};
constexpr std::uint64_t RegenWindowRva = 0x448D35;
constexpr std::uint64_t RegenSiteRva   = 0x448D45;  // mov r8d,r14d / lea edx,[r9+6]
constexpr std::uint8_t  RegenSiteStock[]{ 0x45, 0x8B, 0xC6, 0x41, 0x8D, 0x51, 0x06 };
static_assert(sizeof(RegenWindow) == 0x448DA6 - RegenWindowRva);

// Relay entered by the call at 448D45 (rdi = the monster, r14d = its new life):
//   push rcx / sub rsp,20h / mov rcx,rdi / mov edx,r14d / call [slot RegenLife]
//   add rsp,20h / pop rcx / mov r14d,eax / mov r8d,eax / xor r9d,r9d
//   lea edx,[r9+6] / ret
constexpr std::uint8_t RegenRelay[]{
    0x51, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x89, 0xF9, 0x44, 0x89, 0xF2, 0xFF, 0x15, 0x17, 0x00, 0x00,
    0x00, 0x48, 0x83, 0xC4, 0x20, 0x59, 0x41, 0x89, 0xC6, 0x41, 0x89, 0xC0, 0x45, 0x31, 0xC9, 0x41,
    0x8D, 0x51, 0x06, 0xC3, 0x90, 0x90, 0x90, 0x90, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
constexpr std::size_t RegenRelaySlot = sizeof(RegenRelay) - 8;

constexpr std::int32_t StatLife   = 6;
constexpr std::uint32_t UnitPlayer = 0, UnitMonster = 1;

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
using SetStatFn    = std::int64_t (__fastcall*)(void*, std::int32_t, std::int32_t, std::int32_t);
using ReadStatFn   = std::int32_t (__fastcall*)(void*, std::int32_t, std::int32_t);
using CheckStateFn = std::int32_t (__fastcall*)(void*, std::int32_t);
using ContextFn    = std::uint8_t (__fastcall*)(void*);
using TablesFn     = std::uint8_t* (__fastcall*)(std::uint8_t);

const D2RL::PluginContext* Context{};
std::uintptr_t             Base{};

struct Config {
    bool                      enabled{ true };
    std::vector<std::int32_t> states;
};
Config      Settings{};
std::string ConfigProblems;

ReadStatFn   ReadStat{};
CheckStateFn CheckState{};
ContextFn    DataContext{};
TablesFn     DataTables{};

std::atomic<SetStatFn> OriginalSetStat{ nullptr };
void**                 SetStatSlot{};
bool                   SlotSwapped{};
std::uint8_t*          RelayPage{};
bool                   RegenPatched{};
std::uint8_t           RegenWritten[sizeof(RegenSiteStock)]{};
std::atomic<bool>      Active{ false };
std::string            Problem;

std::atomic<std::uint64_t> HitsRefused{ 0 };
std::atomic<std::uint64_t> RegenRefused{ 0 };

template <typename T> auto At(std::uint64_t rva) noexcept -> T { return reinterpret_cast<T>(Base + rva); }
template <typename T> auto Read(const void* p, std::size_t off) noexcept -> T {
    T v{}; std::memcpy(&v, static_cast<const std::uint8_t*>(p) + off, sizeof(T)); return v;
}

// ---------------------------------------------------------------------------
// Configuration (enabled = bool, states = [ids])
// ---------------------------------------------------------------------------
auto Trim(std::string_view s) -> std::string_view {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
    return s;
}

void ParseConfig(std::string_view text) {
    Config parsed{};
    std::string pending;
    std::size_t pos = 0;
    while (pos <= text.size()) {
        std::size_t end = text.find('\n', pos);
        if (end == std::string_view::npos) end = text.size();
        std::string_view line = text.substr(pos, end - pos);
        pos = end + 1;
        if (const std::size_t hash = line.find('#'); hash != std::string_view::npos) line = line.substr(0, hash);
        line = Trim(line);
        if (!pending.empty()) {
            pending.append(line);
            if (line.find(']') == std::string_view::npos) continue;
            line = pending; pending.clear();
        }
        if (line.empty()) continue;
        const std::size_t eq = line.find('=');
        if (eq == std::string_view::npos) { ConfigProblems += " a line without '='"; continue; }
        const std::string key(Trim(line.substr(0, eq)));
        std::string value(Trim(line.substr(eq + 1)));
        if (key == "states" && value.find('[') != std::string::npos && value.find(']') == std::string::npos) {
            pending = key + " = " + value; continue;
        }
        if (key == "enabled") {
            if (value == "true") parsed.enabled = true;
            else if (value == "false") parsed.enabled = false;
            else ConfigProblems += " enabled expects true or false;";
        } else if (key == "states") {
            const std::size_t a = value.find('['), b = value.rfind(']');
            if (a == std::string::npos || b == std::string::npos || b < a) { ConfigProblems += " states expects [ids];"; continue; }
            std::string list = value.substr(a + 1, b - a - 1);
            std::size_t p = 0;
            while (p < list.size()) {
                std::size_t q = list.find(',', p);
                if (q == std::string::npos) q = list.size();
                const std::string item(Trim(std::string_view(list).substr(p, q - p)));
                p = q + 1;
                if (item.empty()) continue;
                char* stop = nullptr;
                const long id = std::strtol(item.c_str(), &stop, 10);
                if (stop == item.c_str() || *stop != '\0' || id < 0 || id > 65535) { ConfigProblems += " states: \"" + item + "\" is not a state id;"; continue; }
                if (std::find(parsed.states.begin(), parsed.states.end(), static_cast<std::int32_t>(id)) == parsed.states.end()) {
                    parsed.states.push_back(static_cast<std::int32_t>(id));
                }
            }
        } else {
            ConfigProblems += " unknown key " + key + ";";
        }
    }
    Settings = parsed;
}

void ReadConfiguration() {
    if (!Context->EnsureConfig(DefaultConfigToml)) {
        Context->LogWarn("InvincibleState: the config file could not be created; using the defaults.");
        ParseConfig(DefaultConfigToml);
        return;
    }
    std::uint32_t required = 0;
    std::string buffer(16384, '\0');
    if (!Context->ReadConfig(buffer.data(), static_cast<std::uint32_t>(buffer.size()), &required)) {
        Context->LogWarn("InvincibleState: the config file could not be read; using the defaults.");
        ParseConfig(DefaultConfigToml);
        return;
    }
    buffer.resize(std::strlen(buffer.c_str()));
    ParseConfig(buffer);
    if (!ConfigProblems.empty()) D2RL::LogWarnF(Context, "InvincibleState: config:%s", ConfigProblems.c_str());
}

// ---------------------------------------------------------------------------
// The rule
// ---------------------------------------------------------------------------
auto Invincible(void* unit) noexcept -> bool {
    if (unit == nullptr) return false;
    const std::uint32_t type = Read<std::uint32_t>(unit, 0);
    if (type != UnitPlayer && type != UnitMonster) return false;
    const std::uint8_t* tables = DataTables(DataContext(unit));
    if (tables == nullptr) return false;
    const std::uint64_t count = Read<std::uint64_t>(tables, StatesCountOffset);
    for (const std::int32_t state : Settings.states) {
        if (static_cast<std::uint64_t>(state) < count && CheckState(unit, state) != 0) return true;
    }
    return false;
}

// The swapped pointer at 0x3E2A248: every SetWideUnitStat call of the game.
auto __fastcall GuardSetStat(void* unit, std::int32_t stat, std::int32_t value, std::int32_t layer) noexcept -> std::int64_t {
    const SetStatFn original = OriginalSetStat.load(std::memory_order_acquire);
    if (stat == StatLife && layer == 0 && Active.load(std::memory_order_relaxed) && Invincible(unit)) {
        const std::int32_t life = ReadStat(unit, StatLife, 0);
        if (value < life) {
            value = life;
            HitsRefused.fetch_add(1, std::memory_order_relaxed);
        }
    }
    return original(unit, stat, value, layer);
}

// Entered from the relay at 448D45: the life the monster regen tick writes and
// then tests for death.
auto __fastcall RegenLife(void* unit, std::int32_t life) noexcept -> std::int32_t {
    if (Active.load(std::memory_order_relaxed) && Invincible(unit)) {
        const std::int32_t current = ReadStat(unit, StatLife, 0);
        if (life < current) {
            RegenRefused.fetch_add(1, std::memory_order_relaxed);
            return current;
        }
    }
    return life;
}

// ---------------------------------------------------------------------------
// Install
// ---------------------------------------------------------------------------
auto Executable(DWORD protect) noexcept -> bool {
    return (protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
}

auto WriteSlot(void* expected, void* replacement) noexcept -> bool {
    DWORD previous = 0;
    if (!VirtualProtect(SetStatSlot, sizeof(void*), PAGE_EXECUTE_READWRITE, &previous)) return false;
    const void* seen = InterlockedCompareExchangePointer(SetStatSlot, replacement, expected);
    DWORD ignored = 0;
    VirtualProtect(SetStatSlot, sizeof(void*), previous, &ignored);
    return seen == expected && *reinterpret_cast<void* volatile*>(SetStatSlot) == replacement;
}

auto SwapSlot() noexcept -> bool {
    SetStatSlot = At<void**>(SetStatSlotRva);
    void* current = *reinterpret_cast<void* volatile*>(SetStatSlot);
    if (current == reinterpret_cast<void*>(&GuardSetStat)) { Problem = "the stat writer already leads here"; return false; }
    MEMORY_BASIC_INFORMATION info{};
    if (current == nullptr || VirtualQuery(current, &info, sizeof(info)) == 0 || info.State != MEM_COMMIT || !Executable(info.Protect)) {
        Problem = "the stat writer pointer at 0x3E2A248 is not code yet (D2RLoader has not bound it)";
        return false;
    }
    OriginalSetStat.store(reinterpret_cast<SetStatFn>(current), std::memory_order_release);
    if (!WriteSlot(current, reinterpret_cast<void*>(&GuardSetStat))) {
        OriginalSetStat.store(nullptr, std::memory_order_release);
        Problem = "the stat writer pointer could not be written";
        return false;
    }
    SlotSwapped = true;
    return true;
}

auto AllocateNear(std::uintptr_t target) noexcept -> std::uint8_t* {
    SYSTEM_INFO system{};
    GetSystemInfo(&system);
    const std::uintptr_t step = system.dwAllocationGranularity;
    const std::uintptr_t low  = target > 0x7FF00000ULL ? target - 0x7FF00000ULL : step;
    const std::uintptr_t high = target + 0x7FF00000ULL;
    for (std::uintptr_t at = (target & ~(step - 1)) - step; at > low; at -= step) {
        if (void* p = VirtualAlloc(reinterpret_cast<void*>(at), 4096, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE)) return static_cast<std::uint8_t*>(p);
    }
    for (std::uintptr_t at = (target & ~(step - 1)) + step; at < high; at += step) {
        if (void* p = VirtualAlloc(reinterpret_cast<void*>(at), 4096, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE)) return static_cast<std::uint8_t*>(p);
    }
    return nullptr;
}

auto PatchRegen() noexcept -> bool {
    if (!Context->CheckExpectedBytes(RegenWindowRva, RegenWindow, static_cast<std::uint32_t>(sizeof(RegenWindow)))) {
        Problem = "the monster regen tick at 0x448D35 does not match D2RLoader 1.3.1 (another plugin may own it)";
        return false;
    }
    RelayPage = AllocateNear(Base + RegenSiteRva);
    if (RelayPage == nullptr) { Problem = "no memory for the relay near the game"; return false; }
    std::memcpy(RelayPage, RegenRelay, sizeof(RegenRelay));
    const auto target = reinterpret_cast<std::uintptr_t>(&RegenLife);
    std::memcpy(RelayPage + RegenRelaySlot, &target, sizeof(target));
    FlushInstructionCache(GetCurrentProcess(), RelayPage, sizeof(RegenRelay));
    const std::intptr_t rel = reinterpret_cast<std::intptr_t>(RelayPage) - static_cast<std::intptr_t>(Base + RegenSiteRva + 5);
    if (rel < INT32_MIN || rel > INT32_MAX) { Problem = "the relay is out of reach of the regen tick"; return false; }
    const auto rel32 = static_cast<std::int32_t>(rel);
    RegenWritten[0] = 0xE8;
    std::memcpy(RegenWritten + 1, &rel32, 4);
    RegenWritten[5] = 0x90;
    RegenWritten[6] = 0x90;
    RegenPatched = Context->PatchBytes(RegenSiteRva, RegenSiteStock, sizeof(RegenSiteStock), RegenWritten, sizeof(RegenWritten));
    if (!RegenPatched) Problem = "the call at 0x448D45 could not be written";
    return RegenPatched;
}

auto Install() noexcept -> bool {
    ReadStat    = At<ReadStatFn>(ReadStatThunkRva);
    CheckState  = At<CheckStateFn>(CheckStateRva);
    DataContext = At<ContextFn>(DataContextRva);
    DataTables  = At<TablesFn>(DataTablesRva);
    if (!Context->CheckExpectedBytes(SetStatThunkRva, SetStatThunk, static_cast<std::uint32_t>(sizeof(SetStatThunk)))
            || !Context->CheckExpectedBytes(ReadStatThunkRva, ReadStatThunk, static_cast<std::uint32_t>(sizeof(ReadStatThunk)))
            || !Context->CheckExpectedBytes(CheckStateCountRva, CheckStateCount, static_cast<std::uint32_t>(sizeof(CheckStateCount)))) {
        Problem = "the stat thunks or STATES_CheckState do not match D2RLoader 1.3.1";
        return false;
    }
    if (!SwapSlot()) return false;
    if (!PatchRegen()) {
        D2RL::LogWarnF(Context, "InvincibleState: %s; monsters can still die from poison and burning while invincible.", Problem.c_str());
    }
    Active.store(true, std::memory_order_release);
    return true;
}

void Restore() noexcept {
    Active.store(false, std::memory_order_release);
    if (RegenPatched && Context->PatchBytes(RegenSiteRva, RegenWritten, sizeof(RegenWritten), RegenSiteStock, sizeof(RegenSiteStock))) {
        RegenPatched = false;
    }
    if (SlotSwapped) {
        const SetStatFn original = OriginalSetStat.load(std::memory_order_acquire);
        if (original != nullptr && WriteSlot(reinterpret_cast<void*>(&GuardSetStat), reinterpret_cast<void*>(original))) {
            SlotSwapped = false;
        } else {
            Context->LogWarn("InvincibleState: the stat writer pointer was changed by someone else; left as it is.");
        }
    }
}

// ---------------------------------------------------------------------------
// Console
// ---------------------------------------------------------------------------
auto __cdecl StatusCommand(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command, void*) noexcept -> D2RL::ConsoleCommandResult {
    if (command == nullptr || command->plugin == nullptr) return D2RL::ConsoleCommandResult::Failed;
    const auto* plugin = command->plugin;
    char text[512];
    std::string list;
    for (const std::int32_t s : Settings.states) list += (list.empty() ? "" : ", ") + std::to_string(s);
    std::snprintf(text, sizeof(text), "Invincible State %s: %s | states: %s", PluginVersion,
        !Settings.enabled ? "off" : Active.load() ? "on" : ("off: " + Problem).c_str(), list.empty() ? "none" : list.c_str());
    plugin->WriteConsoleMessage(text);
    std::snprintf(text, sizeof(text), "Life loss refused: %llu writes, %llu monster regen ticks (poison, burning). Monster regen guard: %s.",
        static_cast<unsigned long long>(HitsRefused.load()), static_cast<unsigned long long>(RegenRefused.load()),
        RegenPatched ? "on" : "off");
    plugin->WriteConsoleMessage(text);
    return D2RL::ConsoleCommandResult::Handled;
}

constexpr D2RL::PluginInfo Info{
    .infoSize    = D2RL::PluginInfoSize,
    .abiVersion  = D2RL_PLUGIN_ABI_VERSION,
    .id          = "celestialrayone.invincible-state",
    .name        = "Invincible State",
    .version     = PluginVersion,
    .author      = "CelestialRayOne",
    .description = "A unit with a configured state cannot lose life by any means and cannot die from damage.",
    .flags       = D2RL::PluginFlags::Shared | D2RL::PluginFlags::NativeHooks,
};

}  // namespace
}  // namespace CelestialRayOne::InvincibleState

using namespace CelestialRayOne::InvincibleState;

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* {
    return &Info;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
    if (!D2RL::HasContext(context)) return false;
    Context = context;
    Base = context->exeBase;
    ReadConfiguration();
    if (!context->RegisterConsoleCommand("invincible", &StatusCommand, "Invincible State: states and counters.")) {
        context->LogWarn("InvincibleState: the console command could not be registered.");
    }
    if (!Settings.enabled) { Problem = "disabled in the config"; return true; }
    if (Settings.states.empty()) context->LogWarn("InvincibleState: no states configured; nothing is invincible until states = [...] is set.");
    if (!Install()) {
        D2RL::LogErrorF(context, "InvincibleState: not installed: %s.", Problem.c_str());
        return true;
    }
    D2RL::LogInfoF(context, "InvincibleState %s: installed (%zu states, monster regen guard %s).", PluginVersion, Settings.states.size(),
        RegenPatched ? "on" : "off");
    return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
    if (Context != nullptr) Restore();
    Context = nullptr;
}
