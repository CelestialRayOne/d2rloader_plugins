// Magic Find Soft Tail
//
// Vanilla D2R applies diminishing returns to Magic Find for the three high
// qualities, and the curve converges to a finite limit, so past a point extra
// MF is worth nothing at all. This adds an unbounded linear term so stacking
// MF keeps paying, slowly.
//
//     vanilla:  MagicFind = 100 + dim * MF / (MF + dim)
//     patched:  MagicFind = 100 + dim * MF / (MF + dim) + MF * tail / 100
//
//     dim = 250 (unique), 500 (set), 600 (rare)
//
// MagicFind is then used as a divisor: chance = chance * 100 / MagicFind, and
// a lower chance value means a better roll. So a larger MagicFind is better.
//
// Native background, all read out of the live D2R image (build 92777) and
// disassembled before being relied on here.
//
//   D2GAME_ITEMS_RollItemQuality @ 0x4421B0 contains three identical blocks,
//   one per quality. Unique shown, the other two differ only in the constant:
//
//     4423CA  cmp  ebp, 0x6E          ; ebp = MF + 100
//     4423CD  jg   4423D3             ; MF > 10 -> diminishing path
//     4423CF  mov  ecx, ebp           ; MF <= 10 -> linear, no diminishing
//     4423D1  jmp  4423E5
//     4423D3  imul eax, edi, 0xFA     ; edi = raw MF, 0xFA = dim 250
//     4423D9  lea  ecx, [rdi + 0xFA]
//     4423DF  cdq
//     4423E0  idiv ecx                ; <- hook, exactly 5 bytes with the lea
//     4423E2  lea  ecx, [rax + 0x64]
//     4423E5  test ecx, ecx           ; <- continuation
//
//   The hook replaces `idiv ecx; lea ecx,[rax+0x64]` (F7 F9 8D 48 64) with a
//   jmp to a relay that re-executes those two instructions, adds the tail and
//   returns. edi and ecx are proven by the very bytes being replaced. Only ecx
//   is modified; every other register and the flags are restored.
//
//   Nothing jumps into the five replaced bytes: the only inbound targets in
//   the area are 4423D3 (from the jg), 4423E5 (from the jmp) and 442404.
//
// Not touched, and deliberately so:
//   - Magic quality divides by ebp = MF + 100 directly at 0x44258E. Vanilla
//     gives magic items the full value of MF with no diminishing returns at
//     all, so there is no tail to add.
//   - Superior (hi-quality) at 0x4425DE and normal never divide by MF.
//   - The MF <= 10 linear path is left alone; a tail there is worth 0 or 1.

#include <D2RLPlugin/api.h>

// Windows.h defines min/max as macros unless NOMINMAX is set.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

