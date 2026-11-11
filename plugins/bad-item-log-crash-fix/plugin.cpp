// Bad Item Log Fix
//
// Stops a crash while a character is being added to a game: when the game's
// saved-item checker throws out one of the HIRELING's items, it writes a log
// header that reads the hireling as if it were a player, and dies.
//
// Everything below was read out of the D2RLoader 1.3.1 process image for
// D2R 3.3 (md5 8af7a47c...) and is checked byte for byte at load. Nothing is
// installed when a check fails.
//
// ---------------------------------------------------------------------------
// Where the checker runs
// ---------------------------------------------------------------------------
//   ClientAddPlayerToGame 483B20 empties the game's item id list (game
//   +1AA70 = 0) and loads the character: 41DF40 -> character reader 420280.
//   The reader takes the player's items (421D20), the corpses (41FB20) and
//   then the hireling section 421240, which spawns the hireling and reads the
//   hireling's item list with the HIRELING as the owner:
//     4215B7  call 421D20 (game, hireling, data, version, ...)
//   Every item goes through the saved-item checker 4229C0 (game, owner,
//   item, version), which returns true for an item the game deletes:
//     - an item without an id ("Item has no unique id!")
//     - an id already seen in this load (sorted list at game +1AA68, 541FA0)
//     - four vanilla row checks (uniqueitems/setitems row 122, 242, 205)
//
// ---------------------------------------------------------------------------
// The crash
// ---------------------------------------------------------------------------
//   For a deleted item the checker writes "Deleting bad item" and a header
//   into Documents\Diablo II\GameLogs\D2TEMP<date>.txt:
//     "\nGame %6d %s\nPlayer: %s\n\n%s"   (1D18408, formatted at 423CF8)
//   It takes the player name like this:
//     423BE0  48 8B 45 E8           mov  rax, [rbp-18h]   ; owner
//     423BE4  48 85 C0 / 74 10      test / je 423BF9
//     423BE9  48 8B 40 10           mov  rax, [rax+10h]   ; unit data
//     423BED  48 85 C0 / 74 07      test / je 423BF9
//     423BF2  4C 8B A8 F0 00 00 00  mov  r13, [rax+0F0h]  ; client
//     ...
//     423CB3  4D 8D 45 2C           lea  r8, [r13+2Ch]    ; character name
//   There is no check that the owner is a player. For a hireling [unit+10h]
//   is its MonsterData, and +0F0h there holds eight spaces
//   (2020202020202020h). The name pointer becomes 202020202020204Ch and
//   strnlen (12DF060, from the %s handler 12DDCC4) faults on it.
//   The checker's own duplicate-item line does it right: a non-player owner
//   hands over to its owner (loop at 422B0E through 4A53C0) before the
//   client is read. 421D20 does the same for its account id line (4226F3).
//
// ---------------------------------------------------------------------------
// The change
// ---------------------------------------------------------------------------
//   The 25 bytes 423BE0..423BF9 become
//     48 8B 4D E8                 mov  rcx, [rbp-18h]   ; owner
//     E8 xx xx xx xx              call ResolveLogClient (relay page)
//     4C 8B E8                    mov  r13, rax         ; client or 0
//     0F 1F 84 00 00 00 00 00     nop
//     0F 1F 44 00 00              nop                   ; ends at 423BF9
//   ResolveLogClient hands a non-player owner to its owner player the way
//   the duplicate-item line does, then reads the client exactly as the
//   checker always did for a player ([unit+10h] -> [+0F0h]). A player owner
//   gives the same client as before, so nothing changes for the player's own
//   items. A null client leaves the name empty, as the game already does for
//   an owner without one (423CBB). While a character is loading, the game's
//   own duplicate-item line cannot find the hireling's player either (it logs
//   "unknown(Merc?)" for the same item), so in that case the name stays empty.
//   Registers: everything the checker keeps across 423BE0..423BF9 is
//   non-volatile (rbx, rsi, r12, r13, r15); rax, rcx, rdx and r8 are written
//   again before their next read (423BFD, 423C69, 423C1B, 423C82, 423CAC).
//   The call is made at the checker's own aligned rsp, like its other calls.
//   The decision to delete the item is untouched: the game deletes exactly
//   what it deleted before, only the log header is now safe.

#include <D2RLPlugin/api.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>

