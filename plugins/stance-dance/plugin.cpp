// Stance Dance
//
// "Stance Dance" in the Controls menu, in its own "Ascendancies" section, with
// no default key. While the ascendancy gives the stance stat (itemstatcost row
// 681 by default), each press moves the character's stat 1 -> 2 -> 3 -> 1. The
// rules are in stance_rules.h.
//
// ---------------------------------------------------------------------------
// The stat
// ---------------------------------------------------------------------------
//   The ascendancy item gives the stat 1. The plugin owns the character's own
//   value of the stat (its base value, apart from items and states) and keeps
//   it at 0, 1 or 2, so the total the game reads is 1, 2 or 3. It writes the
//   host's copy of the character; for rows with Saved = 1 the game sends the
//   change on to that player's client.
//
// ---------------------------------------------------------------------------
// From the key to the host
// ---------------------------------------------------------------------------
//   The Controls action queues a UI task, which reads the local player's unit
//   id and sends it to the host over the plugin's network channel. Single
//   player and a TCP/IP host connect to their own host inside the process; a
//   TCP/IP client connects over the network. A press made before the channel
//   is up waits (up to 5 seconds) and is sent when it connects. Holding the
//   key is one press: a press counts only after the key was released.
//
// ---------------------------------------------------------------------------
// The host
// ---------------------------------------------------------------------------
//   A game-thread task runs once per game update. It re-arms itself through a
//   UI task, so it never queues itself on the queue that is running it. Each
//   run:
//     1. lists every player: the server's client registry, each client's
//        player resolved through the game's own server unit lookup
//     2. applies the key presses that arrived, each to the player whose unit
//        id it carries, once D2RLoader confirms the sender is a player of the
//        game
//     3. keeps every player's part legal: removed without the ascendancy,
//        otherwise within 0 .. 3 - ascendancy
//
// ---------------------------------------------------------------------------
// Native pieces, D2RLoader 1.3.1 process image, all checked byte for byte at
// load. Nothing is patched or hooked.
// ---------------------------------------------------------------------------
//   2F5020  stat read (unit, stat, layer), loader thunk into D2RCore
//   2F48C0  own value read (unit, stat, layer), loader thunk into D2RCore
//   2F7D10  own value write (unit, stat, value, layer), loader thunk into
//           D2RCore
//   34B9D0  unit type ([unit+0])      34A330  unit id ([unit+8])
//   8B2D0   local player index        9A480   local player from that index
//   48FE80  server unit lookup (game, type, id): the game's hash buckets for
//           that type, chained through unit+158h
//   The server's client registry: 256 buckets at 2AAECA0, ready flag 2AAF4A0
//     485290  registry reset: zeroes the 256 buckets, then sets the flag
//     485320  client lookup by id: the id at +0, the next client at +630h
//     484CDB  in the client's player lookup: unit type +26Ch, unit id
//             +270h, game +2B0h, then 48FE80
//   Clients are added (484150) and removed (485AB0) only together with their
//   game, on the game thread, so the registry is read only from game-thread
//   tasks.

#include <D2RLPlugin/api.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <string_view>

#include "stance_rules.h"