namespace CelestialRayOne::MagicFindSoftTail {
namespace {

// ---------------------------------------------------------------------------
// Native anchors
// ---------------------------------------------------------------------------

constexpr std::size_t QualityCount = 3;

struct QualitySite {
    const char*     name;
    std::uintptr_t  witnessRva;   // the cmp/jg gate, start of the 27-byte witness
    std::uintptr_t  hookRva;      // idiv ecx; lea ecx,[rax+0x64]
    std::int32_t    dim;
};

constexpr std::array<QualitySite, QualityCount> Sites{{
    { "unique", 0x4423CA, 0x4423E0, 250 },
    { "set",    0x442467, 0x44247D, 500 },
    { "rare",   0x4424F4, 0x44250A, 600 },
}};

constexpr std::uint32_t HookSize    = 5;
constexpr std::uint32_t WitnessSize = 27;

// 27-byte witness per quality: the gate, the linear bypass and the whole
// diminishing block including the dim constant. Pins each site to its quality.
constexpr std::array<std::array<std::uint8_t, WitnessSize>, QualityCount> Witnesses{{
    // unique, dim 250 = 0xFA
    {{0x83,0xFD,0x6E,0x7F,0x04,0x8B,0xCD,0xEB,0x12,
      0x69,0xC7,0xFA,0x00,0x00,0x00,0x8D,0x8F,0xFA,0x00,0x00,0x00,
      0x99,0xF7,0xF9,0x8D,0x48,0x64}},
    // set, dim 500 = 0x1F4
    {{0x83,0xFD,0x6E,0x7F,0x04,0x8B,0xCD,0xEB,0x12,
      0x69,0xC7,0xF4,0x01,0x00,0x00,0x8D,0x8F,0xF4,0x01,0x00,0x00,
      0x99,0xF7,0xF9,0x8D,0x48,0x64}},
    // rare, dim 600 = 0x258
    {{0x83,0xFD,0x6E,0x7F,0x04,0x8B,0xCD,0xEB,0x12,
      0x69,0xC7,0x58,0x02,0x00,0x00,0x8D,0x8F,0x58,0x02,0x00,0x00,
      0x99,0xF7,0xF9,0x8D,0x48,0x64}},
}};

// idiv ecx ; lea ecx,[rax+0x64]
constexpr std::array<std::uint8_t, HookSize> HookExpected{
    0xF7, 0xF9, 0x8D, 0x48, 0x64
};

constexpr std::size_t MaximumConfigBytes = 32'768;
constexpr std::size_t RelayBytes         = 4'096;

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

constexpr std::int32_t Inherit = -1;

struct Config {
    bool         enabled  = true;
    std::int32_t tail     = 10;                       // percent of raw MF
    std::array<std::int32_t, QualityCount> perQuality{Inherit, Inherit, Inherit};
};

constexpr char DefaultConfigToml[] =
    "# Magic Find Soft Tail\n"
    "#\n"
    "# Vanilla:  MagicFind = 100 + dim * MF / (MF + dim)\n"
    "# Patched:  MagicFind = 100 + dim * MF / (MF + dim) + MF * tail / 100\n"
    "#\n"
    "# dim is 250 for unique, 500 for set, 600 for rare. The vanilla term\n"
    "# converges, so past a point extra MF is worth nothing; the tail is an\n"
    "# unbounded trickle that keeps rewarding MF without removing the curve.\n"
    "#\n"
    "# Only unique, set and rare have diminishing returns in the first place.\n"
    "# Magic quality already gets the full value of MF, and superior and\n"
    "# normal never look at MF at all, so neither is affected here.\n"
    "\n"
    "[magic_find_soft_tail]\n"
    "\n"
    "# Master switch.\n"
    "enabled = true\n"
    "\n"
    "# Percent of raw MF added on top of the vanilla curve.\n"
    "# 10 keeps 10% of all your MF forever, which is the original port.\n"
    "# 0 reproduces vanilla exactly.\n"
    "tail_percent = 10\n"
    "\n"
    "# Optional per-quality overrides. -1 means inherit tail_percent.\n"
    "tail_percent_unique = -1\n"
    "tail_percent_set = -1\n"
    "tail_percent_rare = -1\n";

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

const D2RL::PluginContext* Context{};
std::uint8_t*              Base{};
Config                     Settings{};
void*                      RelayPage{};
std::int32_t               EffectiveTail[QualityCount]{};
std::atomic<std::uint64_t> Applied[QualityCount]{};

// ---------------------------------------------------------------------------
// Config parsing (small hand-rolled TOML subset, no external dependency)
// ---------------------------------------------------------------------------

auto Trim(std::string_view value) noexcept -> std::string_view {
    std::size_t first = 0;
    while (first < value.size()
            && (value[first] == ' ' || value[first] == '\t'
                || value[first] == '\r' || value[first] == '\n')) {
        ++first;
    }
    std::size_t last = value.size();
    while (last > first
            && (value[last - 1] == ' ' || value[last - 1] == '\t'
                || value[last - 1] == '\r' || value[last - 1] == '\n')) {
        --last;
    }
    return value.substr(first, last - first);
}

auto ParseBool(std::string_view value, bool& out) noexcept -> bool {
    if (value == "true" || value == "1") { out = true; return true; }
    if (value == "false" || value == "0") { out = false; return true; }
    return false;
}

auto ParseInteger(std::string_view value, std::int32_t& out) noexcept -> bool {
    if (value.empty()) return false;
    bool negative = false;
    std::size_t index = 0;
    if (value[0] == '+' || value[0] == '-') {
        negative = value[0] == '-';
        index = 1;
    }
    if (index >= value.size()) return false;
    std::int64_t accumulator = 0;
    for (; index < value.size(); ++index) {
        const char character = value[index];
        if (character == '_') continue;
        if (character < '0' || character > '9') return false;
        accumulator = accumulator * 10 + (character - '0');
        if (accumulator > 1'000'000) return false;
    }
    out = static_cast<std::int32_t>(negative ? -accumulator : accumulator);
    return true;
}

void ApplyConfigLine(std::string_view key, std::string_view value) noexcept {
    std::int32_t number = 0;
    if (key == "enabled") {
        ParseBool(value, Settings.enabled);
    } else if (key == "tail_percent") {
        if (ParseInteger(value, number)) Settings.tail = number;
    } else if (key == "tail_percent_unique") {
        if (ParseInteger(value, number)) Settings.perQuality[0] = number;
    } else if (key == "tail_percent_set") {
        if (ParseInteger(value, number)) Settings.perQuality[1] = number;
    } else if (key == "tail_percent_rare") {
        if (ParseInteger(value, number)) Settings.perQuality[2] = number;
    }
}

void ParseConfig(std::string_view text) noexcept {
    std::size_t cursor = 0;
    while (cursor <= text.size()) {
        const std::size_t breakAt = text.find('\n', cursor);
        const std::size_t end = breakAt == std::string_view::npos ? text.size() : breakAt;
        std::string_view line = Trim(text.substr(cursor, end - cursor));
        cursor = end + 1;
        if (breakAt == std::string_view::npos && line.empty()) break;

        const std::size_t comment = line.find('#');
        if (comment != std::string_view::npos) line = Trim(line.substr(0, comment));
        if (!line.empty() && line.front() != '[') {
            const std::size_t equals = line.find('=');
            if (equals != std::string_view::npos) {
                ApplyConfigLine(Trim(line.substr(0, equals)),
                                Trim(line.substr(equals + 1)));
            }
        }
        if (breakAt == std::string_view::npos) break;
    }
}

auto ReadConfiguration() noexcept -> bool {
    if (!Context->EnsureConfig(DefaultConfigToml)) {
        Context->LogWarn(
            "MagicFindSoftTail: config file could not be created; using defaults.");
    } else {
        std::string buffer(MaximumConfigBytes, '\0');
        if (Context->ReadConfig(buffer.data(),
                static_cast<std::uint32_t>(buffer.size()), nullptr)) {
            buffer.resize(std::strlen(buffer.c_str()));
            ParseConfig(buffer);
        } else {
            Context->LogWarn(
                "MagicFindSoftTail: config file could not be read; using defaults.");
        }
    }

    if (Settings.tail < 0) Settings.tail = 0;
    for (std::size_t i = 0; i < QualityCount; ++i) {
        const std::int32_t override = Settings.perQuality[i];
        EffectiveTail[i] = (override == Inherit) ? Settings.tail : override;
        if (EffectiveTail[i] < 0) EffectiveTail[i] = 0;
    }
    return true;
}

// ---------------------------------------------------------------------------
// The tail callback, reached from the relay
// ---------------------------------------------------------------------------
//
// rcx = the vanilla MagicFind (already 100 + dim*MF/(MF+dim))
// rdx = raw MF, guaranteed > 10 because the native gate only reaches the
//       diminishing block when MF + 100 > 110
// r8  = quality index

std::int32_t __fastcall MagicFindTail(std::int32_t vanilla, std::int32_t rawMagicFind,
                                      std::uint32_t quality) noexcept {
    if (quality >= QualityCount) return vanilla;

    std::int64_t value = vanilla;
    if (rawMagicFind > 0) {
        value += (static_cast<std::int64_t>(rawMagicFind)
                  * EffectiveTail[quality]) / 100;
    }

    // MagicFind is used as a divisor, so it must never reach zero or go
    // negative. Vanilla can only produce >= 100 here, but a hand-edited
    // config should not be able to break the roll.
    if (value < 1) value = 1;
    if (value > INT32_MAX) value = INT32_MAX;

    Applied[quality].fetch_add(1, std::memory_order_relaxed);
    return static_cast<std::int32_t>(value);
}

// ---------------------------------------------------------------------------
// Relay page
// ---------------------------------------------------------------------------

auto CanEncodeRel32(std::uintptr_t from, std::uintptr_t to) noexcept -> bool {
    const std::int64_t delta =
        static_cast<std::int64_t>(to) - static_cast<std::int64_t>(from) - 5;
    return delta >= INT32_MIN && delta <= INT32_MAX;
}

auto AllocateNear(void* hint, std::size_t size) noexcept -> void* {
    SYSTEM_INFO systemInfo{};
    GetSystemInfo(&systemInfo);
    const auto granularity =
        static_cast<std::uintptr_t>(systemInfo.dwAllocationGranularity);
    const auto aligned =
        reinterpret_cast<std::uintptr_t>(hint) & ~(granularity - 1U);
    for (std::uintptr_t delta = granularity;
            delta < 0x7000'0000ULL; delta += granularity) {
        const auto candidate = aligned + delta;
        if (!CanEncodeRel32(reinterpret_cast<std::uintptr_t>(hint), candidate)) break;
        if (auto* memory = VirtualAlloc(reinterpret_cast<void*>(candidate), size,
                MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)) {
            return memory;
        }
    }
    return nullptr;
}

// Builds the stub replacing `idiv ecx; lea ecx,[rax+0x64]`.
//
// Entry contract: ecx = MF + dim, edx:eax = the dividend set up by the native
// cdq, edi = raw MF. All three are proven by the bytes being replaced and the
// two instructions immediately above them.
//
// The replaced bytes contained no call, so the stub saves every volatile
// register, the flags and xmm0-5. Only ecx differs on exit.
auto BuildTailRelay(std::uint8_t* out, const void* callback,
        std::uint32_t quality, std::uintptr_t continuation) noexcept -> std::size_t {
    std::size_t index = 0;
    const auto emit = [&](std::initializer_list<std::uint8_t> bytes) {
        for (const auto byte : bytes) out[index++] = byte;
    };

    emit({0xF7, 0xF9});                             // idiv ecx      (stolen)
    emit({0x8D, 0x48, 0x64});                       // lea ecx,[rax+0x64] (stolen)

    emit({0x50});                                   // push rax
    emit({0x52});                                   // push rdx
    emit({0x41, 0x50});                             // push r8
    emit({0x41, 0x51});                             // push r9
    emit({0x41, 0x52});                             // push r10
    emit({0x41, 0x53});                             // push r11
    emit({0x9C});                                   // pushfq
    emit({0x55});                                   // push rbp
    emit({0x48, 0x89, 0xE5});                       // mov rbp, rsp
    emit({0x48, 0x83, 0xE4, 0xF0});                 // and rsp, -16
    emit({0x48, 0x81, 0xEC, 0x80, 0x00, 0x00, 0x00}); // sub rsp, 0x80

    for (std::uint8_t slot = 0; slot < 6; ++slot) {
        emit({0x0F, 0x29,
              static_cast<std::uint8_t>(0x44 + (slot << 3)),
              0x24,
              static_cast<std::uint8_t>(0x20 + (slot * 0x10))});
    }

    emit({0x89, 0xFA});                             // mov edx, edi   (arg2 = MF)
    emit({0x41, 0xB8});                             // mov r8d, imm32 (arg3)
    std::memcpy(out + index, &quality, sizeof(quality));
    index += sizeof(quality);
    emit({0x48, 0xB8});                             // mov rax, imm64
    const auto target = reinterpret_cast<std::uint64_t>(callback);
    std::memcpy(out + index, &target, sizeof(target));
    index += sizeof(target);
    emit({0xFF, 0xD0});                             // call rax
    emit({0x8B, 0xC8});                             // mov ecx, eax

    for (std::uint8_t slot = 0; slot < 6; ++slot) {
        emit({0x0F, 0x28,
              static_cast<std::uint8_t>(0x44 + (slot << 3)),
              0x24,
              static_cast<std::uint8_t>(0x20 + (slot * 0x10))});
    }

    emit({0x48, 0x89, 0xEC});                       // mov rsp, rbp
    emit({0x5D});                                   // pop rbp
    emit({0x9D});                                   // popfq
    emit({0x41, 0x5B});                             // pop r11
    emit({0x41, 0x5A});                             // pop r10
    emit({0x41, 0x59});                             // pop r9
    emit({0x41, 0x58});                             // pop r8
    emit({0x5A});                                   // pop rdx
    emit({0x58});                                   // pop rax
    emit({0xFF, 0x25, 0x00, 0x00, 0x00, 0x00});     // jmp qword ptr [rip+0]

    const auto resume = static_cast<std::uint64_t>(continuation);
    std::memcpy(out + index, &resume, sizeof(resume));
    index += sizeof(resume);
    return index;
}

// ---------------------------------------------------------------------------
// Verification and installation
// ---------------------------------------------------------------------------

auto VerifyNativeContract() noexcept -> bool {
    for (std::size_t i = 0; i < QualityCount; ++i) {
        if (!Context->CheckExpectedBytes(Sites[i].witnessRva,
                Witnesses[i].data(), WitnessSize)) {
            char message[224];
            std::snprintf(message, sizeof(message),
                "MagicFindSoftTail: the %s magic-find block at 0x%llX does not "
                "match build 92777, or is already owned by another plugin. "
                "Refusing to load.",
                Sites[i].name,
                static_cast<unsigned long long>(Sites[i].witnessRva));
            Context->LogError(message);
            return false;
        }
    }
    return true;
}

auto InstallHooks() noexcept -> bool {
    RelayPage = AllocateNear(Base + Sites[0].hookRva, RelayBytes);
    if (!RelayPage) {
        Context->LogError(
            "MagicFindSoftTail: no relay page was available within rel32 reach.");
        return false;
    }
    auto* relay = static_cast<std::uint8_t*>(RelayPage);
    const auto relayBase = reinterpret_cast<std::uintptr_t>(relay);
    const auto imageBase = reinterpret_cast<std::uintptr_t>(Base);

    std::size_t stub[QualityCount]{};
    std::size_t cursor = 0;
    for (std::size_t i = 0; i < QualityCount; ++i) {
        stub[i] = cursor;
        cursor += BuildTailRelay(relay + cursor,
            reinterpret_cast<const void*>(&MagicFindTail),
            static_cast<std::uint32_t>(i),
            imageBase + Sites[i].hookRva + HookSize);
        cursor = (cursor + 15) & ~static_cast<std::size_t>(15);
    }

    DWORD previousProtection = 0;
    if (!VirtualProtect(relay, RelayBytes, PAGE_EXECUTE_READ, &previousProtection)) {
        Context->LogError(
            "MagicFindSoftTail: relay page protection could not be finalized.");
        return false;
    }
    FlushInstructionCache(GetCurrentProcess(), relay, RelayBytes);

    for (std::size_t i = 0; i < QualityCount; ++i) {
        if (!CanEncodeRel32(imageBase + Sites[i].hookRva, relayBase + stub[i])) {
            Context->LogError(
                "MagicFindSoftTail: relay displacement validation failed.");
            return false;
        }
    }

    for (std::size_t i = 0; i < QualityCount; ++i) {
        if (!Context->PatchJmpRel32(
                Sites[i].hookRva,
                HookExpected.data(),
                HookSize,
                relayBase + stub[i] - imageBase,
                HookSize)) {
            char message[192];
            std::snprintf(message, sizeof(message),
                "MagicFindSoftTail: the %s block at 0x%llX could not be redirected.",
                Sites[i].name,
                static_cast<unsigned long long>(Sites[i].hookRva));
            Context->LogError(message);
            return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Console command
// ---------------------------------------------------------------------------

auto __cdecl StatusCommand(D2R::Game::Client*,
        const D2RL::ConsoleCommandContext* command, void*) noexcept
        -> D2RL::ConsoleCommandResult {
    if (!command || !command->plugin) return D2RL::ConsoleCommandResult::Failed;
    char message[352];
    std::snprintf(message, sizeof(message),
        "Magic Find Soft Tail: %s | tail unique %d%% set %d%% rare %d%% | "
        "rolls unique %llu set %llu rare %llu",
        Settings.enabled ? "on" : "off",
        EffectiveTail[0], EffectiveTail[1], EffectiveTail[2],
        static_cast<unsigned long long>(Applied[0].load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(Applied[1].load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(Applied[2].load(std::memory_order_relaxed)));
    command->plugin->WriteConsoleMessage(message);
    return D2RL::ConsoleCommandResult::Handled;
}

constexpr D2RL::PluginInfo Info{
    .infoSize = D2RL::PluginInfoSize,
    .apiVersion = D2RL_PLUGIN_API_VERSION,
    .id = "celestialrayone.magic-find-soft-tail",
    .name = "Magic Find Soft Tail",
    .version = "1.0.0",
    .author = "CelestialRayOne",
    .description =
        "Adds a configurable unbounded tail to the magic find diminishing "
        "returns curve for unique, set and rare items.",
    .flags = D2RL::PluginFlags::Server | D2RL::PluginFlags::NativeHooks,
};

}  // namespace

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept
        -> const D2RL::PluginInfo* {
    return &Info;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(
        const D2RL::PluginContext* context) noexcept -> bool {
    if (!D2RL::HasContext(context)) return false;
    Context = context;
    Base = reinterpret_cast<std::uint8_t*>(context->exeBase);

    if (!ReadConfiguration()) return false;

    if (!Settings.enabled) {
        Context->LogInfo("MagicFindSoftTail: disabled by configuration.");
        return true;
    }
    if (EffectiveTail[0] == 0 && EffectiveTail[1] == 0 && EffectiveTail[2] == 0) {
        Context->LogInfo(
            "MagicFindSoftTail: every tail is 0, which is vanilla behaviour; "
            "no hooks installed.");
        return true;
    }
    if (!VerifyNativeContract()) return false;
    if (!InstallHooks()) return false;

    char message[288];
    std::snprintf(message, sizeof(message),
        "MagicFindSoftTail: armed. tail unique %d%% (dim 250), set %d%% "
        "(dim 500), rare %d%% (dim 600). Magic, superior and normal are "
        "unaffected by design.",
        EffectiveTail[0], EffectiveTail[1], EffectiveTail[2]);
    Context->LogInfo(message);

    if (!Context->RegisterConsoleCommand("mftail", &StatusCommand,
            "Reports Magic Find Soft Tail status and counters.")) {
        Context->LogWarn(
            "MagicFindSoftTail: the status console command was refused.");
    }
    return true;
}

}  // namespace CelestialRayOne::MagicFindSoftTail