namespace {

constexpr char PluginIdText[] = "celestialrayone.bad-item-log-fix";

const D2RL::PluginContext* Context{};
std::uintptr_t             Base{};

// ---------------------------------------------------------------------------
// Native contract
// ---------------------------------------------------------------------------

constexpr std::uint64_t WindowRva       = 0x423BD8;
constexpr std::uint64_t BlockRva        = 0x423BE0;
constexpr std::uint64_t CallRva         = 0x423BE4;
constexpr std::size_t   BlockSize       = 25;
constexpr std::size_t   CallSize        = 5;
constexpr std::size_t   BlockOffset     = BlockRva - WindowRva;
constexpr std::uint64_t ConsumerRva     = 0x423CA9;
constexpr std::uint64_t HeaderFormatRva = 0x1D18408;
constexpr std::uint64_t UnitTypeRva     = 0x34B9D0;
constexpr std::uint64_t MinionOwnerRva  = 0x4A53C0;

// 423BD8..423BFD: the extracted log text into rsi, the 25-byte block, then
// test ebx,ebx / je 423C63.
constexpr std::uint8_t Window[]{
    0x48,0x8B,0xF0, 0xEB,0x03, 0x49,0x8B,0xF5,
    0x48,0x8B,0x45,0xE8, 0x48,0x85,0xC0, 0x74,0x10,
    0x48,0x8B,0x40,0x10, 0x48,0x85,0xC0, 0x74,0x07,
    0x4C,0x8B,0xA8,0xF0,0x00,0x00,0x00,
    0x85,0xDB, 0x74,0x66 };
static_assert(sizeof(Window) == 37);
static_assert(BlockOffset == 8 && BlockOffset + BlockSize == 33);

// 423CA9..423CF5: r13 turned into the name argument, then the header format.
constexpr std::uint8_t ConsumerWindow[]{
    0x48,0x85,0xF6, 0x48,0x8D,0x15,0x35,0x47,0x8F,0x01, 0x4D,0x8D,0x45,0x2C, 0x48,0x0F,0x45,0xD6,
    0x4D,0x85,0xED, 0x75,0x07, 0x4C,0x8D,0x05,0x99,0x60,0x8A,0x01, 0x45,0x8B,0x8F,0x70,0x01,0x00,0x00,
    0x49,0x8D,0x47,0x0A, 0x48,0x89,0x54,0x24,0x30, 0x48,0x8D,0x8D,0x10,0x08,0x00,0x00,
    0x4C,0x89,0x44,0x24,0x28, 0x4D,0x2B,0xF4, 0x49,0x03,0xCC, 0x48,0x89,0x44,0x24,0x20,
    0x4C,0x8D,0x05,0x13,0x47,0x8F,0x01 };

// 1D18408: the header the checker formats (terminator included).
constexpr char HeaderFormat[] = "\nGame %6d %s\nPlayer: %s\n\n%s";

// 34B9D0: UNITS_GetUnitType, returns dword [unit+0]; asserts on null.
constexpr std::uint8_t UnitTypeWitness[]{
    0x48,0x83,0xEC,0x28, 0x48,0x85,0xC9, 0x75,0x1D, 0x88,0x4C,0x24,0x30, 0x48,0x8D,0x4C,0x24,0x30,
    0xE8,0x39,0x9E,0xFF,0xFF, 0x84,0xC0, 0x74,0x01, 0xCC, 0xB8,0x06,0x00,0x00,0x00,
    0x48,0x83,0xC4,0x28, 0xC3, 0x8B,0x01, 0x48,0x83,0xC4,0x28, 0xC3 };

// 4A53C0..4A547D: D2GAME_GetMinionOwner, whole body. Null unless the unit is a
// monster; MonsterData [unit+10h] -> AI control [+30h] -> owner. Asserts when
// either pointer is missing, so ResolveLogClient checks both first.
constexpr std::uint8_t MinionOwnerWitness[]{
    0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0xD9,0xE8,0x02,0x66,0xEA,0xFF,0x83,0xF8,
    0x01,0x0F,0x85,0x9E,0x00,0x00,0x00,0x48,0x85,0xDB,0x74,0x0D,0x48,0x8B,0xCB,0xE8,
    0xEC,0x65,0xEA,0xFF,0x83,0xF8,0x01,0x74,0x19,0x48,0x8D,0x4C,0x24,0x38,0xC6,0x44,
    0x24,0x38,0x00,0xE8,0x48,0xB6,0xF5,0xFF,0x84,0xC0,0x74,0x01,0xCC,0x48,0x85,0xDB,
    0x74,0x73,0x48,0x8B,0xCB,0xE8,0xC6,0x65,0xEA,0xFF,0x83,0xF8,0x01,0x75,0x66,0x48,
    0x83,0x7B,0x10,0x00,0x75,0x14,0x48,0x8D,0x4C,0x24,0x38,0xC6,0x44,0x24,0x38,0x00,
    0xE8,0xBB,0xB5,0xF5,0xFF,0x84,0xC0,0x74,0x01,0xCC,0x48,0x8B,0x43,0x10,0x48,0x85,
    0xC0,0x74,0x42,0x48,0x83,0x78,0x30,0x00,0x75,0x14,0x48,0x8D,0x4C,0x24,0x38,0xC6,
    0x44,0x24,0x38,0x00,0xE8,0x67,0xB5,0xF5,0xFF,0x84,0xC0,0x74,0x01,0xCC,0x48,0x8B,
    0x43,0x10,0x48,0x8B,0x50,0x30,0x48,0x85,0xD2,0x74,0x1A,0x48,0x8B,0x4A,0x60,0x48,
    0x85,0xC9,0x74,0x11,0x44,0x8B,0x42,0x68,0x8B,0x52,0x6C,0x48,0x83,0xC4,0x20,0x5B,
    0xE9,0x0B,0xAA,0xFE,0xFF,0x33,0xC0,0x48,0x83,0xC4,0x20,0x5B,0xC3 };

using UnitTypeFn    = std::int32_t (__fastcall*)(void* unit);
using MinionOwnerFn = void* (__fastcall*)(void* unit);
UnitTypeFn    UnitType{};
MinionOwnerFn MinionOwner{};

constexpr std::int32_t UnitPlayer       = 0;
constexpr std::int32_t UnitMonster      = 1;
constexpr std::size_t  UnitDataOffset   = 0x10;   // player or monster data (423BE9, 4A5434)
constexpr std::size_t  AiControlOffset  = 0x30;   // MonsterData -> AI control (4A5452)
constexpr std::size_t  ClientOffset     = 0xF0;   // PlayerData -> client (423BF2, 48FDFF)
constexpr std::size_t  ClientNameOffset = 0x2C;   // client -> character name (423CB3)
constexpr int          MaxOwnerHops     = 8;

// ---------------------------------------------------------------------------
// State and config
// ---------------------------------------------------------------------------

enum class PluginState { NotLoaded, DisabledByConfig, UnsupportedBuild, InstallFailed, Active };
PluginState State{ PluginState::NotLoaded };

std::atomic<std::uint64_t> HeadersBuilt{};
std::atomic<std::uint64_t> HirelingHeaders{};
std::atomic<std::uint64_t> UnresolvedOwners{};

constexpr char DefaultConfigToml[] =
    "# Bad Item Log Fix\n"
    "#\n"
    "# While a character is loaded into a game, the game checks every saved item\n"
    "# and deletes the ones it considers bad: an item with no id, an item whose id\n"
    "# was already loaded (a duplicate), or one of four hard-coded vanilla checks\n"
    "# on uniqueitems.txt / setitems.txt rows 122, 242 and 205.\n"
    "#\n"
    "# For every deleted item it writes a line into\n"
    "#   Documents\\Diablo II\\GameLogs\\D2TEMP<date>.txt\n"
    "# with the item and the player's name. When the item belonged to the\n"
    "# HIRELING, the game read the hireling as if it were a player and crashed\n"
    "# while writing that line. This plugin makes it look up the hireling's\n"
    "# player instead, or leave the name empty when the game cannot find it\n"
    "# (normal while a character is loading), so the line is written and the\n"
    "# character loads.\n"
    "#\n"
    "# What it does not change: which items the game deletes. A deleted item is\n"
    "# still deleted; this only stops the crash. The plugin log gets one line\n"
    "# for every hireling item the game deletes.\n"
    "#\n"
    "# Console command: baditemlogfix (install state and counters)\n"
    "\n"
    "[bad_item_log_fix]\n"
    "\n"
    "# Master switch.\n"
    "enabled = true\n";

constexpr std::size_t MaximumConfigBytes = 16 * 1024;
bool Enabled{ true };

auto Trim(std::string_view value) noexcept -> std::string_view {
    std::size_t first = 0;
    while (first < value.size() && (value[first] == ' ' || value[first] == '\t'
            || value[first] == '\r' || value[first] == '\n')) {
        ++first;
    }
    std::size_t last = value.size();
    while (last > first && (value[last - 1] == ' ' || value[last - 1] == '\t'
            || value[last - 1] == '\r' || value[last - 1] == '\n')) {
        --last;
    }
    return value.substr(first, last - first);
}

void ParseConfig(std::string_view text) {
    std::string_view section;
    while (!text.empty()) {
        const std::size_t end = text.find('\n');
        std::string_view line = text.substr(0, end);
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
        if (const std::size_t hash = line.find('#'); hash != std::string_view::npos) line = line.substr(0, hash);
        line = Trim(line);
        if (line.empty()) continue;
        if (line.front() == '[') {
            if (line.back() == ']') section = Trim(line.substr(1, line.size() - 2));
            continue;
        }
        const std::size_t equals = line.find('=');
        if (equals == std::string_view::npos || section != "bad_item_log_fix") continue;
        const std::string_view key   = Trim(line.substr(0, equals));
        const std::string_view value = Trim(line.substr(equals + 1));
        if (key == "enabled") {
            if (value == "true") Enabled = true;
            else if (value == "false") Enabled = false;
            else Context->LogWarn("BadItemLogFix: enabled: expected true or false, default kept.");
        }
    }
}

void ReadConfiguration() {
    if (!Context->EnsureConfig(DefaultConfigToml)) {
        Context->LogWarn("BadItemLogFix: the config file could not be created; using defaults.");
        return;
    }
    std::string buffer(MaximumConfigBytes, '\0');
    if (!Context->ReadConfig(buffer.data(), static_cast<std::uint32_t>(buffer.size()), nullptr)) {
        Context->LogWarn("BadItemLogFix: the config file could not be read; using defaults.");
        return;
    }
    buffer.resize(std::strlen(buffer.c_str()));
    ParseConfig(buffer);
}

// ---------------------------------------------------------------------------
// Owner resolution
// ---------------------------------------------------------------------------

auto ReadPointer(const void* base, std::size_t offset) noexcept -> void* {
    void* value = nullptr;
    std::memcpy(&value, static_cast<const std::uint8_t*>(base) + offset, sizeof(value));
    return value;
}

void LogHirelingItem(void* client) noexcept {
    char name[33]{};
    if (client != nullptr) {
        const char* text = static_cast<const char*>(client) + ClientNameOffset;
        const std::size_t length = strnlen(text, sizeof(name) - 1);
        std::memcpy(name, text, length);
    }
    char line[256];
    if (name[0] != '\0') {
        std::snprintf(line, sizeof(line),
            "BadItemLogFix: the game deleted an item belonging to %s's hireling while loading the character; "
            "its GameLogs\\D2TEMP line was written with the player's name.", name);
    } else {
        std::snprintf(line, sizeof(line),
            "BadItemLogFix: the game deleted an item belonging to a hireling while loading the character; "
            "its player has no client, so its GameLogs\\D2TEMP line has no name.");
    }
    Context->LogInfo(line);
}

void LogUnresolvedOwner() noexcept {
    UnresolvedOwners.fetch_add(1, std::memory_order_relaxed);
    Context->LogWarn("BadItemLogFix: the game deleted an item held by a hireling or other non-player whose player "
        "could not be found (normal while a character is loading); its GameLogs\\D2TEMP line was written "
        "without a player name.");
}

// Replaces 423BE0..423BF9. rcx = the checker's owner. Returns the client the
// header takes the name from, or null for an empty name.
void* __fastcall ResolveLogClient(void* owner) noexcept {
    HeadersBuilt.fetch_add(1, std::memory_order_relaxed);
    if (owner == nullptr) return nullptr;   // 423BE7: no owner, no name

    // Same hand-over as the duplicate-item line (422B0E): a non-player owner
    // passes to its owner until a player is reached.
    void* unit = owner;
    int   hops = 0;
    for (std::int32_t type = UnitType(unit); type != UnitPlayer; type = UnitType(unit)) {
        void* data = type == UnitMonster && hops < MaxOwnerHops ? ReadPointer(unit, UnitDataOffset) : nullptr;
        if (data == nullptr || ReadPointer(data, AiControlOffset) == nullptr) {
            LogUnresolvedOwner();
            return nullptr;
        }
        unit = MinionOwner(unit);
        ++hops;
        if (unit == nullptr) {
            LogUnresolvedOwner();
            return nullptr;
        }
    }

    // The checker's own player read (423BE9..423BF2).
    void* playerData = ReadPointer(unit, UnitDataOffset);
    void* client     = playerData != nullptr ? ReadPointer(playerData, ClientOffset) : nullptr;
    if (hops > 0) {
        HirelingHeaders.fetch_add(1, std::memory_order_relaxed);
        LogHirelingItem(client);
    }
    return client;
}

// ---------------------------------------------------------------------------
// Relay page and install
// ---------------------------------------------------------------------------

constexpr std::size_t RelayPageBytes = 4'096;
void* RelayPage{};
bool  Patched{};

auto CanEncodeRel32(std::uintptr_t from, std::uintptr_t to) noexcept -> bool {
    const std::int64_t delta = static_cast<std::int64_t>(to) - static_cast<std::int64_t>(from + CallSize);
    return delta >= std::numeric_limits<std::int32_t>::min() && delta <= std::numeric_limits<std::int32_t>::max();
}

auto AllocateNear(std::uintptr_t site) noexcept -> void* {
    SYSTEM_INFO systemInfo{};
    GetSystemInfo(&systemInfo);
    const auto granularity = static_cast<std::uintptr_t>(systemInfo.dwAllocationGranularity);
    const auto aligned     = site & ~(granularity - 1U);
    for (std::uintptr_t delta = granularity; delta < 0x7000'0000ULL; delta += granularity) {
        const auto candidate = aligned + delta;
        if (!CanEncodeRel32(site, candidate + RelayPageBytes)) break;
        if (void* memory = VirtualAlloc(reinterpret_cast<void*>(candidate), RelayPageBytes,
                MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)) {
            return memory;
        }
    }
    return nullptr;
}

// jmp qword [rip+0] / dq ResolveLogClient
auto BuildRelayPage() -> bool {
    RelayPage = AllocateNear(Base + CallRva);
    if (RelayPage == nullptr) return false;
    auto* page = static_cast<std::uint8_t*>(RelayPage);
    std::memset(page, 0xCC, RelayPageBytes);
    const std::uint8_t jump[]{ 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00 };
    std::memcpy(page, jump, sizeof(jump));
    const auto target = reinterpret_cast<std::uint64_t>(&ResolveLogClient);
    std::memcpy(page + sizeof(jump), &target, sizeof(target));
    DWORD previous = 0;
    const bool sealed = VirtualProtect(page, RelayPageBytes, PAGE_EXECUTE_READ, &previous) != FALSE;
    FlushInstructionCache(GetCurrentProcess(), page, RelayPageBytes);
    return sealed;
}

// mov rcx,[rbp-18h] / call relay / mov r13,rax / 8-byte nop / 5-byte nop
constexpr std::uint8_t BlockHead[]{ 0x48, 0x8B, 0x4D, 0xE8 };
constexpr std::uint8_t BlockTail[]{
    0x4C, 0x8B, 0xE8,
    0x0F, 0x1F, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x0F, 0x1F, 0x44, 0x00, 0x00 };
static_assert(sizeof(BlockHead) + CallSize + sizeof(BlockTail) == BlockSize);
static_assert(BlockRva + sizeof(BlockHead) == CallRva);

void EncodeBlock(std::uint8_t (&out)[BlockSize]) noexcept {
    const auto rel = static_cast<std::int32_t>(
        static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(RelayPage))
        - static_cast<std::int64_t>(Base + CallRva + CallSize));
    std::memcpy(out, BlockHead, sizeof(BlockHead));
    out[sizeof(BlockHead)] = 0xE8;
    std::memcpy(out + sizeof(BlockHead) + 1, &rel, sizeof(rel));
    std::memcpy(out + sizeof(BlockHead) + CallSize, BlockTail, sizeof(BlockTail));
}