namespace {

constexpr char PluginIdText[]  = "celestialrayone.stance-dance";
constexpr char PluginVersion[] = "1.0.0";

const D2RL::PluginContext* Context{};
std::uintptr_t             Base{};

// ---------------------------------------------------------------------------
// Addresses (RVAs into the D2RLoader 1.3.1 process image)
// ---------------------------------------------------------------------------

constexpr std::uint64_t ReadStatRva         = 0x2F5020;
constexpr std::uint64_t ReadOwnStatRva      = 0x2F48C0;
constexpr std::uint64_t WriteOwnStatRva     = 0x2F7D10;
constexpr std::uint64_t UnitTypeRva         = 0x34B9D0;
constexpr std::uint64_t UnitIdRva           = 0x34A330;
constexpr std::uint64_t LocalPlayerIndexRva = 0x08B2D0;
constexpr std::uint64_t PlayerFromIndexRva  = 0x09A480;
constexpr std::uint64_t ServerUnitRva       = 0x48FE80;
constexpr std::uint64_t RegistryResetRva    = 0x485290;
constexpr std::uint64_t ClientLookupRva     = 0x485320;
constexpr std::uint64_t ClientPlayerRva     = 0x484CDB;

// Data. Both addresses are proven by the instructions checked below: the lea
// and the mov inside RegistryResetBytes, the lea inside ClientLookupBytes.
constexpr std::uint64_t RegistryRva      = 0x2AAECA0;
constexpr std::uint64_t RegistryReadyRva = 0x2AAF4A0;
constexpr std::size_t   RegistryBuckets  = 256;

constexpr std::size_t ClientNextOffset     = 0x630;
constexpr std::size_t ClientUnitTypeOffset = 0x26C;
constexpr std::size_t ClientUnitIdOffset   = 0x270;
constexpr std::size_t ClientGameOffset     = 0x2B0;

constexpr std::uint32_t UnitTypePlayer = 0;

// Compiled itemstatcost row: flags dword at +4 (Saved = bit 11), CSvBits byte
// at +0Ah (the game's own character stat writer reads it there, 5351DD).
constexpr std::size_t   IscFlagsOffset   = 0x04;
constexpr std::uint32_t IscFlagSaved     = 0x800;
constexpr std::size_t   IscCsvBitsOffset = 0x0A;

// ---------------------------------------------------------------------------
// Expected bytes
// ---------------------------------------------------------------------------

// Loader thunks: jmp [rip+slot], then four nops. The slot offset changes with
// the loader version, so only the fixed parts are checked.
constexpr std::uint8_t ThunkHead[]{ 0xFF,0x25 };
constexpr std::uint8_t ThunkTail[]{ 0x90,0x90,0x90,0x90 };

// 34B9D0, the whole function: null -> 6, else [rcx].
constexpr std::uint8_t UnitTypeBytes[]{
    0x48,0x83,0xEC,0x28,0x48,0x85,0xC9,0x75,0x1D,0x88,0x4C,0x24,0x30,0x48,0x8D,0x4C,
    0x24,0x30,0xE8,0x39,0x9E,0xFF,0xFF,0x84,0xC0,0x74,0x01,0xCC,0xB8,0x06,0x00,0x00,
    0x00,0x48,0x83,0xC4,0x28,0xC3,0x8B,0x01,0x48,0x83,0xC4,0x28,0xC3 };
// 34A330, the whole function: null -> -1, else [rcx+8].
constexpr std::uint8_t UnitIdBytes[]{
    0x48,0x83,0xEC,0x28,0x48,0x85,0xC9,0x75,0x1D,0x88,0x4C,0x24,0x30,0x48,0x8D,0x4C,
    0x24,0x30,0xE8,0x39,0xCA,0xFF,0xFF,0x84,0xC0,0x74,0x01,0xCC,0xB8,0xFF,0xFF,0xFF,
    0xFF,0x48,0x83,0xC4,0x28,0xC3,0x8B,0x41,0x08,0x48,0x83,0xC4,0x28,0xC3 };
// 8B2D0: mov eax, [local player index]; ret.
constexpr std::uint8_t LocalPlayerIndexBytes[]{ 0x8B,0x05,0x2E,0x84,0x99,0x02,0xC3 };
// 9A480 (index): an index of 8 or more gives no player.
constexpr std::uint8_t PlayerFromIndexBytes[]{
    0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,0x83,0xF9,0x08 };
// 48FE80 (game, type, id), prologue.
constexpr std::uint8_t ServerUnitBytes[]{
    0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xEC,0x20,0x41,
    0x8B,0xD8,0x8B,0xF2,0x48,0x8B,0xF9,0x48,0x85,0xC9 };
// 485290, the whole function: lea rax, [2AAECA0]; 32 rounds of 8 qword
// stores; mov dword [2AAF4A0], 1; ret.
constexpr std::uint8_t RegistryResetBytes[]{
    0x33,0xD2,0x48,0x8D,0x05,0x07,0x9A,0x62,0x02,0x8D,0x4A,0x20,0x0F,0x1F,0x40,0x00,
    0x48,0x89,0x10,0x48,0x89,0x50,0x08,0x48,0x89,0x50,0x10,0x48,0x8D,0x40,0x40,0x48,
    0x89,0x50,0xD8,0x48,0x89,0x50,0xE0,0x48,0x89,0x50,0xE8,0x48,0x89,0x50,0xF0,0x48,
    0x89,0x50,0xF8,0x48,0x83,0xE9,0x01,0x75,0xD7,0xC7,0x05,0xCD,0xA1,0x62,0x02,0x01,
    0x00,0x00,0x00,0xC3 };
// 485320 (client id), the whole function: lea rdx, [2AAECA0]; bucket by the
// id's low byte; cmp [client], id; next client at [client+630h].
constexpr std::uint8_t ClientLookupBytes[]{
    0x0F,0xB6,0xC1,0x48,0x8D,0x15,0x76,0x99,0x62,0x02,0x48,0x8B,0x04,0xC2,0x48,0x85,
    0xC0,0x74,0x10,0x39,0x08,0x74,0x0E,0x48,0x8B,0x80,0x30,0x06,0x00,0x00,0x48,0x85,
    0xC0,0x75,0xF0,0x33,0xC0,0xC3 };
// 484CDB: mov r8d, [rbx+270h]; mov edx, [rbx+26Ch]; mov rcx, [rbx+2B0h];
// call 48FE80.
constexpr std::uint8_t ClientPlayerBytes[]{
    0x44,0x8B,0x83,0x70,0x02,0x00,0x00,0x8B,0x93,0x6C,0x02,0x00,0x00,0x48,0x8B,0x8B,
    0xB0,0x02,0x00,0x00,0xE8,0x8C,0xB1,0x00,0x00 };

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

const std::array<Witness, 14> Witnesses{
    W(ReadStatRva, ThunkHead, "stat read"),
    W(ReadStatRva + 6, ThunkTail, "stat read"),
    W(ReadOwnStatRva, ThunkHead, "own stat read"),
    W(ReadOwnStatRva + 6, ThunkTail, "own stat read"),
    W(WriteOwnStatRva, ThunkHead, "own stat write"),
    W(WriteOwnStatRva + 6, ThunkTail, "own stat write"),
    W(UnitTypeRva, UnitTypeBytes, "unit type"),
    W(UnitIdRva, UnitIdBytes, "unit id"),
    W(LocalPlayerIndexRva, LocalPlayerIndexBytes, "local player index"),
    W(PlayerFromIndexRva, PlayerFromIndexBytes, "local player"),
    W(ServerUnitRva, ServerUnitBytes, "server unit lookup"),
    W(RegistryResetRva, RegistryResetBytes, "client registry reset"),
    W(ClientLookupRva, ClientLookupBytes, "client lookup"),
    W(ClientPlayerRva, ClientPlayerBytes, "client player fields"),
};

// ---------------------------------------------------------------------------
// Native functions
// ---------------------------------------------------------------------------

using ReadStatFn    = std::int32_t(__fastcall*)(void* unit, std::int32_t stat, std::int32_t layer);
using WriteStatFn   = void(__fastcall*)(void* unit, std::int32_t stat, std::int32_t value, std::int32_t layer);
using UnitTypeFn    = std::uint32_t(__fastcall*)(void* unit);
using UnitIdFn      = std::uint32_t(__fastcall*)(void* unit);
using PlayerIndexFn = std::int32_t(__fastcall*)();
using PlayerFromFn  = void*(__fastcall*)(std::int32_t index);
using ServerUnitFn  = void*(__fastcall*)(void* game, std::uint32_t type, std::uint32_t id);

ReadStatFn    ReadStat{};
ReadStatFn    ReadOwnStat{};
WriteStatFn   WriteOwnStat{};
UnitTypeFn    UnitType{};
UnitIdFn      UnitId{};
PlayerIndexFn LocalPlayerIndex{};
PlayerFromFn  PlayerFromIndex{};
ServerUnitFn  ServerUnit{};

template <typename T>
auto At(std::uint64_t rva) noexcept -> T {
    return reinterpret_cast<T>(Base + rva);
}

template <typename T>
auto Read(const void* base, std::size_t offset) noexcept -> T {
    T value{};
    std::memcpy(&value, static_cast<const std::uint8_t*>(base) + offset, sizeof(T));
    return value;
}

// ---------------------------------------------------------------------------
// Services and state
// ---------------------------------------------------------------------------

const D2RL::InputService*     InputSvc{};
const D2RL::NetworkService*   NetworkSvc{};
const D2RL::ThreadService*    ThreadSvc{};
const D2RL::LifecycleService* LifecycleSvc{};
const D2RL::DataTableService* TableSvc{};

enum class PluginState { NotLoaded, DisabledByConfig, UnsupportedBuild, ServicesMissing, InstallFailed, Active };
PluginState       State{ PluginState::NotLoaded };
std::atomic<bool> Active{};

D2RL::Network::ChannelHandle           Channel{ D2RL::Network::InvalidChannelHandle };
std::atomic<D2RL::Input::ActionHandle> Action{ D2RL::Input::InvalidHandle };

constexpr std::uint16_t ChannelLocalId     = 1;
constexpr std::uint64_t CompatibilityToken = 0x5354414E43453031ULL;  // "STANCE01"
constexpr std::uint16_t StepMessageId      = 1;

// One key press: the unit id of the player who pressed it.
struct StepMessage {
    std::uint32_t playerId;
};

// Counters, shown by the console command.
std::atomic<std::uint64_t> KeyPresses{};
std::atomic<std::uint64_t> RepeatsIgnored{};
std::atomic<std::uint64_t> PressesSent{};
std::atomic<std::uint64_t> PressesDropped{};
std::atomic<std::uint64_t> RequestsReceived{};
std::atomic<std::uint64_t> RequestsRefused{};
std::atomic<std::uint64_t> RequestsUnknown{};
std::atomic<std::uint64_t> StanceChanges{};
std::atomic<std::uint64_t> PressesLocked{};
std::atomic<std::uint64_t> PartsRemoved{};
std::atomic<std::uint64_t> PartsCorrected{};

std::atomic<std::uint32_t> ConnectionNow{ static_cast<std::uint32_t>(D2RL::Network::ConnectionState::Disconnected) };
std::atomic<std::uint32_t> LastConnectResult{ 0xFFFFFFFFU };

// ---------------------------------------------------------------------------
// Config
// ---------------------------------------------------------------------------

constexpr std::size_t MaximumConfigBytes = 16'384;

// The default config, identical to celestialrayone.stance-dance.toml.
constexpr char DefaultConfigToml[] =
    "# Stance Dance\n"
    "#\n"
    "# Adds \"Stance Dance\" to Options > Controls, in its own \"Ascendancies\"\n"
    "# section, with no default key. Bind it there.\n"
    "#\n"
    "# What the key does\n"
    "#   While the character has the stance stat from its ascendancy (the\n"
    "#   ascendancy item gives 1), each press moves the stat to the next stance:\n"
    "#   1 -> 2 -> 3 -> 1. There is no key to go back.\n"
    "#   - Without the ascendancy the key does nothing.\n"
    "#   - The stat never drops to 0 and never goes above 3.\n"
    "#   - Taking the ascendancy item off removes the plugin's part at once, so a\n"
    "#     stance never outlives the ascendancy. With the item back on, the stance\n"
    "#     starts again at 1.\n"
    "#   - Holding the key down counts as one press.\n"
    "#\n"
    "# How the stat is built\n"
    "#   The ascendancy item gives 1. The plugin owns the character's own value of\n"
    "#   the same stat and keeps it at 0, 1 or 2, so the total is 1, 2 or 3. The\n"
    "#   host checks every player on every game update and corrects anything else\n"
    "#   that writes the character's own value of this stat.\n"
    "#\n"
    "# itemstatcost.txt row for stat_id\n"
    "#   - Saved = 1. Required. The game sends a change made on the character\n"
    "#     itself to that player's client only for rows with Saved = 1. Without it\n"
    "#     the stance changes on the host, but the client keeps seeing 1.\n"
    "#   - Save Bits above 0, so the ascendancy item keeps the stat when saved.\n"
    "#   - ValShift blank and no op, so the values are plain 1, 2 and 3.\n"
    "#   - CSvBits: with 0, every game starts at stance 1. Whether a value above 0\n"
    "#     keeps the stance between games in D2RLoader's extended save format is\n"
    "#     not confirmed.\n"
    "#   The plugin log reports the Saved check every time the tables load.\n"
    "#\n"
    "# Multiplayer\n"
    "#   Works in single player and TCP/IP. A key press goes to the host over\n"
    "#   D2RLoader's plugin network channel and the host makes the change, so the\n"
    "#   host and every client need this plugin.\n"
    "#\n"
    "# Console command\n"
    "#   stancedance   status, the itemstatcost check, counters, and the stance\n"
    "#                 of your client and of every player on the host.\n"
    "\n"
    "[stance_dance]\n"
    "\n"
    "# Master switch.\n"
    "enabled = true\n"
    "\n"
    "# itemstatcost.txt row of the stance stat.\n"
    "stat_id = 681\n";

struct Config {
    bool         enabled{ true };
    std::int32_t statId{ 681 };
};

Config      Settings;
std::string ConfigProblems;

void Problem(std::string_view text) {
    if (!ConfigProblems.empty()) ConfigProblems += "; ";
    ConfigProblems.append(text);
}

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

// Drops a trailing # comment that is not inside a quoted string.
auto StripComment(std::string_view line) noexcept -> std::string_view {
    bool quoted = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        if (line[i] == '"') quoted = !quoted;
        if (line[i] == '#' && !quoted) return line.substr(0, i);
    }
    return line;
}

auto ParseBool(std::string_view value, bool& out) noexcept -> bool {
    if (value == "true") { out = true; return true; }
    if (value == "false") { out = false; return true; }
    return false;
}

auto ParseInt(std::string_view value, std::int32_t minimum, std::int32_t maximum, std::int32_t& out) noexcept -> bool {
    if (value.empty()) return false;
    std::int64_t result   = 0;
    std::size_t  i        = 0;
    bool         negative = false;
    if (value[0] == '-' || value[0] == '+') {
        negative = value[0] == '-';
        i = 1;
    }
    if (i >= value.size()) return false;
    for (; i < value.size(); ++i) {
        if (value[i] == '_') continue;
        if (value[i] < '0' || value[i] > '9') return false;
        result = result * 10 + (value[i] - '0');
        if (result > 1'000'000'000) return false;
    }
    if (negative) result = -result;
    if (result < minimum || result > maximum) return false;
    out = static_cast<std::int32_t>(result);
    return true;
}

void ApplyConfigLine(std::string_view section, std::string_view key, std::string_view value) {
    if (section != "stance_dance") return;
    char note[160];
    const auto bad = [&](const char* what) {
        std::snprintf(note, sizeof(note), "%.*s: %s, default kept", static_cast<int>(key.size()), key.data(), what);
        Problem(note);
    };
    if (key == "enabled") {
        if (!ParseBool(value, Settings.enabled)) bad("expected true or false");
    } else if (key == "stat_id") {
        if (!ParseInt(value, 0, 32767, Settings.statId)) bad("expected 0 to 32767");
    }
}

void ParseConfig(std::string_view text) {
    std::string_view section;
    while (!text.empty()) {
        const std::size_t end = text.find('\n');
        const std::string_view line = Trim(StripComment(text.substr(0, end)));
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
        if (line.empty()) continue;
        if (line.front() == '[') {
            if (line.back() == ']') section = Trim(line.substr(1, line.size() - 2));
            continue;
        }
        const std::size_t equals = line.find('=');
        if (equals == std::string_view::npos) continue;
        ApplyConfigLine(section, Trim(line.substr(0, equals)), Trim(line.substr(equals + 1)));
    }
}

void ReadConfiguration() {
    if (!Context->EnsureConfig(DefaultConfigToml)) {
        Context->LogWarn("Stance Dance: the config file could not be created; using defaults.");
    } else {
        std::string buffer(MaximumConfigBytes, '\0');
        if (Context->ReadConfig(buffer.data(), static_cast<std::uint32_t>(buffer.size()), nullptr)) {
            buffer.resize(std::strlen(buffer.c_str()));
            ParseConfig(buffer);
        } else {
            Context->LogWarn("Stance Dance: the config file could not be read; using defaults.");
        }
    }
    if (!ConfigProblems.empty()) {
        D2RL::LogWarnF(Context, "Stance Dance: %s", ConfigProblems.c_str());
    }
}

// ---------------------------------------------------------------------------
// The stance stat on a unit
// ---------------------------------------------------------------------------

auto ReadStance(void* unit) noexcept -> stance::Reading {
    return { ReadStat(unit, Settings.statId, 0), ReadOwnStat(unit, Settings.statId, 0) };
}

void WritePart(void* unit, std::int64_t part) noexcept {
    WriteOwnStat(unit, Settings.statId, static_cast<std::int32_t>(part), 0);
}

auto LocalPlayer() noexcept -> void* {
    return PlayerFromIndex(LocalPlayerIndex());
}

// ---------------------------------------------------------------------------
// itemstatcost check, every time the tables load (game thread)
// ---------------------------------------------------------------------------

enum : std::uint32_t { RowNotChecked = 0, RowMissing = 1, RowUnsaved = 2, RowSaved = 3, RowUnreadable = 4 };

constexpr std::array<D2RL::DataTables::Bank, 3> Banks{
    D2RL::DataTables::Bank::Classic, D2RL::DataTables::Bank::Lod, D2RL::DataTables::Bank::Rotw };
constexpr std::array<const char*, 3> BankNames{ "classic", "lod", "rotw" };

// Per bank: row state | CSvBits << 8.
std::array<std::atomic<std::uint32_t>, 3> RowStates{};

auto CheckRow(const D2RL::PluginContext* context, D2RL::DataTables::Bank bank) noexcept -> std::uint32_t {
    D2RL::DataTables::RowView row{};
    row.structSize = D2RL::DataTables::RowViewSize;
    const D2RL::DataTables::Result result = TableSvc->getRow(context, bank, D2RL::DataTables::TableId::ItemStatCost,
        static_cast<std::uint32_t>(Settings.statId), &row);
    if (result == D2RL::DataTables::Result::NotFound || result == D2RL::DataTables::Result::InvalidArgument) {
        return RowMissing;
    }
    if (result != D2RL::DataTables::Result::Success
            || !D2RL::DataTables::HasRowViewField(&row, D2RL::DataTables::RowViewRequiredSize)
            || row.row == nullptr || row.rowSize <= IscCsvBitsOffset) {
        return RowUnreadable;
    }
    const std::uint32_t flags   = Read<std::uint32_t>(row.row, IscFlagsOffset);
    const std::uint32_t csvBits = Read<std::uint8_t>(row.row, IscCsvBitsOffset);
    return ((flags & IscFlagSaved) != 0 ? RowSaved : RowUnsaved) | (csvBits << 8);
}

void DescribeRows(char* out, std::size_t size) noexcept {
    out[0] = '\0';
    std::size_t used = 0;
    for (std::size_t i = 0; i < Banks.size() && used < size; ++i) {
        const std::uint32_t value   = RowStates[i].load();
        const unsigned      csvBits = (value >> 8) & 0xFFU;
        const char*         comma   = i == 0 ? "" : ", ";
        int written = 0;
        switch (value & 0xFFU) {
        case RowMissing:
            written = std::snprintf(out + used, size - used, "%s%s no row", comma, BankNames[i]);
            break;
        case RowUnsaved:
            written = std::snprintf(out + used, size - used, "%s%s Saved 0 (CSvBits %u)", comma, BankNames[i], csvBits);
            break;
        case RowSaved:
            written = std::snprintf(out + used, size - used, "%s%s Saved 1 (CSvBits %u)", comma, BankNames[i], csvBits);
            break;
        case RowUnreadable:
            written = std::snprintf(out + used, size - used, "%s%s unreadable", comma, BankNames[i]);
            break;
        default:
            written = std::snprintf(out + used, size - used, "%s%s not checked yet", comma, BankNames[i]);
            break;
        }
        if (written < 0) break;
        used += static_cast<std::size_t>(written);
    }
}

void __cdecl OnDataTablesLoaded(const D2RL::PluginContext* context, const D2RL::Lifecycle::DataTablesLoadedEvent*,
        void*) noexcept {
    if (context == nullptr || TableSvc == nullptr) return;
    bool anyRow     = false;
    bool anyUnsaved = false;
    for (std::size_t i = 0; i < Banks.size(); ++i) {
        const std::uint32_t value = CheckRow(context, Banks[i]);
        RowStates[i].store(value);
        const std::uint32_t state = value & 0xFFU;
        anyRow     = anyRow || state == RowSaved || state == RowUnsaved;
        anyUnsaved = anyUnsaved || state == RowUnsaved;
    }
    char rows[256];
    DescribeRows(rows, sizeof(rows));
    if (!anyRow) {
        D2RL::LogErrorF(context, "Stance Dance: itemstatcost.txt has no row %d (%s). The key does nothing until the "
            "row exists.", Settings.statId, rows);
    } else if (anyUnsaved) {
        D2RL::LogWarnF(context, "Stance Dance: itemstatcost.txt row %d has Saved = 0 (%s). The host changes the "
            "stance, but the game sends a change made on the character itself to that player's client only for "
            "rows with Saved = 1, so the client keeps seeing the ascendancy's 1.", Settings.statId, rows);
    } else {
        D2RL::LogInfoF(context, "Stance Dance: itemstatcost.txt row %d checked (%s).", Settings.statId, rows);
    }
}

// ---------------------------------------------------------------------------
// Host: players, key presses and the legal part (game thread)
// ---------------------------------------------------------------------------

struct PlayerRef {
    std::uint32_t id;
    void*         unit;
};

constexpr std::size_t MaxPlayers        = 16;
constexpr std::size_t MaxClientsVisited = 256;

// Every player of the local server: each client in the registry, its player
// resolved through the game's own lookup (never a stored pointer).
auto CollectPlayers(std::array<PlayerRef, MaxPlayers>& players) noexcept -> std::size_t {
    if (Read<std::int32_t>(reinterpret_cast<const void*>(Base + RegistryReadyRva), 0) == 0) return 0;
    const auto* const buckets = reinterpret_cast<const std::uint8_t* const*>(Base + RegistryRva);
    std::size_t count   = 0;
    std::size_t visited = 0;
    for (std::size_t bucket = 0; bucket < RegistryBuckets; ++bucket) {
        for (const std::uint8_t* client = buckets[bucket]; client != nullptr;
                client = Read<const std::uint8_t*>(client, ClientNextOffset)) {
            if (++visited > MaxClientsVisited) return count;
            void* const game = Read<void*>(client, ClientGameOffset);
            if (game == nullptr || Read<std::uint32_t>(client, ClientUnitTypeOffset) != UnitTypePlayer) continue;
            const std::uint32_t id   = Read<std::uint32_t>(client, ClientUnitIdOffset);
            void* const         unit = ServerUnit(game, UnitTypePlayer, id);
            if (unit == nullptr || UnitType(unit) != UnitTypePlayer || count >= players.size()) continue;
            players[count++] = { id, unit };
        }
    }
    return count;
}

auto FindPlayer(const std::array<PlayerRef, MaxPlayers>& players, std::size_t count, std::uint32_t id) noexcept
        -> void* {
    for (std::size_t i = 0; i < count; ++i) {
        if (players[i].id == id) return players[i].unit;
    }
    return nullptr;
}

// At most 20 correction lines per 10 seconds, in case something keeps
// writing the character's own value of the stat.
std::uint64_t HostLogWindow{};
std::uint32_t HostLogLines{};

auto HostLogAllowed() noexcept -> bool {
    const std::uint64_t window = GetTickCount64() / 10'000;
    if (window != HostLogWindow) {
        HostLogWindow = window;
        HostLogLines  = 0;
    }
    return ++HostLogLines <= 20;
}

// One key press for this player.
void Step(const D2RL::PluginContext* context, void* unit, std::uint32_t playerId) noexcept {
    const stance::Reading before = ReadStance(unit);
    if (!stance::Unlocked(before)) {
        PressesLocked.fetch_add(1);
        D2RL::LogInfoF(context, "Stance Dance: player %u pressed the key without the ascendancy (stat %d is %lld); "
            "nothing changed.", playerId, Settings.statId, static_cast<long long>(before.total));
        return;
    }
    const std::int64_t part = stance::NextPart(before);
    if (part != before.base) WritePart(unit, part);
    const stance::Reading after = ReadStance(unit);
    StanceChanges.fetch_add(1);
    D2RL::LogInfoF(context, "Stance Dance: player %u stance %lld -> %lld (ascendancy %lld + plugin %lld).", playerId,
        static_cast<long long>(before.total), static_cast<long long>(after.total),
        static_cast<long long>(stance::Ascendancy(after)), static_cast<long long>(after.base));
}

// Keeps this player's part legal: none without the ascendancy, otherwise
// within 0 .. 3 - ascendancy.
void Keep(const D2RL::PluginContext* context, void* unit, std::uint32_t playerId) noexcept {
    const stance::Reading now     = ReadStance(unit);
    const std::int64_t    allowed = stance::AllowedPart(now);
    if (allowed == now.base) return;
    WritePart(unit, allowed);
    const bool unlocked = stance::Unlocked(now);
    (unlocked ? PartsCorrected : PartsRemoved).fetch_add(1);
    if (!HostLogAllowed()) return;
    if (unlocked) {
        D2RL::LogInfoF(context, "Stance Dance: player %u had plugin part %lld, outside 0 to %lld; set to %lld.",
            playerId, static_cast<long long>(now.base),
            static_cast<long long>(stance::MaxPart(stance::Ascendancy(now))), static_cast<long long>(allowed));
    } else {
        D2RL::LogInfoF(context, "Stance Dance: player %u no longer has the ascendancy; plugin part %lld removed.",
            playerId, static_cast<long long>(now.base));
    }
}

// Key presses that reached the host, applied on its next run.
struct Request {
    D2RL::Network::PeerHandle peer;
    std::uint32_t             playerId;
};

constexpr std::size_t            MaxRequests = 64;
std::mutex                       RequestMutex;
std::array<Request, MaxRequests> RequestQueue{};
std::size_t                      RequestCount{};

// What the last host run saw, for the console command.
struct PlayerView {
    std::uint32_t id;
    std::int64_t  total;
    std::int64_t  base;
};

std::mutex                          ViewMutex;
std::array<PlayerView, MaxPlayers>  Views{};
std::size_t                         ViewCount{};

void ClearHostState() noexcept {
    {
        std::lock_guard lock(RequestMutex);
        RequestCount = 0;
    }
    std::lock_guard lock(ViewMutex);
    ViewCount = 0;
}

void HostPass(const D2RL::PluginContext* context) noexcept {
    std::array<PlayerRef, MaxPlayers> players{};
    const std::size_t count = CollectPlayers(players);

    std::array<Request, MaxRequests> requests{};
    std::size_t                      requestCount = 0;
    {
        std::lock_guard lock(RequestMutex);
        requestCount = RequestCount;
        std::copy_n(RequestQueue.begin(), requestCount, requests.begin());
        RequestCount = 0;
    }
    for (std::size_t i = 0; i < requestCount; ++i) {
        D2RL::PlayerHandle sender = D2RL::InvalidPlayerHandle;
        if (NetworkSvc->getPeerPlayer(context, Channel, requests[i].peer, &sender) != D2RL::Network::Result::Success
                || sender == D2RL::InvalidPlayerHandle) {
            RequestsRefused.fetch_add(1);
            continue;
        }
        void* const unit = FindPlayer(players, count, requests[i].playerId);
        if (unit == nullptr) {
            RequestsUnknown.fetch_add(1);
            continue;
        }
        Step(context, unit, requests[i].playerId);
    }

    for (std::size_t i = 0; i < count; ++i) Keep(context, players[i].unit, players[i].id);

    std::array<PlayerView, MaxPlayers> views{};
    for (std::size_t i = 0; i < count; ++i) {
        const stance::Reading reading = ReadStance(players[i].unit);
        views[i] = { players[i].id, reading.total, reading.base };
    }
    std::lock_guard lock(ViewMutex);
    Views     = views;
    ViewCount = count;
}

// ---------------------------------------------------------------------------
// Host run scheduling
// ---------------------------------------------------------------------------
//   HostTick runs on the game thread, then queues RearmTick on the UI thread,
//   which queues the next HostTick. Only the chain whose id is LiveChain
//   carries on, so starting a new chain retires the old one.

std::atomic<std::uint64_t> LiveChain{};
std::atomic<std::uint64_t> ChainCounter{};
std::atomic<std::uint64_t> ChainStartedMs{};
std::atomic<std::uint64_t> LastRunMs{};
constexpr std::uint64_t    StaleChainMs = 2000;

void __cdecl HostTick(const D2RL::PluginContext* context, void* userData) noexcept;
void __cdecl RearmTick(const D2RL::PluginContext* context, void* userData) noexcept;

auto ChainOf(void* userData) noexcept -> std::uint64_t {
    return static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(userData));
}