void RemovePatch() {
    if (!Patched) return;
    std::uint8_t current[BlockSize]{};
    EncodeBlock(current);
    if (Context->PatchBytes(BlockRva, current, BlockSize, Window + BlockOffset, BlockSize)) Patched = false;
    // The relay page is kept: a thread may be inside it right now.
}

auto Verify(std::uint64_t rva, const void* bytes, std::size_t size, const char* what) -> bool {
    if (Context->CheckExpectedBytes(rva, bytes, static_cast<std::uint32_t>(size))) return true;
    D2RL::LogErrorF(Context, "BadItemLogFix: %s at RVA 0x%llX does not match D2RLoader 1.3.1 (wrong game or "
        "loader build).", what, static_cast<unsigned long long>(rva));
    return false;
}

auto Install() -> bool {
    if (!Verify(WindowRva, Window, sizeof(Window), "the checker's log header block")
        || !Verify(ConsumerRva, ConsumerWindow, sizeof(ConsumerWindow), "the checker's header arguments")
        || !Verify(HeaderFormatRva, HeaderFormat, sizeof(HeaderFormat), "the header format string")
        || !Verify(UnitTypeRva, UnitTypeWitness, sizeof(UnitTypeWitness), "UNITS_GetUnitType")
        || !Verify(MinionOwnerRva, MinionOwnerWitness, sizeof(MinionOwnerWitness), "D2GAME_GetMinionOwner")) {
        State = PluginState::UnsupportedBuild;
        return false;
    }
    UnitType    = reinterpret_cast<UnitTypeFn>(Base + UnitTypeRva);
    MinionOwner = reinterpret_cast<MinionOwnerFn>(Base + MinionOwnerRva);
    if (!BuildRelayPage()) {
        Context->LogError("BadItemLogFix: no relay page could be placed within reach of the game code.");
        State = PluginState::InstallFailed;
        return false;
    }
    std::uint8_t block[BlockSize]{};
    EncodeBlock(block);
    Patched = Context->PatchBytes(BlockRva, Window + BlockOffset, BlockSize, block, BlockSize);
    if (!Patched) {
        Context->LogError("BadItemLogFix: the log header block could not be patched.");
        State = PluginState::InstallFailed;
        return false;
    }
    State = PluginState::Active;
    return true;
}