auto ChainData(std::uint64_t id) noexcept -> void* {
    return reinterpret_cast<void*>(static_cast<std::uintptr_t>(id));
}

void StopChain(std::uint64_t id) noexcept {
    std::uint64_t expected = id;
    (void)LiveChain.compare_exchange_strong(expected, 0);
}

void StartChain(const D2RL::PluginContext* context) noexcept {
    const std::uint64_t id = ChainCounter.fetch_add(1) + 1;
    LiveChain.store(id);
    ChainStartedMs.store(GetTickCount64());
    if (ThreadSvc->runOnGameThread(context, &HostTick, ChainData(id)) != D2RL::Threads::Result::Success) {
        StopChain(id);  // no local game here (a TCP/IP client, or not in a game)
    }
}

// Starts a chain when none is running, or when the running one has stopped
// arriving (its game task was dropped with the old game session).
void EnsureChain(const D2RL::PluginContext* context) noexcept {
    const std::uint64_t now = GetTickCount64();
    if (LiveChain.load() != 0
            && (now - LastRunMs.load() <= StaleChainMs || now - ChainStartedMs.load() <= StaleChainMs)) {
        return;
    }
    StartChain(context);
}

auto HostRunning() noexcept -> bool {
    return LiveChain.load() != 0 && GetTickCount64() - LastRunMs.load() <= StaleChainMs;
}

void __cdecl HostTick(const D2RL::PluginContext* context, void* userData) noexcept {
    const std::uint64_t id = ChainOf(userData);
    if (context == nullptr || LiveChain.load() != id) return;
    if (Active.load()) HostPass(context);
    LastRunMs.store(GetTickCount64());
    if (ThreadSvc->runOnUiThread(context, &RearmTick, userData) != D2RL::Threads::Result::Success) StopChain(id);
}

void __cdecl RearmTick(const D2RL::PluginContext* context, void* userData) noexcept {
    const std::uint64_t id = ChainOf(userData);
    if (context == nullptr || LiveChain.load() != id) return;
    if (ThreadSvc->runOnGameThread(context, &HostTick, userData) != D2RL::Threads::Result::Success) StopChain(id);
}

// ---------------------------------------------------------------------------
// Network channel
// ---------------------------------------------------------------------------

void __cdecl OnHostMessage(const D2RL::PluginContext* context, D2RL::Network::ChannelHandle,
        D2RL::Network::PeerHandle peer, std::uint16_t messageId, const void* data, std::uint32_t size,
        void*) noexcept {
    if (context == nullptr || !Active.load()) return;
    if (messageId != StepMessageId || data == nullptr || size != sizeof(StepMessage)) {
        RequestsRefused.fetch_add(1);
        return;
    }
    StepMessage message{};
    std::memcpy(&message, data, sizeof(message));
    bool queued = false;
    {
        std::lock_guard lock(RequestMutex);
        if (RequestCount < RequestQueue.size()) {
            RequestQueue[RequestCount++] = { peer, message.playerId };
            queued = true;
        }
    }
    if (!queued) {
        RequestsRefused.fetch_add(1);
        return;
    }
    RequestsReceived.fetch_add(1);
    EnsureChain(context);
}