auto StateName(PluginState state) noexcept -> const char* {
    switch (state) {
    case PluginState::NotLoaded:        return "not loaded";
    case PluginState::DisabledByConfig: return "disabled by config";
    case PluginState::UnsupportedBuild: return "unsupported build, nothing installed";
    case PluginState::InstallFailed:    return "install failed, nothing active";
    case PluginState::Active:           return "active";
    }
    return "?";
}

auto __cdecl StatusCommand(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command, void*) noexcept
        -> D2RL::ConsoleCommandResult {
    char line[256];
    std::snprintf(line, sizeof(line),
        "Bad Item Log Fix: %s. Deleted-item headers %llu, hireling items %llu, owner without player %llu.",
        StateName(State),
        static_cast<unsigned long long>(HeadersBuilt.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(HirelingHeaders.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(UnresolvedOwners.load(std::memory_order_relaxed)));
    command->plugin->WriteConsoleMessage(line);
    return D2RL::ConsoleCommandResult::Handled;
}

constexpr D2RL::PluginInfo PluginInfoData{
    .infoSize    = D2RL::PluginInfoSize,
    .abiVersion  = D2RL_PLUGIN_ABI_VERSION,
    .id          = PluginIdText,
    .name        = "Bad Item Log Fix",
    .version     = "1.0.0",
    .author      = "CelestialRayOne",
    .description = "Stops the crash when the game deletes a bad item from a hireling while loading a character.",
    .flags       = D2RL::PluginFlags::Server | D2RL::PluginFlags::NativeHooks,
};
static_assert(D2RL::HasValidPluginRole(PluginInfoData.flags), "Exactly one execution role must be set.");
static_assert(D2RL::HasFlag(PluginInfoData.flags, D2RL::PluginFlags::NativeHooks),
    "The relay page is native code the game calls into.");

} // namespace

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* {
    return &PluginInfoData;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
    Context = context;
    if (Context == nullptr || Context->exeBase == 0) return false;
    Base = Context->exeBase;

    if (!Context->RegisterConsoleCommand("baditemlogfix", &StatusCommand,
            "Show Bad Item Log Fix install state and counters.")) {
        Context->LogWarn("BadItemLogFix: the console command could not be registered.");
    }
    ReadConfiguration();
    if (!Enabled) {
        State = PluginState::DisabledByConfig;
        Context->LogInfo("BadItemLogFix: disabled by config.");
        return true;
    }
    if (!Install()) {
        Context->LogError("BadItemLogFix: nothing was installed.");
        return true;
    }
    Context->LogInfo("BadItemLogFix: active; deleted hireling items are logged with their player's name.");
    return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
    if (Context != nullptr) RemovePatch();
}