// The host never sends anything back; the stat itself reaches the client.
void __cdecl OnClientMessage(const D2RL::PluginContext*, D2RL::Network::ChannelHandle, std::uint16_t, const void*,
        std::uint32_t, void*) noexcept {}

auto Send(const D2RL::PluginContext* context, std::uint32_t playerId) noexcept -> bool {
    const StepMessage message{ playerId };
    return NetworkSvc->sendToHost(context, Channel, StepMessageId, &message, sizeof(message))
        == D2RL::Network::Result::Success;
}

void Connect(const D2RL::PluginContext* context) noexcept {
    LastConnectResult.store(static_cast<std::uint32_t>(NetworkSvc->connectToHost(context, Channel)));
}

// Presses made before the channel was up.
struct Waiting {
    std::uint32_t playerId{};
    std::uint32_t count{};
    std::uint64_t lastMs{};
};

constexpr std::uint32_t MaxWaitingPresses = 8;
constexpr std::uint64_t WaitingLifetimeMs = 5000;
std::mutex              WaitingMutex;
Waiting                 WaitingPresses{};

void ClearWaiting() noexcept {
    std::lock_guard lock(WaitingMutex);
    WaitingPresses = {};
}

void FlushWaiting(const D2RL::PluginContext* context) noexcept {
    Waiting taken{};
    {
        std::lock_guard lock(WaitingMutex);
        taken          = WaitingPresses;
        WaitingPresses = {};
    }
    if (taken.count == 0) return;
    if (GetTickCount64() - taken.lastMs > WaitingLifetimeMs) {
        PressesDropped.fetch_add(taken.count);
        return;
    }
    for (std::uint32_t i = 0; i < taken.count; ++i) {
        (Send(context, taken.playerId) ? PressesSent : PressesDropped).fetch_add(1);
    }
}

void __cdecl OnConnectionState(const D2RL::PluginContext* context, const D2RL::Network::ConnectionEvent* event,
        void*) noexcept {
    if (context == nullptr
            || !D2RL::Network::HasConnectionEventField(event, D2RL::Network::ConnectionEventRequiredSize)) {
        return;
    }
    ConnectionNow.store(static_cast<std::uint32_t>(event->state));
    if (event->state == D2RL::Network::ConnectionState::Rejected) {
        D2RL::LogWarnF(context, "Stance Dance: the host refused the plugin channel (reason %u); key presses cannot "
            "reach it. The host needs this plugin, the same version.", static_cast<unsigned>(event->reason));
        return;
    }
    if (event->state == D2RL::Network::ConnectionState::Connected) FlushWaiting(context);
}

// ---------------------------------------------------------------------------
// The Controls key
// ---------------------------------------------------------------------------

// A binding stays held from its press to its release; presses in between are
// repeats of the same press.
std::mutex                   KeyMutex;
std::array<std::uint32_t, 2> HeldKeys{};

auto PackKey(const D2RL::Input::Binding& binding) noexcept -> std::uint32_t {
    return static_cast<std::uint32_t>(binding.key) | (static_cast<std::uint32_t>(binding.modifier) << 16);
}

// True for a new press, false while the same binding is still held.
auto HoldKey(std::uint32_t key) noexcept -> bool {
    std::lock_guard lock(KeyMutex);
    for (const std::uint32_t held : HeldKeys) {
        if (held == key) return false;
    }
    for (std::uint32_t& held : HeldKeys) {
        if (held == 0) {
            held = key;
            return true;
        }
    }
    HeldKeys[0] = key;  // both bindings held: take the older slot
    return true;
}

auto ReleaseKey(std::uint32_t key) noexcept -> bool {
    std::lock_guard lock(KeyMutex);
    for (std::uint32_t& held : HeldKeys) {
        if (held == key) {
            held = 0;
            return true;
        }
    }
    return false;
}

void ReleaseAllKeys() noexcept {
    std::lock_guard lock(KeyMutex);
    HeldKeys = {};
}

// UI thread: sends this press to the host, or keeps it until the channel is up.
void __cdecl SendStep(const D2RL::PluginContext* context, void*) noexcept {
    if (context == nullptr || !Active.load()) return;
    void* const player = LocalPlayer();
    if (player == nullptr) {
        PressesDropped.fetch_add(1);
        return;
    }
    const std::uint32_t playerId = UnitId(player);
    if (Send(context, playerId)) {
        PressesSent.fetch_add(1);
        return;
    }
    {
        std::lock_guard lock(WaitingMutex);
        if (WaitingPresses.count == 0 || WaitingPresses.playerId != playerId) WaitingPresses = { playerId, 0, 0 };
        if (WaitingPresses.count < MaxWaitingPresses) {
            ++WaitingPresses.count;
        } else {
            PressesDropped.fetch_add(1);
        }
        WaitingPresses.lastMs = GetTickCount64();
    }
    Connect(context);
}

auto __cdecl OnStanceKey(const D2RL::PluginContext* context, const D2RL::Input::ActionEvent* event, void*) noexcept
        -> D2RL::Input::ActionResult {
    if (context == nullptr || !Active.load()
            || !D2RL::Input::HasActionEventField(event, D2RL::Input::ActionEventRequiredSize)
            || event->action != Action.load()) {
        return D2RL::Input::ActionResult::Ignored;
    }
    const std::uint32_t key = PackKey(event->binding);
    if (key == 0) return D2RL::Input::ActionResult::Ignored;
    if (event->kind == D2RL::Input::ActionEventKind::Released) {
        return ReleaseKey(key) ? D2RL::Input::ActionResult::Handled : D2RL::Input::ActionResult::Ignored;
    }
    if (event->kind != D2RL::Input::ActionEventKind::Pressed) return D2RL::Input::ActionResult::Ignored;
    if (!HoldKey(key)) {
        RepeatsIgnored.fetch_add(1);
        return D2RL::Input::ActionResult::Handled;
    }
    if (ThreadSvc->runOnUiThread(context, &SendStep, nullptr) != D2RL::Threads::Result::Success) {
        (void)ReleaseKey(key);
        PressesDropped.fetch_add(1);
        return D2RL::Input::ActionResult::Ignored;
    }
    KeyPresses.fetch_add(1);
    return D2RL::Input::ActionResult::Handled;
}

// ---------------------------------------------------------------------------
// Game join and leave (UI thread)
// ---------------------------------------------------------------------------

void __cdecl OnGameJoined(const D2RL::PluginContext* context, const D2RL::Lifecycle::GameplayEvent*, void*) noexcept {
    ReleaseAllKeys();
    ClearWaiting();
    ClearHostState();
    ConnectionNow.store(static_cast<std::uint32_t>(D2RL::Network::ConnectionState::Disconnected));
    if (context != nullptr && Active.load()) StartChain(context);
}

void __cdecl OnLocalPlayerReady(const D2RL::PluginContext* context, const D2RL::Lifecycle::GameplayEvent*,
        void*) noexcept {
    if (context == nullptr || !Active.load()) return;
    Connect(context);
    StartChain(context);
}

void __cdecl OnGameLeft(const D2RL::PluginContext*, const D2RL::Lifecycle::GameplayEvent*, void*) noexcept {
    LiveChain.store(0);
    ReleaseAllKeys();
    ClearWaiting();
    ClearHostState();
    ConnectionNow.store(static_cast<std::uint32_t>(D2RL::Network::ConnectionState::Disconnected));
}

// ---------------------------------------------------------------------------
// Console
// ---------------------------------------------------------------------------

auto StateName(PluginState state) noexcept -> const char* {
    switch (state) {
    case PluginState::NotLoaded:        return "not loaded";
    case PluginState::DisabledByConfig: return "disabled by config";
    case PluginState::UnsupportedBuild: return "unsupported build, nothing added";
    case PluginState::ServicesMissing:  return "a D2RLoader service is missing, nothing added";
    case PluginState::InstallFailed:    return "registration failed, off";
    case PluginState::Active:           return "active";
    }
    return "?";
}

auto ConnectionName(std::uint32_t state) noexcept -> const char* {
    switch (static_cast<D2RL::Network::ConnectionState>(state)) {
    case D2RL::Network::ConnectionState::Disconnected: return "not connected";
    case D2RL::Network::ConnectionState::Connecting:   return "connecting";
    case D2RL::Network::ConnectionState::Connected:    return "connected";
    case D2RL::Network::ConnectionState::Rejected:     return "refused by the host";
    }
    return "?";
}

auto Count(const std::atomic<std::uint64_t>& counter) noexcept -> unsigned long long {
    return static_cast<unsigned long long>(counter.load());
}

auto __cdecl StatusCommand(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command, void*) noexcept
        -> D2RL::ConsoleCommandResult {
    if (command == nullptr || command->plugin == nullptr) return D2RL::ConsoleCommandResult::Failed;
    char       line[400];
    const auto say = [&](const char* text) { command->plugin->WriteConsoleMessage(text); };

    std::snprintf(line, sizeof(line), "Stance Dance %s: %s. Stat %d, stances 1 to 3.", PluginVersion,
        StateName(State), Settings.statId);
    say(line);
    if (!Active.load()) return D2RL::ConsoleCommandResult::Handled;

    char rows[256];
    DescribeRows(rows, sizeof(rows));
    std::snprintf(line, sizeof(line), "  itemstatcost row %d: %s.", Settings.statId, rows);
    say(line);

    std::uint32_t waiting = 0;
    {
        std::lock_guard lock(WaitingMutex);
        waiting = WaitingPresses.count;
    }
    std::snprintf(line, sizeof(line), "  channel: %s (last connect result %u). Host runs: %s.",
        ConnectionName(ConnectionNow.load()), LastConnectResult.load(),
        HostRunning() ? "every game update" : "none here (a TCP/IP client, or no game)");
    say(line);
    std::snprintf(line, sizeof(line), "  key: %llu presses, %llu sent, %u waiting for the channel, %llu dropped, "
        "%llu held-key repeats ignored.", Count(KeyPresses), Count(PressesSent), waiting, Count(PressesDropped),
        Count(RepeatsIgnored));
    say(line);
    std::snprintf(line, sizeof(line), "  host: %llu presses received, %llu stance changes, %llu without the "
        "ascendancy, %llu for unknown players, %llu refused; plugin part removed %llu times, corrected %llu times.",
        Count(RequestsReceived), Count(StanceChanges), Count(PressesLocked), Count(RequestsUnknown),
        Count(RequestsRefused), Count(PartsRemoved), Count(PartsCorrected));
    say(line);

    if (void* const local = LocalPlayer(); local != nullptr) {
        const stance::Reading reading = ReadStance(local);
        std::snprintf(line, sizeof(line), "  your client sees stance %lld (ascendancy %lld + plugin %lld).",
            static_cast<long long>(reading.total), static_cast<long long>(stance::Ascendancy(reading)),
            static_cast<long long>(reading.base));
        say(line);
    }
    std::lock_guard lock(ViewMutex);
    for (std::size_t i = 0; i < ViewCount; ++i) {
        const stance::Reading reading{ Views[i].total, Views[i].base };
        std::snprintf(line, sizeof(line), "  host, player %u: stance %lld (ascendancy %lld + plugin %lld).",
            Views[i].id, static_cast<long long>(reading.total), static_cast<long long>(stance::Ascendancy(reading)),
            static_cast<long long>(reading.base));
        say(line);
    }
    return D2RL::ConsoleCommandResult::Handled;
}

// ---------------------------------------------------------------------------
// Load
// ---------------------------------------------------------------------------

auto VerifyAll() noexcept -> bool {
    bool ok = true;
    for (const Witness& witness : Witnesses) {
        if (!Context->CheckExpectedBytes(witness.rva, witness.bytes, witness.size)) {
            D2RL::LogErrorF(Context, "Stance Dance: %s at RVA 0x%llX does not match this build.", witness.name,
                static_cast<unsigned long long>(witness.rva));
            ok = false;
        }
    }
    return ok;
}

void BindNatives() noexcept {
    ReadStat         = At<ReadStatFn>(ReadStatRva);
    ReadOwnStat      = At<ReadStatFn>(ReadOwnStatRva);
    WriteOwnStat     = At<WriteStatFn>(WriteOwnStatRva);
    UnitType         = At<UnitTypeFn>(UnitTypeRva);
    UnitId           = At<UnitIdFn>(UnitIdRva);
    LocalPlayerIndex = At<PlayerIndexFn>(LocalPlayerIndexRva);
    PlayerFromIndex  = At<PlayerFromFn>(PlayerFromIndexRva);
    ServerUnit       = At<ServerUnitFn>(ServerUnitRva);
}

auto QueryServices() noexcept -> bool {
    if (Context->QueryService(&InputSvc) != D2RL::ServiceQueryResult::Success
            || !D2RL::HasInputServiceField(InputSvc, D2RL::InputServiceRequiredSize)) {
        Context->LogError("Stance Dance: the Controls (input) service is unavailable.");
        return false;
    }
    if (Context->QueryService(&NetworkSvc) != D2RL::ServiceQueryResult::Success
            || !D2RL::HasNetworkServiceField(NetworkSvc, D2RL::NetworkServiceRequiredSize)) {
        Context->LogError("Stance Dance: the network service is unavailable.");
        return false;
    }
    if (Context->QueryService(&ThreadSvc) != D2RL::ServiceQueryResult::Success
            || !D2RL::HasThreadServiceField(ThreadSvc, D2RL::ThreadServiceRequiredSize)) {
        Context->LogError("Stance Dance: the thread service is unavailable.");
        return false;
    }
    if (Context->QueryService(&LifecycleSvc) != D2RL::ServiceQueryResult::Success
            || !D2RL::HasLifecycleServiceField(LifecycleSvc, D2RL::LifecycleServiceRequiredSize)) {
        Context->LogError("Stance Dance: the lifecycle service is unavailable.");
        return false;
    }
    if (Context->QueryService(&TableSvc) != D2RL::ServiceQueryResult::Success
            || !D2RL::HasDataTableServiceField(TableSvc, D2RL::DataTableServiceRequiredSize)) {
        TableSvc = nullptr;
        Context->LogWarn("Stance Dance: the data table service is unavailable; the itemstatcost check is off.");
    }
    return true;
}

auto RegisterListeners() noexcept -> bool {
    if (TableSvc != nullptr) {
        const D2RL::Lifecycle::DataTablesLoadedListener tables{
            .structSize = D2RL::Lifecycle::DataTablesLoadedListenerSize,
            .callback   = &OnDataTablesLoaded,
        };
        D2RL::Lifecycle::ListenerHandle handle = D2RL::Lifecycle::InvalidHandle;
        if (LifecycleSvc->registerDataTablesLoadedListener(Context, &tables, &handle)
                != D2RL::Lifecycle::Result::Success) {
            Context->LogWarn("Stance Dance: the table listener was refused; the itemstatcost check is off.");
        }
    }
    struct Game {
        D2RL::Lifecycle::GameplayEventKind     kind;
        D2RL::Lifecycle::GameplayEventCallback callback;
    };
    const std::array<Game, 3> games{ {
        { D2RL::Lifecycle::GameplayEventKind::GameJoined, &OnGameJoined },
        { D2RL::Lifecycle::GameplayEventKind::LocalPlayerReady, &OnLocalPlayerReady },
        { D2RL::Lifecycle::GameplayEventKind::GameLeft, &OnGameLeft },
    } };
    for (const Game& game : games) {
        const D2RL::Lifecycle::GameplayEventListener listener{
            .structSize = D2RL::Lifecycle::GameplayEventListenerSize,
            .kind       = game.kind,
            .callback   = game.callback,
        };
        D2RL::Lifecycle::ListenerHandle handle = D2RL::Lifecycle::InvalidHandle;
        if (LifecycleSvc->registerGameplayEventListener(Context, &listener, &handle)
                != D2RL::Lifecycle::Result::Success) {
            Context->LogError("Stance Dance: a game join/leave listener was refused.");
            return false;
        }
    }
    return true;
}

auto RegisterChannel() noexcept -> bool {
    const D2RL::Network::ChannelRegistration registration{
        .structSize         = D2RL::Network::ChannelRegistrationSize,
        .localChannelId     = ChannelLocalId,
        .compatibilityToken = CompatibilityToken,
        .hostMessage        = &OnHostMessage,
        .clientMessage      = &OnClientMessage,
        .connectionState    = &OnConnectionState,
    };
    const D2RL::Network::Result result = NetworkSvc->registerChannel(Context, &registration, &Channel);
    if (result != D2RL::Network::Result::Success || Channel == D2RL::Network::InvalidChannelHandle) {
        D2RL::LogErrorF(Context, "Stance Dance: the network channel was refused (result %u).",
            static_cast<unsigned>(result));
        return false;
    }
    return true;
}

auto RegisterAction() noexcept -> bool {
    const D2RL::Input::ActionRegistration registration{
        .structSize       = D2RL::Input::ActionRegistrationSize,
        .flags            = 0,
        .logicalId        = "stance-dance",
        .displayName      = "Stance Dance",
        .category         = "Ascendancies",
        .defaultPrimary   = { .key = D2RL::Input::Key::None, .modifier = D2RL::Input::Modifier::None },
        .defaultSecondary = { .key = D2RL::Input::Key::None, .modifier = D2RL::Input::Modifier::None },
        .callback         = &OnStanceKey,
        .userData         = nullptr,
    };
    D2RL::Input::ActionHandle handle = D2RL::Input::InvalidHandle;
    const D2RL::Input::Result result = InputSvc->registerAction(Context, &registration, &handle);
    if (result != D2RL::Input::Result::Success || handle == D2RL::Input::InvalidHandle) {
        D2RL::LogErrorF(Context, "Stance Dance: the Controls entry was refused (result %u).",
            static_cast<unsigned>(result));
        return false;
    }
    Action.store(handle);
    return true;
}

constexpr D2RL::PluginInfo PluginInfoData{
    .infoSize    = D2RL::PluginInfoSize,
    .abiVersion  = D2RL_PLUGIN_ABI_VERSION,
    .id          = PluginIdText,
    .name        = "Stance Dance",
    .version     = PluginVersion,
    .author      = "CelestialRayOne",
    .description = "Stance Dance key in Controls (Ascendancies): cycles the ascendancy stance stat 1, 2, 3.",
    .flags       = D2RL::PluginFlags::Shared,
};

} // namespace

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* {
    return &PluginInfoData;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
    Context = context;
    if (Context == nullptr || Context->exeBase == 0) return false;
    Base = Context->exeBase;

    if (!Context->RegisterConsoleCommand("stancedance", &StatusCommand,
            "Stance Dance status: the itemstatcost check, counters and every player's stance.")) {
        Context->LogWarn("Stance Dance: the console command could not be registered.");
    }

    ReadConfiguration();
    if (!Settings.enabled) {
        State = PluginState::DisabledByConfig;
        Context->LogInfo("Stance Dance: disabled by config.");
        return true;
    }
    if (!VerifyAll()) {
        State = PluginState::UnsupportedBuild;
        Context->LogError("Stance Dance: this D2R build does not match; the key was not added.");
        return true;
    }
    BindNatives();
    if (!QueryServices()) {
        State = PluginState::ServicesMissing;
        return true;
    }
    // The Controls entry goes last, so it only appears when everything behind it works.
    if (!RegisterListeners() || !RegisterChannel() || !RegisterAction()) {
        State = PluginState::InstallFailed;
        return true;
    }
    Active.store(true);
    State = PluginState::Active;
    D2RL::LogInfoF(Context, "Stance Dance %s: active, stat %d. Bind the key in Options > Controls > Ascendancies.",
        PluginVersion, Settings.statId);
    return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
    Active.store(false);
    LiveChain.store(0);
}
